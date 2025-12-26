# LrgColliderComponent

Collision bounds component.

## Overview

`LrgColliderComponent` defines collision bounds for a game object. It supports collision layers and masks for filtering which objects can collide with each other.

The component provides:
- Collision bounds relative to entity position
- World-space bounds calculation
- Layer/mask system for selective collision detection
- Intersection and collision compatibility testing
- Enable/disable per-collider

## Basic Usage

### Creating Colliders

```c
#include <libregnum.h>

/* Create empty collider */
g_autoptr(LrgColliderComponent) collider = lrg_collider_component_new ();

/* Create with bounds (x, y offset from entity, width, height) */
g_autoptr(LrgColliderComponent) collider_with_bounds =
    lrg_collider_component_new_with_bounds (8.0f, 8.0f, 16.0f, 16.0f);

/* Add to game object */
g_autoptr(LrgGameObject) enemy = lrg_game_object_new ();
lrg_game_object_add_component (enemy, LRG_COMPONENT (collider_with_bounds));
```

### Bounds Management

```c
g_autoptr(LrgColliderComponent) collider =
    lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);

/* Get local bounds (relative to entity) */
g_autoptr(GrlRectangle) local_bounds = lrg_collider_component_get_bounds (collider);
gfloat local_x = local_bounds->x;
gfloat local_y = local_bounds->y;

/* Get world bounds (at actual position) - requires owner */
/* First add to game object at position (100, 150) */
g_autoptr(LrgGameObject) obj = lrg_game_object_new_at (100.0f, 150.0f);
lrg_game_object_add_component (obj, LRG_COMPONENT (collider));

g_autoptr(GrlRectangle) world_bounds =
    lrg_collider_component_get_world_bounds (collider);
/* world_bounds.x = 100, world_bounds.y = 150 */

/* Update bounds */
lrg_collider_component_set_bounds (collider, 4.0f, 4.0f, 24.0f, 24.0f);
```

### Collision Detection

```c
g_autoptr(LrgColliderComponent) collider_a = lrg_collider_component_new ();
g_autoptr(LrgColliderComponent) collider_b = lrg_collider_component_new ();

/* Check bounds intersection (ignores layers/masks) */
gboolean intersects = lrg_collider_component_intersects (collider_a, collider_b);

/* Check if collision is allowed by layers/masks */
gboolean can_collide = lrg_collider_component_can_collide_with (collider_a, collider_b);

/* Both checks */
if (can_collide && intersects)
{
    /* Collision detected! */
}
```

### Layer and Mask System

```c
/* Create two colliders on different layers */
g_autoptr(LrgColliderComponent) player = lrg_collider_component_new ();
g_autoptr(LrgColliderComponent) enemy = lrg_collider_component_new ();

/* Set up player */
lrg_collider_component_set_layer (player, 1);      /* Layer 0 (bit 0) */
lrg_collider_component_set_mask (player, 6);       /* Collide with layers 1 and 2 */

/* Set up enemy */
lrg_collider_component_set_layer (enemy, 2);       /* Layer 1 (bit 1) */
lrg_collider_component_set_mask (enemy, 1);        /* Collide with layer 0 */

/* Collision possible both directions */
gboolean player_can_hit = lrg_collider_component_can_collide_with (player, enemy);
gboolean enemy_can_hit = lrg_collider_component_can_collide_with (enemy, player);
```

## API Reference

### Construction

```c
LrgColliderComponent * lrg_collider_component_new (void);
```

Creates a new collider component with default bounds (0, 0, 0, 0).

Returns: (transfer full) A new `LrgColliderComponent`

```c
LrgColliderComponent * lrg_collider_component_new_with_bounds (gfloat x,
                                                               gfloat y,
                                                               gfloat width,
                                                               gfloat height);
```

Creates a new collider component with the specified bounds.

Parameters:
- `x` - X offset from entity position
- `y` - Y offset from entity position
- `width` - collision width
- `height` - collision height

Returns: (transfer full) A new `LrgColliderComponent`

### Bounds (Relative to Entity Position)

```c
void lrg_collider_component_set_bounds (LrgColliderComponent *self,
                                        gfloat                x,
                                        gfloat                y,
                                        gfloat                width,
                                        gfloat                height);
```

Sets the collision bounds relative to the entity's position.

```c
GrlRectangle * lrg_collider_component_get_bounds (LrgColliderComponent *self);
```

Gets the collision bounds relative to the entity's position.

Returns: (transfer full) The bounds rectangle

```c
GrlRectangle * lrg_collider_component_get_world_bounds (LrgColliderComponent *self);
```

Gets the collision bounds in world coordinates. This combines the entity's position with the relative bounds to produce the actual collision area in world space. Requires the collider to be attached to a game object.

Returns: (transfer full) (nullable) The world bounds, or `NULL` if no owner

### Collision Enable/Disable

```c
void lrg_collider_component_set_collision_enabled (LrgColliderComponent *self,
                                                   gboolean              enabled);
```

Sets whether collision checking is enabled for this collider. Disabled colliders are ignored during collision detection.

```c
gboolean lrg_collider_component_get_collision_enabled (LrgColliderComponent *self);
```

Gets whether collision checking is enabled.

Returns: `TRUE` if collision checking is enabled

### Collision Layers

```c
void lrg_collider_component_set_layer (LrgColliderComponent *self, guint32 layer);
```

Sets the collision layer(s) this collider belongs to. An object collides with another if `(a.layer & b.mask) != 0`.

Parameters:
- `self` - an `LrgColliderComponent`
- `layer` - the collision layer bitmask

```c
guint32 lrg_collider_component_get_layer (LrgColliderComponent *self);
```

Gets the collision layer bitmask.

```c
void lrg_collider_component_set_mask (LrgColliderComponent *self, guint32 mask);
```

Sets which layers this collider can collide with. An object collides with another if `(a.layer & b.mask) != 0`.

Parameters:
- `self` - an `LrgColliderComponent`
- `mask` - the collision mask bitmask

```c
guint32 lrg_collider_component_get_mask (LrgColliderComponent *self);
```

Gets the collision mask bitmask.

### Collision Testing

```c
gboolean lrg_collider_component_intersects (LrgColliderComponent *self,
                                            LrgColliderComponent *other);
```

Tests whether this collider intersects with another. This only checks bounds intersection, not layer/mask filtering.

Parameters:
- `self` - an `LrgColliderComponent`
- `other` - another `LrgColliderComponent`

Returns: `TRUE` if the colliders intersect

```c
gboolean lrg_collider_component_can_collide_with (LrgColliderComponent *self,
                                                  LrgColliderComponent *other);
```

Tests whether this collider can collide with another based on layers. Checks if both:
1. Self is enabled
2. Other is enabled
3. Layer/mask rules match

Parameters:
- `self` - an `LrgColliderComponent`
- `other` - another `LrgColliderComponent`

Returns: `TRUE` if collision is possible (layer/mask match and both enabled)

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `bounds` | `GrlRectangle*` | RW | (0, 0, 0, 0) | Local bounds |
| `collision-enabled` | `gboolean` | RW | `TRUE` | Collision enabled |
| `layer` | `guint32` | RW | 1 | Layer bitmask |
| `mask` | `guint32` | RW | G_MAXUINT32 | Mask bitmask |

## Layer/Mask System

Collision filtering uses bitmasks. Default settings allow all collisions.

### Basic Example: Player vs Enemies

```c
#define LAYER_PLAYER  1   /* bit 0 */
#define LAYER_ENEMY   2   /* bit 1 */
#define LAYER_HAZARD  4   /* bit 2 */
#define LAYER_BULLET  8   /* bit 3 */

/* Player collides with enemies and hazards */
lrg_collider_component_set_layer (player_collider, LAYER_PLAYER);
lrg_collider_component_set_mask (player_collider, LAYER_ENEMY | LAYER_HAZARD);

/* Enemy collides with player and bullets */
lrg_collider_component_set_layer (enemy_collider, LAYER_ENEMY);
lrg_collider_component_set_mask (enemy_collider, LAYER_PLAYER | LAYER_BULLET);

/* Bullet collides with enemies only */
lrg_collider_component_set_layer (bullet_collider, LAYER_BULLET);
lrg_collider_component_set_mask (bullet_collider, LAYER_ENEMY);

/* Hazard collides with player only */
lrg_collider_component_set_layer (hazard_collider, LAYER_HAZARD);
lrg_collider_component_set_mask (hazard_collider, LAYER_PLAYER);
```

### Complex Scenarios: Multiple Layers

```c
#define LAYER_PLAYER   1        /* 0b0001 */
#define LAYER_ALLY     2        /* 0b0010 */
#define LAYER_ENEMY    4        /* 0b0100 */
#define LAYER_NPC      8        /* 0b1000 */
#define LAYER_BULLET   16       /* 0b10000 */

/* Friendly fire OFF: player bullets don't hit allies */
/* Player collides with enemies and NPCs */
lrg_collider_component_set_layer (player_collider, LAYER_PLAYER);
lrg_collider_component_set_mask (player_collider, LAYER_ENEMY | LAYER_NPC);

/* Ally: same collision as player */
lrg_collider_component_set_layer (ally_collider, LAYER_ALLY);
lrg_collider_component_set_mask (ally_collider, LAYER_ENEMY | LAYER_NPC);

/* Enemy: attacks player, allies, and NPCs */
lrg_collider_component_set_layer (enemy_collider, LAYER_ENEMY);
lrg_collider_component_set_mask (enemy_collider, LAYER_PLAYER | LAYER_ALLY | LAYER_NPC);

/* NPC: can be hit by player, allies, and enemies */
lrg_collider_component_set_layer (npc_collider, LAYER_NPC);
lrg_collider_component_set_mask (npc_collider, LAYER_PLAYER | LAYER_ALLY | LAYER_ENEMY);

/* Player bullet: only hits enemies */
lrg_collider_component_set_layer (player_bullet, LAYER_BULLET);
lrg_collider_component_set_mask (player_bullet, LAYER_ENEMY);
```

## Game Loop Integration

Typical collision detection loop:

```c
void
detect_collisions (LrgWorld *world)
{
    GList *objects = lrg_world_get_objects (world);

    for (GList *iter_a = objects; iter_a != NULL; iter_a = iter_a->next)
    {
        LrgGameObject *obj_a = LRG_GAME_OBJECT (iter_a->data);
        LrgColliderComponent *col_a =
            lrg_game_object_get_component_of_type (obj_a,
                                                   LrgColliderComponent,
                                                   LRG_TYPE_COLLIDER_COMPONENT);
        if (col_a == NULL) continue;

        for (GList *iter_b = iter_a->next; iter_b != NULL; iter_b = iter_b->next)
        {
            LrgGameObject *obj_b = LRG_GAME_OBJECT (iter_b->data);
            LrgColliderComponent *col_b =
                lrg_game_object_get_component_of_type (obj_b,
                                                       LrgColliderComponent,
                                                       LRG_TYPE_COLLIDER_COMPONENT);
            if (col_b == NULL) continue;

            /* Check layer/mask and bounds */
            if (lrg_collider_component_can_collide_with (col_a, col_b) &&
                lrg_collider_component_intersects (col_a, col_b))
            {
                on_collision (obj_a, obj_b);
            }
        }
    }

    g_list_free (objects);
}
```

## Related Types

- [LrgComponent](../component.md) - Base component class
- [LrgGameObject](../game-object.md) - Entity container
- `GrlRectangle` - Rectangle from graylib

## See Also

- [ECS Overview](../index.md) - Module overview
- [ECS Examples](../../examples/ecs-basics.md) - Comprehensive examples
