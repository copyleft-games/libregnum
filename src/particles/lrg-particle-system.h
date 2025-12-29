/* lrg-particle-system.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Complete particle system with emitters, forces, and rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../graphics/lrg-drawable.h"
#include "lrg-particle.h"
#include "lrg-particle-pool.h"
#include "lrg-particle-emitter.h"
#include "lrg-particle-force.h"

G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_SYSTEM (lrg_particle_system_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgParticleSystem, lrg_particle_system, LRG, PARTICLE_SYSTEM, GObject)

/**
 * LrgParticleSystemClass:
 * @parent_class: Parent class
 * @update: Virtual method called each frame
 * @draw: Virtual method to render particles
 * @on_particle_spawn: Called when a particle is spawned
 * @on_particle_death: Called when a particle dies
 *
 * The class structure for #LrgParticleSystem.
 */
struct _LrgParticleSystemClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgParticleSystemClass::update:
     * @self: A #LrgParticleSystem
     * @delta_time: Time since last frame
     *
     * Updates all particles, emitters, and forces.
     */
    void    (*update)               (LrgParticleSystem   *self,
                                     gfloat               delta_time);

    /**
     * LrgParticleSystemClass::draw:
     * @self: A #LrgParticleSystem
     *
     * Renders all active particles.
     */
    void    (*draw)                 (LrgParticleSystem   *self);

    /**
     * LrgParticleSystemClass::on_particle_spawn:
     * @self: A #LrgParticleSystem
     * @particle: The spawned particle
     *
     * Called when a particle is spawned (for custom initialization).
     */
    void    (*on_particle_spawn)    (LrgParticleSystem   *self,
                                     LrgParticle         *particle);

    /**
     * LrgParticleSystemClass::on_particle_death:
     * @self: A #LrgParticleSystem
     * @particle: The dying particle
     *
     * Called when a particle dies (for effects like sub-emitters).
     */
    void    (*on_particle_death)    (LrgParticleSystem   *self,
                                     LrgParticle         *particle);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_particle_system_new:
 * @max_particles: Maximum number of particles
 *
 * Creates a new particle system.
 *
 * Returns: (transfer full): A new #LrgParticleSystem
 */
LRG_AVAILABLE_IN_ALL
LrgParticleSystem *     lrg_particle_system_new                 (guint                max_particles);

/**
 * lrg_particle_system_update:
 * @self: A #LrgParticleSystem
 * @delta_time: Time since last frame
 *
 * Updates all particles, emitters, and forces.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_update              (LrgParticleSystem   *self,
                                                                 gfloat               delta_time);

/**
 * lrg_particle_system_draw:
 * @self: A #LrgParticleSystem
 *
 * Renders all active particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_draw                (LrgParticleSystem   *self);

/* Emitter management */

/**
 * lrg_particle_system_add_emitter:
 * @self: A #LrgParticleSystem
 * @emitter: (transfer none): Emitter to add
 *
 * Adds an emitter to the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_add_emitter         (LrgParticleSystem   *self,
                                                                 LrgParticleEmitter  *emitter);

/**
 * lrg_particle_system_remove_emitter:
 * @self: A #LrgParticleSystem
 * @emitter: Emitter to remove
 *
 * Removes an emitter from the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_remove_emitter      (LrgParticleSystem   *self,
                                                                 LrgParticleEmitter  *emitter);

/**
 * lrg_particle_system_get_emitters:
 * @self: A #LrgParticleSystem
 *
 * Gets all emitters.
 *
 * Returns: (transfer none) (element-type LrgParticleEmitter): The emitter list
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_particle_system_get_emitters        (LrgParticleSystem   *self);

/**
 * lrg_particle_system_clear_emitters:
 * @self: A #LrgParticleSystem
 *
 * Removes all emitters.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear_emitters      (LrgParticleSystem   *self);

/* Force management */

/**
 * lrg_particle_system_add_force:
 * @self: A #LrgParticleSystem
 * @force: (transfer none): Force to add
 *
 * Adds a force to the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_add_force           (LrgParticleSystem   *self,
                                                                 LrgParticleForce    *force);

/**
 * lrg_particle_system_remove_force:
 * @self: A #LrgParticleSystem
 * @force: Force to remove
 *
 * Removes a force from the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_remove_force        (LrgParticleSystem   *self,
                                                                 LrgParticleForce    *force);

/**
 * lrg_particle_system_get_forces:
 * @self: A #LrgParticleSystem
 *
 * Gets all forces.
 *
 * Returns: (transfer none) (element-type LrgParticleForce): The force list
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_particle_system_get_forces          (LrgParticleSystem   *self);

/**
 * lrg_particle_system_clear_forces:
 * @self: A #LrgParticleSystem
 *
 * Removes all forces.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear_forces        (LrgParticleSystem   *self);

/* Particle control */

/**
 * lrg_particle_system_emit:
 * @self: A #LrgParticleSystem
 * @count: Number of particles to emit
 *
 * Manually emits particles using the first emitter.
 *
 * Returns: Number of particles actually emitted
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_emit                (LrgParticleSystem   *self,
                                                                 guint                count);

/**
 * lrg_particle_system_emit_at:
 * @self: A #LrgParticleSystem
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @count: Number of particles
 *
 * Emits particles at a specific position.
 *
 * Returns: Number of particles actually emitted
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_emit_at             (LrgParticleSystem   *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z,
                                                                 guint                count);

/**
 * lrg_particle_system_clear:
 * @self: A #LrgParticleSystem
 *
 * Kills all active particles immediately.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear               (LrgParticleSystem   *self);

/* State control */

/**
 * lrg_particle_system_play:
 * @self: A #LrgParticleSystem
 *
 * Starts or resumes the particle system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_play                (LrgParticleSystem   *self);

/**
 * lrg_particle_system_pause:
 * @self: A #LrgParticleSystem
 *
 * Pauses the particle system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_pause               (LrgParticleSystem   *self);

/**
 * lrg_particle_system_stop:
 * @self: A #LrgParticleSystem
 *
 * Stops the system and clears all particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_stop                (LrgParticleSystem   *self);

/**
 * lrg_particle_system_is_playing:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system is currently playing.
 *
 * Returns: %TRUE if playing
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_is_playing          (LrgParticleSystem   *self);

/**
 * lrg_particle_system_is_alive:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system has any active particles or enabled emitters.
 *
 * Returns: %TRUE if alive
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_is_alive            (LrgParticleSystem   *self);

/* Properties */

/**
 * lrg_particle_system_get_active_count:
 * @self: A #LrgParticleSystem
 *
 * Gets the number of currently active particles.
 *
 * Returns: Active particle count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_get_active_count    (LrgParticleSystem   *self);

/**
 * lrg_particle_system_get_max_particles:
 * @self: A #LrgParticleSystem
 *
 * Gets the maximum particle capacity.
 *
 * Returns: Maximum particles
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_get_max_particles   (LrgParticleSystem   *self);

/**
 * lrg_particle_system_get_render_mode:
 * @self: A #LrgParticleSystem
 *
 * Gets the particle render mode.
 *
 * Returns: The render mode
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRenderMode   lrg_particle_system_get_render_mode     (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_render_mode:
 * @self: A #LrgParticleSystem
 * @mode: The render mode
 *
 * Sets the particle render mode.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_render_mode     (LrgParticleSystem   *self,
                                                                 LrgParticleRenderMode mode);

/**
 * lrg_particle_system_get_blend_mode:
 * @self: A #LrgParticleSystem
 *
 * Gets the particle blend mode.
 *
 * Returns: The blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgParticleBlendMode    lrg_particle_system_get_blend_mode      (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_blend_mode:
 * @self: A #LrgParticleSystem
 * @mode: The blend mode
 *
 * Sets the particle blend mode.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_blend_mode      (LrgParticleSystem   *self,
                                                                 LrgParticleBlendMode mode);

/**
 * lrg_particle_system_get_position:
 * @self: A #LrgParticleSystem
 * @x: (out) (nullable): X position
 * @y: (out) (nullable): Y position
 * @z: (out) (nullable): Z position
 *
 * Gets the system world position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_get_position        (LrgParticleSystem   *self,
                                                                 gfloat              *x,
                                                                 gfloat              *y,
                                                                 gfloat              *z);

/**
 * lrg_particle_system_set_position:
 * @self: A #LrgParticleSystem
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the system world position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_position        (LrgParticleSystem   *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_system_get_loop:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system loops.
 *
 * Returns: %TRUE if looping
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_get_loop            (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_loop:
 * @self: A #LrgParticleSystem
 * @loop: Whether to loop
 *
 * Sets whether the system loops after all particles die.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_loop            (LrgParticleSystem   *self,
                                                                 gboolean             loop);

/**
 * lrg_particle_system_get_duration:
 * @self: A #LrgParticleSystem
 *
 * Gets the system duration (for non-looping systems).
 *
 * Returns: Duration in seconds (0 = infinite)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_system_get_duration        (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_duration:
 * @self: A #LrgParticleSystem
 * @duration: Duration in seconds (0 = infinite)
 *
 * Sets the system duration.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_duration        (LrgParticleSystem   *self,
                                                                 gfloat               duration);

/**
 * lrg_particle_system_get_time_scale:
 * @self: A #LrgParticleSystem
 *
 * Gets the time scale multiplier.
 *
 * Returns: Time scale
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_system_get_time_scale      (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_time_scale:
 * @self: A #LrgParticleSystem
 * @scale: Time scale multiplier
 *
 * Sets the time scale (for slow-motion or fast-forward effects).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_time_scale      (LrgParticleSystem   *self,
                                                                 gfloat               scale);

G_END_DECLS
