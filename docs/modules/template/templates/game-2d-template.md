# LrgGame2DTemplate

`LrgGame2DTemplate` extends `LrgGameTemplate` with 2D-specific features including virtual resolution scaling, integrated 2D camera, and layered rendering.

## Inheritance Hierarchy

```
LrgGameTemplate
└── LrgGame2DTemplate (derivable)
    ├── LrgPlatformerTemplate
    ├── LrgTopDownTemplate
    ├── LrgShooter2DTemplate
    │   ├── LrgTwinStickTemplate
    │   └── LrgShmupTemplate
    ├── LrgRacing2DTemplate
    └── LrgTycoonTemplate
```

## Features

- Virtual resolution with automatic scaling
- Multiple scaling modes (letterbox, stretch, pixel-perfect)
- Integrated 2D camera with follow, deadzone, and smoothing
- Layered rendering (background, world, UI)
- Coordinate transformation between virtual and screen space
- Camera bounds for level constraints

## Quick Start

```c
#define MY_TYPE_GAME (my_game_get_type ())
G_DECLARE_FINAL_TYPE (MyGame, my_game, MY, GAME, LrgGame2DTemplate)

struct _MyGame
{
    LrgGame2DTemplate parent_instance;
};

G_DEFINE_TYPE (MyGame, my_game, LRG_TYPE_GAME_2D_TEMPLATE)

static void
my_game_configure (LrgGameTemplate *template)
{
    LrgGame2DTemplate *template_2d = LRG_GAME_2D_TEMPLATE (template);

    g_object_set (template,
                  "title", "My 2D Game",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    /* Set virtual resolution (design resolution) */
    lrg_game_2d_template_set_virtual_resolution (template_2d, 320, 180);
    lrg_game_2d_template_set_scaling_mode (template_2d, LRG_SCALING_MODE_LETTERBOX);
    lrg_game_2d_template_set_pixel_perfect (template_2d, TRUE);
}

static void
my_game_draw_world (LrgGame2DTemplate *template)
{
    /* Draw game entities in world space */
    draw_tilemap ();
    draw_entities ();
}

static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    /* Draw HUD in virtual resolution space */
    draw_health_bar (10, 10);
    draw_score (300, 10);
}

static void
my_game_class_init (MyGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame2DTemplateClass *template_2d_class = LRG_GAME_2D_TEMPLATE_CLASS (klass);

    template_class->configure = my_game_configure;
    template_2d_class->draw_world = my_game_draw_world;
    template_2d_class->draw_ui = my_game_draw_ui;
}
```

## Virtual Methods

```c
/* Called when window resolution changes */
void (*on_resolution_changed) (LrgGame2DTemplate *self,
                               gint               new_width,
                               gint               new_height);

/* Renders background layer (before camera transform) */
void (*draw_background) (LrgGame2DTemplate *self);

/* Renders world layer (with camera transform) */
void (*draw_world) (LrgGame2DTemplate *self);

/* Renders UI layer (screen-space, after camera) */
void (*draw_ui) (LrgGame2DTemplate *self);

/* Updates camera each frame */
void (*update_camera) (LrgGame2DTemplate *self, gdouble delta);
```

## Virtual Resolution

Virtual resolution lets you design at a fixed resolution and scale to any window size.

```c
/* Set design resolution */
lrg_game_2d_template_set_virtual_resolution (template, 320, 180);

/* Or set individually */
lrg_game_2d_template_set_virtual_width (template, 320);
lrg_game_2d_template_set_virtual_height (template, 180);

/* Query current virtual resolution */
gint vw = lrg_game_2d_template_get_virtual_width (template);
gint vh = lrg_game_2d_template_get_virtual_height (template);
```

### Runtime Resolution Changes

Virtual resolution can be changed at runtime. The scaling factors are automatically
recalculated when you call any of the setter functions:

```c
/* Change virtual resolution at runtime */
lrg_game_2d_template_set_virtual_resolution (template, 1920, 1080);
/* Scaling updates immediately - no additional action needed */

/* Also works with individual setters */
lrg_game_2d_template_set_virtual_width (template, 640);
lrg_game_2d_template_set_virtual_height (template, 360);
```

This is useful for games that support multiple virtual resolutions (e.g., a quality
setting that changes render resolution), or for dynamic resolution scaling.

## Scaling Modes

```c
typedef enum {
    LRG_SCALING_MODE_LETTERBOX,     /* Maintain aspect, add bars */
    LRG_SCALING_MODE_STRETCH,       /* Stretch to fill (distorts) */
    LRG_SCALING_MODE_CROP,          /* Fill and crop excess */
    LRG_SCALING_MODE_INTEGER,       /* Integer scaling only */
} LrgScalingMode;

lrg_game_2d_template_set_scaling_mode (template, LRG_SCALING_MODE_LETTERBOX);
LrgScalingMode mode = lrg_game_2d_template_get_scaling_mode (template);
```

### Pixel Perfect Mode

For pixel art games, enable integer scaling to avoid sub-pixel artifacts:

```c
lrg_game_2d_template_set_pixel_perfect (template, TRUE);
gboolean pp = lrg_game_2d_template_get_pixel_perfect (template);
```

### Letterbox Color

```c
/* Set color for letterbox/pillarbox bars */
g_autoptr(GrlColor) color = grl_color_new (20, 20, 30, 255);
lrg_game_2d_template_set_letterbox_color (template, color);
```

## Programmatic Window Resize

When using `lrg_game_template_set_window_size()` to resize the window at runtime,
the 2D template automatically recalculates scaling and emits the `resolution-changed`
signal:

```c
/* Resize window - viewport updates immediately */
lrg_game_template_set_window_size (LRG_GAME_TEMPLATE (template), 1920, 1080);

/* Handle resize in subclass (optional) */
static void
my_game_on_resolution_changed (LrgGame2DTemplate *template,
                                gint               width,
                                gint               height)
{
    /* Recalculate UI layouts if needed */
    update_ui_layout (width, height);
}
```

This works correctly for both programmatic resizes and user-initiated window
resizes (dragging window borders, fullscreen toggle, etc.).

## Camera

### Access Camera

```c
LrgCamera2D *camera = lrg_game_2d_template_get_camera (template);

/* Set custom camera */
lrg_game_2d_template_set_camera (template, my_camera);

/* Reset to default camera */
lrg_game_2d_template_set_camera (template, NULL);
```

### Camera Follow

```c
/* Set target position (camera follows smoothly) */
lrg_game_2d_template_set_camera_target (template, player_x, player_y);

/* Configure smoothing (0.0 = instant, higher = slower) */
lrg_game_2d_template_set_camera_smoothing (template, 0.15f);
gfloat smoothing = lrg_game_2d_template_get_camera_smoothing (template);
```

### Camera Deadzone

The camera won't move while the target is within the deadzone.

```c
/* Set deadzone size in virtual pixels */
lrg_game_2d_template_set_camera_deadzone (template, 64.0f, 48.0f);

/* Query deadzone */
gfloat dz_w, dz_h;
lrg_game_2d_template_get_camera_deadzone (template, &dz_w, &dz_h);
```

### Camera Bounds

Constrain camera to level boundaries:

```c
/* Set world bounds (camera won't show areas outside) */
lrg_game_2d_template_set_camera_bounds (template,
                                         0.0f, 0.0f,          /* min x, y */
                                         level_width, level_height);  /* max x, y */

/* Remove bounds (unlimited scrolling) */
lrg_game_2d_template_clear_camera_bounds (template);
```

## Coordinate Transformation

### World to Screen

```c
/* Convert world position to screen position */
gfloat screen_x, screen_y;
lrg_game_2d_template_world_to_screen (template,
                                       entity_x, entity_y,
                                       &screen_x, &screen_y);
```

### Screen to World

```c
/* Convert mouse click to world position */
gfloat world_x, world_y;
lrg_game_2d_template_screen_to_world (template,
                                       mouse_x, mouse_y,
                                       &world_x, &world_y);
```

### Virtual to Screen (UI)

```c
/* Convert virtual resolution coords to screen coords */
gfloat screen_x, screen_y;
lrg_game_2d_template_virtual_to_screen (template,
                                         ui_x, ui_y,
                                         &screen_x, &screen_y);
```

### Screen to Virtual (UI)

```c
/* Convert screen coords to virtual resolution coords */
gfloat virtual_x, virtual_y;
lrg_game_2d_template_screen_to_virtual (template,
                                         screen_x, screen_y,
                                         &virtual_x, &virtual_y);
```

## Render Target

Access the internal render texture for post-processing:

```c
GrlRenderTexture *rt = lrg_game_2d_template_get_render_texture (template);

/* Use for custom post-processing in post_draw */
static void
my_game_post_draw (LrgGameTemplate *template)
{
    GrlRenderTexture *rt = lrg_game_2d_template_get_render_texture (
        LRG_GAME_2D_TEMPLATE (template));

    /* Apply shader to render texture */
    apply_crt_shader (rt);
}
```

## Rendering Order

The default rendering pipeline:

1. Begin render to virtual resolution texture
2. Clear with background color
3. `draw_background()` - Static backgrounds, parallax (no camera)
4. Begin camera transform
5. `draw_world()` - Game entities, tilemaps (with camera)
6. End camera transform
7. `draw_ui()` - HUD, menus (screen-space)
8. End render to texture
9. Scale texture to window with letterboxing

## Example: Parallax Background

```c
static void
my_game_draw_background (LrgGame2DTemplate *template)
{
    MyGame *self = MY_GAME (template);
    LrgCamera2D *camera = lrg_game_2d_template_get_camera (template);

    gfloat cam_x, cam_y;
    lrg_camera2d_get_target (camera, &cam_x, &cam_y);

    /* Far layer (slow parallax) */
    draw_texture (self->bg_far, -cam_x * 0.1f, -cam_y * 0.1f);

    /* Mid layer */
    draw_texture (self->bg_mid, -cam_x * 0.3f, -cam_y * 0.3f);

    /* Near layer (faster parallax) */
    draw_texture (self->bg_near, -cam_x * 0.6f, -cam_y * 0.6f);
}
```

## Related Documentation

- [LrgGameTemplate](game-template.md) - Base template features
- [LrgPlatformerTemplate](platformer-template.md) - 2D platformer physics
- [LrgTopDownTemplate](top-down-template.md) - Top-down movement
- [LrgShooter2DTemplate](shooter-template.md) - 2D shooter mechanics
