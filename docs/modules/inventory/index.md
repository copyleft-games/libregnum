# Inventory Module

The Inventory module provides item management systems for games, including item definitions, stackable items, containers, and equipment management. It's designed around a flexible data-driven approach where items can have custom properties and support both stackable and unique items.

## Overview

The Inventory module consists of four main components:

1. **LrgItemDef** - Defines the blueprint for item types with properties like name, description, and custom attributes
2. **LrgItemStack** - Represents a collection of items of the same type with instance-specific data
3. **LrgInventory** - A container for managing multiple item stacks across slots
4. **LrgEquipment** - Specialized inventory for equipped items in specific slots (head, chest, weapon, etc.)

## Key Features

- **Item Stacking**: Automatically stack identical items with configurable maximum stack sizes
- **Instance Data**: Store per-instance information like durability or enchantments
- **Custom Properties**: Define arbitrary attributes on items (damage, defense, weight, etc.)
- **Equipment Management**: Manage equipped items with slot validation
- **Stat Bonuses**: Calculate aggregated stats from equipped items
- **Flexible Subclassing**: Extend Inventory for custom container types (shops, chests, etc.)
- **Signal Support**: React to inventory changes via GObject signals

## Components

### [LrgItemDef - Item Type Definition](item-def.md)

Item definitions represent the blueprint for item types. They define base properties that all items of that type share, including name, description, stackability, value, and custom properties. Item definitions are derivable, allowing you to create custom item subclasses with behavior.

**Key Concepts:**
- Unique ID identifies each item type
- Stackable vs non-stackable items
- Custom properties (int, float, string, bool)
- Virtual functions for custom behavior (on_use, can_stack_with, get_tooltip)

### [LrgItemStack - Item Instances](item-stack.md)

Item stacks represent actual items in the game. A stack is a collection of identical items plus instance-specific data. Each stack has a quantity that respects the maximum stack size from its definition.

**Key Concepts:**
- Reference-counted boxed type
- Quantity management with max stack enforcement
- Stack merging and splitting operations
- Instance data storage (durability, enchantments, etc.)
- Item usage and consumption

### [LrgInventory - Item Container](inventory.md)

Inventories are containers for item stacks organized in slots. They handle the complexity of adding, removing, and organizing items, including automatic stacking logic. Inventories are derivable for custom container types.

**Key Concepts:**
- Fixed capacity slot-based storage
- Automatic stacking of identical items
- Slot operations (swap, move, clear)
- Item searching and counting
- Inventory sorting and compaction
- Signal support for inventory changes

### [LrgEquipment - Equipment Slots](equipment.md)

Equipment managers handle equipped items in specific slots. Unlike general inventories, equipment slots have semantic meaning (head, chest, weapon, etc.) and may enforce type restrictions.

**Key Concepts:**
- Predefined equipment slots
- One item per slot
- Slot-based type validation
- Stat bonus calculation from equipped items
- Signals for equip/unequip events

## Usage Example

```c
/* Create item definitions */
g_autoptr(LrgItemDef) sword = lrg_item_def_new("sword_iron");
lrg_item_def_set_name(sword, "Iron Sword");
lrg_item_def_set_item_type(sword, LRG_ITEM_TYPE_WEAPON);
lrg_item_def_set_stackable(sword, FALSE);
lrg_item_def_set_property_int(sword, "attack", 15);

g_autoptr(LrgItemDef) potion = lrg_item_def_new("potion_health");
lrg_item_def_set_name(potion, "Health Potion");
lrg_item_def_set_item_type(potion, LRG_ITEM_TYPE_CONSUMABLE);
lrg_item_def_set_stackable(potion, TRUE);
lrg_item_def_set_max_stack(potion, 10);

/* Create inventory and add items */
g_autoptr(LrgInventory) inventory = lrg_inventory_new(20);
lrg_inventory_add_item(inventory, sword, 1);
lrg_inventory_add_item(inventory, potion, 5);

/* Use items */
g_autoptr(LrgItemStack) stack = lrg_inventory_find_item(inventory, "potion_health");
lrg_item_stack_use(stack, NULL, 1);

/* Equipment */
g_autoptr(LrgEquipment) equipment = lrg_equipment_new();
g_autoptr(LrgItemStack) sword_stack = lrg_item_stack_new(sword, 1);
lrg_equipment_equip(equipment, LRG_EQUIPMENT_SLOT_WEAPON, sword_stack);

/* Get stat bonus */
gint attack = lrg_equipment_get_stat_bonus(equipment, "attack");
```

## Design Patterns

### Item Type System

Items are categorized by type via `LrgItemType` enum:
- `LRG_ITEM_TYPE_GENERIC` - General purpose items
- `LRG_ITEM_TYPE_WEAPON` - Weapons and attacking tools
- `LRG_ITEM_TYPE_ARMOR` - Armor and protective gear
- `LRG_ITEM_TYPE_CONSUMABLE` - Consumable items (potions, food)
- `LRG_ITEM_TYPE_MATERIAL` - Raw materials and crafting components
- `LRG_ITEM_TYPE_KEY` - Quest items and keys
- `LRG_ITEM_TYPE_QUEST` - Quest-specific items

Equipment slots validate item types:
- WEAPON slot accepts `LRG_ITEM_TYPE_WEAPON`
- HEAD/CHEST/LEGS/FEET/HANDS accept `LRG_ITEM_TYPE_ARMOR`
- OFFHAND accepts WEAPON or ARMOR (for shields)
- ACCESSORY accepts `LRG_ITEM_TYPE_GENERIC` (rings, amulets)

### Custom Properties

Items support arbitrary custom properties for game-specific attributes:

```c
/* Set custom properties on item definition */
lrg_item_def_set_property_int(sword, "damage", 25);
lrg_item_def_set_property_float(sword, "weight", 2.5f);
lrg_item_def_set_property_string(sword, "element", "fire");
lrg_item_def_set_property_bool(sword, "magical", TRUE);

/* Or store instance-specific data */
LrgItemStack *stack = lrg_item_stack_new(sword, 1);
lrg_item_stack_set_data_int(stack, "durability", 85);
lrg_item_stack_set_data_string(stack, "owner", "player1");
```

### Virtual Functions and Subclassing

Create custom item types with behavior:

```c
/* Define custom item subclass */
typedef struct _MyCustomItem MyCustomItem;
G_DECLARE_DERIVABLE_TYPE(MyCustomItem, my_custom_item, MY, CUSTOM_ITEM, LrgItemDef)

static gboolean
my_custom_item_on_use(LrgItemDef *def, GObject *owner, guint quantity)
{
    MyCustomItem *self = MY_CUSTOM_ITEM(def);
    /* Custom use behavior */
    return TRUE;
}

/* Implement custom subclass */
...
```

## Common Patterns

### Checking Inventory Space

```c
guint free_slots = lrg_inventory_get_free_slots(inventory);
if (free_slots > 0 || lrg_inventory_can_accept(inventory, item_def, -1)) {
    guint added = lrg_inventory_add_item(inventory, item_def, quantity);
}
```

### Moving Items Between Inventories

```c
/* Remove from source */
guint removed = lrg_inventory_remove_item(src_inv, "item_id", 5);

/* Add to destination */
LrgItemStack *temp_stack = lrg_item_stack_new(def, removed);
guint added = lrg_inventory_add_stack(dest_inv, temp_stack);
lrg_item_stack_unref(temp_stack);
```

### Equipment with Stats

```c
/* Equip items and calculate total bonuses */
LrgItemStack *helmet = lrg_item_stack_new(helmet_def, 1);
LrgItemStack *armor = lrg_item_stack_new(armor_def, 1);

lrg_equipment_equip(equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet);
lrg_equipment_equip(equipment, LRG_EQUIPMENT_SLOT_CHEST, armor);

gint total_defense = lrg_equipment_get_stat_bonus(equipment, "defense");
gfloat total_weight = lrg_equipment_get_stat_bonus_float(equipment, "weight");
```

### Listening to Inventory Changes

```c
g_signal_connect(inventory, "item-added",
                G_CALLBACK(on_item_added), user_data);
g_signal_connect(inventory, "item-removed",
                G_CALLBACK(on_item_removed), user_data);
g_signal_connect(inventory, "slot-changed",
                G_CALLBACK(on_slot_changed), user_data);
```

## Integration with Other Modules

The Inventory module integrates with:
- **Data Loader**: Load item definitions from YAML
- **Saveable Interface**: Serialize/deserialize inventories
- **UI Module**: Display inventories and equipment
- **Dialog System**: Handle item-related dialogs
- **Quest System**: Track quest items in inventory

## See Also

- [LrgItemDef Documentation](item-def.md)
- [LrgItemStack Documentation](item-stack.md)
- [LrgInventory Documentation](inventory.md)
- [LrgEquipment Documentation](equipment.md)
