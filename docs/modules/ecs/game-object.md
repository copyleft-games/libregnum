# LrgGameObject

Game object with component support.

## Overview

`LrgGameObject` is the entity container in the ECS system. It extends graylib's `GrlEntity` to add component-based functionality.

Game objects inherit all transform properties (position, rotation, scale) and rendering capabilities from `GrlEntity`. They provide a flexible container for attaching components that customize behavior.

Each game object maintains:
- Transform properties: position (x, y), rotation, scale
- Visibility and z-index for rendering
- A list of attached components
- An optional tag for quick lookup in worlds

## Basic Usage

### Creating Game Objects

```c
#include <libregnum.h>

/* Create at origin */
g_autoptr(LrgGameObject) player = lrg_game_object_new ();

/* Create at specific position */
g_autoptr(LrgGameObject) enemy = lrg_game_object_new_at (150.0f, 200.0f);

/* Objects inherit GrlEntity properties */
gfl_entity_set_x (GRL_ENTITY (player), 100.0f);
grl_entity_set_y (GRL_ENTITY (player), 150.0f);
grl_entity_set_rotation (GRL_ENTITY (player), 45.0f);
```

### Managing Components

```c
g_autoptr(LrgGameObject) obj = lrg_game_object_new ();

/* Add a component */
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
lrg_game_object_add_component (obj, LRG_COMPONENT (sprite));

/* Add multiple components */
g_autoptr(LrgColliderComponent) collider =
    lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);
lrg_game_object_add_component (obj, LRG_COMPONENT (collider));

g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
lrg_game_object_add_component (obj, LRG_COMPONENT (animator));

/* Get component count */
guint count = lrg_game_object_get_component_count (obj);
g_assert_cmpuint (count, ==, 3);

/* Remove a component */
lrg_game_object_remove_component (obj, LRG_COMPONENT (collider));

/* Remove all components */
lrg_game_object_remove_all_components (obj);
```

### Querying Components

```c
/* Check if has component type */
if (lrg_game_object_has_component (obj, LRG_TYPE_SPRITE_COMPONENT))
{
    /* Has sprite */
}

/* Get single component by type (first found) */
LrgSpriteComponent *sprite =
    lrg_game_object_get_component_of_type (obj,
                                           LrgSpriteComponent,
                                           LRG_TYPE_SPRITE_COMPONENT);

/* Get all components of a type */
GList *colliders =
    lrg_game_object_get_components_of_type (obj, LRG_TYPE_COLLIDER_COMPONENT);
for (GList *iter = colliders; iter != NULL; iter = iter->next)
{
    LrgColliderComponent *col = LRG_COLLIDER_COMPONENT (iter->data);
    /* Process collider */
}
g_list_free (colliders);

/* Get all components */
GList *all = lrg_game_object_get_components (obj);
for (GList *iter = all; iter != NULL; iter = iter->next)
{
    LrgComponent *comp = LRG_COMPONENT (iter->data);
    /* Process component */
}
g_list_free (all);
```

## API Reference

### Construction

```c
LrgGameObject * lrg_game_object_new (void);
```

Creates a new game object at position (0, 0).

Returns: (transfer full) A new `LrgGameObject`

```c
LrgGameObject * lrg_game_object_new_at (gfloat x, gfloat y);
```

Creates a new game object at the specified position.

Parameters:
- `x` - Initial X position
- `y` - Initial Y position

Returns: (transfer full) A new `LrgGameObject`

### Component Management

```c
void lrg_game_object_add_component (LrgGameObject *self, LrgComponent *component);
```

Adds a component to this game object. The game object takes a reference to the component and calls its `attached()` virtual method. A component can only be attached to one game object at a time.

Parameters:
- `self` - an `LrgGameObject`
- `component` - (transfer none) the component to add

```c
void lrg_game_object_remove_component (LrgGameObject *self, LrgComponent *component);
```

Removes a component from this game object. The component's `detached()` virtual method is called, and the game object releases its reference to the component.

Parameters:
- `self` - an `LrgGameObject`
- `component` - the component to remove

```c
LrgComponent * lrg_game_object_get_component (LrgGameObject *self, GType component_type);
```

Finds a component by type. If the game object has multiple components of the same type, the first one found is returned.

Parameters:
- `self` - an `LrgGameObject`
- `component_type` - the `GType` of the component to find

Returns: (transfer none) (nullable) The component, or `NULL` if not found

```c
GList * lrg_game_object_get_components (LrgGameObject *self);
```

Gets a list of all components attached to this game object.

Parameters:
- `self` - an `LrgGameObject`

Returns: (transfer container) (element-type LrgComponent) List of components (must be freed with `g_list_free()`)

```c
gboolean lrg_game_object_has_component (LrgGameObject *self, GType component_type);
```

Checks if the game object has a component of the specified type.

Parameters:
- `self` - an `LrgGameObject`
- `component_type` - the `GType` to check for

Returns: `TRUE` if a component of that type is attached

```c
GList * lrg_game_object_get_components_of_type (LrgGameObject *self, GType component_type);
```

Gets all components of the specified type. Useful when an object has multiple components of the same type.

Parameters:
- `self` - an `LrgGameObject`
- `component_type` - the `GType` to find

Returns: (transfer container) (element-type LrgComponent) List of matching components

```c
void lrg_game_object_remove_all_components (LrgGameObject *self);
```

Removes all components from this game object. Each component's `detached()` method is called before removal.

Parameters:
- `self` - an `LrgGameObject`

```c
guint lrg_game_object_get_component_count (LrgGameObject *self);
```

Gets the number of components attached to this game object.

Parameters:
- `self` - an `LrgGameObject`

Returns: The number of attached components

### Convenience Macros

```c
#define lrg_game_object_get_component_of_type(obj, T, t) \
    ((T *)lrg_game_object_get_component ((obj), (t)))
```

Gets a component and casts it to the specified type.

Parameters:
- `obj` - an `LrgGameObject`
- `T` - the C type to cast to (e.g., `LrgSpriteComponent`)
- `t` - the `GType` of the component (e.g., `LRG_TYPE_SPRITE_COMPONENT`)

Returns: (nullable) The component cast to type T, or `NULL` if not found

Example:
```c
LrgSpriteComponent *sprite =
    lrg_game_object_get_component_of_type (obj, LrgSpriteComponent,
                                           LRG_TYPE_SPRITE_COMPONENT);
```

## Inherited Properties

From `GrlEntity`:

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `x` | `gfloat` | RW | X position |
| `y` | `gfloat` | RW | Y position |
| `rotation` | `gfloat` | RW | Rotation angle in degrees |
| `scale-x` | `gfloat` | RW | Horizontal scale |
| `scale-y` | `gfloat` | RW | Vertical scale |
| `visible` | `gboolean` | RW | Visibility flag |
| `z-index` | `gint` | RW | Drawing order |
| `tag` | `gchar*` | RW | Optional identifier for lookup |

## Implementation Notes

### Component Ownership

When a component is added to a game object:
1. Object takes a reference to the component
2. Component's `attached()` method is called
3. Component can now access owner via `lrg_component_get_owner()`

When a component is removed:
1. Component's `detached()` method is called
2. Object releases its reference
3. Component's owner is cleared

### Update Propagation

Game objects do not directly update components. Instead, `LrgWorld` calls `lrg_game_object_update()` (inherited from `GrlEntity`), which propagates to components. Components only update if:
1. Component is enabled
2. Object is active
3. World is not paused

### Component Type Queries

Component lookups are O(n) where n is the number of components on the object. For performance-critical code, cache component references:

```c
g_autoptr(LrgSpriteComponent) sprite =
    lrg_game_object_get_component_of_type (obj,
                                           LrgSpriteComponent,
                                           LRG_TYPE_SPRITE_COMPONENT);

/* Now access sprite multiple times without repeated lookup */
lrg_sprite_component_set_visible (sprite, TRUE);
lrg_sprite_component_set_tint (sprite, color);
```

## Example: Player Entity

```c
g_autoptr(LrgGameObject) player = lrg_game_object_new_at (640.0f, 360.0f);

/* Set tag for quick lookup */
grl_entity_set_tag (GRL_ENTITY (player), "player");

/* Add sprite component */
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
lrg_game_object_add_component (player, LRG_COMPONENT (sprite));

/* Add collider for physics */
g_autoptr(LrgColliderComponent) collider =
    lrg_collider_component_new_with_bounds (8.0f, 8.0f, 16.0f, 16.0f);
lrg_game_object_add_component (player, LRG_COMPONENT (collider));

/* Add animator for walking/idle animations */
g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
lrg_animator_component_add_animation (animator, "walk", 4, 8, 15.0f, TRUE);
lrg_game_object_add_component (player, LRG_COMPONENT (animator));

/* Add to world */
lrg_world_add_object (world, player);
```

## Related Types

- [LrgComponent](component.md) - Base class for components
- [LrgWorld](world.md) - Container for game objects
- `GrlEntity` - Base class from graylib
- [LrgSpriteComponent](components/sprite-component.md)
- [LrgColliderComponent](components/collider-component.md)
- [LrgTransformComponent](components/transform-component.md)
- [LrgAnimatorComponent](components/animator-component.md)

## See Also

- [ECS Overview](index.md) - Module overview
- [ECS Examples](../examples/ecs-basics.md) - Comprehensive examples
