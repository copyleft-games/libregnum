# Photo Mode Module

The Photo Mode module provides a complete photo mode implementation for capturing
in-game screenshots with free camera control and UI hiding.

## Overview

Photo mode allows players to pause the game and freely position a camera to
capture screenshots. The module provides:

- **Free camera control** with smooth movement and rotation
- **UI visibility toggle** for clean screenshots
- **Screenshot capture and save** in PNG or JPG format
- **Automatic filename generation** with timestamps

## Key Types

| Type | Description |
|------|-------------|
| `LrgPhotoMode` | Singleton controller for photo mode state |
| `LrgPhotoCameraController` | Free camera movement and rotation |
| `LrgScreenshot` | Screenshot data with save functionality |

## Quick Start

### Entering Photo Mode

```c
LrgPhotoMode *mode = lrg_photo_mode_get_default ();
g_autoptr(GError) error = NULL;

/* Enter photo mode, optionally from game camera */
if (!lrg_photo_mode_enter (mode, game_camera, &error))
{
    g_warning ("Failed to enter photo mode: %s", error->message);
    return;
}

/* Photo mode is now active */
g_assert_true (lrg_photo_mode_is_active (mode));
```

### Handling Input

```c
void
update_photo_mode (gfloat delta)
{
    LrgPhotoMode *mode = lrg_photo_mode_get_default ();
    LrgPhotoCameraController *controller;

    if (!lrg_photo_mode_is_active (mode))
        return;

    controller = lrg_photo_mode_get_camera_controller (mode);

    /* Handle movement input */
    if (key_pressed (KEY_W))
        lrg_photo_camera_controller_move_forward (controller, delta);
    if (key_pressed (KEY_S))
        lrg_photo_camera_controller_move_forward (controller, -delta);
    if (key_pressed (KEY_A))
        lrg_photo_camera_controller_move_right (controller, -delta);
    if (key_pressed (KEY_D))
        lrg_photo_camera_controller_move_right (controller, delta);
    if (key_pressed (KEY_SPACE))
        lrg_photo_camera_controller_move_up (controller, delta);
    if (key_pressed (KEY_LCTRL))
        lrg_photo_camera_controller_move_up (controller, -delta);

    /* Handle mouse look */
    lrg_photo_camera_controller_rotate (controller, mouse_dx, -mouse_dy);

    /* Toggle UI */
    if (key_just_pressed (KEY_U))
        lrg_photo_mode_toggle_ui (mode);

    /* Capture screenshot */
    if (key_just_pressed (KEY_F12))
    {
        g_autofree gchar *filename;
        g_autoptr(GError) error = NULL;

        filename = lrg_photo_mode_generate_filename (mode,
            lrg_photo_mode_get_default_format (mode));

        if (!lrg_photo_mode_capture_and_save (mode, filename,
                lrg_photo_mode_get_default_format (mode), &error))
        {
            g_warning ("Screenshot failed: %s", error->message);
        }
    }

    /* Exit photo mode */
    if (key_just_pressed (KEY_ESC))
        lrg_photo_mode_exit (mode);

    /* Update camera smoothing */
    lrg_photo_mode_update (mode, delta);
}
```

### Rendering

```c
void
render_game (void)
{
    LrgPhotoMode *mode = lrg_photo_mode_get_default ();
    LrgCamera3D *camera;

    /* Use photo camera when in photo mode */
    if (lrg_photo_mode_is_active (mode))
        camera = lrg_photo_mode_get_camera (mode);
    else
        camera = game_camera;

    /* Render world with appropriate camera */
    begin_mode_3d (camera);
    render_world ();
    end_mode_3d ();

    /* Only show UI if not hidden in photo mode */
    if (!lrg_photo_mode_is_active (mode) || lrg_photo_mode_get_ui_visible (mode))
        render_ui ();
}
```

## LrgPhotoMode

The singleton controller that manages photo mode state.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `active` | `gboolean` | Read-only, whether photo mode is active |
| `ui-visible` | `gboolean` | Whether UI should be shown |
| `screenshot-directory` | `gchar*` | Directory for saving screenshots |
| `default-format` | `LrgScreenshotFormat` | Default screenshot format |

### Signals

| Signal | Description |
|--------|-------------|
| `entered` | Emitted when photo mode is entered |
| `exited` | Emitted when photo mode is exited |
| `screenshot-taken` | Emitted when a screenshot is captured |

### Example: Responding to Signals

```c
static void
on_photo_mode_entered (LrgPhotoMode *mode, gpointer user_data)
{
    /* Pause game simulation */
    game_set_paused (TRUE);

    /* Hide HUD */
    hud_set_visible (FALSE);
}

static void
on_photo_mode_exited (LrgPhotoMode *mode, gpointer user_data)
{
    /* Resume game */
    game_set_paused (FALSE);

    /* Show HUD */
    hud_set_visible (TRUE);
}

/* Connect signals */
g_signal_connect (mode, "entered", G_CALLBACK (on_photo_mode_entered), NULL);
g_signal_connect (mode, "exited", G_CALLBACK (on_photo_mode_exited), NULL);
```

## LrgPhotoCameraController

Provides free camera movement independent of the game camera.

### Movement Configuration

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `move-speed` | `gfloat` | 10.0 | Movement speed (units/second) |
| `look-sensitivity` | `gfloat` | 0.5 | Mouse look sensitivity |
| `smoothing` | `gfloat` | 0.8 | Movement smoothing (0=instant, 1=very smooth) |
| `fov` | `gfloat` | 45.0 | Field of view in degrees |

### Rotation

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `yaw` | `gfloat` | -inf to inf | Horizontal rotation (degrees) |
| `pitch` | `gfloat` | -89 to 89 | Vertical rotation (degrees, clamped) |
| `roll` | `gfloat` | -180 to 180 | Camera tilt (degrees) |

### Example: Custom Camera Controls

```c
LrgPhotoCameraController *controller = lrg_photo_camera_controller_new ();

/* Configure for slow, smooth cinematic movement */
lrg_photo_camera_controller_set_move_speed (controller, 2.0f);
lrg_photo_camera_controller_set_smoothing (controller, 0.95f);
lrg_photo_camera_controller_set_look_sensitivity (controller, 0.2f);

/* Set starting position */
g_autoptr(GrlVector3) start = grl_vector3_new (0.0f, 5.0f, 10.0f);
lrg_photo_camera_controller_set_position (controller, start);

/* Point camera down slightly */
lrg_photo_camera_controller_set_pitch (controller, -15.0f);
```

## LrgScreenshot

Handles screenshot capture and saving.

### Capture and Save

```c
/* Capture current frame */
g_autoptr(GError) error = NULL;
g_autoptr(LrgScreenshot) screenshot = lrg_screenshot_capture (&error);

if (screenshot == NULL)
{
    g_warning ("Capture failed: %s", error->message);
    return;
}

/* Get dimensions */
g_print ("Screenshot: %dx%d\n",
         lrg_screenshot_get_width (screenshot),
         lrg_screenshot_get_height (screenshot));

/* Save as PNG */
if (!lrg_screenshot_save_png (screenshot, "screenshot.png", &error))
{
    g_warning ("Save failed: %s", error->message);
    return;
}

/* Or save as JPG with quality setting */
if (!lrg_screenshot_save_jpg (screenshot, "screenshot.jpg", 85, &error))
{
    g_warning ("Save failed: %s", error->message);
    return;
}
```

### Creating from Existing Image

```c
/* Create from a GrlImage */
g_autoptr(GrlImage) image = grl_image_load_from_file ("captured.png");
g_autoptr(LrgScreenshot) screenshot = lrg_screenshot_new_from_image (image);

/* Convert to texture for display */
g_autoptr(GrlTexture) texture = lrg_screenshot_to_texture (screenshot);
```

## Screenshot Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| `LRG_SCREENSHOT_FORMAT_PNG` | .png | Lossless compression |
| `LRG_SCREENSHOT_FORMAT_JPG` | .jpg | Lossy compression with quality setting |

## Error Handling

Photo mode operations use the `LRG_PHOTO_MODE_ERROR` domain:

| Error Code | Description |
|------------|-------------|
| `LRG_PHOTO_MODE_ERROR_FAILED` | Generic failure |
| `LRG_PHOTO_MODE_ERROR_CAPTURE` | Screenshot capture failed |
| `LRG_PHOTO_MODE_ERROR_SAVE` | Screenshot save failed |
| `LRG_PHOTO_MODE_ERROR_INVALID_FORMAT` | Invalid image format |
| `LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE` | Photo mode already active |

```c
g_autoptr(GError) error = NULL;

if (!lrg_photo_mode_enter (mode, NULL, &error))
{
    if (g_error_matches (error, LRG_PHOTO_MODE_ERROR,
                         LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE))
    {
        g_debug ("Photo mode already active");
    }
    else
    {
        g_warning ("Failed: %s", error->message);
    }
}
```

## Integration with Post-Processing

Photo mode works well with the post-processing system for applying
filters to screenshots:

```c
/* Apply temporary filter during photo mode */
if (lrg_photo_mode_is_active (mode))
{
    /* Add vignette for cinematic look */
    lrg_post_processor_set_effect_enabled (processor, "vignette", TRUE);
    lrg_vignette_set_intensity (vignette, 0.5f);

    /* Optional: apply color grading */
    lrg_post_processor_set_effect_enabled (processor, "color-grade", TRUE);
}
```

## Best Practices

1. **Pause Game Logic**: Photo mode should pause gameplay, physics, and AI
   to allow the player to compose shots without time pressure.

2. **Hide Non-Essential UI**: Toggle off HUD elements, markers, and other
   game UI during photo mode for cleaner screenshots.

3. **Preserve Camera State**: When exiting photo mode, restore the original
   camera position and orientation.

4. **Provide Feedback**: Show brief on-screen notification when screenshots
   are captured, including the save location.

5. **Support Multiple Formats**: Let players choose between PNG (lossless)
   and JPG (smaller files) based on their preference.
