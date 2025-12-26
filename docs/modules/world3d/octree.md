# LrgOctree

## Overview

`LrgOctree` is a spatial data structure that partitions 3D space into 8 sub-regions recursively. It enables efficient spatial queries by eliminating branches that don't contain the query region.

## Type Information

- **Type Name**: `LrgOctree`
- **Type ID**: `LRG_TYPE_OCTREE`
- **Base Class**: `GObject`
- **Final Type**: Yes

## Construction

### lrg_octree_new

```c
LrgOctree *lrg_octree_new(const LrgBoundingBox3D *bounds);
```

Creates a new octree with default settings (max_depth=8, max_objects=16).

### lrg_octree_new_with_depth

```c
LrgOctree *lrg_octree_new_with_depth(const LrgBoundingBox3D *bounds, guint max_depth);
```

Creates a new octree with specified maximum depth.

**Parameters:**
- `max_depth` - Maximum subdivision levels (typical: 6-10)

## Object Management

### lrg_octree_insert

```c
gboolean lrg_octree_insert(LrgOctree *self, gpointer object, const LrgBoundingBox3D *bounds);
```

Inserts an object with its bounding box.

**Returns:** `TRUE` if inserted successfully

### lrg_octree_remove

```c
gboolean lrg_octree_remove(LrgOctree *self, gpointer object);
```

Removes an object.

**Returns:** `TRUE` if found and removed

### lrg_octree_update

```c
gboolean lrg_octree_update(LrgOctree *self, gpointer object, const LrgBoundingBox3D *new_bounds);
```

Updates an object's position. Removes and re-inserts the object.

### lrg_octree_clear

```c
void lrg_octree_clear(LrgOctree *self);
```

Removes all objects.

## Spatial Queries

### lrg_octree_query_box

```c
GPtrArray *lrg_octree_query_box(LrgOctree *self, const LrgBoundingBox3D *query);
```

Finds all objects intersecting a bounding box.

**Returns:** (transfer container) (element-type gpointer) Results

### lrg_octree_query_sphere

```c
GPtrArray *lrg_octree_query_sphere(LrgOctree *self, const GrlVector3 *center, gfloat radius);
```

Finds all objects intersecting a sphere.

**Performance:** Faster than box queries in many cases.

### lrg_octree_query_point

```c
GPtrArray *lrg_octree_query_point(LrgOctree *self, const GrlVector3 *point);
```

Finds all objects containing a point.

### lrg_octree_query_nearest

```c
gpointer lrg_octree_query_nearest(LrgOctree *self, const GrlVector3 *point);
```

Finds the nearest object to a point.

**Returns:** (transfer none) (nullable) The nearest object

## Properties and Configuration

### lrg_octree_get_bounds

```c
LrgBoundingBox3D *lrg_octree_get_bounds(LrgOctree *self);
```

Gets the world bounds.

### lrg_octree_get_object_count

```c
guint lrg_octree_get_object_count(LrgOctree *self);
```

Gets total number of objects.

### lrg_octree_get_node_count

```c
guint lrg_octree_get_node_count(LrgOctree *self);
```

Gets number of nodes in the tree.

### lrg_octree_get_max_depth / lrg_octree_set_max_depth

```c
guint lrg_octree_get_max_depth(LrgOctree *self);
void lrg_octree_set_max_depth(LrgOctree *self, guint max_depth);
```

Gets/sets maximum subdivision depth. Only affects future insertions.

### lrg_octree_get_max_objects_per_node / lrg_octree_set_max_objects_per_node

```c
guint lrg_octree_get_max_objects_per_node(LrgOctree *self);
void lrg_octree_set_max_objects_per_node(LrgOctree *self, guint max_objects);
```

Gets/sets threshold for node subdivision.

### lrg_octree_rebuild

```c
void lrg_octree_rebuild(LrgOctree *self);
```

Rebuilds the entire tree. Call after changing max_depth or max_objects.

## Example

```c
g_autoptr(LrgBoundingBox3D) bounds = lrg_bounding_box3d_new(-1000, -1000, -1000, 1000, 1000, 1000);
g_autoptr(LrgOctree) tree = lrg_octree_new_with_depth(bounds, 8);

/* Insert objects */
for (int i = 0; i < 100; i++) {
    g_autoptr(LrgBoundingBox3D) obj_bounds = lrg_bounding_box3d_new(i*10, i*10, i*10, i*10+5, i*10+5, i*10+5);
    lrg_octree_insert(tree, &objects[i], obj_bounds);
}

/* Query nearby objects */
g_autoptr(GrlVector3) center = g_new(GrlVector3, 1);
center->x = 500; center->y = 500; center->z = 500;
g_autoptr(GPtrArray) nearby = lrg_octree_query_sphere(tree, center, 100.0f);
g_print("Found %u nearby objects\n", nearby->len);
```

## Performance Tuning

| Setting | Impact | Typical Range |
|---------|--------|---------------|
| max_depth | Balance depth vs queries | 6-10 |
| max_objects | Node subdivision threshold | 8-32 |

Lower max_depth = faster updates, slower queries. Higher max_depth = slower updates, faster queries.

## See Also

- [LrgLevel3D](level3d.md) - Uses octree internally
- [LrgBoundingBox3D](bounding-box3d.md) - Bounding box type
