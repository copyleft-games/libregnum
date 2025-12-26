# LrgInventory - Item Container

`LrgInventory` is a derivable GObject class that manages a collection of item stacks organized in fixed slots. It handles the complexity of adding, removing, organizing, and finding items with automatic stacking logic.

## Type Information

- **Type Name**: `LrgInventory`
- **Base Class**: `GObject`
- **Type Class**: Derivable (can be subclassed for custom containers)
- **Transfer**: Full ownership when returned

## Overview

Inventories provide a slot-based container system:
- Fixed number of slots (capacity)
- Each slot holds zero or one item stack
- Items are automatically stacked if compatible
- Support for item searching, counting, and organization
- Signal support for monitoring changes

Perfect for character inventories, chests, shop stock, or any container.

## Construction

### Creating Inventories

```c
/* Create inventory with 20 slots */
g_autoptr(LrgInventory) inventory = lrg_inventory_new(20);

g_assert_cmpuint(lrg_inventory_get_capacity(inventory), ==, 20);
g_assert_true(lrg_inventory_is_empty(inventory));
```

### Changing Capacity

```c
/* Increase capacity */
lrg_inventory_set_capacity(inventory, 30);

/* Decrease capacity - items in removed slots are lost */
lrg_inventory_set_capacity(inventory, 10);
```

## Properties

### Capacity and Usage

```c
guint capacity = lrg_inventory_get_capacity(inventory);
guint used = lrg_inventory_get_used_slots(inventory);
guint free = lrg_inventory_get_free_slots(inventory);

if (lrg_inventory_is_full(inventory)) {
    /* No more free slots */
}

if (lrg_inventory_is_empty(inventory)) {
    /* No items */
}
```

### Slot Access

#### Get Slot Contents

```c
/* Get item stack in slot (transfer none) */
LrgItemStack *stack = lrg_inventory_get_slot(inventory, 3);
if (stack != NULL) {
    guint qty = lrg_item_stack_get_quantity(stack);
}
```

#### Check Slot Status

```c
if (lrg_inventory_is_slot_empty(inventory, 5)) {
    /* Slot is empty */
}
```

#### Set Slot Contents

```c
/* Replace slot contents entirely */
g_autoptr(LrgItemStack) new_stack = lrg_item_stack_new(item_def, 5);
gboolean success = lrg_inventory_set_slot(inventory, 5, new_stack);
```

#### Clear Slot

```c
/* Remove and return items (transfer full) */
g_autoptr(LrgItemStack) removed = lrg_inventory_clear_slot(inventory, 5);
if (removed != NULL) {
    /* Handle removed items */
}
```

#### Find Empty Slot

```c
gint empty_slot = lrg_inventory_find_empty_slot(inventory);
if (empty_slot >= 0) {
    /* Found empty slot at index */
} else {
    /* Inventory full */
}
```

## Adding Items

### Simple Item Addition

```c
/* Add items to inventory - automatically stacks */
guint added = lrg_inventory_add_item(inventory, potion_def, 10);
```

This function:
1. Tries to add to existing partial stacks
2. Creates new stacks in empty slots as needed
3. Returns the actual amount added (may be less if full)

### Adding Item Stacks

```c
/* Add a pre-constructed stack */
g_autoptr(LrgItemStack) stack = lrg_item_stack_new(sword_def, 1);
guint added = lrg_inventory_add_stack(inventory, stack);
```

The stack is merged with existing compatible stacks, or placed in a new slot.

### Adding to Specific Slot

```c
/* Force add to specific slot */
guint added = lrg_inventory_add_to_slot(inventory, 5, potion_def, 3);
```

Adds to the specified slot. If the slot is full or incompatible, operation may fail.

## Removing Items

### Remove by Item ID

```c
guint removed = lrg_inventory_remove_item(inventory, "potion_health", 5);
```

Removes items from any slot containing the specified ID. Returns actual amount removed.

### Remove from Specific Slot

```c
guint removed = lrg_inventory_remove_from_slot(inventory, 3, 5);
```

Removes items from a specific slot.

## Finding Items

### Find Item Stack

```c
/* Find first stack with this item ID (transfer none) */
LrgItemStack *stack = lrg_inventory_find_item(inventory, "potion_health");
if (stack != NULL) {
    /* Process stack */
}
```

### Find Item Slot

```c
/* Find slot index of item */
gint slot = lrg_inventory_find_item_slot(inventory, "sword_iron");
if (slot >= 0) {
    /* Item is in slot */
} else {
    /* Item not found */
}
```

### Count Items

```c
/* Count total quantity of item across all slots */
guint total = lrg_inventory_count_item(inventory, "gold_coin");

/* Check if inventory has minimum quantity */
if (lrg_inventory_has_item(inventory, "potion_health", 5)) {
    /* Inventory has at least 5 potions */
}
```

## Slot Operations

### Swap Slots

```c
/* Exchange contents of two slots */
gboolean success = lrg_inventory_swap_slots(inventory, 0, 5);
```

### Move Items Between Slots

```c
/* Move items from one slot to another */
guint moved = lrg_inventory_move_to_slot(inventory, 0, 5, 3);

/* Move all items from slot */
guint all_moved = lrg_inventory_move_to_slot(inventory, 0, 5, -1);
```

If destination is occupied, items merge if compatible. Fails silently if incompatible.

### Sort Inventory

```c
/* Sort by item type, then ID, and combine partial stacks */
lrg_inventory_sort(inventory);
```

Useful for UI organization and ensuring efficient space usage.

### Clear Inventory

```c
/* Remove all items */
lrg_inventory_clear(inventory);
```

## Virtual Functions

Subclass `LrgInventory` to customize behavior:

### can_accept

```c
static gboolean
my_inventory_can_accept(LrgInventory *self,
                        LrgItemDef   *def,
                        gint          slot)
{
    /* Return TRUE if item can be added */
    /* slot == -1 means any slot */
    /* Implement to restrict item types */
    return TRUE;
}
```

Override to implement item restrictions. For example, a weapon-only rack or armor-only chest.

### on_item_added

```c
static void
my_inventory_on_item_added(LrgInventory *self,
                           guint         slot,
                           LrgItemStack *stack)
{
    /* Called after item successfully added */
    /* Update UI, trigger events, etc. */
}
```

### on_item_removed

```c
static void
my_inventory_on_item_removed(LrgInventory *self,
                             guint         slot,
                             LrgItemStack *stack)
{
    /* Called after item stack completely removed */
    /* Not called for partial removals */
}
```

## Signals

Inventories emit signals you can connect to:

```c
/* Item was added to a slot */
g_signal_connect(inventory, "item-added",
                G_CALLBACK(on_item_added), user_data);

/* Item stack was completely removed */
g_signal_connect(inventory, "item-removed",
                G_CALLBACK(on_item_removed), user_data);

/* Slot contents changed (add, remove, or swap) */
g_signal_connect(inventory, "slot-changed",
                G_CALLBACK(on_slot_changed), user_data);
```

Signal handler example:

```c
static void
on_item_added(LrgInventory *inventory,
              guint         slot,
              LrgItemStack *stack,
              gpointer      user_data)
{
    g_message("Added %u items to slot %u",
              lrg_item_stack_get_quantity(stack), slot);
}
```

## API Reference

### Construction
- `lrg_inventory_new(guint capacity)` → `LrgInventory *` (transfer full)

### Properties
- `lrg_inventory_get_capacity(LrgInventory *self)` → `guint`
- `lrg_inventory_set_capacity(LrgInventory *self, guint capacity)` → `void`
- `lrg_inventory_get_used_slots(LrgInventory *self)` → `guint`
- `lrg_inventory_get_free_slots(LrgInventory *self)` → `guint`
- `lrg_inventory_is_full(LrgInventory *self)` → `gboolean`
- `lrg_inventory_is_empty(LrgInventory *self)` → `gboolean`

### Slot Access
- `lrg_inventory_get_slot(LrgInventory *self, guint slot)` → `LrgItemStack *` (transfer none, nullable)
- `lrg_inventory_set_slot(LrgInventory *self, guint slot, LrgItemStack *stack)` → `gboolean`
- `lrg_inventory_clear_slot(LrgInventory *self, guint slot)` → `LrgItemStack *` (transfer full, nullable)
- `lrg_inventory_is_slot_empty(LrgInventory *self, guint slot)` → `gboolean`
- `lrg_inventory_find_empty_slot(LrgInventory *self)` → `gint`

### Adding Items
- `lrg_inventory_add_item(LrgInventory *self, LrgItemDef *def, guint quantity)` → `guint`
- `lrg_inventory_add_stack(LrgInventory *self, LrgItemStack *stack)` → `guint`
- `lrg_inventory_add_to_slot(LrgInventory *self, guint slot, LrgItemDef *def, guint quantity)` → `guint`

### Removing Items
- `lrg_inventory_remove_item(LrgInventory *self, const gchar *item_id, guint quantity)` → `guint`
- `lrg_inventory_remove_from_slot(LrgInventory *self, guint slot, guint quantity)` → `guint`

### Finding Items
- `lrg_inventory_find_item(LrgInventory *self, const gchar *item_id)` → `LrgItemStack *` (transfer none, nullable)
- `lrg_inventory_find_item_slot(LrgInventory *self, const gchar *item_id)` → `gint`
- `lrg_inventory_count_item(LrgInventory *self, const gchar *item_id)` → `guint`
- `lrg_inventory_has_item(LrgInventory *self, const gchar *item_id, guint quantity)` → `gboolean`

### Slot Operations
- `lrg_inventory_swap_slots(LrgInventory *self, guint slot_a, guint slot_b)` → `gboolean`
- `lrg_inventory_move_to_slot(LrgInventory *self, guint from_slot, guint to_slot, gint quantity)` → `guint`
- `lrg_inventory_sort(LrgInventory *self)` → `void`
- `lrg_inventory_clear(LrgInventory *self)` → `void`

### Virtual Function Wrappers
- `lrg_inventory_can_accept(LrgInventory *self, LrgItemDef *def, gint slot)` → `gboolean`

## Example: Custom Item Container

```c
/* Create custom chest that only accepts materials */
typedef struct _MyChest MyChest;
G_DECLARE_DERIVABLE_TYPE(MyChest, my_chest, MY, CHEST, LrgInventory)

static gboolean
my_chest_can_accept(LrgInventory *self,
                    LrgItemDef   *def,
                    gint          slot)
{
    /* Only accept material items */
    return lrg_item_def_get_item_type(def) == LRG_ITEM_TYPE_MATERIAL;
}

struct _MyChestClass {
    LrgInventoryClass parent_class;
};

/* In implementation */
g_type_class_add_private(klass, sizeof(MyChestPrivate));
klass->can_accept = my_chest_can_accept;
```

## Example: Inventory UI Integration

```c
/* Create inventory and connect to UI updates */
g_autoptr(LrgInventory) inventory = lrg_inventory_new(20);

g_signal_connect(inventory, "item-added",
                G_CALLBACK(on_item_added), ui_context);
g_signal_connect(inventory, "item-removed",
                G_CALLBACK(on_item_removed), ui_context);
g_signal_connect(inventory, "slot-changed",
                G_CALLBACK(on_slot_changed), ui_context);

/* Add item and UI automatically updates */
lrg_inventory_add_item(inventory, potion_def, 10);
```

## See Also

- [LrgItemDef](item-def.md) - Item type definitions
- [LrgItemStack](item-stack.md) - Item instances
- [LrgEquipment](equipment.md) - Equipment slot management
- [Saveable Interface](../../guides/implementing-saveable.md) - Persist inventory state
