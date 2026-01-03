# 2D Shooter Templates

Libregnum provides a family of 2D shooter templates for different shooter sub-genres. All extend from `LrgShooter2DTemplate` with specialized mechanics.

## Template Hierarchy

```
LrgGameTemplate
└── LrgGame2DTemplate
    └── LrgShooter2DTemplate (derivable)
        ├── LrgTwinStickTemplate (final)
        └── LrgShmupTemplate (final)
```

---

# LrgShooter2DTemplate

Base template for 2D shooter games with projectile spawning, fire rate handling, weapon slots, and score tracking.

## Features

- Projectile spawning and management
- Fire rate and cooldown handling
- Multiple weapon slots
- Score tracking with multipliers
- Play area bounds
- High score tracking

## Quick Start

```c
#define MY_TYPE_SHOOTER (my_shooter_get_type ())
G_DECLARE_FINAL_TYPE (MyShooter, my_shooter, MY, SHOOTER, LrgShooter2DTemplate)

struct _MyShooter
{
    LrgShooter2DTemplate parent_instance;
};

G_DEFINE_TYPE (MyShooter, my_shooter, LRG_TYPE_SHOOTER_2D_TEMPLATE)

static void
my_shooter_configure (LrgGameTemplate *template)
{
    LrgShooter2DTemplate *shooter = LRG_SHOOTER_2D_TEMPLATE (template);
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);

    g_object_set (template,
                  "title", "Space Shooter",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    lrg_game_2d_template_set_virtual_resolution (template_2d, 320, 180);

    /* Configure weapons */
    lrg_shooter_2d_template_set_fire_rate (shooter, 10.0f);  /* 10 shots/sec */
    lrg_shooter_2d_template_set_projectile_speed (shooter, 300.0f);
    lrg_shooter_2d_template_set_max_projectiles (shooter, 100);

    /* Set play area */
    lrg_shooter_2d_template_set_play_area (shooter, 0, 0, 320, 180);
}
```

## Virtual Methods

```c
/* Spawns a new projectile */
gboolean (*spawn_projectile) (LrgShooter2DTemplate *self,
                              gfloat                x,
                              gfloat                y,
                              gfloat                direction_x,
                              gfloat                direction_y,
                              gfloat                speed,
                              guint                 owner_id);

/* Updates all active projectiles */
void (*update_projectiles) (LrgShooter2DTemplate *self,
                            gdouble               delta);

/* Called when a projectile hits something */
void (*on_projectile_hit) (LrgShooter2DTemplate *self,
                           guint                 projectile_id,
                           guint                 target_id,
                           gfloat                x,
                           gfloat                y);

/* Fires the current weapon */
gboolean (*fire_weapon) (LrgShooter2DTemplate *self);

/* Switches to a different weapon slot */
gboolean (*switch_weapon) (LrgShooter2DTemplate *self,
                           guint                 slot);

/* Called when an enemy is destroyed */
void (*on_enemy_destroyed) (LrgShooter2DTemplate *self,
                            guint                 enemy_id,
                            gfloat                x,
                            gfloat                y,
                            gint64                points);
```

## Player Position

```c
gfloat x, y;
lrg_shooter_2d_template_get_player_position (template, &x, &y);
lrg_shooter_2d_template_set_player_position (template, 160.0f, 150.0f);
```

## Fire Rate & Cooldown

```c
/* Shots per second */
lrg_shooter_2d_template_set_fire_rate (template, 10.0f);
gfloat rate = lrg_shooter_2d_template_get_fire_rate (template);

/* Check if ready to fire */
gfloat cooldown = lrg_shooter_2d_template_get_fire_cooldown (template);
gboolean can_fire = lrg_shooter_2d_template_can_fire (template);

/* Fire weapon */
gboolean fired = lrg_shooter_2d_template_fire (template);
```

## Projectile Settings

```c
/* Projectile speed */
lrg_shooter_2d_template_set_projectile_speed (template, 300.0f);
gfloat speed = lrg_shooter_2d_template_get_projectile_speed (template);

/* Maximum simultaneous projectiles */
lrg_shooter_2d_template_set_max_projectiles (template, 100);
guint max = lrg_shooter_2d_template_get_max_projectiles (template);

/* Current active projectiles */
guint active = lrg_shooter_2d_template_get_active_projectile_count (template);

/* Clear all projectiles */
lrg_shooter_2d_template_clear_projectiles (template);
```

## Weapons

```c
/* Current weapon slot */
guint weapon = lrg_shooter_2d_template_get_current_weapon (template);

/* Number of weapon slots */
lrg_shooter_2d_template_set_weapon_count (template, 4);
guint count = lrg_shooter_2d_template_get_weapon_count (template);
```

## Score

```c
/* Get/set score */
gint64 score = lrg_shooter_2d_template_get_score (template);
lrg_shooter_2d_template_set_score (template, 0);

/* Add points */
lrg_shooter_2d_template_add_score (template, 100);

/* High score */
gint64 high = lrg_shooter_2d_template_get_high_score (template);

/* Score multiplier */
lrg_shooter_2d_template_set_score_multiplier (template, 2.0f);
gfloat mult = lrg_shooter_2d_template_get_score_multiplier (template);
```

## Play Area

```c
/* Set bounds */
lrg_shooter_2d_template_set_play_area (template, 0, 0, 320, 180);

/* Query bounds */
gfloat min_x, min_y, max_x, max_y;
lrg_shooter_2d_template_get_play_area (template,
                                        &min_x, &min_y, &max_x, &max_y);
```

---

# LrgTwinStickTemplate

Specialized template for twin-stick shooter games with dual-stick controls for movement and aiming.

## Features

- Left stick / WASD controls player movement
- Right stick / mouse controls aim direction
- Continuous firing in aim direction
- 360-degree aiming
- Gamepad and keyboard+mouse support
- Dash/dodge mechanics

## Quick Start

```c
#define MY_TYPE_TWIN_STICK (my_twin_stick_get_type ())
G_DECLARE_FINAL_TYPE (MyTwinStick, my_twin_stick, MY, TWIN_STICK, LrgTwinStickTemplate)

static void
my_twin_stick_configure (LrgGameTemplate *template)
{
    LrgTwinStickTemplate *ts = LRG_TWIN_STICK_TEMPLATE (template);
    LrgShooter2DTemplate *shooter = LRG_SHOOTER_2D_TEMPLATE (template);

    /* Shooter settings */
    lrg_shooter_2d_template_set_fire_rate (shooter, 15.0f);
    lrg_shooter_2d_template_set_projectile_speed (shooter, 400.0f);

    /* Twin-stick settings */
    lrg_twin_stick_template_set_aim_mode (ts, LRG_TWIN_STICK_AIM_HYBRID);
    lrg_twin_stick_template_set_aim_deadzone (ts, 0.2f);
    lrg_twin_stick_template_set_move_deadzone (ts, 0.15f);
    lrg_twin_stick_template_set_fire_threshold (ts, 0.5f);

    /* Dash settings */
    lrg_twin_stick_template_set_dash_speed (ts, 3.0f);
    lrg_twin_stick_template_set_dash_duration (ts, 0.15f);
    lrg_twin_stick_template_set_dash_cooldown (ts, 0.5f);
}
```

## Aim Direction

```c
/* Get normalized aim direction */
gfloat aim_x, aim_y;
lrg_twin_stick_template_get_aim_direction (template, &aim_x, &aim_y);

/* Set aim direction (normalized automatically) */
lrg_twin_stick_template_set_aim_direction (template, 1.0f, 0.5f);

/* Get/set aim angle (radians, 0 = right, PI/2 = down) */
gfloat angle = lrg_twin_stick_template_get_aim_angle (template);
lrg_twin_stick_template_set_aim_angle (template, G_PI_4);
```

## Movement Direction

```c
/* Get movement input (-1 to 1) */
gfloat move_x, move_y;
lrg_twin_stick_template_get_move_direction (template, &move_x, &move_y);

/* Set movement direction */
lrg_twin_stick_template_set_move_direction (template, -0.5f, 1.0f);
```

## Input Settings

```c
typedef enum {
    LRG_TWIN_STICK_AIM_STICK,   /* Aim using right stick */
    LRG_TWIN_STICK_AIM_MOUSE,   /* Aim toward mouse cursor */
    LRG_TWIN_STICK_AIM_HYBRID   /* Auto-switch based on last input */
} LrgTwinStickAimMode;

/* Aim input mode */
lrg_twin_stick_template_set_aim_mode (template, LRG_TWIN_STICK_AIM_HYBRID);
LrgTwinStickAimMode mode = lrg_twin_stick_template_get_aim_mode (template);

/* Gamepad deadzones */
lrg_twin_stick_template_set_aim_deadzone (template, 0.2f);
lrg_twin_stick_template_set_move_deadzone (template, 0.15f);

/* Minimum aim magnitude to fire */
lrg_twin_stick_template_set_fire_threshold (template, 0.5f);
gfloat threshold = lrg_twin_stick_template_get_fire_threshold (template);
```

## Dash/Dodge

```c
/* Configure dash */
lrg_twin_stick_template_set_dash_speed (template, 3.0f);     /* Speed multiplier */
lrg_twin_stick_template_set_dash_duration (template, 0.15f); /* Seconds */
lrg_twin_stick_template_set_dash_cooldown (template, 0.5f);  /* Seconds */

/* Query state */
gboolean can_dash = lrg_twin_stick_template_can_dash (template);
gboolean dashing = lrg_twin_stick_template_is_dashing (template);

/* Initiate dash */
gboolean dashed = lrg_twin_stick_template_dash (template);
```

---

# LrgShmupTemplate

Specialized template for scrolling shooter (shmup) games with automatic scrolling, power-ups, bombs, and lives systems.

## Features

- Automatic vertical or horizontal scrolling
- Power-up collection and weapon upgrades
- Bomb/super weapon system
- Lives and continues
- Bullet grazing (scoring bonus for near-misses)
- Focus mode for precise movement
- Boss encounter support

## Quick Start

```c
#define MY_TYPE_SHMUP (my_shmup_get_type ())
G_DECLARE_FINAL_TYPE (MyShmup, my_shmup, MY, SHMUP, LrgShmupTemplate)

static void
my_shmup_configure (LrgGameTemplate *template)
{
    LrgShmupTemplate *shmup = LRG_SHMUP_TEMPLATE (template);
    LrgShooter2DTemplate *shooter = LRG_SHOOTER_2D_TEMPLATE (template);
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);

    lrg_game_2d_template_set_virtual_resolution (template_2d, 240, 320);

    /* Configure scrolling */
    lrg_shmup_template_set_scroll_direction (shmup, LRG_SHMUP_SCROLL_UP);
    lrg_shmup_template_set_scroll_speed (shmup, 30.0f);

    /* Configure lives */
    lrg_shmup_template_set_lives (shmup, 3);
    lrg_shmup_template_set_max_lives (shmup, 5);
    lrg_shmup_template_set_continues (shmup, 2);

    /* Configure bombs */
    lrg_shmup_template_set_bombs (shmup, 3);
    lrg_shmup_template_set_max_bombs (shmup, 5);
    lrg_shmup_template_set_bomb_duration (shmup, 3.0f);

    /* Configure grazing */
    lrg_shmup_template_set_graze_radius (shmup, 16.0f);
    lrg_shmup_template_set_graze_points (shmup, 100);

    /* Configure hitbox */
    lrg_shmup_template_set_hitbox_radius (shmup, 2.0f);
    lrg_shmup_template_set_show_hitbox (shmup, TRUE);

    /* Focus mode */
    lrg_shmup_template_set_focus_speed_multiplier (shmup, 0.5f);
}
```

## Scrolling

```c
typedef enum {
    LRG_SHMUP_SCROLL_UP,     /* Vertical shmup */
    LRG_SHMUP_SCROLL_DOWN,
    LRG_SHMUP_SCROLL_LEFT,
    LRG_SHMUP_SCROLL_RIGHT,  /* Horizontal shmup (R-Type style) */
    LRG_SHMUP_SCROLL_NONE    /* No automatic scrolling */
} LrgShmupScrollDirection;

/* Direction */
lrg_shmup_template_set_scroll_direction (template, LRG_SHMUP_SCROLL_UP);
LrgShmupScrollDirection dir = lrg_shmup_template_get_scroll_direction (template);

/* Speed */
lrg_shmup_template_set_scroll_speed (template, 30.0f);
gfloat speed = lrg_shmup_template_get_scroll_speed (template);

/* Position (level progress) */
gfloat pos = lrg_shmup_template_get_scroll_position (template);
lrg_shmup_template_set_scroll_position (template, 0.0f);

/* Pause scrolling (for boss battles) */
lrg_shmup_template_set_scroll_paused (template, TRUE);
gboolean paused = lrg_shmup_template_get_scroll_paused (template);
```

## Lives & Continues

```c
/* Lives */
lrg_shmup_template_set_lives (template, 3);
gint lives = lrg_shmup_template_get_lives (template);

lrg_shmup_template_set_max_lives (template, 5);
gint max = lrg_shmup_template_get_max_lives (template);

/* Lose a life (emits "life-lost" signal) */
gint remaining = lrg_shmup_template_lose_life (template);

/* Continues */
lrg_shmup_template_set_continues (template, 2);
gint continues = lrg_shmup_template_get_continues (template);

/* Use a continue (restores lives) */
gboolean used = lrg_shmup_template_use_continue (template);
```

## Bombs

```c
/* Bomb count */
lrg_shmup_template_set_bombs (template, 3);
gint bombs = lrg_shmup_template_get_bombs (template);

lrg_shmup_template_set_max_bombs (template, 5);
gint max = lrg_shmup_template_get_max_bombs (template);

/* Use bomb (emits "bomb-used" signal) */
gboolean used = lrg_shmup_template_use_bomb (template);

/* Check if bomb effect is active */
gboolean active = lrg_shmup_template_is_bomb_active (template);

/* Bomb duration */
lrg_shmup_template_set_bomb_duration (template, 3.0f);
gfloat duration = lrg_shmup_template_get_bomb_duration (template);
```

## Power Level

```c
/* Current/max power level */
lrg_shmup_template_set_power_level (template, 0);
gint power = lrg_shmup_template_get_power_level (template);

lrg_shmup_template_set_max_power_level (template, 4);
gint max = lrg_shmup_template_get_max_power_level (template);

/* Add power (auto levels up) */
lrg_shmup_template_add_power (template, 10);
```

## Grazing (Bullet Dodging Bonus)

```c
/* Graze detection radius */
lrg_shmup_template_set_graze_radius (template, 16.0f);
gfloat radius = lrg_shmup_template_get_graze_radius (template);

/* Points per graze */
lrg_shmup_template_set_graze_points (template, 100);
gint64 points = lrg_shmup_template_get_graze_points (template);

/* Record a graze (emits "bullet-grazed" signal) */
lrg_shmup_template_add_graze (template);

/* Total grazes */
guint count = lrg_shmup_template_get_graze_count (template);
```

## Player Hitbox

```c
/* Hitbox radius */
lrg_shmup_template_set_hitbox_radius (template, 2.0f);
gfloat radius = lrg_shmup_template_get_hitbox_radius (template);

/* Show hitbox visually */
lrg_shmup_template_set_show_hitbox (template, TRUE);
gboolean shown = lrg_shmup_template_get_show_hitbox (template);
```

## Focus Mode

```c
/* Speed when focused (0-1) */
lrg_shmup_template_set_focus_speed_multiplier (template, 0.5f);
gfloat mult = lrg_shmup_template_get_focus_speed_multiplier (template);

/* Enable/check focus */
lrg_shmup_template_set_focused (template, TRUE);
gboolean focused = lrg_shmup_template_is_focused (template);
```

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D template features
- [LrgGameTemplate](game-template.md) - Base template features
