/* lrg-particle-system.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-particle-system.h"
#include "../graphics/lrg-camera.h"
#include "../tween/lrg-keyframe-curve.h"

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

/* Number of LrgParticleModuleStage values */
#define LRG_PARTICLE_STAGE_COUNT (LRG_PARTICLE_MODULE_STAGE_EVENT + 1)

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
    guint                 layer;           /* render layer 0 .. 31 */

    /* World transform */
    gfloat               position_x;
    gfloat               position_y;
    gfloat               position_z;

    /* Module stack (0.9.0) */
    GPtrArray           *modules;          /* LrgParticleModule *, owned, stack order */
    GPtrArray           *stages[LRG_PARTICLE_STAGE_COUNT]; /* borrowed, per stage */
    gboolean             stages_dirty;
    LrgParticleRng       rng;
    guint64              seed;
    gchar               *name;
    guint32              next_id;
    gfloat               start_delay;
    gfloat               rotation[4];
    gboolean             local_space;
    gfloat               module_time;      /* seconds since play (delay included) */
    gboolean             emitting;
    gboolean             stopped;          /* next play() restarts */
    gfloat               last_position[3]; /* for moved / inherited velocity */
    gboolean             have_last;
    LrgParticleModuleContext ctx;          /* the current step's context */

    LrgParticleSystemLookupFunc lookup;
    gpointer             lookup_data;
    GDestroyNotify       lookup_destroy;
    LrgParticleRaycastFunc raycast;
    gpointer             raycast_data;
    GDestroyNotify       raycast_destroy;
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
    PROP_LAYER,
    PROP_SEED,
    PROP_NAME,
    PROP_START_DELAY,
    PROP_LOCAL_SPACE,
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

/* ==========================================================================
 * LrgParticleRng: PCG32 (XSH-RR), M. E. O'Neill, pcg-random.org.
 * The algorithm is public domain / Apache-2.0; this is a from-scratch
 * implementation of the published reference.
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgParticleRng, lrg_particle_rng, lrg_particle_rng_copy, lrg_particle_rng_free)

#define PCG_MULT G_GUINT64_CONSTANT (6364136223846793005)

LrgParticleRng *
lrg_particle_rng_new (guint64 seed)
{
	LrgParticleRng *self = g_new0 (LrgParticleRng, 1);

	lrg_particle_rng_seed (self, seed);
	return self;
}

LrgParticleRng *
lrg_particle_rng_copy (const LrgParticleRng *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return g_memdup2 (self, sizeof *self);
}

void
lrg_particle_rng_free (LrgParticleRng *self)
{
	g_free (self);
}

void
lrg_particle_rng_seed (LrgParticleRng *self,
                       guint64         seed)
{
	g_return_if_fail (self != NULL);

	/* pcg32_srandom_r (seed, stream): the stream is derived from the seed
	 * so nearby seeds give unrelated sequences */
	self->state = 0;
	self->inc = ((seed ^ G_GUINT64_CONSTANT (0xda3e39cb94b95bdb)) << 1) | 1u;
	(void) lrg_particle_rng_next_u32 (self);
	self->state += seed;
	(void) lrg_particle_rng_next_u32 (self);
}

guint32
lrg_particle_rng_next_u32 (LrgParticleRng *self)
{
	guint64 old;
	guint32 xorshifted;
	guint32 rot;

	g_return_val_if_fail (self != NULL, 0);

	old = self->state;
	self->state = old * PCG_MULT + self->inc;
	xorshifted = (guint32) (((old >> 18) ^ old) >> 27);
	rot = (guint32) (old >> 59);
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

gfloat
lrg_particle_rng_next_float (LrgParticleRng *self)
{
	/* 24 bits: every value is exact in a float and < 1 */
	return (gfloat) (lrg_particle_rng_next_u32 (self) >> 8) * (1.0f / 16777216.0f);
}

gfloat
lrg_particle_rng_range (LrgParticleRng *self,
                        gfloat          min,
                        gfloat          max)
{
	if (min == max)
		return min;
	return min + (max - min) * lrg_particle_rng_next_float (self);
}

void
lrg_particle_rng_unit_vector (LrgParticleRng *self,
                              gfloat         *out)
{
	gfloat z, phi, r;

	g_return_if_fail (out != NULL);

	/* Archimedes: z uniform in [-1, 1], azimuth uniform */
	z = 2.0f * lrg_particle_rng_next_float (self) - 1.0f;
	phi = 2.0f * (gfloat) G_PI * lrg_particle_rng_next_float (self);
	r = sqrtf (MAX (0.0f, 1.0f - z * z));
	out[0] = r * cosf (phi);
	out[1] = z;
	out[2] = r * sinf (phi);
}

/* ==========================================================================
 * Enums
 * ========================================================================== */

GType
lrg_particle_module_stage_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_MODULE_STAGE_SPAWN, "LRG_PARTICLE_MODULE_STAGE_SPAWN", "spawn" },
			{ LRG_PARTICLE_MODULE_STAGE_SHAPE, "LRG_PARTICLE_MODULE_STAGE_SHAPE", "shape" },
			{ LRG_PARTICLE_MODULE_STAGE_INIT, "LRG_PARTICLE_MODULE_STAGE_INIT", "init" },
			{ LRG_PARTICLE_MODULE_STAGE_FORCE, "LRG_PARTICLE_MODULE_STAGE_FORCE", "force" },
			{ LRG_PARTICLE_MODULE_STAGE_COLLISION, "LRG_PARTICLE_MODULE_STAGE_COLLISION", "collision" },
			{ LRG_PARTICLE_MODULE_STAGE_OVER_LIFE, "LRG_PARTICLE_MODULE_STAGE_OVER_LIFE", "over-life" },
			{ LRG_PARTICLE_MODULE_STAGE_EVENT, "LRG_PARTICLE_MODULE_STAGE_EVENT", "event" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleModuleStage", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_particle_event_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_EVENT_BIRTH, "LRG_PARTICLE_EVENT_BIRTH", "birth" },
			{ LRG_PARTICLE_EVENT_DEATH, "LRG_PARTICLE_EVENT_DEATH", "death" },
			{ LRG_PARTICLE_EVENT_COLLISION, "LRG_PARTICLE_EVENT_COLLISION", "collision" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleEvent", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgParticleModule (abstract base)
 * ========================================================================== */

typedef struct
{
	gboolean enabled;
} LrgParticleModulePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgParticleModule, lrg_particle_module, G_TYPE_OBJECT)

enum
{
	MODULE_PROP_0,
	MODULE_PROP_ENABLED,
	MODULE_N_PROPS
};

static GParamSpec *module_properties[MODULE_N_PROPS];

static void
lrg_particle_module_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgParticleModulePrivate *priv = lrg_particle_module_get_instance_private (LRG_PARTICLE_MODULE (object));

	switch (prop_id)
	{
	case MODULE_PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_module_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	switch (prop_id)
	{
	case MODULE_PROP_ENABLED:
		lrg_particle_module_set_enabled (LRG_PARTICLE_MODULE (object), g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_module_class_init (LrgParticleModuleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_particle_module_get_property;
	object_class->set_property = lrg_particle_module_set_property;
	klass->stage = LRG_PARTICLE_MODULE_STAGE_INIT;

	/**
	 * LrgParticleModule:enabled:
	 *
	 * Whether the module runs.  Disabled modules stay in the stack.
	 */
	module_properties[MODULE_PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether the module runs", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, MODULE_N_PROPS, module_properties);
}

static void
lrg_particle_module_init (LrgParticleModule *self)
{
	LrgParticleModulePrivate *priv = lrg_particle_module_get_instance_private (self);

	priv->enabled = TRUE;
}

LrgParticleModuleStage
lrg_particle_module_get_stage (LrgParticleModule *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), LRG_PARTICLE_MODULE_STAGE_INIT);
	return LRG_PARTICLE_MODULE_GET_CLASS (self)->stage;
}

gboolean
lrg_particle_module_get_enabled (LrgParticleModule *self)
{
	LrgParticleModulePrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), FALSE);
	priv = lrg_particle_module_get_instance_private (self);
	return priv->enabled;
}

void
lrg_particle_module_set_enabled (LrgParticleModule *self,
                                 gboolean           enabled)
{
	LrgParticleModulePrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_MODULE (self));
	priv = lrg_particle_module_get_instance_private (self);
	enabled = !!enabled;
	if (priv->enabled == enabled)
		return;
	priv->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), module_properties[MODULE_PROP_ENABLED]);
}

void
lrg_particle_module_reset (LrgParticleModule *self)
{
	LrgParticleModuleClass *klass;

	g_return_if_fail (LRG_IS_PARTICLE_MODULE (self));
	klass = LRG_PARTICLE_MODULE_GET_CLASS (self);
	if (klass->reset != NULL)
		klass->reset (self);
}

guint
lrg_particle_module_spawn_count (LrgParticleModule        *self,
                                 LrgParticleModuleContext *ctx)
{
	LrgParticleModuleClass *klass;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), 0);
	klass = LRG_PARTICLE_MODULE_GET_CLASS (self);
	if (klass->spawn_count == NULL || !lrg_particle_module_get_enabled (self))
		return 0;
	return klass->spawn_count (self, ctx);
}

void
lrg_particle_module_init_particle (LrgParticleModule        *self,
                                   LrgParticleModuleContext *ctx,
                                   LrgParticle              *particle)
{
	LrgParticleModuleClass *klass;

	g_return_if_fail (LRG_IS_PARTICLE_MODULE (self));
	klass = LRG_PARTICLE_MODULE_GET_CLASS (self);
	if (klass->init_particle != NULL && lrg_particle_module_get_enabled (self))
		klass->init_particle (self, ctx, particle);
}

void
lrg_particle_module_update_particle (LrgParticleModule        *self,
                                     LrgParticleModuleContext *ctx,
                                     LrgParticle              *particle)
{
	LrgParticleModuleClass *klass;

	g_return_if_fail (LRG_IS_PARTICLE_MODULE (self));
	klass = LRG_PARTICLE_MODULE_GET_CLASS (self);
	if (klass->update_particle != NULL && lrg_particle_module_get_enabled (self))
		klass->update_particle (self, ctx, particle);
}

void
lrg_particle_module_particle_event (LrgParticleModule        *self,
                                    LrgParticleModuleContext *ctx,
                                    LrgParticle              *particle,
                                    LrgParticleEvent          event)
{
	LrgParticleModuleClass *klass;

	g_return_if_fail (LRG_IS_PARTICLE_MODULE (self));
	klass = LRG_PARTICLE_MODULE_GET_CLASS (self);
	if (klass->particle_event != NULL && lrg_particle_module_get_enabled (self))
		klass->particle_event (self, ctx, particle, event);
}

LrgParticleModule *
lrg_particle_module_copy (LrgParticleModule *self)
{
	g_autofree GParamSpec **pspecs = NULL;
	GObject                *copy;
	guint                   n, i;

	g_return_val_if_fail (LRG_IS_PARTICLE_MODULE (self), NULL);

	/* Same type, every read-write property copied by value; object
	 * values (curves) and boxed values are duplicated so the copy can be
	 * edited independently */
	copy = g_object_new (G_OBJECT_TYPE (self), NULL);
	pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (self), &n);
	for (i = 0; i < n; i++)
	{
		GParamSpec    *ps = pspecs[i];
		g_auto(GValue) v = G_VALUE_INIT;

		if ((ps->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE ||
		    (ps->flags & G_PARAM_CONSTRUCT_ONLY) != 0)
			continue;
		g_value_init (&v, G_PARAM_SPEC_VALUE_TYPE (ps));
		g_object_get_property (G_OBJECT (self), ps->name, &v);
		if (G_VALUE_HOLDS (&v, LRG_TYPE_KEYFRAME_CURVE) && g_value_get_object (&v) != NULL)
			g_value_take_object (&v, lrg_keyframe_curve_copy (LRG_KEYFRAME_CURVE (g_value_get_object (&v))));
		g_object_set_property (copy, ps->name, &v);
	}
	return LRG_PARTICLE_MODULE (copy);
}

/* ==========================================================================
 * Module stack helpers
 * ========================================================================== */

/* Rotates @v by the unit quaternion @q (x, y, z, w) into @out. */
static void
quat_rotate (const gfloat *q,
             const gfloat *v,
             gfloat       *out)
{
	gfloat tx, ty, tz;

	/* t = 2 * cross (q.xyz, v); out = v + w * t + cross (q.xyz, t) */
	tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

/* Rebuilds the per-stage lists after the stack changed. */
static void
ensure_stages (LrgParticleSystemPrivate *priv)
{
	guint i;

	if (!priv->stages_dirty)
		return;
	for (i = 0; i < LRG_PARTICLE_STAGE_COUNT; i++)
		g_ptr_array_set_size (priv->stages[i], 0);
	for (i = 0; i < priv->modules->len; i++)
	{
		LrgParticleModule     *m = g_ptr_array_index (priv->modules, i);
		LrgParticleModuleStage st = lrg_particle_module_get_stage (m);

		if ((guint) st < LRG_PARTICLE_STAGE_COUNT)
			g_ptr_array_add (priv->stages[st], m);
	}
	priv->stages_dirty = FALSE;
}

/* Runs update_particle() of every module of @stage on @p. */
static void
run_stage (LrgParticleSystemPrivate *priv,
           LrgParticleModuleStage    stage,
           LrgParticleModuleContext *ctx,
           LrgParticle              *p)
{
	GPtrArray *list = priv->stages[stage];
	guint      i;

	/* the hot path: no type checks, the vfunc called directly */
	for (i = 0; i < list->len && p->alive; i++)
	{
		LrgParticleModule      *m = g_ptr_array_index (list, i);
		LrgParticleModuleClass *klass = LRG_PARTICLE_MODULE_GET_CLASS (m);
		LrgParticleModulePrivate *mp = lrg_particle_module_get_instance_private (m);

		if (mp->enabled && klass->update_particle != NULL)
			klass->update_particle (m, ctx, p);
	}
}

/* Delivers @event to the event-stage modules with @ctx's event fields set. */
static void
deliver_event (LrgParticleSystemPrivate *priv,
               LrgParticleModuleContext *ctx,
               LrgParticle              *p,
               LrgParticleEvent          event,
               const gfloat             *point,
               const gfloat             *normal)
{
	GPtrArray *list;
	guint      i;

	ensure_stages (priv);
	list = priv->stages[LRG_PARTICLE_MODULE_STAGE_EVENT];
	if (list->len == 0)
		return;
	if (point != NULL)
		memcpy (ctx->event_point, point, sizeof ctx->event_point);
	else
	{
		ctx->event_point[0] = p->position_x;
		ctx->event_point[1] = p->position_y;
		ctx->event_point[2] = p->position_z;
	}
	if (normal != NULL)
		memcpy (ctx->event_normal, normal, sizeof ctx->event_normal);
	else
		memset (ctx->event_normal, 0, sizeof ctx->event_normal);
	for (i = 0; i < list->len; i++)
		lrg_particle_module_particle_event (g_ptr_array_index (list, i), ctx, p, event);
}

/*
 * Spawns @count particles through the shape and init stages at
 * ctx->origin.  Each starts at the origin with default values (white,
 * size 1, a one second life), draws its per-particle random number,
 * then the modules fill it in; the result becomes its start values.
 */
static guint
spawn_particles (LrgParticleSystem        *self,
                 LrgParticleModuleContext *ctx,
                 guint                     count)
{
	LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
	LrgParticleSystemClass   *klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);
	static const gfloat       up[3] = { 0.0f, 1.0f, 0.0f };
	guint                     n;

	ensure_stages (priv);
	for (n = 0; n < count; n++)
	{
		LrgParticle *p = lrg_particle_pool_acquire (priv->pool);
		guint        i;

		if (p == NULL)
			break;   /* pool exhausted */

		lrg_particle_spawn (p, ctx->origin[0], ctx->origin[1], ctx->origin[2], 1.0f);
		p->random = lrg_particle_rng_next_float (&priv->rng);
		p->id = ++priv->next_id;
		quat_rotate (ctx->rotation, up, ctx->direction);

		for (i = 0; i < priv->stages[LRG_PARTICLE_MODULE_STAGE_SHAPE]->len; i++)
			lrg_particle_module_init_particle (g_ptr_array_index (priv->stages[LRG_PARTICLE_MODULE_STAGE_SHAPE], i),
			                                   ctx, p);
		for (i = 0; i < priv->stages[LRG_PARTICLE_MODULE_STAGE_INIT]->len; i++)
			lrg_particle_module_init_particle (g_ptr_array_index (priv->stages[LRG_PARTICLE_MODULE_STAGE_INIT], i),
			                                   ctx, p);

		/* a module may have left an unusable lifetime */
		if (!(p->max_life > 0.0f))
			p->max_life = 1.0f;
		p->life = p->max_life;
		p->age = 0.0f;
		p->alive = TRUE;
		p->start_size = p->size;
		p->start_color_r = p->color_r;
		p->start_color_g = p->color_g;
		p->start_color_b = p->color_b;
		p->start_color_a = p->color_a;
		p->start_rotation_velocity = p->rotation_velocity;

		g_ptr_array_add (priv->active_particles, p);
		priv->active_count++;
		if (klass->on_particle_spawn != NULL)
			klass->on_particle_spawn (self, p);
		deliver_event (priv, ctx, p, LRG_PARTICLE_EVENT_BIRTH, NULL, NULL);
	}
	return n;
}

/*
 * Fills priv->ctx with the clock and transform for a step of @dt: the
 * emission cycle from module_time (after the start delay), the origin,
 * rotation and the movement since the previous step.
 */
static void
fill_context (LrgParticleSystem *self,
              gfloat             dt)
{
	LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
	LrgParticleModuleContext *ctx = &priv->ctx;
	gfloat                    t_emit;
	gint                      k;

	ctx->system = self;
	ctx->rng = &priv->rng;
	ctx->dt = dt;
	ctx->time = priv->module_time;
	t_emit = priv->module_time - priv->start_delay;
	if (t_emit < 0.0f)
	{
		ctx->cycle = 0;
		ctx->cycle_time = 0.0f;
		ctx->cycle_fraction = 0.0f;
	}
	else if (priv->duration > 0.0f)
	{
		gfloat cycles = floorf (t_emit / priv->duration);

		if (!priv->loop && cycles >= 1.0f)
		{
			priv->emitting = FALSE;
			ctx->cycle = 0;
			ctx->cycle_time = priv->duration;
			ctx->cycle_fraction = 1.0f;
		}
		else
		{
			ctx->cycle = (guint) cycles;
			ctx->cycle_time = t_emit - cycles * priv->duration;
			ctx->cycle_fraction = ctx->cycle_time / priv->duration;
		}
	}
	else
	{
		ctx->cycle = 0;
		ctx->cycle_time = t_emit;
		ctx->cycle_fraction = 0.0f;
	}

	ctx->origin[0] = priv->position_x;
	ctx->origin[1] = priv->position_y;
	ctx->origin[2] = priv->position_z;
	memcpy (ctx->rotation, priv->rotation, sizeof ctx->rotation);
	ctx->moved = 0.0f;
	memset (ctx->inherit_velocity, 0, sizeof ctx->inherit_velocity);
	if (priv->have_last && dt > 0.0f)
	{
		gfloat d2 = 0.0f;

		for (k = 0; k < 3; k++)
		{
			gfloat d = ctx->origin[k] - priv->last_position[k];

			ctx->inherit_velocity[k] = d / dt;
			d2 += d * d;
		}
		ctx->moved = sqrtf (d2);
	}
	memset (ctx->direction, 0, sizeof ctx->direction);
	ctx->direction[1] = 1.0f;
}

/* Resets the module run state and the generator: a fresh start. */
static void
rewind_modules (LrgParticleSystemPrivate *priv)
{
	guint i;

	lrg_particle_rng_seed (&priv->rng, priv->seed);
	priv->next_id = 0;
	priv->module_time = 0.0f;
	priv->emitting = TRUE;
	priv->have_last = FALSE;
	for (i = 0; i < priv->modules->len; i++)
		lrg_particle_module_reset (g_ptr_array_index (priv->modules, i));
}

static void
lrg_particle_system_real_update (LrgParticleSystem *self,
                                 gfloat             delta_time)
{
    LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);
    LrgParticleSystemClass   *klass = LRG_PARTICLE_SYSTEM_GET_CLASS (self);
    LrgParticleModuleContext *ctx = &priv->ctx;
    GList *l;
    gfloat scaled_dt;
    guint  i;
    guint  removed;
    gboolean modular;

    if (!priv->playing)
        return;

    scaled_dt = delta_time * priv->time_scale;
    priv->elapsed_time += scaled_dt;
    priv->module_time += scaled_dt;

    /* Check duration (legacy emitters) */
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

    /* Module stack: spawn-stage modules decide how many to emit */
    modular = priv->modules->len > 0;
    if (modular)
    {
        guint count = 0;

        ensure_stages (priv);
        fill_context (self, scaled_dt);
        if (priv->emitting && priv->module_time >= priv->start_delay)
        {
            GPtrArray *spawners = priv->stages[LRG_PARTICLE_MODULE_STAGE_SPAWN];

            for (i = 0; i < spawners->len; i++)
                count += lrg_particle_module_spawn_count (g_ptr_array_index (spawners, i), ctx);
        }
        if (count > 0)
            spawn_particles (self, ctx, count);
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
     * compacted in one stable pass.  Sub-emitters may append to the
     * array while it is walked; appended particles are past the cursor.
     */
    removed = 0;
    for (i = priv->active_particles->len; i > 0; i--)
    {
        LrgParticle *particle;
        GList *force_iter;

        particle = (LrgParticle *)g_ptr_array_index (priv->active_particles, i - 1);
        if (particle == NULL)
            continue;

        /* Apply all forces */
        for (force_iter = priv->forces; force_iter != NULL; force_iter = force_iter->next)
        {
            LrgParticleForce *force = LRG_PARTICLE_FORCE (force_iter->data);
            lrg_particle_force_apply (force, particle, scaled_dt);
        }
        if (modular)
            run_stage (priv, LRG_PARTICLE_MODULE_STAGE_FORCE, ctx, particle);

        /* Update particle physics */
        lrg_particle_update (particle, scaled_dt);

        /* After the move: collisions, then the over-life values */
        if (modular && lrg_particle_is_alive (particle))
        {
            run_stage (priv, LRG_PARTICLE_MODULE_STAGE_COLLISION, ctx, particle);
            if (lrg_particle_is_alive (particle))
                run_stage (priv, LRG_PARTICLE_MODULE_STAGE_OVER_LIFE, ctx, particle);
        }

        /* Check if particle is dead */
        if (!lrg_particle_is_alive (particle))
        {
            if (modular)
                deliver_event (priv, ctx, particle, LRG_PARTICLE_EVENT_DEATH, NULL, NULL);

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

    priv->last_position[0] = priv->position_x;
    priv->last_position[1] = priv->position_y;
    priv->last_position[2] = priv->position_z;
    priv->have_last = TRUE;
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
    {
        guint k;

        for (k = 0; k < LRG_PARTICLE_STAGE_COUNT; k++)
            g_clear_pointer (&priv->stages[k], g_ptr_array_unref);
    }
    g_clear_pointer (&priv->modules, g_ptr_array_unref);
    g_clear_pointer (&priv->name, g_free);
    if (priv->lookup_destroy != NULL)
        priv->lookup_destroy (priv->lookup_data);
    if (priv->raycast_destroy != NULL)
        priv->raycast_destroy (priv->raycast_data);

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
    case PROP_LAYER:
        g_value_set_uint (value, priv->layer);
        break;
    case PROP_SEED:
        g_value_set_uint64 (value, priv->seed);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_START_DELAY:
        g_value_set_float (value, priv->start_delay);
        break;
    case PROP_LOCAL_SPACE:
        g_value_set_boolean (value, priv->local_space);
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
    case PROP_LAYER:
        lrg_particle_system_set_layer (self, g_value_get_uint (value));
        break;
    case PROP_SEED:
        lrg_particle_system_set_seed (self, g_value_get_uint64 (value));
        break;
    case PROP_NAME:
        lrg_particle_system_set_name (self, g_value_get_string (value));
        break;
    case PROP_START_DELAY:
        lrg_particle_system_set_start_delay (self, g_value_get_float (value));
        break;
    case PROP_LOCAL_SPACE:
        lrg_particle_system_set_local_space (self, g_value_get_boolean (value));
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

    /**
     * LrgParticleSystem:layer:
     *
     * The render layer (0 .. 31, see lrg_render_layers_get_name()) the
     * system draws on.  lrg_particle_system_draw() and
     * lrg_particle_billboard_renderer_draw() draw nothing while it is
     * outside lrg_render_layers_get_active_mask().
     */
    properties[PROP_LAYER] =
        g_param_spec_uint ("layer",
                           "Layer",
                           "Render layer the system draws on",
                           0, LRG_RENDER_LAYERS_COUNT - 1, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleSystem:seed:
     *
     * Seed of the system's #LrgParticleRng (since 0.9.0).  The module
     * stack draws all its randomness from it.
     */
    properties[PROP_SEED] =
        g_param_spec_uint64 ("seed", "Seed", "Seed of the random generator",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleSystem:name:
     *
     * The system's name (since 0.9.0): an effect names each system after
     * its emitter, and sub-emitters find their target by it.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Name sub-emitters refer to", NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleSystem:start-delay:
     *
     * Seconds after play before the module stack emits (since 0.9.0).
     */
    properties[PROP_START_DELAY] =
        g_param_spec_float ("start-delay", "Start Delay", "Seconds before the module stack emits",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleSystem:local-space:
     *
     * Whether live particles follow the system's movement (since 0.9.0).
     */
    properties[PROP_LOCAL_SPACE] =
        g_param_spec_boolean ("local-space", "Local Space", "Particles follow the system's movement",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

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

    {
        guint k;

        priv->modules = g_ptr_array_new_with_free_func (g_object_unref);
        for (k = 0; k < LRG_PARTICLE_STAGE_COUNT; k++)
            priv->stages[k] = g_ptr_array_new ();
    }
    priv->stages_dirty = TRUE;
    priv->rotation[3] = 1.0f;
    priv->emitting = TRUE;
    priv->stopped = TRUE;
    lrg_particle_rng_seed (&priv->rng, 0);
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

    /* A system on a render layer outside the active cull mask (the
     * begun camera's) draws nothing. */
    if (!lrg_particle_system_is_layer_visible (self))
        return;

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

    /* After a stop (or the first time) the module stack starts over:
     * reseeded generator, reset modules, clock at zero.  After a pause
     * it resumes where it was. */
    if (priv->stopped)
    {
        rewind_modules (priv);
        priv->stopped = FALSE;
    }

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
    {
        LrgParticleSystemPrivate *priv = lrg_particle_system_get_instance_private (self);

        priv->stopped = TRUE;
    }
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

    /* Has a module stack that still emits */
    if (priv->playing && priv->emitting && priv->modules->len > 0)
    {
        ensure_stages (priv);
        if (priv->stages[LRG_PARTICLE_MODULE_STAGE_SPAWN]->len > 0)
            return TRUE;
    }

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
lrg_particle_system_set_layer (LrgParticleSystem *self,
                               guint              layer)
{
    LrgParticleSystemPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
    g_return_if_fail (layer < LRG_RENDER_LAYERS_COUNT);

    priv = lrg_particle_system_get_instance_private (self);

    if (priv->layer == layer)
        return;

    priv->layer = layer;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

guint
lrg_particle_system_get_layer (LrgParticleSystem *self)
{
    LrgParticleSystemPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

    priv = lrg_particle_system_get_instance_private (self);

    return priv->layer;
}

gboolean
lrg_particle_system_is_layer_visible (LrgParticleSystem *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

    return lrg_render_layers_is_visible (lrg_particle_system_get_layer (self));
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

    /* Local space: live particles move with the system */
    if (priv->local_space && priv->active_particles != NULL)
    {
        gfloat dx = x - priv->position_x;
        gfloat dy = y - priv->position_y;
        gfloat dz = z - priv->position_z;
        guint  i;

        for (i = 0; i < priv->active_particles->len; i++)
        {
            LrgParticle *p = g_ptr_array_index (priv->active_particles, i);

            p->position_x += dx;
            p->position_y += dy;
            p->position_z += dz;
        }
        /* the move is not emitter motion: no inherited velocity from it */
        priv->last_position[0] += dx;
        priv->last_position[1] += dy;
        priv->last_position[2] += dz;
    }

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

/* ==========================================================================
 * Module stack, seeding, naming, sub-emitters (0.9.0)
 * ========================================================================== */

void
lrg_particle_system_add_module (LrgParticleSystem *self,
                                LrgParticleModule *module)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
	g_return_if_fail (LRG_IS_PARTICLE_MODULE (module));

	priv = lrg_particle_system_get_instance_private (self);
	g_ptr_array_add (priv->modules, g_object_ref (module));
	priv->stages_dirty = TRUE;
}

gboolean
lrg_particle_system_remove_module (LrgParticleSystem *self,
                                   LrgParticleModule *module)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

	priv = lrg_particle_system_get_instance_private (self);
	if (!g_ptr_array_remove (priv->modules, module))
		return FALSE;
	priv->stages_dirty = TRUE;
	return TRUE;
}

void
lrg_particle_system_clear_modules (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	g_ptr_array_set_size (priv->modules, 0);
	priv->stages_dirty = TRUE;
}

guint
lrg_particle_system_get_n_modules (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->modules->len;
}

LrgParticleModule *
lrg_particle_system_get_module (LrgParticleSystem *self,
                                guint              index)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

	priv = lrg_particle_system_get_instance_private (self);
	if (index >= priv->modules->len)
		return NULL;
	return g_ptr_array_index (priv->modules, index);
}

guint64
lrg_particle_system_get_seed (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->seed;
}

void
lrg_particle_system_set_seed (LrgParticleSystem *self,
                              guint64            seed)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	priv->seed = seed;
	lrg_particle_rng_seed (&priv->rng, seed);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
}

LrgParticleRng *
lrg_particle_system_get_rng (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

	priv = lrg_particle_system_get_instance_private (self);
	return &priv->rng;
}

const gchar *
lrg_particle_system_get_name (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->name;
}

void
lrg_particle_system_set_name (LrgParticleSystem *self,
                              const gchar       *name)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	if (g_set_str (&priv->name, name))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

void
lrg_particle_system_set_lookup_func (LrgParticleSystem          *self,
                                     LrgParticleSystemLookupFunc func,
                                     gpointer                    user_data,
                                     GDestroyNotify              destroy)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	if (priv->lookup_destroy != NULL)
		priv->lookup_destroy (priv->lookup_data);
	priv->lookup = func;
	priv->lookup_data = user_data;
	priv->lookup_destroy = destroy;
}

LrgParticleSystem *
lrg_particle_system_lookup (LrgParticleSystem *self,
                            const gchar       *name)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), NULL);

	priv = lrg_particle_system_get_instance_private (self);
	if (priv->lookup == NULL || name == NULL)
		return NULL;
	return priv->lookup (name, priv->lookup_data);
}

void
lrg_particle_system_set_raycast_func (LrgParticleSystem     *self,
                                      LrgParticleRaycastFunc func,
                                      gpointer               user_data,
                                      GDestroyNotify         destroy)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	if (priv->raycast_destroy != NULL)
		priv->raycast_destroy (priv->raycast_data);
	priv->raycast = func;
	priv->raycast_data = user_data;
	priv->raycast_destroy = destroy;
}

gboolean
lrg_particle_system_raycast (LrgParticleSystem *self,
                             const gfloat      *from,
                             const gfloat      *to,
                             guint32            layer_mask,
                             gfloat            *out_point,
                             gfloat            *out_normal)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);
	g_return_val_if_fail (from != NULL && to != NULL && out_point != NULL && out_normal != NULL, FALSE);

	priv = lrg_particle_system_get_instance_private (self);
	if (priv->raycast == NULL)
		return FALSE;
	return priv->raycast (from, to, layer_mask, out_point, out_normal, priv->raycast_data);
}

guint
lrg_particle_system_spawn (LrgParticleSystem *self,
                           const gfloat      *origin,
                           const gfloat      *inherit_velocity,
                           guint              count)
{
	LrgParticleSystemPrivate *priv;
	LrgParticleModuleContext  ctx;
	guint                     first, n, i;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0);
	g_return_val_if_fail (origin != NULL, 0);

	priv = lrg_particle_system_get_instance_private (self);

	/* The system's own clock and transform, at the given origin; a copy
	 * so a spawn from inside another system's step leaves ours intact */
	fill_context (self, priv->ctx.dt);
	ctx = priv->ctx;
	memcpy (ctx.origin, origin, sizeof ctx.origin);
	memset (ctx.inherit_velocity, 0, sizeof ctx.inherit_velocity);
	ctx.moved = 0.0f;
	first = priv->active_particles->len;
	n = spawn_particles (self, &ctx, count);

	/* the offered velocity is added to every new particle */
	for (i = 0; inherit_velocity != NULL && i < n; i++)
	{
		LrgParticle *p = g_ptr_array_index (priv->active_particles, first + i);

		p->velocity_x += inherit_velocity[0];
		p->velocity_y += inherit_velocity[1];
		p->velocity_z += inherit_velocity[2];
	}
	return n;
}

void
lrg_particle_system_raise_event (LrgParticleSystem *self,
                                 LrgParticle       *particle,
                                 LrgParticleEvent   event,
                                 const gfloat      *point,
                                 const gfloat      *normal)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
	g_return_if_fail (particle != NULL);

	priv = lrg_particle_system_get_instance_private (self);
	priv->ctx.system = self;
	priv->ctx.rng = &priv->rng;
	deliver_event (priv, &priv->ctx, particle, event, point, normal);
}

void
lrg_particle_system_restart (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	lrg_particle_system_stop (self);
	lrg_particle_system_play (self);
	(void) priv;
}

gfloat
lrg_particle_system_get_time (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0.0f);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->module_time;
}

gboolean
lrg_particle_system_is_emitting (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->emitting;
}

gfloat
lrg_particle_system_get_start_delay (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), 0.0f);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->start_delay;
}

void
lrg_particle_system_set_start_delay (LrgParticleSystem *self,
                                     gfloat             delay)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	delay = MAX (delay, 0.0f);
	if (priv->start_delay == delay)
		return;
	priv->start_delay = delay;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_DELAY]);
}

void
lrg_particle_system_get_rotation (LrgParticleSystem *self,
                                  gfloat            *out)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
	g_return_if_fail (out != NULL);

	priv = lrg_particle_system_get_instance_private (self);
	memcpy (out, priv->rotation, sizeof priv->rotation);
}

void
lrg_particle_system_set_rotation (LrgParticleSystem *self,
                                  const gfloat      *rotation)
{
	LrgParticleSystemPrivate *priv;
	gfloat                    len;
	gint                      k;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));
	g_return_if_fail (rotation != NULL);

	priv = lrg_particle_system_get_instance_private (self);

	/* normalized, identity for a degenerate quaternion */
	len = sqrtf (rotation[0] * rotation[0] + rotation[1] * rotation[1] +
	             rotation[2] * rotation[2] + rotation[3] * rotation[3]);
	if (!(len > 1e-6f))
	{
		priv->rotation[0] = priv->rotation[1] = priv->rotation[2] = 0.0f;
		priv->rotation[3] = 1.0f;
		return;
	}
	for (k = 0; k < 4; k++)
		priv->rotation[k] = rotation[k] / len;
}

gboolean
lrg_particle_system_get_local_space (LrgParticleSystem *self)
{
	LrgParticleSystemPrivate *priv;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);

	priv = lrg_particle_system_get_instance_private (self);
	return priv->local_space;
}

void
lrg_particle_system_set_local_space (LrgParticleSystem *self,
                                     gboolean           local_space)
{
	LrgParticleSystemPrivate *priv;

	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (self));

	priv = lrg_particle_system_get_instance_private (self);
	local_space = !!local_space;
	if (priv->local_space == local_space)
		return;
	priv->local_space = local_space;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_SPACE]);
}

gboolean
lrg_particle_system_get_bounds (LrgParticleSystem *self,
                                gfloat            *out_min,
                                gfloat            *out_max)
{
	LrgParticleSystemPrivate *priv;
	gboolean                  any = FALSE;
	guint                     i;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);

	priv = lrg_particle_system_get_instance_private (self);
	out_min[0] = out_max[0] = priv->position_x;
	out_min[1] = out_max[1] = priv->position_y;
	out_min[2] = out_max[2] = priv->position_z;
	for (i = 0; i < priv->active_particles->len; i++)
	{
		const LrgParticle *p = g_ptr_array_index (priv->active_particles, i);
		gfloat             r = fabsf (p->size) * 0.5f;
		gfloat             pos[3];
		gint               k;

		pos[0] = p->position_x;
		pos[1] = p->position_y;
		pos[2] = p->position_z;
		for (k = 0; k < 3; k++)
		{
			if (!any || pos[k] - r < out_min[k])
				out_min[k] = pos[k] - r;
			if (!any || pos[k] + r > out_max[k])
				out_max[k] = pos[k] + r;
		}
		any = TRUE;
	}
	return any;
}
