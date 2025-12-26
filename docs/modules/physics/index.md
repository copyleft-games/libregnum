# Physics Module

The Physics module provides a 2D rigid body physics simulation system built on GObject principles. It allows you to create and simulate physical objects with realistic collision detection, forces, and constraints.

## Overview

The Physics module consists of three main components:

- **LrgPhysicsWorld**: The main physics simulation environment
- **LrgRigidBody**: Individual physical objects with mass, velocity, and collision shapes
- **LrgCollisionInfo**: Detailed information about collisions between bodies

## Core Concepts

### Rigid Bodies

A rigid body is a solid object that can move, rotate, and collide with other bodies. Each rigid body has three possible types:

- **Dynamic**: Affected by forces, gravity, and collisions. Used for movable objects like projectiles and characters.
- **Kinematic**: Moves according to velocity only, not affected by forces or gravity. Used for moving platforms and animated objects.
- **Static**: Does not move. Used for walls, floors, and other immovable obstacles.

### Collision Shapes

Each rigid body has a collision shape that defines its physical boundary:

- **Box**: Rectangular collision shape (width and height)
- **Circle**: Circular collision shape (radius)

### Physics World

The physics world is the container for all rigid bodies and manages the simulation. It provides:

- Gravity settings
- Time step configuration
- Collision detection and response
- Spatial queries (raycast, AABB, point)
- Constraint solver with configurable iterations

## Quick Start

```c
#include <libregnum.h>

/* Create physics world */
LrgPhysicsWorld *world = lrg_physics_world_new ();
lrg_physics_world_set_gravity (world, 0.0f, -9.8f);

/* Create a dynamic body */
LrgRigidBody *body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
lrg_rigid_body_set_mass (body, 1.0f);
lrg_rigid_body_set_box_shape (body, 32.0f, 32.0f);
lrg_rigid_body_set_position (body, 100.0f, 200.0f);

/* Add to world */
lrg_physics_world_add_body (world, body);

/* Simulation loop */
for (guint frame = 0; frame < num_frames; frame++)
{
    /* Update physics */
    lrg_physics_world_step (world, delta_time);

    /* Get updated position */
    gfloat x, y;
    lrg_rigid_body_get_position (body, &x, &y);
    /* Render at (x, y) */
}

g_object_unref (body);
g_object_unref (world);
```

## Common Patterns

### Applying Forces

```c
/* Direct impulse (instant velocity change) */
lrg_rigid_body_add_force (body, 10.0f, 0.0f, LRG_FORCE_MODE_IMPULSE);

/* Force applied over time */
lrg_rigid_body_add_force (body, 10.0f, 0.0f, LRG_FORCE_MODE_FORCE);

/* Direct velocity change */
lrg_rigid_body_add_force (body, 10.0f, 0.0f, LRG_FORCE_MODE_VELOCITY_CHANGE);

/* Force at a point (causes rotation) */
lrg_rigid_body_add_force_at_point (body, force_x, force_y, point_x, point_y, mode);
```

### Querying the World

```c
/* Raycast from point A to point B */
LrgRigidBody *hit_body;
gfloat hit_x, hit_y, hit_nx, hit_ny;
gboolean hit = lrg_physics_world_raycast (world,
                                          start_x, start_y,
                                          end_x, end_y,
                                          &hit_body, &hit_x, &hit_y,
                                          &hit_nx, &hit_ny);

/* Query axis-aligned bounding box */
GPtrArray *results = lrg_physics_world_query_aabb (world,
                                                   min_x, min_y,
                                                   max_x, max_y);

/* Query point */
GPtrArray *results = lrg_physics_world_query_point (world, x, y);
```

### Collision Detection

Implement collision handling by subclassing LrgRigidBody or using signals:

```c
typedef struct
{
    LrgRigidBody parent;
} MyRigidBody;

static void
my_rigid_body_on_collision (LrgRigidBody *self,
                            LrgRigidBody *other,
                            gfloat        normal_x,
                            gfloat        normal_y)
{
    /* Handle collision here */
    g_print ("Collision with normal: (%.2f, %.2f)\n", normal_x, normal_y);
}
```

## Performance Tuning

### Iterations

Higher constraint iterations produce more accurate results but are slower:

```c
/* Default is usually 3 */
lrg_physics_world_set_velocity_iterations (world, 8);
lrg_physics_world_set_position_iterations (world, 4);
```

### Sleeping

Bodies that are not moving are automatically put to sleep to save performance:

```c
/* Manually control sleep state */
if (lrg_rigid_body_is_sleeping (body))
{
    lrg_rigid_body_wake_up (body);
}
```

### Time Step

Use a fixed time step for consistent physics:

```c
/* 60 Hz physics */
lrg_physics_world_set_time_step (world, 1.0f / 60.0f);

/* In game loop */
delta_accumulator += delta_time;
while (delta_accumulator >= fixed_time_step)
{
    lrg_physics_world_step (world, fixed_time_step);
    delta_accumulator -= fixed_time_step;
}
```

## Module Files

- `/var/home/zach/Source/Projects/libregnum/src/physics/lrg-physics-world.h` - World management
- `/var/home/zach/Source/Projects/libregnum/src/physics/lrg-rigid-body.h` - Rigid body implementation
- `/var/home/zach/Source/Projects/libregnum/src/physics/lrg-collision-info.h` - Collision data

## See Also

- [LrgPhysicsWorld Documentation](./physics-world.md)
- [LrgRigidBody Documentation](./rigid-body.md)
- [LrgCollisionInfo Documentation](./collision-info.md)
