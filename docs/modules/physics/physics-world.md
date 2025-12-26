# LrgPhysicsWorld

The physics world is the main container for all physics simulation. It manages rigid bodies, performs collision detection, and integrates the equations of motion.

## Overview

`LrgPhysicsWorld` is a derivable GObject type that can be subclassed to extend physics behavior. It is the central object for all physics operations in a game or simulation.

## Construction

```c
LrgPhysicsWorld *world = lrg_physics_world_new ();
```

## World Properties

### Gravity

Sets the acceleration applied to all dynamic bodies.

```c
/* Set gravity to earth-like (9.8 m/s² downward) */
lrg_physics_world_set_gravity (world, 0.0f, -9.8f);

/* Get current gravity */
gfloat gx, gy;
lrg_physics_world_get_gravity (world, &gx, &gy);

/* No gravity */
lrg_physics_world_set_gravity (world, 0.0f, 0.0f);
```

### Time Step

The fixed time step used for physics integration. Smaller values are more accurate but slower.

```c
/* 60 Hz physics (16.67 milliseconds per frame) */
lrg_physics_world_set_time_step (world, 1.0f / 60.0f);

gfloat step = lrg_physics_world_get_time_step (world);
```

### Constraint Iterations

The number of iterations used to solve velocity and position constraints. More iterations produce better results but are slower.

```c
/* Velocity constraint iterations (default: 8) */
lrg_physics_world_set_velocity_iterations (world, 10);
guint v_iter = lrg_physics_world_get_velocity_iterations (world);

/* Position constraint iterations (default: 3) */
lrg_physics_world_set_position_iterations (world, 4);
guint p_iter = lrg_physics_world_get_position_iterations (world);
```

## Body Management

### Adding Bodies

```c
LrgRigidBody *body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
lrg_rigid_body_set_position (body, 100.0f, 200.0f);
lrg_rigid_body_set_box_shape (body, 32.0f, 32.0f);

lrg_physics_world_add_body (world, body);
```

### Removing Bodies

```c
gboolean removed = lrg_physics_world_remove_body (world, body);
if (removed)
{
    g_print ("Body removed successfully\n");
}
```

### Querying Bodies

```c
/* Get total body count */
guint count = lrg_physics_world_get_body_count (world);

/* Get all bodies */
GPtrArray *bodies = lrg_physics_world_get_bodies (world);
for (guint i = 0; i < bodies->len; i++)
{
    LrgRigidBody *body = g_ptr_array_index (bodies, i);
    /* Process body */
}

/* Clear all bodies */
lrg_physics_world_clear (world);
```

## Simulation

### Stepping the Simulation

Call `lrg_physics_world_step()` each frame to advance the physics simulation.

```c
/* In game loop */
gfloat delta_time = get_frame_delta_time ();
lrg_physics_world_step (world, delta_time);
```

For consistent physics, use a fixed time step:

```c
static gfloat accumulator = 0.0f;
gfloat fixed_step = 1.0f / 60.0f;

accumulator += delta_time;
while (accumulator >= fixed_step)
{
    lrg_physics_world_step (world, fixed_step);
    accumulator -= fixed_step;
}
```

### Pausing Simulation

```c
/* Pause physics */
lrg_physics_world_set_paused (world, TRUE);

/* Check if paused */
if (lrg_physics_world_is_paused (world))
{
    g_print ("Physics is paused\n");
}

/* Resume */
lrg_physics_world_set_paused (world, FALSE);
```

When paused, `lrg_physics_world_step()` will not advance the simulation.

## Spatial Queries

### Raycast

Cast a ray and find the first body it hits.

```c
LrgRigidBody *hit_body = NULL;
gfloat hit_x, hit_y, hit_nx, hit_ny;

gboolean hit = lrg_physics_world_raycast (world,
                                          0.0f, 0.0f,      /* start */
                                          100.0f, 100.0f,  /* end */
                                          &hit_body,
                                          &hit_x, &hit_y,
                                          &hit_nx, &hit_ny);

if (hit)
{
    g_print ("Hit at (%.1f, %.1f), normal: (%.2f, %.2f)\n",
             hit_x, hit_y, hit_nx, hit_ny);
}
```

### AABB Query

Find all bodies overlapping an axis-aligned bounding box.

```c
GPtrArray *results = lrg_physics_world_query_aabb (world,
                                                   10.0f, 10.0f,   /* min */
                                                   100.0f, 100.0f); /* max */

for (guint i = 0; i < results->len; i++)
{
    LrgRigidBody *body = g_ptr_array_index (results, i);
    /* Process body */
}

g_ptr_array_unref (results);
```

### Point Query

Find all bodies containing a specific point.

```c
GPtrArray *results = lrg_physics_world_query_point (world, 50.0f, 50.0f);

for (guint i = 0; i < results->len; i++)
{
    LrgRigidBody *body = g_ptr_array_index (results, i);
    /* Body contains point (50, 50) */
}

g_ptr_array_unref (results);
```

## Virtual Methods

Subclasses can override these methods to customize physics behavior:

```c
/* Called before physics step */
void (*pre_step)  (LrgPhysicsWorld *self, gfloat delta_time);

/* Called after physics step */
void (*post_step) (LrgPhysicsWorld *self, gfloat delta_time);
```

Example implementation:

```c
static void
my_physics_world_pre_step (LrgPhysicsWorld *world,
                           gfloat           delta_time)
{
    /* Custom setup before physics */
    g_print ("Physics step starting (%.3f sec)\n", delta_time);
    LRG_PHYSICS_WORLD_CLASS (my_physics_world_parent_class)->pre_step (world, delta_time);
}
```

## Complete Example

```c
#include <libregnum.h>

int main (void)
{
    g_autoptr(LrgPhysicsWorld) world = lrg_physics_world_new ();
    g_autoptr(LrgRigidBody) ground = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    g_autoptr(LrgRigidBody) ball = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

    /* Setup world */
    lrg_physics_world_set_gravity (world, 0.0f, -9.8f);
    lrg_physics_world_set_time_step (world, 1.0f / 60.0f);

    /* Setup ground */
    lrg_rigid_body_set_position (ground, 0.0f, -50.0f);
    lrg_rigid_body_set_box_shape (ground, 200.0f, 20.0f);
    lrg_physics_world_add_body (world, ground);

    /* Setup ball */
    lrg_rigid_body_set_position (ball, 0.0f, 100.0f);
    lrg_rigid_body_set_circle_shape (ball, 10.0f);
    lrg_rigid_body_set_mass (ball, 1.0f);
    lrg_physics_world_add_body (world, ball);

    /* Simulate 10 seconds */
    for (gint frame = 0; frame < 600; frame++)
    {
        lrg_physics_world_step (world, 1.0f / 60.0f);

        gfloat x, y;
        lrg_rigid_body_get_position (ball, &x, &y);
        g_print ("Ball position: (%.1f, %.1f)\n", x, y);
    }

    return 0;
}
```

## Memory Management

`LrgPhysicsWorld` uses reference counting. Always unref when done:

```c
g_object_unref (world);
/* or use g_autoptr for automatic cleanup */
g_autoptr(LrgPhysicsWorld) world = lrg_physics_world_new ();
```
