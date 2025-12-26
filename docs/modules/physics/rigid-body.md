# LrgRigidBody

A rigid body is a solid object that can move, rotate, and collide with other bodies in the physics world.

## Overview

`LrgRigidBody` is a derivable GObject type representing a single physical object. Each rigid body has mass, velocity, forces, and a collision shape. It can be dynamic (affected by physics), kinematic (moving but not affected by forces), or static (immovable).

## Construction

```c
/* Create a dynamic body (affected by gravity and collisions) */
LrgRigidBody *body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

/* Create a static body (immovable) */
LrgRigidBody *ground = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);

/* Create a kinematic body (moves but not affected by forces) */
LrgRigidBody *platform = lrg_rigid_body_new (LRG_RIGID_BODY_KINEMATIC);
```

## Body Type

Determines how the body is affected by physics:

```c
/* Get body type */
LrgRigidBodyType type = lrg_rigid_body_get_body_type (body);

/* Change body type */
lrg_rigid_body_set_body_type (body, LRG_RIGID_BODY_KINEMATIC);
```

### Types:
- `LRG_RIGID_BODY_DYNAMIC`: Affected by gravity, forces, and collisions
- `LRG_RIGID_BODY_STATIC`: Immovable, no physics applied
- `LRG_RIGID_BODY_KINEMATIC`: Moves via velocity, not affected by forces

## Physical Properties

### Mass

Controls how much the body resists acceleration.

```c
/* Default mass is 1.0 kg */
lrg_rigid_body_set_mass (body, 5.0f);
gfloat mass = lrg_rigid_body_get_mass (body);

/* For static bodies, mass is ignored */
```

### Restitution (Bounciness)

Controls how bouncy the body is. Range 0.0 (no bounce) to 1.0 (perfect bounce).

```c
lrg_rigid_body_set_restitution (body, 0.8f);  /* Very bouncy */
gfloat rest = lrg_rigid_body_get_restitution (body);
```

### Friction

Controls how much friction is applied during collisions. Range 0.0 (slippery) to 1.0 (maximum friction).

```c
lrg_rigid_body_set_friction (body, 0.5f);  /* Moderate friction */
gfloat friction = lrg_rigid_body_get_friction (body);
```

### Damping

Controls how quickly the body loses energy due to air resistance.

```c
/* Linear damping (affects linear velocity) */
lrg_rigid_body_set_linear_damping (body, 0.1f);
gfloat lin_damp = lrg_rigid_body_get_linear_damping (body);

/* Angular damping (affects rotation) */
lrg_rigid_body_set_angular_damping (body, 0.1f);
gfloat ang_damp = lrg_rigid_body_get_angular_damping (body);
```

### Gravity Scale

Multiplier for how much gravity affects this body.

```c
/* Normal gravity */
lrg_rigid_body_set_gravity_scale (body, 1.0f);

/* Half gravity */
lrg_rigid_body_set_gravity_scale (body, 0.5f);

/* No gravity (floating) */
lrg_rigid_body_set_gravity_scale (body, 0.0f);

gfloat scale = lrg_rigid_body_get_gravity_scale (body);
```

### Trigger Flag

Marks the body as a trigger (no physical response, only collision detection).

```c
/* Convert to trigger */
lrg_rigid_body_set_is_trigger (body, TRUE);

if (lrg_rigid_body_get_is_trigger (body))
{
    g_print ("This is a trigger body\n");
}
```

## Position and Rotation

### Position

```c
/* Set position (teleports the body) */
lrg_rigid_body_set_position (body, 100.0f, 200.0f);

/* Get position */
gfloat x, y;
lrg_rigid_body_get_position (body, &x, &y);
```

### Rotation

Angle in radians (0 = pointing right, π/2 = pointing up).

```c
/* Set rotation in radians */
lrg_rigid_body_set_rotation (body, 0.0f);  /* 0 degrees */
lrg_rigid_body_set_rotation (body, G_PI / 2.0f);  /* 90 degrees */

gfloat angle = lrg_rigid_body_get_rotation (body);
```

## Velocity

### Linear Velocity

```c
lrg_rigid_body_set_velocity (body, 50.0f, 0.0f);  /* Moving right */

gfloat vx, vy;
lrg_rigid_body_get_velocity (body, &vx, &vy);
```

### Angular Velocity

Rotation speed in radians per second.

```c
lrg_rigid_body_set_angular_velocity (body, 2.0f);  /* Rotating counter-clockwise */

gfloat ang_vel = lrg_rigid_body_get_angular_velocity (body);
```

## Forces and Impulses

### Force Modes

- `LRG_FORCE_MODE_FORCE`: Accumulates forces over time (F = ma)
- `LRG_FORCE_MODE_IMPULSE`: Instant change in momentum (J = m * Δv)
- `LRG_FORCE_MODE_VELOCITY_CHANGE`: Direct velocity change (skips mass)

### Adding Forces

```c
/* Apply an impulse (instant velocity change) */
lrg_rigid_body_add_force (body, 10.0f, 5.0f, LRG_FORCE_MODE_IMPULSE);

/* Apply continuous force */
lrg_rigid_body_add_force (body, 0.0f, -100.0f, LRG_FORCE_MODE_FORCE);

/* Direct velocity change */
lrg_rigid_body_add_force (body, 20.0f, 0.0f, LRG_FORCE_MODE_VELOCITY_CHANGE);
```

### Force at a Point

Applies force at a specific location, causing rotation.

```c
/* Apply force at center (no rotation) */
lrg_rigid_body_add_force_at_point (body, 10.0f, 0.0f, cx, cy, mode);

/* Apply force offset from center (causes rotation) */
gfloat body_x, body_y;
lrg_rigid_body_get_position (body, &body_x, &body_y);
lrg_rigid_body_add_force_at_point (body, 10.0f, 0.0f,
                                   body_x + 20.0f, body_y,  /* offset point */
                                   LRG_FORCE_MODE_IMPULSE);
```

### Torque

Apply rotational force directly.

```c
lrg_rigid_body_add_torque (body, 5.0f, LRG_FORCE_MODE_IMPULSE);
```

### Clearing Forces

```c
lrg_rigid_body_clear_forces (body);
```

## Collision Shapes

### Box Shape

```c
lrg_rigid_body_set_box_shape (body, 32.0f, 64.0f);  /* width, height */

gfloat w, h;
lrg_rigid_body_get_shape_bounds (body, &w, &h);
```

### Circle Shape

```c
lrg_rigid_body_set_circle_shape (body, 16.0f);  /* radius */

gfloat w, h;
lrg_rigid_body_get_shape_bounds (body, &w, &h);  /* diameter x diameter */
```

### Getting Shape Info

```c
LrgCollisionShape shape = lrg_rigid_body_get_shape_type (body);

if (shape == LRG_COLLISION_SHAPE_BOX)
{
    g_print ("Body is a box\n");
}
else if (shape == LRG_COLLISION_SHAPE_CIRCLE)
{
    g_print ("Body is a circle\n");
}
```

## Sleep State

Bodies at rest are automatically put to sleep to improve performance.

```c
if (lrg_rigid_body_is_sleeping (body))
{
    lrg_rigid_body_wake_up (body);
}

/* Manually put to sleep */
lrg_rigid_body_sleep (body);
```

## Collision Callbacks

Subclass LrgRigidBody to handle collisions:

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
    g_print ("Collision with normal: (%.2f, %.2f)\n", normal_x, normal_y);

    /* Can respond to collision here */
    gfloat vx, vy;
    lrg_rigid_body_get_velocity (self, &vx, &vy);
    g_print ("Our velocity: (%.2f, %.2f)\n", vx, vy);
}

static void
my_rigid_body_on_trigger (LrgRigidBody *self,
                          LrgRigidBody *other,
                          gboolean      entering)
{
    if (entering)
        g_print ("Entered trigger\n");
    else
        g_print ("Exited trigger\n");
}
```

## Complete Example

```c
#include <libregnum.h>

int main (void)
{
    g_autoptr(LrgPhysicsWorld) world = lrg_physics_world_new ();
    g_autoptr(LrgRigidBody) projectile = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

    /* Configure world */
    lrg_physics_world_set_gravity (world, 0.0f, -9.8f);

    /* Configure projectile */
    lrg_rigid_body_set_mass (projectile, 0.1f);
    lrg_rigid_body_set_circle_shape (projectile, 2.0f);
    lrg_rigid_body_set_position (projectile, 0.0f, 100.0f);
    lrg_rigid_body_set_restitution (projectile, 0.9f);
    lrg_rigid_body_set_friction (projectile, 0.1f);

    /* Launch projectile */
    lrg_rigid_body_set_velocity (projectile, 50.0f, 30.0f);

    /* Add to world */
    lrg_physics_world_add_body (world, projectile);

    /* Simulate */
    for (gint i = 0; i < 1000; i++)
    {
        lrg_physics_world_step (world, 1.0f / 60.0f);

        gfloat x, y;
        lrg_rigid_body_get_position (projectile, &x, &y);

        if (y < -100.0f) break;  /* Out of bounds */

        g_print ("Projectile at (%.1f, %.1f)\n", x, y);
    }

    return 0;
}
```
