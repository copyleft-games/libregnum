# LrgLevel3D

## Overview

`LrgLevel3D` is the main container for 3D game levels. It manages all spatial data including models, spawn points, triggers, and provides efficient spatial queries through an internal octree. It also supports custom properties for game-specific data.

## Type Information

- **Type Name**: `LrgLevel3D`
- **Type ID**: `LRG_TYPE_LEVEL3D`
- **Base Class**: `GObject`
- **Final Type**: Yes (cannot be subclassed)

## Construction

### lrg_level3d_new

```c
LrgLevel3D *lrg_level3d_new(const gchar *id);
```

Creates a new 3D level.

**Parameters:**
- `id` - Unique identifier for this level

**Returns:** (transfer full) A new `LrgLevel3D`

**Example:**
```c
g_autoptr(LrgLevel3D) level = lrg_level3d_new("forest_level");
```

## Properties

### lrg_level3d_get_id / lrg_level3d_set_id

```c
const gchar *lrg_level3d_get_id(LrgLevel3D *self);
```

Gets the level's unique identifier.

**Returns:** (transfer none) The level ID

### lrg_level3d_get_name / lrg_level3d_set_name

```c
const gchar *lrg_level3d_get_name(LrgLevel3D *self);
void lrg_level3d_set_name(LrgLevel3D *self, const gchar *name);
```

Gets/sets the display name.

**Parameters:**
- `name` - (nullable) Display name

### lrg_level3d_get_bounds / lrg_level3d_set_bounds

```c
LrgBoundingBox3D *lrg_level3d_get_bounds(LrgLevel3D *self);
void lrg_level3d_set_bounds(LrgLevel3D *self, const LrgBoundingBox3D *bounds);
```

Gets/sets the level bounds. Setting bounds reinitializes the internal octree.

**Parameters:**
- `bounds` - (transfer none) New level bounds

**Note:** Changing bounds rebuilds the octree and may be expensive for large levels.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(-1000, -1000, -1000, 1000, 1000, 1000);
lrg_level3d_set_bounds(level, bounds);
```

## Spawn Point Management

### lrg_level3d_add_spawn_point

```c
void lrg_level3d_add_spawn_point(LrgLevel3D *self, const LrgSpawnPoint3D *spawn);
```

Adds a spawn point to the level.

**Parameters:**
- `spawn` - (transfer none) Spawn point to add

### lrg_level3d_remove_spawn_point

```c
gboolean lrg_level3d_remove_spawn_point(LrgLevel3D *self, const gchar *id);
```

Removes a spawn point by ID.

**Returns:** `TRUE` if found and removed

### lrg_level3d_get_spawn_point

```c
const LrgSpawnPoint3D *lrg_level3d_get_spawn_point(LrgLevel3D *self, const gchar *id);
```

Gets a spawn point by ID.

**Returns:** (transfer none) (nullable) The spawn point, or NULL

### lrg_level3d_get_spawn_points

```c
GPtrArray *lrg_level3d_get_spawn_points(LrgLevel3D *self);
```

Gets all spawn points.

**Returns:** (transfer container) (element-type LrgSpawnPoint3D) Array of spawn points

### lrg_level3d_get_spawn_points_by_type

```c
GPtrArray *lrg_level3d_get_spawn_points_by_type(LrgLevel3D *self, LrgSpawnType spawn_type);
```

Gets spawn points of a specific type.

**Parameters:**
- `spawn_type` - Type of spawn points (PLAYER, ENEMY, ITEM, NPC, etc.)

**Returns:** (transfer container) (element-type LrgSpawnPoint3D) Matching spawn points

**Example:**
```c
g_autoptr(GPtrArray) player_spawns = lrg_level3d_get_spawn_points_by_type(level, LRG_SPAWN_TYPE_PLAYER);
if (player_spawns->len > 0) {
    LrgSpawnPoint3D *spawn = g_ptr_array_index(player_spawns, 0);
    g_autoptr(GrlVector3) pos = lrg_spawn_point3d_get_position(spawn);
    /* Spawn player at position */
}
```

### lrg_level3d_get_spawn_point_count

```c
guint lrg_level3d_get_spawn_point_count(LrgLevel3D *self);
```

Gets the number of spawn points.

**Returns:** Spawn point count

## Trigger Management

### lrg_level3d_add_trigger

```c
void lrg_level3d_add_trigger(LrgLevel3D *self, const LrgTrigger3D *trigger);
```

Adds a trigger to the level.

**Parameters:**
- `trigger` - (transfer none) Trigger to add

### lrg_level3d_remove_trigger

```c
gboolean lrg_level3d_remove_trigger(LrgLevel3D *self, const gchar *id);
```

Removes a trigger by ID.

**Returns:** `TRUE` if found and removed

### lrg_level3d_get_trigger

```c
const LrgTrigger3D *lrg_level3d_get_trigger(LrgLevel3D *self, const gchar *id);
```

Gets a trigger by ID.

**Returns:** (transfer none) (nullable) The trigger, or NULL

### lrg_level3d_get_triggers

```c
GPtrArray *lrg_level3d_get_triggers(LrgLevel3D *self);
```

Gets all triggers.

**Returns:** (transfer container) (element-type LrgTrigger3D) Array of triggers

### lrg_level3d_get_trigger_count

```c
guint lrg_level3d_get_trigger_count(LrgLevel3D *self);
```

Gets the number of triggers.

**Returns:** Trigger count

### lrg_level3d_check_triggers

```c
GPtrArray *lrg_level3d_check_triggers(LrgLevel3D *self, const GrlVector3 *point);
```

Finds all enabled triggers containing a point.

**Parameters:**
- `point` - (transfer none) Point to test

**Returns:** (transfer container) (element-type LrgTrigger3D) Activated triggers

**Example:**
```c
g_autoptr(GrlVector3) entity_pos = /* get position */;
g_autoptr(GPtrArray) active = lrg_level3d_check_triggers(level, entity_pos);

for (guint i = 0; i < active->len; i++) {
    LrgTrigger3D *trigger = g_ptr_array_index(active, i);
    const gchar *target = lrg_trigger3d_get_target_id(trigger);
    g_print("Trigger activated: %s\n", target);
}
```

## Model Management

### lrg_level3d_add_model

```c
void lrg_level3d_add_model(LrgLevel3D *self, GrlModel *model, const LrgBoundingBox3D *bounds);
```

Adds a 3D model to the level.

**Parameters:**
- `model` - (transfer none) Model from graylib
- `bounds` - (transfer none) Bounding box for spatial indexing

### lrg_level3d_remove_model

```c
gboolean lrg_level3d_remove_model(LrgLevel3D *self, GrlModel *model);
```

Removes a model from the level.

**Returns:** `TRUE` if found and removed

### lrg_level3d_get_models

```c
GPtrArray *lrg_level3d_get_models(LrgLevel3D *self);
```

Gets all models.

**Returns:** (transfer container) (element-type GrlModel) Array of models

### lrg_level3d_get_model_count

```c
guint lrg_level3d_get_model_count(LrgLevel3D *self);
```

Gets the number of models.

**Returns:** Model count

## Spatial Queries

### lrg_level3d_query_box

```c
GPtrArray *lrg_level3d_query_box(LrgLevel3D *self, const LrgBoundingBox3D *box);
```

Finds all objects intersecting a box.

**Parameters:**
- `box` - (transfer none) Query bounding box

**Returns:** (transfer container) (element-type gpointer) Array of objects

**Example:**
```c
g_autoptr(LrgBoundingBox3D) region = lrg_bounding_box3d_new(-50, -50, -50, 50, 50, 50);
g_autoptr(GPtrArray) results = lrg_level3d_query_box(level, region);
```

### lrg_level3d_query_sphere

```c
GPtrArray *lrg_level3d_query_sphere(LrgLevel3D *self, const GrlVector3 *center, gfloat radius);
```

Finds all objects intersecting a sphere.

**Parameters:**
- `center` - (transfer none) Sphere center
- `radius` - Sphere radius

**Returns:** (transfer container) (element-type gpointer) Array of objects

**Performance Note:** Sphere queries are typically faster than box queries.

## Custom Properties

### lrg_level3d_set_property_value

```c
void lrg_level3d_set_property_value(LrgLevel3D *self, const gchar *key, const GValue *value);
```

Sets a custom property.

**Parameters:**
- `key` - Property key
- `value` - (transfer none) Property value (any GType)

### lrg_level3d_get_property_value

```c
const GValue *lrg_level3d_get_property_value(LrgLevel3D *self, const gchar *key);
```

Gets a custom property.

**Returns:** (transfer none) (nullable) Property value, or NULL if not found

### lrg_level3d_has_property

```c
gboolean lrg_level3d_has_property(LrgLevel3D *self, const gchar *key);
```

Checks if a property is set.

**Returns:** `TRUE` if property exists

### lrg_level3d_get_property_keys

```c
GList *lrg_level3d_get_property_keys(LrgLevel3D *self);
```

Gets all property keys.

**Returns:** (transfer container) (element-type utf8) List of property keys

**Example:**
```c
g_autoptr(GValue) ambient = g_new0(GValue, 1);
g_value_init(ambient, G_TYPE_FLOAT);
g_value_set_float(ambient, 0.5f);
lrg_level3d_set_property_value(level, "ambient_light", ambient);

const GValue *retrieved = lrg_level3d_get_property_value(level, "ambient_light");
if (retrieved)
    g_print("Ambient: %f\n", g_value_get_float(retrieved));
```

## Octree Access

### lrg_level3d_get_octree

```c
LrgOctree *lrg_level3d_get_octree(LrgLevel3D *self);
```

Gets the internal octree for advanced queries.

**Returns:** (transfer none) The octree

### lrg_level3d_rebuild_octree

```c
void lrg_level3d_rebuild_octree(LrgLevel3D *self);
```

Rebuilds the octree structure. Call after adding many models.

**Performance Note:** Rebuilding is expensive but optimizes future queries.

## Utility

### lrg_level3d_clear

```c
void lrg_level3d_clear(LrgLevel3D *self);
```

Removes all content from the level (models, spawn points, triggers).

## Common Use Cases

### Loading Level Data

```c
g_autoptr(LrgLevel3D) level = lrg_level3d_new("castle");
lrg_level3d_set_name(level, "Castle Dungeon");

g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(-1000, -500, -1000, 1000, 500, 1000);
lrg_level3d_set_bounds(level, bounds);

/* Load spawn points and triggers from data file */
/* ... */

lrg_level3d_rebuild_octree(level);
```

### Game Loop Queries

```c
/* Get nearby entities */
g_autoptr(GrlVector3) player_pos = /* get player position */;
g_autoptr(GPtrArray) nearby = lrg_level3d_query_sphere(level, player_pos, 100.0f);

/* Check trigger activation */
g_autoptr(GPtrArray) active_triggers = lrg_level3d_check_triggers(level, player_pos);

/* Render visible models (would use portal system for actual culling) */
g_autoptr(GPtrArray) models = lrg_level3d_get_models(level);
```

## See Also

- [LrgBoundingBox3D](bounding-box3d.md) - Bounding box type
- [LrgOctree](octree.md) - Spatial partitioning
- [LrgSpawnPoint3D](spawn-point3d.md) - Spawn point type
- [LrgTrigger3D](trigger3d.md) - Trigger volume type
