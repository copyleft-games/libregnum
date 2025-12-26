/* lrg-item-stack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgItemStack - A stack of items with quantity and instance data.
 *
 * This is a boxed type representing an actual stack of items.
 * Each stack references an LrgItemDef and has a quantity.
 * Instance-specific data (durability, enchantments) can be stored.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-item-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_ITEM_STACK (lrg_item_stack_get_type ())

/**
 * LrgItemStack:
 *
 * A stack of items in an inventory slot.
 *
 * #LrgItemStack is a reference-counted boxed type representing
 * one or more items of the same type. Use lrg_item_stack_ref()
 * and lrg_item_stack_unref() to manage its lifetime.
 *
 * Since: 1.0
 */
typedef struct _LrgItemStack LrgItemStack;

LRG_AVAILABLE_IN_ALL
GType lrg_item_stack_get_type (void) G_GNUC_CONST;

/* Construction */

/**
 * lrg_item_stack_new:
 * @def: the item definition
 * @quantity: initial quantity
 *
 * Creates a new item stack.
 *
 * Returns: (transfer full): a new #LrgItemStack
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_item_stack_new (LrgItemDef *def,
                    guint       quantity);

/**
 * lrg_item_stack_ref:
 * @self: an #LrgItemStack
 *
 * Increases the reference count by one.
 *
 * Returns: (transfer full): the #LrgItemStack
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_item_stack_ref (LrgItemStack *self);

/**
 * lrg_item_stack_unref:
 * @self: an #LrgItemStack
 *
 * Decreases the reference count by one. When it reaches zero,
 * the stack is freed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_unref (LrgItemStack *self);

/**
 * lrg_item_stack_copy:
 * @self: an #LrgItemStack
 *
 * Creates a deep copy of the item stack.
 *
 * Returns: (transfer full): a new #LrgItemStack
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_item_stack_copy (const LrgItemStack *self);

/* Properties */

/**
 * lrg_item_stack_get_def:
 * @self: an #LrgItemStack
 *
 * Gets the item definition.
 *
 * Returns: (transfer none): the #LrgItemDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemDef *
lrg_item_stack_get_def (const LrgItemStack *self);

/**
 * lrg_item_stack_get_quantity:
 * @self: an #LrgItemStack
 *
 * Gets the current quantity.
 *
 * Returns: the quantity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_get_quantity (const LrgItemStack *self);

/**
 * lrg_item_stack_set_quantity:
 * @self: an #LrgItemStack
 * @quantity: new quantity
 *
 * Sets the quantity. Clamped to max_stack from the item definition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_set_quantity (LrgItemStack *self,
                             guint         quantity);

/**
 * lrg_item_stack_get_max_quantity:
 * @self: an #LrgItemStack
 *
 * Gets the maximum quantity this stack can hold.
 *
 * Returns: the maximum quantity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_get_max_quantity (const LrgItemStack *self);

/**
 * lrg_item_stack_is_full:
 * @self: an #LrgItemStack
 *
 * Checks if the stack is at maximum capacity.
 *
 * Returns: %TRUE if full
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_stack_is_full (const LrgItemStack *self);

/**
 * lrg_item_stack_is_empty:
 * @self: an #LrgItemStack
 *
 * Checks if the stack is empty.
 *
 * Returns: %TRUE if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_stack_is_empty (const LrgItemStack *self);

/**
 * lrg_item_stack_get_space_remaining:
 * @self: an #LrgItemStack
 *
 * Gets how many more items can be added to this stack.
 *
 * Returns: remaining space
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_get_space_remaining (const LrgItemStack *self);

/* Quantity Operations */

/**
 * lrg_item_stack_add:
 * @self: an #LrgItemStack
 * @amount: amount to add
 *
 * Adds items to the stack, up to max_stack.
 *
 * Returns: the actual amount added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_add (LrgItemStack *self,
                    guint         amount);

/**
 * lrg_item_stack_remove:
 * @self: an #LrgItemStack
 * @amount: amount to remove
 *
 * Removes items from the stack.
 *
 * Returns: the actual amount removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_remove (LrgItemStack *self,
                       guint         amount);

/**
 * lrg_item_stack_split:
 * @self: an #LrgItemStack
 * @amount: amount to split off
 *
 * Splits the stack, creating a new stack with the specified amount.
 * The amount is removed from this stack.
 *
 * Returns: (transfer full) (nullable): a new #LrgItemStack, or %NULL if
 *          amount is 0 or greater than current quantity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_item_stack_split (LrgItemStack *self,
                      guint         amount);

/**
 * lrg_item_stack_can_merge:
 * @self: an #LrgItemStack
 * @other: another #LrgItemStack
 *
 * Checks if two stacks can be merged together.
 *
 * Returns: %TRUE if the stacks can merge
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_stack_can_merge (const LrgItemStack *self,
                          const LrgItemStack *other);

/**
 * lrg_item_stack_merge:
 * @self: an #LrgItemStack
 * @other: another #LrgItemStack to merge from
 *
 * Merges items from @other into @self, up to max_stack.
 * The quantity is removed from @other.
 *
 * Returns: the amount merged
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_merge (LrgItemStack *self,
                      LrgItemStack *other);

/* Instance Data */

/**
 * lrg_item_stack_get_data_int:
 * @self: an #LrgItemStack
 * @key: the data key
 * @default_value: value to return if key not found
 *
 * Gets instance-specific integer data.
 *
 * Returns: the data value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_item_stack_get_data_int (const LrgItemStack *self,
                             const gchar        *key,
                             gint                default_value);

/**
 * lrg_item_stack_set_data_int:
 * @self: an #LrgItemStack
 * @key: the data key
 * @value: the value
 *
 * Sets instance-specific integer data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_set_data_int (LrgItemStack *self,
                             const gchar  *key,
                             gint          value);

/**
 * lrg_item_stack_get_data_float:
 * @self: an #LrgItemStack
 * @key: the data key
 * @default_value: value to return if key not found
 *
 * Gets instance-specific float data.
 *
 * Returns: the data value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_item_stack_get_data_float (const LrgItemStack *self,
                               const gchar        *key,
                               gfloat              default_value);

/**
 * lrg_item_stack_set_data_float:
 * @self: an #LrgItemStack
 * @key: the data key
 * @value: the value
 *
 * Sets instance-specific float data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_set_data_float (LrgItemStack *self,
                               const gchar  *key,
                               gfloat        value);

/**
 * lrg_item_stack_get_data_string:
 * @self: an #LrgItemStack
 * @key: the data key
 *
 * Gets instance-specific string data.
 *
 * Returns: (transfer none) (nullable): the data value or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_item_stack_get_data_string (const LrgItemStack *self,
                                const gchar        *key);

/**
 * lrg_item_stack_set_data_string:
 * @self: an #LrgItemStack
 * @key: the data key
 * @value: (nullable): the value
 *
 * Sets instance-specific string data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_set_data_string (LrgItemStack *self,
                                const gchar  *key,
                                const gchar  *value);

/**
 * lrg_item_stack_has_data:
 * @self: an #LrgItemStack
 * @key: the data key
 *
 * Checks if instance data exists for a key.
 *
 * Returns: %TRUE if the key exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_stack_has_data (const LrgItemStack *self,
                         const gchar        *key);

/**
 * lrg_item_stack_remove_data:
 * @self: an #LrgItemStack
 * @key: the data key
 *
 * Removes instance data for a key.
 *
 * Returns: %TRUE if the key was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_stack_remove_data (LrgItemStack *self,
                            const gchar  *key);

/**
 * lrg_item_stack_clear_data:
 * @self: an #LrgItemStack
 *
 * Removes all instance data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_stack_clear_data (LrgItemStack *self);

/* Usage */

/**
 * lrg_item_stack_use:
 * @self: an #LrgItemStack
 * @owner: (nullable): the object using the item
 * @quantity: how many to use
 *
 * Uses items from this stack. Calls the item definition's on_use
 * and removes the quantity if consumed.
 *
 * Returns: the number of items consumed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_stack_use (LrgItemStack *self,
                    GObject      *owner,
                    guint         quantity);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgItemStack, lrg_item_stack_unref)

G_END_DECLS
