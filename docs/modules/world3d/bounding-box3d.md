# LrgBoundingBox3D

## Overview

`LrgBoundingBox3D` is an axis-aligned bounding box (AABB) value type used throughout the World3D module for spatial queries, collision, and geometry representation.

## Type Information

- **Type Name**: `LrgBoundingBox3D`
- **Type ID**: `LRG_TYPE_BOUNDING_BOX3D`
- **Type Category**: Boxed Type (immutable value type)
- **Instance Size**: Contains two `GrlVector3` (min, max)

## Structure

```c
struct _LrgBoundingBox3D {
    GrlVector3 min;  /* Minimum corner (lower-left-back) */
    GrlVector3 max;  /* Maximum corner (upper-right-front) */
};
```

## Construction

### lrg_bounding_box3d_new

```c
LrgBoundingBox3D *lrg_bounding_box3d_new(gfloat min_x, gfloat min_y, gfloat min_z,
                                         gfloat max_x, gfloat max_y, gfloat max_z);
```

Creates a bounding box from individual coordinates.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) box = lrg_bounding_box3d_new(-10, -10, -10, 10, 10, 10);
```

### lrg_bounding_box3d_new_from_vectors

```c
LrgBoundingBox3D *lrg_bounding_box3d_new_from_vectors(const GrlVector3 *min, const GrlVector3 *max);
```

Creates a bounding box from two vectors.

### lrg_bounding_box3d_new_from_center

```c
LrgBoundingBox3D *lrg_bounding_box3d_new_from_center(const GrlVector3 *center, gfloat half_size);
```

Creates a bounding box centered at a point with uniform half-size.

**Example:**
```c
g_autoptr(GrlVector3) center = g_new(GrlVector3, 1);
center->x = 0; center->y = 0; center->z = 0;
g_autoptr(LrgBoundingBox3D) box = lrg_bounding_box3d_new_from_center(center, 50.0f);
/* Creates box from (-50,-50,-50) to (50,50,50) */
```

### lrg_bounding_box3d_copy

```c
LrgBoundingBox3D *lrg_bounding_box3d_copy(const LrgBoundingBox3D *self);
```

Creates a copy (nullable safe).

### lrg_bounding_box3d_free

```c
void lrg_bounding_box3d_free(LrgBoundingBox3D *self);
```

Frees a bounding box (nullable safe).

**Note:** Use `g_autoptr(LrgBoundingBox3D)` for automatic cleanup.

## Properties

### lrg_bounding_box3d_get_min / get_max

```c
GrlVector3 *lrg_bounding_box3d_get_min(const LrgBoundingBox3D *self);
GrlVector3 *lrg_bounding_box3d_get_max(const LrgBoundingBox3D *self);
```

Gets minimum/maximum corners.

### lrg_bounding_box3d_get_center

```c
GrlVector3 *lrg_bounding_box3d_get_center(const LrgBoundingBox3D *self);
```

Gets the center point.

### lrg_bounding_box3d_get_size

```c
GrlVector3 *lrg_bounding_box3d_get_size(const LrgBoundingBox3D *self);
```

Gets dimensions (width, height, depth).

### lrg_bounding_box3d_get_volume

```c
gfloat lrg_bounding_box3d_get_volume(const LrgBoundingBox3D *self);
```

Calculates volume.

### lrg_bounding_box3d_get_surface_area

```c
gfloat lrg_bounding_box3d_get_surface_area(const LrgBoundingBox3D *self);
```

Calculates surface area.

## Geometric Tests

### lrg_bounding_box3d_contains_point

```c
gboolean lrg_bounding_box3d_contains_point(const LrgBoundingBox3D *self, const GrlVector3 *point);
gboolean lrg_bounding_box3d_contains_point_xyz(const LrgBoundingBox3D *self, gfloat x, gfloat y, gfloat z);
```

Tests if a point is inside the box.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) box = lrg_bounding_box3d_new(-10, -10, -10, 10, 10, 10);
g_autoptr(GrlVector3) point = g_new(GrlVector3, 1);
point->x = 0; point->y = 0; point->z = 0;

if (lrg_bounding_box3d_contains_point(box, point))
    g_print("Point is inside\n");
```

### lrg_bounding_box3d_intersects

```c
gboolean lrg_bounding_box3d_intersects(const LrgBoundingBox3D *self, const LrgBoundingBox3D *other);
```

Tests if two boxes intersect (overlap).

### lrg_bounding_box3d_contains

```c
gboolean lrg_bounding_box3d_contains(const LrgBoundingBox3D *self, const LrgBoundingBox3D *other);
```

Tests if this box fully contains another.

## Transformations

### lrg_bounding_box3d_expand

```c
LrgBoundingBox3D *lrg_bounding_box3d_expand(const LrgBoundingBox3D *self, gfloat amount);
```

Creates a new expanded box (uniform expansion in all directions).

**Example:**
```c
g_autoptr(LrgBoundingBox3D) box = lrg_bounding_box3d_new(-10, -10, -10, 10, 10, 10);
g_autoptr(LrgBoundingBox3D) expanded = lrg_bounding_box3d_expand(box, 5.0f);
/* Result: (-15,-15,-15) to (15,15,15) */
```

### lrg_bounding_box3d_merge

```c
LrgBoundingBox3D *lrg_bounding_box3d_merge(const LrgBoundingBox3D *self, const LrgBoundingBox3D *other);
```

Creates a box containing both input boxes.

**Example:**
```c
g_autoptr(LrgBoundingBox3D) box1 = lrg_bounding_box3d_new(-10, -10, -10, 0, 0, 0);
g_autoptr(LrgBoundingBox3D) box2 = lrg_bounding_box3d_new(0, 0, 0, 10, 10, 10);
g_autoptr(LrgBoundingBox3D) combined = lrg_bounding_box3d_merge(box1, box2);
/* Result: (-10,-10,-10) to (10,10,10) */
```

## Common Usage Patterns

### Trigger Volume Testing

```c
g_autoptr(GrlVector3) entity_pos = get_entity_position();
const LrgTrigger3D *trigger = lrg_level3d_get_trigger(level, "exit");
g_autoptr(LrgBoundingBox3D) trigger_bounds = lrg_trigger3d_get_bounds(trigger);

if (lrg_bounding_box3d_contains_point(trigger_bounds, entity_pos))
    g_print("Trigger activated!\n");
```

### Visibility Queries

```c
g_autoptr(LrgBoundingBox3D) view_box = lrg_bounding_box3d_new_from_center(camera_pos, 200.0f);
g_autoptr(GPtrArray) visible = lrg_level3d_query_box(level, view_box);
```

### Collision Detection

```c
g_autoptr(LrgBoundingBox3D) player_box = get_player_bounds();
g_autoptr(LrgBoundingBox3D) obstacle_box = get_obstacle_bounds();

if (lrg_bounding_box3d_intersects(player_box, obstacle_box))
    handle_collision();
```

## Performance Notes

- All operations are O(1) constant time
- Boxes use axis-aligned geometry for fast tests
- Use sphere queries instead of box for better performance in spatial indices

## See Also

- [LrgLevel3D](level3d.md) - Uses bounding boxes
- [LrgSector](sector.md) - Sectors contain bounds
- [LrgTrigger3D](trigger3d.md) - Triggers use bounds
