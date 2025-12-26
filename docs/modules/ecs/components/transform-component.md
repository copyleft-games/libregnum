# LrgTransformComponent

Transform component with parent/child hierarchy.

## Overview

`LrgTransformComponent` provides hierarchical transforms where child transforms are relative to their parent. This allows for scene graphs where moving a parent automatically moves all children.

The component stores local position, rotation, and scale. World-space values are computed by combining with parent transforms. This enables building complex hierarchical structures like character rigs (body -> arm -> hand).

## Basic Usage

### Creating and Positioning Transforms

```c
#include <libregnum.h>

/* Create transform at origin */
g_autoptr(LrgTransformComponent) root = lrg_transform_component_new ();

/* Create at specific position */
g_autoptr(LrgTransformComponent) arm =
    lrg_transform_component_new_at (50.0f, 0.0f);

/* Set position */
lrg_transform_component_set_local_position_xy (arm, 25.0f, 10.0f);

/* Get position as vector */
g_autoptr(GrlVector2) pos = lrg_transform_component_get_local_position (arm);
gfloat x = pos->x;
gfloat y = pos->y;
```

### Building Hierarchies

```c
/* Create parent and child */
g_autoptr(LrgTransformComponent) parent =
    lrg_transform_component_new_at (100.0f, 100.0f);
g_autoptr(LrgTransformComponent) child =
    lrg_transform_component_new_at (10.0f, 10.0f);

/* Set parent relationship */
lrg_transform_component_set_parent (child, parent);

/* Moving parent moves child in world space */
lrg_transform_component_set_local_position_xy (parent, 200.0f, 200.0f);

/* Child's world position is now (210, 210) */
g_autoptr(GrlVector2) world_pos =
    lrg_transform_component_get_world_position (child);

/* Unparent */
lrg_transform_component_set_parent (child, NULL);
```

### Rotation and Scale

```c
g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new ();

/* Set rotation in degrees */
lrg_transform_component_set_local_rotation (transform, 45.0f);

/* Rotate relative to current */
lrg_transform_component_rotate (transform, 15.0f);  /* Now 60 degrees */

/* Set scale */
lrg_transform_component_set_local_scale_xy (transform, 2.0f, 1.5f);

/* Uniform scale */
lrg_transform_component_set_local_scale_uniform (transform, 2.0f);

/* Look at target position */
g_autoptr(GrlVector2) target = grl_vector2_new (200.0f, 150.0f);
lrg_transform_component_look_at (transform, target);
```

## API Reference

### Construction

```c
LrgTransformComponent * lrg_transform_component_new (void);
```

Creates a new transform component at position (0, 0).

Returns: (transfer full) A new `LrgTransformComponent`

```c
LrgTransformComponent * lrg_transform_component_new_at (gfloat x, gfloat y);
```

Creates a new transform component at the specified local position.

Parameters:
- `x` - Initial local X position
- `y` - Initial local Y position

Returns: (transfer full) A new `LrgTransformComponent`

### Local Transform (Relative to Parent)

```c
GrlVector2 * lrg_transform_component_get_local_position (LrgTransformComponent *self);
```

Gets the local position relative to parent.

Returns: (transfer full) A new `GrlVector2` with the local position

```c
void lrg_transform_component_set_local_position (LrgTransformComponent *self,
                                                 GrlVector2            *position);
```

Sets the local position relative to parent.

```c
void lrg_transform_component_set_local_position_xy (LrgTransformComponent *self,
                                                    gfloat                 x,
                                                    gfloat                 y);
```

Sets the local position using X and Y coordinates.

```c
gfloat lrg_transform_component_get_local_x (LrgTransformComponent *self);
void lrg_transform_component_set_local_x (LrgTransformComponent *self, gfloat x);

gfloat lrg_transform_component_get_local_y (LrgTransformComponent *self);
void lrg_transform_component_set_local_y (LrgTransformComponent *self, gfloat y);
```

Get/set individual X and Y coordinates.

```c
gfloat lrg_transform_component_get_local_rotation (LrgTransformComponent *self);
```

Gets the local rotation in degrees.

```c
void lrg_transform_component_set_local_rotation (LrgTransformComponent *self,
                                                 gfloat                 rotation);
```

Sets the local rotation in degrees.

```c
GrlVector2 * lrg_transform_component_get_local_scale (LrgTransformComponent *self);
```

Gets the local scale as a vector (x, y).

Returns: (transfer full) A new `GrlVector2` with the local scale

```c
void lrg_transform_component_set_local_scale (LrgTransformComponent *self,
                                              GrlVector2            *scale);
```

Sets the local scale from a vector.

```c
void lrg_transform_component_set_local_scale_xy (LrgTransformComponent *self,
                                                 gfloat                 scale_x,
                                                 gfloat                 scale_y);
```

Sets the local scale using separate X and Y factors.

```c
void lrg_transform_component_set_local_scale_uniform (LrgTransformComponent *self,
                                                      gfloat                 scale);
```

Sets uniform scale for both X and Y.

### World Transform (Absolute, Computed from Hierarchy)

```c
GrlVector2 * lrg_transform_component_get_world_position (LrgTransformComponent *self);
```

Gets the world-space position (combining all parent transforms).

Returns: (transfer full) A new `GrlVector2` with the world position

```c
gfloat lrg_transform_component_get_world_rotation (LrgTransformComponent *self);
```

Gets the world-space rotation in degrees (combining all parent rotations).

```c
GrlVector2 * lrg_transform_component_get_world_scale (LrgTransformComponent *self);
```

Gets the world-space scale (combining all parent scales).

Returns: (transfer full) A new `GrlVector2` with the world scale

### Parent/Child Hierarchy

```c
LrgTransformComponent * lrg_transform_component_get_parent (LrgTransformComponent *self);
```

Gets the parent transform.

Returns: (transfer none) (nullable) The parent transform, or `NULL`

```c
void lrg_transform_component_set_parent (LrgTransformComponent *self,
                                         LrgTransformComponent *parent);
```

Sets the parent transform. The local position becomes relative to the parent. Setting to `NULL` removes the parent relationship.

Parameters:
- `self` - an `LrgTransformComponent`
- `parent` - (nullable) The new parent transform, or `NULL` to unparent

```c
GList * lrg_transform_component_get_children (LrgTransformComponent *self);
```

Gets a list of all child transforms.

Returns: (transfer container) (element-type LrgTransformComponent) List of children

```c
guint lrg_transform_component_get_child_count (LrgTransformComponent *self);
```

Gets the number of child transforms.

```c
void lrg_transform_component_detach_children (LrgTransformComponent *self);
```

Removes all children from this transform. Children become root transforms with their current world position.

### Utility Methods

```c
void lrg_transform_component_translate (LrgTransformComponent *self,
                                        GrlVector2            *offset);
```

Translates the transform by the given offset.

Parameters:
- `self` - an `LrgTransformComponent`
- `offset` - Translation offset in local space

```c
void lrg_transform_component_rotate (LrgTransformComponent *self,
                                     gfloat                 degrees);
```

Rotates the transform by the given amount.

Parameters:
- `self` - an `LrgTransformComponent`
- `degrees` - Rotation amount in degrees

```c
void lrg_transform_component_look_at (LrgTransformComponent *self,
                                      GrlVector2            *target);
```

Rotates the transform to face the target position.

Parameters:
- `self` - an `LrgTransformComponent`
- `target` - World-space target position

```c
void lrg_transform_component_sync_to_entity (LrgTransformComponent *self);
```

Syncs the world transform to the owning game object's entity transform. Call this after modifying the transform to update the entity's display position.

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `local-position` | `GrlVector2*` | RW | (0, 0) | Position relative to parent |
| `local-x` | `gfloat` | RW | 0 | X coordinate |
| `local-y` | `gfloat` | RW | 0 | Y coordinate |
| `local-rotation` | `gfloat` | RW | 0 | Rotation in degrees |
| `local-scale` | `GrlVector2*` | RW | (1, 1) | Scale factors |
| `parent` | `LrgTransformComponent*` | RW | `NULL` | Parent transform |

## World vs Local Space

The component maintains two coordinate systems:

**Local Space**: Relative to the parent transform
- `get_local_position()`, `get_local_rotation()`, `get_local_scale()`
- Used for positioning within hierarchies

**World Space**: Absolute position in the scene
- `get_world_position()`, `get_world_rotation()`, `get_world_scale()`
- Computed by combining all ancestor transforms
- Read-only

Example:
```c
parent.position = (100, 100)
child.local_position = (10, 10)
child.world_position = (110, 110)
```

## Hierarchy Best Practices

### Building Character Rigs

```c
/* Create body as root */
g_autoptr(LrgTransformComponent) body =
    lrg_transform_component_new_at (0.0f, 0.0f);

/* Add arm as child of body */
g_autoptr(LrgTransformComponent) arm =
    lrg_transform_component_new_at (15.0f, 0.0f);
lrg_transform_component_set_parent (arm, body);

/* Add hand as child of arm */
g_autoptr(LrgTransformComponent) hand =
    lrg_transform_component_new_at (20.0f, 0.0f);
lrg_transform_component_set_parent (hand, arm);

/* Moving body moves entire chain */
lrg_transform_component_set_local_position_xy (body, 200.0f, 150.0f);
```

### Deep Hierarchies

Hierarchies can be arbitrarily deep. World calculations walk up the parent chain, so performance scales with depth. Keep hierarchies reasonably shallow (< 10 levels) for best performance.

### Circular References

The component prevents circular parent relationships. Setting a transform as parent of itself or its descendants is not allowed.

## Related Types

- [LrgComponent](../component.md) - Base component class
- [LrgGameObject](../game-object.md) - Entity container
- `GrlVector2` - 2D vector from graylib
- `GrlRectangle` - Rectangle type from graylib

## See Also

- [ECS Overview](../index.md) - Module overview
- [ECS Examples](../../examples/ecs-basics.md) - Comprehensive examples
