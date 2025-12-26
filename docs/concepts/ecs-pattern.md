---
title: Entity-Component-System Pattern
concept: advanced
phase: 1
---

# Entity-Component-System Pattern

The ECS (Entity-Component-System) pattern is a game architecture that decouples entities, components, and systems for maximum flexibility and code reuse.

> **[Home](../../index.md)** > **[Concepts](index.md)** > ECS Pattern

## Overview

Traditional OOP game design uses deep inheritance hierarchies:

```
GameObject
  ├── Player
  │   ├── PlayerMage
  │   └── PlayerWarrior
  ├── Enemy
  │   ├── EnemyGoblin
  │   └── EnemyOrc
  └── Item
      ├── ItemSword
      └── ItemPotion
```

Problems: Deep nesting, code duplication, inflexibility, tight coupling.

ECS solves this with composition:

```
Entity (ID + Components)
  ├── Transform Component (position, rotation, scale)
  ├── Sprite Component (texture, animation)
  ├── Physics Component (velocity, mass, collider)
  └── Health Component (hp, damage)

System (updates all entities with specific components)
  ├── Physics System (updates entities with Physics component)
  ├── Rendering System (renders entities with Sprite component)
  └── Health System (manages entities with Health component)
```

## Core Concepts

### Entities

An entity is a container of components. In Libregnum:

```c
LrgGameObject *entity = lrg_game_object_new ();
```

Entities have unique IDs:

```c
guint id = lrg_game_object_get_id (entity);
```

Add multiple components to one entity:

```c
lrg_game_object_add_component (entity, LRG_COMPONENT (transform));
lrg_game_object_add_component (entity, LRG_COMPONENT (sprite));
lrg_game_object_add_component (entity, LRG_COMPONENT (collider));
```

### Components

Components are data containers attached to entities:

```c
/* Create component */
LrgTransformComponent *transform = lrg_transform_component_new ();
lrg_transform_component_set_position (transform, 10.0f, 20.0f);

/* Attach to entity */
lrg_game_object_add_component (entity, LRG_COMPONENT (transform));

/* Later, retrieve it */
LrgTransformComponent *t = LRG_TRANSFORM_COMPONENT (
    lrg_game_object_get_component (entity, LRG_TYPE_TRANSFORM_COMPONENT)
);
```

Built-in components:

- **TransformComponent** - Position, rotation, scale
- **SpriteComponent** - Texture and rendering
- **ColliderComponent** - Collision shapes
- **AnimatorComponent** - Animation state

### Systems

Systems process all entities with specific component combinations:

```c
/* Pseudo-code - actual implementation differs */
class PhysicsSystem
{
    void update(World world, float delta) {
        for each entity in world with PhysicsComponent {
            PhysicsComponent physics = entity.physics;
            TransformComponent transform = entity.transform;

            /* Update velocity from forces */
            physics.velocity += physics.force / physics.mass * delta;

            /* Update position from velocity */
            transform.position += physics.velocity * delta;
        }
    }
}
```

Systems are responsible for specific game behavior.

### World

The World contains all entities and coordinates systems:

```c
LrgWorld *world = lrg_world_new ();

/* Add entity to world */
lrg_world_add_object (world, entity);

/* Update all systems in world */
lrg_world_update (world, delta_time);
```

## Building with ECS

### Simple Player Example

```c
/* Create player entity */
LrgGameObject *player = lrg_game_object_new ();

/* Add position */
LrgTransformComponent *transform = lrg_transform_component_new ();
lrg_transform_component_set_position (transform, 100.0f, 100.0f);
lrg_game_object_add_component (player, LRG_COMPONENT (transform));

/* Add sprite */
LrgSpriteComponent *sprite = lrg_sprite_component_new ();
lrg_sprite_component_set_texture (sprite, texture);
lrg_game_object_add_component (player, LRG_COMPONENT (sprite));

/* Add physics */
LrgColliderComponent *collider = lrg_collider_component_new ();
lrg_collider_component_set_shape (collider, LRG_COLLISION_SHAPE_BOX, 32.0f, 32.0f);
lrg_game_object_add_component (player, LRG_COMPONENT (collider));

/* Add to world */
lrg_world_add_object (world, player);
```

### Complex Entity

```c
LrgGameObject *enemy = lrg_game_object_new ();

/* Position and graphics */
lrg_game_object_add_component (enemy, LRG_COMPONENT (transform));
lrg_game_object_add_component (enemy, LRG_COMPONENT (sprite));

/* Physics and collision */
lrg_game_object_add_component (enemy, LRG_COMPONENT (collider));

/* Animation */
LrgAnimatorComponent *animator = lrg_animator_component_new ();
lrg_animator_component_set_animation (animator, "idle");
lrg_game_object_add_component (enemy, LRG_COMPONENT (animator));

/* Custom health component (user-defined) */
lrg_game_object_add_component (enemy, LRG_COMPONENT (health));

/* AI component (future) */
lrg_game_object_add_component (enemy, LRG_COMPONENT (ai));

lrg_world_add_object (world, enemy);
```

## Advantages

### 1. Composition Over Inheritance

No deep hierarchies:

```c
/* Before ECS: Must choose class at creation */
class Soldier : public Character { };  /* Armed human */
class Mage : public Character { };      /* Spellcasting human */
class MageKnight : public Mage, public Soldier { };  /* Multiple inheritance! */

/* With ECS: Mix and match components */
LrgGameObject *warrior = create_entity_with(transform, sprite, weapon, armor);
LrgGameObject *mage = create_entity_with(transform, sprite, mana, spellbook);
LrgGameObject *mage_knight = create_entity_with(transform, sprite, weapon, armor, mana, spellbook);
```

### 2. Code Reuse

Systems apply to all entities:

```c
/* One physics system handles all physics entities */
PhysicsSystem handles:
  - Player (if has physics component)
  - Enemy (if has physics component)
  - Projectile (if has physics component)
  - Falling debris (if has physics component)
```

No code duplication across entity types.

### 3. Easy Extension

Add new behavior with new components and systems:

```c
/* New feature: particle effects */
/* Just create ParticleComponent and ParticleSystem */
/* Existing code unchanged! */
```

### 4. Performance

Systems process homogeneous data:

```c
/* Physics system processes memory in order */
for (i = 0; i < physics_entities.length; i++)
{
    PhysicsComponent *phys = physics_entities[i];
    /* Data locality - cache friendly */
    update_physics (phys);
}
```

Better cache locality than deep inheritance hierarchies.

### 5. Testability

Test components independently:

```c
/* Test transform independently */
LrgTransformComponent *t = create_test_component ();
lrg_transform_component_set_position (t, 10, 20);
assert (position matches);

/* Test system with specific entities */
LrgWorld *test_world = create_test_world ();
add_entities_with_components (test_world, ...);
physics_system_update (test_world, 0.016f);
assert (physics works correctly);
```

## Component Patterns

### Generic Component

Data-only, no special behavior:

```c
/* HealthComponent stores health data */
gint hp = lrg_health_component_get_hp (health);
gint max_hp = lrg_health_component_get_max_hp (health);
```

### Behavioral Component

Has methods that modify behavior:

```c
/* AnimatorComponent controls animation state */
lrg_animator_component_set_animation (animator, "walk");
lrg_animator_component_play (animator);

/* System updates animation based on animator state */
```

### Linked Component

References other components:

```c
/* WeaponComponent references AttackComponent */
LrgAttackComponent *attack = weapon->attack;

/* System uses both to determine damage */
```

## System Patterns

### Iteration System

Process all entities with specific components:

```c
/* Pseudo-code */
PhysicsSystem::update () {
    for each entity in world with PhysicsComponent {
        update entity physics
    }
}
```

### Event System

Respond to component events:

```c
/* Pseudo-code */
CollisionSystem::on_collision (entity_a, entity_b) {
    if entity_a has HealthComponent and entity_b has WeaponComponent {
        apply_damage (entity_a, get_damage (entity_b));
    }
}
```

### Interaction System

Multiple components interact:

```c
/* Pseudo-code */
MovementSystem::update () {
    for each entity with TransformComponent and InputComponent {
        InputComponent input = entity.input;
        TransformComponent transform = entity.transform;

        if input.moving_left:
            transform.position.x -= speed * delta

        if input.moving_right:
            transform.position.x += speed * delta
    }
}
```

## Challenges and Solutions

### Challenge: Component Communication

How does one component affect another?

**Solution 1: Through entity**
```c
LrgComponent *other = lrg_game_object_get_component (entity, TYPE);
```

**Solution 2: Through system**
```c
/* System mediates component interaction */
```

### Challenge: Serialization

Save/load ECS entities to YAML:

**Solution**: Registry maps components to types, DataLoader deserializes

```yaml
type: player
position: {x: 100, y: 200}
sprite:
  texture: player.png
  frame: 0
health:
  hp: 100
  max_hp: 100
```

### Challenge: Performance

Many entities with many components:

**Solution**: Use object pooling, spatial partitioning, lazy initialization

```c
/* Object pool for common entities */
GQueue *entity_pool = create_entity_pool (1000);

LrgGameObject *entity = g_queue_pop_head (entity_pool);
/* ... use entity ... */
reset_entity (entity);
g_queue_push_tail (entity_pool, entity);
```

## Migration from OOP

Migrating from class hierarchy to ECS:

### Before (OOP)

```c
class Player : public Character
{
    void update (float delta)
    {
        /* Movement logic */
        position += velocity * delta;

        /* Rendering logic */
        draw (texture, position);

        /* Physics logic */
        check_collisions ();
    }
};
```

### After (ECS)

```c
/* Split into systems */
MovementSystem::update (world, delta)
{
    for each entity with TransformComponent, InputComponent:
        apply_input_to_transform (entity);
}

RenderingSystem::update (world)
{
    for each entity with TransformComponent, SpriteComponent:
        draw_sprite (entity.sprite, entity.transform.position);
}

PhysicsSystem::update (world, delta)
{
    for each entity with ColliderComponent:
        check_collisions (entity);
}
```

## ECS + Data-Driven Design

Combine ECS with YAML:

```yaml
# entities/player.yaml
type: game-object
components:
  - type: transform
    position: {x: 100, y: 200}
    scale: {x: 1.0, y: 1.0}
  - type: sprite
    texture: player.png
    animation: idle
  - type: health
    hp: 100
    max_hp: 100
```

Load into ECS:

```c
GObject *obj = lrg_data_loader_load_file (loader, "entities/player.yaml", &error);
LrgGameObject *entity = LRG_GAME_OBJECT (obj);

/* Components automatically added during deserialization */
LrgTransformComponent *t = lrg_game_object_get_component (entity, ...);
```

## Complete Example

See the [ECS Example](../modules/core/examples/ecs-setup.c) for a complete working implementation.

## See Also

- [ECS Module Documentation](../modules/ecs/index.md) *(Phase 1)*
- [Architecture Overview](../architecture.md)
- [GObject Composition](https://developer.gnome.org/gobject/stable/chapter-gobject.html)
