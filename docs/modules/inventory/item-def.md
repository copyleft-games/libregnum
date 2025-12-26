# LrgItemDef - Item Type Definition

`LrgItemDef` is a derivable GObject class that defines the blueprint for item types. It stores the common properties and behavior shared by all items of that type.

## Type Information

- **Type Name**: `LrgItemDef`
- **Base Class**: `GObject`
- **Type Class**: Derivable (can be subclassed)
- **Transfer**: Full ownership when returned (unless noted otherwise)

## Overview

Item definitions represent the template for items. They contain:
- **Identification**: Unique ID
- **Display**: Name and description for UI
- **Categorization**: Item type (weapon, armor, consumable, etc.)
- **Stack Configuration**: Whether stackable and max stack size
- **Value**: Base price/value for trade
- **Custom Properties**: Arbitrary game-specific attributes

Each item definition is created once and referenced by item instances (stacks). This separation allows efficient storage and sharing of common properties.

## Construction

### Creating Item Definitions

```c
LrgItemDef *def = lrg_item_def_new("sword_iron");
lrg_item_def_set_name(def, "Iron Sword");
lrg_item_def_set_description(def, "A well-crafted iron sword");
lrg_item_def_set_item_type(def, LRG_ITEM_TYPE_WEAPON);
lrg_item_def_set_stackable(def, FALSE);
lrg_item_def_set_max_stack(def, 1);
lrg_item_def_set_value(def, 100);
```

## Properties

### Basic Properties

All items have these core properties:

#### ID (Read-Only)
```c
const gchar *id = lrg_item_def_get_id(def);
```

The unique identifier for the item type. Set at construction time, cannot be changed.

#### Name
```c
lrg_item_def_set_name(def, "Iron Sword");
const gchar *name = lrg_item_def_get_name(def);
```

Display name for UI. Can be localized via the I18N system.

#### Description
```c
lrg_item_def_set_description(def, "A sturdy iron weapon");
const gchar *desc = lrg_item_def_get_description(def);
```

Detailed description shown in item tooltips or inventory UI.

#### Item Type
```c
lrg_item_def_set_item_type(def, LRG_ITEM_TYPE_WEAPON);
LrgItemType type = lrg_item_def_get_item_type(def);
```

Categorizes the item. Valid values:
- `LRG_ITEM_TYPE_GENERIC` (default)
- `LRG_ITEM_TYPE_WEAPON`
- `LRG_ITEM_TYPE_ARMOR`
- `LRG_ITEM_TYPE_CONSUMABLE`
- `LRG_ITEM_TYPE_MATERIAL`
- `LRG_ITEM_TYPE_KEY`
- `LRG_ITEM_TYPE_QUEST`

#### Stackability
```c
lrg_item_def_set_stackable(def, TRUE);
gboolean can_stack = lrg_item_def_get_stackable(def);
```

Whether items of this type can stack. Non-stackable items always have max_stack = 1.

#### Maximum Stack Size
```c
lrg_item_def_set_max_stack(def, 10);
guint max = lrg_item_def_get_max_stack(def);
```

Maximum quantity in a single stack. Automatically set to 1 if stackable = FALSE. Default is 99.

#### Value
```c
lrg_item_def_set_value(def, 25);
gint value = lrg_item_def_get_value(def);
```

Base monetary value for trading. Can be negative for debts or special items.

## Custom Properties

Items support arbitrary custom properties for game-specific attributes:

### Setting Custom Properties

```c
/* Integer properties */
lrg_item_def_set_property_int(def, "damage", 25);
lrg_item_def_set_property_int(def, "defense", 10);

/* Float properties */
lrg_item_def_set_property_float(def, "weight", 2.5f);
lrg_item_def_set_property_float(def, "attack_speed", 1.2f);

/* String properties */
lrg_item_def_set_property_string(def, "element", "fire");
lrg_item_def_set_property_string(def, "material", "iron");

/* Boolean properties */
lrg_item_def_set_property_bool(def, "magical", TRUE);
lrg_item_def_set_property_bool(def, "unique", FALSE);
```

### Getting Custom Properties

```c
/* Return default if not found */
gint damage = lrg_item_def_get_property_int(def, "damage", 0);
gfloat weight = lrg_item_def_get_property_float(def, "weight", 0.0f);
const gchar *element = lrg_item_def_get_property_string(def, "element");
gboolean magical = lrg_item_def_get_property_bool(def, "magical", FALSE);
```

### Managing Custom Properties

```c
/* Check if property exists */
if (lrg_item_def_has_custom_property(def, "damage")) {
    /* ... */
}

/* Remove property */
lrg_item_def_remove_custom_property(def, "damage");
```

## Virtual Functions

### on_use

Called when an item stack is used. Override to implement item behavior.

```c
static gboolean
my_item_on_use(LrgItemDef *def, GObject *owner, guint quantity)
{
    /* Called when item is used */
    /* owner can be NULL */
    /* quantity is how many items are being used */
    /* Return TRUE if items were consumed */
    return TRUE;
}

/* In your item subclass */
struct _MyItemClass {
    LrgItemDefClass parent_class;
    /* ... */
};
klass->on_use = my_item_on_use;
```

### can_stack_with

Determines if two item definitions can stack together. Default implementation returns TRUE if both have the same ID.

```c
static gboolean
my_item_can_stack_with(LrgItemDef *self, LrgItemDef *other)
{
    /* Return TRUE if items can stack */
    /* Called before stacking items together */
    return TRUE;
}
```

Override to implement custom stacking rules, e.g., allowing same items with different enchantments to stack.

### get_tooltip

Generates tooltip text shown in UI. Default implementation returns the description.

```c
static gchar *
my_item_get_tooltip(LrgItemDef *self)
{
    /* Return newly allocated tooltip string (transfer full) */
    return g_strdup("Item tooltip here");
}
```

## API Reference

### Construction
- `lrg_item_def_new(const gchar *id)` - Create new item definition

### Properties
- `lrg_item_def_get_id(LrgItemDef *self)` → `const gchar *` (transfer none)
- `lrg_item_def_get_name(LrgItemDef *self)` → `const gchar *` (transfer none, nullable)
- `lrg_item_def_set_name(LrgItemDef *self, const gchar *name)`
- `lrg_item_def_get_description(LrgItemDef *self)` → `const gchar *` (transfer none, nullable)
- `lrg_item_def_set_description(LrgItemDef *self, const gchar *description)`
- `lrg_item_def_get_item_type(LrgItemDef *self)` → `LrgItemType`
- `lrg_item_def_set_item_type(LrgItemDef *self, LrgItemType item_type)`
- `lrg_item_def_get_stackable(LrgItemDef *self)` → `gboolean`
- `lrg_item_def_set_stackable(LrgItemDef *self, gboolean stackable)`
- `lrg_item_def_get_max_stack(LrgItemDef *self)` → `guint`
- `lrg_item_def_set_max_stack(LrgItemDef *self, guint max_stack)`
- `lrg_item_def_get_value(LrgItemDef *self)` → `gint`
- `lrg_item_def_set_value(LrgItemDef *self, gint value)`

### Custom Properties
- `lrg_item_def_get_property_int(LrgItemDef *self, const gchar *key, gint default_value)` → `gint`
- `lrg_item_def_set_property_int(LrgItemDef *self, const gchar *key, gint value)`
- `lrg_item_def_get_property_float(LrgItemDef *self, const gchar *key, gfloat default_value)` → `gfloat`
- `lrg_item_def_set_property_float(LrgItemDef *self, const gchar *key, gfloat value)`
- `lrg_item_def_get_property_string(LrgItemDef *self, const gchar *key)` → `const gchar *` (transfer none, nullable)
- `lrg_item_def_set_property_string(LrgItemDef *self, const gchar *key, const gchar *value)`
- `lrg_item_def_get_property_bool(LrgItemDef *self, const gchar *key, gboolean default_value)` → `gboolean`
- `lrg_item_def_set_property_bool(LrgItemDef *self, const gchar *key, gboolean value)`
- `lrg_item_def_has_custom_property(LrgItemDef *self, const gchar *key)` → `gboolean`
- `lrg_item_def_remove_custom_property(LrgItemDef *self, const gchar *key)` → `gboolean`

### Virtual Function Wrappers
- `lrg_item_def_use(LrgItemDef *self, GObject *owner, guint quantity)` → `gboolean`
- `lrg_item_def_can_stack_with(LrgItemDef *self, LrgItemDef *other)` → `gboolean`
- `lrg_item_def_get_tooltip(LrgItemDef *self)` → `gchar *` (transfer full, nullable)

## Example: Complex Item Definition

```c
/* Create a magical sword with enchantment properties */
g_autoptr(LrgItemDef) enchanted_sword = lrg_item_def_new("sword_flame_burst");
lrg_item_def_set_name(enchanted_sword, "Flame Burst Sword");
lrg_item_def_set_description(enchanted_sword,
    "A legendary blade infused with fire magic");
lrg_item_def_set_item_type(enchanted_sword, LRG_ITEM_TYPE_WEAPON);
lrg_item_def_set_stackable(enchanted_sword, FALSE);
lrg_item_def_set_value(enchanted_sword, 5000);

/* Combat stats */
lrg_item_def_set_property_int(enchanted_sword, "attack", 35);
lrg_item_def_set_property_float(enchanted_sword, "attack_speed", 1.3f);

/* Enchantment properties */
lrg_item_def_set_property_string(enchanted_sword, "element", "fire");
lrg_item_def_set_property_int(enchanted_sword, "fire_damage", 20);
lrg_item_def_set_property_float(enchanted_sword, "weight", 3.5f);
lrg_item_def_set_property_bool(enchanted_sword, "magical", TRUE);
lrg_item_def_set_property_bool(enchanted_sword, "unique", TRUE);
```

## Loading from YAML

Item definitions can be loaded from YAML files via the Data Loader:

```yaml
# items/sword_iron.yaml
id: sword_iron
name: Iron Sword
description: A sturdy iron weapon
type: weapon
stackable: false
max_stack: 1
value: 100

properties:
  attack: 15
  weight: 2.5
  material: iron
```

## See Also

- [LrgItemStack](item-stack.md) - Item instances with quantity
- [LrgInventory](inventory.md) - Container for item stacks
- [LrgEquipment](equipment.md) - Equipment slot management
- [Data Loader](../../guides/data-loading.md) - Loading definitions from YAML
