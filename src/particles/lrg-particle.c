/* lrg-particle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-particle.h"

/**
 * SECTION:lrg-particle
 * @Title: LrgParticle
 * @Short_description: Particle data structure
 *
 * #LrgParticle represents the complete state of a single particle in a
 * particle system. It includes position, velocity, color, size, rotation,
 * and lifetime information.
 *
 * Particles are typically managed by an #LrgParticlePool which efficiently
 * reuses particle storage, and updated by an #LrgParticleSystem which
 * handles emission, forces, and rendering.
 *
 * Example:
 * |[<!-- language="C" -->
 * g_autoptr(LrgParticle) particle = lrg_particle_new_at (100.0f, 100.0f, 0.0f, 2.0f);
 * lrg_particle_set_velocity (particle, 0.0f, -50.0f, 0.0f);
 * lrg_particle_set_color (particle, 1.0f, 0.5f, 0.0f, 1.0f);
 *
 * while (lrg_particle_is_alive (particle))
 * {
 *     lrg_particle_update (particle, delta_time);
 *     // render particle...
 * }
 * ]|
 */

G_DEFINE_BOXED_TYPE (LrgParticle, lrg_particle,
                     lrg_particle_copy, lrg_particle_free)

/**
 * lrg_particle_new:
 *
 * Creates a new particle with default values.
 * The particle is initialized as dead (alive = FALSE).
 *
 * Returns: (transfer full): A newly allocated #LrgParticle
 */
LrgParticle *
lrg_particle_new (void)
{
    LrgParticle *self;

    self = g_new0 (LrgParticle, 1);

    /* Default values */
    self->position_x = 0.0f;
    self->position_y = 0.0f;
    self->position_z = 0.0f;
    self->velocity_x = 0.0f;
    self->velocity_y = 0.0f;
    self->velocity_z = 0.0f;
    self->color_r = 1.0f;
    self->color_g = 1.0f;
    self->color_b = 1.0f;
    self->color_a = 1.0f;
    self->size = 1.0f;
    self->rotation = 0.0f;
    self->rotation_velocity = 0.0f;
    self->life = 0.0f;
    self->max_life = 0.0f;
    self->age = 0.0f;
    self->alive = FALSE;

    return self;
}

/**
 * lrg_particle_new_at:
 * @x: Initial X position
 * @y: Initial Y position
 * @z: Initial Z position
 * @life: Lifetime in seconds
 *
 * Creates a new particle at the specified position with a given lifetime.
 * The particle is initialized as alive with default color (white) and size (1.0).
 *
 * Returns: (transfer full): A newly allocated #LrgParticle
 */
LrgParticle *
lrg_particle_new_at (gfloat x,
                     gfloat y,
                     gfloat z,
                     gfloat life)
{
    LrgParticle *self;

    self = lrg_particle_new ();
    lrg_particle_spawn (self, x, y, z, life);

    return self;
}

/**
 * lrg_particle_copy:
 * @self: (nullable): A #LrgParticle
 *
 * Creates a copy of the particle.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL
 */
LrgParticle *
lrg_particle_copy (const LrgParticle *self)
{
    LrgParticle *copy;

    if (self == NULL)
        return NULL;

    copy = g_new (LrgParticle, 1);
    *copy = *self;

    return copy;
}

/**
 * lrg_particle_free:
 * @self: (nullable): A #LrgParticle
 *
 * Frees a particle.
 */
void
lrg_particle_free (LrgParticle *self)
{
    g_free (self);
}

/**
 * lrg_particle_reset:
 * @self: A #LrgParticle
 *
 * Resets a particle to default values and marks it as dead.
 * This is more efficient than freeing and reallocating.
 */
void
lrg_particle_reset (LrgParticle *self)
{
    g_return_if_fail (self != NULL);

    self->position_x = 0.0f;
    self->position_y = 0.0f;
    self->position_z = 0.0f;
    self->velocity_x = 0.0f;
    self->velocity_y = 0.0f;
    self->velocity_z = 0.0f;
    self->color_r = 1.0f;
    self->color_g = 1.0f;
    self->color_b = 1.0f;
    self->color_a = 1.0f;
    self->size = 1.0f;
    self->rotation = 0.0f;
    self->rotation_velocity = 0.0f;
    self->life = 0.0f;
    self->max_life = 0.0f;
    self->age = 0.0f;
    self->alive = FALSE;
}

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
void
lrg_particle_spawn (LrgParticle *self,
                    gfloat       x,
                    gfloat       y,
                    gfloat       z,
                    gfloat       life)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (life > 0.0f);

    self->position_x = x;
    self->position_y = y;
    self->position_z = z;
    self->velocity_x = 0.0f;
    self->velocity_y = 0.0f;
    self->velocity_z = 0.0f;
    self->color_r = 1.0f;
    self->color_g = 1.0f;
    self->color_b = 1.0f;
    self->color_a = 1.0f;
    self->size = 1.0f;
    self->rotation = 0.0f;
    self->rotation_velocity = 0.0f;
    self->life = life;
    self->max_life = life;
    self->age = 0.0f;
    self->alive = TRUE;
}

/**
 * lrg_particle_update:
 * @self: A #LrgParticle
 * @delta_time: Time step in seconds
 *
 * Updates the particle by one time step.
 * Applies velocity to position, updates rotation, age/life, and marks
 * as dead if lifetime expired.
 *
 * Returns: %TRUE if the particle is still alive after update
 */
gboolean
lrg_particle_update (LrgParticle *self,
                     gfloat       delta_time)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!self->alive)
        return FALSE;

    /* Update position from velocity */
    self->position_x += self->velocity_x * delta_time;
    self->position_y += self->velocity_y * delta_time;
    self->position_z += self->velocity_z * delta_time;

    /* Update rotation */
    self->rotation += self->rotation_velocity * delta_time;

    /* Update lifetime */
    self->life -= delta_time;
    self->age += delta_time;

    /* Check if dead */
    if (self->life <= 0.0f)
    {
        self->life = 0.0f;
        self->alive = FALSE;
        return FALSE;
    }

    return TRUE;
}

/**
 * lrg_particle_is_alive:
 * @self: A #LrgParticle
 *
 * Checks if the particle is alive.
 *
 * Returns: %TRUE if the particle is active
 */
gboolean
lrg_particle_is_alive (const LrgParticle *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->alive;
}

/**
 * lrg_particle_kill:
 * @self: A #LrgParticle
 *
 * Immediately marks the particle as dead.
 */
void
lrg_particle_kill (LrgParticle *self)
{
    g_return_if_fail (self != NULL);

    self->alive = FALSE;
    self->life = 0.0f;
}

/**
 * lrg_particle_get_normalized_age:
 * @self: A #LrgParticle
 *
 * Gets the normalized age of the particle (0.0 = just spawned, 1.0 = about to die).
 * Useful for interpolating color, size, etc. over the particle's lifetime.
 *
 * Returns: Normalized age between 0.0 and 1.0
 */
gfloat
lrg_particle_get_normalized_age (const LrgParticle *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    if (self->max_life <= 0.0f)
        return 0.0f;

    return CLAMP (self->age / self->max_life, 0.0f, 1.0f);
}

/**
 * lrg_particle_set_velocity:
 * @self: A #LrgParticle
 * @vx: X velocity
 * @vy: Y velocity
 * @vz: Z velocity
 *
 * Sets the velocity of the particle.
 */
void
lrg_particle_set_velocity (LrgParticle *self,
                           gfloat       vx,
                           gfloat       vy,
                           gfloat       vz)
{
    g_return_if_fail (self != NULL);

    self->velocity_x = vx;
    self->velocity_y = vy;
    self->velocity_z = vz;
}

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
void
lrg_particle_set_color (LrgParticle *self,
                        gfloat       r,
                        gfloat       g,
                        gfloat       b,
                        gfloat       a)
{
    g_return_if_fail (self != NULL);

    self->color_r = CLAMP (r, 0.0f, 1.0f);
    self->color_g = CLAMP (g, 0.0f, 1.0f);
    self->color_b = CLAMP (b, 0.0f, 1.0f);
    self->color_a = CLAMP (a, 0.0f, 1.0f);
}

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
void
lrg_particle_apply_force (LrgParticle *self,
                          gfloat       fx,
                          gfloat       fy,
                          gfloat       fz,
                          gfloat       delta_time)
{
    g_return_if_fail (self != NULL);

    /* F = ma, assuming m = 1, a = F */
    self->velocity_x += fx * delta_time;
    self->velocity_y += fy * delta_time;
    self->velocity_z += fz * delta_time;
}
