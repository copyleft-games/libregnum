# Accessibility Features

The Accessibility module provides comprehensive accessibility options to make games playable by a wider audience, including players with visual, auditory, motor, and cognitive disabilities.

## Overview

- **LrgAccessibilitySettings**: Container for all accessibility preferences (extends LrgSettingsGroup)
- **LrgColorFilter**: Abstract base for colorblind filters
- **Pre-built colorblind filter implementations** (Deuteranopia, Protanopia, Tritanopia)

## Categories

### Visual Accessibility

| Feature | Description |
|---------|-------------|
| Colorblind Modes | Deuteranopia, Protanopia, Tritanopia filters |
| High Contrast | Increases contrast for better visibility |
| UI Scale | Scales UI elements (50% - 200%) |
| Reduce Motion | Reduces animations and screen shake |
| Screen Shake Intensity | Adjustable shake intensity (0% - 100%) |

### Audio Accessibility

| Feature | Description |
|---------|-------------|
| Subtitles | Enable/disable subtitle display |
| Closed Captions | Sound effect descriptions for deaf players |
| Subtitle Size | Adjustable font size (50% - 200%) |
| Subtitle Background | Background opacity for readability |
| Visual Audio Cues | Visual indicators for important sounds |

### Motor Accessibility

| Feature | Description |
|---------|-------------|
| Hold to Toggle | Convert hold actions to toggles |
| Auto-Aim | Aim assist for targeting |
| Input Timing Multiplier | Extended time windows for inputs |

### Cognitive Accessibility

| Feature | Description |
|---------|-------------|
| Objective Reminders | Periodic quest/objective reminders |
| Skip Cutscenes | Allow skipping cinematics |
| Pause During Cutscenes | Allow pausing in cutscenes |

### Screen Reader

| Feature | Description |
|---------|-------------|
| Screen Reader | Enable text-to-speech |
| Speech Rate | Adjustable TTS speed |

## Basic Usage

```c
#include <libregnum.h>

/* Get accessibility settings */
LrgSettings *settings = lrg_settings_get_default ();
LrgAccessibilitySettings *a11y;

a11y = LRG_ACCESSIBILITY_SETTINGS (
    lrg_settings_get_group (settings, "accessibility")
);

/* Enable colorblind mode */
lrg_accessibility_settings_set_colorblind_mode (a11y, LRG_COLORBLIND_DEUTERANOPIA);

/* Enable subtitles with larger text */
lrg_accessibility_settings_set_subtitles_enabled (a11y, TRUE);
lrg_accessibility_settings_set_subtitle_size (a11y, 1.5f);
lrg_accessibility_settings_set_subtitle_background (a11y, 0.8f);

/* Reduce motion for photosensitivity */
lrg_accessibility_settings_set_reduce_motion (a11y, TRUE);
lrg_accessibility_settings_set_screen_shake_intensity (a11y, 0.0f);

/* Motor accessibility */
lrg_accessibility_settings_set_hold_to_toggle (a11y, TRUE);
lrg_accessibility_settings_set_input_timing_multiplier (a11y, 2.0f);
```

## Colorblind Filters

### Available Modes

```c
typedef enum
{
    LRG_COLORBLIND_NONE = 0,      /* No filter */
    LRG_COLORBLIND_DEUTERANOPIA,  /* Red-green (deutan) */
    LRG_COLORBLIND_PROTANOPIA,    /* Red-green (protan) */
    LRG_COLORBLIND_TRITANOPIA     /* Blue-yellow */
} LrgColorblindMode;
```

### Applying Filters

Colorblind filters are typically applied as post-processing shaders:

```c
/* Check current mode */
LrgColorblindMode mode;
mode = lrg_accessibility_settings_get_colorblind_mode (a11y);

if (mode != LRG_COLORBLIND_NONE)
{
    /* Apply the corresponding shader */
    LrgColorFilter *filter = get_filter_for_mode (mode);
    gfloat matrix[16];
    lrg_color_filter_get_matrix (filter, matrix);

    /* Pass matrix to your shader */
    set_shader_uniform ("u_colorblind_matrix", matrix);
}
```

### Custom Color Filters

Create custom filters by subclassing `LrgColorFilter`:

```c
#define MY_TYPE_CUSTOM_FILTER (my_custom_filter_get_type ())
G_DECLARE_FINAL_TYPE (MyCustomFilter, my_custom_filter, MY, CUSTOM_FILTER, LrgColorFilter)

static void
my_custom_filter_get_matrix (LrgColorFilter *filter,
                             gfloat          matrix[16])
{
    /* Fill in your custom color transformation matrix */
    /* Identity matrix example: */
    matrix[0] = 1.0f;  matrix[1] = 0.0f;  matrix[2] = 0.0f;  matrix[3] = 0.0f;
    matrix[4] = 0.0f;  matrix[5] = 1.0f;  matrix[6] = 0.0f;  matrix[7] = 0.0f;
    matrix[8] = 0.0f;  matrix[9] = 0.0f;  matrix[10] = 1.0f; matrix[11] = 0.0f;
    matrix[12] = 0.0f; matrix[13] = 0.0f; matrix[14] = 0.0f; matrix[15] = 1.0f;
}

static void
my_custom_filter_class_init (MyCustomFilterClass *klass)
{
    LrgColorFilterClass *filter_class = LRG_COLOR_FILTER_CLASS (klass);
    filter_class->get_matrix = my_custom_filter_get_matrix;
}
```

## Using Accessibility Settings in Game Logic

### Respecting Reduce Motion

```c
void
play_screen_shake (gfloat intensity, gfloat duration)
{
    LrgAccessibilitySettings *a11y = get_accessibility_settings ();

    if (lrg_accessibility_settings_get_reduce_motion (a11y))
    {
        /* Skip or reduce the effect */
        return;
    }

    gfloat actual_intensity = intensity *
        lrg_accessibility_settings_get_screen_shake_intensity (a11y);

    if (actual_intensity > 0.0f)
    {
        do_screen_shake (actual_intensity, duration);
    }
}
```

### Hold-to-Toggle Support

```c
void
handle_sprint_input (gboolean is_held)
{
    LrgAccessibilitySettings *a11y = get_accessibility_settings ();
    static gboolean is_sprinting = FALSE;

    if (lrg_accessibility_settings_get_hold_to_toggle (a11y))
    {
        /* Toggle mode: press once to start, press again to stop */
        if (is_held && !was_held_last_frame)
        {
            is_sprinting = !is_sprinting;
        }
    }
    else
    {
        /* Hold mode: sprint while button is held */
        is_sprinting = is_held;
    }

    set_player_sprinting (is_sprinting);
}
```

### Extended Input Timing

```c
gfloat
get_input_window_duration (gfloat base_duration)
{
    LrgAccessibilitySettings *a11y = get_accessibility_settings ();
    gfloat multiplier;

    multiplier = lrg_accessibility_settings_get_input_timing_multiplier (a11y);

    return base_duration * multiplier;
}
```

## Settings Persistence

Accessibility settings are automatically saved with other settings:

```yaml
accessibility:
  colorblind-mode: deuteranopia
  high-contrast: false
  ui-scale: 1.25
  reduce-motion: true
  screen-shake-intensity: 0.0
  subtitles-enabled: true
  closed-captions: true
  subtitle-size: 1.5
  subtitle-background: 0.8
  visual-audio-cues: true
  hold-to-toggle: true
  auto-aim: false
  input-timing-multiplier: 1.5
  objective-reminders: true
  skip-cutscenes: true
  pause-during-cutscenes: true
  screen-reader-enabled: false
  screen-reader-rate: 1.0
```

## API Reference

### LrgAccessibilitySettings

#### Visual

| Method | Description |
|--------|-------------|
| `get/set_colorblind_mode()` | Colorblind filter mode |
| `get/set_high_contrast()` | High contrast mode |
| `get/set_ui_scale()` | UI scale factor (0.5-2.0) |
| `get/set_reduce_motion()` | Reduce animations |
| `get/set_screen_shake_intensity()` | Shake intensity (0.0-1.0) |

#### Audio

| Method | Description |
|--------|-------------|
| `get/set_subtitles_enabled()` | Enable subtitles |
| `get/set_closed_captions()` | Enable closed captions |
| `get/set_subtitle_size()` | Font size multiplier |
| `get/set_subtitle_background()` | Background opacity |
| `get/set_visual_audio_cues()` | Visual sound indicators |

#### Motor

| Method | Description |
|--------|-------------|
| `get/set_hold_to_toggle()` | Convert holds to toggles |
| `get/set_auto_aim()` | Enable auto-aim |
| `get/set_input_timing_multiplier()` | Input window multiplier (1.0-3.0) |

#### Cognitive

| Method | Description |
|--------|-------------|
| `get/set_objective_reminders()` | Enable objective reminders |
| `get/set_skip_cutscenes()` | Allow skipping cutscenes |
| `get/set_pause_during_cutscenes()` | Allow pausing in cutscenes |

#### Screen Reader

| Method | Description |
|--------|-------------|
| `get/set_screen_reader_enabled()` | Enable TTS |
| `get/set_screen_reader_rate()` | Speech rate (0.5-2.0) |

## Legal Considerations

Many regions have accessibility requirements for software:

- **Europe**: European Accessibility Act (EAA) - applies to games from 2025
- **USA**: CVAA - applies to games with online communication
- **WCAG**: Web Content Accessibility Guidelines (voluntary but recommended)

Implementing these features helps ensure compliance and makes your game accessible to the ~15% of the global population with disabilities.
