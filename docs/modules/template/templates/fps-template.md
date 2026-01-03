# LrgFPSTemplate

`LrgFPSTemplate` is a game template specialized for first-person shooter games. It provides first-person movement, weapon handling, health/armor systems, and FPS-specific rendering features.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame3DTemplate
    └── LrgFPSTemplate (derivable)
```

## Features

- First-person movement (WASD + mouse look)
- Sprint, crouch, and jump mechanics
- Weapon handling (fire, reload, switch)
- Health and armor system
- Crosshair rendering
- Head bob effect
- Posture system (standing, crouching, prone)

## Quick Start

```c
#define MY_TYPE_FPS (my_fps_get_type ())
G_DECLARE_FINAL_TYPE (MyFPS, my_fps, MY, FPS, LrgFPSTemplate)

struct _MyFPS
{
    LrgFPSTemplate parent_instance;
    GrlTexture *weapon_textures[4];
    gint ammo[4];
};

G_DEFINE_TYPE (MyFPS, my_fps, LRG_TYPE_FPS_TEMPLATE)

static void
my_fps_configure (LrgGameTemplate *template)
{
    LrgFPSTemplate *fps = LRG_FPS_TEMPLATE (template);

    g_object_set (template,
                  "title", "My FPS Game",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Configure movement */
    lrg_fps_template_set_walk_speed (fps, 5.0f);
    lrg_fps_template_set_sprint_multiplier (fps, 1.5f);
    lrg_fps_template_set_jump_height (fps, 1.5f);
    lrg_fps_template_set_gravity (fps, 20.0f);

    /* Configure player */
    lrg_fps_template_set_max_health (fps, 100.0f);
    lrg_fps_template_set_health (fps, 100.0f);
    lrg_fps_template_set_standing_height (fps, 1.8f);
    lrg_fps_template_set_crouch_height (fps, 1.0f);

    /* Enable effects */
    lrg_fps_template_set_head_bob_enabled (fps, TRUE);
    lrg_fps_template_set_head_bob_intensity (fps, 0.5f);
    lrg_fps_template_set_crosshair_visible (fps, TRUE);
}

static gboolean
my_fps_on_fire (LrgFPSTemplate *fps, gboolean is_primary)
{
    MyFPS *self = MY_FPS (fps);
    gint weapon = lrg_fps_template_get_current_weapon (fps);

    if (self->ammo[weapon] <= 0)
        return FALSE;

    self->ammo[weapon]--;

    if (is_primary)
    {
        /* Primary fire - single shot */
        spawn_bullet (self);
        play_sound ("gunshot.wav");
    }
    else
    {
        /* Secondary fire - scope/alt mode */
        toggle_scope (self);
    }

    return TRUE;
}

static void
my_fps_class_init (MyFPSClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgFPSTemplateClass *fps_class = LRG_FPS_TEMPLATE_CLASS (klass);

    template_class->configure = my_fps_configure;
    fps_class->on_fire = my_fps_on_fire;
}
```

## Virtual Methods

```c
/* Called when fire button is pressed */
gboolean (*on_fire) (LrgFPSTemplate *self,
                     gboolean        is_primary);

/* Called when reload is triggered */
gboolean (*on_reload) (LrgFPSTemplate *self);

/* Called when switching weapons */
void (*on_weapon_switch) (LrgFPSTemplate *self,
                          gint            weapon_index);

/* Called when player jumps */
void (*on_jump) (LrgFPSTemplate *self);

/* Called when player lands on ground */
void (*on_land) (LrgFPSTemplate *self,
                 gfloat          fall_velocity);

/* Called when player takes damage */
void (*on_damage) (LrgFPSTemplate *self,
                   gfloat          amount,
                   gfloat          source_x,
                   gfloat          source_y,
                   gfloat          source_z);

/* Called when player dies */
void (*on_death) (LrgFPSTemplate *self);

/* Called when posture changes */
void (*on_posture_changed) (LrgFPSTemplate *self,
                            LrgFPSPosture   old_posture,
                            LrgFPSPosture   new_posture);

/* Updates player position */
void (*update_movement) (LrgFPSTemplate *self,
                         gdouble         delta);

/* Checks if player is on ground */
gboolean (*check_ground) (LrgFPSTemplate *self);

/* Renders the weapon viewmodel */
void (*draw_weapon) (LrgFPSTemplate *self);

/* Renders the crosshair */
void (*draw_crosshair) (LrgFPSTemplate *self);

/* Renders the HUD */
void (*draw_hud) (LrgFPSTemplate *self);
```

## Player Position

```c
/* Get/set player position */
gfloat x, y, z;
lrg_fps_template_get_position (template, &x, &y, &z);
lrg_fps_template_set_position (template, 10.0f, 0.0f, 5.0f);
```

## Movement Settings

```c
/* Walking speed */
lrg_fps_template_set_walk_speed (template, 5.0f);
gfloat speed = lrg_fps_template_get_walk_speed (template);

/* Sprint multiplier (1.5 = 50% faster) */
lrg_fps_template_set_sprint_multiplier (template, 1.5f);
gfloat sprint = lrg_fps_template_get_sprint_multiplier (template);

/* Crouch speed multiplier (0.5 = 50% slower) */
lrg_fps_template_set_crouch_multiplier (template, 0.5f);
gfloat crouch = lrg_fps_template_get_crouch_multiplier (template);

/* Jump height */
lrg_fps_template_set_jump_height (template, 1.5f);
gfloat jump = lrg_fps_template_get_jump_height (template);

/* Gravity */
lrg_fps_template_set_gravity (template, 20.0f);
gfloat gravity = lrg_fps_template_get_gravity (template);
```

## Posture System

```c
typedef enum {
    LRG_FPS_POSTURE_STANDING,   /* Standing upright */
    LRG_FPS_POSTURE_CROUCHING,  /* Crouching (lower height, slower) */
    LRG_FPS_POSTURE_PRONE       /* Prone (lying down, very low) */
} LrgFPSPosture;

/* Get/set posture */
LrgFPSPosture posture = lrg_fps_template_get_posture (template);
lrg_fps_template_set_posture (template, LRG_FPS_POSTURE_CROUCHING);

/* Query state */
gboolean sprinting = lrg_fps_template_is_sprinting (template);
gboolean grounded = lrg_fps_template_is_on_ground (template);
```

## Player Height

```c
/* Eye height when standing */
lrg_fps_template_set_standing_height (template, 1.8f);
gfloat stand_h = lrg_fps_template_get_standing_height (template);

/* Eye height when crouching */
lrg_fps_template_set_crouch_height (template, 1.0f);
gfloat crouch_h = lrg_fps_template_get_crouch_height (template);
```

## Health System

```c
/* Current/max health */
lrg_fps_template_set_health (template, 75.0f);
gfloat health = lrg_fps_template_get_health (template);

lrg_fps_template_set_max_health (template, 100.0f);
gfloat max_health = lrg_fps_template_get_max_health (template);

/* Armor */
lrg_fps_template_set_armor (template, 50.0f);
gfloat armor = lrg_fps_template_get_armor (template);

/* Apply damage (absorbed by armor first) */
lrg_fps_template_apply_damage (template, 25.0f,
                                enemy_x, enemy_y, enemy_z);

/* Check death */
gboolean dead = lrg_fps_template_is_dead (template);
```

## Weapon System

```c
/* Get/set current weapon */
gint weapon = lrg_fps_template_get_current_weapon (template);
lrg_fps_template_set_current_weapon (template, 2);

/* Ammo management */
gint ammo = lrg_fps_template_get_ammo (template);
lrg_fps_template_set_ammo (template, 30);

/* Check reload state */
gboolean reloading = lrg_fps_template_is_reloading (template);
```

## Head Bob Effect

```c
/* Enable/disable */
lrg_fps_template_set_head_bob_enabled (template, TRUE);
gboolean enabled = lrg_fps_template_get_head_bob_enabled (template);

/* Intensity (0.0-1.0) */
lrg_fps_template_set_head_bob_intensity (template, 0.5f);
gfloat intensity = lrg_fps_template_get_head_bob_intensity (template);
```

## Crosshair

```c
/* Show/hide crosshair */
lrg_fps_template_set_crosshair_visible (template, TRUE);
gboolean visible = lrg_fps_template_get_crosshair_visible (template);
```

## Custom Weapon Rendering

```c
static void
my_fps_draw_weapon (LrgFPSTemplate *fps)
{
    MyFPS *self = MY_FPS (fps);
    gint weapon = lrg_fps_template_get_current_weapon (fps);

    /* Draw weapon viewmodel in screen space */
    gfloat bob_x = 0.0f, bob_y = 0.0f;
    if (lrg_fps_template_get_head_bob_enabled (fps))
    {
        /* Calculate bob offset */
        bob_x = sinf (self->walk_time * 10.0f) * 5.0f;
        bob_y = fabsf (cosf (self->walk_time * 10.0f)) * 3.0f;
    }

    /* Draw at bottom-right of screen */
    gint screen_w, screen_h;
    lrg_game_template_get_window_size (LRG_GAME_TEMPLATE (fps),
                                       &screen_w, &screen_h);

    grl_draw_texture (self->weapon_textures[weapon],
                      screen_w - 256 + bob_x,
                      screen_h - 192 + bob_y,
                      WHITE);
}
```

## Custom Ground Detection

```c
static gboolean
my_fps_check_ground (LrgFPSTemplate *fps)
{
    MyFPS *self = MY_FPS (fps);
    gfloat x, y, z;
    lrg_fps_template_get_position (fps, &x, &y, &z);

    /* Raycast downward to check for ground */
    return physics_raycast_down (self->world, x, y, z, 0.1f);
}
```

## Fall Damage

```c
static void
my_fps_on_land (LrgFPSTemplate *fps, gfloat fall_velocity)
{
    /* Calculate fall damage based on impact velocity */
    if (fall_velocity > 10.0f)
    {
        gfloat damage = (fall_velocity - 10.0f) * 5.0f;
        gfloat health = lrg_fps_template_get_health (fps);
        lrg_fps_template_set_health (fps, health - damage);

        play_sound ("land_hard.wav");
    }
    else
    {
        play_sound ("land_soft.wav");
    }
}
```

## Related Documentation

- [LrgGame3DTemplate](game-3d-template.md) - 3D template features
- [LrgGameTemplate](game-template.md) - Base template features
- [LrgThirdPersonTemplate](third-person-template.md) - Third-person alternative
