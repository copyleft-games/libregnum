/* lrg-particle-system.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-particle-system.h"

/**
 * SECTION:lrg-particle-system
 * @Title: LrgParticleSystem
 * @Short_description: Complete particle effect system
 *
 * #LrgParticleSystem is the main class for creating particle effects.
 * It combines:
 *
 * - #LrgParticlePool for memory management
 * - #LrgParticleEmitter for spawning particles
 * - #LrgParticleForce for physics simulation
 *
 * Basic usage:
 * |[<!-- language="C" -->
 * LrgParticleSystem *system = lrg_particle_system_new (1000);
 *
 * LrgParticleEmitter *emitter = lrg_particle_emitter_new ();
 * lrg_particle_emitter_set_emission_rate (emitter, 50.0f);
 * lrg_particle_system_add_emitter (system, emitter);
 *
 * LrgParticleForce *gravity = lrg_particle_force_gravity_new (0, -9.81f, 0);
 * lrg_particle_system_add_force (system, gravity);
 *
 * lrg_particle_system_play (system);
 *
 * // In game loop:
 * lrg_particle_system_update (system, delta_time);
 * lrg_particle_system_draw (system);
 * ]|
 */

typedef struct
{
    /* Particle storage */
    LrgParticlePool     *pool;
    guint                max_particles;     /* construct-only capacity */
    GPtrArray           *active_particles;  /* LrgParticle *, oldest first */
    guint                active_count;

    /* Components */
    GList               *emitters;         /* List of LrgParticleEmitter */
    GList               *forces;           /* List of LrgParticleForce */

    /* State */
    gboolean             playing;
    gboolean             loop;
    gfloat               elapsed_time;
    gfloat               duration;
    gfloat               time_scale;

    /* Rendering */
    LrgParticleRenderMode render_mode;
    LrgParticleBlendMode  blend_mode;

    /* World transform */
    gfloat               position_x;
    gfloat               position_y;
    gfloat               position_z;
} LrgParticleSystemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgParticleSystem, lrg_particle_system, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MAX_PARTICLES,
    PROP_ACTIVE_COUNT,
    PROP_PLAYING,
    PROP_LOOP,
    PROP_DURATION,
    PROP_TIME_SCALE,
    PROP_RENDER_MODE,
    PROP_BLEND_MODE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Drops the NULL holes left by released particles, keeping order. */
static void
active_compact (GPtrArray *active)
{
    guint read;
    guint write;

    write = 0;
    for (read = 0; read < active->len; read++)
    {
        gpointer p = g_ptr_array_index (active, read);

        if (p != NULL)
            g_ptr_array_index (active, write++) = p;
    }
    g_ptr_array_set_size (active, write);
}

static void
lrg_particle_system_real_update (LrgParticleSystem *self,
                                 gfloat             delta_time)
{
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
    LrgParticleSystemClass   *klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);
    GList *l;
    gfloat scaled_dt;
    guint  i;
    guint  removed;

    if (!priv->playing)
        return;

    scaled_dt = delta_time * priv->time_scale;
    priv->elapsed_time += scaled_dt;

    /* Check duration */
    if (priv->duration > 0.0f && priv->elapsed_time >= priv->duration)
    {
        if (priv->loop)
        {
            priv->elapsed_time = 0.0f;
        }
        else
        {
            /* Disable emitters but let existing particles finish */
            for (l = priv->emitters; l != NULL; l = l->next)
            {
                LrgParticleEmitter *emitter = LRG_PARTICLE_EMITTER (l->data);
                lrg_particle_emitter_set_enabled (emitter, FALSE);
            }
        }
    }

    /* Update emitters */
    for (l = priv->emitters; l != NULL; l = l->next)
    {
        LrgParticleEmitter *emitter = LRG_PARTICLE_EMITTER (l->data);
        lrg_particle_emitter_update (emitter, scaled_dt);

        /* Emit particles based on rate */
        while (lrg_particle_emitter_should_emit (emitter))
        {
            LrgParticle *particle;

            particle = lrg_particle_pool_acquire (priv->pool);
            if (particle == NULL)
                break;  /* Pool exhausted */

            lrg_particle_emitter_emit (emitter, particle);

            /* Apply world position offset */
            particle->position_x += priv->position_x;
            particle->position_y += priv->position_y;
            particle->position_z += priv->position_z;

            g_ptr_array_add (priv->active_particles, particle);
            priv->active_count++;

            /* Callback for custom spawn behavior */
            if (klass->on_particle_spawn != NULL)
                klass->on_particle_spawn (self, particle);
        }
    }

    /* Update forces */
    for (l = priv->forces; l != NULL; l = l->next)
    {
        LrgParticleForce *force = LRG_PARTICLE_FORCE (l->data);
        lrg_particle_force_update (force, scaled_dt);
    }

    /*
     * Update active particles, newest first (the order the list used to
     * hold them in, so force, callback and draw order are unchanged).
     * Particles live in the pool's fixed array, so the pointers stay
     * valid; dead ones are released and NULLed here, then the array is
     * compacted in one stable pass.
     */
    removed = 0;
    for (i = priv->active_particles->len; i > 0; i--)
    {
        LrgParticle *particle;
        GList *force_iter;

        particle = (LrgParticle *)g_ptr_array_index (priv->active_particles, i - 1);

        /* Apply all forces */
        for (force_iter = priv->forces; force_iter != NULL; force_iter = force_iter->next)
        {
            LrgParticleForce *force = LRG_PARTICLE_FORCE (force_iter->data);
            lrg_particle_force_apply (force, particle, scaled_dt);
        }

        /* Update particle physics */
        lrg_particle_update (particle, scaled_dt);

        /* Check if particle is dead */
        if (!lrg_particle_is_alive (particle))
        {
            /* Callback for custom death behavior */
            if (klass->on_particle_death != NULL)
                klass->on_particle_death (self, particle);

            lrg_particle_pool_release (priv->pool, particle);
            g_ptr_array_index (priv->active_particles, i - 1) = NULL;
            priv->active_count--;
            removed++;
        }
    }

    if (removed > 0)
        active_compact (priv->active_particles);
}

static void
lrg_particle_system_real_draw (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
    guint i;

    /*
     * Default implementation does nothing.
     * Actual rendering depends on the graphics backend (graylib).
     * Subclasses or the application should implement actual drawing.
     */

    /* Iterate active particles for drawing */
    for (i = priv->active_particles->len; i > 0; i--)
    {
        LrgParticle *particle = (LrgParticle *)g_ptr_array_index (priv->active_particles, i - 1);

        /* In a real implementation, you would draw the particle here
         * based on render_mode and blend_mode */
        (void)particle;
    }
}

static void
lrg_particle_system_finalize (GObject *object)
{
    LrgParticleSystem        *self = LRG_PARTICLE_SYSTEM (object);
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);

    g_clear_object (&priv->pool);
    g_clear_pointer (&priv->active_particles, g_ptr_array_unref);
    g_list_free_full (priv->emitters, g_object_unref);
    g_list_free_full (priv->forces, g_object_unref);

    G_OBJECT_CLASS (lrg_particle_system_parent_class)->finalize (object);
}

static void
lrg_particle_system_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgParticleSystem        *self = LRG_PARTICLE_SYSTEM (object);
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_MAX_PARTICLES:
        g_value_set_uint (value, lrg_particle_pool_get_capacity (priv->pool));
        break;
    case PROP_ACTIVE_COUNT:
        g_value_set_uint (value, priv->active_count);
        break;
    case PROP_PLAYING:
        g_value_set_boolean (value, priv->playing);
        break;
    case PROP_LOOP:
        g_value_set_boolean (value, priv->loop);
        break;
    case PROP_DURATION:
        g_value_set_float (value, priv->duration);
        break;
    case PROP_TIME_SCALE:
        g_value_set_float (value, priv->time_scale);
        break;
    case PROP_RENDER_MODE:
        g_value_set_enum (value, priv->render_mode);
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, priv->blend_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_system_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgParticleSystem *self = LRG_PARTICLE_SYSTEM (object);

    switch (prop_id)
    {
    case PROP_MAX_PARTICLES:
        {
            LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);

            priv->max_particles = g_value_get_uint (value);
        }
        break;
    case PROP_LOOP:
        lrg_particle_system_set_loop (self, g_value_get_boolean (value));
        break;
    case PROP_DURATION:
        lrg_particle_system_set_duration (self, g_value_get_float (value));
        break;
    case PROP_TIME_SCALE:
        lrg_particle_system_set_time_scale (self, g_value_get_float (value));
        break;
    case PROP_RENDER_MODE:
        lrg_particle_system_set_render_mode (self, g_value_get_enum (value));
        break;
    case PROP_BLEND_MODE:
        lrg_particle_system_set_blend_mode (self, g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* The pool is created once the construct-only capacity is known, so
 * subclasses built with g_object_new() get one too. */
static void
lrg_particle_system_constructed (GObject *object)
{
    LrgParticleSystem        *self = LRG_PARTICLE_SYSTEM (object);
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
    guint                     capacity;

    G_OBJECT_CLASS (lrg_particle_system_parent_class)->constructed (object);

    /*
     * A fixed-size pool: max-particles is the system's capacity, and the
     * active array holds pointers into the pool's storage, which a
     * growing pool would reallocate out from under them.
     */
    capacity = priv->max_particles > 0 ? priv->max_particles : 1000;
    priv->pool = lrg_particle_pool_new_with_policy (capacity,
                                                    LRG_POOL_GROW_NONE,
                                                    capacity);
    priv->active_particles = g_ptr_array_sized_new (capacity);
}

static void
lrg_particle_system_class_init (LrgParticleSystemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_particle_system_finalize;
    object_class->constructed = lrg_particle_system_constructed;
    object_class->get_property = lrg_particle_system_get_property;
    object_class->set_property = lrg_particle_system_set_property;

    /* Virtual methods */
    klass->update = lrg_particle_system_real_update;
    klass->draw = lrg_particle_system_real_draw;
    klass->on_particle_spawn = NULL;
    klass->on_particle_death = NULL;

    properties[PROP_MAX_PARTICLES] =
        g_param_spec_uint ("max-particles",
                           "Max Particles",
                           "Maximum particle capacity",
                           1, G_MAXUINT, 1000,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_ACTIVE_COUNT] =
        g_param_spec_uint ("active-count",
                           "Active Count",
                           "Number of active particles",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    properties[PROP_PLAYING] =
        g_param_spec_boolean ("playing",
                              "Playing",
                              "Whether system is playing",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOP] =
        g_param_spec_boolean ("loop",
                              "Loop",
                              "Whether system loops",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_DURATION] =
        g_param_spec_float ("duration",
                            "Duration",
                            "System duration (0 = infinite)",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_TIME_SCALE] =
        g_param_spec_float ("time-scale",
                            "Time Scale",
                            "Time scale multiplier",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_RENDER_MODE] =
        g_param_spec_enum ("render-mode",
                           "Render Mode",
                           "Particle render mode",
                           LRG_TYPE_PARTICLE_RENDER_MODE,
                           LRG_PARTICLE_RENDER_BILLBOARD,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode",
                           "Blend Mode",
                           "Particle blend mode",
                           LRG_TYPE_PARTICLE_BLEND_MODE,
                           LRG_PARTICLE_BLEND_ALPHA,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_particle_system_init (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);

    priv->pool = NULL;  /* Created in constructed() */
    priv->active_particles = NULL;
    priv->active_count = 0;
    priv->emitters = NULL;
    priv->forces = NULL;
    priv->playing = FALSE;
    priv->loop = TRUE;
    priv->elapsed_time = 0.0f;
    priv->duration = 0.0f;  /* Infinite */
    priv->time_scale = 1.0f;
    priv->render_mode = LRG_PARTICLE_RENDER_BILLBOARD;
    priv->blend_mode = LRG_PARTICLE_BLEND_ALPHA;
    priv->position_x = 0.0f;
    priv->position_y = 0.0f;
    priv->position_z = 0.0f;
}

LrgParticleSystem *
lrg_particle_system_new (guint max_particles)
{
    return g_object_new (LRG_TYPE_PARTICLE_SYSTEM,
                         "max-particles", MAX (max_particles, 1u),
                         NULL);
}

void
lrg_particle_system_update (LrgParticleSystem *self,
                            gfloat             delta_time)
{
    LrgParticleSystemClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);

    if (klass->update != NULL)
        klass->update (self, delta_time);
}

void
lrg_particle_system_draw (LrgParticleSystem *self)
{
    LrgParticleSystemClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);

    if (klass->draw != NULL)
        klass->draw (self);
}

void
lrg_particle_system_foreach_active (LrgParticleSystem *self,
                                    LrgParticleFunc    func,
                                    gpointer           user_data)
{
    LrgParticleSystemPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (func != NULL);

    priv = lrg_particle_system_get_instance_private (self);

    /* The active list only holds live particles: dead ones are released
     * during update.  Skip a particle killed by a direct lrg_particle_kill()
     * since the last update all the same. */
    for (i = priv->active_particles->len; i > 0; i--)
    {
        const LrgParticle *particle =
            (const LrgParticle *)g_ptr_array_index (priv->active_particles, i - 1);

        if (particle->alive)
            func (particle, user_data);
    }
}

void
lrg_particle_system_add_emitter (LrgParticleSystem  *self,
                                 LrgParticleEmitter *emitter)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (emitter));

    priv = lrg_particle_system_get_instance_private (self);

    priv->emitters = g_list_append (priv->emitters, g_object_ref (emitter));
}

void
lrg_particle_system_remove_emitter (LrgParticleSystem  *self,
                                    LrgParticleEmitter *emitter)
{
    LrgParticleSystemPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (emitter));

    priv = lrg_particle_system_get_instance_private (self);

    l = g_list_find (priv->emitters, emitter);
    if (l != NULL)
    {
        priv->emitters = g_list_delete_link (priv->emitters, l);
        g_object_unref (emitter);
    }
}

GList *
lrg_particle_system_get_emitters (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->emitters;
}

void
lrg_particle_system_clear_emitters (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    g_list_free_full (priv->emitters, g_object_unref);
    priv->emitters = NULL;
}

void
lrg_particle_system_add_force (LrgParticleSystem *self,
                               LrgParticleForce  *force)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (LRG_IS_PARTICLE_FORCE (force));

    priv = lrg_particle_system_get_instance_private (self);

    priv->forces = g_list_append (priv->forces, g_object_ref (force));
}

void
lrg_particle_system_remove_force (LrgParticleSystem *self,
                                  LrgParticleForce  *force)
{
    LrgParticleSystemPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (LRG_IS_PARTICLE_FORCE (force));

    priv = lrg_particle_system_get_instance_private (self);

    l = g_list_find (priv->forces, force);
    if (l != NULL)
    {
        priv->forces = g_list_delete_link (priv->forces, l);
        g_object_unref (force);
    }
}

GList *
lrg_particle_system_get_forces (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->forces;
}

void
lrg_particle_system_clear_forces (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    g_list_free_full (priv->forces, g_object_unref);
    priv->forces = NULL;
}

guint
lrg_particle_system_emit (LrgParticleSystem *self,
                          guint              count)
{
    LrgParticleSystemPrivate *priv;
    LrgParticleSystemClass   *klass;
    LrgParticleEmitter *emitter;
    guint emitted;
    guint i;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

    priv = lrg_particle_system_get_instance_private (self);
    klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);

    if (priv->emitters == NULL)
        return 0;

    emitter = LRG_PARTICLE_EMITTER (priv->emitters->data);
    emitted = 0;

    for (i = 0; i < count; i++)
    {
        LrgParticle *particle;

        particle = lrg_particle_pool_acquire (priv->pool);
        if (particle == NULL)
            break;

        lrg_particle_emitter_emit (emitter, particle);

        particle->position_x += priv->position_x;
        particle->position_y += priv->position_y;
        particle->position_z += priv->position_z;

        g_ptr_array_add (priv->active_particles, particle);
        priv->active_count++;
        emitted++;

        /* Same hook as rate emission in update(): burst particles get the
         * subclass's custom initialisation too. */
        if (klass->on_particle_spawn != NULL)
            klass->on_particle_spawn (self, particle);
    }

    return emitted;
}

guint
lrg_particle_system_emit_at (LrgParticleSystem *self,
                             gfloat             x,
                             gfloat             y,
                             gfloat             z,
                             guint              count)
{
    LrgParticleSystemPrivate *priv;
    gfloat old_x, old_y, old_z;
    guint result;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

    priv = lrg_particle_system_get_instance_private (self);

    /* Temporarily change position */
    old_x = priv->position_x;
    old_y = priv->position_y;
    old_z = priv->position_z;

    priv->position_x = x;
    priv->position_y = y;
    priv->position_z = z;

    result = lrg_particle_system_emit (self, count);

    /* Restore position */
    priv->position_x = old_x;
    priv->position_y = old_y;
    priv->position_z = old_z;

    return result;
}

void
lrg_particle_system_clear (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    for (i = 0; i < priv->active_particles->len; i++)
    {
        LrgParticle *particle = (LrgParticle *)g_ptr_array_index (priv->active_particles, i);
        lrg_particle_pool_release (priv->pool, particle);
    }

    g_ptr_array_set_size (priv->active_particles, 0);
    priv->active_count = 0;
}

void
lrg_particle_system_play (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->playing)
        return;

    priv->playing = TRUE;
    priv->elapsed_time = 0.0f;

    /* Re-enable emitters */
    {
        GList *l;
        for (l = priv->emitters; l != NULL; l = l->next)
        {
            LrgParticleEmitter *emitter = LRG_PARTICLE_EMITTER (l->data);
            lrg_particle_emitter_set_enabled (emitter, TRUE);
            lrg_particle_emitter_reset (emitter);
        }
    }
}

void
lrg_particle_system_pause (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    priv->playing = FALSE;
}

void
lrg_particle_system_stop (LrgParticleSystem *self)
{
    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    lrg_particle_system_pause (self);
    lrg_particle_system_clear (self);
}

gboolean
lrg_particle_system_is_playing (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->playing;
}

gboolean
lrg_particle_system_is_alive (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;
    GList *l;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

    priv = lrg_particle_system_get_instance_private (self);

    /* Has active particles */
    if (priv->active_count > 0)
        return TRUE;

    /* Has enabled emitters */
    for (l = priv->emitters; l != NULL; l = l->next)
    {
        LrgParticleEmitter *emitter = LRG_PARTICLE_EMITTER (l->data);
        if (lrg_particle_emitter_get_enabled (emitter))
            return TRUE;
    }

    return FALSE;
}

guint
lrg_particle_system_get_active_count (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->active_count;
}

guint
lrg_particle_system_get_max_particles (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

    priv = lrg_particle_system_get_instance_private (self);

    return lrg_particle_pool_get_capacity (priv->pool);
}

LrgParticleRenderMode
lrg_particle_system_get_render_mode (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), LRG_PARTICLE_RENDER_BILLBOARD);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->render_mode;
}

void
lrg_particle_system_set_render_mode (LrgParticleSystem     *self,
                                     LrgParticleRenderMode  mode)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->render_mode == mode)
        return;

    priv->render_mode = mode;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_MODE]);
}

LrgParticleBlendMode
lrg_particle_system_get_blend_mode (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), LRG_PARTICLE_BLEND_ALPHA);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->blend_mode;
}

void
lrg_particle_system_set_blend_mode (LrgParticleSystem    *self,
                                    LrgParticleBlendMode  mode)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->blend_mode == mode)
        return;

    priv->blend_mode = mode;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
}

void
lrg_particle_system_get_position (LrgParticleSystem *self,
                                  gfloat            *x,
                                  gfloat            *y,
                                  gfloat            *z)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (x != NULL)
        *x = priv->position_x;
    if (y != NULL)
        *y = priv->position_y;
    if (z != NULL)
        *z = priv->position_z;
}

void
lrg_particle_system_set_position (LrgParticleSystem *self,
                                  gfloat             x,
                                  gfloat             y,
                                  gfloat             z)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    priv->position_x = x;
    priv->position_y = y;
    priv->position_z = z;
}

gboolean
lrg_particle_system_get_loop (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), TRUE);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->loop;
}

void
lrg_particle_system_set_loop (LrgParticleSystem *self,
                              gboolean           loop)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    loop = !!loop;
    if (priv->loop == loop)
        return;

    priv->loop = loop;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOP]);
}

gfloat
lrg_particle_system_get_duration (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0.0f);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->duration;
}

void
lrg_particle_system_set_duration (LrgParticleSystem *self,
                                  gfloat             duration)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->duration == duration)
        return;

    priv->duration = MAX (0.0f, duration);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
}

gfloat
lrg_particle_system_get_time_scale (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 1.0f);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->time_scale;
}

void
lrg_particle_system_set_time_scale (LrgParticleSystem *self,
                                    gfloat             scale)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->time_scale == scale)
        return;

    priv->time_scale = MAX (0.0f, scale);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_SCALE]);
}
