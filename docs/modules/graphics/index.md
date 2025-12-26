---
title: Graphics Module
module: graphics
---

# Graphics Module Documentation

The Graphics module provides window management, camera systems, and rendering abstractions for Libregnum games.

> **[Home](../../index.md)** > **[Modules](../index.md)** > Graphics

## Overview

The Graphics module provides:

- **[LrgDrawable](drawable.md)** - Interface for renderable objects
- **[LrgWindow](window.md)** - Abstract window base class
- **[LrgGrlWindow](grl-window.md)** - Graylib window implementation
- **[LrgCamera](camera.md)** - Abstract camera base class
- **[LrgCamera2D](camera2d.md)** - 2D camera for top-down/side-scrolling games
- **[LrgCamera3D](camera3d.md)** - 3D camera for 3D games
- **[LrgCameraIsometric](camera-isometric.md)** - Isometric camera for strategy/tile-based games
- **[LrgCameraTopDown](camera-topdown.md)** - Top-down camera with smooth follow and screen shake
- **[LrgCameraSideOn](camera-sideon.md)** - Platformer camera with lookahead and deadzones
- **[LrgCameraFirstPerson](camera-firstperson.md)** - First-person camera with head bob
- **[LrgCameraThirdPerson](camera-thirdperson.md)** - Third-person camera with orbit and collision avoidance
- **[LrgRenderer](renderer.md)** - Render management and layer system

## Architecture

The graphics system is designed for extensibility:

```
                 LrgWindow (abstract)
                      │
                      ├── LrgGrlWindow (graylib backend)
                      └── LrgGtkWindow (future: GTK for editors)

                 LrgCamera (abstract)
                      │
                      ├── LrgCamera2D (derivable)
                      │       │
                      │       ├── LrgCameraTopDown (smooth follow, deadzone, screen shake)
                      │       └── LrgCameraSideOn (platformer lookahead, vertical bias)
                      │
                      └── LrgCamera3D (derivable)
                              │
                              ├── LrgCameraIsometric (fixed isometric angle)
                              ├── LrgCameraFirstPerson (FPS with head bob)
                              └── LrgCameraThirdPerson (orbit, collision avoidance)

     LrgDrawable (interface)
           │
           └── Implemented by game objects, components, etc.

                  LrgRenderer
                      │
                      ├── Manages window
                      ├── Manages active camera
                      └── Provides layer-based rendering
```

## Engine Integration

The graphics system integrates with `LrgEngine`:

```c
/* Create window */
g_autoptr(LrgGrlWindow) window = lrg_grl_window_new (800, 600, "My Game");

/* Get engine and set window */
LrgEngine *engine = lrg_engine_get_default ();
lrg_engine_set_window (engine, LRG_WINDOW (window));

/* Renderer is created automatically */
lrg_engine_startup (engine, &error);
LrgRenderer *renderer = lrg_engine_get_renderer (engine);
```

The engine supports headless mode (no window) for server or testing scenarios.

## Quick Start

### Basic Window and Rendering

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgGrlWindow) window = NULL;
    g_autoptr(LrgCamera2D) camera = NULL;
    LrgEngine *engine;
    LrgRenderer *renderer;

    /* Create window */
    window = lrg_grl_window_new (800, 600, "My Game");
    lrg_window_set_target_fps (LRG_WINDOW (window), 60);

    /* Setup engine */
    engine = lrg_engine_get_default ();
    lrg_engine_set_window (engine, LRG_WINDOW (window));

    if (!lrg_engine_startup (engine, &error))
    {
        g_error ("Startup failed: %s", error->message);
    }

    /* Get renderer and create camera */
    renderer = lrg_engine_get_renderer (engine);
    camera = lrg_camera2d_new ();
    lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

    /* Game loop */
    while (!lrg_window_should_close (LRG_WINDOW (window)))
    {
        gfloat delta = lrg_window_get_frame_time (LRG_WINDOW (window));

        /* Begin frame */
        lrg_renderer_begin_frame (renderer);
        lrg_renderer_clear (renderer, NULL);  /* Default background */

        /* Render world layer */
        lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
        /* Draw world content here */
        lrg_renderer_end_layer (renderer);

        /* Render UI layer */
        lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_UI);
        /* Draw UI here */
        lrg_renderer_end_layer (renderer);

        /* End frame */
        lrg_renderer_end_frame (renderer);
    }

    lrg_engine_shutdown (engine);
    return 0;
}
```

### 3D Camera Setup

```c
g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();

/* Position camera */
lrg_camera3d_set_position_xyz (camera, 0.0f, 10.0f, 10.0f);
lrg_camera3d_set_target_xyz (camera, 0.0f, 0.0f, 0.0f);

/* Configure projection */
lrg_camera3d_set_fovy (camera, 60.0f);
lrg_camera3d_set_projection (camera, LRG_PROJECTION_PERSPECTIVE);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));
```

### 2D Camera Follow

```c
g_autoptr(LrgCamera2D) camera = lrg_camera2d_new ();

/* Center offset (half screen) */
lrg_camera2d_set_offset_xy (camera, 400.0f, 300.0f);

/* In game loop, follow player */
lrg_camera2d_set_target_xy (camera, player_x, player_y);
```

### Isometric Camera Setup

```c
g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();

/* Configure for tile-based game */
lrg_camera_isometric_set_tile_width (camera, 64.0f);
lrg_camera_isometric_set_tile_height (camera, 32.0f);
lrg_camera_isometric_set_zoom (camera, 2.0f);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

/* In game loop, focus on player position */
lrg_camera_isometric_focus_on (camera, player_x, player_y, player_z);

/* Convert screen click to tile coordinates */
gint tile_x, tile_y;
lrg_camera_isometric_world_to_tile (camera, world_x, world_y, world_z,
                                    &tile_x, &tile_y);
```

### Top-Down Camera (Zelda, Hotline Miami)

```c
g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();

/* Configure smooth follow */
lrg_camera_topdown_set_follow_speed (camera, 8.0f);
lrg_camera_topdown_set_deadzone_radius (camera, 30.0f);

/* Set world bounds to prevent camera from showing outside the map */
lrg_camera_topdown_set_bounds (camera, 0.0f, 0.0f, 3200.0f, 2400.0f);
lrg_camera_topdown_set_bounds_enabled (camera, TRUE);

/* Set screen offset (center of screen) */
lrg_camera2d_set_offset_xy (LRG_CAMERA2D (camera), 400.0f, 300.0f);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

/* In game loop */
lrg_camera_topdown_follow (camera, player_x, player_y, delta_time);

/* Screen shake on damage */
if (player_took_damage)
    lrg_camera_topdown_shake (camera, 10.0f, 0.3f);
```

### Side-On Camera (Platformers)

```c
g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();

/* Configure for platformer - slower vertical to reduce jump jitter */
lrg_camera_sideon_set_follow_speed_x (camera, 10.0f);
lrg_camera_sideon_set_follow_speed_y (camera, 5.0f);

/* Rectangular deadzone - larger vertical for jumps */
lrg_camera_sideon_set_deadzone (camera, 80.0f, 120.0f);

/* Horizontal lookahead based on movement direction */
lrg_camera_sideon_set_lookahead_distance (camera, 150.0f);
lrg_camera_sideon_set_lookahead_speed (camera, 3.0f);

/* Show more ground than sky */
lrg_camera_sideon_set_vertical_bias (camera, 0.2f);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

/* In game loop */
lrg_camera_sideon_follow (camera, player_x, player_y, delta_time);
```

### First-Person Camera (FPS Games)

```c
g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();

/* Configure mouse sensitivity */
lrg_camera_firstperson_set_sensitivity_x (camera, 0.15f);
lrg_camera_firstperson_set_sensitivity_y (camera, 0.15f);

/* Enable head bob while walking */
lrg_camera_firstperson_set_head_bob_enabled (camera, TRUE);
lrg_camera_firstperson_set_head_bob (camera, 10.0f, 0.05f, 0.02f);

/* Set eye height */
lrg_camera_firstperson_set_eye_height (camera, 1.7f);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

/* In game loop - handle mouse input */
lrg_camera_firstperson_rotate (camera, mouse_delta_x, mouse_delta_y);
lrg_camera_firstperson_set_body_position (camera, player_x, player_y, player_z);
lrg_camera_firstperson_update_head_bob (camera, is_walking, delta_time);

/* Get movement direction for player */
g_autoptr(GrlVector3) forward = lrg_camera_firstperson_get_forward (camera);
g_autoptr(GrlVector3) right = lrg_camera_firstperson_get_right (camera);
```

### Third-Person Camera (Action Games)

```c
g_autoptr(LrgCameraThirdPerson) camera = lrg_camera_thirdperson_new ();

/* Configure orbit distance */
lrg_camera_thirdperson_set_distance (camera, 6.0f);
lrg_camera_thirdperson_set_distance_limits (camera, 2.0f, 15.0f);

/* Over-the-shoulder offset */
lrg_camera_thirdperson_set_height_offset (camera, 1.5f);
lrg_camera_thirdperson_set_shoulder_offset (camera, 0.8f);

/* Smooth orbit and follow */
lrg_camera_thirdperson_set_orbit_smoothing (camera, 10.0f);
lrg_camera_thirdperson_set_follow_smoothing (camera, 8.0f);

/* Enable collision avoidance */
lrg_camera_thirdperson_set_collision_enabled (camera, TRUE);
lrg_camera_thirdperson_set_collision_radius (camera, 0.3f);

/* Set on renderer */
lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));

/* In game loop - handle right stick input */
lrg_camera_thirdperson_orbit (camera, stick_x, stick_y);
lrg_camera_thirdperson_follow (camera, player_x, player_y, player_z, delta_time);

/* Get actual distance (may be reduced due to collision) */
gfloat actual_dist = lrg_camera_thirdperson_get_actual_distance (camera);
```

## Core Types

| Type | Description | Derivable |
|------|-------------|-----------|
| `LrgDrawable` | Interface for renderable objects | N/A (Interface) |
| `LrgWindow` | Abstract window base class | Yes (Abstract) |
| `LrgGrlWindow` | Graylib window implementation | No (Final) |
| `LrgCamera` | Abstract camera base class | Yes (Abstract) |
| `LrgCamera2D` | 2D camera | Yes |
| `LrgCamera3D` | 3D camera | Yes |
| `LrgCameraIsometric` | Isometric camera (inherits LrgCamera3D) | Yes |
| `LrgCameraTopDown` | Top-down camera (inherits LrgCamera2D) | Yes |
| `LrgCameraSideOn` | Platformer camera (inherits LrgCamera2D) | Yes |
| `LrgCameraFirstPerson` | First-person camera (inherits LrgCamera3D) | Yes |
| `LrgCameraThirdPerson` | Third-person camera (inherits LrgCamera3D) | Yes |
| `LrgRenderer` | Render management | Yes |

## Enumerations

| Enum | Values | Description |
|------|--------|-------------|
| `LrgRenderLayer` | BACKGROUND, WORLD, EFFECTS, UI, DEBUG | Render layer ordering |
| `LrgProjectionType` | PERSPECTIVE, ORTHOGRAPHIC | 3D camera projection |

## Render Layers

The renderer provides a layer system for proper draw ordering:

| Layer | Order | Purpose |
|-------|-------|---------|
| `BACKGROUND` | 0 | Skybox, parallax backgrounds |
| `WORLD` | 1 | Main game content (3D world, tilemap) |
| `EFFECTS` | 2 | Particles, weather, post-processing |
| `UI` | 3 | 2D UI overlay (menus, HUD) |
| `DEBUG` | 4 | Debug overlays (FPS, colliders) |

The camera transform is automatically applied for the `WORLD` layer.

## LrgDrawable Interface

Implement `LrgDrawable` for custom renderable objects:

```c
static void
my_object_draw (LrgDrawable *drawable,
                gfloat       delta)
{
    MyObject *self = MY_OBJECT (drawable);

    /* Use grl_draw_* functions */
    grl_draw_circle (self->x, self->y, self->radius, self->color);
}

static void
my_object_drawable_init (LrgDrawableInterface *iface)
{
    iface->draw = my_object_draw;
    iface->get_bounds = my_object_get_bounds; /* Optional */
}

G_DEFINE_TYPE_WITH_CODE (MyObject, my_object, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DRAWABLE,
                                                my_object_drawable_init))
```

Then use with the renderer:

```c
lrg_renderer_render_drawable (renderer, LRG_DRAWABLE (my_object), delta);
```

## Window Signals

`LrgWindow` emits signals for input and window events:

```c
/* Connect to key press */
g_signal_connect (window, "key-pressed",
                  G_CALLBACK (on_key_pressed), user_data);

/* Signal handlers */
void on_key_pressed (LrgWindow *window, gint key, gpointer user_data);
void on_key_released (LrgWindow *window, gint key, gpointer user_data);
void on_mouse_button_pressed (LrgWindow *window, gint button,
                              gfloat x, gfloat y, gpointer user_data);
void on_mouse_moved (LrgWindow *window, gfloat x, gfloat y,
                     gfloat dx, gfloat dy, gpointer user_data);
void on_resize (LrgWindow *window, gint width, gint height, gpointer user_data);
void on_close_requested (LrgWindow *window, gpointer user_data);
```

## Renderer Signals

`LrgRenderer` emits signals for render events:

```c
g_signal_connect (renderer, "frame-begin", G_CALLBACK (on_frame_begin), NULL);
g_signal_connect (renderer, "frame-end", G_CALLBACK (on_frame_end), NULL);
g_signal_connect (renderer, "layer-render", G_CALLBACK (on_layer), NULL);
```

## API Overview

### Window Management

```c
/* Create window */
LrgGrlWindow *window = lrg_grl_window_new (width, height, title);

/* Window operations */
lrg_window_begin_frame (window);
lrg_window_end_frame (window);
gboolean close = lrg_window_should_close (window);
gfloat delta = lrg_window_get_frame_time (window);
gint fps = lrg_window_get_fps (window);

/* Configuration */
lrg_window_set_target_fps (window, 60);
lrg_grl_window_toggle_fullscreen (window);
lrg_grl_window_set_vsync (window, TRUE);
```

### Camera Control

```c
/* 2D Camera */
lrg_camera2d_set_offset_xy (camera, x, y);
lrg_camera2d_set_target_xy (camera, x, y);
lrg_camera2d_set_rotation (camera, degrees);
lrg_camera2d_set_zoom (camera, zoom);

/* 3D Camera */
lrg_camera3d_set_position_xyz (camera, x, y, z);
lrg_camera3d_set_target_xyz (camera, x, y, z);
lrg_camera3d_set_fovy (camera, degrees);
lrg_camera3d_set_projection (camera, LRG_PROJECTION_PERSPECTIVE);

/* Isometric Camera */
lrg_camera_isometric_set_tile_width (camera, width);
lrg_camera_isometric_set_tile_height (camera, height);
lrg_camera_isometric_set_height_scale (camera, scale);
lrg_camera_isometric_set_zoom (camera, zoom);
lrg_camera_isometric_focus_on (camera, world_x, world_y, world_z);
lrg_camera_isometric_world_to_tile (camera, wx, wy, wz, &tile_x, &tile_y);
lrg_camera_isometric_tile_to_world (camera, tile_x, tile_y, &wx, &wz);

/* Camera begin/end (manual usage) */
lrg_camera_begin (camera);
/* Draw world content */
lrg_camera_end (camera);
```

### Rendering

```c
/* Frame management */
lrg_renderer_begin_frame (renderer);
lrg_renderer_clear (renderer, &color);
lrg_renderer_end_frame (renderer);

/* Layer management */
lrg_renderer_begin_layer (renderer, LRG_RENDER_LAYER_WORLD);
/* Draw content */
lrg_renderer_end_layer (renderer);

/* Camera */
lrg_renderer_set_camera (renderer, camera);
LrgCamera *cam = lrg_renderer_get_camera (renderer);

/* Draw drawables */
lrg_renderer_render_drawable (renderer, drawable, delta);
```

## Future Extensions

The abstract window/camera design enables:

- **GTK Window Backend**: `LrgGtkWindow` for map editors and tools
- **Vulkan/OpenGL Backends**: New window implementations as needed
- **Multiple Windows**: Each window gets its own renderer
- **Custom Cameras**: Cinematic cameras, orbit cameras, etc.
- **Render Passes**: Shadow maps, post-processing effects

## Type Reference

### LrgDrawable

Interface for renderable objects.

- **Methods**: draw, get_bounds
- **Type**: Interface

### LrgWindow

Abstract base class for windows.

- **Properties**: title, width, height, target-fps
- **Signals**: resize, close-requested, key-pressed, key-released, mouse-*
- **Type**: Abstract

### LrgGrlWindow

Graylib window implementation.

- **Methods**: toggle_fullscreen, set_vsync, get_grl_window
- **Type**: Final

### LrgCamera

Abstract base class for cameras.

- **Methods**: begin, end, world_to_screen, screen_to_world
- **Type**: Abstract

### LrgCamera2D

2D camera wrapping GrlCamera2D.

- **Properties**: offset, target, rotation, zoom
- **Type**: Derivable

### LrgCamera3D

3D camera wrapping GrlCamera3D.

- **Properties**: position-x/y/z, target-x/y/z, up-x/y/z, fovy, projection
- **Type**: Derivable

### LrgCameraIsometric

Isometric camera inheriting from LrgCamera3D with fixed isometric angle (45° horizontal, ~35.264° vertical).

- **Properties**: tile-width, tile-height, height-scale, zoom
- **Methods**: focus_on, world_to_tile, tile_to_world
- **Constraints**: Orthographic projection (locked), fixed isometric angle
- **Type**: Derivable

### LrgCameraTopDown

Top-down camera inheriting from LrgCamera2D for games like Zelda, Hotline Miami, and twin-stick shooters.

- **Properties**: follow-speed, deadzone-radius, bounds-enabled, bounds-min-x/y, bounds-max-x/y
- **Methods**: follow, set_bounds, shake, stop_shake, is_shaking
- **Features**: Smooth exponential following, circular deadzone, world bounds clamping, screen shake with decay
- **Type**: Derivable

### LrgCameraSideOn

Side-on (platformer) camera inheriting from LrgCamera2D for games like Mario, Celeste, and Hollow Knight.

- **Properties**: follow-speed-x, follow-speed-y, deadzone-width, deadzone-height, lookahead-distance, lookahead-speed, vertical-bias, bounds-enabled, bounds-min-x/y, bounds-max-x/y
- **Methods**: follow, set_deadzone, set_bounds, shake, stop_shake, is_shaking
- **Features**: Separate axis following, rectangular deadzone, horizontal lookahead based on velocity, vertical bias for ground visibility, screen shake
- **Type**: Derivable

### LrgCameraFirstPerson

First-person camera inheriting from LrgCamera3D for FPS games like Doom, Half-Life, and Counter-Strike.

- **Properties**: pitch, yaw, sensitivity-x, sensitivity-y, pitch-min, pitch-max, eye-height, head-bob-enabled, head-bob-speed, head-bob-amount, head-sway-amount
- **Methods**: rotate, set_body_position, set_pitch_limits, get_pitch_limits, update_head_bob, get_forward, get_right, get_look_direction
- **Features**: Pitch/yaw mouse look with sensitivity, pitch clamping to prevent gimbal lock, head bob with vertical bob + horizontal sway, direction vectors for movement
- **Type**: Derivable

### LrgCameraThirdPerson

Third-person camera inheriting from LrgCamera3D for action games like Dark Souls, God of War, and Tomb Raider.

- **Properties**: distance, min-distance, max-distance, pitch, yaw, pitch-min, pitch-max, sensitivity-x, sensitivity-y, height-offset, shoulder-offset, orbit-smoothing, follow-smoothing, collision-enabled, collision-radius, collision-layers
- **Methods**: orbit, follow, snap_to_target, set_distance_limits, set_pitch_limits, get_pitch_limits, set_collision_callback, get_forward, get_right, get_actual_distance
- **Features**: Spherical orbit around target, smooth following with exponential decay, over-the-shoulder offset, collision avoidance via callback, yaw wrap-around handling
- **Type**: Derivable

### LrgRenderer

Render management.

- **Properties**: window, camera, background-color
- **Signals**: frame-begin, frame-end, layer-render
- **Type**: Derivable

## See Also

- [Core Module](../core/index.md) - Engine integration
- [ECS Module](../ecs/index.md) - Components use LrgDrawable
- [UI Module](../ui/index.md) - UI widgets
- [World3D Module](../world3d/index.md) - 3D level management
