# ECS Basics - Comprehensive Examples

## Overview

This document provides complete working examples of using the ECS (Entity-Component-System) module in Libregnum. The examples progress from basic setup to complex scenarios.

## Example 1: Simple Game Object Creation

Creating a basic game object and adding it to a world.

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    /* Create a world */
    g_autoptr(LrgWorld) world = lrg_world_new ();

    /* Create a game object at specific position */
    g_autoptr(LrgGameObject) player = lrg_game_object_new_at (100.0f, 150.0f);

    /* Tag for easy lookup */
    grl_entity_set_tag (GRL_ENTITY (player), "player");

    /* Add to world */
    lrg_world_add_object (world, player);

    /* Verify it's in the world */
    g_assert_cmpuint (lrg_world_get_object_count (world), ==, 1);
    LrgGameObject *found = lrg_world_find_by_tag (world, "player");
    g_assert_true (found == player);

    g_print ("Player created at (%f, %f)\n",
             grl_entity_get_x (GRL_ENTITY (player)),
             grl_entity_get_y (GRL_ENTITY (player)));

    return 0;
}
```

Compile with:
```bash
gcc `pkg-config --cflags libregnum` -o example1 example1.c `pkg-config --libs libregnum`
```

## Example 2: Game Object with Components

Building a sprite-based game object with multiple components.

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(LrgWorld) world = lrg_world_new ();

    /* Create player object */
    g_autoptr(LrgGameObject) player = lrg_game_object_new_at (640.0f, 360.0f);
    grl_entity_set_tag (GRL_ENTITY (player), "player");

    /* Create and attach sprite component */
    g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
    GrlTexture *player_texture = grl_texture_new_from_file ("assets/player.png", NULL);
    lrg_sprite_component_set_texture (sprite, player_texture);
    lrg_game_object_add_component (player, LRG_COMPONENT (sprite));

    /* Create and attach collider component */
    g_autoptr(LrgColliderComponent) collider =
        lrg_collider_component_new_with_bounds (8.0f, 8.0f, 16.0f, 16.0f);
    lrg_game_object_add_component (player, LRG_COMPONENT (collider));

    /* Create and attach animator component */
    g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
    lrg_animator_component_set_texture (animator, player_texture, 32, 32);
    lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
    lrg_animator_component_add_animation (animator, "walk", 4, 4, 15.0f, TRUE);
    lrg_game_object_add_component (player, LRG_COMPONENT (animator));

    /* Add player to world */
    lrg_world_add_object (world, player);

    /* Verify components */
    g_assert_true (lrg_game_object_has_component (player, LRG_TYPE_SPRITE_COMPONENT));
    g_assert_true (lrg_game_object_has_component (player, LRG_TYPE_COLLIDER_COMPONENT));
    g_assert_true (lrg_game_object_has_component (player, LRG_TYPE_ANIMATOR_COMPONENT));
    g_assert_cmpuint (lrg_game_object_get_component_count (player), ==, 3);

    /* Start animation */
    lrg_animator_component_play (animator, "idle");

    g_print ("Player created with %u components\n",
             lrg_game_object_get_component_count (player));

    return 0;
}
```

## Example 3: Multiple Objects and Collision Detection

Creating multiple game objects and detecting collisions.

```c
#include <libregnum.h>

typedef struct
{
    LrgWorld *world;
    gint collisions_detected;
} GameState;

void
detect_collisions (GameState *state)
{
    GList *objects = lrg_world_get_objects (state->world);

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

            /* Check collision */
            if (lrg_collider_component_can_collide_with (col_a, col_b) &&
                lrg_collider_component_intersects (col_a, col_b))
            {
                const gchar *tag_a = grl_entity_get_tag (GRL_ENTITY (obj_a));
                const gchar *tag_b = grl_entity_get_tag (GRL_ENTITY (obj_b));

                g_print ("Collision: %s <-> %s\n", tag_a, tag_b);
                state->collisions_detected++;
            }
        }
    }

    g_list_free (objects);
}

int
main (int argc, char *argv[])
{
    GameState state = {
        .world = lrg_world_new (),
        .collisions_detected = 0
    };

    /* Create player */
    g_autoptr(LrgGameObject) player = lrg_game_object_new_at (100.0f, 100.0f);
    grl_entity_set_tag (GRL_ENTITY (player), "player");
    g_autoptr(LrgColliderComponent) player_col =
        lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);
    lrg_collider_component_set_layer (player_col, 1);
    lrg_collider_component_set_mask (player_col, 2 | 4);  /* Enemy | Hazard */
    lrg_game_object_add_component (player, LRG_COMPONENT (player_col));
    lrg_world_add_object (state.world, player);

    /* Create enemy 1 */
    g_autoptr(LrgGameObject) enemy1 = lrg_game_object_new_at (120.0f, 110.0f);
    grl_entity_set_tag (GRL_ENTITY (enemy1), "enemy1");
    g_autoptr(LrgColliderComponent) enemy1_col =
        lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);
    lrg_collider_component_set_layer (enemy1_col, 2);
    lrg_collider_component_set_mask (enemy1_col, 1);  /* Player */
    lrg_game_object_add_component (enemy1, LRG_COMPONENT (enemy1_col));
    lrg_world_add_object (state.world, enemy1);

    /* Create enemy 2 (too far, no collision) */
    g_autoptr(LrgGameObject) enemy2 = lrg_game_object_new_at (200.0f, 200.0f);
    grl_entity_set_tag (GRL_ENTITY (enemy2), "enemy2");
    g_autoptr(LrgColliderComponent) enemy2_col =
        lrg_collider_component_new_with_bounds (0.0f, 0.0f, 32.0f, 32.0f);
    lrg_collider_component_set_layer (enemy2_col, 2);
    lrg_collider_component_set_mask (enemy2_col, 1);
    lrg_game_object_add_component (enemy2, LRG_COMPONENT (enemy2_col));
    lrg_world_add_object (state.world, enemy2);

    /* Detect collisions */
    detect_collisions (&state);

    g_print ("Detected %d collision(s)\n", state.collisions_detected);
    g_assert_cmpint (state.collisions_detected, ==, 1);  /* Only player-enemy1 */

    g_clear_object (&state.world);
    return 0;
}
```

## Example 4: Transform Hierarchies

Building a character with parent/child transform relationships.

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    /* Create character hierarchy
       Body (root at 100, 100)
       ├─ Left Arm (relative to body)
       └─ Right Arm (relative to body)
    */

    /* Body as root */
    g_autoptr(LrgTransformComponent) body =
        lrg_transform_component_new_at (100.0f, 100.0f);

    /* Left arm as child of body */
    g_autoptr(LrgTransformComponent) left_arm =
        lrg_transform_component_new_at (-15.0f, 0.0f);
    lrg_transform_component_set_parent (left_arm, body);

    /* Right arm as child of body */
    g_autoptr(LrgTransformComponent) right_arm =
        lrg_transform_component_new_at (15.0f, 0.0f);
    lrg_transform_component_set_parent (right_arm, body);

    /* Print world positions */
    g_autoptr(GrlVector2) body_world = lrg_transform_component_get_world_position (body);
    g_autoptr(GrlVector2) left_world = lrg_transform_component_get_world_position (left_arm);
    g_autoptr(GrlVector2) right_world = lrg_transform_component_get_world_position (right_arm);

    g_print ("Body world pos:      (%f, %f)\n", body_world->x, body_world->y);
    g_print ("Left arm world pos:  (%f, %f)\n", left_world->x, left_world->y);
    g_print ("Right arm world pos: (%f, %f)\n", right_world->x, right_world->y);

    /* Move body - children move automatically */
    lrg_transform_component_set_local_position_xy (body, 200.0f, 150.0f);

    g_clear_pointer (&body_world, grl_vector2_free);
    g_clear_pointer (&left_world, grl_vector2_free);
    g_clear_pointer (&right_world, grl_vector2_free);

    body_world = lrg_transform_component_get_world_position (body);
    left_world = lrg_transform_component_get_world_position (left_arm);
    right_world = lrg_transform_component_get_world_position (right_arm);

    g_print ("\nAfter moving body:\n");
    g_print ("Body world pos:      (%f, %f)\n", body_world->x, body_world->y);
    g_print ("Left arm world pos:  (%f, %f)\n", left_world->x, left_world->y);
    g_print ("Right arm world pos: (%f, %f)\n", right_world->x, right_world->y);

    return 0;
}
```

## Example 5: Animation Playback

Setting up and controlling sprite animations.

```c
#include <libregnum.h>

typedef struct
{
    LrgAnimatorComponent *animator;
    gfloat time_elapsed;
} AnimationState;

int
main (int argc, char *argv[])
{
    g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();

    /* Load sprite sheet */
    GrlTexture *texture = grl_texture_new_from_file ("assets/knight.png", NULL);
    lrg_animator_component_set_texture (animator, texture, 32, 32);

    /* Define animations */
    lrg_animator_component_add_animation (animator, "idle", 0, 4, 8.0f, TRUE);
    lrg_animator_component_add_animation (animator, "walk", 4, 4, 12.0f, TRUE);
    lrg_animator_component_add_animation (animator, "slash", 8, 3, 15.0f, FALSE);
    lrg_animator_component_add_animation (animator, "hit", 11, 2, 10.0f, FALSE);

    /* Set default animation for transitions */
    lrg_animator_component_set_default_animation (animator, "idle");

    /* List all animations */
    GList *anim_names = lrg_animator_component_get_animation_names (animator);
    g_print ("Animations:\n");
    for (GList *iter = anim_names; iter != NULL; iter = iter->next)
    {
        g_print ("  - %s\n", (gchar *)iter->data);
    }
    g_list_free (anim_names);

    /* Play animation */
    lrg_animator_component_play (animator, "idle");
    g_assert_true (lrg_animator_component_is_playing (animator));
    g_assert_cmpstr (lrg_animator_component_get_current_animation (animator),
                    ==, "idle");

    /* Simulate frame updates */
    for (gint frame = 0; frame < 5; frame++)
    {
        gint current_frame = lrg_animator_component_get_current_frame (animator);
        g_autoptr(GrlRectangle) frame_rect =
            lrg_animator_component_get_current_frame_rect (animator);

        g_print ("Frame %d: source rect (%d, %d, %d, %d)\n",
                 frame, (gint)frame_rect->x, (gint)frame_rect->y,
                 (gint)frame_rect->width, (gint)frame_rect->height);
    }

    /* Change animation */
    lrg_animator_component_play (animator, "slash");
    g_assert_cmpstr (lrg_animator_component_get_current_animation (animator),
                    ==, "slash");
    g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 8);

    /* Pause and resume */
    lrg_animator_component_pause (animator);
    g_assert_false (lrg_animator_component_is_playing (animator));

    lrg_animator_component_resume (animator);
    g_assert_true (lrg_animator_component_is_playing (animator));

    /* Stop */
    lrg_animator_component_stop (animator);
    g_assert_false (lrg_animator_component_is_playing (animator));
    g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 0);

    return 0;
}
```

## Example 6: Complete Game Loop

A minimal game loop with input, update, and render phases.

```c
#include <libregnum.h>

typedef struct
{
    LrgWorld *world;
    LrgGameObject *player;
    gfloat player_velocity_x;
    gboolean running;
} GameState;

void
on_input (GameState *state)
{
    /* Simplified input handling */
    /* In real code, use input system */
    state->player_velocity_x = 0.0f;
}

void
update_player (GameState *state, gfloat delta)
{
    LrgSpriteComponent *sprite =
        lrg_game_object_get_component_of_type (state->player,
                                               LrgSpriteComponent,
                                               LRG_TYPE_SPRITE_COMPONENT);

    /* Update position */
    gfloat x = grl_entity_get_x (GRL_ENTITY (state->player));
    grl_entity_set_x (GRL_ENTITY (state->player), x + state->player_velocity_x * delta);

    /* Update animation based on velocity */
    LrgAnimatorComponent *animator =
        lrg_game_object_get_component_of_type (state->player,
                                               LrgAnimatorComponent,
                                               LRG_TYPE_ANIMATOR_COMPONENT);

    if (state->player_velocity_x != 0.0f)
    {
        lrg_animator_component_play_if_different (animator, "walk");

        /* Face direction */
        if (state->player_velocity_x < 0.0f)
            lrg_sprite_component_set_flip_h (sprite, TRUE);
        else
            lrg_sprite_component_set_flip_h (sprite, FALSE);
    }
    else
    {
        lrg_animator_component_play_if_different (animator, "idle");
    }
}

void
update_game (GameState *state, gfloat delta)
{
    on_input (state);
    update_player (state, delta);
    lrg_world_update (state->world, delta);
}

void
render_game (GameState *state)
{
    /* Clear screen (handled by graylib) */
    lrg_world_draw (state->world);
    /* Swap buffers (handled by graylib) */
}

int
main (int argc, char *argv[])
{
    GameState state = {
        .world = lrg_world_new (),
        .player_velocity_x = 0.0f,
        .running = TRUE
    };

    /* Create player */
    state.player = lrg_game_object_new_at (320.0f, 240.0f);
    grl_entity_set_tag (GRL_ENTITY (state.player), "player");

    /* Add sprite */
    g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
    lrg_game_object_add_component (state.player, LRG_COMPONENT (sprite));

    /* Add animator */
    g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
    lrg_animator_component_add_animation (animator, "idle", 0, 4, 8.0f, TRUE);
    lrg_animator_component_add_animation (animator, "walk", 4, 4, 12.0f, TRUE);
    lrg_game_object_add_component (state.player, LRG_COMPONENT (animator));

    lrg_animator_component_play (animator, "idle");

    /* Add to world */
    lrg_world_add_object (state.world, state.player);

    /* Main loop (simplified - no actual rendering) */
    gfloat delta = 0.016f;  /* 60 FPS */
    for (gint frame = 0; frame < 60 && state.running; frame++)
    {
        update_game (&state, delta);
        render_game (&state);
    }

    g_clear_object (&state.world);
    return 0;
}
```

## Example 7: Custom Component

Creating a custom damage/health component.

```c
#include <libregnum.h>

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

static void
my_health_component_attached (LrgComponent  *self,
                              LrgGameObject *owner)
{
    MyHealthComponent *health = MY_HEALTH_COMPONENT (self);
    health->health = health->max_health;
    g_print ("Entity %s gained %d health\n",
             grl_entity_get_tag (GRL_ENTITY (owner)), health->health);
}

static void
my_health_component_update (LrgComponent *self, gfloat delta)
{
    /* Update health-related logic */
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

MyHealthComponent *
my_health_component_new (gint max_health)
{
    MyHealthComponent *self = g_object_new (MY_TYPE_HEALTH_COMPONENT, NULL);
    self->max_health = max_health;
    self->health = max_health;
    return self;
}

gint
my_health_component_get_health (MyHealthComponent *self)
{
    return self->health;
}

void
my_health_component_take_damage (MyHealthComponent *self, gint damage)
{
    self->health -= damage;
    if (self->health < 0)
        self->health = 0;

    LrgGameObject *owner = lrg_component_get_owner (LRG_COMPONENT (self));
    g_print ("%s took %d damage, health now: %d\n",
             grl_entity_get_tag (GRL_ENTITY (owner)), damage, self->health);
}

/* Example usage */
int
main (int argc, char *argv[])
{
    g_autoptr(LrgGameObject) player = lrg_game_object_new ();
    grl_entity_set_tag (GRL_ENTITY (player), "player");

    MyHealthComponent *health_comp = my_health_component_new (100);
    lrg_game_object_add_component (player, LRG_COMPONENT (health_comp));

    /* Damage the player */
    my_health_component_take_damage (health_comp, 25);
    g_assert_cmpint (my_health_component_get_health (health_comp), ==, 75);

    my_health_component_take_damage (health_comp, 100);
    g_assert_cmpint (my_health_component_get_health (health_comp), ==, 0);

    return 0;
}
```

## Performance Considerations

### Component Lookup
Component lookups via `lrg_game_object_get_component()` are O(n) where n is component count. Cache frequently accessed components:

```c
/* Bad: repeated lookups */
for (gint i = 0; i < 100; i++)
{
    LrgSpriteComponent *sprite =
        lrg_game_object_get_component_of_type (obj, LrgSpriteComponent,
                                               LRG_TYPE_SPRITE_COMPONENT);
    lrg_sprite_component_set_flip_h (sprite, (i % 2) == 0);
}

/* Good: cache */
LrgSpriteComponent *sprite =
    lrg_game_object_get_component_of_type (obj, LrgSpriteComponent,
                                           LRG_TYPE_SPRITE_COMPONENT);
for (gint i = 0; i < 100; i++)
{
    lrg_sprite_component_set_flip_h (sprite, (i % 2) == 0);
}
```

### Object Iteration
Iterating all objects in a world is O(n). Consider spatial partitioning for large worlds.

```c
/* Bad: O(n²) collision detection */
GList *objects = lrg_world_get_objects (world);
for (GList *i = objects; i != NULL; i = i->next)
{
    for (GList *j = objects; j != NULL; j = j->next)
    {
        /* Check collision */
    }
}

/* Better: skip already-checked pairs */
for (GList *i = objects; i != NULL; i = i->next)
{
    for (GList *j = i->next; j != NULL; j = j->next)
    {
        /* Check collision */
    }
}
```

### Component Counts
Keep per-object component counts reasonable. Typical objects have 3-5 components. Avoid attaching dozens of components to single objects.

## Related Documentation

- [ECS Module Overview](../modules/ecs/index.md)
- [LrgComponent](../modules/ecs/component.md)
- [LrgGameObject](../modules/ecs/game-object.md)
- [LrgWorld](../modules/ecs/world.md)
- [Component Types](../modules/ecs/components/)
