/* lrg-particle-force.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-particle-force.h"
#include <math.h>

/**
 * SECTION:lrg-particle-force
 * @Title: LrgParticleForce
 * @Short_description: Forces that affect particle motion
 *
 * #LrgParticleForce is the base class for forces that can be applied
 * to particles in a #LrgParticleSystem. Subclasses implement specific
 * force behaviors:
 *
 * - #LrgParticleForceGravity: Constant directional force
 * - #LrgParticleForceWind: Directional force with optional turbulence
 * - #LrgParticleForceAttractor: Pull particles toward a point
 * - #LrgParticleForceTurbulence: Perlin noise-based random forces
 */

/* ==========================================================================
 * Base Force Class
 * ========================================================================== */

typedef struct
{
    gboolean enabled;
    gfloat   strength;
} LrgParticleForcePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgParticleForce, lrg_particle_force, G_TYPE_OBJECT)

enum
{
    PROP_BASE_0,
    PROP_BASE_ENABLED,
    PROP_BASE_STRENGTH,
    N_BASE_PROPS
};

static GParamSpec *base_properties[N_BASE_PROPS];

static void
lrg_particle_force_real_apply (LrgParticleForce *self,
                               LrgParticle      *particle,
                               gfloat            delta_time)
{
    /* Base implementation does nothing */
}

static void
lrg_particle_force_real_update (LrgParticleForce *self,
                                gfloat            delta_time)
{
    /* Base implementation does nothing */
}

static void
lrg_particle_force_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgParticleForce        *self = LRG_PARTICLE_FORCE (object);
    LrgParticleForcePrivate *priv = lrg_particle_force_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_BASE_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_BASE_STRENGTH:
        g_value_set_float (value, priv->strength);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_force_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgParticleForce *self = LRG_PARTICLE_FORCE (object);

    switch (prop_id)
    {
    case PROP_BASE_ENABLED:
        lrg_particle_force_set_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_BASE_STRENGTH:
        lrg_particle_force_set_strength (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_force_class_init (LrgParticleForceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_particle_force_get_property;
    object_class->set_property = lrg_particle_force_set_property;

    klass->apply = lrg_particle_force_real_apply;
    klass->update = lrg_particle_force_real_update;

    base_properties[PROP_BASE_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether force is active",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    base_properties[PROP_BASE_STRENGTH] =
        g_param_spec_float ("strength",
                            "Strength",
                            "Force strength multiplier",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_BASE_PROPS, base_properties);
}

static void
lrg_particle_force_init (LrgParticleForce *self)
{
    LrgParticleForcePrivate *priv = lrg_particle_force_get_instance_private (self);

    priv->enabled = TRUE;
    priv->strength = 1.0f;
}

void
lrg_particle_force_apply (LrgParticleForce *self,
                          LrgParticle      *particle,
                          gfloat            delta_time)
{
    LrgParticleForceClass   *klass;
    LrgParticleForcePrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_FORCE (self));
    g_return_if_fail (particle != NULL);

    priv = lrg_particle_force_get_instance_private (self);

    if (!priv->enabled)
        return;

    klass = LRG_PARTICLE_FORCE_GET_CLASS (self);

    if (klass->apply != NULL)
        klass->apply (self, particle, delta_time);
}

void
lrg_particle_force_update (LrgParticleForce *self,
                           gfloat            delta_time)
{
    LrgParticleForceClass *klass;

    g_return_if_fail (LRG_IS_PARTICLE_FORCE (self));

    klass = LRG_PARTICLE_FORCE_GET_CLASS (self);

    if (klass->update != NULL)
        klass->update (self, delta_time);
}

gboolean
lrg_particle_force_get_enabled (LrgParticleForce *self)
{
    LrgParticleForcePrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE (self), FALSE);

    priv = lrg_particle_force_get_instance_private (self);

    return priv->enabled;
}

void
lrg_particle_force_set_enabled (LrgParticleForce *self,
                                gboolean          enabled)
{
    LrgParticleForcePrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_FORCE (self));

    priv = lrg_particle_force_get_instance_private (self);

    enabled = !!enabled;
    if (priv->enabled == enabled)
        return;

    priv->enabled = enabled;

    g_object_notify_by_pspec (G_OBJECT (self), base_properties[PROP_BASE_ENABLED]);
}

gfloat
lrg_particle_force_get_strength (LrgParticleForce *self)
{
    LrgParticleForcePrivate *priv;

    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE (self), 0.0f);

    priv = lrg_particle_force_get_instance_private (self);

    return priv->strength;
}

void
lrg_particle_force_set_strength (LrgParticleForce *self,
                                 gfloat            strength)
{
    LrgParticleForcePrivate *priv;

    g_return_if_fail (LRG_IS_PARTICLE_FORCE (self));

    priv = lrg_particle_force_get_instance_private (self);

    if (priv->strength == strength)
        return;

    priv->strength = strength;

    g_object_notify_by_pspec (G_OBJECT (self), base_properties[PROP_BASE_STRENGTH]);
}

/* ==========================================================================
 * Gravity Force
 * ========================================================================== */

struct _LrgParticleForceGravity
{
    LrgParticleForce parent_instance;

    gfloat gravity_x;
    gfloat gravity_y;
    gfloat gravity_z;
};

G_DEFINE_FINAL_TYPE (LrgParticleForceGravity, lrg_particle_force_gravity, LRG_TYPE_PARTICLE_FORCE)

static void
lrg_particle_force_gravity_apply (LrgParticleForce *force,
                                  LrgParticle      *particle,
                                  gfloat            delta_time)
{
    LrgParticleForceGravity *self = LRG_PARTICLE_FORCE_GRAVITY (force);
    LrgParticleForcePrivate *priv = lrg_particle_force_get_instance_private (force);
    gfloat strength;
    gfloat ax, ay, az;

    strength = priv->strength;

    ax = self->gravity_x * strength;
    ay = self->gravity_y * strength;
    az = self->gravity_z * strength;

    lrg_particle_apply_force (particle, ax, ay, az, delta_time);
}

static void
lrg_particle_force_gravity_class_init (LrgParticleForceGravityClass *klass)
{
    LrgParticleForceClass *force_class = LRG_PARTICLE_FORCE_CLASS (klass);

    force_class->apply = lrg_particle_force_gravity_apply;
}

static void
lrg_particle_force_gravity_init (LrgParticleForceGravity *self)
{
    /* Default: Earth-like gravity pointing down */
    self->gravity_x = 0.0f;
    self->gravity_y = -9.81f;
    self->gravity_z = 0.0f;
}

LrgParticleForce *
lrg_particle_force_gravity_new (gfloat x,
                                gfloat y,
                                gfloat z)
{
    LrgParticleForceGravity *self;

    self = g_object_new (LRG_TYPE_PARTICLE_FORCE_GRAVITY, NULL);
    self->gravity_x = x;
    self->gravity_y = y;
    self->gravity_z = z;

    return LRG_PARTICLE_FORCE (self);
}

void
lrg_particle_force_gravity_get_direction (LrgParticleForceGravity *self,
                                          gfloat                  *x,
                                          gfloat                  *y,
                                          gfloat                  *z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_GRAVITY (self));

    if (x != NULL)
        *x = self->gravity_x;
    if (y != NULL)
        *y = self->gravity_y;
    if (z != NULL)
        *z = self->gravity_z;
}

void
lrg_particle_force_gravity_set_direction (LrgParticleForceGravity *self,
                                          gfloat                   x,
                                          gfloat                   y,
                                          gfloat                   z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_GRAVITY (self));

    self->gravity_x = x;
    self->gravity_y = y;
    self->gravity_z = z;
}

/* ==========================================================================
 * Wind Force
 * ========================================================================== */

struct _LrgParticleForceWind
{
    LrgParticleForce parent_instance;

    gfloat wind_x;
    gfloat wind_y;
    gfloat wind_z;
    gfloat turbulence;
    gfloat time_offset;
};

G_DEFINE_FINAL_TYPE (LrgParticleForceWind, lrg_particle_force_wind, LRG_TYPE_PARTICLE_FORCE)

static void
lrg_particle_force_wind_apply (LrgParticleForce *force,
                               LrgParticle      *particle,
                               gfloat            delta_time)
{
    LrgParticleForceWind    *self = LRG_PARTICLE_FORCE_WIND (force);
    LrgParticleForcePrivate *priv = lrg_particle_force_get_instance_private (force);
    gfloat strength;
    gfloat wx, wy, wz;
    gfloat turbulence_x, turbulence_y, turbulence_z;

    strength = priv->strength;

    wx = self->wind_x * strength;
    wy = self->wind_y * strength;
    wz = self->wind_z * strength;

    /* Add turbulence noise */
    if (self->turbulence > 0.0001f)
    {
        gfloat t;

        t = self->time_offset + particle->position_x * 0.1f + particle->position_y * 0.1f;
        turbulence_x = sinf (t * 3.7f) * self->turbulence * strength;
        turbulence_y = sinf (t * 2.3f + 1.5f) * self->turbulence * strength;
        turbulence_z = sinf (t * 4.1f + 2.7f) * self->turbulence * strength;

        wx += turbulence_x;
        wy += turbulence_y;
        wz += turbulence_z;
    }

    lrg_particle_apply_force (particle, wx, wy, wz, delta_time);
}

static void
lrg_particle_force_wind_update (LrgParticleForce *force,
                                gfloat            delta_time)
{
    LrgParticleForceWind *self = LRG_PARTICLE_FORCE_WIND (force);

    self->time_offset += delta_time;
}

static void
lrg_particle_force_wind_class_init (LrgParticleForceWindClass *klass)
{
    LrgParticleForceClass *force_class = LRG_PARTICLE_FORCE_CLASS (klass);

    force_class->apply = lrg_particle_force_wind_apply;
    force_class->update = lrg_particle_force_wind_update;
}

static void
lrg_particle_force_wind_init (LrgParticleForceWind *self)
{
    self->wind_x = 1.0f;
    self->wind_y = 0.0f;
    self->wind_z = 0.0f;
    self->turbulence = 0.0f;
    self->time_offset = 0.0f;
}

LrgParticleForce *
lrg_particle_force_wind_new (gfloat x,
                             gfloat y,
                             gfloat z)
{
    LrgParticleForceWind *self;

    self = g_object_new (LRG_TYPE_PARTICLE_FORCE_WIND, NULL);
    self->wind_x = x;
    self->wind_y = y;
    self->wind_z = z;

    return LRG_PARTICLE_FORCE (self);
}

void
lrg_particle_force_wind_get_direction (LrgParticleForceWind *self,
                                       gfloat               *x,
                                       gfloat               *y,
                                       gfloat               *z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_WIND (self));

    if (x != NULL)
        *x = self->wind_x;
    if (y != NULL)
        *y = self->wind_y;
    if (z != NULL)
        *z = self->wind_z;
}

void
lrg_particle_force_wind_set_direction (LrgParticleForceWind *self,
                                       gfloat                x,
                                       gfloat                y,
                                       gfloat                z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_WIND (self));

    self->wind_x = x;
    self->wind_y = y;
    self->wind_z = z;
}

gfloat
lrg_particle_force_wind_get_turbulence (LrgParticleForceWind *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_WIND (self), 0.0f);

    return self->turbulence;
}

void
lrg_particle_force_wind_set_turbulence (LrgParticleForceWind *self,
                                        gfloat                turbulence)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_WIND (self));

    self->turbulence = MAX (0.0f, turbulence);
}

/* ==========================================================================
 * Attractor Force
 * ========================================================================== */

struct _LrgParticleForceAttractor
{
    LrgParticleForce parent_instance;

    gfloat pos_x;
    gfloat pos_y;
    gfloat pos_z;
    gfloat radius;
    gfloat falloff;
};

G_DEFINE_FINAL_TYPE (LrgParticleForceAttractor, lrg_particle_force_attractor, LRG_TYPE_PARTICLE_FORCE)

static void
lrg_particle_force_attractor_apply (LrgParticleForce *force,
                                    LrgParticle      *particle,
                                    gfloat            delta_time)
{
    LrgParticleForceAttractor *self = LRG_PARTICLE_FORCE_ATTRACTOR (force);
    LrgParticleForcePrivate   *priv = lrg_particle_force_get_instance_private (force);
    gfloat dx, dy, dz;
    gfloat dist_sq, dist;
    gfloat strength;
    gfloat factor;
    gfloat force_x, force_y, force_z;

    /* Vector from particle to attractor */
    dx = self->pos_x - particle->position_x;
    dy = self->pos_y - particle->position_y;
    dz = self->pos_z - particle->position_z;

    dist_sq = dx * dx + dy * dy + dz * dz;

    /* Skip if outside radius or at attractor position */
    if (dist_sq > self->radius * self->radius || dist_sq < 0.0001f)
        return;

    dist = sqrtf (dist_sq);
    strength = priv->strength;

    /* Calculate falloff */
    factor = powf (1.0f - (dist / self->radius), self->falloff);

    /* Normalize direction and apply force */
    force_x = (dx / dist) * factor * strength;
    force_y = (dy / dist) * factor * strength;
    force_z = (dz / dist) * factor * strength;

    lrg_particle_apply_force (particle, force_x, force_y, force_z, delta_time);
}

static void
lrg_particle_force_attractor_class_init (LrgParticleForceAttractorClass *klass)
{
    LrgParticleForceClass *force_class = LRG_PARTICLE_FORCE_CLASS (klass);

    force_class->apply = lrg_particle_force_attractor_apply;
}

static void
lrg_particle_force_attractor_init (LrgParticleForceAttractor *self)
{
    self->pos_x = 0.0f;
    self->pos_y = 0.0f;
    self->pos_z = 0.0f;
    self->radius = 10.0f;
    self->falloff = 1.0f;  /* Linear falloff */
}

LrgParticleForce *
lrg_particle_force_attractor_new (gfloat x,
                                  gfloat y,
                                  gfloat z,
                                  gfloat radius)
{
    LrgParticleForceAttractor *self;

    self = g_object_new (LRG_TYPE_PARTICLE_FORCE_ATTRACTOR, NULL);
    self->pos_x = x;
    self->pos_y = y;
    self->pos_z = z;
    self->radius = MAX (0.001f, radius);

    return LRG_PARTICLE_FORCE (self);
}

void
lrg_particle_force_attractor_get_position (LrgParticleForceAttractor *self,
                                           gfloat                    *x,
                                           gfloat                    *y,
                                           gfloat                    *z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self));

    if (x != NULL)
        *x = self->pos_x;
    if (y != NULL)
        *y = self->pos_y;
    if (z != NULL)
        *z = self->pos_z;
}

void
lrg_particle_force_attractor_set_position (LrgParticleForceAttractor *self,
                                           gfloat                     x,
                                           gfloat                     y,
                                           gfloat                     z)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self));

    self->pos_x = x;
    self->pos_y = y;
    self->pos_z = z;
}

gfloat
lrg_particle_force_attractor_get_radius (LrgParticleForceAttractor *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self), 0.0f);

    return self->radius;
}

void
lrg_particle_force_attractor_set_radius (LrgParticleForceAttractor *self,
                                         gfloat                     radius)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self));

    self->radius = MAX (0.001f, radius);
}

gfloat
lrg_particle_force_attractor_get_falloff (LrgParticleForceAttractor *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self), 0.0f);

    return self->falloff;
}

void
lrg_particle_force_attractor_set_falloff (LrgParticleForceAttractor *self,
                                          gfloat                     falloff)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_ATTRACTOR (self));

    self->falloff = MAX (0.0f, falloff);
}

/* ==========================================================================
 * Turbulence Force
 * ========================================================================== */

struct _LrgParticleForceTurbulence
{
    LrgParticleForce parent_instance;

    gfloat frequency;
    gfloat amplitude;
    gfloat scroll_speed;
    gfloat time_offset;
};

G_DEFINE_FINAL_TYPE (LrgParticleForceTurbulence, lrg_particle_force_turbulence, LRG_TYPE_PARTICLE_FORCE)

/*
 * Simple gradient noise function.
 * For production, consider using a proper Perlin/Simplex noise implementation.
 */
static gfloat
noise3d (gfloat x, gfloat y, gfloat z)
{
    gfloat n;

    /* Simple pseudo-random noise based on position */
    n = sinf (x * 12.9898f + y * 78.233f + z * 37.719f);
    n = sinf (n * 43758.5453f);

    return n;
}

static void
lrg_particle_force_turbulence_apply (LrgParticleForce *force,
                                     LrgParticle      *particle,
                                     gfloat            delta_time)
{
    LrgParticleForceTurbulence *self = LRG_PARTICLE_FORCE_TURBULENCE (force);
    LrgParticleForcePrivate    *priv = lrg_particle_force_get_instance_private (force);
    gfloat strength;
    gfloat fx, fy, fz;
    gfloat px, py, pz;

    strength = priv->strength * self->amplitude;

    /* Sample position with time offset for animation */
    px = particle->position_x * self->frequency + self->time_offset;
    py = particle->position_y * self->frequency;
    pz = particle->position_z * self->frequency;

    /* Sample noise at three offset positions to get 3D force vector */
    fx = noise3d (px, py, pz) * strength;
    fy = noise3d (px + 17.3f, py + 29.7f, pz + 41.1f) * strength;
    fz = noise3d (px + 67.2f, py + 83.5f, pz + 97.9f) * strength;

    lrg_particle_apply_force (particle, fx, fy, fz, delta_time);
}

static void
lrg_particle_force_turbulence_update (LrgParticleForce *force,
                                      gfloat            delta_time)
{
    LrgParticleForceTurbulence *self = LRG_PARTICLE_FORCE_TURBULENCE (force);

    self->time_offset += delta_time * self->scroll_speed;
}

static void
lrg_particle_force_turbulence_class_init (LrgParticleForceTurbulenceClass *klass)
{
    LrgParticleForceClass *force_class = LRG_PARTICLE_FORCE_CLASS (klass);

    force_class->apply = lrg_particle_force_turbulence_apply;
    force_class->update = lrg_particle_force_turbulence_update;
}

static void
lrg_particle_force_turbulence_init (LrgParticleForceTurbulence *self)
{
    self->frequency = 1.0f;
    self->amplitude = 1.0f;
    self->scroll_speed = 1.0f;
    self->time_offset = 0.0f;
}

LrgParticleForce *
lrg_particle_force_turbulence_new (gfloat frequency,
                                   gfloat amplitude)
{
    LrgParticleForceTurbulence *self;

    self = g_object_new (LRG_TYPE_PARTICLE_FORCE_TURBULENCE, NULL);
    self->frequency = MAX (0.001f, frequency);
    self->amplitude = amplitude;

    return LRG_PARTICLE_FORCE (self);
}

gfloat
lrg_particle_force_turbulence_get_frequency (LrgParticleForceTurbulence *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self), 0.0f);

    return self->frequency;
}

void
lrg_particle_force_turbulence_set_frequency (LrgParticleForceTurbulence *self,
                                             gfloat                      frequency)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self));

    self->frequency = MAX (0.001f, frequency);
}

gfloat
lrg_particle_force_turbulence_get_amplitude (LrgParticleForceTurbulence *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self), 0.0f);

    return self->amplitude;
}

void
lrg_particle_force_turbulence_set_amplitude (LrgParticleForceTurbulence *self,
                                             gfloat                      amplitude)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self));

    self->amplitude = amplitude;
}

gfloat
lrg_particle_force_turbulence_get_scroll_speed (LrgParticleForceTurbulence *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self), 0.0f);

    return self->scroll_speed;
}

void
lrg_particle_force_turbulence_set_scroll_speed (LrgParticleForceTurbulence *self,
                                                gfloat                      speed)
{
    g_return_if_fail (LRG_IS_PARTICLE_FORCE_TURBULENCE (self));

    self->scroll_speed = speed;
}
