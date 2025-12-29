# 2D Trigger System Module

The 2D Trigger System provides area-based event triggers for gameplay mechanics like entering rooms, picking up items, activating switches, and scripted events.

## Overview

The trigger system extends the 3D trigger concepts to 2D with multiple shape types:

- **LrgTrigger2D** - Abstract base class for 2D triggers (derivable)
- **LrgTriggerRect** - Rectangle trigger zone
- **LrgTriggerCircle** - Circular trigger zone
- **LrgTriggerPolygon** - Arbitrary polygon trigger zone
- **LrgTriggerEvent** - Event data passed to callbacks (boxed)
- **LrgTriggerManager** - Manages all triggers and collision detection

## Quick Start

```c
/* Create a trigger manager */
LrgTriggerManager *manager = lrg_trigger_manager_new ();

/* Create a rectangular trigger */
LrgTriggerRect *door_trigger = lrg_trigger_rect_new ();
lrg_trigger_rect_set_bounds (door_trigger, 100.0f, 200.0f, 64.0f, 96.0f);
lrg_trigger2d_set_id (LRG_TRIGGER2D (door_trigger), "door-01");

/* Connect to triggered signal */
g_signal_connect (door_trigger, "triggered", G_CALLBACK (on_door_trigger), NULL);

/* Add to manager */
lrg_trigger_manager_add (manager, LRG_TRIGGER2D (door_trigger));

/* In game loop - test player position */
lrg_trigger_manager_test_point (manager, player_x, player_y, player_entity);
```

## Trigger Shapes

### Rectangle Trigger

```c
LrgTriggerRect *rect = lrg_trigger_rect_new ();
lrg_trigger_rect_set_bounds (rect, x, y, width, height);
```

### Circle Trigger

```c
LrgTriggerCircle *circle = lrg_trigger_circle_new ();
lrg_trigger_circle_set_center (circle, x, y);
lrg_trigger_circle_set_radius (circle, 50.0f);
```

### Polygon Trigger

```c
LrgTriggerPolygon *poly = lrg_trigger_polygon_new ();
gfloat points[] = { 0, 0, 100, 0, 100, 50, 50, 100, 0, 50 };
lrg_trigger_polygon_set_points (poly, points, 5);
```

## Event Types

| Event | Description |
|-------|-------------|
| `ENTER` | Entity entered the trigger zone |
| `STAY` | Entity is inside the trigger zone |
| `EXIT` | Entity left the trigger zone |

```c
static void
on_trigger (LrgTrigger2D     *trigger,
            LrgTrigger2DEventType event_type,
            gpointer          entity,
            gpointer          user_data)
{
    switch (event_type)
    {
    case LRG_TRIGGER2D_EVENT_ENTER:
        g_print ("Entered trigger: %s\n", lrg_trigger2d_get_id (trigger));
        break;
    case LRG_TRIGGER2D_EVENT_STAY:
        /* Called every frame while inside */
        break;
    case LRG_TRIGGER2D_EVENT_EXIT:
        g_print ("Exited trigger: %s\n", lrg_trigger2d_get_id (trigger));
        break;
    }
}
```

## Trigger Properties

| Property | Description |
|----------|-------------|
| `id` | Unique identifier for the trigger |
| `enabled` | Whether the trigger is active |
| `one-shot` | Fire only once, then disable |
| `cooldown` | Minimum time between activations |
| `collision-layer` | Which layer this trigger belongs to |
| `collision-mask` | Which layers can activate this trigger |

```c
lrg_trigger2d_set_enabled (trigger, TRUE);
lrg_trigger2d_set_one_shot (trigger, TRUE);  /* Only fires once */
lrg_trigger2d_set_cooldown (trigger, 2.0f);  /* 2 second cooldown */
lrg_trigger2d_set_collision_layer (trigger, 1);
lrg_trigger2d_set_collision_mask (trigger, 2);  /* Only layer 2 activates */
```

## Collision Layers

Use layers to control which entities can activate which triggers:

```c
/* Player is on layer 1 */
/* Enemies are on layer 2 */
/* Items are on layer 4 */

/* Door trigger: activated by player only */
lrg_trigger2d_set_collision_mask (door, 1);

/* Trap trigger: activated by player and enemies */
lrg_trigger2d_set_collision_mask (trap, 1 | 2);
```

## YAML Configuration

```yaml
type: TriggerRect
id: "door-01"
x: 100
y: 200
width: 64
height: 96
enabled: true
one-shot: false
cooldown: 0.5
collision-layer: 1
collision-mask: 1
```

```yaml
type: TriggerCircle
id: "pickup-zone"
center-x: 400
center-y: 300
radius: 32
one-shot: true
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgTrigger2D` | Abstract base (derivable) |
| `LrgTriggerRect` | Rectangle trigger |
| `LrgTriggerCircle` | Circle trigger |
| `LrgTriggerPolygon` | Polygon trigger |
| `LrgTriggerEvent` | Event data (boxed) |
| `LrgTriggerManager` | Trigger coordinator |

## Files

| File | Description |
|------|-------------|
| `src/trigger2d/lrg-trigger2d.h` | Base trigger class |
| `src/trigger2d/lrg-trigger-rect.h` | Rectangle trigger |
| `src/trigger2d/lrg-trigger-circle.h` | Circle trigger |
| `src/trigger2d/lrg-trigger-polygon.h` | Polygon trigger |
| `src/trigger2d/lrg-trigger-event.h` | Event boxed type |
| `src/trigger2d/lrg-trigger-manager.h` | Trigger manager |
