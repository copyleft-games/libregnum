/* lrg-particle.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Particle data structure for particle systems.
 * This is a GBoxed type representing individual particle state.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

typedef struct _LrgParticle LrgParticle;

/**
 * LrgParticle:
 * @position_x: X position in world space
 * @position_y: Y position in world space
 * @position_z: Z position in world space (for 3D particles)
 * @velocity_x: X velocity component
 * @velocity_y: Y velocity component
 * @velocity_z: Z velocity component (for 3D particles)
 * @color_r: Red color component (0.0 - 1.0)
 * @color_g: Green color component (0.0 - 1.0)
 * @color_b: Blue color component (0.0 - 1.0)
 * @color_a: Alpha/opacity component (0.0 - 1.0)
 * @size: Current particle size (radius or scale)
 * @rotation: Current rotation in radians
 * @rotation_velocity: Angular velocity in radians per second
 * @life: Remaining lifetime in seconds
 * @max_life: Initial lifetime in seconds (for interpolation)
 * @age: Current age (max_life - life)
 * @alive: Whether the particle is active
 *
 * Represents the complete state of a single particle.
 * Used by #LrgParticleSystem and #LrgParticlePool.
 */
struct _LrgParticle
{
    /* Position (world space) */
    gfloat position_x;
    gfloat position_y;
    gfloat position_z;

    /* Velocity (units per second) */
    gfloat velocity_x;
    gfloat velocity_y;
    gfloat velocity_z;

    /* Color (RGBA, 0.0-1.0) */
    gfloat color_r;
    gfloat color_g;
    gfloat color_b;
    gfloat color_a;

    /* Visual properties */
    gfloat size;
    gfloat rotation;
    gfloat rotation_velocity;

    /* Lifetime */
    gfloat life;
    gfloat max_life;
    gfloat age;

    /* State */
    gboolean alive;

    /*< private >*/
    gpointer _reserved[4];
};

#define LRG_TYPE_PARTICLE (lrg_particle_get_type ())

LRG_AVAILABLE_IN_ALL
GType           lrg_particle_get_type       (void) G_GNUC_CONST;

/**
 * lrg_particle_new:
 *
 * Creates a new particle with default values.
 * The particle is initialized as dead (alive = FALSE).
 *
 * Returns: (transfer full): A newly allocated #LrgParticle
 */
LRG_AVAILABLE_IN_ALL
LrgParticle *   lrg_particle_new            (void);

/**
 * lrg_particle_new_at:
 * @x: Initial X position
 * @y: Initial Y position
 * @z: Initial Z position
 * @life: Lifetime in seconds
 *
 * Creates a new particle at the specified position with a given lifetime.
 * The particle is initialized as alive.
 *
 * Returns: (transfer full): A newly allocated #LrgParticle
 */
LRG_AVAILABLE_IN_ALL
LrgParticle *   lrg_particle_new_at         (gfloat              x,
                                             gfloat              y,
                                             gfloat              z,
                                             gfloat              life);

/**
 * lrg_particle_copy:
 * @self: (nullable): A #LrgParticle
 *
 * Creates a copy of the particle.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgParticle *   lrg_particle_copy           (const LrgParticle  *self);

/**
 * lrg_particle_free:
 * @self: (nullable): A #LrgParticle
 *
 * Frees a particle allocated with lrg_particle_new() or lrg_particle_copy().
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_free           (LrgParticle        *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgParticle, lrg_particle_free)

/**
 * lrg_particle_reset:
 * @self: A #LrgParticle
 *
 * Resets a particle to default values and marks it as dead.
 * This is more efficient than freeing and reallocating.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_reset          (LrgParticle        *self);

/**
 * lrg_particle_spawn:
 * @self: A #LrgParticle
 * @x: Initial X position
 * @y: Initial Y position
 * @z: Initial Z position
 * @life: Lifetime in seconds
 *
 * Spawns (or respawns) a particle at the given position.
 * Sets velocity to zero, color to white, size to 1.0, and marks as alive.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_spawn          (LrgParticle        *self,
                                             gfloat              x,
                                             gfloat              y,
                                             gfloat              z,
                                             gfloat              life);

/**
 * lrg_particle_update:
 * @self: A #LrgParticle
 * @delta_time: Time step in seconds
 *
 * Updates the particle by one time step.
 * Applies velocity to position, updates age/life, and marks as dead
 * if lifetime expired.
 *
 * Returns: %TRUE if the particle is still alive after update
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_particle_update         (LrgParticle        *self,
                                             gfloat              delta_time);

/**
 * lrg_particle_is_alive:
 * @self: A #LrgParticle
 *
 * Checks if the particle is alive.
 *
 * Returns: %TRUE if the particle is active
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_particle_is_alive       (const LrgParticle  *self);

/**
 * lrg_particle_kill:
 * @self: A #LrgParticle
 *
 * Immediately marks the particle as dead.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_kill           (LrgParticle        *self);

/**
 * lrg_particle_get_normalized_age:
 * @self: A #LrgParticle
 *
 * Gets the normalized age of the particle (0.0 = just spawned, 1.0 = about to die).
 * Useful for interpolating color, size, etc. over the particle's lifetime.
 *
 * Returns: Normalized age between 0.0 and 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_particle_get_normalized_age (const LrgParticle *self);

/**
 * lrg_particle_set_velocity:
 * @self: A #LrgParticle
 * @vx: X velocity
 * @vy: Y velocity
 * @vz: Z velocity
 *
 * Sets the velocity of the particle.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_set_velocity   (LrgParticle        *self,
                                             gfloat              vx,
                                             gfloat              vy,
                                             gfloat              vz);

/**
 * lrg_particle_set_color:
 * @self: A #LrgParticle
 * @r: Red component (0.0 - 1.0)
 * @g: Green component (0.0 - 1.0)
 * @b: Blue component (0.0 - 1.0)
 * @a: Alpha component (0.0 - 1.0)
 *
 * Sets the color of the particle.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_set_color      (LrgParticle        *self,
                                             gfloat              r,
                                             gfloat              g,
                                             gfloat              b,
                                             gfloat              a);

/**
 * lrg_particle_apply_force:
 * @self: A #LrgParticle
 * @fx: X force component
 * @fy: Y force component
 * @fz: Z force component
 * @delta_time: Time step in seconds
 *
 * Applies a force to the particle, modifying its velocity.
 * The force is assumed to be an acceleration (mass = 1).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_particle_apply_force    (LrgParticle        *self,
                                             gfloat              fx,
                                             gfloat              fy,
                                             gfloat              fz,
                                             gfloat              delta_time);

G_END_DECLS
