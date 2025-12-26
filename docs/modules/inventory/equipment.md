# LrgEquipment - Equipment Slot Management

`LrgEquipment` is a final GObject class that manages equipped items in specific slots. Unlike general inventories, each equipment slot has semantic meaning and type validation.

## Type Information

- **Type Name**: `LrgEquipment`
- **Base Class**: `GObject`
- **Type Class**: Final (cannot be subclassed)
- **Transfer**: Full ownership when returned

## Overview

Equipment managers handle character gear:
- Predefined slots for specific equipment locations (head, chest, weapon, etc.)
- One item per slot
- Type validation (weapons in weapon slot, armor in armor slots)
- Stat bonus calculation from equipped items
- Signal support for equip/unequip events

Perfect for character equipment screens, inventory UI, and stat calculations.

## Equipment Slots

Equipment supports the following slots:

| Slot | Type | Accepts |
|------|------|---------|
| HEAD | `LRG_EQUIPMENT_SLOT_HEAD` | LRG_ITEM_TYPE_ARMOR |
| CHEST | `LRG_EQUIPMENT_SLOT_CHEST` | LRG_ITEM_TYPE_ARMOR |
| LEGS | `LRG_EQUIPMENT_SLOT_LEGS` | LRG_ITEM_TYPE_ARMOR |
| FEET | `LRG_EQUIPMENT_SLOT_FEET` | LRG_ITEM_TYPE_ARMOR |
| HANDS | `LRG_EQUIPMENT_SLOT_HANDS` | LRG_ITEM_TYPE_ARMOR |
| WEAPON | `LRG_EQUIPMENT_SLOT_WEAPON` | LRG_ITEM_TYPE_WEAPON |
| OFFHAND | `LRG_EQUIPMENT_SLOT_OFFHAND` | WEAPON or ARMOR (shields) |
| ACCESSORY | `LRG_EQUIPMENT_SLOT_ACCESSORY` | LRG_ITEM_TYPE_GENERIC |

## Construction

### Creating Equipment

```c
/* Create new equipment manager */
g_autoptr(LrgEquipment) equipment = lrg_equipment_new();

/* All slots are empty initially */
g_assert_true(lrg_equipment_is_slot_empty(equipment, LRG_EQUIPMENT_SLOT_HEAD));
```

## Slot Access

### Get Equipped Item

```c
/* Get item in slot (transfer none, nullable) */
LrgItemStack *helmet = lrg_equipment_get_slot(equipment, LRG_EQUIPMENT_SLOT_HEAD);
if (helmet != NULL) {
    g_message("Wearing: %s", lrg_item_def_get_name(lrg_item_stack_get_def(helmet)));
}
```

### Check Slot Status

```c
if (lrg_equipment_is_slot_empty(equipment, LRG_EQUIPMENT_SLOT_CHEST)) {
    /* No chest armor equipped */
}
```

## Equipping and Unequipping

### Equip Item

```c
/* Create item stack and equip */
g_autoptr(LrgItemStack) sword = lrg_item_stack_new(sword_def, 1);

/* Equip returns previously equipped item (if any) */
g_autoptr(LrgItemStack) old = lrg_equipment_equip(equipment,
                                                   LRG_EQUIPMENT_SLOT_WEAPON,
                                                   sword);

if (old != NULL) {
    /* A different weapon was equipped before */
    g_message("Unequipped: %s",
              lrg_item_def_get_name(lrg_item_stack_get_def(old)));
}

/* Emits "item-equipped" signal */
```

### Unequip Item

```c
/* Remove and return equipped item */
g_autoptr(LrgItemStack) removed = lrg_equipment_unequip(equipment,
                                                        LRG_EQUIPMENT_SLOT_HEAD);

if (removed != NULL) {
    /* Handle unequipped item (add back to inventory, drop, etc.) */
    lrg_inventory_add_stack(inventory, removed);
}

/* Emits "item-unequipped" signal if item was present */
```

### Clear All Equipment

```c
/* Unequip everything */
lrg_equipment_clear(equipment);

/* All slots now empty */
g_assert_true(lrg_equipment_is_slot_empty(equipment, LRG_EQUIPMENT_SLOT_HEAD));
```

## Validation

### Can Equip

```c
/* Check if item can be equipped in a slot */
if (lrg_equipment_can_equip(equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_def)) {
    /* Can equip */
} else {
    /* Item type doesn't match slot */
    g_warning("Cannot equip weapon in armor slot");
}
```

Validation rules:
- Armor slots (HEAD, CHEST, LEGS, FEET, HANDS) only accept armor items
- Weapon slot only accepts weapons
- Offhand accepts weapons or armor (for shields)
- Accessory accepts generic items

## Queries

### Get Equipped Slots

```c
/* Get list of slots with items equipped (transfer container) */
g_autoptr(GList) slots = lrg_equipment_get_equipped_slots(equipment);

for (GList *l = slots; l; l = l->next) {
    LrgEquipmentSlot slot = GPOINTER_TO_INT(l->data);
    LrgItemStack *item = lrg_equipment_get_slot(equipment, slot);
    g_message("Slot %d: %s",
              slot,
              lrg_item_def_get_name(lrg_item_stack_get_def(item)));
}
```

## Stat Bonuses

Equipment items can provide stat bonuses. Stats are stored as custom properties on item definitions.

### Integer Stat Bonuses

```c
/* Sum up defense from all equipped items */
gint total_defense = lrg_equipment_get_stat_bonus(equipment, "defense");

/* Helmet: defense +5 */
/* Chestplate: defense +10 */
/* Total: 15 */
```

### Float Stat Bonuses

```c
/* Sum up weight from equipped items */
gfloat total_weight = lrg_equipment_get_stat_bonus_float(equipment, "weight");

/* Helmet: weight 2.5 */
/* Chestplate: weight 5.0 */
/* Total: 7.5 */
```

Example: Defining items with stats

```c
/* Create armor with defense stat */
g_autoptr(LrgItemDef) helmet = lrg_item_def_new("helmet_iron");
lrg_item_def_set_item_type(helmet, LRG_ITEM_TYPE_ARMOR);
lrg_item_def_set_property_int(helmet, "defense", 5);
lrg_item_def_set_property_float(helmet, "weight", 2.5f);

g_autoptr(LrgItemDef) chestplate = lrg_item_def_new("chestplate_iron");
lrg_item_def_set_item_type(chestplate, LRG_ITEM_TYPE_ARMOR);
lrg_item_def_set_property_int(chestplate, "defense", 10);
lrg_item_def_set_property_float(chestplate, "weight", 5.0f);

/* Equip and calculate bonuses */
LrgItemStack *helmet_stack = lrg_item_stack_new(helmet, 1);
LrgItemStack *chest_stack = lrg_item_stack_new(chestplate, 1);

lrg_equipment_equip(equipment, LRG_EQUIPMENT_SLOT_HEAD, helmet_stack);
lrg_equipment_equip(equipment, LRG_EQUIPMENT_SLOT_CHEST, chest_stack);

gint defense = lrg_equipment_get_stat_bonus(equipment, "defense");  /* 15 */
gfloat weight = lrg_equipment_get_stat_bonus_float(equipment, "weight");  /* 7.5 */
```

## Signals

Equipment emits signals when items are equipped or unequipped:

```c
/* Item was equipped */
g_signal_connect(equipment, "item-equipped",
                G_CALLBACK(on_item_equipped), user_data);

/* Item was unequipped */
g_signal_connect(equipment, "item-unequipped",
                G_CALLBACK(on_item_unequipped), user_data);
```

Signal handler example:

```c
static void
on_item_equipped(LrgEquipment    *equipment,
                 LrgEquipmentSlot slot,
                 LrgItemStack    *item,
                 gpointer         user_data)
{
    g_message("Equipped in slot %d: %s",
              slot,
              lrg_item_def_get_name(lrg_item_stack_get_def(item)));

    /* Update UI, recalculate stats, etc. */
}
```

## API Reference

### Construction
- `lrg_equipment_new(void)` → `LrgEquipment *` (transfer full)

### Slot Access
- `lrg_equipment_get_slot(LrgEquipment *self, LrgEquipmentSlot slot)` → `LrgItemStack *` (transfer none, nullable)
- `lrg_equipment_is_slot_empty(LrgEquipment *self, LrgEquipmentSlot slot)` → `gboolean`

### Equip/Unequip
- `lrg_equipment_equip(LrgEquipment *self, LrgEquipmentSlot slot, LrgItemStack *stack)` → `LrgItemStack *` (transfer full, nullable)
- `lrg_equipment_unequip(LrgEquipment *self, LrgEquipmentSlot slot)` → `LrgItemStack *` (transfer full, nullable)
- `lrg_equipment_clear(LrgEquipment *self)` → `void`

### Queries
- `lrg_equipment_get_equipped_slots(LrgEquipment *self)` → `GList *` (transfer container)
- `lrg_equipment_can_equip(LrgEquipment *self, LrgEquipmentSlot slot, LrgItemDef *def)` → `gboolean`

### Stats
- `lrg_equipment_get_stat_bonus(LrgEquipment *self, const gchar *stat_name)` → `gint`
- `lrg_equipment_get_stat_bonus_float(LrgEquipment *self, const gchar *stat_name)` → `gfloat`

## Example: Character Equipment System

```c
typedef struct {
    LrgEquipment *equipment;
    gint base_attack;
    gint base_defense;
} Character;

/* Calculate effective stats with equipment bonuses */
gint get_character_attack(Character *ch)
{
    gint attack_bonus = lrg_equipment_get_stat_bonus(ch->equipment, "attack");
    return ch->base_attack + attack_bonus;
}

gint get_character_defense(Character *ch)
{
    gint defense_bonus = lrg_equipment_get_stat_bonus(ch->equipment, "defense");
    return ch->base_defense + defense_bonus;
}

/* Equip an item */
void equip_item(Character *ch, LrgEquipmentSlot slot, LrgItemDef *item_def)
{
    if (!lrg_equipment_can_equip(ch->equipment, slot, item_def)) {
        g_warning("Cannot equip item in this slot");
        return;
    }

    g_autoptr(LrgItemStack) stack = lrg_item_stack_new(item_def, 1);
    g_autoptr(LrgItemStack) old = lrg_equipment_equip(ch->equipment, slot, stack);

    g_message("Attack: %d, Defense: %d",
              get_character_attack(ch),
              get_character_defense(ch));
}
```

## See Also

- [LrgItemDef](item-def.md) - Item type definitions
- [LrgItemStack](item-stack.md) - Item instances
- [LrgInventory](inventory.md) - General item containers
