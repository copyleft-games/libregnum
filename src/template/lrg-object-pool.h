/* lrg-object-pool.h - Generic object pool for efficient object reuse
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgObjectPool provides a generic object pooling mechanism that reduces
 * allocation overhead by reusing objects. Objects must implement the
 * #LrgPoolable interface to be managed by a pool.
 *
 * ## When to Use Object Pooling
 *
 * Object pooling is beneficial for:
 * - Frequently created/destroyed objects (bullets, particles, enemies)
 * - Short-lived objects with predictable lifecycles
 * - Performance-critical code paths in the game loop
 *
 * ## Example Usage
 *
 * ```c
 * // Create a pool for bullets with initial size of 100
 * LrgObjectPool *bullet_pool = lrg_object_pool_new (MY_TYPE_BULLET, 100,
 *                                                    LRG_POOL_GROWTH_DOUBLE);
 *
 * // Pre-allocate objects for better performance
 * lrg_object_pool_prewarm (bullet_pool, 100);
 *
 * // Acquire a bullet from the pool
 * Bullet *bullet = MY_BULLET (lrg_object_pool_acquire (bullet_pool));
 * if (bullet != NULL)
 * {
 *     // Initialize the bullet
 *     bullet->x = player_x;
 *     bullet->y = player_y;
 *     bullet->velocity = 10.0f;
 *
 *     // Use the bullet...
 * }
 *
 * // When done, release back to the pool
 * lrg_object_pool_release (bullet_pool, LRG_POOLABLE (bullet));
 *
 * // Clean up
 * g_object_unref (bullet_pool);
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_OBJECT_POOL_H
#define LRG_OBJECT_POOL_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-poolable.h"

G_BEGIN_DECLS

#define LRG_TYPE_OBJECT_POOL (lrg_object_pool_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgObjectPool, lrg_object_pool, LRG, OBJECT_POOL, GObject)

/**
 * LrgObjectPoolForeachFunc:
 * @object: (transfer none): the poolable object
 * @user_data: user data passed to lrg_object_pool_foreach_active()
 *
 * Callback function for iterating over active pool objects.
 *
 * Returns: %FALSE to stop iteration, %TRUE to continue
 *
 * Since: 1.0
 */
typedef gboolean (*LrgObjectPoolForeachFunc) (LrgPoolable *object,
                                               gpointer     user_data);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_object_pool_new:
 * @object_type: the #GType of objects to pool (must implement #LrgPoolable)
 * @initial_size: initial pool capacity (0 = allocate on demand)
 * @growth_policy: how the pool should grow when exhausted
 *
 * Creates a new object pool for the specified type. The type must
 * implement the #LrgPoolable interface.
 *
 * The pool will pre-allocate @initial_size objects. When the pool
 * is exhausted, the @growth_policy determines whether and how to
 * allocate more objects.
 *
 * Returns: (transfer full): a new #LrgObjectPool, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgObjectPool *
lrg_object_pool_new (GType               object_type,
                     guint               initial_size,
                     LrgPoolGrowthPolicy growth_policy);

/**
 * lrg_object_pool_new_with_max:
 * @object_type: the #GType of objects to pool (must implement #LrgPoolable)
 * @initial_size: initial pool capacity (0 = allocate on demand)
 * @max_size: maximum pool size (0 = unlimited)
 * @growth_policy: how the pool should grow when exhausted
 *
 * Creates a new object pool with a maximum size limit.
 *
 * When the pool reaches @max_size, acquire() will return %NULL
 * if no objects are available.
 *
 * Returns: (transfer full): a new #LrgObjectPool, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgObjectPool *
lrg_object_pool_new_with_max (GType               object_type,
                              guint               initial_size,
                              guint               max_size,
                              LrgPoolGrowthPolicy growth_policy);

/* ==========================================================================
 * Pool Operations
 * ========================================================================== */

/**
 * lrg_object_pool_acquire:
 * @self: an #LrgObjectPool
 *
 * Acquires an object from the pool. If no objects are available,
 * the pool may allocate new ones based on its growth policy.
 *
 * The returned object will have its active state set to %TRUE.
 * The caller is responsible for initializing the object before use.
 *
 * Returns: (transfer none) (nullable): a pooled object, or %NULL if exhausted
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GObject *
lrg_object_pool_acquire (LrgObjectPool *self);

/**
 * lrg_object_pool_acquire_with_init:
 * @self: an #LrgObjectPool
 * @first_property_name: (nullable): name of first property to set, or %NULL
 * @...: value of first property, followed by more name/value pairs, NULL-terminated
 *
 * Acquires an object and sets properties in a single call.
 * This is equivalent to acquire() followed by g_object_set().
 *
 * Returns: (transfer none) (nullable): a pooled object, or %NULL if exhausted
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GObject *
lrg_object_pool_acquire_with_init (LrgObjectPool *self,
                                    const gchar   *first_property_name,
                                    ...);

/**
 * lrg_object_pool_release:
 * @self: an #LrgObjectPool
 * @object: (transfer none): the object to release
 *
 * Releases an object back to the pool. The object's reset() method
 * is called to reinitialize it, and its active state is set to %FALSE.
 *
 * The object must have been acquired from this pool. Passing an object
 * from a different pool or a non-pooled object is an error.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_release (LrgObjectPool *self,
                         LrgPoolable   *object);

/**
 * lrg_object_pool_prewarm:
 * @self: an #LrgObjectPool
 * @count: number of objects to pre-allocate
 *
 * Pre-allocates objects in the pool to avoid allocation during gameplay.
 * This is useful during loading screens or level transitions.
 *
 * Objects are created up to the specified count or the pool's max size,
 * whichever is smaller.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_prewarm (LrgObjectPool *self,
                         guint          count);

/**
 * lrg_object_pool_shrink_to_fit:
 * @self: an #LrgObjectPool
 *
 * Releases excess capacity by freeing inactive objects beyond
 * the initial pool size. Active objects are not affected.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_shrink_to_fit (LrgObjectPool *self);

/**
 * lrg_object_pool_clear:
 * @self: an #LrgObjectPool
 *
 * Releases all objects in the pool, both active and inactive.
 * After calling this, the pool will be empty.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_clear (LrgObjectPool *self);

/* ==========================================================================
 * Pool Information
 * ========================================================================== */

/**
 * lrg_object_pool_get_object_type:
 * @self: an #LrgObjectPool
 *
 * Gets the GType of objects managed by this pool.
 *
 * Returns: the #GType of pooled objects
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GType
lrg_object_pool_get_object_type (LrgObjectPool *self);

/**
 * lrg_object_pool_get_active_count:
 * @self: an #LrgObjectPool
 *
 * Gets the number of currently active (in-use) objects.
 *
 * Returns: number of active objects
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_object_pool_get_active_count (LrgObjectPool *self);

/**
 * lrg_object_pool_get_available_count:
 * @self: an #LrgObjectPool
 *
 * Gets the number of available (inactive) objects in the pool.
 *
 * Returns: number of available objects
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_object_pool_get_available_count (LrgObjectPool *self);

/**
 * lrg_object_pool_get_total_size:
 * @self: an #LrgObjectPool
 *
 * Gets the total number of objects (active + available) in the pool.
 *
 * Returns: total object count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_object_pool_get_total_size (LrgObjectPool *self);

/**
 * lrg_object_pool_get_max_size:
 * @self: an #LrgObjectPool
 *
 * Gets the maximum size of the pool. Returns 0 if unlimited.
 *
 * Returns: maximum pool size, or 0 if unlimited
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_object_pool_get_max_size (LrgObjectPool *self);

/**
 * lrg_object_pool_get_growth_policy:
 * @self: an #LrgObjectPool
 *
 * Gets the growth policy of the pool.
 *
 * Returns: the #LrgPoolGrowthPolicy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPoolGrowthPolicy
lrg_object_pool_get_growth_policy (LrgObjectPool *self);

/* ==========================================================================
 * Iteration
 * ========================================================================== */

/**
 * lrg_object_pool_foreach_active:
 * @self: an #LrgObjectPool
 * @callback: (scope call): function to call for each active object
 * @user_data: user data for @callback
 *
 * Iterates over all active objects in the pool. The callback can
 * return %FALSE to stop iteration early.
 *
 * It is safe to release objects from within the callback.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_foreach_active (LrgObjectPool            *self,
                                LrgObjectPoolForeachFunc  callback,
                                gpointer                  user_data);

/**
 * lrg_object_pool_release_all_active:
 * @self: an #LrgObjectPool
 *
 * Releases all currently active objects back to the pool.
 * This resets and deactivates all objects without destroying them.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_object_pool_release_all_active (LrgObjectPool *self);

G_END_DECLS

#endif /* LRG_OBJECT_POOL_H */
