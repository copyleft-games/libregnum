# VR Support Module

The VR module provides virtual reality support for Libregnum games through an abstract backend interface that can support different VR runtimes.

## Overview

The VR module follows an interface pattern allowing different VR runtime implementations:

- **LrgVRService** - Abstract interface for VR backends
- **LrgVRStub** - No-op implementation when VR is unavailable
- **LrgVRComfortSettings** - VR comfort options for motion sickness mitigation

## Architecture

```
LrgVRService (Interface)
    │
    ├── LrgVRStub (No-op fallback)
    └── [Future: LrgOpenVRBackend, etc.]

LrgVRComfortSettings (Standalone)
```

## Core Types

### LrgVRService

Abstract interface that defines the VR backend API:

```c
/* Check if VR runtime is available */
gboolean lrg_vr_service_is_available (LrgVRService *self);

/* Check if HMD is connected */
gboolean lrg_vr_service_is_hmd_present (LrgVRService *self);

/* Initialize VR runtime */
gboolean lrg_vr_service_initialize (LrgVRService  *self,
                                    GError       **error);

/* Shutdown VR runtime */
void lrg_vr_service_shutdown (LrgVRService *self);

/* Poll and process VR events */
void lrg_vr_service_poll_events (LrgVRService *self);

/* Get recommended render target size per eye */
void lrg_vr_service_get_recommended_render_size (LrgVRService *self,
                                                  guint        *width,
                                                  guint        *height);

/* Get eye projection matrix */
void lrg_vr_service_get_eye_projection (LrgVRService *self,
                                         LrgVREye      eye,
                                         gfloat        near_clip,
                                         gfloat        far_clip,
                                         gfloat       *matrix);

/* Get eye-to-head transform */
void lrg_vr_service_get_eye_to_head (LrgVRService *self,
                                      LrgVREye      eye,
                                      gfloat       *matrix);

/* Get HMD pose */
void lrg_vr_service_get_hmd_pose (LrgVRService *self,
                                   gfloat       *matrix);

/* Submit rendered frame to compositor */
gboolean lrg_vr_service_submit_frame (LrgVRService *self,
                                       LrgVREye      eye,
                                       guint         texture_id,
                                       GError      **error);

/* Get controller pose */
void lrg_vr_service_get_controller_pose (LrgVRService *self,
                                          LrgVRHand     hand,
                                          gfloat       *matrix);

/* Get controller button state */
guint lrg_vr_service_get_controller_buttons (LrgVRService *self,
                                              LrgVRHand     hand);

/* Get controller axis value */
gfloat lrg_vr_service_get_controller_axis (LrgVRService *self,
                                            LrgVRHand     hand,
                                            guint         axis);

/* Trigger haptic feedback */
void lrg_vr_service_trigger_haptic (LrgVRService *self,
                                     LrgVRHand     hand,
                                     gfloat        duration,
                                     gfloat        amplitude);
```

### LrgVRStub

No-op VR implementation used when no VR runtime is available:

```c
/* Create a new stub instance */
LrgVRStub *lrg_vr_stub_new (void);

/* Get the default singleton stub */
LrgVRStub *lrg_vr_stub_get_default (void);
```

The stub implementation:
- `is_available()` returns `FALSE`
- `is_hmd_present()` returns `FALSE`
- `initialize()` returns `FALSE` with `LRG_VR_ERROR_NOT_AVAILABLE`
- `submit_frame()` returns `FALSE` with `LRG_VR_ERROR_COMPOSITOR`
- Matrix functions return identity matrices
- Controller queries return no input

### LrgVRComfortSettings

VR comfort options to help reduce motion sickness:

```c
/* Create new comfort settings with defaults */
LrgVRComfortSettings *lrg_vr_comfort_settings_new (void);

/* Turning mode */
LrgVRTurnMode lrg_vr_comfort_settings_get_turn_mode (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_turn_mode (LrgVRComfortSettings *self,
                                             LrgVRTurnMode         mode);

/* Snap turn angle (15-90 degrees) */
gfloat lrg_vr_comfort_settings_get_snap_turn_angle (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_snap_turn_angle (LrgVRComfortSettings *self,
                                                   gfloat                angle);

/* Locomotion mode */
LrgVRLocomotionMode lrg_vr_comfort_settings_get_locomotion_mode (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_locomotion_mode (LrgVRComfortSettings *self,
                                                   LrgVRLocomotionMode   mode);

/* Comfort vignette */
gboolean lrg_vr_comfort_settings_get_vignette_enabled (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_vignette_enabled (LrgVRComfortSettings *self,
                                                    gboolean              enabled);

/* Vignette intensity (0.0-1.0) */
gfloat lrg_vr_comfort_settings_get_vignette_intensity (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_vignette_intensity (LrgVRComfortSettings *self,
                                                      gfloat                intensity);

/* Height adjustment (-2.0 to 2.0 meters) */
gfloat lrg_vr_comfort_settings_get_height_adjustment (LrgVRComfortSettings *self);
void lrg_vr_comfort_settings_set_height_adjustment (LrgVRComfortSettings *self,
                                                     gfloat                adjustment);
```

## Enumerations

### LrgVREye

```c
typedef enum {
    LRG_VR_EYE_LEFT,
    LRG_VR_EYE_RIGHT
} LrgVREye;
```

### LrgVRHand

```c
typedef enum {
    LRG_VR_HAND_LEFT,
    LRG_VR_HAND_RIGHT
} LrgVRHand;
```

### LrgVRControllerButton (Flags)

```c
typedef enum {
    LRG_VR_CONTROLLER_BUTTON_SYSTEM    = (1 << 0),
    LRG_VR_CONTROLLER_BUTTON_MENU      = (1 << 1),
    LRG_VR_CONTROLLER_BUTTON_GRIP      = (1 << 2),
    LRG_VR_CONTROLLER_BUTTON_TRIGGER   = (1 << 3),
    LRG_VR_CONTROLLER_BUTTON_A         = (1 << 4),
    LRG_VR_CONTROLLER_BUTTON_B         = (1 << 5),
    LRG_VR_CONTROLLER_BUTTON_X         = (1 << 6),
    LRG_VR_CONTROLLER_BUTTON_Y         = (1 << 7),
    LRG_VR_CONTROLLER_BUTTON_THUMBSTICK = (1 << 8),
    LRG_VR_CONTROLLER_BUTTON_TRACKPAD  = (1 << 9)
} LrgVRControllerButton;
```

### LrgVRTurnMode

```c
typedef enum {
    LRG_VR_TURN_MODE_SMOOTH,  /* Continuous rotation */
    LRG_VR_TURN_MODE_SNAP     /* Discrete angle steps */
} LrgVRTurnMode;
```

### LrgVRLocomotionMode

```c
typedef enum {
    LRG_VR_LOCOMOTION_SMOOTH,   /* Continuous movement */
    LRG_VR_LOCOMOTION_TELEPORT  /* Point-and-teleport */
} LrgVRLocomotionMode;
```

### LrgVRError

```c
typedef enum {
    LRG_VR_ERROR_FAILED,
    LRG_VR_ERROR_NOT_AVAILABLE,
    LRG_VR_ERROR_NOT_INITIALIZED,
    LRG_VR_ERROR_HMD_NOT_FOUND,
    LRG_VR_ERROR_COMPOSITOR
} LrgVRError;
```

## Usage Examples

### Basic VR Initialization

```c
LrgVRService *vr;
g_autoptr(GError) error = NULL;

/* Get VR service (stub if no runtime available) */
vr = LRG_VR_SERVICE (lrg_vr_stub_get_default ());

/* Check if real VR is available */
if (lrg_vr_service_is_available (vr))
{
    if (!lrg_vr_service_initialize (vr, &error))
    {
        g_warning ("VR init failed: %s", error->message);
    }
}
else
{
    g_info ("VR not available, running in desktop mode");
}
```

### Rendering for VR

```c
guint width, height;
gfloat left_projection[16], right_projection[16];
gfloat left_eye_to_head[16], right_eye_to_head[16];
gfloat hmd_pose[16];

/* Get recommended render size */
lrg_vr_service_get_recommended_render_size (vr, &width, &height);

/* Get projection matrices */
lrg_vr_service_get_eye_projection (vr, LRG_VR_EYE_LEFT,
                                    0.1f, 1000.0f, left_projection);
lrg_vr_service_get_eye_projection (vr, LRG_VR_EYE_RIGHT,
                                    0.1f, 1000.0f, right_projection);

/* Get eye-to-head transforms */
lrg_vr_service_get_eye_to_head (vr, LRG_VR_EYE_LEFT, left_eye_to_head);
lrg_vr_service_get_eye_to_head (vr, LRG_VR_EYE_RIGHT, right_eye_to_head);

/* Get HMD pose */
lrg_vr_service_get_hmd_pose (vr, hmd_pose);

/* Render left eye to texture */
/* ... render scene with left_projection and left_eye_to_head ... */
lrg_vr_service_submit_frame (vr, LRG_VR_EYE_LEFT, left_texture_id, NULL);

/* Render right eye to texture */
/* ... render scene with right_projection and right_eye_to_head ... */
lrg_vr_service_submit_frame (vr, LRG_VR_EYE_RIGHT, right_texture_id, NULL);
```

### Controller Input

```c
gfloat left_pose[16], right_pose[16];
guint left_buttons, right_buttons;

/* Get controller poses */
lrg_vr_service_get_controller_pose (vr, LRG_VR_HAND_LEFT, left_pose);
lrg_vr_service_get_controller_pose (vr, LRG_VR_HAND_RIGHT, right_pose);

/* Check button state */
left_buttons = lrg_vr_service_get_controller_buttons (vr, LRG_VR_HAND_LEFT);
right_buttons = lrg_vr_service_get_controller_buttons (vr, LRG_VR_HAND_RIGHT);

if (right_buttons & LRG_VR_CONTROLLER_BUTTON_TRIGGER)
{
    /* Trigger pressed on right controller */
    fire_weapon ();

    /* Haptic feedback */
    lrg_vr_service_trigger_haptic (vr, LRG_VR_HAND_RIGHT, 0.1f, 0.5f);
}

/* Get thumbstick axis */
gfloat stick_x = lrg_vr_service_get_controller_axis (vr, LRG_VR_HAND_LEFT, 0);
gfloat stick_y = lrg_vr_service_get_controller_axis (vr, LRG_VR_HAND_LEFT, 1);
```

### Comfort Settings

```c
LrgVRComfortSettings *comfort = lrg_vr_comfort_settings_new ();

/* Configure for maximum comfort (seated play) */
lrg_vr_comfort_settings_set_turn_mode (comfort, LRG_VR_TURN_MODE_SNAP);
lrg_vr_comfort_settings_set_snap_turn_angle (comfort, 45.0f);
lrg_vr_comfort_settings_set_locomotion_mode (comfort, LRG_VR_LOCOMOTION_TELEPORT);
lrg_vr_comfort_settings_set_vignette_enabled (comfort, TRUE);
lrg_vr_comfort_settings_set_vignette_intensity (comfort, 0.7f);
lrg_vr_comfort_settings_set_height_adjustment (comfort, -0.3f);  /* Seated */

/* Apply comfort settings during movement */
if (is_player_moving && lrg_vr_comfort_settings_get_vignette_enabled (comfort))
{
    gfloat intensity = lrg_vr_comfort_settings_get_vignette_intensity (comfort);
    apply_comfort_vignette (intensity);
}
```

## Comfort Options Explained

### Turn Modes

- **Smooth**: Continuous rotation follows thumbstick input. More immersive but can cause motion sickness.
- **Snap**: Rotation happens in discrete steps (e.g., 45 degrees). Less immersive but more comfortable.

### Locomotion Modes

- **Smooth**: Continuous movement via thumbstick. Natural but can cause motion sickness.
- **Teleport**: Point at destination and teleport. Eliminates artificial movement entirely.

### Comfort Vignette

Darkens the peripheral vision during movement, reducing the perception of motion in peripheral vision which is a major cause of VR sickness.

### Height Adjustment

Adjusts the virtual camera height for seated play or players of different heights.

## Error Handling

```c
g_autoptr(GError) error = NULL;

if (!lrg_vr_service_initialize (vr, &error))
{
    switch (error->code)
    {
    case LRG_VR_ERROR_NOT_AVAILABLE:
        /* VR runtime not installed */
        break;
    case LRG_VR_ERROR_HMD_NOT_FOUND:
        /* Runtime available but no headset connected */
        break;
    default:
        g_warning ("VR error: %s", error->message);
        break;
    }
}
```

## Properties

### LrgVRComfortSettings

| Property | Type | Range | Default | Description |
|----------|------|-------|---------|-------------|
| `turn-mode` | LrgVRTurnMode | - | SMOOTH | Turning mode |
| `snap-turn-angle` | gfloat | 15-90 | 45.0 | Snap turn angle in degrees |
| `locomotion-mode` | LrgVRLocomotionMode | - | SMOOTH | Locomotion mode |
| `vignette-enabled` | gboolean | - | FALSE | Enable comfort vignette |
| `vignette-intensity` | gfloat | 0.0-1.0 | 0.5 | Vignette intensity |
| `height-adjustment` | gfloat | -2.0-2.0 | 0.0 | Height adjustment in meters |

## Future Extensions

The VR module is designed to support additional backends:

- **OpenVR/SteamVR** - For HTC Vive, Valve Index, and compatible headsets
- **OpenXR** - Cross-platform VR standard
- **Oculus SDK** - For Oculus/Meta headsets

Implementing a new backend requires implementing the `LrgVRService` interface with the appropriate SDK calls.

## See Also

- [Input Module](../input/index.md) - General input handling
- [Camera3D](../graphics/camera3d.md) - 3D camera for non-VR rendering
- [Post-Processing](../postprocess/index.md) - Effects like comfort vignette
