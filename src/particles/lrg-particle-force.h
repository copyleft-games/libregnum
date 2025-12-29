/* lrg-particle-force.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Force fields that affect particle motion.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-particle.h"

G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_FORCE (lrg_particle_force_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgParticleForce, lrg_particle_force, LRG, PARTICLE_FORCE, GObject)

/**
 * LrgParticleForceClass:
 * @parent_class: Parent class
 * @apply: Virtual method to apply force to a particle
 * @update: Virtual method called each frame
 *
 * The class structure for #LrgParticleForce.
 */
struct _LrgParticleForceClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgParticleForceClass::apply:
     * @self: A #LrgParticleForce
     * @particle: The particle to affect
     * @delta_time: Time step in seconds
     *
     * Applies the force to a single particle.
     */
    void    (*apply)    (LrgParticleForce    *self,
                         LrgParticle         *particle,
                         gfloat               delta_time);

    /**
     * LrgParticleForceClass::update:
     * @self: A #LrgParticleForce
     * @delta_time: Time since last frame
     *
     * Updates force internal state (e.g., for animated forces).
     */
    void    (*update)   (LrgParticleForce    *self,
                         gfloat               delta_time);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_particle_force_apply:
 * @self: A #LrgParticleForce
 * @particle: Particle to affect
 * @delta_time: Time step
 *
 * Applies this force to a particle.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_apply                (LrgParticleForce    *self,
                                                                 LrgParticle         *particle,
                                                                 gfloat               delta_time);

/**
 * lrg_particle_force_update:
 * @self: A #LrgParticleForce
 * @delta_time: Time since last frame
 *
 * Updates the force state.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_update               (LrgParticleForce    *self,
                                                                 gfloat               delta_time);

/**
 * lrg_particle_force_get_enabled:
 * @self: A #LrgParticleForce
 *
 * Checks if the force is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_force_get_enabled          (LrgParticleForce    *self);

/**
 * lrg_particle_force_set_enabled:
 * @self: A #LrgParticleForce
 * @enabled: Whether to enable the force
 *
 * Enables or disables the force.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_set_enabled          (LrgParticleForce    *self,
                                                                 gboolean             enabled);

/**
 * lrg_particle_force_get_strength:
 * @self: A #LrgParticleForce
 *
 * Gets the force strength multiplier.
 *
 * Returns: Strength multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_get_strength         (LrgParticleForce    *self);

/**
 * lrg_particle_force_set_strength:
 * @self: A #LrgParticleForce
 * @strength: Strength multiplier
 *
 * Sets the force strength multiplier.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_set_strength         (LrgParticleForce    *self,
                                                                 gfloat               strength);

/* ============================================================================
 * Gravity Force
 * ============================================================================ */

#define LRG_TYPE_PARTICLE_FORCE_GRAVITY (lrg_particle_force_gravity_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleForceGravity, lrg_particle_force_gravity, LRG, PARTICLE_FORCE_GRAVITY, LrgParticleForce)

/**
 * lrg_particle_force_gravity_new:
 * @x: X component of gravity
 * @y: Y component of gravity
 * @z: Z component of gravity
 *
 * Creates a new gravity force.
 *
 * Returns: (transfer full): A new #LrgParticleForceGravity
 */
LRG_AVAILABLE_IN_ALL
LrgParticleForce *      lrg_particle_force_gravity_new          (gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_force_gravity_get_direction:
 * @self: A #LrgParticleForceGravity
 * @x: (out) (nullable): X component
 * @y: (out) (nullable): Y component
 * @z: (out) (nullable): Z component
 *
 * Gets the gravity direction.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_gravity_get_direction (LrgParticleForceGravity *self,
                                                                  gfloat                  *x,
                                                                  gfloat                  *y,
                                                                  gfloat                  *z);

/**
 * lrg_particle_force_gravity_set_direction:
 * @self: A #LrgParticleForceGravity
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets the gravity direction.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_gravity_set_direction (LrgParticleForceGravity *self,
                                                                  gfloat                   x,
                                                                  gfloat                   y,
                                                                  gfloat                   z);

/* ============================================================================
 * Wind Force
 * ============================================================================ */

#define LRG_TYPE_PARTICLE_FORCE_WIND (lrg_particle_force_wind_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleForceWind, lrg_particle_force_wind, LRG, PARTICLE_FORCE_WIND, LrgParticleForce)

/**
 * lrg_particle_force_wind_new:
 * @x: X component of wind direction
 * @y: Y component of wind direction
 * @z: Z component of wind direction
 *
 * Creates a new wind force.
 *
 * Returns: (transfer full): A new #LrgParticleForceWind
 */
LRG_AVAILABLE_IN_ALL
LrgParticleForce *      lrg_particle_force_wind_new             (gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_force_wind_get_direction:
 * @self: A #LrgParticleForceWind
 * @x: (out) (nullable): X component
 * @y: (out) (nullable): Y component
 * @z: (out) (nullable): Z component
 *
 * Gets the wind direction.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_wind_get_direction   (LrgParticleForceWind    *self,
                                                                 gfloat                  *x,
                                                                 gfloat                  *y,
                                                                 gfloat                  *z);

/**
 * lrg_particle_force_wind_set_direction:
 * @self: A #LrgParticleForceWind
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets the wind direction.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_wind_set_direction   (LrgParticleForceWind    *self,
                                                                 gfloat                   x,
                                                                 gfloat                   y,
                                                                 gfloat                   z);

/**
 * lrg_particle_force_wind_get_turbulence:
 * @self: A #LrgParticleForceWind
 *
 * Gets the wind turbulence amount.
 *
 * Returns: Turbulence amount (0.0 = steady, 1.0 = very turbulent)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_wind_get_turbulence  (LrgParticleForceWind    *self);

/**
 * lrg_particle_force_wind_set_turbulence:
 * @self: A #LrgParticleForceWind
 * @turbulence: Turbulence amount
 *
 * Sets the wind turbulence.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_wind_set_turbulence  (LrgParticleForceWind    *self,
                                                                 gfloat                   turbulence);

/* ============================================================================
 * Attractor Force
 * ============================================================================ */

#define LRG_TYPE_PARTICLE_FORCE_ATTRACTOR (lrg_particle_force_attractor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleForceAttractor, lrg_particle_force_attractor, LRG, PARTICLE_FORCE_ATTRACTOR, LrgParticleForce)

/**
 * lrg_particle_force_attractor_new:
 * @x: X position of attractor
 * @y: Y position of attractor
 * @z: Z position of attractor
 * @radius: Attraction radius
 *
 * Creates a new attractor force. Particles within the radius
 * are pulled toward the attractor position.
 *
 * Returns: (transfer full): A new #LrgParticleForceAttractor
 */
LRG_AVAILABLE_IN_ALL
LrgParticleForce *      lrg_particle_force_attractor_new        (gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z,
                                                                 gfloat               radius);

/**
 * lrg_particle_force_attractor_get_position:
 * @self: A #LrgParticleForceAttractor
 * @x: (out) (nullable): X position
 * @y: (out) (nullable): Y position
 * @z: (out) (nullable): Z position
 *
 * Gets the attractor position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_attractor_get_position (LrgParticleForceAttractor *self,
                                                                   gfloat                    *x,
                                                                   gfloat                    *y,
                                                                   gfloat                    *z);

/**
 * lrg_particle_force_attractor_set_position:
 * @self: A #LrgParticleForceAttractor
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the attractor position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_attractor_set_position (LrgParticleForceAttractor *self,
                                                                   gfloat                     x,
                                                                   gfloat                     y,
                                                                   gfloat                     z);

/**
 * lrg_particle_force_attractor_get_radius:
 * @self: A #LrgParticleForceAttractor
 *
 * Gets the attraction radius.
 *
 * Returns: Radius
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_attractor_get_radius (LrgParticleForceAttractor *self);

/**
 * lrg_particle_force_attractor_set_radius:
 * @self: A #LrgParticleForceAttractor
 * @radius: Attraction radius
 *
 * Sets the attraction radius.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_attractor_set_radius (LrgParticleForceAttractor *self,
                                                                 gfloat                     radius);

/**
 * lrg_particle_force_attractor_get_falloff:
 * @self: A #LrgParticleForceAttractor
 *
 * Gets the falloff exponent.
 *
 * Returns: Falloff exponent (1.0 = linear, 2.0 = inverse square)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_attractor_get_falloff (LrgParticleForceAttractor *self);

/**
 * lrg_particle_force_attractor_set_falloff:
 * @self: A #LrgParticleForceAttractor
 * @falloff: Falloff exponent
 *
 * Sets the falloff exponent.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_attractor_set_falloff (LrgParticleForceAttractor *self,
                                                                  gfloat                     falloff);

/* ============================================================================
 * Turbulence Force
 * ============================================================================ */

#define LRG_TYPE_PARTICLE_FORCE_TURBULENCE (lrg_particle_force_turbulence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleForceTurbulence, lrg_particle_force_turbulence, LRG, PARTICLE_FORCE_TURBULENCE, LrgParticleForce)

/**
 * lrg_particle_force_turbulence_new:
 * @frequency: Noise frequency
 * @amplitude: Noise amplitude
 *
 * Creates a new turbulence force using Perlin noise.
 *
 * Returns: (transfer full): A new #LrgParticleForceTurbulence
 */
LRG_AVAILABLE_IN_ALL
LrgParticleForce *      lrg_particle_force_turbulence_new       (gfloat               frequency,
                                                                 gfloat               amplitude);

/**
 * lrg_particle_force_turbulence_get_frequency:
 * @self: A #LrgParticleForceTurbulence
 *
 * Gets the noise frequency.
 *
 * Returns: Frequency
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_turbulence_get_frequency (LrgParticleForceTurbulence *self);

/**
 * lrg_particle_force_turbulence_set_frequency:
 * @self: A #LrgParticleForceTurbulence
 * @frequency: Noise frequency
 *
 * Sets the noise frequency.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_turbulence_set_frequency (LrgParticleForceTurbulence *self,
                                                                     gfloat                      frequency);

/**
 * lrg_particle_force_turbulence_get_amplitude:
 * @self: A #LrgParticleForceTurbulence
 *
 * Gets the noise amplitude.
 *
 * Returns: Amplitude
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_turbulence_get_amplitude (LrgParticleForceTurbulence *self);

/**
 * lrg_particle_force_turbulence_set_amplitude:
 * @self: A #LrgParticleForceTurbulence
 * @amplitude: Noise amplitude
 *
 * Sets the noise amplitude.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_turbulence_set_amplitude (LrgParticleForceTurbulence *self,
                                                                     gfloat                      amplitude);

/**
 * lrg_particle_force_turbulence_get_scroll_speed:
 * @self: A #LrgParticleForceTurbulence
 *
 * Gets the noise scroll speed (for animated turbulence).
 *
 * Returns: Scroll speed
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_force_turbulence_get_scroll_speed (LrgParticleForceTurbulence *self);

/**
 * lrg_particle_force_turbulence_set_scroll_speed:
 * @self: A #LrgParticleForceTurbulence
 * @speed: Scroll speed
 *
 * Sets the noise scroll speed.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_force_turbulence_set_scroll_speed (LrgParticleForceTurbulence *self,
                                                                        gfloat                      speed);

G_END_DECLS
