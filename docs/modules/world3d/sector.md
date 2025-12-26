# LrgSector

## Overview

`LrgSector` represents a convex region of space in a portal-based visibility system. Each sector contains references to portals that connect to other sectors.

## Type Information

- **Type Name**: `LrgSector`
- **Type ID**: `LRG_TYPE_SECTOR`
- **Type Category**: Boxed Type (immutable value type)
- **Base Type**: None (value type)

## Construction

### lrg_sector_new

```c
LrgSector *lrg_sector_new(const gchar *id, const LrgBoundingBox3D *bounds);
```

Creates a new sector.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(-100, -50, -100, 100, 50, 100);
g_autoptr(LrgSector) sector = lrg_sector_new("hallway_1", bounds);
```

### lrg_sector_new_box

```c
LrgSector *lrg_sector_new_box(const gchar *id, gfloat min_x, gfloat min_y, gfloat min_z,
                              gfloat max_x, gfloat max_y, gfloat max_z);
```

Creates a sector from box coordinates.

### lrg_sector_copy

```c
LrgSector *lrg_sector_copy(const LrgSector *self);
```

Creates a copy (nullable safe).

### lrg_sector_free

```c
void lrg_sector_free(LrgSector *self);
```

Frees a sector (nullable safe).

**Use with `g_autoptr(LrgSector)` for automatic cleanup.**

## Properties

### lrg_sector_get_id

```c
const gchar *lrg_sector_get_id(const LrgSector *self);
```

Gets the sector identifier.

### lrg_sector_get_bounds

```c
LrgBoundingBox3D *lrg_sector_get_bounds(const LrgSector *self);
```

Gets the sector bounds.

### lrg_sector_get_center

```c
GrlVector3 *lrg_sector_get_center(const LrgSector *self);
```

Gets the center point of the sector.

## Portal Connections

### lrg_sector_add_portal

```c
void lrg_sector_add_portal(LrgSector *self, const gchar *portal_id);
```

Adds a portal connection to this sector.

**Parameters:**
- `portal_id` - ID of the portal to connect

### lrg_sector_remove_portal

```c
gboolean lrg_sector_remove_portal(LrgSector *self, const gchar *portal_id);
```

Removes a portal connection.

**Returns:** `TRUE` if found and removed

### lrg_sector_has_portal

```c
gboolean lrg_sector_has_portal(const LrgSector *self, const gchar *portal_id);
```

Checks if this sector has a specific portal.

### lrg_sector_get_portal_ids

```c
GPtrArray *lrg_sector_get_portal_ids(const LrgSector *self);
```

Gets all connected portal IDs.

**Returns:** (transfer container) (element-type utf8) Portal ID list

### lrg_sector_get_portal_count

```c
guint lrg_sector_get_portal_count(const LrgSector *self);
```

Gets the number of connected portals.

**Example:**
```c
g_autoptr(LrgSector) sector = lrg_portal_system_get_sector(portal_sys, "room1");
if (sector) {
    guint portal_count = lrg_sector_get_portal_count(sector);
    g_print("Room has %u portals\n", portal_count);
}
```

## Visibility

### lrg_sector_is_visible

```c
gboolean lrg_sector_is_visible(const LrgSector *self);
```

Gets the visibility flag. Set by portal system during visibility determination.

### lrg_sector_set_visible

```c
void lrg_sector_set_visible(LrgSector *self, gboolean visible);
```

Sets the visibility flag (typically called by portal system).

## Geometric Tests

### lrg_sector_contains_point

```c
gboolean lrg_sector_contains_point(const LrgSector *self, const GrlVector3 *point);
gboolean lrg_sector_contains_point_xyz(const LrgSector *self, gfloat x, gfloat y, gfloat z);
```

Tests if a point is inside the sector bounds.

**Example:**
```c
g_autoptr(GrlVector3) player_pos = get_player_position();
const LrgSector *current = lrg_portal_system_find_sector_at(portal_sys, player_pos);
if (current && lrg_sector_contains_point(current, player_pos))
    g_print("Player confirmed in sector: %s\n", lrg_sector_get_id(current));
```

## Example Usage

```c
/* Create two sectors in a simple map */
g_autoptr(LrgBoundingBox3D) room1_bounds = lrg_bounding_box3d_new(-100, 0, -100, 0, 100, 100);
g_autoptr(LrgBoundingBox3D) room2_bounds = lrg_bounding_box3d_new(0, 0, -100, 100, 100, 100);

g_autoptr(LrgSector) room1 = lrg_sector_new("room1", room1_bounds);
g_autoptr(LrgSector) room2 = lrg_sector_new("room2", room2_bounds);

/* Connect with portal */
lrg_sector_add_portal(room1, "door_1_2");
lrg_sector_add_portal(room2, "door_1_2");

/* Add to portal system */
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();
lrg_portal_system_add_sector(portal_sys, room1);
lrg_portal_system_add_sector(portal_sys, room2);

/* Check portals */
g_print("Room1 has %u portals\n", lrg_sector_get_portal_count(room1));
g_autoptr(GPtrArray) portal_ids = lrg_sector_get_portal_ids(room1);
for (guint i = 0; i < portal_ids->len; i++) {
    const gchar *portal_id = g_ptr_array_index(portal_ids, i);
    g_print("  Portal: %s\n", portal_id);
}
```

## See Also

- [LrgPortalSystem](portal-system.md) - Uses sectors
- [LrgPortal](portal.md) - Portal connections
- [LrgBoundingBox3D](bounding-box3d.md) - Sector bounds
