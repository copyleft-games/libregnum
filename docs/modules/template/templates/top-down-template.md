# LrgTopDownTemplate

`LrgTopDownTemplate` is a game template specialized for top-down 2D games. It provides configurable movement modes, character facing direction tracking, interaction systems, and camera look-ahead.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame2DTemplate
    └── LrgTopDownTemplate (derivable)
```

## Features

- Multiple movement modes (4-dir, 8-dir, free, tank controls)
- Character facing direction tracking
- Interaction system for NPCs and objects
- Movement physics with acceleration and friction
- Camera look-ahead for smoother scrolling
- Configurable player collision bounds

## Quick Start

```c
#define MY_TYPE_RPG (my_rpg_get_type ())
G_DECLARE_FINAL_TYPE (MyRPG, my_rpg, MY, RPG, LrgTopDownTemplate)

struct _MyRPG
{
    LrgTopDownTemplate parent_instance;
    GrlTexture *player_sprites[8];  /* One per direction */
};

G_DEFINE_TYPE (MyRPG, my_rpg, LRG_TYPE_TOP_DOWN_TEMPLATE)

static void
my_rpg_configure (LrgGameTemplate *template)
{
    LrgTopDownTemplate *td = LRG_TOP_DOWN_TEMPLATE (template);
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);

    g_object_set (template,
                  "title", "My RPG",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Pixel art with virtual resolution */
    lrg_game_2d_template_set_virtual_resolution (template_2d, 320, 180);
    lrg_game_2d_template_set_pixel_perfect (template_2d, TRUE);

    /* Configure movement */
    lrg_top_down_template_set_movement_mode (td, LRG_TOP_DOWN_MOVEMENT_8_DIR);
    lrg_top_down_template_set_move_speed (td, 120.0f);
    lrg_top_down_template_set_acceleration (td, 800.0f);
    lrg_top_down_template_set_friction (td, 1200.0f);

    /* Configure interaction */
    lrg_top_down_template_set_interact_radius (td, 24.0f);

    /* Camera look-ahead */
    lrg_top_down_template_set_look_ahead (td, 32.0f);
    lrg_top_down_template_set_look_ahead_speed (td, 0.1f);
}

static void
my_rpg_class_init (MyRPGClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_rpg_configure;
}
```

## Virtual Methods

```c
/* Processes movement input and returns desired velocity */
void (*on_movement_input) (LrgTopDownTemplate *self,
                           gfloat              input_x,
                           gfloat              input_y,
                           gdouble             delta,
                           gfloat             *velocity_x,
                           gfloat             *velocity_y);

/* Called when character facing direction changes */
void (*on_facing_changed) (LrgTopDownTemplate *self,
                           LrgFacingDirection  old_facing,
                           LrgFacingDirection  new_facing);

/* Called when player presses interact button */
gboolean (*on_interact) (LrgTopDownTemplate *self);

/* Called when closest interactable target changes */
void (*on_interact_target_changed) (LrgTopDownTemplate *self,
                                    gpointer            target);

/* Updates position based on velocity (handles physics) */
void (*update_movement) (LrgTopDownTemplate *self,
                         gdouble             delta);

/* Collision detection - return TRUE if collision occurred */
gboolean (*check_collision) (LrgTopDownTemplate *self,
                             gfloat              new_x,
                             gfloat              new_y,
                             gfloat             *resolved_x,
                             gfloat             *resolved_y);

/* Renders the player character */
void (*draw_player) (LrgTopDownTemplate *self);

/* Draws interaction prompt near interactable object */
void (*draw_interact_prompt) (LrgTopDownTemplate *self,
                              gfloat              target_x,
                              gfloat              target_y);
```

## Movement Modes

```c
typedef enum {
    LRG_TOP_DOWN_MOVEMENT_4_DIR,  /* Classic RPG (up/down/left/right only) */
    LRG_TOP_DOWN_MOVEMENT_8_DIR,  /* Action games (includes diagonals) */
    LRG_TOP_DOWN_MOVEMENT_FREE,   /* Twin-stick style (any angle) */
    LRG_TOP_DOWN_MOVEMENT_TANK    /* Tank controls (forward/back + rotate) */
} LrgTopDownMovementMode;

/* Get/set movement mode */
LrgTopDownMovementMode mode = lrg_top_down_template_get_movement_mode (template);
lrg_top_down_template_set_movement_mode (template, LRG_TOP_DOWN_MOVEMENT_8_DIR);
```

## Facing Directions

```c
typedef enum {
    LRG_FACING_DOWN,        /* Default facing */
    LRG_FACING_UP,
    LRG_FACING_LEFT,
    LRG_FACING_RIGHT,
    LRG_FACING_DOWN_LEFT,   /* For 8-dir games */
    LRG_FACING_DOWN_RIGHT,
    LRG_FACING_UP_LEFT,
    LRG_FACING_UP_RIGHT
} LrgFacingDirection;

/* Get/set facing direction */
LrgFacingDirection facing = lrg_top_down_template_get_facing (template);
lrg_top_down_template_set_facing (template, LRG_FACING_RIGHT);

/* Get facing as angle (radians, 0 = right, PI/2 = down) */
gfloat angle = lrg_top_down_template_get_facing_angle (template);
```

## Player Position

```c
/* Get position */
gfloat x = lrg_top_down_template_get_player_x (template);
gfloat y = lrg_top_down_template_get_player_y (template);

/* Set position (teleport, spawn) */
lrg_top_down_template_set_player_position (template, 100.0f, 50.0f);

/* Get current velocity */
gfloat vel_x, vel_y;
lrg_top_down_template_get_player_velocity (template, &vel_x, &vel_y);
```

## Movement Settings

```c
/* Movement speed (pixels per second) */
lrg_top_down_template_set_move_speed (template, 120.0f);
gfloat speed = lrg_top_down_template_get_move_speed (template);

/* Acceleration (higher = more responsive) */
lrg_top_down_template_set_acceleration (template, 800.0f);
gfloat accel = lrg_top_down_template_get_acceleration (template);

/* Friction/deceleration (higher = faster stopping) */
lrg_top_down_template_set_friction (template, 1200.0f);
gfloat friction = lrg_top_down_template_get_friction (template);
```

## Tank Controls

For `LRG_TOP_DOWN_MOVEMENT_TANK` mode:

```c
/* Rotation speed (radians per second) */
lrg_top_down_template_set_rotation_speed (template, G_PI);  /* 180 deg/sec */
gfloat rot_speed = lrg_top_down_template_get_rotation_speed (template);
```

## Interaction System

```c
/* Set interaction detection radius */
lrg_top_down_template_set_interact_radius (template, 24.0f);
gfloat radius = lrg_top_down_template_get_interact_radius (template);

/* Get/set current interact target (auto-detected or manual) */
gpointer target = lrg_top_down_template_get_interact_target (template);
lrg_top_down_template_set_interact_target (template, npc);

/* Trigger interaction with current target */
gboolean success = lrg_top_down_template_trigger_interact (template);
```

## Camera Look-Ahead

```c
/* Look-ahead distance (camera offsets in movement direction) */
lrg_top_down_template_set_look_ahead (template, 32.0f);
gfloat distance = lrg_top_down_template_get_look_ahead (template);

/* Look-ahead interpolation speed (0.0-1.0) */
lrg_top_down_template_set_look_ahead_speed (template, 0.1f);
gfloat speed = lrg_top_down_template_get_look_ahead_speed (template);
```

## Player Size

```c
/* Collision/render bounds */
lrg_top_down_template_set_player_width (template, 16.0f);
lrg_top_down_template_set_player_height (template, 16.0f);

gfloat width = lrg_top_down_template_get_player_width (template);
gfloat height = lrg_top_down_template_get_player_height (template);
```

## Collision Implementation

```c
static gboolean
my_rpg_check_collision (LrgTopDownTemplate *template,
                        gfloat              new_x,
                        gfloat              new_y,
                        gfloat             *resolved_x,
                        gfloat             *resolved_y)
{
    MyRPG *self = MY_RPG (template);
    gfloat width = lrg_top_down_template_get_player_width (template);
    gfloat height = lrg_top_down_template_get_player_height (template);

    /* Check tilemap collision */
    if (tilemap_rect_collides (self->tilemap,
                               new_x - width / 2, new_y - height / 2,
                               width, height))
    {
        /* Slide along walls - try X only */
        gfloat old_x = lrg_top_down_template_get_player_x (template);
        gfloat old_y = lrg_top_down_template_get_player_y (template);

        if (!tilemap_rect_collides (self->tilemap,
                                    new_x - width / 2, old_y - height / 2,
                                    width, height))
        {
            *resolved_x = new_x;
            *resolved_y = old_y;
        }
        else if (!tilemap_rect_collides (self->tilemap,
                                         old_x - width / 2, new_y - height / 2,
                                         width, height))
        {
            *resolved_x = old_x;
            *resolved_y = new_y;
        }
        else
        {
            *resolved_x = old_x;
            *resolved_y = old_y;
        }
        return TRUE;
    }

    *resolved_x = new_x;
    *resolved_y = new_y;
    return FALSE;
}
```

## Direction-Based Sprite Rendering

```c
static void
my_rpg_draw_player (LrgTopDownTemplate *template)
{
    MyRPG *self = MY_RPG (template);
    LrgFacingDirection facing = lrg_top_down_template_get_facing (template);
    gfloat x = lrg_top_down_template_get_player_x (template);
    gfloat y = lrg_top_down_template_get_player_y (template);

    /* Use facing direction as sprite index */
    GrlTexture *sprite = self->player_sprites[facing];

    gfloat width = lrg_top_down_template_get_player_width (template);
    gfloat height = lrg_top_down_template_get_player_height (template);

    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    grl_draw_texture_ex (sprite,
                         x - width / 2, y - height / 2,
                         width, height,
                         white);
}
```

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D template features
- [LrgGameTemplate](game-template.md) - Base template features
- [LrgPlatformerTemplate](platformer-template.md) - Alternative 2D template
