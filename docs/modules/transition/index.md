# Transition Module

The Transition module provides smooth scene transitions including fades, wipes, dissolves, and custom shader-based effects for professional scene changes.

## Overview

The transition system uses render-to-texture to capture scenes and blend between them:

- **LrgTransition** - Abstract base class for all transitions (derivable)
- **LrgFadeTransition** - Fade to/from a color
- **LrgWipeTransition** - Directional wipe effect
- **LrgDissolveTransition** - Noise-based dissolve
- **LrgSlideTransition** - Push, cover, or reveal slides
- **LrgZoomTransition** - Zoom in/out effect
- **LrgShaderTransition** - Custom shader-based transitions
- **LrgTransitionManager** - Coordinates transitions with game state

## Quick Start

```c
/* Create a fade transition */
LrgFadeTransition *fade = lrg_fade_transition_new ();
lrg_fade_transition_set_color (fade, 0, 0, 0);  /* Fade to black */
lrg_transition_set_duration (LRG_TRANSITION (fade), 0.5f);
lrg_transition_set_easing (LRG_TRANSITION (fade), LRG_EASING_EASE_IN_OUT_QUAD);

/* Use with game state manager */
lrg_game_state_manager_push_with_transition (
    state_manager,
    new_state,
    LRG_TRANSITION (fade)
);
```

## Transition Types

### Fade Transition

```c
LrgFadeTransition *fade = lrg_fade_transition_new ();
lrg_fade_transition_set_color (fade, 0, 0, 0);      /* Black */
lrg_fade_transition_set_hold_time (fade, 0.2f);     /* Hold at midpoint */
lrg_transition_set_duration (LRG_TRANSITION (fade), 1.0f);
```

### Wipe Transition

```c
LrgWipeTransition *wipe = lrg_wipe_transition_new ();
lrg_wipe_transition_set_direction (wipe, LRG_TRANSITION_DIRECTION_LEFT);
lrg_wipe_transition_set_softness (wipe, 0.1f);  /* Edge softness */
```

### Dissolve Transition

```c
LrgDissolveTransition *dissolve = lrg_dissolve_transition_new ();
lrg_dissolve_transition_set_noise_scale (dissolve, 4.0f);
lrg_dissolve_transition_set_edge_color (dissolve, 255, 200, 100);  /* Orange glow */
```

### Slide Transition

```c
LrgSlideTransition *slide = lrg_slide_transition_new ();
lrg_slide_transition_set_direction (slide, LRG_TRANSITION_DIRECTION_RIGHT);
lrg_slide_transition_set_mode (slide, LRG_SLIDE_MODE_PUSH);  /* Push old scene */
```

### Zoom Transition

```c
LrgZoomTransition *zoom = lrg_zoom_transition_new ();
lrg_zoom_transition_set_direction (zoom, LRG_ZOOM_IN);
lrg_zoom_transition_set_center (zoom, 0.5f, 0.5f);  /* Zoom to center */
```

### Custom Shader Transition

```c
LrgShaderTransition *custom = lrg_shader_transition_new ();
lrg_shader_transition_load_shader (custom, "shaders/pixelate.frag", NULL);
lrg_shader_transition_set_uniform_float (custom, "block_size", 8.0f);
```

## Transition States

| State | Description |
|-------|-------------|
| `IDLE` | Not running |
| `OUT` | Transitioning out of current scene |
| `HOLD` | Holding at midpoint (optional) |
| `IN` | Transitioning into new scene |
| `COMPLETE` | Finished |

## Signals

```c
g_signal_connect (transition, "started", G_CALLBACK (on_start), NULL);
g_signal_connect (transition, "midpoint-reached", G_CALLBACK (on_mid), NULL);
g_signal_connect (transition, "completed", G_CALLBACK (on_complete), NULL);
```

## YAML Configuration

```yaml
type: FadeTransition
color: { r: 0, g: 0, b: 0 }
duration: 0.5
hold-time: 0.1
easing: ease-in-out-quad
```

```yaml
type: WipeTransition
direction: left
softness: 0.05
duration: 0.4
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgTransition` | Abstract base (derivable) |
| `LrgFadeTransition` | Fade to color |
| `LrgWipeTransition` | Directional wipe |
| `LrgDissolveTransition` | Noise dissolve |
| `LrgSlideTransition` | Push/cover/reveal |
| `LrgZoomTransition` | Zoom effect |
| `LrgShaderTransition` | Custom shader |
| `LrgTransitionManager` | Transition coordinator |

## Files

| File | Description |
|------|-------------|
| `src/transition/lrg-transition.h` | Base transition class |
| `src/transition/lrg-fade-transition.h` | Fade effect |
| `src/transition/lrg-wipe-transition.h` | Wipe effect |
| `src/transition/lrg-dissolve-transition.h` | Dissolve effect |
| `src/transition/lrg-slide-transition.h` | Slide effect |
| `src/transition/lrg-zoom-transition.h` | Zoom effect |
| `src/transition/lrg-shader-transition.h` | Custom shader |
| `src/transition/lrg-transition-manager.h` | Manager |
