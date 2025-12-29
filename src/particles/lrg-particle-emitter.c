/* lrg-particle-emitter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-particle-emitter.h"
#include <math.h>

/**
 * SECTION:lrg-particle-emitter
 * @Title: LrgParticleEmitter
 * @Short_description: Particle emission configuration
 *
 * #LrgParticleEmitter defines how particles are spawned in a #LrgParticleSystem.
 * It configures:
 *
 * - Emission rate (particles per second)
 * - Emission shape (point, circle, rectangle, cone)
 * - Initial particle properties (speed, lifetime, size, color)
 *
 * The emitter can be subclassed to provide custom emission patterns.
 */

typedef struct
{
    /* Emission settings */
    gfloat           emission_rate;      /* Particles per second */
    LrgEmissionShape emission_shape;
    gboolean         enabled;

    /* Emitter position and direction */
    gfloat           position_x;
    gfloat           position_y;
    gfloat           position_z;
    gfloat           direction_x;
    gfloat           direction_y;
    gfloat           direction_z;

    /* Shape parameters */
    gfloat           spread_angle;       /* For cone */
    gfloat           shape_radius;       /* For circle/sphere/cone */
    gfloat           shape_width;        /* For rectangle */
    gfloat           shape_height;
    gfloat           shape_depth;

    /* Particle initial values (min/max for randomization) */
    gfloat           speed_min;
    gfloat           speed_max;
    gfloat           lifetime_min;
    gfloat           lifetime_max;
    gfloat           size_min;
    gfloat           size_max;

    /* Color gradient */
    gfloat           start_color_r;
    gfloat           start_color_g;
    gfloat           start_color_b;
    gfloat           start_color_a;
    gfloat           end_color_r;
    gfloat           end_color_g;
    gfloat           end_color_b;
    gfloat           end_color_a;

    /* Internal state */
    gfloat           accumulated_time;
    gfloat           emission_interval;
} LrgParticleEmitterPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgParticleEmitter, lrg_particle_emitter, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_EMISSION_RATE,
    PROP_EMISSION_SHAPE,
    PROP_ENABLED,
    PROP_SPREAD_ANGLE,
    PROP_SHAPE_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Helper function to generate a random float in range [min, max].
 */
static gfloat
random_range (gfloat min, gfloat max)
{
    if (min >= max)
        return min;
    return min + (gfloat)g_random_double () * (max - min);
}

/*
 * Helper function to normalize a vector.
 */
static void
normalize_vector (gfloat *x, gfloat *y, gfloat *z)
{
    gfloat len;

    len = sqrtf ((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (len > 0.0001f)
    {
        *x /= len;
        *y /= len;
        *z /= len;
    }
}

/*
 * Generate a random direction within a cone around the given direction.
 */
static void
random_direction_in_cone (gfloat  dir_x,
                          gfloat  dir_y,
                          gfloat  dir_z,
                          gfloat  angle,
                          gfloat *out_x,
                          gfloat *out_y,
                          gfloat *out_z)
{
    gfloat cos_angle;
    gfloat sin_angle;
    gfloat phi;
    gfloat cos_theta;
    gfloat sin_theta;
    gfloat x, y, z;
    gfloat up_x, up_y, up_z;
    gfloat right_x, right_y, right_z;
    gfloat len;

    /* Random angle within cone */
    cos_angle = 1.0f - (gfloat)g_random_double () * (1.0f - cosf (angle));
    sin_angle = sqrtf (1.0f - cos_angle * cos_angle);
    phi = (gfloat)g_random_double () * 2.0f * G_PI;

    /* Generate local coordinate system */
    /* Find a vector not parallel to direction */
    if (fabsf (dir_y) < 0.9f)
    {
        up_x = 0.0f;
        up_y = 1.0f;
        up_z = 0.0f;
    }
    else
    {
        up_x = 1.0f;
        up_y = 0.0f;
        up_z = 0.0f;
    }

    /* Cross product: right = dir x up */
    right_x = dir_y * up_z - dir_z * up_y;
    right_y = dir_z * up_x - dir_x * up_z;
    right_z = dir_x * up_y - dir_y * up_x;
    len = sqrtf (right_x * right_x + right_y * right_y + right_z * right_z);
    if (len > 0.0001f)
    {
        right_x /= len;
        right_y /= len;
        right_z /= len;
    }

    /* Cross product: up = right x dir */
    up_x = right_y * dir_z - right_z * dir_y;
    up_y = right_z * dir_x - right_x * dir_z;
    up_z = right_x * dir_y - right_y * dir_x;

    /* Build rotated direction */
    cos_theta = cosf (phi);
    sin_theta = sinf (phi);

    x = dir_x * cos_angle +
        (right_x * cos_theta + up_x * sin_theta) * sin_angle;
    y = dir_y * cos_angle +
        (right_y * cos_theta + up_y * sin_theta) * sin_angle;
    z = dir_z * cos_angle +
        (right_z * cos_theta + up_z * sin_theta) * sin_angle;

    normalize_vector (&x, &y, &z);

    *out_x = x;
    *out_y = y;
    *out_z = z;
}

static void
lrg_particle_emitter_real_emit (LrgParticleEmitter *self,
                                LrgParticle        *out_particle)
{
    LrgParticleEmitterPrivate *priv = lrg_particle_emitter_get_instance_private (self);
    gfloat pos_x, pos_y, pos_z;
    gfloat vel_x, vel_y, vel_z;
    gfloat speed;
    gfloat angle;
    gfloat radius;
    gfloat dir_x, dir_y, dir_z;

    /* Calculate spawn position based on shape */
    pos_x = priv->position_x;
    pos_y = priv->position_y;
    pos_z = priv->position_z;

    switch (priv->emission_shape)
    {
    case LRG_EMISSION_SHAPE_POINT:
        /* Position is already set */
        break;

    case LRG_EMISSION_SHAPE_CIRCLE:
        /* Random position on circle/sphere surface */
        angle = (gfloat)g_random_double () * 2.0f * G_PI;
        radius = priv->shape_radius * sqrtf ((gfloat)g_random_double ());
        pos_x += cosf (angle) * radius;
        pos_y += sinf (angle) * radius;
        break;

    case LRG_EMISSION_SHAPE_RECTANGLE:
        /* Random position in rectangle */
        pos_x += random_range (-priv->shape_width * 0.5f, priv->shape_width * 0.5f);
        pos_y += random_range (-priv->shape_height * 0.5f, priv->shape_height * 0.5f);
        pos_z += random_range (-priv->shape_depth * 0.5f, priv->shape_depth * 0.5f);
        break;

    case LRG_EMISSION_SHAPE_CONE:
    case LRG_EMISSION_SHAPE_MESH:
        /* Default to point for cone (velocity handles the cone shape) */
        break;
    }

    /* Calculate velocity direction */
    dir_x = priv->direction_x;
    dir_y = priv->direction_y;
    dir_z = priv->direction_z;
    normalize_vector (&dir_x, &dir_y, &dir_z);

    if (priv->emission_shape == LRG_EMISSION_SHAPE_CONE && priv->spread_angle > 0.0001f)
    {
        random_direction_in_cone (dir_x, dir_y, dir_z, priv->spread_angle,
                                  &vel_x, &vel_y, &vel_z);
    }
    else
    {
        vel_x = dir_x;
        vel_y = dir_y;
        vel_z = dir_z;
    }

    /* Apply speed */
    speed = random_range (priv->speed_min, priv->speed_max);
    vel_x *= speed;
    vel_y *= speed;
    vel_z *= speed;

    /* Initialize particle */
    lrg_particle_spawn (out_particle,
                        pos_x, pos_y, pos_z,
                        random_range (priv->lifetime_min, priv->lifetime_max));

    /* Set velocity */
    out_particle->velocity_x = vel_x;
    out_particle->velocity_y = vel_y;
    out_particle->velocity_z = vel_z;

    /* Set size and color */
    out_particle->size = random_range (priv->size_min, priv->size_max);
    out_particle->color_r = priv->start_color_r;
    out_particle->color_g = priv->start_color_g;
    out_particle->color_b = priv->start_color_b;
    out_particle->color_a = priv->start_color_a;
}

static void
lrg_particle_emitter_real_burst (LrgParticleEmitter *self,
                                 LrgParticle        *particles,
                                 guint               count)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        lrg_particle_emitter_emit (self, &particles[i]);
    }
}

static void
lrg_particle_emitter_real_update (LrgParticleEmitter *self,
                                  gfloat              delta_time)
{
    LrgParticleEmitterPrivate *priv = lrg_particle_emitter_get_instance_private (self);

    if (!priv->enabled)
        return;

    priv->accumulated_time += delta_time;
}

static void
lrg_particle_emitter_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgParticleEmitter        *self = LRG_PARTICLE_EMITTER (object);
    LrgParticleEmitterPrivate *priv = lrg_particle_emitter_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_EMISSION_RATE:
        g_value_set_float (value, priv->emission_rate);
        break;
    case PROP_EMISSION_SHAPE:
        g_value_set_enum (value, priv->emission_shape);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_SPREAD_ANGLE:
        g_value_set_float (value, priv->spread_angle);
        break;
    case PROP_SHAPE_RADIUS:
        g_value_set_float (value, priv->shape_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_emitter_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgParticleEmitter *self = LRG_PARTICLE_EMITTER (object);

    switch (prop_id)
    {
    case PROP_EMISSION_RATE:
        lrg_particle_emitter_set_emission_rate (self, g_value_get_float (value));
        break;
    case PROP_EMISSION_SHAPE:
        lrg_particle_emitter_set_emission_shape (self, g_value_get_enum (value));
        break;
    case PROP_ENABLED:
        lrg_particle_emitter_set_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_SPREAD_ANGLE:
        lrg_particle_emitter_set_spread_angle (self, g_value_get_float (value));
        break;
    case PROP_SHAPE_RADIUS:
        lrg_particle_emitter_set_shape_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_emitter_class_init (LrgParticleEmitterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_particle_emitter_get_property;
    object_class->set_property = lrg_particle_emitter_set_property;

    /* Virtual methods */
    klass->emit = lrg_particle_emitter_real_emit;
    klass->burst = lrg_particle_emitter_real_burst;
    klass->update = lrg_particle_emitter_real_update;

    /**
     * LrgParticleEmitter:emission-rate:
     *
     * Particles emitted per second.
     */
    properties[PROP_EMISSION_RATE] =
        g_param_spec_float ("emission-rate",
                            "Emission Rate",
                            "Particles per second",
                            0.0f, G_MAXFLOAT, 10.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleEmitter:emission-shape:
     *
     * The shape from which particles are emitted.
     */
    properties[PROP_EMISSION_SHAPE] =
        g_param_spec_enum ("emission-shape",
                           "Emission Shape",
                           "Shape of emission area",
                           LRG_TYPE_EMISSION_SHAPE,
                           LRG_EMISSION_SHAPE_POINT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleEmitter:enabled:
     *
     * Whether the emitter is actively emitting.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether emitter is active",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleEmitter:spread-angle:
     *
     * Spread angle in radians for cone emissions.
     */
    properties[PROP_SPREAD_ANGLE] =
        g_param_spec_float ("spread-angle",
                            "Spread Angle",
                            "Cone spread angle in radians",
                            0.0f, G_PI, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticleEmitter:shape-radius:
     *
     * Radius for circle/sphere/cone shapes.
     */
    properties[PROP_SHAPE_RADIUS] =
        g_param_spec_float ("shape-radius",
                            "Shape Radius",
                            "Radius for circular shapes",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_particle_emitter_init (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv = lrg_particle_emitter_get_instance_private (self);

    /* Default emission settings */
    priv->emission_rate = 10.0f;
    priv->emission_shape = LRG_EMISSION_SHAPE_POINT;
    priv->enabled = TRUE;

    /* Default position and direction */
    priv->position_x = 0.0f;
    priv->position_y = 0.0f;
    priv->position_z = 0.0f;
    priv->direction_x = 0.0f;
    priv->direction_y = 1.0f;  /* Up by default */
    priv->direction_z = 0.0f;

    /* Default shape parameters */
    priv->spread_angle = 0.5f;  /* About 28 degrees */
    priv->shape_radius = 1.0f;
    priv->shape_width = 1.0f;
    priv->shape_height = 1.0f;
    priv->shape_depth = 0.0f;

    /* Default particle ranges */
    priv->speed_min = 1.0f;
    priv->speed_max = 2.0f;
    priv->lifetime_min = 1.0f;
    priv->lifetime_max = 2.0f;
    priv->size_min = 0.1f;
    priv->size_max = 0.2f;

    /* Default colors (white, fade out) */
    priv->start_color_r = 1.0f;
    priv->start_color_g = 1.0f;
    priv->start_color_b = 1.0f;
    priv->start_color_a = 1.0f;
    priv->end_color_r = 1.0f;
    priv->end_color_g = 1.0f;
    priv->end_color_b = 1.0f;
    priv->end_color_a = 0.0f;

    /* Internal state */
    priv->accumulated_time = 0.0f;
    priv->emission_interval = 1.0f / priv->emission_rate;
}

LrgParticleEmitter *
lrg_particle_emitter_new (void)
{
    return g_object_new (LRG_TYPE_PARTICLE_EMITTER, NULL);
}

void
lrg_particle_emitter_emit (LrgParticleEmitter *self,
                           LrgParticle        *out_particle)
{
    LrgParticleEmitterClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));
    g_return_if_fail (out_particle != NULL);

    klass = LRG_PARTICLE_EMITTER_GET_CLASS (self);

    if (klass->emit != NULL)
        klass->emit (self, out_particle);
}

void
lrg_particle_emitter_burst (LrgParticleEmitter *self,
                            LrgParticle        *particles,
                            guint               count)
{
    LrgParticleEmitterClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));
    g_return_if_fail (particles != NULL || count == 0);

    klass = LRG_PARTICLE_EMITTER_GET_CLASS (self);

    if (klass->burst != NULL)
        klass->burst (self, particles, count);
}

void
lrg_particle_emitter_update (LrgParticleEmitter *self,
                             gfloat              delta_time)
{
    LrgParticleEmitterClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    klass = LRG_PARTICLE_EMITTER_GET_CLASS (self);

    if (klass->update != NULL)
        klass->update (self, delta_time);
}

gboolean
lrg_particle_emitter_should_emit (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), FALSE);

    priv = lrg_particle_emitter_get_instance_private (self);

    if (!priv->enabled)
        return FALSE;

    if (priv->accumulated_time >= priv->emission_interval)
    {
        priv->accumulated_time -= priv->emission_interval;
        return TRUE;
    }

    return FALSE;
}

gfloat
lrg_particle_emitter_get_emission_rate (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), 0.0f);

    priv = lrg_particle_emitter_get_instance_private (self);

    return priv->emission_rate;
}

void
lrg_particle_emitter_set_emission_rate (LrgParticleEmitter *self,
                                        gfloat              rate)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (priv->emission_rate == rate)
        return;

    priv->emission_rate = rate;
    priv->emission_interval = (rate > 0.0001f) ? (1.0f / rate) : 1000000.0f;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EMISSION_RATE]);
}

LrgEmissionShape
lrg_particle_emitter_get_emission_shape (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), LRG_EMISSION_SHAPE_POINT);

    priv = lrg_particle_emitter_get_instance_private (self);

    return priv->emission_shape;
}

void
lrg_particle_emitter_set_emission_shape (LrgParticleEmitter *self,
                                         LrgEmissionShape    shape)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (priv->emission_shape == shape)
        return;

    priv->emission_shape = shape;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EMISSION_SHAPE]);
}

void
lrg_particle_emitter_get_position (LrgParticleEmitter *self,
                                   gfloat             *x,
                                   gfloat             *y,
                                   gfloat             *z)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (x != NULL)
        *x = priv->position_x;
    if (y != NULL)
        *y = priv->position_y;
    if (z != NULL)
        *z = priv->position_z;
}

void
lrg_particle_emitter_set_position (LrgParticleEmitter *self,
                                   gfloat              x,
                                   gfloat              y,
                                   gfloat              z)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->position_x = x;
    priv->position_y = y;
    priv->position_z = z;
}

void
lrg_particle_emitter_get_direction (LrgParticleEmitter *self,
                                    gfloat             *x,
                                    gfloat             *y,
                                    gfloat             *z)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (x != NULL)
        *x = priv->direction_x;
    if (y != NULL)
        *y = priv->direction_y;
    if (z != NULL)
        *z = priv->direction_z;
}

void
lrg_particle_emitter_set_direction (LrgParticleEmitter *self,
                                    gfloat              x,
                                    gfloat              y,
                                    gfloat              z)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->direction_x = x;
    priv->direction_y = y;
    priv->direction_z = z;
}

gfloat
lrg_particle_emitter_get_spread_angle (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), 0.0f);

    priv = lrg_particle_emitter_get_instance_private (self);

    return priv->spread_angle;
}

void
lrg_particle_emitter_set_spread_angle (LrgParticleEmitter *self,
                                       gfloat              angle)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (priv->spread_angle == angle)
        return;

    priv->spread_angle = CLAMP (angle, 0.0f, G_PI);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPREAD_ANGLE]);
}

gfloat
lrg_particle_emitter_get_shape_radius (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), 0.0f);

    priv = lrg_particle_emitter_get_instance_private (self);

    return priv->shape_radius;
}

void
lrg_particle_emitter_set_shape_radius (LrgParticleEmitter *self,
                                       gfloat              radius)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (priv->shape_radius == radius)
        return;

    priv->shape_radius = MAX (0.0f, radius);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHAPE_RADIUS]);
}

void
lrg_particle_emitter_get_shape_size (LrgParticleEmitter *self,
                                     gfloat             *width,
                                     gfloat             *height,
                                     gfloat             *depth)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (width != NULL)
        *width = priv->shape_width;
    if (height != NULL)
        *height = priv->shape_height;
    if (depth != NULL)
        *depth = priv->shape_depth;
}

void
lrg_particle_emitter_set_shape_size (LrgParticleEmitter *self,
                                     gfloat              width,
                                     gfloat              height,
                                     gfloat              depth)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->shape_width = MAX (0.0f, width);
    priv->shape_height = MAX (0.0f, height);
    priv->shape_depth = MAX (0.0f, depth);
}

void
lrg_particle_emitter_get_initial_speed (LrgParticleEmitter *self,
                                        gfloat             *min,
                                        gfloat             *max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (min != NULL)
        *min = priv->speed_min;
    if (max != NULL)
        *max = priv->speed_max;
}

void
lrg_particle_emitter_set_initial_speed (LrgParticleEmitter *self,
                                        gfloat              min,
                                        gfloat              max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->speed_min = MAX (0.0f, min);
    priv->speed_max = MAX (priv->speed_min, max);
}

void
lrg_particle_emitter_get_initial_lifetime (LrgParticleEmitter *self,
                                           gfloat             *min,
                                           gfloat             *max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (min != NULL)
        *min = priv->lifetime_min;
    if (max != NULL)
        *max = priv->lifetime_max;
}

void
lrg_particle_emitter_set_initial_lifetime (LrgParticleEmitter *self,
                                           gfloat              min,
                                           gfloat              max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->lifetime_min = MAX (0.001f, min);
    priv->lifetime_max = MAX (priv->lifetime_min, max);
}

void
lrg_particle_emitter_get_initial_size (LrgParticleEmitter *self,
                                       gfloat             *min,
                                       gfloat             *max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (min != NULL)
        *min = priv->size_min;
    if (max != NULL)
        *max = priv->size_max;
}

void
lrg_particle_emitter_set_initial_size (LrgParticleEmitter *self,
                                       gfloat              min,
                                       gfloat              max)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->size_min = MAX (0.001f, min);
    priv->size_max = MAX (priv->size_min, max);
}

void
lrg_particle_emitter_get_start_color (LrgParticleEmitter *self,
                                      gfloat             *r,
                                      gfloat             *g,
                                      gfloat             *b,
                                      gfloat             *a)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (r != NULL)
        *r = priv->start_color_r;
    if (g != NULL)
        *g = priv->start_color_g;
    if (b != NULL)
        *b = priv->start_color_b;
    if (a != NULL)
        *a = priv->start_color_a;
}

void
lrg_particle_emitter_set_start_color (LrgParticleEmitter *self,
                                      gfloat              r,
                                      gfloat              g,
                                      gfloat              b,
                                      gfloat              a)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->start_color_r = CLAMP (r, 0.0f, 1.0f);
    priv->start_color_g = CLAMP (g, 0.0f, 1.0f);
    priv->start_color_b = CLAMP (b, 0.0f, 1.0f);
    priv->start_color_a = CLAMP (a, 0.0f, 1.0f);
}

void
lrg_particle_emitter_get_end_color (LrgParticleEmitter *self,
                                    gfloat             *r,
                                    gfloat             *g,
                                    gfloat             *b,
                                    gfloat             *a)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    if (r != NULL)
        *r = priv->end_color_r;
    if (g != NULL)
        *g = priv->end_color_g;
    if (b != NULL)
        *b = priv->end_color_b;
    if (a != NULL)
        *a = priv->end_color_a;
}

void
lrg_particle_emitter_set_end_color (LrgParticleEmitter *self,
                                    gfloat              r,
                                    gfloat              g,
                                    gfloat              b,
                                    gfloat              a)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->end_color_r = CLAMP (r, 0.0f, 1.0f);
    priv->end_color_g = CLAMP (g, 0.0f, 1.0f);
    priv->end_color_b = CLAMP (b, 0.0f, 1.0f);
    priv->end_color_a = CLAMP (a, 0.0f, 1.0f);
}

gboolean
lrg_particle_emitter_get_enabled (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_EMITTER (self), FALSE);

    priv = lrg_particle_emitter_get_instance_private (self);

    return priv->enabled;
}

void
lrg_particle_emitter_set_enabled (LrgParticleEmitter *self,
                                  gboolean            enabled)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    enabled = !!enabled;
    if (priv->enabled == enabled)
        return;

    priv->enabled = enabled;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

void
lrg_particle_emitter_reset (LrgParticleEmitter *self)
{
    LrgParticleEmitterPrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_EMITTER (self));

    priv = lrg_particle_emitter_get_instance_private (self);

    priv->accumulated_time = 0.0f;
}
