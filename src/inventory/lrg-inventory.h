/* lrg-inventory.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgInventory - A container for item stacks.
 *
 * This is a derivable class that manages a collection of item slots.
 * It can be subclassed to create specialized containers like equipment
 * slots or shop inventories.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-item-def.h"
#include "lrg-item-stack.h"

G_BEGIN_DECLS

#define LRG_TYPE_INVENTORY (lrg_inventory_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgInventory, lrg_inventory, LRG, INVENTORY, GObject)

/**
 * LrgInventoryClass:
 * @parent_class: the parent class
 * @can_accept: virtual function to check if an item can be added
 * @on_item_added: called when an item is added
 * @on_item_removed: called when an item is removed
 *
 * The class structure for #LrgInventory.
 *
 * Since: 1.0
 */
struct _LrgInventoryClass
{
    GObjectClass parent_class;

    /**
     * LrgInventoryClass::can_accept:
     * @self: the inventory
     * @def: the item definition to check
     * @slot: the target slot (-1 for any)
     *
     * Checks if an item can be added to the inventory.
     * Default implementation returns TRUE if there's room.
     *
     * Returns: %TRUE if the item can be added
     */
    gboolean (* can_accept)      (LrgInventory *self,
                                  LrgItemDef   *def,
                                  gint          slot);

    /**
     * LrgInventoryClass::on_item_added:
     * @self: the inventory
     * @slot: the slot index
     * @stack: the item stack
     *
     * Called after an item is added to the inventory.
     */
    void (* on_item_added)       (LrgInventory *self,
                                  guint         slot,
                                  LrgItemStack *stack);

    /**
     * LrgInventoryClass::on_item_removed:
     * @self: the inventory
     * @slot: the slot index
     * @stack: the removed item stack
     *
     * Called after an item is removed from the inventory.
     */
    void (* on_item_removed)     (LrgInventory *self,
                                  guint         slot,
                                  LrgItemStack *stack);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_inventory_new:
 * @capacity: the number of slots
 *
 * Creates a new inventory with the specified capacity.
 *
 * Returns: (transfer full): a new #LrgInventory
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInventory *
lrg_inventory_new (guint capacity);

/* Properties */

/**
 * lrg_inventory_get_capacity:
 * @self: an #LrgInventory
 *
 * Gets the total number of slots.
 *
 * Returns: the capacity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_get_capacity (LrgInventory *self);

/**
 * lrg_inventory_set_capacity:
 * @self: an #LrgInventory
 * @capacity: the new capacity
 *
 * Sets the inventory capacity. If reduced, items in removed slots are lost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_inventory_set_capacity (LrgInventory *self,
                            guint         capacity);

/**
 * lrg_inventory_get_used_slots:
 * @self: an #LrgInventory
 *
 * Gets the number of slots that contain items.
 *
 * Returns: number of used slots
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_get_used_slots (LrgInventory *self);

/**
 * lrg_inventory_get_free_slots:
 * @self: an #LrgInventory
 *
 * Gets the number of empty slots.
 *
 * Returns: number of empty slots
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_get_free_slots (LrgInventory *self);

/**
 * lrg_inventory_is_full:
 * @self: an #LrgInventory
 *
 * Checks if all slots are occupied.
 *
 * Returns: %TRUE if full
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_is_full (LrgInventory *self);

/**
 * lrg_inventory_is_empty:
 * @self: an #LrgInventory
 *
 * Checks if all slots are empty.
 *
 * Returns: %TRUE if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_is_empty (LrgInventory *self);

/* Slot Access */

/**
 * lrg_inventory_get_slot:
 * @self: an #LrgInventory
 * @slot: the slot index
 *
 * Gets the item stack in a slot.
 *
 * Returns: (transfer none) (nullable): the item stack, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_inventory_get_slot (LrgInventory *self,
                        guint         slot);

/**
 * lrg_inventory_set_slot:
 * @self: an #LrgInventory
 * @slot: the slot index
 * @stack: (nullable): the item stack to set, or %NULL to clear
 *
 * Sets the item stack in a slot. The previous contents are replaced.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_set_slot (LrgInventory *self,
                        guint         slot,
                        LrgItemStack *stack);

/**
 * lrg_inventory_clear_slot:
 * @self: an #LrgInventory
 * @slot: the slot index
 *
 * Clears a slot, removing any items.
 *
 * Returns: (transfer full) (nullable): the removed stack, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_inventory_clear_slot (LrgInventory *self,
                          guint         slot);

/**
 * lrg_inventory_is_slot_empty:
 * @self: an #LrgInventory
 * @slot: the slot index
 *
 * Checks if a slot is empty.
 *
 * Returns: %TRUE if the slot is empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_is_slot_empty (LrgInventory *self,
                             guint         slot);

/**
 * lrg_inventory_find_empty_slot:
 * @self: an #LrgInventory
 *
 * Finds the first empty slot.
 *
 * Returns: the slot index, or -1 if none available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_inventory_find_empty_slot (LrgInventory *self);

/* Adding Items */

/**
 * lrg_inventory_add_item:
 * @self: an #LrgInventory
 * @def: the item definition
 * @quantity: number to add
 *
 * Adds items to the inventory, stacking where possible.
 * First tries to add to existing stacks, then uses empty slots.
 *
 * Returns: the number of items actually added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_add_item (LrgInventory *self,
                        LrgItemDef   *def,
                        guint         quantity);

/**
 * lrg_inventory_add_stack:
 * @self: an #LrgInventory
 * @stack: the item stack to add
 *
 * Adds an item stack to the inventory.
 * The stack is merged with existing stacks where possible.
 *
 * Returns: the number of items actually added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_add_stack (LrgInventory *self,
                         LrgItemStack *stack);

/**
 * lrg_inventory_add_to_slot:
 * @self: an #LrgInventory
 * @slot: the target slot
 * @def: the item definition
 * @quantity: number to add
 *
 * Adds items to a specific slot.
 *
 * Returns: the number of items actually added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_add_to_slot (LrgInventory *self,
                           guint         slot,
                           LrgItemDef   *def,
                           guint         quantity);

/* Removing Items */

/**
 * lrg_inventory_remove_item:
 * @self: an #LrgInventory
 * @item_id: the item ID to remove
 * @quantity: number to remove
 *
 * Removes items by ID from the inventory.
 *
 * Returns: the number of items actually removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_remove_item (LrgInventory *self,
                           const gchar  *item_id,
                           guint         quantity);

/**
 * lrg_inventory_remove_from_slot:
 * @self: an #LrgInventory
 * @slot: the slot index
 * @quantity: number to remove
 *
 * Removes items from a specific slot.
 *
 * Returns: the number of items actually removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_remove_from_slot (LrgInventory *self,
                                guint         slot,
                                guint         quantity);

/* Finding Items */

/**
 * lrg_inventory_find_item:
 * @self: an #LrgInventory
 * @item_id: the item ID to find
 *
 * Finds the first stack containing the specified item.
 *
 * Returns: (transfer none) (nullable): the item stack, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_inventory_find_item (LrgInventory *self,
                         const gchar  *item_id);

/**
 * lrg_inventory_find_item_slot:
 * @self: an #LrgInventory
 * @item_id: the item ID to find
 *
 * Finds the slot containing the specified item.
 *
 * Returns: the slot index, or -1 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_inventory_find_item_slot (LrgInventory *self,
                              const gchar  *item_id);

/**
 * lrg_inventory_count_item:
 * @self: an #LrgInventory
 * @item_id: the item ID to count
 *
 * Counts the total quantity of an item across all slots.
 *
 * Returns: total quantity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_count_item (LrgInventory *self,
                          const gchar  *item_id);

/**
 * lrg_inventory_has_item:
 * @self: an #LrgInventory
 * @item_id: the item ID to check
 * @quantity: minimum quantity required
 *
 * Checks if the inventory contains at least the specified quantity.
 *
 * Returns: %TRUE if the inventory has enough
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_has_item (LrgInventory *self,
                        const gchar  *item_id,
                        guint         quantity);

/* Slot Operations */

/**
 * lrg_inventory_swap_slots:
 * @self: an #LrgInventory
 * @slot_a: first slot
 * @slot_b: second slot
 *
 * Swaps the contents of two slots.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_swap_slots (LrgInventory *self,
                          guint         slot_a,
                          guint         slot_b);

/**
 * lrg_inventory_move_to_slot:
 * @self: an #LrgInventory
 * @from_slot: source slot
 * @to_slot: destination slot
 * @quantity: amount to move (-1 for all)
 *
 * Moves items from one slot to another.
 *
 * Returns: the number of items actually moved
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_inventory_move_to_slot (LrgInventory *self,
                            guint         from_slot,
                            guint         to_slot,
                            gint          quantity);

/**
 * lrg_inventory_sort:
 * @self: an #LrgInventory
 *
 * Sorts the inventory by item type and ID.
 * Combines partial stacks.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_inventory_sort (LrgInventory *self);

/**
 * lrg_inventory_clear:
 * @self: an #LrgInventory
 *
 * Removes all items from the inventory.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_inventory_clear (LrgInventory *self);

/* Virtual Function Wrappers */

/**
 * lrg_inventory_can_accept:
 * @self: an #LrgInventory
 * @def: the item definition
 * @slot: target slot (-1 for any)
 *
 * Checks if an item can be added to the inventory.
 *
 * Returns: %TRUE if the item can be added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_inventory_can_accept (LrgInventory *self,
                          LrgItemDef   *def,
                          gint          slot);

G_END_DECLS
