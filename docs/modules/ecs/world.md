# LrgWorld

World container for game objects.

## Overview

`LrgWorld` provides a container for managing game objects in a scene. It wraps graylib's `GrlScene` and provides game-object-centric APIs for adding, removing, and finding objects.

A world manages:
- Collection of game objects
- Active/paused state for update control
- Object updates and rendering
- Tag-based object lookup
- Integration with graylib's scene system

## Basic Usage

### Creating a World

```c
#include <libregnum.h>

g_autoptr(LrgWorld) world = lrg_world_new ();

/* World is active and not paused by default */
g_assert_true (lrg_world_get_active (world));
g_assert_false (lrg_world_get_paused (world));
```

### Managing Objects

```c
/* Create and add objects */
g_autoptr(LrgGameObject) player = lrg_game_object_new_at (100.0f, 150.0f);
grl_entity_set_tag (GRL_ENTITY (player), "player");
lrg_world_add_object (world, player);

g_autoptr(LrgGameObject) enemy = lrg_game_object_new_at (200.0f, 150.0f);
grl_entity_set_tag (GRL_ENTITY (enemy), "enemy");
lrg_world_add_object (world, enemy);

/* Get object count */
guint count = lrg_world_get_object_count (world);
g_assert_cmpuint (count, ==, 2);

/* Get all objects */
GList *objects = lrg_world_get_objects (world);
for (GList *iter = objects; iter != NULL; iter = iter->next)
{
    LrgGameObject *obj = LRG_GAME_OBJECT (iter->data);
    /* Process object */
}
g_list_free (objects);

/* Remove specific object */
lrg_world_remove_object (world, enemy);

/* Clear all objects */
lrg_world_clear (world);
```

### Finding Objects

```c
/* Find by tag (first match) */
LrgGameObject *player = lrg_world_find_by_tag (world, "player");
if (player != NULL)
{
    gfloat x = grl_entity_get_x (GRL_ENTITY (player));
    gfloat y = grl_entity_get_y (GRL_ENTITY (player));
}

/* Find all with tag */
GList *enemies = lrg_world_find_all_by_tag (world, "enemy");
for (GList *iter = enemies; iter != NULL; iter = iter->next)
{
    LrgGameObject *enemy = LRG_GAME_OBJECT (iter->data);
    /* Process enemy */
}
g_list_free (enemies);
```

### Game Loop Integration

```c
while (game_running)
{
    /* Update world (updates all active objects and their components) */
    lrg_world_update (world, delta_time);

    /* Draw world (draws all visible objects) */
    lrg_world_draw (world);

    /* Input handling, collision detection, etc. */
}
```

### World State Control

```c
/* Pause world (draws but doesn't update) */
lrg_world_set_paused (world, TRUE);

/* Unpause */
lrg_world_set_paused (world, FALSE);

/* Disable world (no updates or draws) */
lrg_world_set_active (world, FALSE);

/* Re-enable */
lrg_world_set_active (world, TRUE);
```

## API Reference

### Construction

```c
LrgWorld * lrg_world_new (void);
```

Creates a new empty world. World is active and not paused by default.

Returns: (transfer full) A new `LrgWorld`

### Game Object Management

```c
void lrg_world_add_object (LrgWorld *self, LrgGameObject *object);
```

Adds a game object to this world. The world takes a reference to the object.

Parameters:
- `self` - an `LrgWorld`
- `object` - (transfer none) the game object to add

```c
void lrg_world_remove_object (LrgWorld *self, LrgGameObject *object);
```

Removes a game object from this world. The world releases its reference to the object.

Parameters:
- `self` - an `LrgWorld`
- `object` - the game object to remove

```c
void lrg_world_clear (LrgWorld *self);
```

Removes all game objects from this world.

Parameters:
- `self` - an `LrgWorld`

```c
GList * lrg_world_get_objects (LrgWorld *self);
```

Gets a list of all game objects in this world.

Parameters:
- `self` - an `LrgWorld`

Returns: (transfer container) (element-type LrgGameObject) List of objects (must be freed with `g_list_free()`)

```c
guint lrg_world_get_object_count (LrgWorld *self);
```

Gets the number of game objects in this world.

Parameters:
- `self` - an `LrgWorld`

Returns: The number of objects

### Object Lookup

```c
LrgGameObject * lrg_world_find_by_tag (LrgWorld *self, const gchar *tag);
```

Finds the first game object with the specified tag.

Parameters:
- `self` - an `LrgWorld`
- `tag` - the tag to search for

Returns: (transfer none) (nullable) The game object, or `NULL` if not found

```c
GList * lrg_world_find_all_by_tag (LrgWorld *self, const gchar *tag);
```

Finds all game objects with the specified tag.

Parameters:
- `self` - an `LrgWorld`
- `tag` - the tag to search for

Returns: (transfer container) (element-type LrgGameObject) List of matching objects

### Frame Processing

```c
void lrg_world_update (LrgWorld *self, gfloat delta);
```

Updates all game objects in the world. Only active objects are updated. If the world is paused, no objects are updated.

Parameters:
- `self` - an `LrgWorld`
- `delta` - time elapsed since last frame in seconds

Update order:
1. If world is not active, nothing happens
2. If world is not paused, all enabled components are updated
3. Updates propagate from world to objects to components

```c
void lrg_world_draw (LrgWorld *self);
```

Draws all visible game objects in the world. Objects are drawn in z-index order (lowest first).

Parameters:
- `self` - an `LrgWorld`

### graylib Integration

```c
GrlScene * lrg_world_get_scene (LrgWorld *self);
```

Gets the underlying graylib scene. This can be used to access graylib-specific features or to add non-game-object entities to the scene.

Parameters:
- `self` - an `LrgWorld`

Returns: (transfer none) The `GrlScene`

### Properties

```c
gboolean lrg_world_get_active (LrgWorld *self);
```

Gets whether the world is active.

Returns: `TRUE` if the world is active

```c
void lrg_world_set_active (LrgWorld *self, gboolean active);
```

Sets whether the world is active. Inactive worlds do not update or draw their objects.

Parameters:
- `self` - an `LrgWorld`
- `active` - whether the world should be active

```c
gboolean lrg_world_get_paused (LrgWorld *self);
```

Gets whether the world is paused.

Returns: `TRUE` if the world is paused

```c
void lrg_world_set_paused (LrgWorld *self, gboolean paused);
```

Sets whether the world is paused. Paused worlds still draw their objects but do not update them.

Parameters:
- `self` - an `LrgWorld`
- `paused` - whether the world should be paused

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `active` | `gboolean` | RW | `TRUE` | Whether the world updates and draws |
| `paused` | `gboolean` | RW | `FALSE` | Whether the world is paused |

## Update and Draw Lifecycle

### Active/Paused States

| State | Update | Draw |
|-------|--------|------|
| Active, Not Paused | Yes | Yes |
| Active, Paused | No | Yes |
| Inactive | No | No |

### Per-Frame Sequence

```
while (running)
{
    1. lrg_world_update(world, delta)
       - If inactive: skip
       - If not paused: update all enabled objects
         - Object update (from GrlEntity)
         - Component update for each enabled component

    2. lrg_world_draw(world)
       - Draw all visible objects (z-index order)
       - Objects inherit GrlEntity rendering

    3. Collision detection, input, game logic
}
```

## Implementation Notes

### Object References

The world maintains references to all contained objects. Objects are freed when:
1. Explicitly removed via `lrg_world_remove_object()`
2. World is destroyed
3. All external references are released

### Tag-Based Lookup

Tags are inherited from `GrlEntity`. Each object can have one tag. Lookups are O(n) - for frequent lookups, cache the returned object reference.

### Scene Access

`lrg_world_get_scene()` returns the underlying `GrlScene`. This allows:
- Direct access to graylib rendering systems
- Adding non-game-object entities
- Custom rendering passes

```c
GrlScene *scene = lrg_world_get_scene (world);
/* Use graylib scene API if needed */
```

### Performance Considerations

For worlds with many objects:
- Prefer `lrg_world_find_by_tag()` for single lookups
- Cache component references on objects
- Disable objects/components when not needed
- Use layers/masks on colliders to reduce collision checks

## Example: Multi-World Game

```c
/* Game state with multiple worlds */
typedef struct
{
    LrgWorld *main_world;      /* Gameplay */
    LrgWorld *ui_world;        /* UI overlays */
    LrgWorld *background_world; /* Parallax background */
} GameState;

/* Update all worlds */
void
game_update (GameState *state, gfloat delta)
{
    lrg_world_update (state->background_world, delta);
    lrg_world_update (state->main_world, delta);
    lrg_world_update (state->ui_world, delta);  /* UI typically paused */
}

/* Draw all worlds */
void
game_draw (GameState *state)
{
    lrg_world_draw (state->background_world);
    lrg_world_draw (state->main_world);
    lrg_world_draw (state->ui_world);
}
```

## Related Types

- [LrgGameObject](game-object.md) - Entity container
- [LrgComponent](component.md) - Base component class
- `GrlScene` - Underlying graylib scene
- `GrlEntity` - Base class for game objects

## See Also

- [ECS Overview](index.md) - Module overview
- [ECS Examples](../examples/ecs-basics.md) - Comprehensive examples
