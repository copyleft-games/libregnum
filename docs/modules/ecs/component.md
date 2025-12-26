# LrgComponent

Abstract base class for game object components.

## Overview

`LrgComponent` is an abstract base class that provides the foundation for all component types in the ECS system. Components are modular pieces of functionality that can be attached to game objects to customize their behavior.

Custom components should derive from `LrgComponent` and override the virtual methods:
- `attached()` - Called when attached to a game object
- `detached()` - Called when detached from a game object
- `update()` - Called each frame to update the component

Components are enabled/disabled independently, allowing fine-grained control over which subsystems are active.

## Basic Usage

### Creating a Custom Component

```c
/* Define a custom component type */
#define MY_TYPE_HEALTH_COMPONENT (my_health_component_get_type ())
G_DECLARE_FINAL_TYPE (MyHealthComponent, my_health_component,
                      MY, HEALTH_COMPONENT, LrgComponent)

struct _MyHealthComponent
{
    LrgComponent parent_instance;
    gint         health;
    gint         max_health;
};

G_DEFINE_TYPE (MyHealthComponent, my_health_component, LRG_TYPE_COMPONENT)

/* Implement virtual methods */
static void
my_health_component_attached (LrgComponent  *self,
                              LrgGameObject *owner)
{
    MyHealthComponent *health = MY_HEALTH_COMPONENT (self);
    /* Called when component is attached to object */
    health->health = health->max_health;
}

static void
my_health_component_update (LrgComponent *self,
                            gfloat        delta)
{
    MyHealthComponent *health = MY_HEALTH_COMPONENT (self);
    /* Called each frame if enabled */
}

static void
my_health_component_class_init (MyHealthComponentClass *klass)
{
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
    component_class->attached = my_health_component_attached;
    component_class->update = my_health_component_update;
}

static void
my_health_component_init (MyHealthComponent *self)
{
    self->health = 100;
    self->max_health = 100;
}

/* Factory function */
MyHealthComponent *
my_health_component_new (void)
{
    return g_object_new (MY_TYPE_HEALTH_COMPONENT, NULL);
}
```

### Using Components with Game Objects

```c
g_autoptr(LrgGameObject) player = lrg_game_object_new ();
g_autoptr(LrgComponent) health = LRG_COMPONENT (my_health_component_new ());

/* Attach component */
lrg_game_object_add_component (player, health);

/* Component is now enabled by default */
g_assert_true (lrg_component_get_enabled (health));

/* Disable component (no update calls will occur) */
lrg_component_set_enabled (health, FALSE);

/* Re-enable */
lrg_component_set_enabled (health, TRUE);

/* Get owner of component */
LrgGameObject *owner = lrg_component_get_owner (health);
g_assert_true (owner == player);
```

## API Reference

### Properties

```c
gboolean lrg_component_get_enabled (LrgComponent *self);
```

Gets whether this component is enabled. Disabled components do not receive `update()` calls.

Returns: `TRUE` if the component is enabled

```c
void lrg_component_set_enabled (LrgComponent *self, gboolean enabled);
```

Sets whether this component is enabled.

Parameters:
- `self` - an `LrgComponent`
- `enabled` - whether to enable the component

### Accessors

```c
LrgGameObject * lrg_component_get_owner (LrgComponent *self);
```

Gets the game object that owns this component.

Returns: (transfer none) (nullable) The owning `LrgGameObject`, or `NULL`

### Methods

```c
void lrg_component_update (LrgComponent *self, gfloat delta);
```

Updates the component for the current frame. This calls the virtual `update()` method if the component is enabled. Typically called by the owning game object, not directly.

Parameters:
- `self` - an `LrgComponent`
- `delta` - time elapsed since last frame in seconds

## Virtual Methods

### attached

```c
void (*attached) (LrgComponent  *self, LrgGameObject *owner);
```

Called when the component is attached to a game object. Subclasses can override this to perform initialization that requires the owner to be set.

Lifecycle: Called after component is added to object and owner reference is set.

### detached

```c
void (*detached) (LrgComponent *self);
```

Called when the component is about to be detached from its owner. Subclasses can override this to perform cleanup.

Lifecycle: Called before component is removed from object.

### update

```c
void (*update) (LrgComponent *self, gfloat delta);
```

Called each frame to update the component. Only called if the component is enabled.

Parameters:
- `self` - the component
- `delta` - time elapsed since last frame in seconds

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `enabled` | `gboolean` | RW | `TRUE` | Whether the component receives update calls |
| `owner` | `LrgGameObject*` | RO | `NULL` | The game object that owns this component |

## Implementation Notes

### Memory Management

Components use GObject reference counting. Game objects hold a reference to attached components, so components are freed when:
1. Explicitly removed via `lrg_game_object_remove_component()`
2. Owner is destroyed
3. All external references are released

### Initialization Order

When a component is attached:
1. `attached()` virtual method is called
2. Component can now access its owner via `lrg_component_get_owner()`
3. Component can access owner's properties (position, components, etc.)

When a component is detached:
1. `detached()` virtual method is called
2. Component's owner reference is cleared
3. Game object releases its reference

### Update Cycle

Each frame, the world updates all its objects:
1. Object updates all its enabled components (calls `lrg_component_update()`)
2. Component's `update()` virtual method is called with delta time
3. Component can modify its state and access owner properties

## Example: Damage System

```c
/* Create a damage component */
typedef struct
{
    LrgComponent parent_instance;
    gint         damage;
} DamageComponent;

static void
damage_component_attached (LrgComponent  *self,
                           LrgGameObject *owner)
{
    DamageComponent *damage = (DamageComponent *)self;
    /* Initialize with owner's position */
}

static void
damage_component_update (LrgComponent *self, gfloat delta)
{
    DamageComponent *damage = (DamageComponent *)self;
    LrgGameObject *owner = lrg_component_get_owner (self);

    /* Check collisions with other objects */
    LrgColliderComponent *collider =
        lrg_game_object_get_component_of_type (owner,
                                               LrgColliderComponent,
                                               LRG_TYPE_COLLIDER_COMPONENT);
    /* Apply damage logic */
}
```

## Related Types

- [LrgGameObject](game-object.md) - Container for components
- [LrgWorld](world.md) - Manages objects and components
- [LrgTransformComponent](components/transform-component.md) - Position/rotation
- [LrgSpriteComponent](components/sprite-component.md) - Rendering
- [LrgColliderComponent](components/collider-component.md) - Collision
- [LrgAnimatorComponent](components/animator-component.md) - Animation

## See Also

- [ECS Overview](index.md) - Module overview
- [ECS Examples](../examples/ecs-basics.md) - Comprehensive examples
