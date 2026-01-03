/* lrg-poolable.h - Interface for poolable objects
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgPoolable is an interface for objects that can be managed by an object
 * pool. Pooled objects must implement reset() to reinitialize their state
 * when returned to the pool, enabling efficient object reuse without the
 * overhead of repeated allocation/deallocation.
 *
 * ## Why Object Pooling?
 *
 * Object pooling is essential for game performance in scenarios with:
 * - Frequent allocation/deallocation (bullets, particles, enemies)
 * - Short-lived objects with predictable lifecycles
 * - Memory pressure from GC/allocation overhead
 *
 * ## Interface Requirements
 *
 * Poolable objects must:
 * 1. Clear all state to initial values in reset()
 * 2. Clear references to other objects (prevent memory leaks)
 * 3. Track active/inactive status
 *
 * ## Example Implementation
 *
 * ```c
 * static void
 * bullet_reset (LrgPoolable *poolable)
 * {
 *     Bullet *self = MY_BULLET (poolable);
 *
 *     self->position = (GrlVector2){0, 0};
 *     self->velocity = (GrlVector2){0, 0};
 *     self->damage = 0;
 *     self->is_active = FALSE;
 *
 *     g_clear_object (&self->owner);
 * }
 *
 * static void
 * bullet_poolable_init (LrgPoolableInterface *iface)
 * {
 *     iface->reset = bullet_reset;
 *     iface->is_active = bullet_is_active;
 *     iface->set_active = bullet_set_active;
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (Bullet, bullet, G_TYPE_OBJECT,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_POOLABLE, bullet_poolable_init))
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_POOLABLE_H
#define LRG_POOLABLE_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* Forward declaration for LrgObjectPool */
typedef struct _LrgObjectPool LrgObjectPool;

#define LRG_TYPE_POOLABLE (lrg_poolable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgPoolable, lrg_poolable, LRG, POOLABLE, GObject)

/**
 * LrgPoolableInterface:
 * @parent_iface: the parent interface
 * @reset: Resets the object to its initial state for reuse
 * @is_active: Returns whether the object is currently active (in use)
 * @set_active: Sets the active state of the object
 * @get_pool: Returns the pool that owns this object (optional)
 *
 * Interface for objects that can be managed by an object pool.
 *
 * The `reset()` method is the most critical - it must completely
 * reinitialize the object to a clean state as if it were newly created.
 * This includes clearing all member variables and releasing any references
 * to other objects.
 *
 * Since: 1.0
 */
struct _LrgPoolableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgPoolableInterface::reset:
     * @self: an #LrgPoolable
     *
     * Resets the object to its initial state for reuse. This method
     * is called when the object is released back to the pool.
     *
     * Implementations MUST:
     * - Reset all member variables to default values
     * - Clear any references using g_clear_object() or g_clear_pointer()
     * - Set active state to %FALSE
     */
    void (*reset) (LrgPoolable *self);

    /**
     * LrgPoolableInterface::is_active:
     * @self: an #LrgPoolable
     *
     * Returns whether the object is currently active (in use).
     * Active objects are being used by the game and should not
     * be acquired from the pool.
     *
     * Returns: %TRUE if active, %FALSE if pooled
     */
    gboolean (*is_active) (LrgPoolable *self);

    /**
     * LrgPoolableInterface::set_active:
     * @self: an #LrgPoolable
     * @active: the new active state
     *
     * Sets the active state of the object. Called by the pool
     * when acquiring (TRUE) or releasing (FALSE) objects.
     */
    void (*set_active) (LrgPoolable *self,
                        gboolean     active);

    /**
     * LrgPoolableInterface::get_pool:
     * @self: an #LrgPoolable
     *
     * Returns the pool that owns this object. This is a back-reference
     * for objects that need to return themselves to their pool.
     *
     * Returns: (transfer none) (nullable): the owning #LrgObjectPool, or %NULL
     */
    LrgObjectPool * (*get_pool) (LrgPoolable *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_poolable_reset:
 * @self: an #LrgPoolable
 *
 * Resets the object to its initial state for reuse. This method
 * should reset all member variables and clear any references to
 * other objects. After calling reset(), the object should be in
 * the same state as if it were newly constructed.
 *
 * This is called automatically by the object pool when releasing
 * objects. You typically don't need to call this directly.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_poolable_reset (LrgPoolable *self);

/**
 * lrg_poolable_is_active:
 * @self: an #LrgPoolable
 *
 * Returns whether the object is currently active (in use).
 * Objects in the pool are inactive; objects acquired from
 * the pool are active.
 *
 * Returns: %TRUE if the object is active, %FALSE if pooled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_poolable_is_active (LrgPoolable *self);

/**
 * lrg_poolable_set_active:
 * @self: an #LrgPoolable
 * @active: whether the object is active
 *
 * Sets the active state of the object. This is typically called
 * by the object pool when acquiring (TRUE) or releasing (FALSE).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_poolable_set_active (LrgPoolable *self,
                         gboolean     active);

/**
 * lrg_poolable_get_pool:
 * @self: an #LrgPoolable
 *
 * Gets the pool that owns this object. This back-reference allows
 * objects to return themselves to their pool without needing to
 * track the pool externally.
 *
 * Returns: (transfer none) (nullable): the owning #LrgObjectPool, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgObjectPool *
lrg_poolable_get_pool (LrgPoolable *self);

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_poolable_release:
 * @self: an #LrgPoolable
 *
 * Convenience method that releases the object back to its pool.
 * Equivalent to calling lrg_object_pool_release() with this object's
 * pool. Does nothing if the object has no pool reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_poolable_release (LrgPoolable *self);

G_END_DECLS

#endif /* LRG_POOLABLE_H */
