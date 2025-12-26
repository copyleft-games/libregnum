# LrgCollisionInfo

Collision information boxed type that contains detailed data about a collision between two rigid bodies.

## Overview

`LrgCollisionInfo` is a boxed type (not a GObject) that stores collision details including the colliding bodies, contact point, collision normal, and penetration depth. It is typically used in collision callbacks and event handlers.

## Construction

```c
LrgCollisionInfo *info = lrg_collision_info_new (body_a, body_b,
                                                  normal_x, normal_y,
                                                  penetration,
                                                  contact_x, contact_y);

/* Cleanup */
lrg_collision_info_free (info);
```

Parameters:

- `body_a`, `body_b`: The two bodies involved in the collision
- `normal_x`, `normal_y`: Collision normal vector (from A to B)
- `penetration`: How deeply the bodies are overlapping
- `contact_x`, `contact_y`: Location of the collision contact point

## Accessing Collision Data

### Colliding Bodies

```c
GObject *a = lrg_collision_info_get_body_a (info);
GObject *b = lrg_collision_info_get_body_b (info);

if (a != NULL)
{
    LrgRigidBody *body_a = LRG_RIGID_BODY (a);
    /* Use body_a */
}
```

### Collision Normal

The normal vector points from body A toward body B and indicates the direction of collision.

```c
gfloat nx, ny;
lrg_collision_info_get_normal (info, &nx, &ny);

g_print ("Collision normal: (%.2f, %.2f)\n", nx, ny);

/* Normalized length is always 1.0 */
gfloat length = sqrtf (nx * nx + ny * ny);
g_assert_cmpfloat (length, ==, 1.0f);
```

### Penetration Depth

How far the bodies are overlapping.

```c
gfloat penetration = lrg_collision_info_get_penetration (info);

if (penetration > 0.5f)
{
    g_print ("Deep collision: %.2f units\n", penetration);
}
```

### Contact Point

The location where the collision occurred.

```c
gfloat cx, cy;
lrg_collision_info_get_contact_point (info, &cx, &cy);

g_print ("Collision at (%.1f, %.1f)\n", cx, cy);
```

## Memory Management

`LrgCollisionInfo` is a boxed type with manual memory management:

```c
/* Create */
LrgCollisionInfo *info = lrg_collision_info_new (body_a, body_b,
                                                  1.0f, 0.0f,
                                                  0.5f,
                                                  100.0f, 100.0f);

/* Copy */
LrgCollisionInfo *copy = lrg_collision_info_copy (info);

/* Cleanup */
lrg_collision_info_free (info);
lrg_collision_info_free (copy);

/* Or use auto cleanup */
g_autoptr(LrgCollisionInfo) auto_info = info;  /* auto cleanup */
```

## Usage in Collision Handlers

### Direct Callback

```c
static void
on_collision (LrgRigidBody *body_a,
              LrgRigidBody *body_b,
              LrgCollisionInfo *info,
              gpointer user_data)
{
    gfloat nx, ny;
    gfloat penetration;
    gfloat cx, cy;

    lrg_collision_info_get_normal (info, &nx, &ny);
    penetration = lrg_collision_info_get_penetration (info);
    lrg_collision_info_get_contact_point (info, &cx, &cy);

    g_print ("Collision at (%.1f, %.1f):\n", cx, cy);
    g_print ("  Normal: (%.2f, %.2f)\n", nx, ny);
    g_print ("  Penetration: %.2f\n", penetration);
}
```

### In Physics Simulation

```c
static void
handle_collisions (LrgPhysicsWorld *world)
{
    GPtrArray *bodies = lrg_physics_world_get_bodies (world);

    /* Check each pair of bodies (simplified collision detection) */
    for (guint i = 0; i < bodies->len; i++)
    {
        LrgRigidBody *body_a = g_ptr_array_index (bodies, i);

        for (guint j = i + 1; j < bodies->len; j++)
        {
            LrgRigidBody *body_b = g_ptr_array_index (bodies, j);

            /* Check collision using raycast or other methods */
            /* If collision detected: */

            LrgCollisionInfo *info = lrg_collision_info_new (
                G_OBJECT (body_a),
                G_OBJECT (body_b),
                1.0f, 0.0f,    /* normal */
                0.5f,          /* penetration */
                50.0f, 100.0f  /* contact point */
            );

            /* Process collision */
            g_print ("Collision detected!\n");

            lrg_collision_info_free (info);
        }
    }
}
```

## Complete Example

```c
#include <libregnum.h>
#include <math.h>

typedef struct
{
    LrgRigidBody parent;
    gint collision_count;
} MyRigidBody;

static void
my_rigid_body_on_collision (LrgRigidBody *self,
                            LrgRigidBody *other,
                            gfloat        normal_x,
                            gfloat        normal_y)
{
    MyRigidBody *my_body = (MyRigidBody *)self;
    my_body->collision_count++;

    gfloat cx, cy;
    gfloat x, y;

    lrg_rigid_body_get_position (self, &x, &y);
    lrg_rigid_body_get_position (other, &cx, &cy);

    g_print ("Collision #%d\n", my_body->collision_count);
    g_print ("  Body A at (%.1f, %.1f)\n", x, y);
    g_print ("  Body B at (%.1f, %.1f)\n", cx, cy);
    g_print ("  Normal: (%.2f, %.2f)\n", normal_x, normal_y);

    /* Bounce response */
    gfloat vx, vy;
    lrg_rigid_body_get_velocity (self, &vx, &vy);

    /* Reflect velocity along normal */
    gfloat dot = vx * normal_x + vy * normal_y;
    vx -= 1.5f * dot * normal_x;
    vy -= 1.5f * dot * normal_y;

    lrg_rigid_body_set_velocity (self, vx, vy);
}

int main (void)
{
    g_autoptr(LrgPhysicsWorld) world = lrg_physics_world_new ();
    g_autoptr(LrgRigidBody) body1 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
    g_autoptr(LrgRigidBody) body2 = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

    /* Setup */
    lrg_physics_world_set_gravity (world, 0.0f, 0.0f);

    lrg_rigid_body_set_position (body1, 0.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body1, 20.0f, 20.0f);
    lrg_rigid_body_set_velocity (body1, 50.0f, 0.0f);

    lrg_rigid_body_set_position (body2, 100.0f, 0.0f);
    lrg_rigid_body_set_box_shape (body2, 20.0f, 20.0f);

    lrg_physics_world_add_body (world, body1);
    lrg_physics_world_add_body (world, body2);

    /* Simulate collision */
    for (gint i = 0; i < 200; i++)
    {
        lrg_physics_world_step (world, 1.0f / 60.0f);
    }

    return 0;
}
```

## Type Information

```c
GType type = lrg_collision_info_get_type ();
g_assert_true (G_TYPE_IS_BOXED (type));
g_assert_cmpstr (g_type_name (type), ==, "LrgCollisionInfo");
```
