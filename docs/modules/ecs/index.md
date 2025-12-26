# ECS Module

## Overview

The Entity-Component-System (ECS) module is the core architecture for building game entities in Libregnum. Rather than using deep inheritance hierarchies, ECS uses composition to give game objects flexibility and extensibility.

### Key Concepts

**Game Objects** (`LrgGameObject`) are containers for game entities. They inherit from `GrlEntity` and provide access to transform properties (position, rotation, scale).

**Components** (`LrgComponent`) are modular pieces of functionality that attach to game objects. Custom components are created by subclassing `LrgComponent` and overriding virtual methods for lifecycle and update behavior.

**Worlds** (`LrgWorld`) manage collections of game objects. They handle updates, drawing, and querying objects by tag. Worlds integrate with graylib's scene system for rendering.

### ECS Philosophy

The ECS approach offers several advantages:

- **Composition over Inheritance**: Mix and match components to create different entity types
- **Flexible Behavior**: Add, remove, or modify entity behavior at runtime
- **Decoupled Systems**: Components operate independently, reducing coupling
- **Easy Testing**: Components can be tested in isolation

## Quick Start

### Creating a World and Objects

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgWorld) world = lrg_world_new ();

    /* Create a game object */
    g_autoptr(LrgGameObject) player = lrg_game_object_new_at (100.0f, 150.0f);

    /* Add to world */
    lrg_world_add_object (world, player);

    /* Game loop */
    gfloat delta = 0.016f;  /* 60 FPS */
    while (TRUE)
    {
        lrg_world_update (world, delta);
        lrg_world_draw (world);

        /* Input handling, collision detection, etc. */
    }

    return 0;
}
```

### Adding Components

```c
/* Create a sprite component */
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
lrg_game_object_add_component (player, LRG_COMPONENT (sprite));

/* Create a collider */
g_autoptr(LrgColliderComponent) collider =
    lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);
lrg_game_object_add_component (player, LRG_COMPONENT (collider));

/* Create a transform component for hierarchies */
g_autoptr(LrgTransformComponent) transform =
    lrg_transform_component_new_at (50.0f, 75.0f);
lrg_game_object_add_component (player, LRG_COMPONENT (transform));
```

### Querying Components

```c
/* Get a specific component by type */
LrgSpriteComponent *sprite =
    lrg_game_object_get_component_of_type (player,
                                           LrgSpriteComponent,
                                           LRG_TYPE_SPRITE_COMPONENT);

/* Check if object has a component */
if (lrg_game_object_has_component (player, LRG_TYPE_COLLIDER_COMPONENT))
{
    g_autoptr(LrgColliderComponent) collider =
        lrg_game_object_get_component_of_type (player,
                                               LrgColliderComponent,
                                               LRG_TYPE_COLLIDER_COMPONENT);
    /* Use collider */
}

/* Get all components */
GList *components = lrg_game_object_get_components (player);
for (GList *iter = components; iter != NULL; iter = iter->next)
{
    LrgComponent *comp = LRG_COMPONENT (iter->data);
    /* Process component */
}
g_list_free (components);
```

### Finding Objects in World

```c
/* Find by tag */
LrgGameObject *player = lrg_world_find_by_tag (world, "player");

/* Find all enemies */
GList *enemies = lrg_world_find_all_by_tag (world, "enemy");
for (GList *iter = enemies; iter != NULL; iter = iter->next)
{
    LrgGameObject *enemy = LRG_GAME_OBJECT (iter->data);
    /* Process enemy */
}
g_list_free (enemies);
```

## Core Types

| Type | Description |
|------|-------------|
| `LrgComponent` | Abstract base class for all components |
| `LrgGameObject` | Entity container with component support |
| `LrgWorld` | Scene manager for game objects |

## Component Types

| Type | Description |
|------|-------------|
| `LrgTransformComponent` | Position, rotation, scale with hierarchy support |
| `LrgSpriteComponent` | Rendering with texture, tint, and flip |
| `LrgColliderComponent` | Collision bounds with layer/mask filtering |
| `LrgAnimatorComponent` | Frame-based animation playback |

## Properties and State

**Worlds** track active/paused state:
- Active worlds update and draw their objects
- Paused worlds draw but don't update
- Inactive worlds are completely disabled

**Components** can be enabled/disabled:
- Disabled components do not receive update() calls
- Component state is independent of owner state

**Game Objects** inherit from graylib's `GrlEntity`:
- Position, rotation, scale properties
- Visibility and z-index support
- Tag system for quick lookup

## Related Types

- `GrlEntity` - Base class from graylib for transform and rendering
- `GrlScene` - Underlying scene system (accessed via `lrg_world_get_scene()`)

## See Also

- [LrgComponent](component.md) - Base class documentation
- [LrgGameObject](game-object.md) - Entity container documentation
- [LrgWorld](world.md) - Scene manager documentation
- [Component Types](components/) - Built-in component documentation
- [ECS Examples](../examples/ecs-basics.md) - Comprehensive usage examples
