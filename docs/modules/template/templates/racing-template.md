# Racing Templates

Libregnum provides two racing game templates for different perspectives: `LrgRacing2DTemplate` for top-down arcade racers and `LrgRacing3DTemplate` for 3D racing games.

## Inheritance Hierarchy

```
LrgGameTemplate
├── LrgGame2DTemplate
│   └── LrgRacing2DTemplate (derivable)
└── LrgGame3DTemplate
    └── LrgRacing3DTemplate (derivable)
```

---

# LrgRacing2DTemplate

A template for top-down 2D racing games like Micro Machines, early Grand Theft Auto, or arcade racers.

## Features

- Vehicle physics (acceleration, braking, steering, drift)
- Lap and checkpoint tracking
- Race state management (countdown, racing, finished)
- Speed boost system
- Surface types affecting handling
- Camera follow with look-ahead

## Quick Start (2D)

```c
#define MY_TYPE_RACER (my_racer_get_type ())
G_DECLARE_FINAL_TYPE (MyRacer, my_racer, MY, RACER, LrgRacing2DTemplate)

struct _MyRacer
{
    LrgRacing2DTemplate parent_instance;
    GrlTexture *car_texture;
};

G_DEFINE_TYPE (MyRacer, my_racer, LRG_TYPE_RACING_2D_TEMPLATE)

static void
my_racer_configure (LrgGameTemplate *template)
{
    LrgRacing2DTemplate *racing = LRG_RACING_2D_TEMPLATE (template);

    g_object_set (template,
                  "title", "Top Down Racer",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Vehicle settings */
    lrg_racing_2d_template_set_max_speed (racing, 300.0f);
    lrg_racing_2d_template_set_acceleration (racing, 200.0f);
    lrg_racing_2d_template_set_brake_power (racing, 400.0f);
    lrg_racing_2d_template_set_turn_speed (racing, 3.0f);
    lrg_racing_2d_template_set_grip (racing, 0.8f);
    lrg_racing_2d_template_set_drift_threshold (racing, 150.0f);

    /* Race settings */
    lrg_racing_2d_template_set_total_laps (racing, 3);
    lrg_racing_2d_template_set_total_checkpoints (racing, 8);

    /* Boost */
    lrg_racing_2d_template_set_boost_multiplier (racing, 1.5f);
}

static void
my_racer_class_init (MyRacerClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_racer_configure;
}
```

## Virtual Methods (2D)

```c
/* Called when race state changes */
void (*on_race_state_changed) (LrgRacing2DTemplate *self,
                               LrgRaceState         old_state,
                               LrgRaceState         new_state);

/* Called when a lap is completed */
void (*on_lap_complete) (LrgRacing2DTemplate *self,
                         guint                lap,
                         gfloat               lap_time);

/* Called when a checkpoint is passed */
void (*on_checkpoint_passed) (LrgRacing2DTemplate *self,
                              guint                checkpoint);

/* Called each second during countdown */
void (*on_countdown_tick) (LrgRacing2DTemplate *self,
                           gint                 count);

/* Called on collision */
void (*on_collision) (LrgRacing2DTemplate *self,
                      gfloat               impact_speed);

/* Updates vehicle physics */
void (*update_vehicle) (LrgRacing2DTemplate *self,
                        gdouble              delta);

/* Returns surface type at position */
LrgSurfaceType (*get_surface_at) (LrgRacing2DTemplate *self,
                                  gfloat               x,
                                  gfloat               y);

/* Renders the player vehicle */
void (*draw_vehicle) (LrgRacing2DTemplate *self);

/* Draws the race HUD */
void (*draw_race_ui) (LrgRacing2DTemplate *self);
```

## Race States

```c
typedef enum {
    LRG_RACE_STATE_WAITING,    /* Pre-race, waiting to start */
    LRG_RACE_STATE_COUNTDOWN,  /* 3, 2, 1, GO! */
    LRG_RACE_STATE_RACING,     /* Race in progress */
    LRG_RACE_STATE_FINISHED,   /* Race complete */
    LRG_RACE_STATE_PAUSED      /* Race paused */
} LrgRaceState;

/* Race control */
LrgRaceState state = lrg_racing_2d_template_get_race_state (template);
lrg_racing_2d_template_start_countdown (template);
lrg_racing_2d_template_start_race (template);
lrg_racing_2d_template_pause_race (template);
lrg_racing_2d_template_resume_race (template);
lrg_racing_2d_template_finish_race (template);
lrg_racing_2d_template_reset_race (template);
```

## Surface Types

```c
typedef enum {
    LRG_SURFACE_ROAD,     /* Normal grip, full speed */
    LRG_SURFACE_OFFROAD,  /* Reduced grip and speed */
    LRG_SURFACE_ICE,      /* Very low grip */
    LRG_SURFACE_BOOST,    /* Speed boost pad */
    LRG_SURFACE_SLOW,     /* Mud, sand */
    LRG_SURFACE_DAMAGE    /* Spikes, lava */
} LrgSurfaceType;
```

## Vehicle State (2D)

```c
/* Position */
gfloat x = lrg_racing_2d_template_get_vehicle_x (template);
gfloat y = lrg_racing_2d_template_get_vehicle_y (template);
lrg_racing_2d_template_set_vehicle_position (template, 100.0f, 200.0f);

/* Heading angle (radians) */
gfloat angle = lrg_racing_2d_template_get_vehicle_angle (template);
lrg_racing_2d_template_set_vehicle_angle (template, G_PI / 2);

/* Speed and drift state */
gfloat speed = lrg_racing_2d_template_get_speed (template);
gboolean drifting = lrg_racing_2d_template_is_drifting (template);
```

## Vehicle Settings (2D)

```c
/* Max speed */
lrg_racing_2d_template_set_max_speed (template, 300.0f);
gfloat max = lrg_racing_2d_template_get_max_speed (template);

/* Acceleration/braking */
lrg_racing_2d_template_set_acceleration (template, 200.0f);
lrg_racing_2d_template_set_brake_power (template, 400.0f);

/* Steering */
lrg_racing_2d_template_set_turn_speed (template, 3.0f);

/* Grip and drift */
lrg_racing_2d_template_set_grip (template, 0.8f);
lrg_racing_2d_template_set_drift_threshold (template, 150.0f);
```

## Lap/Checkpoint Tracking (2D)

```c
/* Current lap (1-based) */
guint lap = lrg_racing_2d_template_get_lap (template);

/* Total laps */
lrg_racing_2d_template_set_total_laps (template, 3);
guint total = lrg_racing_2d_template_get_total_laps (template);

/* Checkpoints */
guint checkpoint = lrg_racing_2d_template_get_checkpoint (template);
lrg_racing_2d_template_set_total_checkpoints (template, 8);
lrg_racing_2d_template_pass_checkpoint (template, 3);
```

## Time Tracking (2D)

```c
/* Total race time */
gfloat race_time = lrg_racing_2d_template_get_race_time (template);

/* Current lap time */
gfloat lap_time = lrg_racing_2d_template_get_lap_time (template);

/* Best lap time (-1 if no lap completed) */
gfloat best = lrg_racing_2d_template_get_best_lap_time (template);
```

## Boost System (2D)

```c
/* Boost meter (0.0-1.0) */
gfloat boost = lrg_racing_2d_template_get_boost (template);
lrg_racing_2d_template_set_boost (template, 1.0f);
lrg_racing_2d_template_add_boost (template, 0.25f);

/* Boost state */
gboolean boosting = lrg_racing_2d_template_is_boosting (template);

/* Boost speed multiplier */
lrg_racing_2d_template_set_boost_multiplier (template, 1.5f);
```

---

# LrgRacing3DTemplate

A template for 3D racing games with chase cameras, vehicle physics, and race management.

## Features

- 3D vehicle physics (acceleration, braking, steering)
- Multiple camera modes (chase, hood, bumper, cockpit, orbit)
- Lap and checkpoint tracking
- Boost/nitro system
- Race position tracking
- Speedometer and minimap HUD

## Quick Start (3D)

```c
#define MY_TYPE_RACER_3D (my_racer_3d_get_type ())
G_DECLARE_FINAL_TYPE (MyRacer3D, my_racer_3d, MY, RACER_3D, LrgRacing3DTemplate)

struct _MyRacer3D
{
    LrgRacing3DTemplate parent_instance;
    GrlModel *car_model;
};

G_DEFINE_TYPE (MyRacer3D, my_racer_3d, LRG_TYPE_RACING_3D_TEMPLATE)

static void
my_racer_3d_configure (LrgGameTemplate *template)
{
    LrgRacing3DTemplate *racing = LRG_RACING_3D_TEMPLATE (template);

    g_object_set (template,
                  "title", "3D Racer",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Vehicle physics */
    lrg_racing_3d_template_set_max_speed (racing, 200.0f);
    lrg_racing_3d_template_set_acceleration (racing, 50.0f);
    lrg_racing_3d_template_set_brake_power (racing, 100.0f);
    lrg_racing_3d_template_set_steering_speed (racing, 120.0f);
    lrg_racing_3d_template_set_grip (racing, 0.85f);

    /* Camera */
    lrg_racing_3d_template_set_camera_mode (racing, LRG_RACING_3D_CAMERA_CHASE);
    lrg_racing_3d_template_set_chase_distance (racing, 10.0f);
    lrg_racing_3d_template_set_chase_height (racing, 4.0f);

    /* Race settings */
    lrg_racing_3d_template_set_total_laps (racing, 3);
    lrg_racing_3d_template_set_total_checkpoints (racing, 12);

    /* Boost */
    lrg_racing_3d_template_set_boost_speed (racing, 1.4f);
}

static void
my_racer_3d_class_init (MyRacer3DClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_racer_3d_configure;
}
```

## Virtual Methods (3D)

```c
/* Called when race state changes */
void (*on_race_state_changed) (LrgRacing3DTemplate   *self,
                               LrgRacing3DRaceState   old_state,
                               LrgRacing3DRaceState   new_state);

/* Called when a lap is completed */
void (*on_lap_complete) (LrgRacing3DTemplate *self,
                         gint                 lap,
                         gfloat               lap_time);

/* Called when a checkpoint is reached */
void (*on_checkpoint_reached) (LrgRacing3DTemplate *self,
                               gint                 checkpoint);

/* Called on collision */
void (*on_collision) (LrgRacing3DTemplate *self,
                      gfloat               impact_force,
                      gfloat               normal_x,
                      gfloat               normal_y,
                      gfloat               normal_z);

/* Called when boost is activated */
void (*on_boost_activated) (LrgRacing3DTemplate *self);

/* Updates vehicle physics */
void (*update_vehicle) (LrgRacing3DTemplate *self,
                        gdouble              delta);

/* Updates chase camera */
void (*update_chase_camera) (LrgRacing3DTemplate *self,
                             gdouble              delta);

/* Checks for checkpoint crossing */
void (*check_checkpoints) (LrgRacing3DTemplate *self);

/* Renders the player vehicle */
void (*draw_vehicle) (LrgRacing3DTemplate *self);

/* Renders the track */
void (*draw_track) (LrgRacing3DTemplate *self);

/* Renders the speedometer */
void (*draw_speedometer) (LrgRacing3DTemplate *self);

/* Renders the minimap */
void (*draw_minimap) (LrgRacing3DTemplate *self);

/* Renders race HUD (lap, position, timer) */
void (*draw_race_hud) (LrgRacing3DTemplate *self);
```

## Camera Modes (3D)

```c
typedef enum {
    LRG_RACING_3D_CAMERA_CHASE,    /* Behind vehicle */
    LRG_RACING_3D_CAMERA_HOOD,     /* Hood/bonnet view */
    LRG_RACING_3D_CAMERA_BUMPER,   /* Front bumper */
    LRG_RACING_3D_CAMERA_COCKPIT,  /* Interior view */
    LRG_RACING_3D_CAMERA_ORBIT     /* Free orbit around vehicle */
} LrgRacing3DCameraMode;

/* Camera control */
LrgRacing3DCameraMode mode = lrg_racing_3d_template_get_camera_mode (template);
lrg_racing_3d_template_set_camera_mode (template, LRG_RACING_3D_CAMERA_HOOD);
lrg_racing_3d_template_cycle_camera (template);

/* Chase camera settings */
lrg_racing_3d_template_set_chase_distance (template, 10.0f);
lrg_racing_3d_template_set_chase_height (template, 4.0f);
```

## Vehicle Position (3D)

```c
/* Get position */
gfloat x, y, z;
lrg_racing_3d_template_get_position (template, &x, &y, &z);

/* Set position */
lrg_racing_3d_template_set_position (template, 0.0f, 0.5f, 0.0f);

/* Rotation (heading in degrees) */
gfloat rotation = lrg_racing_3d_template_get_rotation (template);
lrg_racing_3d_template_set_rotation (template, 90.0f);
```

## Vehicle Physics (3D)

```c
/* Speed */
gfloat speed = lrg_racing_3d_template_get_speed (template);
lrg_racing_3d_template_set_max_speed (template, 200.0f);

/* Acceleration and braking */
lrg_racing_3d_template_set_acceleration (template, 50.0f);
lrg_racing_3d_template_set_brake_power (template, 100.0f);

/* Steering */
lrg_racing_3d_template_set_steering_speed (template, 120.0f);

/* Grip */
lrg_racing_3d_template_set_grip (template, 0.85f);

/* Ground state */
gboolean grounded = lrg_racing_3d_template_is_grounded (template);
```

## Boost System (3D)

```c
/* Boost meter */
gfloat boost = lrg_racing_3d_template_get_boost (template);
lrg_racing_3d_template_set_boost (template, 1.0f);

/* Boost speed multiplier */
lrg_racing_3d_template_set_boost_speed (template, 1.4f);

/* Boost state */
gboolean boosting = lrg_racing_3d_template_is_boosting (template);
```

## Race Progress (3D)

```c
/* Laps */
gint lap = lrg_racing_3d_template_get_current_lap (template);
gint total_laps = lrg_racing_3d_template_get_total_laps (template);
lrg_racing_3d_template_set_total_laps (template, 3);

/* Checkpoints */
gint checkpoint = lrg_racing_3d_template_get_current_checkpoint (template);
gint total_checkpoints = lrg_racing_3d_template_get_total_checkpoints (template);
lrg_racing_3d_template_set_total_checkpoints (template, 12);
lrg_racing_3d_template_reach_checkpoint (template, 5);

/* Times */
gfloat race_time = lrg_racing_3d_template_get_race_time (template);
gfloat lap_time = lrg_racing_3d_template_get_lap_time (template);
gfloat best_lap = lrg_racing_3d_template_get_best_lap_time (template);

/* Position in race */
gint position = lrg_racing_3d_template_get_race_position (template);
lrg_racing_3d_template_set_total_racers (template, 8);
```

## Race State (3D)

```c
typedef enum {
    LRG_RACING_3D_RACE_STATE_WAITING,
    LRG_RACING_3D_RACE_STATE_COUNTDOWN,
    LRG_RACING_3D_RACE_STATE_RACING,
    LRG_RACING_3D_RACE_STATE_FINISHED,
    LRG_RACING_3D_RACE_STATE_PAUSED
} LrgRacing3DRaceState;

LrgRacing3DRaceState state = lrg_racing_3d_template_get_race_state (template);
lrg_racing_3d_template_set_race_state (template, LRG_RACING_3D_RACE_STATE_RACING);
lrg_racing_3d_template_start_countdown (template);

gint countdown = lrg_racing_3d_template_get_countdown (template);  /* 3, 2, 1, 0 */
```

## HUD Visibility (3D)

```c
/* Speedometer */
lrg_racing_3d_template_set_speedometer_visible (template, TRUE);
gboolean speedo = lrg_racing_3d_template_get_speedometer_visible (template);

/* Minimap */
lrg_racing_3d_template_set_minimap_visible (template, TRUE);
gboolean minimap = lrg_racing_3d_template_get_minimap_visible (template);
```

---

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D base features
- [LrgGame3DTemplate](game-3d-template.md) - 3D base features
- [LrgGameTemplate](game-template.md) - Base template features
