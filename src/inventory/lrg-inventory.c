/* lrg-inventory.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INVENTORY

#include "config.h"
#include "lrg-inventory.h"
#include "../lrg-log.h"

/* Private data structure */
typedef struct
{
    GPtrArray *slots;       /* LrgItemStack* or NULL */
    guint      capacity;
} LrgInventoryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgInventory, lrg_inventory, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CAPACITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_ITEM_ADDED,
    SIGNAL_ITEM_REMOVED,
    SIGNAL_ITEM_USED,
    SIGNAL_SLOT_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Default Virtual Function Implementations
 * ========================================================================== */

static gboolean
lrg_inventory_real_can_accept (LrgInventory *self,
                               LrgItemDef   *def G_GNUC_UNUSED,
                               gint          slot)
{
    LrgInventoryPrivate *priv = lrg_inventory_get_instance_private (self);

    if (slot < 0)
    {
        /* Check if there's room anywhere */
        return lrg_inventory_get_free_slots (self) > 0;
    }

    if ((guint)slot >= priv->capacity)
        return FALSE;

    return TRUE;
}

static void
lrg_inventory_real_on_item_added (LrgInventory *self G_GNUC_UNUSED,
                                  guint         slot G_GNUC_UNUSED,
                                  LrgItemStack *stack G_GNUC_UNUSED)
{
    /* Default implementation does nothing */
}

static void
lrg_inventory_real_on_item_removed (LrgInventory *self G_GNUC_UNUSED,
                                    guint         slot G_GNUC_UNUSED,
                                    LrgItemStack *stack G_GNUC_UNUSED)
{
    /* Default implementation does nothing */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_inventory_finalize (GObject *object)
{
    LrgInventory *self = LRG_INVENTORY (object);
    LrgInventoryPrivate *priv = lrg_inventory_get_instance_private (self);

    g_clear_pointer (&priv->slots, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_inventory_parent_class)->finalize (object);
}

static void
lrg_inventory_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgInventory *self = LRG_INVENTORY (object);
    LrgInventoryPrivate *priv = lrg_inventory_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CAPACITY:
        g_value_set_uint (value, priv->capacity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_inventory_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgInventory *self = LRG_INVENTORY (object);

    switch (prop_id)
    {
    case PROP_CAPACITY:
        lrg_inventory_set_capacity (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_inventory_class_init (LrgInventoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_inventory_finalize;
    object_class->get_property = lrg_inventory_get_property;
    object_class->set_property = lrg_inventory_set_property;

    /* Virtual functions */
    klass->can_accept = lrg_inventory_real_can_accept;
    klass->on_item_added = lrg_inventory_real_on_item_added;
    klass->on_item_removed = lrg_inventory_real_on_item_removed;

    /**
     * LrgInventory:capacity:
     *
     * The total number of slots in the inventory.
     */
    properties[PROP_CAPACITY] =
        g_param_spec_uint ("capacity",
                           "Capacity",
                           "Number of slots",
                           1, G_MAXUINT, 20,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgInventory::item-added:
     * @self: the inventory
     * @slot: the slot index
     * @stack: the added item stack
     *
     * Emitted when an item is added to the inventory.
     */
    signals[SIGNAL_ITEM_ADDED] =
        g_signal_new ("item-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT,
                      LRG_TYPE_ITEM_STACK);

    /**
     * LrgInventory::item-removed:
     * @self: the inventory
     * @slot: the slot index
     * @stack: the removed item stack
     *
     * Emitted when an item is removed from the inventory.
     */
    signals[SIGNAL_ITEM_REMOVED] =
        g_signal_new ("item-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT,
                      LRG_TYPE_ITEM_STACK);

    /**
     * LrgInventory::item-used:
     * @self: the inventory
     * @slot: the slot index
     * @stack: the item stack
     * @quantity: amount used
     *
     * Emitted when an item is used from the inventory.
     */
    signals[SIGNAL_ITEM_USED] =
        g_signal_new ("item-used",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 3,
                      G_TYPE_UINT,
                      LRG_TYPE_ITEM_STACK,
                      G_TYPE_UINT);

    /**
     * LrgInventory::slot-changed:
     * @self: the inventory
     * @slot: the slot index
     *
     * Emitted when a slot's contents change.
     */
    signals[SIGNAL_SLOT_CHANGED] =
        g_signal_new ("slot-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT);
}

/* Helper to safely unref item stacks (handles NULL) */
static void
item_stack_free_func (gpointer data)
{
    if (data != NULL)
        lrg_item_stack_unref ((LrgItemStack *)data);
}

static void
lrg_inventory_init (LrgInventory *self)
{
    LrgInventoryPrivate *priv = lrg_inventory_get_instance_private (self);

    priv->capacity = 20;
    priv->slots = g_ptr_array_new_with_free_func (item_stack_free_func);
    g_ptr_array_set_size (priv->slots, priv->capacity);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgInventory *
lrg_inventory_new (guint capacity)
{
    return g_object_new (LRG_TYPE_INVENTORY,
                         "capacity", capacity,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

guint
lrg_inventory_get_capacity (LrgInventory *self)
{
    LrgInventoryPrivate *priv;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);

    priv = lrg_inventory_get_instance_private (self);
    return priv->capacity;
}

void
lrg_inventory_set_capacity (LrgInventory *self,
                            guint         capacity)
{
    LrgInventoryPrivate *priv;

    g_return_if_fail (LRG_IS_INVENTORY (self));
    g_return_if_fail (capacity > 0);

    priv = lrg_inventory_get_instance_private (self);

    if (priv->capacity != capacity)
    {
        priv->capacity = capacity;
        g_ptr_array_set_size (priv->slots, capacity);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPACITY]);
    }
}

guint
lrg_inventory_get_used_slots (LrgInventory *self)
{
    LrgInventoryPrivate *priv;
    guint count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);

    priv = lrg_inventory_get_instance_private (self);

    for (i = 0; i < priv->capacity; i++)
    {
        if (g_ptr_array_index (priv->slots, i) != NULL)
            count++;
    }

    return count;
}

guint
lrg_inventory_get_free_slots (LrgInventory *self)
{
    LrgInventoryPrivate *priv;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);

    priv = lrg_inventory_get_instance_private (self);
    return priv->capacity - lrg_inventory_get_used_slots (self);
}

gboolean
lrg_inventory_is_full (LrgInventory *self)
{
    g_return_val_if_fail (LRG_IS_INVENTORY (self), TRUE);

    return lrg_inventory_get_free_slots (self) == 0;
}

gboolean
lrg_inventory_is_empty (LrgInventory *self)
{
    g_return_val_if_fail (LRG_IS_INVENTORY (self), TRUE);

    return lrg_inventory_get_used_slots (self) == 0;
}

/* ==========================================================================
 * Slot Access
 * ========================================================================== */

LrgItemStack *
lrg_inventory_get_slot (LrgInventory *self,
                        guint         slot)
{
    LrgInventoryPrivate *priv;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), NULL);

    priv = lrg_inventory_get_instance_private (self);

    if (slot >= priv->capacity)
        return NULL;

    return g_ptr_array_index (priv->slots, slot);
}

gboolean
lrg_inventory_set_slot (LrgInventory *self,
                        guint         slot,
                        LrgItemStack *stack)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    LrgItemStack *old_stack;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), FALSE);

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    if (slot >= priv->capacity)
        return FALSE;

    old_stack = g_ptr_array_index (priv->slots, slot);

    /* Emit removal signal if replacing */
    if (old_stack != NULL)
    {
        if (klass->on_item_removed != NULL)
            klass->on_item_removed (self, slot, old_stack);
        g_signal_emit (self, signals[SIGNAL_ITEM_REMOVED], 0, slot, old_stack);
    }

    /* Set new stack */
    if (stack != NULL)
        g_ptr_array_index (priv->slots, slot) = lrg_item_stack_ref (stack);
    else
        g_ptr_array_index (priv->slots, slot) = NULL;

    /* Unref old after setting new */
    if (old_stack != NULL)
        lrg_item_stack_unref (old_stack);

    /* Emit addition signal */
    if (stack != NULL)
    {
        if (klass->on_item_added != NULL)
            klass->on_item_added (self, slot, stack);
        g_signal_emit (self, signals[SIGNAL_ITEM_ADDED], 0, slot, stack);
    }

    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot);

    return TRUE;
}

LrgItemStack *
lrg_inventory_clear_slot (LrgInventory *self,
                          guint         slot)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    LrgItemStack *old_stack;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), NULL);

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    if (slot >= priv->capacity)
        return NULL;

    old_stack = g_ptr_array_index (priv->slots, slot);
    if (old_stack == NULL)
        return NULL;

    g_ptr_array_index (priv->slots, slot) = NULL;

    if (klass->on_item_removed != NULL)
        klass->on_item_removed (self, slot, old_stack);
    g_signal_emit (self, signals[SIGNAL_ITEM_REMOVED], 0, slot, old_stack);
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot);

    return old_stack;  /* Transfer ownership to caller */
}

gboolean
lrg_inventory_is_slot_empty (LrgInventory *self,
                             guint         slot)
{
    LrgInventoryPrivate *priv;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), TRUE);

    priv = lrg_inventory_get_instance_private (self);

    if (slot >= priv->capacity)
        return TRUE;

    return g_ptr_array_index (priv->slots, slot) == NULL;
}

gint
lrg_inventory_find_empty_slot (LrgInventory *self)
{
    LrgInventoryPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), -1);

    priv = lrg_inventory_get_instance_private (self);

    for (i = 0; i < priv->capacity; i++)
    {
        if (g_ptr_array_index (priv->slots, i) == NULL)
            return (gint)i;
    }

    return -1;
}

/* ==========================================================================
 * Adding Items
 * ========================================================================== */

guint
lrg_inventory_add_item (LrgInventory *self,
                        LrgItemDef   *def,
                        guint         quantity)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    guint remaining;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);
    g_return_val_if_fail (LRG_IS_ITEM_DEF (def), 0);

    if (quantity == 0)
        return 0;

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    remaining = quantity;

    /* First try to add to existing stacks (even if inventory is "full") */
    if (lrg_item_def_get_stackable (def))
    {
        for (i = 0; i < priv->capacity && remaining > 0; i++)
        {
            LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
            if (stack != NULL)
            {
                LrgItemDef *stack_def = lrg_item_stack_get_def (stack);
                if (lrg_item_def_can_stack_with (def, stack_def))
                {
                    guint added = lrg_item_stack_add (stack, remaining);
                    remaining -= added;
                    if (added > 0)
                        g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, i);
                }
            }
        }
    }

    /* If everything fit in existing stacks, we're done */
    if (remaining == 0)
        return quantity;

    /* Check if we can create new stacks */
    if (!klass->can_accept (self, def, -1))
        return quantity - remaining;

    /* Create new stacks in empty slots */
    while (remaining > 0)
    {
        LrgItemStack *new_stack;
        guint added;
        gint slot;

        slot = lrg_inventory_find_empty_slot (self);
        if (slot < 0)
            break;

        new_stack = lrg_item_stack_new (def, remaining);
        added = lrg_item_stack_get_quantity (new_stack);
        remaining -= added;

        g_ptr_array_index (priv->slots, slot) = new_stack;

        if (klass->on_item_added != NULL)
            klass->on_item_added (self, slot, new_stack);
        g_signal_emit (self, signals[SIGNAL_ITEM_ADDED], 0, slot, new_stack);
        g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot);
    }

    return quantity - remaining;
}

guint
lrg_inventory_add_stack (LrgInventory *self,
                         LrgItemStack *stack)
{
    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);
    g_return_val_if_fail (stack != NULL, 0);

    return lrg_inventory_add_item (self,
                                   lrg_item_stack_get_def (stack),
                                   lrg_item_stack_get_quantity (stack));
}

guint
lrg_inventory_add_to_slot (LrgInventory *self,
                           guint         slot,
                           LrgItemDef   *def,
                           guint         quantity)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    LrgItemStack *existing;
    guint added;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);
    g_return_val_if_fail (LRG_IS_ITEM_DEF (def), 0);

    if (quantity == 0)
        return 0;

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    if (slot >= priv->capacity)
        return 0;

    if (!klass->can_accept (self, def, slot))
        return 0;

    existing = g_ptr_array_index (priv->slots, slot);

    if (existing != NULL)
    {
        /* Add to existing stack if compatible */
        LrgItemDef *existing_def = lrg_item_stack_get_def (existing);
        if (!lrg_item_def_can_stack_with (def, existing_def))
            return 0;

        added = lrg_item_stack_add (existing, quantity);
    }
    else
    {
        /* Create new stack */
        LrgItemStack *new_stack = lrg_item_stack_new (def, quantity);
        added = lrg_item_stack_get_quantity (new_stack);
        g_ptr_array_index (priv->slots, slot) = new_stack;

        if (klass->on_item_added != NULL)
            klass->on_item_added (self, slot, new_stack);
        g_signal_emit (self, signals[SIGNAL_ITEM_ADDED], 0, slot, new_stack);
    }

    if (added > 0)
        g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot);

    return added;
}

/* ==========================================================================
 * Removing Items
 * ========================================================================== */

guint
lrg_inventory_remove_item (LrgInventory *self,
                           const gchar  *item_id,
                           guint         quantity)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    guint remaining;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);
    g_return_val_if_fail (item_id != NULL, 0);

    if (quantity == 0)
        return 0;

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);
    remaining = quantity;

    for (i = 0; i < priv->capacity && remaining > 0; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL)
        {
            LrgItemDef *def = lrg_item_stack_get_def (stack);
            if (g_strcmp0 (lrg_item_def_get_id (def), item_id) == 0)
            {
                guint removed = lrg_item_stack_remove (stack, remaining);
                remaining -= removed;

                if (lrg_item_stack_is_empty (stack))
                {
                    g_ptr_array_index (priv->slots, i) = NULL;
                    if (klass->on_item_removed != NULL)
                        klass->on_item_removed (self, i, stack);
                    g_signal_emit (self, signals[SIGNAL_ITEM_REMOVED], 0, i, stack);
                    lrg_item_stack_unref (stack);
                }

                g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, i);
            }
        }
    }

    return quantity - remaining;
}

guint
lrg_inventory_remove_from_slot (LrgInventory *self,
                                guint         slot,
                                guint         quantity)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    LrgItemStack *stack;
    guint removed;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    if (slot >= priv->capacity)
        return 0;

    stack = g_ptr_array_index (priv->slots, slot);
    if (stack == NULL)
        return 0;

    removed = lrg_item_stack_remove (stack, quantity);

    if (lrg_item_stack_is_empty (stack))
    {
        g_ptr_array_index (priv->slots, slot) = NULL;
        if (klass->on_item_removed != NULL)
            klass->on_item_removed (self, slot, stack);
        g_signal_emit (self, signals[SIGNAL_ITEM_REMOVED], 0, slot, stack);
        lrg_item_stack_unref (stack);
    }

    if (removed > 0)
        g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot);

    return removed;
}

/* ==========================================================================
 * Finding Items
 * ========================================================================== */

LrgItemStack *
lrg_inventory_find_item (LrgInventory *self,
                         const gchar  *item_id)
{
    LrgInventoryPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), NULL);
    g_return_val_if_fail (item_id != NULL, NULL);

    priv = lrg_inventory_get_instance_private (self);

    for (i = 0; i < priv->capacity; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL)
        {
            LrgItemDef *def = lrg_item_stack_get_def (stack);
            if (g_strcmp0 (lrg_item_def_get_id (def), item_id) == 0)
                return stack;
        }
    }

    return NULL;
}

gint
lrg_inventory_find_item_slot (LrgInventory *self,
                              const gchar  *item_id)
{
    LrgInventoryPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), -1);
    g_return_val_if_fail (item_id != NULL, -1);

    priv = lrg_inventory_get_instance_private (self);

    for (i = 0; i < priv->capacity; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL)
        {
            LrgItemDef *def = lrg_item_stack_get_def (stack);
            if (g_strcmp0 (lrg_item_def_get_id (def), item_id) == 0)
                return (gint)i;
        }
    }

    return -1;
}

guint
lrg_inventory_count_item (LrgInventory *self,
                          const gchar  *item_id)
{
    LrgInventoryPrivate *priv;
    guint count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);
    g_return_val_if_fail (item_id != NULL, 0);

    priv = lrg_inventory_get_instance_private (self);

    for (i = 0; i < priv->capacity; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL)
        {
            LrgItemDef *def = lrg_item_stack_get_def (stack);
            if (g_strcmp0 (lrg_item_def_get_id (def), item_id) == 0)
                count += lrg_item_stack_get_quantity (stack);
        }
    }

    return count;
}

gboolean
lrg_inventory_has_item (LrgInventory *self,
                        const gchar  *item_id,
                        guint         quantity)
{
    g_return_val_if_fail (LRG_IS_INVENTORY (self), FALSE);
    g_return_val_if_fail (item_id != NULL, FALSE);

    return lrg_inventory_count_item (self, item_id) >= quantity;
}

/* ==========================================================================
 * Slot Operations
 * ========================================================================== */

gboolean
lrg_inventory_swap_slots (LrgInventory *self,
                          guint         slot_a,
                          guint         slot_b)
{
    LrgInventoryPrivate *priv;
    LrgItemStack *temp;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), FALSE);

    priv = lrg_inventory_get_instance_private (self);

    if (slot_a >= priv->capacity || slot_b >= priv->capacity)
        return FALSE;

    if (slot_a == slot_b)
        return TRUE;

    temp = g_ptr_array_index (priv->slots, slot_a);
    g_ptr_array_index (priv->slots, slot_a) = g_ptr_array_index (priv->slots, slot_b);
    g_ptr_array_index (priv->slots, slot_b) = temp;

    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot_a);
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, slot_b);

    return TRUE;
}

guint
lrg_inventory_move_to_slot (LrgInventory *self,
                            guint         from_slot,
                            guint         to_slot,
                            gint          quantity)
{
    LrgInventoryPrivate *priv;
    LrgItemStack *from_stack;
    LrgItemStack *to_stack;
    guint moved;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), 0);

    priv = lrg_inventory_get_instance_private (self);

    if (from_slot >= priv->capacity || to_slot >= priv->capacity)
        return 0;

    if (from_slot == to_slot)
        return 0;

    from_stack = g_ptr_array_index (priv->slots, from_slot);
    if (from_stack == NULL)
        return 0;

    if (quantity < 0)
        quantity = lrg_item_stack_get_quantity (from_stack);

    to_stack = g_ptr_array_index (priv->slots, to_slot);

    if (to_stack != NULL)
    {
        /* Try to merge */
        if (lrg_item_stack_can_merge (to_stack, from_stack))
        {
            guint available = MIN ((guint)quantity, lrg_item_stack_get_quantity (from_stack));
            guint space = lrg_item_stack_get_space_remaining (to_stack);
            moved = MIN (available, space);

            lrg_item_stack_remove (from_stack, moved);
            lrg_item_stack_add (to_stack, moved);
        }
        else
        {
            /* Can't merge, swap instead if moving all */
            if ((guint)quantity >= lrg_item_stack_get_quantity (from_stack))
            {
                lrg_inventory_swap_slots (self, from_slot, to_slot);
                moved = lrg_item_stack_get_quantity (from_stack);
            }
            else
            {
                return 0;
            }
        }
    }
    else
    {
        /* Empty destination */
        if ((guint)quantity >= lrg_item_stack_get_quantity (from_stack))
        {
            /* Move entire stack */
            g_ptr_array_index (priv->slots, to_slot) = from_stack;
            g_ptr_array_index (priv->slots, from_slot) = NULL;
            moved = lrg_item_stack_get_quantity (from_stack);
        }
        else
        {
            /* Split stack */
            LrgItemStack *split = lrg_item_stack_split (from_stack, quantity);
            if (split == NULL)
                return 0;
            g_ptr_array_index (priv->slots, to_slot) = split;
            moved = lrg_item_stack_get_quantity (split);
        }
    }

    /* Check if source is now empty */
    from_stack = g_ptr_array_index (priv->slots, from_slot);
    if (from_stack != NULL && lrg_item_stack_is_empty (from_stack))
    {
        g_ptr_array_index (priv->slots, from_slot) = NULL;
        lrg_item_stack_unref (from_stack);
    }

    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, from_slot);
    g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, to_slot);

    return moved;
}

static gint
compare_stacks (gconstpointer a,
                gconstpointer b)
{
    LrgItemStack *stack_a = *(LrgItemStack **)a;
    LrgItemStack *stack_b = *(LrgItemStack **)b;
    LrgItemDef *def_a;
    LrgItemDef *def_b;
    gint type_cmp;

    /* NULL stacks go to the end */
    if (stack_a == NULL && stack_b == NULL)
        return 0;
    if (stack_a == NULL)
        return 1;
    if (stack_b == NULL)
        return -1;

    def_a = lrg_item_stack_get_def (stack_a);
    def_b = lrg_item_stack_get_def (stack_b);

    /* Sort by item type first */
    type_cmp = (gint)lrg_item_def_get_item_type (def_a) -
               (gint)lrg_item_def_get_item_type (def_b);
    if (type_cmp != 0)
        return type_cmp;

    /* Then by ID */
    return g_strcmp0 (lrg_item_def_get_id (def_a), lrg_item_def_get_id (def_b));
}

void
lrg_inventory_sort (LrgInventory *self)
{
    LrgInventoryPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_INVENTORY (self));

    priv = lrg_inventory_get_instance_private (self);

    /* Combine partial stacks first */
    for (i = 0; i < priv->capacity; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL && !lrg_item_stack_is_full (stack))
        {
            guint j;
            for (j = i + 1; j < priv->capacity; j++)
            {
                LrgItemStack *other = g_ptr_array_index (priv->slots, j);
                if (other != NULL && lrg_item_stack_can_merge (stack, other))
                {
                    lrg_item_stack_merge (stack, other);
                    if (lrg_item_stack_is_empty (other))
                    {
                        g_ptr_array_index (priv->slots, j) = NULL;
                        lrg_item_stack_unref (other);
                    }
                    if (lrg_item_stack_is_full (stack))
                        break;
                }
            }
        }
    }

    /* Sort the array */
    g_ptr_array_sort (priv->slots, compare_stacks);

    /* Emit slot changed for all slots */
    for (i = 0; i < priv->capacity; i++)
    {
        g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, i);
    }
}

void
lrg_inventory_clear (LrgInventory *self)
{
    LrgInventoryPrivate *priv;
    LrgInventoryClass *klass;
    guint i;

    g_return_if_fail (LRG_IS_INVENTORY (self));

    priv = lrg_inventory_get_instance_private (self);
    klass = LRG_INVENTORY_GET_CLASS (self);

    for (i = 0; i < priv->capacity; i++)
    {
        LrgItemStack *stack = g_ptr_array_index (priv->slots, i);
        if (stack != NULL)
        {
            g_ptr_array_index (priv->slots, i) = NULL;
            if (klass->on_item_removed != NULL)
                klass->on_item_removed (self, i, stack);
            g_signal_emit (self, signals[SIGNAL_ITEM_REMOVED], 0, i, stack);
            g_signal_emit (self, signals[SIGNAL_SLOT_CHANGED], 0, i);
            lrg_item_stack_unref (stack);
        }
    }
}

/* ==========================================================================
 * Virtual Function Wrappers
 * ========================================================================== */

gboolean
lrg_inventory_can_accept (LrgInventory *self,
                          LrgItemDef   *def,
                          gint          slot)
{
    LrgInventoryClass *klass;

    g_return_val_if_fail (LRG_IS_INVENTORY (self), FALSE);
    g_return_val_if_fail (LRG_IS_ITEM_DEF (def), FALSE);

    klass = LRG_INVENTORY_GET_CLASS (self);
    if (klass->can_accept != NULL)
        return klass->can_accept (self, def, slot);

    return FALSE;
}
