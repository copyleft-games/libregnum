# LrgSpawnPoint3D

## Overview

`LrgSpawnPoint3D` defines locations where entities (players, enemies, NPCs, items) can be created in a 3D level. Each spawn point can carry custom properties to configure spawned entities.

## Type Information

- **Type Name**: `LrgSpawnPoint3D`
- **Type ID**: `LRG_TYPE_SPAWN_POINT3D`
- **Type Category**: Boxed Type (immutable value type)

## Construction

### lrg_spawn_point3d_new

```c
LrgSpawnPoint3D *lrg_spawn_point3d_new(const gchar *id, gfloat x, gfloat y, gfloat z,
                                       LrgSpawnType spawn_type);
```

Creates a new spawn point at the given position.

**Parameters:**
- `spawn_type` - Type: PLAYER, ENEMY, ITEM, NPC, etc.

**Example:**
```c
g_autoptr(LrgSpawnPoint3D) spawn = lrg_spawn_point3d_new("player_start", 0.0f, 5.0f, 0.0f, LRG_SPAWN_TYPE_PLAYER);
```

### lrg_spawn_point3d_new_from_vector

```c
LrgSpawnPoint3D *lrg_spawn_point3d_new_from_vector(const gchar *id, const GrlVector3 *position,
                                                   LrgSpawnType spawn_type);
```

Creates a spawn point from a vector position.

### lrg_spawn_point3d_copy

```c
LrgSpawnPoint3D *lrg_spawn_point3d_copy(const LrgSpawnPoint3D *self);
```

Creates a copy (nullable safe).

### lrg_spawn_point3d_free

```c
void lrg_spawn_point3d_free(LrgSpawnPoint3D *self);
```

Frees a spawn point (nullable safe).

**Use with `g_autoptr(LrgSpawnPoint3D)` for automatic cleanup.**

## Properties

### lrg_spawn_point3d_get_id

```c
const gchar *lrg_spawn_point3d_get_id(const LrgSpawnPoint3D *self);
```

Gets the spawn point identifier.

### lrg_spawn_point3d_get_position

```c
GrlVector3 *lrg_spawn_point3d_get_position(const LrgSpawnPoint3D *self);
```

Gets the world position.

**Example:**
```c
const LrgSpawnPoint3D *spawn = lrg_level3d_get_spawn_point(level, "enemy_1");
g_autoptr(GrlVector3) pos = lrg_spawn_point3d_get_position(spawn);
g_print("Spawn at: (%.1f, %.1f, %.1f)\n", pos->x, pos->y, pos->z);
```

### lrg_spawn_point3d_get_rotation / lrg_spawn_point3d_set_rotation

```c
GrlVector3 *lrg_spawn_point3d_get_rotation(const LrgSpawnPoint3D *self);
void lrg_spawn_point3d_set_rotation(LrgSpawnPoint3D *self, const GrlVector3 *rotation);
```

Gets/sets rotation in Euler angles (degrees).

## Spawn Configuration

### lrg_spawn_point3d_get_spawn_type

```c
LrgSpawnType lrg_spawn_point3d_get_spawn_type(const LrgSpawnPoint3D *self);
```

Gets the spawn type.

**Possible Values:**
- `LRG_SPAWN_TYPE_PLAYER` - Player character
- `LRG_SPAWN_TYPE_ENEMY` - Enemy/monster
- `LRG_SPAWN_TYPE_NPC` - Non-player character
- `LRG_SPAWN_TYPE_ITEM` - Collectable item
- `LRG_SPAWN_TYPE_PROJECTILE` - Bullet, missile, etc.
- `LRG_SPAWN_TYPE_PICKUP` - Health, ammo, etc.
- `LRG_SPAWN_TYPE_DECORATION` - Visual only
- `LRG_SPAWN_TYPE_LIGHT` - Light source

### lrg_spawn_point3d_get_entity_type / lrg_spawn_point3d_set_entity_type

```c
const gchar *lrg_spawn_point3d_get_entity_type(const LrgSpawnPoint3D *self);
void lrg_spawn_point3d_set_entity_type(LrgSpawnPoint3D *self, const gchar *entity_type);
```

Gets/sets the entity type name to spawn.

**Example:**
```c
g_autoptr(LrgSpawnPoint3D) spawn = lrg_spawn_point3d_new("orc_1", 50, 0, 50, LRG_SPAWN_TYPE_ENEMY);
lrg_spawn_point3d_set_entity_type(spawn, "orc_warrior");

const gchar *type = lrg_spawn_point3d_get_entity_type(spawn);
g_print("Will spawn: %s\n", type); /* Prints "orc_warrior" */
```

## Custom Properties

### lrg_spawn_point3d_set_property

```c
void lrg_spawn_point3d_set_property(LrgSpawnPoint3D *self, const gchar *key, const GValue *value);
```

Sets a custom property for entity configuration.

### lrg_spawn_point3d_get_property

```c
const GValue *lrg_spawn_point3d_get_property(const LrgSpawnPoint3D *self, const gchar *key);
```

Gets a custom property.

**Returns:** (transfer none) (nullable) Property value, or NULL

### lrg_spawn_point3d_has_property

```c
gboolean lrg_spawn_point3d_has_property(const LrgSpawnPoint3D *self, const gchar *key);
```

Checks if a property is set.

### lrg_spawn_point3d_get_property_keys

```c
GList *lrg_spawn_point3d_get_property_keys(const LrgSpawnPoint3D *self);
```

Gets all property keys.

**Returns:** (transfer container) (element-type utf8) List of keys

## Example Usage

```c
/* Create spawn point with properties */
g_autoptr(LrgSpawnPoint3D) spawn = lrg_spawn_point3d_new("boss_spawn", 100.0f, 20.0f, 100.0f, LRG_SPAWN_TYPE_ENEMY);
lrg_spawn_point3d_set_entity_type(spawn, "dragon_boss");

/* Set difficulty */
g_autoptr(GValue) difficulty = g_new0(GValue, 1);
g_value_init(difficulty, G_TYPE_INT);
g_value_set_int(difficulty, 3);
lrg_spawn_point3d_set_property(spawn, "difficulty", difficulty);

/* Set loot */
g_autoptr(GValue) loot = g_new0(GValue, 1);
g_value_init(loot, G_TYPE_STRING);
g_value_set_string(loot, "dragon_hoard");
lrg_spawn_point3d_set_property(spawn, "loot_table", loot);

/* Set rotation */
g_autoptr(GrlVector3) rot = g_new(GrlVector3, 1);
rot->x = 0; rot->y = 45; rot->z = 0;
lrg_spawn_point3d_set_rotation(spawn, rot);

/* Add to level */
lrg_level3d_add_spawn_point(level, spawn);

/* Later: retrieve and spawn */
const LrgSpawnPoint3D *sp = lrg_level3d_get_spawn_point(level, "boss_spawn");
if (sp) {
    const gchar *entity_type = lrg_spawn_point3d_get_entity_type(sp);
    g_autoptr(GrlVector3) pos = lrg_spawn_point3d_get_position(sp);
    const GValue *difficulty_val = lrg_spawn_point3d_get_property(sp, "difficulty");

    gint difficulty = g_value_get_int(difficulty_val);
    GObject *entity = spawn_entity(entity_type, pos, difficulty);
}
```

## Common Property Patterns

### Enemy Spawns

| Property | Type | Example |
|----------|------|---------|
| `difficulty` | int | 1-5 difficulty level |
| `loot_table` | string | "orc_drops", "treasure_chest" |
| `patrol_path` | string | "path_1", path identifier |
| `aggro_range` | float | 50.0 units |

### Item Spawns

| Property | Type | Example |
|----------|------|---------|
| `quantity` | int | 5 health potions |
| `respawn_time` | float | 30.0 seconds |
| `rarity` | string | "common", "rare", "legendary" |

### NPC Spawns

| Property | Type | Example |
|----------|------|---------|
| `dialog_tree` | string | "vendor_npc_1" |
| `faction` | string | "neutral", "friendly" |
| `quest` | string | quest ID |

## See Also

- [LrgLevel3D](level3d.md) - Contains spawn points
- [LrgTrigger3D](trigger3d.md) - May trigger spawning
