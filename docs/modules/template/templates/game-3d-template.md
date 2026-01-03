# LrgGame3DTemplate

`LrgGame3DTemplate` extends `LrgGameTemplate` with 3D-specific features including camera management, mouse look, quaternion-based orientation, and layered 3D rendering.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame3DTemplate (derivable)
    ├── LrgFPSTemplate
    ├── LrgThirdPersonTemplate
    └── LrgRacing3DTemplate
```

## Features

- Integrated 3D camera with multiple projection modes
- Quaternion-based camera orientation
- Mouse look with sensitivity and invert options
- Pitch limits to prevent camera flip
- View frustum management
- Layered rendering (skybox, world, effects, UI)
- First-person camera movement helpers
- Screen-to-ray conversion for picking

## Quick Start

```c
#define MY_TYPE_GAME (my_game_get_type ())
G_DECLARE_FINAL_TYPE (MyGame, my_game, MY, GAME, LrgGame3DTemplate)

struct _MyGame
{
    LrgGame3DTemplate parent_instance;
    gfloat player_x, player_y, player_z;
};

G_DEFINE_TYPE (MyGame, my_game, LRG_TYPE_GAME_3D_TEMPLATE)

static void
my_game_configure (LrgGameTemplate *template)
{
    LrgGame3DTemplate *template_3d = LRG_GAME_3D_TEMPLATE (template);

    g_object_set (template,
                  "title", "My 3D Game",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);

    /* Configure 3D camera */
    lrg_game_3d_template_set_fov (template_3d, 75.0f);
    lrg_game_3d_template_set_near_clip (template_3d, 0.1f);
    lrg_game_3d_template_set_far_clip (template_3d, 1000.0f);

    /* Enable mouse look */
    lrg_game_3d_template_set_mouse_look_enabled (template_3d, TRUE);
    lrg_game_3d_template_set_mouse_sensitivity (template_3d, 0.1f);
}

static void
my_game_draw_world (LrgGame3DTemplate *template)
{
    /* Draw 3D content */
    draw_terrain ();
    draw_models ();
}

static void
my_game_draw_ui (LrgGame3DTemplate *template)
{
    /* Draw 2D UI overlay */
    draw_crosshair ();
    draw_health_bar ();
}

static void
my_game_class_init (MyGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame3DTemplateClass *template_3d_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);

    template_class->configure = my_game_configure;
    template_3d_class->draw_world = my_game_draw_world;
    template_3d_class->draw_ui = my_game_draw_ui;
}
```

## Virtual Methods

```c
/* Renders the skybox or background (before world) */
void (*draw_skybox) (LrgGame3DTemplate *self);

/* Renders the 3D world (with camera transform) */
void (*draw_world) (LrgGame3DTemplate *self);

/* Renders visual effects (particles, transparent objects) */
void (*draw_effects) (LrgGame3DTemplate *self);

/* Renders the 2D UI overlay (screen space) */
void (*draw_ui) (LrgGame3DTemplate *self);

/* Updates camera each frame */
void (*update_camera) (LrgGame3DTemplate *self, gdouble delta);

/* Handles mouse movement for camera rotation */
void (*on_mouse_look) (LrgGame3DTemplate *self, gfloat delta_x, gfloat delta_y);
```

## Camera

### Access Camera

```c
LrgCamera3D *camera = lrg_game_3d_template_get_camera (template);

/* Set custom camera */
lrg_game_3d_template_set_camera (template, my_camera);

/* Reset to default camera */
lrg_game_3d_template_set_camera (template, NULL);
```

### Camera Configuration

```c
/* Field of view (vertical, degrees) */
lrg_game_3d_template_set_fov (template, 75.0f);
gfloat fov = lrg_game_3d_template_get_fov (template);

/* Clipping planes */
lrg_game_3d_template_set_near_clip (template, 0.1f);
lrg_game_3d_template_set_far_clip (template, 1000.0f);
gfloat near = lrg_game_3d_template_get_near_clip (template);
gfloat far = lrg_game_3d_template_get_far_clip (template);
```

## Mouse Look

### Enable/Disable

```c
/* Enable mouse look (locks and hides cursor) */
lrg_game_3d_template_set_mouse_look_enabled (template, TRUE);
gboolean enabled = lrg_game_3d_template_get_mouse_look_enabled (template);
```

### Sensitivity

```c
/* Set mouse sensitivity (default 0.1) */
lrg_game_3d_template_set_mouse_sensitivity (template, 0.15f);
gfloat sens = lrg_game_3d_template_get_mouse_sensitivity (template);
```

### Invert Y-Axis

```c
lrg_game_3d_template_set_invert_y (template, TRUE);
gboolean inverted = lrg_game_3d_template_get_invert_y (template);
```

### Pitch Limits

Prevent camera from flipping over:

```c
/* Set pitch limits (degrees) */
lrg_game_3d_template_set_pitch_limits (template, -89.0f, 89.0f);

/* Query limits */
gfloat min_pitch, max_pitch;
lrg_game_3d_template_get_pitch_limits (template, &min_pitch, &max_pitch);
```

## Camera Orientation

### Yaw and Pitch

```c
/* Get current orientation */
gfloat yaw = lrg_game_3d_template_get_yaw (template);
gfloat pitch = lrg_game_3d_template_get_pitch (template);

/* Set orientation */
lrg_game_3d_template_set_yaw (template, 45.0f);
lrg_game_3d_template_set_pitch (template, -15.0f);
```

### Look At Target

```c
/* Point camera at a world position */
lrg_game_3d_template_look_at (template, target_x, target_y, target_z);
```

## First-Person Movement

Built-in movement helpers that respect camera orientation:

```c
/* Move forward/backward relative to facing direction */
lrg_game_3d_template_move_forward (template, speed * delta);
lrg_game_3d_template_move_forward (template, -speed * delta);  /* backward */

/* Strafe left/right */
lrg_game_3d_template_move_right (template, speed * delta);
lrg_game_3d_template_move_right (template, -speed * delta);  /* left */

/* Move up/down in world space */
lrg_game_3d_template_move_up (template, jump_velocity * delta);
```

### Example: WASD Movement

```c
static void
my_game_fixed_update (LrgGameTemplate *template, gdouble delta)
{
    LrgGame3DTemplate *template_3d = LRG_GAME_3D_TEMPLATE (template);
    LrgInputMap *input = lrg_game_template_get_input_map (template);

    gfloat move_speed = 5.0f * delta;

    if (lrg_input_action_is_held (input, "move_forward"))
        lrg_game_3d_template_move_forward (template_3d, move_speed);
    if (lrg_input_action_is_held (input, "move_back"))
        lrg_game_3d_template_move_forward (template_3d, -move_speed);
    if (lrg_input_action_is_held (input, "move_left"))
        lrg_game_3d_template_move_right (template_3d, -move_speed);
    if (lrg_input_action_is_held (input, "move_right"))
        lrg_game_3d_template_move_right (template_3d, move_speed);
}
```

## Coordinate Transformation

### World to Screen

Project 3D positions to 2D screen coordinates:

```c
gfloat screen_x, screen_y;
lrg_game_3d_template_world_to_screen (template,
                                       world_x, world_y, world_z,
                                       &screen_x, &screen_y);

/* Note: Returns negative coords if point is behind camera */
if (screen_x >= 0 && screen_y >= 0)
{
    draw_indicator_at (screen_x, screen_y);
}
```

### Screen to Ray

Create a ray for picking/raycasting:

```c
gfloat origin_x, origin_y, origin_z;
gfloat dir_x, dir_y, dir_z;

lrg_game_3d_template_screen_to_ray (template,
                                     mouse_x, mouse_y,
                                     &origin_x, &origin_y, &origin_z,
                                     &dir_x, &dir_y, &dir_z);

/* Use ray for picking */
Entity *hit = raycast_entities (origin_x, origin_y, origin_z,
                                 dir_x, dir_y, dir_z);
```

## Rendering Order

The default 3D rendering pipeline:

1. Clear with clear color
2. Begin 3D mode
3. `draw_skybox()` - Background/skybox (depth disabled)
4. `draw_world()` - 3D models, terrain (depth enabled)
5. `draw_effects()` - Particles, transparent objects
6. End 3D mode
7. `draw_ui()` - 2D overlay (screen-space)

## Example: Skybox

```c
static void
my_game_draw_skybox (LrgGame3DTemplate *template)
{
    MyGame *self = MY_GAME (template);
    LrgCamera3D *camera = lrg_game_3d_template_get_camera (template);

    /* Disable depth for skybox */
    grl_disable_depth_mask ();

    /* Get camera position for skybox center */
    gfloat cam_x, cam_y, cam_z;
    lrg_camera3d_get_position (camera, &cam_x, &cam_y, &cam_z);

    /* Draw skybox centered on camera */
    draw_skybox_cube (self->skybox_texture, cam_x, cam_y, cam_z);

    /* Re-enable depth */
    grl_enable_depth_mask ();
}
```

## Related Documentation

- [LrgGameTemplate](game-template.md) - Base template features
- [LrgFPSTemplate](fps-template.md) - First-person shooter mechanics
- [LrgThirdPersonTemplate](third-person-template.md) - Third-person camera and controls
