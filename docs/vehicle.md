---
title: Vehicle System
---

# Vehicle/Driving System

Libregnum's vehicle module provides arcade-style driving physics, traffic AI, and road networks for racing, driving, and open-world games.

> **[Home](index.md)** > Vehicle

## Overview

The vehicle system consists of 8 core classes:

| Class | Type | Description |
|-------|------|-------------|
| `LrgVehicle` | Derivable | Base vehicle with arcade physics |
| `LrgWheel` | Boxed | Wheel with suspension and grip data |
| `LrgVehicleController` | Final | Maps input to vehicle controls |
| `LrgVehicleCamera` | Derivable | Follow/hood/cockpit camera modes |
| `LrgVehicleAudio` | Final | Engine, tire, and horn sounds |
| `LrgRoad` | Boxed | Road segment with waypoints |
| `LrgRoadNetwork` | Final | Connected road system with pathfinding |
| `LrgTrafficAgent` | Derivable | AI-controlled traffic participant |

## Quick Start

```c
#include <libregnum.h>

/* Create a vehicle */
LrgVehicle *car = lrg_vehicle_new ();

/* Configure physics properties */
lrg_vehicle_set_max_speed (car, 50.0f);       /* units/second */
lrg_vehicle_set_acceleration (car, 15.0f);    /* units/s^2 */
lrg_vehicle_set_braking (car, 25.0f);
lrg_vehicle_set_mass (car, 1200.0f);          /* kg */
lrg_vehicle_set_drive_type (car, LRG_DRIVE_TYPE_REAR);

/* Setup standard 4-wheel configuration */
lrg_vehicle_setup_standard_wheels (car,
                                   2.5f,   /* wheelbase */
                                   1.6f,   /* track width */
                                   0.35f); /* wheel radius */

/* Set initial position */
lrg_vehicle_set_position (car, 0.0f, 0.0f, 0.0f);

/* Create a controller for input handling */
LrgVehicleController *controller = lrg_vehicle_controller_new ();
lrg_vehicle_controller_set_vehicle (controller, car);

/* In game loop: apply input and update */
lrg_vehicle_controller_set_input (controller,
                                  throttle,  /* 0-1 */
                                  brake,     /* 0-1 */
                                  steering); /* -1 to 1 */
lrg_vehicle_controller_update (controller, delta_time);
lrg_vehicle_update (car, delta_time);

/* Get vehicle state */
gfloat speed = lrg_vehicle_get_speed (car);
gfloat heading = lrg_vehicle_get_heading (car);

/* Clean up */
g_object_unref (controller);
g_object_unref (car);
```

## Core Concepts

### Vehicles

`LrgVehicle` provides arcade-style physics suitable for casual racing and driving games. The physics model prioritizes fun and responsiveness over simulation accuracy.

**Key Properties:**

| Property | Description | Default |
|----------|-------------|---------|
| `max-speed` | Maximum velocity (units/s) | 30.0 |
| `acceleration` | Acceleration rate (units/s^2) | 10.0 |
| `braking` | Braking deceleration | 20.0 |
| `mass` | Vehicle mass (kg) | 1000.0 |
| `max-steering-angle` | Max wheel turn (radians) | 0.5 |
| `drive-type` | FWD, RWD, or AWD | `LRG_DRIVE_TYPE_REAR` |

**Drive Types:**

```c
typedef enum {
    LRG_DRIVE_TYPE_FRONT,  /* Front-wheel drive */
    LRG_DRIVE_TYPE_REAR,   /* Rear-wheel drive */
    LRG_DRIVE_TYPE_ALL     /* All-wheel drive */
} LrgDriveType;
```

**Signals:**

| Signal | Description |
|--------|-------------|
| `collision` | Emitted on impact with `impact_force` parameter |
| `damaged` | Emitted when taking damage |
| `destroyed` | Emitted when health reaches zero |
| `entered` | Emitted when player enters vehicle |
| `exited` | Emitted when player exits vehicle |

### Wheels

`LrgWheel` is a boxed type representing a single wheel with suspension, grip, and runtime state.

```c
/* Create a custom wheel */
LrgWheel *wheel = lrg_wheel_new (1.0f,   /* offset_x (from center) */
                                 -0.3f,  /* offset_y (height) */
                                 1.2f,   /* offset_z (from center) */
                                 0.35f); /* radius */

/* Configure suspension */
lrg_wheel_set_suspension (wheel,
                          0.3f,    /* rest length */
                          50000.0f, /* stiffness */
                          4000.0f); /* damping */

/* Configure grip */
lrg_wheel_set_friction (wheel, 1.0f);
lrg_wheel_set_grip_multiplier (wheel, 1.0f); /* reduce for ice/wet */

/* Mark as drive/steering wheel */
lrg_wheel_set_is_drive_wheel (wheel, TRUE);
lrg_wheel_set_is_steering_wheel (wheel, TRUE);

/* Add to vehicle */
lrg_vehicle_add_wheel (car, wheel);
```

**Wheel State (Runtime):**

The wheel struct contains runtime state updated during physics:

| Field | Description |
|-------|-------------|
| `compression` | Suspension compression (0-1) |
| `rotation_angle` | Wheel spin in radians |
| `steering_angle` | Current steering angle |
| `slip_ratio` | Longitudinal slip (wheelspin) |
| `slip_angle` | Lateral slip angle |
| `is_grounded` | Whether touching ground |

### Vehicle Controller

`LrgVehicleController` translates player input into vehicle controls with smoothing and sensitivity options.

```c
LrgVehicleController *controller = lrg_vehicle_controller_new ();
lrg_vehicle_controller_set_vehicle (controller, car);

/* Configure sensitivity */
lrg_vehicle_controller_set_steering_sensitivity (controller, 1.5f);
lrg_vehicle_controller_set_throttle_sensitivity (controller, 1.0f);

/* Enable auto-centering for steering */
lrg_vehicle_controller_set_auto_center (controller, TRUE);
lrg_vehicle_controller_set_center_speed (controller, 5.0f);

/* Apply input each frame */
lrg_vehicle_controller_set_input (controller, throttle, brake, steering);
lrg_vehicle_controller_update (controller, delta);
```

### Vehicle Camera

`LrgVehicleCamera` provides multiple camera modes for vehicle gameplay:

```c
LrgVehicleCamera *camera = lrg_vehicle_camera_new ();
lrg_vehicle_camera_set_vehicle (camera, car);

/* Set camera mode */
lrg_vehicle_camera_set_mode (camera, LRG_VEHICLE_CAMERA_FOLLOW);

/* Configure follow mode */
lrg_vehicle_camera_set_follow_distance (camera, 8.0f);
lrg_vehicle_camera_set_follow_height (camera, 3.0f);
lrg_vehicle_camera_set_smoothing (camera, 5.0f);

/* Enable speed-based look-ahead */
lrg_vehicle_camera_set_look_ahead (camera, TRUE);
lrg_vehicle_camera_set_look_ahead_distance (camera, 5.0f);

/* Update each frame */
lrg_vehicle_camera_update (camera, delta);
```

**Camera Modes:**

```c
typedef enum {
    LRG_VEHICLE_CAMERA_FOLLOW,   /* Third-person chase camera */
    LRG_VEHICLE_CAMERA_HOOD,     /* Hood/bonnet camera */
    LRG_VEHICLE_CAMERA_COCKPIT,  /* First-person interior */
    LRG_VEHICLE_CAMERA_FREE      /* Free-look around vehicle */
} LrgVehicleCameraMode;
```

### Vehicle Audio

`LrgVehicleAudio` manages engine, tire, and effect sounds:

```c
LrgVehicleAudio *audio = lrg_vehicle_audio_new ();
lrg_vehicle_audio_set_vehicle (audio, car);

/* Set sound assets */
lrg_vehicle_audio_set_engine_sound (audio, "engine_v8_loop");
lrg_vehicle_audio_set_tire_screech_sound (audio, "tire_screech");
lrg_vehicle_audio_set_horn_sound (audio, "horn_default");
lrg_vehicle_audio_set_collision_sound (audio, "crash_metal");

/* Configure engine sound */
lrg_vehicle_audio_set_engine_rpm_range (audio, 800.0f, 7000.0f);
lrg_vehicle_audio_set_engine_pitch_range (audio, 0.8f, 2.0f);

/* Volume controls */
lrg_vehicle_audio_set_master_volume (audio, 1.0f);
lrg_vehicle_audio_set_engine_volume (audio, 0.8f);
lrg_vehicle_audio_set_effects_volume (audio, 1.0f);

/* Start audio (call when vehicle starts) */
lrg_vehicle_audio_start (audio);

/* Update each frame (adjusts pitch/volume based on vehicle state) */
lrg_vehicle_audio_update (audio, delta);

/* Play effects */
lrg_vehicle_audio_play_horn (audio);
lrg_vehicle_audio_play_collision (audio, 0.8f); /* intensity 0-1 */
```

## Road Network & Traffic

### Roads

`LrgRoad` represents a road segment with waypoints:

```c
/* Create a road */
LrgRoad *main_street = lrg_road_new ("main_street");

/* Add waypoints */
lrg_road_add_waypoint (main_street, 0.0f, 0.0f, 0.0f, 10.0f, 30.0f);
lrg_road_add_waypoint (main_street, 50.0f, 0.0f, 0.0f, 10.0f, 30.0f);
lrg_road_add_waypoint (main_street, 100.0f, 0.0f, 20.0f, 8.0f, 25.0f);
/* x, y, z, width, speed_limit */

/* Get road properties */
gfloat length = lrg_road_get_length (main_street);

/* Interpolate position along road (t = 0 to 1) */
gfloat x, y, z, heading;
lrg_road_interpolate (main_street, 0.5f, &x, &y, &z, &heading);
```

### Road Network

`LrgRoadNetwork` manages connected roads with pathfinding:

```c
LrgRoadNetwork *network = lrg_road_network_new ();

/* Add roads (takes ownership) */
lrg_road_network_add_road (network, main_street);
lrg_road_network_add_road (network, side_road);

/* Connect roads at endpoints */
lrg_road_network_connect (network,
                          "main_street", TRUE,   /* from end */
                          "side_road", FALSE);   /* to start */

/* Find route between points */
GList *route = NULL;
gboolean found = lrg_road_network_find_route (network,
                                              "main_street", 0.0f,
                                              "side_road", 1.0f,
                                              &route);
if (found)
{
    /* route contains list of road IDs */
    gfloat total_length = lrg_road_network_get_route_length (network, route);
    g_list_free (route);
}

/* Find nearest road to a point */
const gchar *road_id;
gfloat t, distance;
lrg_road_network_get_nearest_road (network, x, y, z,
                                   &road_id, &t, &distance);

/* Get random spawn point */
gfloat spawn_x, spawn_y, spawn_z, spawn_heading;
lrg_road_network_get_random_spawn_point (network,
                                         &spawn_x, &spawn_y, &spawn_z,
                                         &spawn_heading,
                                         &road_id, &t);
```

### Traffic Agents

`LrgTrafficAgent` provides AI-controlled vehicles:

```c
/* Create traffic agent */
LrgTrafficAgent *agent = lrg_traffic_agent_new ();

/* Assign a vehicle */
LrgVehicle *traffic_car = lrg_vehicle_new ();
/* ... configure vehicle ... */
lrg_traffic_agent_set_vehicle (agent, traffic_car);

/* Set road network for navigation */
lrg_traffic_agent_set_road_network (agent, network);

/* Configure behavior */
lrg_traffic_agent_set_behavior (agent, LRG_TRAFFIC_BEHAVIOR_NORMAL);
lrg_traffic_agent_set_max_speed (agent, 25.0f);

/* Set destination */
lrg_traffic_agent_set_destination (agent, "side_road", 0.8f);
/* Or random destination */
lrg_traffic_agent_set_random_destination (agent);

/* Configure obstacle avoidance */
lrg_traffic_agent_set_obstacle_detection_range (agent, 20.0f);

/* Start driving */
lrg_traffic_agent_start (agent);

/* Update each frame */
lrg_traffic_agent_update (agent, delta);

/* Check state */
LrgTrafficState state = lrg_traffic_agent_get_state (agent);
if (state == LRG_TRAFFIC_STATE_ARRIVED)
{
    /* Pick new destination */
    lrg_traffic_agent_set_random_destination (agent);
}
```

**Traffic Behaviors:**

```c
typedef enum {
    LRG_TRAFFIC_BEHAVIOR_CALM,       /* Slow, careful driver */
    LRG_TRAFFIC_BEHAVIOR_NORMAL,     /* Average driver */
    LRG_TRAFFIC_BEHAVIOR_AGGRESSIVE  /* Fast, risky driver */
} LrgTrafficBehavior;
```

**Traffic States:**

```c
typedef enum {
    LRG_TRAFFIC_STATE_IDLE,      /* Stationary */
    LRG_TRAFFIC_STATE_DRIVING,   /* Following road */
    LRG_TRAFFIC_STATE_STOPPED,   /* At traffic light/obstacle */
    LRG_TRAFFIC_STATE_AVOIDING,  /* Avoiding obstacle */
    LRG_TRAFFIC_STATE_ARRIVED    /* Reached destination */
} LrgTrafficState;
```

**Signals:**

| Signal | Description |
|--------|-------------|
| `destination-reached` | Emitted when agent reaches destination |
| `obstacle-detected` | Emitted when obstacle detected (with distance) |

## Complete Example

A simple driving scene:

```c
#include <libregnum.h>

typedef struct {
    LrgVehicle          *player_car;
    LrgVehicleController *controller;
    LrgVehicleCamera    *camera;
    LrgVehicleAudio     *audio;
    LrgRoadNetwork      *roads;
    GPtrArray           *traffic;
} DrivingScene;

static DrivingScene *
driving_scene_new (void)
{
    DrivingScene *scene = g_new0 (DrivingScene, 1);

    /* Create player vehicle */
    scene->player_car = lrg_vehicle_new ();
    lrg_vehicle_set_max_speed (scene->player_car, 60.0f);
    lrg_vehicle_set_acceleration (scene->player_car, 20.0f);
    lrg_vehicle_set_drive_type (scene->player_car, LRG_DRIVE_TYPE_REAR);
    lrg_vehicle_setup_standard_wheels (scene->player_car, 2.5f, 1.6f, 0.35f);

    /* Controller */
    scene->controller = lrg_vehicle_controller_new ();
    lrg_vehicle_controller_set_vehicle (scene->controller, scene->player_car);
    lrg_vehicle_controller_set_auto_center (scene->controller, TRUE);

    /* Camera */
    scene->camera = lrg_vehicle_camera_new ();
    lrg_vehicle_camera_set_vehicle (scene->camera, scene->player_car);
    lrg_vehicle_camera_set_mode (scene->camera, LRG_VEHICLE_CAMERA_FOLLOW);
    lrg_vehicle_camera_set_follow_distance (scene->camera, 10.0f);
    lrg_vehicle_camera_set_follow_height (scene->camera, 4.0f);

    /* Audio */
    scene->audio = lrg_vehicle_audio_new ();
    lrg_vehicle_audio_set_vehicle (scene->audio, scene->player_car);
    lrg_vehicle_audio_set_engine_sound (scene->audio, "engine_sports");
    lrg_vehicle_audio_start (scene->audio);

    /* Road network */
    scene->roads = lrg_road_network_new ();
    /* ... add roads ... */

    /* Spawn traffic */
    scene->traffic = g_ptr_array_new_with_free_func (g_object_unref);
    for (int i = 0; i < 10; i++)
    {
        LrgTrafficAgent *agent = lrg_traffic_agent_new ();
        LrgVehicle *car = lrg_vehicle_new ();
        lrg_vehicle_setup_standard_wheels (car, 2.4f, 1.5f, 0.33f);

        lrg_traffic_agent_set_vehicle (agent, car);
        lrg_traffic_agent_set_road_network (agent, scene->roads);
        lrg_traffic_agent_set_random_destination (agent);
        lrg_traffic_agent_start (agent);

        g_ptr_array_add (scene->traffic, agent);
        g_object_unref (car);
    }

    return scene;
}

static void
driving_scene_update (DrivingScene *scene,
                      gfloat        delta,
                      gfloat        throttle,
                      gfloat        brake,
                      gfloat        steering)
{
    /* Update player */
    lrg_vehicle_controller_set_input (scene->controller,
                                      throttle, brake, steering);
    lrg_vehicle_controller_update (scene->controller, delta);
    lrg_vehicle_update (scene->player_car, delta);
    lrg_vehicle_camera_update (scene->camera, delta);
    lrg_vehicle_audio_update (scene->audio, delta);

    /* Update traffic */
    for (guint i = 0; i < scene->traffic->len; i++)
    {
        LrgTrafficAgent *agent = g_ptr_array_index (scene->traffic, i);
        lrg_traffic_agent_update (agent, delta);

        /* Re-route arrived agents */
        if (lrg_traffic_agent_get_state (agent) == LRG_TRAFFIC_STATE_ARRIVED)
            lrg_traffic_agent_set_random_destination (agent);
    }
}

static void
driving_scene_free (DrivingScene *scene)
{
    lrg_vehicle_audio_stop (scene->audio);
    g_ptr_array_unref (scene->traffic);
    g_object_unref (scene->roads);
    g_object_unref (scene->audio);
    g_object_unref (scene->camera);
    g_object_unref (scene->controller);
    g_object_unref (scene->player_car);
    g_free (scene);
}
```

## Integration with Other Systems

### With ECS

Attach vehicles to entities:

```c
LrgEntity *car_entity = lrg_entity_new ();

/* Vehicle as component data */
LrgVehicle *vehicle = lrg_vehicle_new ();
lrg_entity_set_data (car_entity, "vehicle", vehicle);

/* Transform synced from vehicle */
void update_vehicle_entity (LrgEntity *entity, gfloat delta)
{
    LrgVehicle *v = lrg_entity_get_data (entity, "vehicle");
    gfloat x, y, z;
    lrg_vehicle_get_position (v, &x, &y, &z);
    lrg_entity_set_position (entity, x, y, z);
}
```

### With Save System

Vehicles implement serialization for save/load:

```c
/* Serialize vehicle state */
GBytes *save_data = lrg_vehicle_serialize (vehicle);

/* Later, restore */
lrg_vehicle_deserialize (vehicle, save_data);
```

## Performance Considerations

- **Physics Update Rate**: Call `lrg_vehicle_update()` at a fixed timestep (e.g., 60Hz) for consistent physics
- **Traffic Agents**: Each agent performs pathfinding; limit active agents based on distance to player
- **Audio**: `LrgVehicleAudio` manages sound resources; create one per audible vehicle
- **Road Network**: Pathfinding is cached; modify network infrequently

## API Reference

### Enums

| Enum | Values |
|------|--------|
| `LrgDriveType` | `FRONT`, `REAR`, `ALL` |
| `LrgVehicleCameraMode` | `FOLLOW`, `HOOD`, `COCKPIT`, `FREE` |
| `LrgTrafficBehavior` | `CALM`, `NORMAL`, `AGGRESSIVE` |
| `LrgTrafficState` | `IDLE`, `DRIVING`, `STOPPED`, `AVOIDING`, `ARRIVED` |

### Classes

| Class | Parent | Type |
|-------|--------|------|
| `LrgVehicle` | GObject | Derivable |
| `LrgWheel` | - | Boxed |
| `LrgVehicleController` | GObject | Final |
| `LrgVehicleCamera` | LrgCamera3D | Derivable |
| `LrgVehicleAudio` | GObject | Final |
| `LrgRoad` | - | Boxed |
| `LrgRoadNetwork` | GObject | Final |
| `LrgTrafficAgent` | GObject | Derivable |

## See Also

- [Economy System](economy.md) - For vehicle purchase/upgrade costs
- [Building System](building.md) - For garage/parking structures
- [Physics Module](modules/physics/) - For collision detection
- [Audio Module](modules/audio/) - For sound asset management
