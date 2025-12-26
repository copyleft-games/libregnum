/* lrg-equipment.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Equipment slot management implementation.
 */

#include "config.h"

#include "lrg-equipment.h"
#include "../lrg-log.h"

/**
 * LrgEquipment:
 *
 * Equipment slot manager.
 */
struct _LrgEquipment
{
    GObject parent_instance;

    GHashTable *slots;  /* LrgEquipmentSlot (as GINT_TO_POINTER) -> LrgItemStack */
};

G_DEFINE_TYPE (LrgEquipment, lrg_equipment, G_TYPE_OBJECT)

/* Signals */
enum
{
    SIGNAL_ITEM_EQUIPPED,
    SIGNAL_ITEM_UNEQUIPPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Private Implementation
 * ========================================================================== */

static void
lrg_equipment_dispose (GObject *object)
{
    LrgEquipment *self = LRG_EQUIPMENT (object);

    g_clear_pointer (&self->slots, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_equipment_parent_class)->dispose (object);
}

static void
lrg_equipment_class_init (LrgEquipmentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_equipment_dispose;

    /**
     * LrgEquipment::item-equipped:
     * @equipment: the equipment manager
     * @slot: the equipment slot
     * @stack: the equipped item stack
     *
     * Emitted when an item is equipped.
     */
    signals[SIGNAL_ITEM_EQUIPPED] =
        g_signal_new ("item-equipped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      2,
                      LRG_TYPE_EQUIPMENT_SLOT,
                      LRG_TYPE_ITEM_STACK);

    /**
     * LrgEquipment::item-unequipped:
     * @equipment: the equipment manager
     * @slot: the equipment slot
     * @stack: the unequipped item stack
     *
     * Emitted when an item is unequipped.
     */
    signals[SIGNAL_ITEM_UNEQUIPPED] =
        g_signal_new ("item-unequipped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE,
                      2,
                      LRG_TYPE_EQUIPMENT_SLOT,
                      LRG_TYPE_ITEM_STACK);
}

static void
lrg_equipment_init (LrgEquipment *self)
{
    self->slots = g_hash_table_new_full (g_direct_hash,
                                         g_direct_equal,
                                         NULL,
                                         (GDestroyNotify)lrg_item_stack_unref);

    lrg_debug (LRG_LOG_DOMAIN_INVENTORY, "Created equipment manager");
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_equipment_new:
 *
 * Creates a new equipment manager with all slots empty.
 *
 * Returns: (transfer full): a new #LrgEquipment
 */
LrgEquipment *
lrg_equipment_new (void)
{
    return g_object_new (LRG_TYPE_EQUIPMENT, NULL);
}

/**
 * lrg_equipment_get_slot:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Gets the item equipped in a slot.
 *
 * Returns: (transfer none) (nullable): the equipped item, or %NULL if empty
 */
LrgItemStack *
lrg_equipment_get_slot (LrgEquipment    *self,
                        LrgEquipmentSlot slot)
{
    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), NULL);

    return g_hash_table_lookup (self->slots, GINT_TO_POINTER (slot));
}

/**
 * lrg_equipment_is_slot_empty:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Checks if a slot is empty.
 *
 * Returns: %TRUE if the slot is empty
 */
gboolean
lrg_equipment_is_slot_empty (LrgEquipment    *self,
                             LrgEquipmentSlot slot)
{
    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), TRUE);

    return !g_hash_table_contains (self->slots, GINT_TO_POINTER (slot));
}

/**
 * lrg_equipment_equip:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 * @stack: the item stack to equip
 *
 * Equips an item in a slot.
 *
 * Returns: (transfer full) (nullable): the previously equipped item, or %NULL
 */
LrgItemStack *
lrg_equipment_equip (LrgEquipment    *self,
                     LrgEquipmentSlot slot,
                     LrgItemStack    *stack)
{
    LrgItemStack *old_stack;
    LrgItemDef   *def;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), NULL);
    g_return_val_if_fail (stack != NULL, NULL);

    /* Get and ref the old item if present */
    old_stack = g_hash_table_lookup (self->slots, GINT_TO_POINTER (slot));
    if (old_stack != NULL)
    {
        lrg_item_stack_ref (old_stack);
    }

    /* Insert new item (takes ownership via ref) */
    g_hash_table_insert (self->slots,
                         GINT_TO_POINTER (slot),
                         lrg_item_stack_ref (stack));

    def = lrg_item_stack_get_def (stack);
    lrg_debug (LRG_LOG_DOMAIN_INVENTORY, "Equipped '%s' in slot %d",
               def != NULL ? lrg_item_def_get_id (def) : "(unknown)",
               slot);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_ITEM_EQUIPPED], 0, slot, stack);

    return old_stack;
}

/**
 * lrg_equipment_unequip:
 * @self: an #LrgEquipment
 * @slot: the equipment slot
 *
 * Removes and returns the item from a slot.
 *
 * Returns: (transfer full) (nullable): the unequipped item, or %NULL if empty
 */
LrgItemStack *
lrg_equipment_unequip (LrgEquipment    *self,
                       LrgEquipmentSlot slot)
{
    LrgItemStack *stack;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), NULL);

    stack = g_hash_table_lookup (self->slots, GINT_TO_POINTER (slot));
    if (stack == NULL)
    {
        return NULL;
    }

    /* Ref before removing so we can return it */
    lrg_item_stack_ref (stack);

    /* Remove from table (will unref the table's copy) */
    g_hash_table_remove (self->slots, GINT_TO_POINTER (slot));

    lrg_debug (LRG_LOG_DOMAIN_INVENTORY, "Unequipped item from slot %d", slot);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_ITEM_UNEQUIPPED], 0, slot, stack);

    return stack;
}

/**
 * lrg_equipment_clear:
 * @self: an #LrgEquipment
 *
 * Unequips all items from all slots.
 */
void
lrg_equipment_clear (LrgEquipment *self)
{
    GHashTableIter iter;
    gpointer       key, value;
    GList         *slots_to_clear = NULL;
    GList         *l;

    g_return_if_fail (LRG_IS_EQUIPMENT (self));

    /* Collect slots first to avoid modifying while iterating */
    g_hash_table_iter_init (&iter, self->slots);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        slots_to_clear = g_list_prepend (slots_to_clear, key);
    }

    /* Now unequip each slot */
    for (l = slots_to_clear; l != NULL; l = l->next)
    {
        g_autoptr(LrgItemStack) stack = NULL;
        LrgEquipmentSlot        slot;

        slot = GPOINTER_TO_INT (l->data);
        stack = lrg_equipment_unequip (self, slot);
    }

    g_list_free (slots_to_clear);

    lrg_debug (LRG_LOG_DOMAIN_INVENTORY, "Cleared all equipment");
}

/**
 * lrg_equipment_get_equipped_slots:
 * @self: an #LrgEquipment
 *
 * Gets a list of slots that have items equipped.
 *
 * Returns: (transfer container) (element-type LrgEquipmentSlot): list of slots
 */
GList *
lrg_equipment_get_equipped_slots (LrgEquipment *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer       key;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), NULL);

    g_hash_table_iter_init (&iter, self->slots);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        result = g_list_prepend (result, key);
    }

    return result;
}

/**
 * lrg_equipment_can_equip:
 * @self: an #LrgEquipment
 * @slot: the target slot
 * @def: the item definition to check
 *
 * Checks if an item can be equipped in a slot based on its type.
 *
 * Returns: %TRUE if the item can be equipped in the slot
 */
gboolean
lrg_equipment_can_equip (LrgEquipment    *self,
                         LrgEquipmentSlot slot,
                         LrgItemDef      *def)
{
    LrgItemType item_type;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), FALSE);
    g_return_val_if_fail (LRG_IS_ITEM_DEF (def), FALSE);

    item_type = lrg_item_def_get_item_type (def);

    switch (slot)
    {
        case LRG_EQUIPMENT_SLOT_WEAPON:
            return (item_type == LRG_ITEM_TYPE_WEAPON);

        case LRG_EQUIPMENT_SLOT_HEAD:
        case LRG_EQUIPMENT_SLOT_CHEST:
        case LRG_EQUIPMENT_SLOT_LEGS:
        case LRG_EQUIPMENT_SLOT_FEET:
        case LRG_EQUIPMENT_SLOT_HANDS:
            return (item_type == LRG_ITEM_TYPE_ARMOR);

        case LRG_EQUIPMENT_SLOT_OFFHAND:
            /* Offhand can hold shield (armor) or secondary weapon */
            return (item_type == LRG_ITEM_TYPE_WEAPON ||
                    item_type == LRG_ITEM_TYPE_ARMOR);

        case LRG_EQUIPMENT_SLOT_ACCESSORY:
            /* Accessories are generic type items with special use */
            return (item_type == LRG_ITEM_TYPE_GENERIC);

        default:
            return FALSE;
    }
}

/**
 * lrg_equipment_get_stat_bonus:
 * @self: an #LrgEquipment
 * @stat_name: the stat property name to sum
 *
 * Gets the total stat bonus from all equipped items.
 *
 * Returns: total stat bonus value
 */
gint
lrg_equipment_get_stat_bonus (LrgEquipment *self,
                              const gchar  *stat_name)
{
    GHashTableIter  iter;
    gpointer        value;
    gint            total = 0;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), 0);
    g_return_val_if_fail (stat_name != NULL, 0);

    g_hash_table_iter_init (&iter, self->slots);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgItemStack *stack = (LrgItemStack *)value;
        LrgItemDef   *def;

        def = lrg_item_stack_get_def (stack);
        if (def != NULL)
        {
            total += lrg_item_def_get_property_int (def, stat_name, 0);
        }
    }

    return total;
}

/**
 * lrg_equipment_get_stat_bonus_float:
 * @self: an #LrgEquipment
 * @stat_name: the stat property name to sum
 *
 * Gets the total float stat bonus from all equipped items.
 *
 * Returns: total stat bonus value
 */
gfloat
lrg_equipment_get_stat_bonus_float (LrgEquipment *self,
                                    const gchar  *stat_name)
{
    GHashTableIter  iter;
    gpointer        value;
    gfloat          total = 0.0f;

    g_return_val_if_fail (LRG_IS_EQUIPMENT (self), 0.0f);
    g_return_val_if_fail (stat_name != NULL, 0.0f);

    g_hash_table_iter_init (&iter, self->slots);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgItemStack *stack = (LrgItemStack *)value;
        LrgItemDef   *def;

        def = lrg_item_stack_get_def (stack);
        if (def != NULL)
        {
            total += lrg_item_def_get_property_float (def, stat_name, 0.0f);
        }
    }

    return total;
}
