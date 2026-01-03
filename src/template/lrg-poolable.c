/* lrg-poolable.c - Interface for poolable objects
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-poolable.h"
#include "lrg-object-pool.h"

/**
 * SECTION:lrg-poolable
 * @title: LrgPoolable
 * @short_description: Interface for poolable objects
 * @see_also: #LrgObjectPool
 *
 * #LrgPoolable is an interface for objects that can be managed by an
 * object pool. Object pooling is a performance optimization that reuses
 * objects instead of repeatedly allocating and freeing them.
 *
 * ## Implementing LrgPoolable
 *
 * To make a class poolable, implement the #LrgPoolable interface:
 *
 * |[<!-- language="C" -->
 * typedef struct
 * {
 *     GObject parent_instance;
 *
 *     gboolean is_active;
 *     LrgObjectPool *pool;
 *
 *     // Object-specific data
 *     gfloat x, y;
 *     gfloat velocity_x, velocity_y;
 *     gint damage;
 * } Bullet;
 *
 * static void
 * bullet_reset (LrgPoolable *poolable)
 * {
 *     Bullet *self = MY_BULLET (poolable);
 *
 *     self->is_active = FALSE;
 *     self->x = 0.0f;
 *     self->y = 0.0f;
 *     self->velocity_x = 0.0f;
 *     self->velocity_y = 0.0f;
 *     self->damage = 0;
 * }
 *
 * static gboolean
 * bullet_is_active (LrgPoolable *poolable)
 * {
 *     return MY_BULLET (poolable)->is_active;
 * }
 *
 * static void
 * bullet_set_active (LrgPoolable *poolable, gboolean active)
 * {
 *     MY_BULLET (poolable)->is_active = active;
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
 * ]|
 *
 * ## Using Pooled Objects
 *
 * |[<!-- language="C" -->
 * // Create pool for bullets
 * LrgObjectPool *pool = lrg_object_pool_new (MY_TYPE_BULLET, 100,
 *                                             LRG_POOL_GROWTH_DOUBLE);
 *
 * // Acquire a bullet
 * Bullet *bullet = LRG_BULLET (lrg_object_pool_acquire (pool));
 * bullet->x = player_x;
 * bullet->y = player_y;
 * bullet->velocity_x = 10.0f;
 * bullet->damage = 5;
 *
 * // When bullet is done, release it
 * lrg_object_pool_release (pool, LRG_POOLABLE (bullet));
 * // Or use the convenience method:
 * lrg_poolable_release (LRG_POOLABLE (bullet));
 * ]|
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgPoolable, lrg_poolable, G_TYPE_OBJECT)

/* ==========================================================================
 * Default Implementations
 * ========================================================================== */

static void
lrg_poolable_real_reset (LrgPoolable *self)
{
    /* Default implementation does nothing - subclasses must implement */
}

static gboolean
lrg_poolable_real_is_active (LrgPoolable *self)
{
    /* Default returns FALSE - object is inactive/pooled */
    return FALSE;
}

static void
lrg_poolable_real_set_active (LrgPoolable *self,
                               gboolean     active)
{
    /* Default implementation does nothing - subclasses must implement */
}

static LrgObjectPool *
lrg_poolable_real_get_pool (LrgPoolable *self)
{
    /* Default returns NULL - no pool reference */
    return NULL;
}

static void
lrg_poolable_default_init (LrgPoolableInterface *iface)
{
    iface->reset = lrg_poolable_real_reset;
    iface->is_active = lrg_poolable_real_is_active;
    iface->set_active = lrg_poolable_real_set_active;
    iface->get_pool = lrg_poolable_real_get_pool;
}

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_poolable_reset:
 * @self: an #LrgPoolable
 *
 * Resets the object to its initial state for reuse.
 *
 * Since: 1.0
 */
void
lrg_poolable_reset (LrgPoolable *self)
{
    LrgPoolableInterface *iface;

    g_return_if_fail (LRG_IS_POOLABLE (self));

    iface = LRG_POOLABLE_GET_IFACE (self);
    if (iface->reset != NULL)
        iface->reset (self);
}

/**
 * lrg_poolable_is_active:
 * @self: an #LrgPoolable
 *
 * Returns whether the object is currently active.
 *
 * Returns: %TRUE if active, %FALSE if pooled
 *
 * Since: 1.0
 */
gboolean
lrg_poolable_is_active (LrgPoolable *self)
{
    LrgPoolableInterface *iface;

    g_return_val_if_fail (LRG_IS_POOLABLE (self), FALSE);

    iface = LRG_POOLABLE_GET_IFACE (self);
    if (iface->is_active != NULL)
        return iface->is_active (self);

    return FALSE;
}

/**
 * lrg_poolable_set_active:
 * @self: an #LrgPoolable
 * @active: the new active state
 *
 * Sets the active state of the object.
 *
 * Since: 1.0
 */
void
lrg_poolable_set_active (LrgPoolable *self,
                         gboolean     active)
{
    LrgPoolableInterface *iface;

    g_return_if_fail (LRG_IS_POOLABLE (self));

    iface = LRG_POOLABLE_GET_IFACE (self);
    if (iface->set_active != NULL)
        iface->set_active (self, active);
}

/**
 * lrg_poolable_get_pool:
 * @self: an #LrgPoolable
 *
 * Gets the pool that owns this object.
 *
 * Returns: (transfer none) (nullable): the owning #LrgObjectPool, or %NULL
 *
 * Since: 1.0
 */
LrgObjectPool *
lrg_poolable_get_pool (LrgPoolable *self)
{
    LrgPoolableInterface *iface;

    g_return_val_if_fail (LRG_IS_POOLABLE (self), NULL);

    iface = LRG_POOLABLE_GET_IFACE (self);
    if (iface->get_pool != NULL)
        return iface->get_pool (self);

    return NULL;
}

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_poolable_release:
 * @self: an #LrgPoolable
 *
 * Releases the object back to its pool.
 *
 * Since: 1.0
 */
void
lrg_poolable_release (LrgPoolable *self)
{
    LrgObjectPool *pool;

    g_return_if_fail (LRG_IS_POOLABLE (self));

    pool = lrg_poolable_get_pool (self);
    if (pool != NULL)
        lrg_object_pool_release (pool, self);
}
