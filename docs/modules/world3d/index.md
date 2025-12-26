# World3D Module

## Overview

The World3D module provides comprehensive 3D level management with spatial optimization, portal-based visibility culling, and interactive elements (spawn points and triggers). It combines octree spatial indexing with portal systems for efficient rendering and gameplay in large 3D environments.

## Key Components

### Core Types

- **LrgLevel3D** - Container for all 3D level data with spatial organization
- **LrgOctree** - Spatial partitioning data structure for efficient queries
- **LrgPortalSystem** - Portal-based occlusion culling and visibility determination
- **LrgSector** (boxed) - Convex regions of space in portal systems
- **LrgPortal** (boxed) - Openings between sectors
- **LrgBoundingBox3D** (boxed) - Axis-aligned bounding box for geometry
- **LrgSpawnPoint3D** (boxed) - Entity spawn locations
- **LrgTrigger3D** (boxed) - Interactive volume triggers

## Architecture

### Spatial Indexing

The World3D module uses an **octree** data structure to partition 3D space efficiently:

- Automatically subdivides space based on object density
- Configurable maximum depth and objects-per-node
- Supports point, box, and sphere spatial queries
- Updates on-the-fly as objects move

### Visibility Culling

The **portal system** determines which sectors are visible from the camera position:

- Sectors define convex volumes connected by portals
- Portal traversal starting from camera sector determines visibility
- Configurable maximum traversal depth for performance
- Reduces rendering to visible sectors only

### Interactive Elements

**Spawn points** define where entities appear; **triggers** define interactive volumes that fire events.

## Quick Start

### Creating a Level

```c
g_autoptr(LrgLevel3D) level = lrg_level3d_new("my_level");
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(-100, -100, -100, 100, 100, 100);
lrg_level3d_set_bounds(level, bounds);
```

### Adding Content

```c
/* Add a spawn point for player */
LrgSpawnPoint3D *spawn = lrg_spawn_point3d_new("player_start", 0.0f, 0.0f, 0.0f, LRG_SPAWN_TYPE_PLAYER);
lrg_level3d_add_spawn_point(level, spawn);
lrg_spawn_point3d_free(spawn);

/* Add a trigger volume */
g_autoptr(LrgBoundingBox3D) trigger_bounds = lrg_bounding_box3d_new(10, 10, 10, 20, 20, 20);
LrgTrigger3D *trigger = lrg_trigger3d_new("exit_trigger", trigger_bounds, LRG_TRIGGER_TYPE_VOLUME);
lrg_trigger3d_set_enabled(trigger, TRUE);
lrg_level3d_add_trigger(level, trigger);
lrg_trigger3d_free(trigger);
```

### Spatial Queries

```c
/* Find all objects in a box */
g_autoptr(LrgBoundingBox3D) query_box = lrg_bounding_box3d_new(-10, -10, -10, 10, 10, 10);
g_autoptr(GPtrArray) results = lrg_level3d_query_box(level, query_box);

/* Find objects near a point */
g_autoptr(GrlVector3) center = g_new(GrlVector3, 1);
center->x = 0; center->y = 0; center->z = 0;
g_autoptr(GPtrArray) nearby = lrg_level3d_query_sphere(level, center, 50.0f);

/* Find triggers at a point */
g_autoptr(GPtrArray) active_triggers = lrg_level3d_check_triggers(level, center);
```

### Portal System

```c
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();

/* Create sectors */
g_autoptr(LrgBoundingBox3D) sector1_bounds = lrg_bounding_box3d_new(-100, -100, -100, 0, 100, 100);
LrgSector *sector1 = lrg_sector_new("room1", sector1_bounds);
lrg_portal_system_add_sector(portal_sys, sector1);
lrg_sector_free(sector1);

/* Create portal between sectors */
g_autoptr(LrgBoundingBox3D) portal_bounds = lrg_bounding_box3d_new(-1, -10, -10, 1, 10, 10);
LrgPortal *portal = lrg_portal_new("door1", portal_bounds, "room1", "room2");
lrg_portal_system_add_portal(portal_sys, portal);
lrg_portal_free(portal);

/* Update visibility from camera position */
g_autoptr(GrlVector3) camera_pos = g_new(GrlVector3, 1);
camera_pos->x = -50; camera_pos->y = 0; camera_pos->z = 0;
lrg_portal_system_update(portal_sys, camera_pos);

/* Get visible sectors */
g_autoptr(GPtrArray) visible = lrg_portal_system_get_visible_sectors(portal_sys);
```

## Module Documentation

- [LrgLevel3D](level3d.md) - 3D level container with spatial indexing
- [LrgPortalSystem](portal-system.md) - Portal-based visibility system
- [LrgOctree](octree.md) - Octree spatial data structure
- [LrgSector](sector.md) - Portal system sectors (boxed type)
- [LrgBoundingBox3D](bounding-box3d.md) - AABB geometry (boxed type)
- [LrgPortal](portal.md) - Portal definitions (boxed type)
- [LrgSpawnPoint3D](spawn-point3d.md) - Entity spawn locations (boxed type)
- [LrgTrigger3D](trigger3d.md) - Interactive trigger volumes (boxed type)

## Common Patterns

### Custom Level Properties

Levels support arbitrary key-value properties for game-specific data:

```c
g_autoptr(GValue) weather = g_new0(GValue, 1);
g_value_init(weather, G_TYPE_STRING);
g_value_set_string(weather, "rainy");
lrg_level3d_set_property_value(level, "weather", weather);

const GValue *retrieved = lrg_level3d_get_property_value(level, "weather");
if (retrieved)
    g_print("Weather: %s\n", g_value_get_string(retrieved));
```

### Spawn Point Properties

Spawn points can carry custom properties for entity configuration:

```c
g_autoptr(GValue) difficulty = g_new0(GValue, 1);
g_value_init(difficulty, G_TYPE_INT);
g_value_set_int(difficulty, 5);
lrg_spawn_point3d_set_property(spawn, "difficulty_level", difficulty);
```

### Trigger Activation

Check for trigger activation in game loop:

```c
g_autoptr(GrlVector3) entity_pos = /* get entity position */;
g_autoptr(GPtrArray) triggers = lrg_level3d_check_triggers(level, entity_pos);

for (guint i = 0; i < triggers->len; i++) {
    LrgTrigger3D *trigger = g_ptr_array_index(triggers, i);
    const gchar *target = lrg_trigger3d_get_target_id(trigger);
    /* Handle trigger activation for target */
}
```

### Octree Optimization

Rebuild octree after many model changes for optimal performance:

```c
/* Add many models */
for (guint i = 0; i < 1000; i++) {
    g_autoptr(LrgBoundingBox3D) model_bounds = /* ... */;
    lrg_level3d_add_model(level, model, model_bounds);
}

/* Rebuild structure */
lrg_level3d_rebuild_octree(level);
```

## Performance Considerations

1. **Octree Depth** - Default is 8 levels; increase for larger worlds, decrease for smaller
2. **Objects Per Node** - Default triggers subdivision at 16 objects; adjust based on query patterns
3. **Portal Depth** - Max portal traversal default is 8; decrease if visibility calculation is slow
4. **Spatial Queries** - Use sphere queries for performance-critical operations (faster than box)

## Type Hierarchy

```
GObject
  LrgLevel3D          (final)
  LrgOctree           (final)
  LrgPortalSystem     (final)

Boxed Types
  LrgBoundingBox3D    (immutable value type)
  LrgSpawnPoint3D     (immutable value type)
  LrgTrigger3D        (immutable value type)
  LrgSector           (immutable value type)
  LrgPortal           (immutable value type)
```

## See Also

- [Glossary](../../reference/glossary.md) - Terminology and concepts
- [Signals](../../reference/signals.md) - Level and system signals
- [Error Codes](../../reference/error-codes.md) - World3D error domains
