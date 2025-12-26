# LrgPortalSystem

## Overview

`LrgPortalSystem` implements portal-based occlusion culling for 3D levels. It organizes space into convex sectors connected by portals, and determines visibility by traversing portals from the camera's current sector. This is a classic technique used in games like Quake and Half-Life for efficient visibility determination.

## Type Information

- **Type Name**: `LrgPortalSystem`
- **Type ID**: `LRG_TYPE_PORTAL_SYSTEM`
- **Base Class**: `GObject`
- **Final Type**: Yes (cannot be subclassed)

## Construction

### lrg_portal_system_new

```c
LrgPortalSystem *lrg_portal_system_new(void);
```

Creates a new portal system.

**Returns:** (transfer full) A new `LrgPortalSystem`

**Example:**
```c
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();
```

## Sector Management

### lrg_portal_system_add_sector

```c
void lrg_portal_system_add_sector(LrgPortalSystem *self, const LrgSector *sector);
```

Adds a sector to the system.

**Parameters:**
- `sector` - (transfer none) Sector to add

### lrg_portal_system_remove_sector

```c
gboolean lrg_portal_system_remove_sector(LrgPortalSystem *self, const gchar *id);
```

Removes a sector by ID.

**Parameters:**
- `id` - Sector ID to remove

**Returns:** `TRUE` if found and removed

### lrg_portal_system_get_sector

```c
const LrgSector *lrg_portal_system_get_sector(LrgPortalSystem *self, const gchar *id);
```

Gets a sector by ID.

**Parameters:**
- `id` - Sector ID

**Returns:** (transfer none) (nullable) The sector, or NULL

### lrg_portal_system_get_sectors

```c
GPtrArray *lrg_portal_system_get_sectors(LrgPortalSystem *self);
```

Gets all sectors.

**Returns:** (transfer container) (element-type LrgSector) Array of sectors

### lrg_portal_system_get_sector_count

```c
guint lrg_portal_system_get_sector_count(LrgPortalSystem *self);
```

Gets the number of sectors.

**Returns:** Sector count

### lrg_portal_system_find_sector_at

```c
const LrgSector *lrg_portal_system_find_sector_at(LrgPortalSystem *self, const GrlVector3 *point);
```

Finds the sector containing a point.

**Parameters:**
- `point` - (transfer none) Point to test

**Returns:** (transfer none) (nullable) The containing sector, or NULL

**Example:**
```c
g_autoptr(GrlVector3) player_pos = /* get position */;
const LrgSector *current_sector = lrg_portal_system_find_sector_at(portal_sys, player_pos);
if (current_sector)
    g_print("Player in sector: %s\n", lrg_sector_get_id(current_sector));
```

## Portal Management

### lrg_portal_system_add_portal

```c
void lrg_portal_system_add_portal(LrgPortalSystem *self, const LrgPortal *portal);
```

Adds a portal to the system.

**Parameters:**
- `portal` - (transfer none) Portal to add

### lrg_portal_system_remove_portal

```c
gboolean lrg_portal_system_remove_portal(LrgPortalSystem *self, const gchar *id);
```

Removes a portal by ID.

**Parameters:**
- `id` - Portal ID to remove

**Returns:** `TRUE` if found and removed

### lrg_portal_system_get_portal

```c
const LrgPortal *lrg_portal_system_get_portal(LrgPortalSystem *self, const gchar *id);
```

Gets a portal by ID.

**Parameters:**
- `id` - Portal ID

**Returns:** (transfer none) (nullable) The portal, or NULL

### lrg_portal_system_get_portals

```c
GPtrArray *lrg_portal_system_get_portals(LrgPortalSystem *self);
```

Gets all portals.

**Returns:** (transfer container) (element-type LrgPortal) Array of portals

### lrg_portal_system_get_portal_count

```c
guint lrg_portal_system_get_portal_count(LrgPortalSystem *self);
```

Gets the number of portals.

**Returns:** Portal count

### lrg_portal_system_get_sector_portals

```c
GPtrArray *lrg_portal_system_get_sector_portals(LrgPortalSystem *self, const gchar *sector_id);
```

Gets all portals connected to a sector.

**Parameters:**
- `sector_id` - Sector ID

**Returns:** (transfer container) (element-type LrgPortal) Array of connected portals

## Visibility Determination

### lrg_portal_system_update

```c
void lrg_portal_system_update(LrgPortalSystem *self, const GrlVector3 *camera_pos);
```

Updates sector visibility based on camera position.

This is the core function that computes which sectors are visible. It:
1. Finds the sector containing the camera
2. Marks it as visible
3. Traverses portals to mark connected sectors as visible
4. Continues until max depth is reached

**Parameters:**
- `camera_pos` - (transfer none) Camera position

**Call Frequency:** Once per frame, before rendering

**Example:**
```c
/* In game loop */
g_autoptr(GrlVector3) camera_pos = /* get camera position */;
lrg_portal_system_update(portal_sys, camera_pos);

g_autoptr(GPtrArray) visible = lrg_portal_system_get_visible_sectors(portal_sys);
for (guint i = 0; i < visible->len; i++) {
    LrgSector *sector = g_ptr_array_index(visible, i);
    g_print("Render sector: %s\n", lrg_sector_get_id(sector));
}
```

### lrg_portal_system_get_visible_sectors

```c
GPtrArray *lrg_portal_system_get_visible_sectors(LrgPortalSystem *self);
```

Gets all currently visible sectors.

Must call `lrg_portal_system_update()` first.

**Returns:** (transfer container) (element-type LrgSector) Array of visible sectors

### lrg_portal_system_get_visible_sector_count

```c
guint lrg_portal_system_get_visible_sector_count(LrgPortalSystem *self);
```

Gets the number of currently visible sectors.

**Returns:** Visible sector count

### lrg_portal_system_is_sector_visible

```c
gboolean lrg_portal_system_is_sector_visible(LrgPortalSystem *self, const gchar *id);
```

Checks if a sector is currently visible.

**Parameters:**
- `id` - Sector ID to check

**Returns:** `TRUE` if sector is visible

### lrg_portal_system_get_current_sector

```c
const gchar *lrg_portal_system_get_current_sector(LrgPortalSystem *self);
```

Gets the sector the camera is currently in.

**Returns:** (transfer none) (nullable) Current sector ID

## Configuration

### lrg_portal_system_get_max_portal_depth

```c
guint lrg_portal_system_get_max_portal_depth(LrgPortalSystem *self);
```

Gets the maximum portal traversal depth.

Default is 8. Higher values explore further through portals, lower values are faster.

**Returns:** Maximum depth

### lrg_portal_system_set_max_portal_depth

```c
void lrg_portal_system_set_max_portal_depth(LrgPortalSystem *self, guint max_depth);
```

Sets the maximum portal traversal depth.

**Parameters:**
- `max_depth` - Maximum traversal depth

**Performance Tip:** Set to 2-4 for outdoor levels, 6-8 for indoor, 1-2 for performance-critical scenarios.

## Utility

### lrg_portal_system_clear

```c
void lrg_portal_system_clear(LrgPortalSystem *self);
```

Removes all sectors and portals.

## Complete Example

```c
/* Create system and add sectors */
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();
lrg_portal_system_set_max_portal_depth(portal_sys, 4);

/* Create three sectors */
g_autoptr(LrgBoundingBox3D) bounds1 = lrg_bounding_box3d_new(-100, -50, -100, -1, 50, 100);
g_autoptr(LrgBoundingBox3D) bounds2 = lrg_bounding_box3d_new(-1, -50, -100, 100, 50, 100);
g_autoptr(LrgBoundingBox3D) bounds3 = lrg_bounding_box3d_new(100, -50, -100, 200, 50, 100);

LrgSector *sec1 = lrg_sector_new("room1", bounds1);
LrgSector *sec2 = lrg_sector_new("hallway", bounds2);
LrgSector *sec3 = lrg_sector_new("room2", bounds3);

lrg_portal_system_add_sector(portal_sys, sec1);
lrg_portal_system_add_sector(portal_sys, sec2);
lrg_portal_system_add_sector(portal_sys, sec3);

lrg_sector_free(sec1);
lrg_sector_free(sec2);
lrg_sector_free(sec3);

/* Create portals between sectors */
g_autoptr(LrgBoundingBox3D) portal1_bounds = lrg_bounding_box3d_new(-2, -40, -50, 2, 40, 50);
LrgPortal *portal1 = lrg_portal_new("door1", portal1_bounds, "room1", "hallway");
lrg_portal_system_add_portal(portal_sys, portal1);
lrg_portal_free(portal1);

g_autoptr(LrgBoundingBox3D) portal2_bounds = lrg_bounding_box3d_new(98, -40, -50, 102, 40, 50);
LrgPortal *portal2 = lrg_portal_new("door2", portal2_bounds, "hallway", "room2");
lrg_portal_system_add_portal(portal_sys, portal2);
lrg_portal_free(portal2);

/* Update visibility from camera position */
g_autoptr(GrlVector3) camera = g_new(GrlVector3, 1);
camera->x = -50; camera->y = 0; camera->z = 0;
lrg_portal_system_update(portal_sys, camera);

/* Render only visible sectors */
g_autoptr(GPtrArray) visible = lrg_portal_system_get_visible_sectors(portal_sys);
g_print("Visible sectors: %u\n", visible->len);
for (guint i = 0; i < visible->len; i++) {
    LrgSector *sector = g_ptr_array_index(visible, i);
    g_print("  - %s\n", lrg_sector_get_id(sector));
}
```

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Add sector | O(1) | Constant time |
| Add portal | O(1) | Constant time |
| Update visibility | O(S * P) | S = sectors, P = max portal depth |
| Find sector at point | O(S) | Linear search (consider spatial index) |

## Common Patterns

### Game Loop Integration

```c
/* Initialization */
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();
/* ... add sectors and portals ... */

/* Main game loop */
while (game_running) {
    /* Update camera position */
    g_autoptr(GrlVector3) camera_pos = get_camera_position();

    /* Update visibility */
    lrg_portal_system_update(portal_sys, camera_pos);

    /* Render only visible sectors */
    g_autoptr(GPtrArray) visible = lrg_portal_system_get_visible_sectors(portal_sys);
    for (guint i = 0; i < visible->len; i++) {
        LrgSector *sector = g_ptr_array_index(visible, i);
        render_sector_contents(sector);
    }
}
```

### Debug Visualization

```c
/* Draw visible sectors in debug view */
g_autoptr(GPtrArray) visible = lrg_portal_system_get_visible_sectors(portal_sys);
for (guint i = 0; i < visible->len; i++) {
    LrgSector *sector = g_ptr_array_index(visible, i);
    g_autoptr(LrgBoundingBox3D) bounds = lrg_sector_get_bounds(sector);
    draw_wireframe_box(bounds);  /* Show sector bounds */
}

g_autoptr(GPtrArray) portals = lrg_portal_system_get_portals(portal_sys);
for (guint i = 0; i < portals->len; i++) {
    LrgPortal *portal = g_ptr_array_index(portals, i);
    g_autoptr(LrgBoundingBox3D) bounds = lrg_portal_get_bounds(portal);
    draw_portal_quad(bounds);  /* Show portal openings */
}
```

## Advantages and Limitations

**Advantages:**
- Extremely efficient visibility determination
- Low memory overhead compared to other culling methods
- Works well for indoor/structured environments

**Limitations:**
- Requires manual level design (sectors and portals)
- Cannot handle dynamic sector geometry
- Inefficient for outdoor/open environments
- Does not handle view frustum clipping

## See Also

- [LrgSector](sector.md) - Sector type
- [LrgPortal](portal.md) - Portal type
- [LrgLevel3D](level3d.md) - Level container
