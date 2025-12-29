/* lrg-particle-pool.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Object pool for efficient particle memory management.
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

#define LRG_TYPE_PARTICLE_POOL (lrg_particle_pool_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticlePool, lrg_particle_pool, LRG, PARTICLE_POOL, GObject)

/**
 * LrgParticlePoolIterFunc:
 * @particle: The current particle
 * @user_data: User-provided data
 *
 * Callback function for iterating over particles.
 *
 * Returns: %TRUE to continue iteration, %FALSE to stop
 */
typedef gboolean (*LrgParticlePoolIterFunc) (LrgParticle *particle,
                                              gpointer     user_data);

/**
 * lrg_particle_pool_new:
 * @initial_capacity: Initial number of particles to allocate
 *
 * Creates a new particle pool with the specified initial capacity.
 * The pool will grow automatically if more particles are needed,
 * based on the grow policy.
 *
 * Returns: (transfer full): A new #LrgParticlePool
 */
LRG_AVAILABLE_IN_ALL
LrgParticlePool *   lrg_particle_pool_new               (guint               initial_capacity);

/**
 * lrg_particle_pool_new_with_policy:
 * @initial_capacity: Initial number of particles to allocate
 * @policy: The grow policy to use when the pool is full
 * @max_capacity: Maximum capacity (0 for unlimited)
 *
 * Creates a new particle pool with custom growth settings.
 *
 * Returns: (transfer full): A new #LrgParticlePool
 */
LRG_AVAILABLE_IN_ALL
LrgParticlePool *   lrg_particle_pool_new_with_policy   (guint               initial_capacity,
                                                         LrgPoolGrowPolicy   policy,
                                                         guint               max_capacity);

/**
 * lrg_particle_pool_acquire:
 * @self: A #LrgParticlePool
 *
 * Acquires a particle from the pool. If no free particles are available,
 * the pool may grow according to its policy, or the oldest alive particle
 * may be recycled.
 *
 * The returned particle is marked as alive but otherwise uninitialized.
 * Use lrg_particle_spawn() to properly initialize it.
 *
 * Returns: (transfer none) (nullable): A particle from the pool, or %NULL if
 *          the pool is full and cannot grow
 */
LRG_AVAILABLE_IN_ALL
LrgParticle *       lrg_particle_pool_acquire           (LrgParticlePool    *self);

/**
 * lrg_particle_pool_release:
 * @self: A #LrgParticlePool
 * @particle: The particle to release
 *
 * Releases a particle back to the pool. The particle is reset and
 * marked as dead. This should be called when a particle's lifetime
 * expires or when it should be explicitly killed.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_release           (LrgParticlePool    *self,
                                                         LrgParticle        *particle);

/**
 * lrg_particle_pool_release_dead:
 * @self: A #LrgParticlePool
 *
 * Releases all dead particles back to the free list.
 * This is typically called after updating all particles.
 *
 * Returns: The number of particles released
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_release_dead      (LrgParticlePool    *self);

/**
 * lrg_particle_pool_get_capacity:
 * @self: A #LrgParticlePool
 *
 * Gets the total capacity of the pool (alive + dead particles).
 *
 * Returns: The pool capacity
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_get_capacity      (LrgParticlePool    *self);

/**
 * lrg_particle_pool_get_alive_count:
 * @self: A #LrgParticlePool
 *
 * Gets the number of currently alive particles.
 *
 * Returns: The number of alive particles
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_get_alive_count   (LrgParticlePool    *self);

/**
 * lrg_particle_pool_get_free_count:
 * @self: A #LrgParticlePool
 *
 * Gets the number of free (dead) particles available for acquisition.
 *
 * Returns: The number of free particles
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_get_free_count    (LrgParticlePool    *self);

/**
 * lrg_particle_pool_is_full:
 * @self: A #LrgParticlePool
 *
 * Checks if the pool is full (no free particles and cannot grow).
 *
 * Returns: %TRUE if the pool is full
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_particle_pool_is_full           (LrgParticlePool    *self);

/**
 * lrg_particle_pool_is_empty:
 * @self: A #LrgParticlePool
 *
 * Checks if the pool has no alive particles.
 *
 * Returns: %TRUE if no particles are alive
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_particle_pool_is_empty          (LrgParticlePool    *self);

/**
 * lrg_particle_pool_clear:
 * @self: A #LrgParticlePool
 *
 * Kills and releases all particles in the pool.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_clear             (LrgParticlePool    *self);

/**
 * lrg_particle_pool_foreach_alive:
 * @self: A #LrgParticlePool
 * @func: (scope call): Callback function
 * @user_data: (closure): User data for the callback
 *
 * Iterates over all alive particles in the pool.
 * It is safe to kill particles during iteration.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_foreach_alive     (LrgParticlePool        *self,
                                                         LrgParticlePoolIterFunc func,
                                                         gpointer                user_data);

/**
 * lrg_particle_pool_get_particles:
 * @self: A #LrgParticlePool
 * @out_count: (out) (optional): Location to store the count
 *
 * Gets direct access to the particle array.
 * This is useful for batch processing or GPU upload.
 *
 * Returns: (transfer none) (array length=out_count): The particle array
 */
LRG_AVAILABLE_IN_ALL
LrgParticle *       lrg_particle_pool_get_particles     (LrgParticlePool    *self,
                                                         guint              *out_count);

/**
 * lrg_particle_pool_update_all:
 * @self: A #LrgParticlePool
 * @delta_time: Time step in seconds
 *
 * Updates all alive particles by one time step.
 * Dead particles are automatically released back to the pool.
 *
 * Returns: The number of particles still alive after update
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_update_all        (LrgParticlePool    *self,
                                                         gfloat              delta_time);

/**
 * lrg_particle_pool_get_grow_policy:
 * @self: A #LrgParticlePool
 *
 * Gets the current grow policy.
 *
 * Returns: The grow policy
 */
LRG_AVAILABLE_IN_ALL
LrgPoolGrowPolicy   lrg_particle_pool_get_grow_policy   (LrgParticlePool    *self);

/**
 * lrg_particle_pool_set_grow_policy:
 * @self: A #LrgParticlePool
 * @policy: The new grow policy
 *
 * Sets the grow policy for when the pool is full.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_set_grow_policy   (LrgParticlePool    *self,
                                                         LrgPoolGrowPolicy   policy);

/**
 * lrg_particle_pool_get_max_capacity:
 * @self: A #LrgParticlePool
 *
 * Gets the maximum capacity (0 = unlimited).
 *
 * Returns: The maximum capacity
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_particle_pool_get_max_capacity  (LrgParticlePool    *self);

/**
 * lrg_particle_pool_set_max_capacity:
 * @self: A #LrgParticlePool
 * @max_capacity: The new maximum capacity (0 = unlimited)
 *
 * Sets the maximum capacity. Does not shrink if already larger.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_set_max_capacity  (LrgParticlePool    *self,
                                                         guint               max_capacity);

/**
 * lrg_particle_pool_reserve:
 * @self: A #LrgParticlePool
 * @capacity: The desired minimum capacity
 *
 * Ensures the pool has at least the specified capacity.
 * Does nothing if the pool already has sufficient capacity.
 *
 * Returns: %TRUE if the pool now has the requested capacity
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_particle_pool_reserve           (LrgParticlePool    *self,
                                                         guint               capacity);

/**
 * lrg_particle_pool_shrink_to_fit:
 * @self: A #LrgParticlePool
 *
 * Shrinks the pool to fit the current number of alive particles.
 * Free particles are discarded. This can help reduce memory usage
 * after a large burst of particles has died.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_particle_pool_shrink_to_fit     (LrgParticlePool    *self);

G_END_DECLS
