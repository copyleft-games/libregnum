# LrgItemStack - Item Instances

`LrgItemStack` is a reference-counted boxed type representing a collection of items of the same type with instance-specific data. It combines a reference to an `LrgItemDef` with a quantity and per-instance attributes like durability or enchantments.

## Type Information

- **Type Name**: `LrgItemStack`
- **Type Class**: Boxed type (reference-counted)
- **Transfer**: Full ownership when returned (call unref when done)
- **Auto Cleanup**: `g_autoptr(LrgItemStack)`

## Overview

While `LrgItemDef` defines the template, `LrgItemStack` represents actual items:

```
LrgItemDef (sword_iron)     ← Template defining all iron swords
  ├─ name: "Iron Sword"
  ├─ attack: 15
  └─ stackable: FALSE

LrgItemStack (quantity: 1)   ← Your actual iron sword
  ├─ def: sword_iron
  ├─ quantity: 1
  └─ instance_data:
     ├─ durability: 85
     └─ owner: "player1"
```

## Construction and Cleanup

### Creating Item Stacks

```c
/* Create a stack of 5 potions */
g_autoptr(LrgItemStack) potions = lrg_item_stack_new(potion_def, 5);

/* Create a single weapon (non-stackable) */
g_autoptr(LrgItemStack) sword = lrg_item_stack_new(sword_def, 1);
```

Quantity is automatically clamped to the item's max_stack value.

### Reference Counting

```c
/* Manual reference management */
LrgItemStack *stack = lrg_item_stack_new(def, 10);

/* Increase reference count */
LrgItemStack *ref = lrg_item_stack_ref(stack);

/* Decrease reference count - free when zero */
lrg_item_stack_unref(stack);
lrg_item_stack_unref(ref);

/* Or use auto cleanup */
{
    g_autoptr(LrgItemStack) auto_stack = lrg_item_stack_new(def, 10);
    /* Automatically unref'd when leaving scope */
}
```

### Copying

```c
/* Deep copy of the stack and all its instance data */
g_autoptr(LrgItemStack) original = lrg_item_stack_new(def, 1);
lrg_item_stack_set_data_int(original, "durability", 85);

g_autoptr(LrgItemStack) copy = lrg_item_stack_copy(original);

/* Modifying copy doesn't affect original */
lrg_item_stack_set_data_int(copy, "durability", 50);
g_assert_cmpint(lrg_item_stack_get_data_int(original, "durability", 0), ==, 85);
```

## Properties

### Item Definition

```c
LrgItemDef *def = lrg_item_stack_get_def(stack);  /* transfer none */
```

Get the definition this stack is based on. Never NULL.

### Quantity Management

#### Current Quantity
```c
guint qty = lrg_item_stack_get_quantity(stack);
lrg_item_stack_set_quantity(stack, 10);
```

Setting quantity automatically clamps to max_stack.

#### Maximum Quantity
```c
guint max = lrg_item_stack_get_max_quantity(stack);
```

Returns the max_stack from the item definition.

#### Space Remaining
```c
guint space = lrg_item_stack_get_space_remaining(stack);
```

How many more items can be added before the stack is full.

#### Status Checks
```c
if (lrg_item_stack_is_full(stack)) {
    /* Stack is at max capacity */
}

if (lrg_item_stack_is_empty(stack)) {
    /* Stack has zero items - can be removed */
}
```

### Quantity Operations

#### Add Items
```c
guint added = lrg_item_stack_add(stack, 5);
```

Adds items up to max_stack. Returns the actual amount added (may be less if limited by capacity).

#### Remove Items
```c
guint removed = lrg_item_stack_remove(stack, 3);
```

Removes items from the stack. Returns the actual amount removed (may be less if fewer items are present).

#### Split Stacks
```c
g_autoptr(LrgItemStack) new_stack = lrg_item_stack_split(stack, 3);
```

Creates a new stack with the specified quantity, removing it from the original. Returns NULL if the amount is invalid or 0.

```c
/* Before: stack has 10 items */
g_autoptr(LrgItemStack) half = lrg_item_stack_split(stack, 5);
/* After: original has 5, new stack has 5 */
```

### Stack Operations

#### Can Merge

```c
gboolean can_merge = lrg_item_stack_can_merge(stack1, stack2);
```

Checks if two stacks can be merged (same definition, not full).

#### Merge Stacks

```c
guint merged = lrg_item_stack_merge(dest, src);
```

Merges items from `src` into `dest` up to `dest`'s max capacity. Removes merged items from `src`. Returns the amount actually merged.

```c
/* Before: stack1 has 7, stack2 has 8, max 10 */
guint merged = lrg_item_stack_merge(stack1, stack2);
/* After: stack1 has 10, stack2 has 5, merged = 3 */
```

## Instance Data

Instance data stores per-stack attributes like durability, enchantments, or ownership.

### Setting Instance Data

```c
/* Integer data */
lrg_item_stack_set_data_int(stack, "durability", 85);
lrg_item_stack_set_data_int(stack, "level", 5);

/* Float data */
lrg_item_stack_set_data_float(stack, "enchant_power", 1.5f);

/* String data */
lrg_item_stack_set_data_string(stack, "owner", "player1");
lrg_item_stack_set_data_string(stack, "enchant", "sharpness");

/* Boolean data - though uncommon */
lrg_item_stack_set_data_int(stack, "cursed", 1);
```

### Getting Instance Data

```c
/* Provide default if not found */
gint durability = lrg_item_stack_get_data_int(stack, "durability", 100);
gfloat power = lrg_item_stack_get_data_float(stack, "enchant_power", 0.0f);
const gchar *owner = lrg_item_stack_get_data_string(stack, "owner");
```

Returns NULL for string data if the key doesn't exist.

### Managing Instance Data

```c
/* Check if data exists */
if (lrg_item_stack_has_data(stack, "durability")) {
    int dur = lrg_item_stack_get_data_int(stack, "durability", 100);
}

/* Remove specific data */
lrg_item_stack_remove_data(stack, "durability");

/* Remove all instance data */
lrg_item_stack_clear_data(stack);
```

## Using Items

### Consuming Items

```c
/* Use (consume) items from the stack */
guint consumed = lrg_item_stack_use(stack, owner, 1);
```

Calls the item definition's `on_use` virtual function and removes the quantity if consumed. The owner can be NULL.

Example: Consuming a potion

```c
g_autoptr(LrgItemStack) potions = lrg_inventory_find_item(inventory, "potion_health");
if (potions != NULL && lrg_item_stack_get_quantity(potions) > 0) {
    guint consumed = lrg_item_stack_use(potions, player, 1);
    if (consumed > 0) {
        /* Potion was consumed, health restored, etc. */
    }
}
```

## API Reference

### Construction
- `lrg_item_stack_new(LrgItemDef *def, guint quantity)` → `LrgItemStack *` (transfer full)

### Reference Counting
- `lrg_item_stack_ref(LrgItemStack *self)` → `LrgItemStack *` (transfer full)
- `lrg_item_stack_unref(LrgItemStack *self)` → `void`
- `lrg_item_stack_copy(const LrgItemStack *self)` → `LrgItemStack *` (transfer full)

### Properties
- `lrg_item_stack_get_def(const LrgItemStack *self)` → `LrgItemDef *` (transfer none)
- `lrg_item_stack_get_quantity(const LrgItemStack *self)` → `guint`
- `lrg_item_stack_set_quantity(LrgItemStack *self, guint quantity)` → `void`
- `lrg_item_stack_get_max_quantity(const LrgItemStack *self)` → `guint`
- `lrg_item_stack_is_full(const LrgItemStack *self)` → `gboolean`
- `lrg_item_stack_is_empty(const LrgItemStack *self)` → `gboolean`
- `lrg_item_stack_get_space_remaining(const LrgItemStack *self)` → `guint`

### Quantity Operations
- `lrg_item_stack_add(LrgItemStack *self, guint amount)` → `guint`
- `lrg_item_stack_remove(LrgItemStack *self, guint amount)` → `guint`
- `lrg_item_stack_split(LrgItemStack *self, guint amount)` → `LrgItemStack *` (transfer full, nullable)
- `lrg_item_stack_can_merge(const LrgItemStack *self, const LrgItemStack *other)` → `gboolean`
- `lrg_item_stack_merge(LrgItemStack *self, LrgItemStack *other)` → `guint`

### Instance Data
- `lrg_item_stack_get_data_int(const LrgItemStack *self, const gchar *key, gint default_value)` → `gint`
- `lrg_item_stack_set_data_int(LrgItemStack *self, const gchar *key, gint value)` → `void`
- `lrg_item_stack_get_data_float(const LrgItemStack *self, const gchar *key, gfloat default_value)` → `gfloat`
- `lrg_item_stack_set_data_float(LrgItemStack *self, const gchar *key, gfloat value)` → `void`
- `lrg_item_stack_get_data_string(const LrgItemStack *self, const gchar *key)` → `const gchar *` (transfer none, nullable)
- `lrg_item_stack_set_data_string(LrgItemStack *self, const gchar *key, const gchar *value)` → `void`
- `lrg_item_stack_has_data(const LrgItemStack *self, const gchar *key)` → `gboolean`
- `lrg_item_stack_remove_data(LrgItemStack *self, const gchar *key)` → `gboolean`
- `lrg_item_stack_clear_data(LrgItemStack *self)` → `void`

### Usage
- `lrg_item_stack_use(LrgItemStack *self, GObject *owner, guint quantity)` → `guint`

## Example: Durability System

```c
/* Create a sword with durability tracking */
g_autoptr(LrgItemStack) sword = lrg_item_stack_new(sword_def, 1);
lrg_item_stack_set_data_int(sword, "max_durability", 100);
lrg_item_stack_set_data_int(sword, "current_durability", 100);

/* Use the sword, reducing durability */
lrg_item_stack_set_data_int(sword,
    "current_durability",
    lrg_item_stack_get_data_int(sword, "current_durability", 100) - 5);

/* Check if broken */
gint durability = lrg_item_stack_get_data_int(sword, "current_durability", 0);
if (durability <= 0) {
    g_message("Sword is broken!");
}
```

## See Also

- [LrgItemDef](item-def.md) - Item type definitions
- [LrgInventory](inventory.md) - Container for stacks
- [LrgEquipment](equipment.md) - Equipment slot management
