/* lrg-particle-emitter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Particle emitter configuration for particle systems.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-particle.h"

G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_EMITTER (lrg_particle_emitter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgParticleEmitter, lrg_particle_emitter, LRG, PARTICLE_EMITTER, GObject)

/**
 * LrgParticleEmitterClass:
 * @parent_class: Parent class
 * @emit: Virtual method to emit a single particle
 * @burst: Virtual method to emit a burst of particles
 * @update: Virtual method called each frame
 *
 * The class structure for #LrgParticleEmitter.
 */
struct _LrgParticleEmitterClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgParticleEmitterClass::emit:
     * @self: A #LrgParticleEmitter
     * @out_particle: (out): Output particle to initialize
     *
     * Emits a single particle with properties based on emitter configuration.
     */
    void    (*emit)     (LrgParticleEmitter  *self,
                         LrgParticle         *out_particle);

    /**
     * LrgParticleEmitterClass::burst:
     * @self: A #LrgParticleEmitter
     * @particles: (array length=count) (out): Array to initialize
     * @count: Number of particles to emit
     *
     * Emits multiple particles in a burst.
     */
    void    (*burst)    (LrgParticleEmitter  *self,
                         LrgParticle         *particles,
                         guint                count);

    /**
     * LrgParticleEmitterClass::update:
     * @self: A #LrgParticleEmitter
     * @delta_time: Time since last frame in seconds
     *
     * Updates the emitter state.
     */
    void    (*update)   (LrgParticleEmitter  *self,
                         gfloat               delta_time);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_particle_emitter_new:
 *
 * Creates a new particle emitter with default settings.
 *
 * Returns: (transfer full): A new #LrgParticleEmitter
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEmitter *    lrg_particle_emitter_new                (void);

/**
 * lrg_particle_emitter_emit:
 * @self: A #LrgParticleEmitter
 * @out_particle: (out): Particle to initialize
 *
 * Emits a single particle based on emitter settings.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_emit               (LrgParticleEmitter  *self,
                                                                 LrgParticle         *out_particle);

/**
 * lrg_particle_emitter_burst:
 * @self: A #LrgParticleEmitter
 * @particles: (array length=count) (out): Array of particles to initialize
 * @count: Number of particles to emit
 *
 * Emits multiple particles at once.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_burst              (LrgParticleEmitter  *self,
                                                                 LrgParticle         *particles,
                                                                 guint                count);

/**
 * lrg_particle_emitter_update:
 * @self: A #LrgParticleEmitter
 * @delta_time: Time since last frame
 *
 * Updates the emitter, accumulating emission time.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_update             (LrgParticleEmitter  *self,
                                                                 gfloat               delta_time);

/**
 * lrg_particle_emitter_should_emit:
 * @self: A #LrgParticleEmitter
 *
 * Checks if the emitter should emit a particle based on rate and time.
 *
 * Returns: %TRUE if should emit, %FALSE otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_emitter_should_emit        (LrgParticleEmitter  *self);

/* Property getters/setters */

/**
 * lrg_particle_emitter_get_emission_rate:
 * @self: A #LrgParticleEmitter
 *
 * Gets the emission rate (particles per second).
 *
 * Returns: Emission rate
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_emitter_get_emission_rate  (LrgParticleEmitter  *self);

/**
 * lrg_particle_emitter_set_emission_rate:
 * @self: A #LrgParticleEmitter
 * @rate: Particles per second
 *
 * Sets the emission rate.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_emission_rate  (LrgParticleEmitter  *self,
                                                                 gfloat               rate);

/**
 * lrg_particle_emitter_get_emission_shape:
 * @self: A #LrgParticleEmitter
 *
 * Gets the emission shape.
 *
 * Returns: The emission shape
 */
LRG_AVAILABLE_IN_ALL
LrgEmissionShape        lrg_particle_emitter_get_emission_shape (LrgParticleEmitter  *self);

/**
 * lrg_particle_emitter_set_emission_shape:
 * @self: A #LrgParticleEmitter
 * @shape: The emission shape
 *
 * Sets the emission shape.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_emission_shape (LrgParticleEmitter  *self,
                                                                 LrgEmissionShape     shape);

/**
 * lrg_particle_emitter_get_position:
 * @self: A #LrgParticleEmitter
 * @x: (out) (nullable): X position
 * @y: (out) (nullable): Y position
 * @z: (out) (nullable): Z position
 *
 * Gets the emitter position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_position       (LrgParticleEmitter  *self,
                                                                 gfloat              *x,
                                                                 gfloat              *y,
                                                                 gfloat              *z);

/**
 * lrg_particle_emitter_set_position:
 * @self: A #LrgParticleEmitter
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the emitter position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_position       (LrgParticleEmitter  *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_emitter_get_direction:
 * @self: A #LrgParticleEmitter
 * @x: (out) (nullable): X direction
 * @y: (out) (nullable): Y direction
 * @z: (out) (nullable): Z direction
 *
 * Gets the emission direction.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_direction      (LrgParticleEmitter  *self,
                                                                 gfloat              *x,
                                                                 gfloat              *y,
                                                                 gfloat              *z);

/**
 * lrg_particle_emitter_set_direction:
 * @self: A #LrgParticleEmitter
 * @x: X direction
 * @y: Y direction
 * @z: Z direction
 *
 * Sets the emission direction (for shaped emissions).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_direction      (LrgParticleEmitter  *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_emitter_get_spread_angle:
 * @self: A #LrgParticleEmitter
 *
 * Gets the spread angle in radians.
 *
 * Returns: Spread angle
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_emitter_get_spread_angle   (LrgParticleEmitter  *self);

/**
 * lrg_particle_emitter_set_spread_angle:
 * @self: A #LrgParticleEmitter
 * @angle: Spread angle in radians
 *
 * Sets the spread angle for cone emissions.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_spread_angle   (LrgParticleEmitter  *self,
                                                                 gfloat               angle);

/**
 * lrg_particle_emitter_get_shape_radius:
 * @self: A #LrgParticleEmitter
 *
 * Gets the shape radius (for circle/cone shapes).
 *
 * Returns: Shape radius
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_emitter_get_shape_radius   (LrgParticleEmitter  *self);

/**
 * lrg_particle_emitter_set_shape_radius:
 * @self: A #LrgParticleEmitter
 * @radius: Shape radius
 *
 * Sets the shape radius.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_shape_radius   (LrgParticleEmitter  *self,
                                                                 gfloat               radius);

/**
 * lrg_particle_emitter_get_shape_size:
 * @self: A #LrgParticleEmitter
 * @width: (out) (nullable): Width
 * @height: (out) (nullable): Height
 * @depth: (out) (nullable): Depth
 *
 * Gets the shape size (for rectangle shapes).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_shape_size     (LrgParticleEmitter  *self,
                                                                 gfloat              *width,
                                                                 gfloat              *height,
                                                                 gfloat              *depth);

/**
 * lrg_particle_emitter_set_shape_size:
 * @self: A #LrgParticleEmitter
 * @width: Width
 * @height: Height
 * @depth: Depth
 *
 * Sets the shape size for rectangle emissions.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_shape_size     (LrgParticleEmitter  *self,
                                                                 gfloat               width,
                                                                 gfloat               height,
                                                                 gfloat               depth);

/* Particle initial values */

/**
 * lrg_particle_emitter_get_initial_speed:
 * @self: A #LrgParticleEmitter
 * @min: (out) (nullable): Minimum speed
 * @max: (out) (nullable): Maximum speed
 *
 * Gets the initial speed range.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_initial_speed  (LrgParticleEmitter  *self,
                                                                 gfloat              *min,
                                                                 gfloat              *max);

/**
 * lrg_particle_emitter_set_initial_speed:
 * @self: A #LrgParticleEmitter
 * @min: Minimum speed
 * @max: Maximum speed
 *
 * Sets the initial speed range for emitted particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_initial_speed  (LrgParticleEmitter  *self,
                                                                 gfloat               min,
                                                                 gfloat               max);

/**
 * lrg_particle_emitter_get_initial_lifetime:
 * @self: A #LrgParticleEmitter
 * @min: (out) (nullable): Minimum lifetime
 * @max: (out) (nullable): Maximum lifetime
 *
 * Gets the initial lifetime range in seconds.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_initial_lifetime (LrgParticleEmitter *self,
                                                                   gfloat             *min,
                                                                   gfloat             *max);

/**
 * lrg_particle_emitter_set_initial_lifetime:
 * @self: A #LrgParticleEmitter
 * @min: Minimum lifetime in seconds
 * @max: Maximum lifetime in seconds
 *
 * Sets the initial lifetime range for emitted particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_initial_lifetime (LrgParticleEmitter *self,
                                                                   gfloat              min,
                                                                   gfloat              max);

/**
 * lrg_particle_emitter_get_initial_size:
 * @self: A #LrgParticleEmitter
 * @min: (out) (nullable): Minimum size
 * @max: (out) (nullable): Maximum size
 *
 * Gets the initial size range.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_initial_size   (LrgParticleEmitter  *self,
                                                                 gfloat              *min,
                                                                 gfloat              *max);

/**
 * lrg_particle_emitter_set_initial_size:
 * @self: A #LrgParticleEmitter
 * @min: Minimum size
 * @max: Maximum size
 *
 * Sets the initial size range for emitted particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_initial_size   (LrgParticleEmitter  *self,
                                                                 gfloat               min,
                                                                 gfloat               max);

/**
 * lrg_particle_emitter_get_start_color:
 * @self: A #LrgParticleEmitter
 * @r: (out) (nullable): Red component (0.0-1.0)
 * @g: (out) (nullable): Green component (0.0-1.0)
 * @b: (out) (nullable): Blue component (0.0-1.0)
 * @a: (out) (nullable): Alpha component (0.0-1.0)
 *
 * Gets the start color for particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_start_color    (LrgParticleEmitter  *self,
                                                                 gfloat              *r,
                                                                 gfloat              *g,
                                                                 gfloat              *b,
                                                                 gfloat              *a);

/**
 * lrg_particle_emitter_set_start_color:
 * @self: A #LrgParticleEmitter
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the start color for particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_start_color    (LrgParticleEmitter  *self,
                                                                 gfloat               r,
                                                                 gfloat               g,
                                                                 gfloat               b,
                                                                 gfloat               a);

/**
 * lrg_particle_emitter_get_end_color:
 * @self: A #LrgParticleEmitter
 * @r: (out) (nullable): Red component (0.0-1.0)
 * @g: (out) (nullable): Green component (0.0-1.0)
 * @b: (out) (nullable): Blue component (0.0-1.0)
 * @a: (out) (nullable): Alpha component (0.0-1.0)
 *
 * Gets the end color for particles (at end of lifetime).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_get_end_color      (LrgParticleEmitter  *self,
                                                                 gfloat              *r,
                                                                 gfloat              *g,
                                                                 gfloat              *b,
                                                                 gfloat              *a);

/**
 * lrg_particle_emitter_set_end_color:
 * @self: A #LrgParticleEmitter
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the end color for particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_end_color      (LrgParticleEmitter  *self,
                                                                 gfloat               r,
                                                                 gfloat               g,
                                                                 gfloat               b,
                                                                 gfloat               a);

/**
 * lrg_particle_emitter_get_enabled:
 * @self: A #LrgParticleEmitter
 *
 * Checks if the emitter is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_emitter_get_enabled        (LrgParticleEmitter  *self);

/**
 * lrg_particle_emitter_set_enabled:
 * @self: A #LrgParticleEmitter
 * @enabled: Whether to enable the emitter
 *
 * Enables or disables the emitter.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_set_enabled        (LrgParticleEmitter  *self,
                                                                 gboolean             enabled);

/**
 * lrg_particle_emitter_reset:
 * @self: A #LrgParticleEmitter
 *
 * Resets the emitter's internal timer.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_emitter_reset              (LrgParticleEmitter  *self);

G_END_DECLS
