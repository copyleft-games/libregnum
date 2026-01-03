# LrgThirdPersonTemplate

`LrgThirdPersonTemplate` is a game template specialized for third-person action games. It provides an orbiting camera system, multiple aim modes, lock-on targeting, and action game mechanics.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame3DTemplate
    └── LrgThirdPersonTemplate (derivable)
```

## Features

- Orbiting camera that follows the player
- Configurable camera distance and height offset
- Shoulder offset for over-the-shoulder aiming
- Multiple aim modes (free, strafe, aim, lock-on)
- Smooth camera collision avoidance
- Character rotation options
- Health and stamina systems
- Dodge/roll mechanics

## Quick Start

```c
#define MY_TYPE_ACTION (my_action_get_type ())
G_DECLARE_FINAL_TYPE (MyAction, my_action, MY, ACTION, LrgThirdPersonTemplate)

struct _MyAction
{
    LrgThirdPersonTemplate parent_instance;
    GrlModel *player_model;
};

G_DEFINE_TYPE (MyAction, my_action, LRG_TYPE_THIRD_PERSON_TEMPLATE)

static void
my_action_configure (LrgGameTemplate *template)
{
    LrgThirdPersonTemplate *tp = LRG_THIRD_PERSON_TEMPLATE (template);

    g_object_set (template,
                  "title", "Action Adventure",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Configure camera */
    lrg_third_person_template_set_camera_distance (tp, 5.0f);
    lrg_third_person_template_set_camera_height (tp, 2.0f);
    lrg_third_person_template_set_camera_smoothing (tp, 0.1f);

    /* Configure aim mode shoulder offset */
    lrg_third_person_template_set_shoulder_offset (tp, 1.5f, 0.5f);
    lrg_third_person_template_set_aim_distance (tp, 3.0f);

    /* Configure movement */
    lrg_third_person_template_set_move_speed (tp, 5.0f);
    lrg_third_person_template_set_run_multiplier (tp, 1.8f);
    lrg_third_person_template_set_rotation_speed (tp, 360.0f);

    /* Configure combat */
    lrg_third_person_template_set_max_health (tp, 100.0f);
    lrg_third_person_template_set_max_stamina (tp, 100.0f);
    lrg_third_person_template_set_dodge_distance (tp, 3.0f);
    lrg_third_person_template_set_dodge_stamina_cost (tp, 25.0f);
}

static void
my_action_class_init (MyActionClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_action_configure;
}
```

## Virtual Methods

```c
/* Called when aim mode changes */
void (*on_aim_mode_changed) (LrgThirdPersonTemplate *self,
                             LrgThirdPersonAimMode   old_mode,
                             LrgThirdPersonAimMode   new_mode);

/* Called when lock-on target changes */
void (*on_lock_on_target_changed) (LrgThirdPersonTemplate *self,
                                   gpointer                old_target,
                                   gpointer                new_target);

/* Called when player jumps */
void (*on_jump) (LrgThirdPersonTemplate *self);

/* Called when player lands */
void (*on_land) (LrgThirdPersonTemplate *self,
                 gfloat                  fall_velocity);

/* Called when player dodges/rolls */
void (*on_dodge) (LrgThirdPersonTemplate *self,
                  gfloat                  direction_x,
                  gfloat                  direction_z);

/* Called when player attacks */
gboolean (*on_attack) (LrgThirdPersonTemplate *self,
                       gint                    attack_type);

/* Called when player takes damage */
void (*on_damage) (LrgThirdPersonTemplate *self,
                   gfloat                  amount,
                   gfloat                  source_x,
                   gfloat                  source_y,
                   gfloat                  source_z);

/* Called when player dies */
void (*on_death) (LrgThirdPersonTemplate *self);

/* Updates player position and rotation */
void (*update_movement) (LrgThirdPersonTemplate *self,
                         gdouble                 delta);

/* Updates camera orbit position */
void (*update_camera_orbit) (LrgThirdPersonTemplate *self,
                             gdouble                 delta);

/* Checks for camera collision */
gboolean (*check_camera_collision) (LrgThirdPersonTemplate *self,
                                    gfloat                  camera_x,
                                    gfloat                  camera_y,
                                    gfloat                  camera_z,
                                    gfloat                 *out_x,
                                    gfloat                 *out_y,
                                    gfloat                 *out_z);

/* Renders the player character */
void (*draw_character) (LrgThirdPersonTemplate *self);

/* Renders lock-on target indicator */
void (*draw_target_indicator) (LrgThirdPersonTemplate *self);

/* Renders aiming crosshair */
void (*draw_crosshair) (LrgThirdPersonTemplate *self);

/* Renders the HUD */
void (*draw_hud) (LrgThirdPersonTemplate *self);
```

## Player Position

```c
/* Get/set position */
gfloat x, y, z;
lrg_third_person_template_get_position (template, &x, &y, &z);
lrg_third_person_template_set_position (template, 10.0f, 0.0f, 5.0f);

/* Get/set rotation (facing direction) */
gfloat rotation = lrg_third_person_template_get_rotation (template);
lrg_third_person_template_set_rotation (template, 45.0f);
```

## Movement Settings

```c
/* Movement speed */
lrg_third_person_template_set_move_speed (template, 5.0f);
gfloat speed = lrg_third_person_template_get_move_speed (template);

/* Run speed multiplier */
lrg_third_person_template_set_run_multiplier (template, 1.8f);
gfloat run = lrg_third_person_template_get_run_multiplier (template);

/* Character rotation speed */
lrg_third_person_template_set_rotation_speed (template, 360.0f);
gfloat rot_speed = lrg_third_person_template_get_rotation_speed (template);

/* Jump height */
lrg_third_person_template_set_jump_height (template, 2.0f);
gfloat jump = lrg_third_person_template_get_jump_height (template);

/* Gravity */
lrg_third_person_template_set_gravity (template, 20.0f);
gfloat gravity = lrg_third_person_template_get_gravity (template);

/* Query state */
gboolean running = lrg_third_person_template_is_running (template);
gboolean grounded = lrg_third_person_template_is_on_ground (template);
```

## Camera Settings

```c
/* Camera distance from player */
lrg_third_person_template_set_camera_distance (template, 5.0f);
gfloat dist = lrg_third_person_template_get_camera_distance (template);

/* Camera height offset */
lrg_third_person_template_set_camera_height (template, 2.0f);
gfloat height = lrg_third_person_template_get_camera_height (template);

/* Camera follow smoothing (0.0-1.0, 1.0 = instant) */
lrg_third_person_template_set_camera_smoothing (template, 0.1f);
gfloat smooth = lrg_third_person_template_get_camera_smoothing (template);
```

## Shoulder Offset (Over-the-Shoulder Aiming)

```c
/* Set shoulder offset (x = left/right, y = up/down) */
lrg_third_person_template_set_shoulder_offset (template, 1.5f, 0.5f);

/* Query offset */
gfloat offset_x, offset_y;
lrg_third_person_template_get_shoulder_offset (template, &offset_x, &offset_y);

/* Swap shoulder side (left/right toggle) */
lrg_third_person_template_swap_shoulder (template);

/* Camera distance when aiming */
lrg_third_person_template_set_aim_distance (template, 3.0f);
gfloat aim_dist = lrg_third_person_template_get_aim_distance (template);
```

## Aim Modes

```c
typedef enum {
    LRG_THIRD_PERSON_AIM_MODE_FREE,     /* Free camera, character moves independently */
    LRG_THIRD_PERSON_AIM_MODE_STRAFE,   /* Character always faces camera direction */
    LRG_THIRD_PERSON_AIM_MODE_AIM,      /* Over-the-shoulder aiming mode */
    LRG_THIRD_PERSON_AIM_MODE_LOCK_ON   /* Locked onto a target */
} LrgThirdPersonAimMode;

/* Get/set aim mode */
LrgThirdPersonAimMode mode = lrg_third_person_template_get_aim_mode (template);
lrg_third_person_template_set_aim_mode (template, LRG_THIRD_PERSON_AIM_MODE_AIM);

/* Check if aiming (AIM or LOCK_ON mode) */
gboolean aiming = lrg_third_person_template_is_aiming (template);
```

## Lock-On System

```c
/* Set lock-on target position */
lrg_third_person_template_set_lock_on_target (template,
                                               enemy_x, enemy_y, enemy_z);

/* Get lock-on target position */
gfloat target_x, target_y, target_z;
gboolean has_target = lrg_third_person_template_get_lock_on_target (
    template, &target_x, &target_y, &target_z);

/* Clear lock-on (return to free mode) */
lrg_third_person_template_clear_lock_on (template);

/* Set maximum lock-on range */
lrg_third_person_template_set_lock_on_range (template, 20.0f);
gfloat range = lrg_third_person_template_get_lock_on_range (template);
```

## Health and Stamina

```c
/* Health */
lrg_third_person_template_set_health (template, 75.0f);
gfloat health = lrg_third_person_template_get_health (template);

lrg_third_person_template_set_max_health (template, 100.0f);
gfloat max_health = lrg_third_person_template_get_max_health (template);

/* Stamina */
lrg_third_person_template_set_stamina (template, 80.0f);
gfloat stamina = lrg_third_person_template_get_stamina (template);

lrg_third_person_template_set_max_stamina (template, 100.0f);
gfloat max_stamina = lrg_third_person_template_get_max_stamina (template);

/* Apply damage */
lrg_third_person_template_apply_damage (template, 25.0f,
                                         source_x, source_y, source_z);

/* Check death */
gboolean dead = lrg_third_person_template_is_dead (template);
```

## Dodge System

```c
/* Configure dodge */
lrg_third_person_template_set_dodge_distance (template, 3.0f);
gfloat dist = lrg_third_person_template_get_dodge_distance (template);

lrg_third_person_template_set_dodge_stamina_cost (template, 25.0f);
gfloat cost = lrg_third_person_template_get_dodge_stamina_cost (template);

/* Query dodge state */
gboolean can_dodge = lrg_third_person_template_can_dodge (template);
gboolean dodging = lrg_third_person_template_is_dodging (template);
```

## Crosshair

```c
/* Show/hide crosshair */
lrg_third_person_template_set_crosshair_visible (template, TRUE);
gboolean visible = lrg_third_person_template_get_crosshair_visible (template);
```

## Camera Collision Detection

```c
static gboolean
my_game_check_camera_collision (LrgThirdPersonTemplate *template,
                                gfloat                  camera_x,
                                gfloat                  camera_y,
                                gfloat                  camera_z,
                                gfloat                 *out_x,
                                gfloat                 *out_y,
                                gfloat                 *out_z)
{
    MyAction *self = MY_ACTION (template);
    gfloat player_x, player_y, player_z;

    lrg_third_person_template_get_position (template,
                                            &player_x, &player_y, &player_z);

    /* Raycast from player to desired camera position */
    RaycastHit hit;
    if (physics_raycast (self->world,
                         player_x, player_y, player_z,
                         camera_x, camera_y, camera_z,
                         &hit))
    {
        /* Move camera closer to avoid clipping */
        *out_x = hit.x - hit.normal_x * 0.5f;
        *out_y = hit.y - hit.normal_y * 0.5f;
        *out_z = hit.z - hit.normal_z * 0.5f;
        return TRUE;
    }

    /* No collision, use original position */
    *out_x = camera_x;
    *out_y = camera_y;
    *out_z = camera_z;
    return FALSE;
}
```

## Combat Implementation

```c
static gboolean
my_game_on_attack (LrgThirdPersonTemplate *template, gint attack_type)
{
    MyAction *self = MY_ACTION (template);
    gfloat stamina = lrg_third_person_template_get_stamina (template);

    /* Check stamina cost */
    gfloat cost = (attack_type == 0) ? 10.0f : 25.0f;  /* Light vs heavy */
    if (stamina < cost)
        return FALSE;

    /* Consume stamina */
    lrg_third_person_template_set_stamina (template, stamina - cost);

    /* Trigger attack animation and hitbox */
    if (attack_type == 0)
        play_animation (self, "attack_light");
    else
        play_animation (self, "attack_heavy");

    return TRUE;
}
```

## Related Documentation

- [LrgGame3DTemplate](game-3d-template.md) - 3D template features
- [LrgGameTemplate](game-template.md) - Base template features
- [LrgFPSTemplate](fps-template.md) - First-person alternative
