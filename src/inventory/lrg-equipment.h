/* lrg-equipment.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEquipment - Equipment slot management.
 *
 * Manages equipped items in specific slots (head, chest, weapon, etc.).
 * Each slot can hold one item stack at a time.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-item-def.h"
#include "lrg-item-stack.h"

G_BEGIN_DECLS

#define LRG_TYPE_EQUIPMENT (lrg_equipment_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEquipment, lrg_equipment, LRG, EQUIPMENT, GObject)

/* Construction */

/**
 * lrg_equipment_new:
 *
 * Creates a new equipment manager with all slots empty.
 *
 * Returns: (transfer full): a new #LrgEquipment
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEquipment *
lrg_equipment_new (void);

/* Slot Access */

/**
 * lrg_equipment_get_slot:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Gets the item equipped in a slot.
 *
 * Returns: (transfer none) (nullable): the equipped item, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_equipment_get_slot (LrgEquipment    *self,
                        LrgEquipmentSlot slot);

/**
 * lrg_equipment_is_slot_empty:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Checks if a slot is empty.
 *
 * Returns: %TRUE if the slot is empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_equipment_is_slot_empty (LrgEquipment    *self,
                             LrgEquipmentSlot slot);

/* Equip/Unequip */

/**
 * lrg_equipment_equip:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 * @stack: the item stack to equip
 *
 * Equips an item in a slot. If the slot already has an item,
 * it is returned (swapped out). Emits "item-equipped" signal.
 *
 * Returns: (transfer full) (nullable): the previously equipped item, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_equipment_equip (LrgEquipment    *self,
                     LrgEquipmentSlot slot,
                     LrgItemStack    *stack);

/**
 * lrg_equipment_unequip:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Removes and returns the item from a slot.
 * Emits "item-unequipped" signal if an item was present.
 *
 * Returns: (transfer full) (nullable): the unequipped item, or %NULL if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemStack *
lrg_equipment_unequip (LrgEquipment    *self,
                       LrgEquipmentSlot slot);

/**
 * lrg_equipment_clear:
 * @self: an #LrgEquipment
 *
 * Unequips all items from all slots.
 * Emits "item-unequipped" for each removed item.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_equipment_clear (LrgEquipment *self);

/* Query */

/**
 * lrg_equipment_get_equipped_slots:
 * @self: an #LrgEquipment
 *
 * Gets a list of slots that have items equipped.
 *
 * Returns: (transfer container) (element-type LrgEquipmentSlot): list of slots
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_equipment_get_equipped_slots (LrgEquipment *self);

/**
 * lrg_equipment_can_equip:
 * @self: an #LrgEquipment
 * @slot: the target slot
 * @def: the item definition to check
 *
 * Checks if an item can be equipped in a slot based on its type.
 * - WEAPON slot accepts LRG_ITEM_TYPE_WEAPON
 * - HEAD, CHEST, LEGS, FEET, HANDS accept LRG_ITEM_TYPE_ARMOR
 * - OFFHAND accepts WEAPON or ARMOR
 * - ACCESSORY accepts LRG_ITEM_TYPE_GENERIC (accessory items)
 *
 * Returns: %TRUE if the item can be equipped in the slot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_equipment_can_equip (LrgEquipment    *self,
                         LrgEquipmentSlot slot,
                         LrgItemDef      *def);

/* Stats */

/**
 * lrg_equipment_get_stat_bonus:
 * @self: an #LrgEquipment
 * @stat_name: the stat property name to sum
 *
 * Gets the total stat bonus from all equipped items.
 * Sums up the custom property value from each item's definition.
 *
 * Returns: total stat bonus value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_equipment_get_stat_bonus (LrgEquipment *self,
                              const gchar  *stat_name);

/**
 * lrg_equipment_get_stat_bonus_float:
 * @self: an #LrgEquipment
 * @stat_name: the stat property name to sum
 *
 * Gets the total float stat bonus from all equipped items.
 *
 * Returns: total stat bonus value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_equipment_get_stat_bonus_float (LrgEquipment *self,
                                    const gchar  *stat_name);

G_END_DECLS
