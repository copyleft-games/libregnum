# LrgPortal

## Overview

`LrgPortal` represents an opening between two sectors in a portal-based visibility system. Portals define the connection points through which visibility can be traversed.

## Type Information

- **Type Name**: `LrgPortal`
- **Type ID**: `LRG_TYPE_PORTAL`
- **Type Category**: Boxed Type (immutable value type)

## Construction

### lrg_portal_new

```c
LrgPortal *lrg_portal_new(const gchar *id, const LrgBoundingBox3D *bounds,
                          const gchar *sector_a, const gchar *sector_b);
```

Creates a new portal connecting two sectors.

**Parameters:**
- `id` - Unique identifier
- `bounds` - (transfer none) Portal opening area
- `sector_a` - ID of first sector
- `sector_b` - ID of second sector

**Example:**
```c
g_autoptr(LrgBoundingBox3D) door = lrg_bounding_box3d_new(-2, -40, -10, 2, 40, 10);
g_autoptr(LrgPortal) portal = lrg_portal_new("door_1", door, "hallway", "bedroom");
```

### lrg_portal_new_with_normal

```c
LrgPortal *lrg_portal_new_with_normal(const gchar *id, const LrgBoundingBox3D *bounds,
                                      const gchar *sector_a, const gchar *sector_b,
                                      const GrlVector3 *normal);
```

Creates a portal with explicit normal direction for visibility culling.

### lrg_portal_copy

```c
LrgPortal *lrg_portal_copy(const LrgPortal *self);
```

Creates a copy (nullable safe).

### lrg_portal_free

```c
void lrg_portal_free(LrgPortal *self);
```

Frees a portal (nullable safe).

**Use with `g_autoptr(LrgPortal)` for automatic cleanup.**

## Properties

### lrg_portal_get_id

```c
const gchar *lrg_portal_get_id(const LrgPortal *self);
```

Gets the portal identifier.

### lrg_portal_get_bounds

```c
LrgBoundingBox3D *lrg_portal_get_bounds(const LrgPortal *self);
```

Gets the portal opening bounds.

### lrg_portal_get_center

```c
GrlVector3 *lrg_portal_get_center(const LrgPortal *self);
```

Gets the center point of the portal.

## Sector Connections

### lrg_portal_get_sector_a / lrg_portal_get_sector_b

```c
const gchar *lrg_portal_get_sector_a(const LrgPortal *self);
const gchar *lrg_portal_get_sector_b(const LrgPortal *self);
```

Gets the IDs of the two connected sectors.

### lrg_portal_get_other_sector

```c
const gchar *lrg_portal_get_other_sector(const LrgPortal *self, const gchar *from_sector);
```

Gets the sector on the other side of the portal.

**Parameters:**
- `from_sector` - The sector you're coming from

**Returns:** (transfer none) (nullable) Other sector ID, or NULL if not connected

**Example:**
```c
const gchar *next_sector = lrg_portal_get_other_sector(portal, "hallway");
g_print("Portal leads to: %s\n", next_sector); /* Prints "bedroom" */
```

### lrg_portal_connects_sector

```c
gboolean lrg_portal_connects_sector(const LrgPortal *self, const gchar *sector_id);
```

Checks if the portal connects to a given sector.

## Normal and Visibility

### lrg_portal_get_normal

```c
GrlVector3 *lrg_portal_get_normal(const LrgPortal *self);
```

Gets the portal normal (facing direction).

### lrg_portal_set_normal

```c
void lrg_portal_set_normal(LrgPortal *self, const GrlVector3 *normal);
```

Sets the portal normal direction.

### lrg_portal_is_visible_from

```c
gboolean lrg_portal_is_visible_from(const LrgPortal *self, const GrlVector3 *point);
```

Checks if the portal is visible from a point (faces toward it).

The portal's normal should point from sector_a toward sector_b.

**Example:**
```c
g_autoptr(GrlVector3) camera_pos = get_camera_position();
if (lrg_portal_is_visible_from(portal, camera_pos))
    g_print("Portal faces camera\n");
```

## Example Usage

```c
/* Create portal between two rooms */
g_autoptr(LrgBoundingBox3D) door_bounds = lrg_bounding_box3d_new(-1.5f, -30, -5, 1.5f, 30, 5);
g_autoptr(LrgPortal) door = lrg_portal_new("main_door", door_bounds, "lobby", "office");

/* Set normal pointing from lobby into office */
g_autoptr(GrlVector3) normal = g_new(GrlVector3, 1);
normal->x = 1; normal->y = 0; normal->z = 0;
lrg_portal_set_normal(door, normal);

/* Add to portal system */
g_autoptr(LrgPortalSystem) portal_sys = lrg_portal_system_new();
lrg_portal_system_add_portal(portal_sys, door);

/* Check connectivity */
if (lrg_portal_connects_sector(door, "lobby")) {
    g_print("Door connects to lobby\n");
    const gchar *other = lrg_portal_get_other_sector(door, "lobby");
    g_print("Other side: %s\n", other); /* Prints "office" */
}

/* Test visibility from a point */
g_autoptr(GrlVector3) test_point = g_new(GrlVector3, 1);
test_point->x = -10; test_point->y = 0; test_point->z = 0;
if (lrg_portal_is_visible_from(door, test_point))
    g_print("Door faces the test point\n");
```

## Portal Design Notes

1. **Bidirectional**: Portals connect two sectors bidirectionally
2. **Normal Convention**: Normal typically points from sector_a to sector_b
3. **Backface Culling**: Use normal to reject portals facing away from viewer
4. **Overlap**: Portal opening area should overlap with sector boundaries

## See Also

- [LrgPortalSystem](portal-system.md) - Portal container
- [LrgSector](sector.md) - Connected sectors
- [LrgBoundingBox3D](bounding-box3d.md) - Portal bounds
