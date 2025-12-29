/* lrg-resource-pool.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgResourcePool - Container for storing resource quantities.
 *
 * A resource pool holds quantities of various resources and provides
 * methods for adding, removing, transferring, and querying amounts.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-resource.h"

G_BEGIN_DECLS

#define LRG_TYPE_RESOURCE_POOL (lrg_resource_pool_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgResourcePool, lrg_resource_pool, LRG, RESOURCE_POOL, GObject)

/* Construction */

/**
 * lrg_resource_pool_new:
 *
 * Creates a new empty resource pool.
 *
 * Returns: (transfer full): A new #LrgResourcePool
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResourcePool *
lrg_resource_pool_new (void);

/* Resource Operations */

/**
 * lrg_resource_pool_get:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to query
 *
 * Gets the current amount of a resource in the pool.
 *
 * Returns: the amount of the resource, or 0.0 if not present
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_get (LrgResourcePool *self,
                       LrgResource     *resource);

/**
 * lrg_resource_pool_get_by_id:
 * @self: an #LrgResourcePool
 * @resource_id: the resource ID to query
 *
 * Gets the current amount of a resource by ID.
 *
 * Returns: the amount of the resource, or 0.0 if not present
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_get_by_id (LrgResourcePool *self,
                             const gchar     *resource_id);

/**
 * lrg_resource_pool_set:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to set
 * @amount: the amount to set
 *
 * Sets the amount of a resource directly. The amount will be clamped
 * to the resource's valid range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_set (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount);

/**
 * lrg_resource_pool_add:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to add
 * @amount: the amount to add (must be positive)
 *
 * Adds an amount of a resource to the pool. The result will be clamped
 * to the resource's maximum value.
 *
 * Returns: the actual amount added (may be less if clamped)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_add (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount);

/**
 * lrg_resource_pool_remove:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to remove
 * @amount: the amount to remove (must be positive)
 *
 * Removes an amount of a resource from the pool. Fails if insufficient
 * resources are available (respects min-value).
 *
 * Returns: %TRUE if the full amount was removed, %FALSE if insufficient
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_pool_remove (LrgResourcePool *self,
                          LrgResource     *resource,
                          gdouble          amount);

/**
 * lrg_resource_pool_remove_clamped:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to remove
 * @amount: the desired amount to remove (must be positive)
 *
 * Removes up to the specified amount of a resource from the pool.
 * Unlike lrg_resource_pool_remove(), this will remove as much as
 * possible without failing.
 *
 * Returns: the actual amount removed (may be less than requested)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_remove_clamped (LrgResourcePool *self,
                                  LrgResource     *resource,
                                  gdouble          amount);

/**
 * lrg_resource_pool_has:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to check
 * @amount: the amount to check for
 *
 * Checks if the pool has at least the specified amount of a resource.
 *
 * Returns: %TRUE if the pool has at least @amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_pool_has (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount);

/**
 * lrg_resource_pool_transfer:
 * @self: the source #LrgResourcePool
 * @destination: the destination #LrgResourcePool
 * @resource: the #LrgResource to transfer
 * @amount: the amount to transfer (must be positive)
 *
 * Transfers an amount of a resource from this pool to another.
 * Fails if the source has insufficient resources.
 *
 * Returns: %TRUE if the transfer succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_pool_transfer (LrgResourcePool *self,
                            LrgResourcePool *destination,
                            LrgResource     *resource,
                            gdouble          amount);

/**
 * lrg_resource_pool_transfer_all:
 * @self: the source #LrgResourcePool
 * @destination: the destination #LrgResourcePool
 * @resource: the #LrgResource to transfer
 *
 * Transfers all of a resource from this pool to another.
 *
 * Returns: the amount transferred
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_transfer_all (LrgResourcePool *self,
                                LrgResourcePool *destination,
                                LrgResource     *resource);

/**
 * lrg_resource_pool_clear:
 * @self: an #LrgResourcePool
 *
 * Removes all resources from the pool.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_clear (LrgResourcePool *self);

/**
 * lrg_resource_pool_clear_resource:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to clear
 *
 * Removes a specific resource from the pool entirely.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_clear_resource (LrgResourcePool *self,
                                  LrgResource     *resource);

/* Query Operations */

/**
 * lrg_resource_pool_is_empty:
 * @self: an #LrgResourcePool
 *
 * Checks if the pool contains no resources.
 *
 * Returns: %TRUE if the pool is empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_pool_is_empty (LrgResourcePool *self);

/**
 * lrg_resource_pool_contains:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource to check
 *
 * Checks if the pool contains any amount of the specified resource.
 *
 * Returns: %TRUE if the resource is present (even if amount is 0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_pool_contains (LrgResourcePool *self,
                            LrgResource     *resource);

/**
 * lrg_resource_pool_get_resources:
 * @self: an #LrgResourcePool
 *
 * Gets a list of all resources in the pool.
 *
 * Returns: (transfer container) (element-type LrgResource): list of resources
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_resource_pool_get_resources (LrgResourcePool *self);

/**
 * lrg_resource_pool_get_count:
 * @self: an #LrgResourcePool
 *
 * Gets the number of different resource types in the pool.
 *
 * Returns: number of resource types
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_resource_pool_get_count (LrgResourcePool *self);

/**
 * lrg_resource_pool_foreach:
 * @self: an #LrgResourcePool
 * @func: (scope call): callback function
 * @user_data: (closure): data to pass to the callback
 *
 * Calls a function for each resource in the pool.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_foreach (LrgResourcePool *self,
                           GFunc            func,
                           gpointer         user_data);

/* Multiplier Support */

/**
 * lrg_resource_pool_set_multiplier:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource
 * @multiplier: the multiplier value (1.0 = normal)
 *
 * Sets a multiplier for a resource. All additions will be multiplied
 * by this value. Useful for bonuses, prestige effects, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_set_multiplier (LrgResourcePool *self,
                                  LrgResource     *resource,
                                  gdouble          multiplier);

/**
 * lrg_resource_pool_get_multiplier:
 * @self: an #LrgResourcePool
 * @resource: the #LrgResource
 *
 * Gets the multiplier for a resource.
 *
 * Returns: the multiplier (1.0 = normal)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_get_multiplier (LrgResourcePool *self,
                                  LrgResource     *resource);

/**
 * lrg_resource_pool_set_global_multiplier:
 * @self: an #LrgResourcePool
 * @multiplier: the global multiplier value (1.0 = normal)
 *
 * Sets a global multiplier applied to all resource additions.
 * This stacks multiplicatively with per-resource multipliers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_pool_set_global_multiplier (LrgResourcePool *self,
                                         gdouble          multiplier);

/**
 * lrg_resource_pool_get_global_multiplier:
 * @self: an #LrgResourcePool
 *
 * Gets the global multiplier.
 *
 * Returns: the global multiplier (1.0 = normal)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_pool_get_global_multiplier (LrgResourcePool *self);

G_END_DECLS
