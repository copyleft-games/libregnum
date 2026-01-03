# Template Module

The template module provides base classes for rapid game development with Libregnum. It implements common patterns and behaviors that most games need, reducing boilerplate code.

## Overview

The template system provides:

- **LrgGameTemplate**: A derivable base class that handles the main game loop, subsystem initialization, settings management, and common game behaviors
- **LrgInputBuffer**: An input buffering system for frame-perfect action games

## LrgGameTemplate

`LrgGameTemplate` is a derivable GObject that provides a complete game lifecycle framework. Games subclass this to get:

- Automatic engine, window, and subsystem initialization
- Fixed timestep game loop with accumulator pattern
- Settings persistence (load/save to XDG config directory)
- Input mapping and controller support
- Game state management integration
- Focus handling with audio ducking
- Controller disconnect detection

### Basic Usage

```c
/* Define your game type */
G_DECLARE_FINAL_TYPE (MyGame, my_game, MY, GAME, LrgGameTemplate)

struct _MyGame
{
    LrgGameTemplate parent_instance;
    /* Your game-specific data */
};

/* Override virtual methods as needed */
static void
my_game_configure (LrgGameTemplate *template)
{
    /* Set window title, dimensions, etc. before window creation */
    g_object_set (template,
                  "window-title", "My Awesome Game",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);
}

static LrgGameState *
my_game_create_initial_state (LrgGameTemplate *template)
{
    /* Return your main menu or initial game state */
    return g_object_new (MY_TYPE_MAIN_MENU_STATE, NULL);
}

static void
my_game_class_init (MyGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    template_class->configure = my_game_configure;
    template_class->create_initial_state = my_game_create_initial_state;
}

/* In main() */
int main (int argc, char *argv[])
{
    LrgGameTemplate *game = g_object_new (MY_TYPE_GAME, NULL);
    return lrg_game_template_run (game, argc, argv);
}
```

### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `window-title` | string | "Game" | Window title |
| `window-width` | int | 1280 | Initial window width |
| `window-height` | int | 720 | Initial window height |
| `fullscreen-mode` | LrgFullscreenMode | WINDOWED | Window mode |
| `fixed-timestep` | double | 1/60 | Fixed update interval |
| `max-frame-time` | double | 0.25 | Maximum frame time (spiral of death prevention) |
| `max-updates-per-frame` | int | 5 | Maximum fixed updates per frame |
| `auto-save-interval` | double | 60.0 | Auto-save interval (0 = disabled) |
| `pause-on-focus-loss` | bool | TRUE | Pause when window loses focus |
| `duck-audio-on-focus-loss` | bool | TRUE | Reduce volume when unfocused |
| `focus-loss-volume` | float | 0.2 | Volume multiplier when unfocused |
| `pause-on-controller-disconnect` | bool | TRUE | Pause when controller disconnects |
| `show-error-screen-on-crash` | bool | TRUE | Show error state on update errors |
| `show-fps` | bool | FALSE | Display FPS counter |
| `show-debug-overlay` | bool | FALSE | Show debug overlay |
| `app-id` | string | NULL | Application ID for config paths |

### Virtual Methods

Override these in your subclass:

| Method | When Called | Purpose |
|--------|-------------|---------|
| `configure` | Before window creation | Set properties, configure subsystems |
| `pre_startup` | After engine init, before window | Initialize data loaders, registries |
| `post_startup` | After initial state pushed | Start music, show logo |
| `shutdown` | Before engine shutdown | Save progress, cleanup |
| `pre_update` | Every frame, before state update | Input polling, physics step |
| `post_update` | Every frame, after state update | Audio sync, achievements |
| `fixed_update` | At fixed timestep (60Hz default) | Physics, movement |
| `create_initial_state` | During startup | Return first game state |
| `create_error_state` | On update error | Return error screen state |
| `setup_default_input` | During startup | Configure input bindings |
| `register_types` | During startup | Register types with registry |
| `on_focus_gained` | Window gains focus | Resume, restore volume |
| `on_focus_lost` | Window loses focus | Pause, duck audio |
| `on_controller_connected` | Gamepad connected | Show controller prompt |
| `on_controller_disconnected` | Gamepad disconnected | Show reconnect prompt |
| `on_save_completed` | After auto-save | Update UI |

### Fixed Timestep

The template uses a fixed timestep with accumulator pattern to ensure consistent physics regardless of frame rate:

```c
static void
my_game_fixed_update (LrgGameTemplate *template,
                      gdouble          fixed_delta)
{
    /* This runs at exactly 60Hz (or configured rate) */
    /* Use for physics, AI, movement */
    update_physics (fixed_delta);
    update_ai (fixed_delta);
}

static void
my_game_pre_update (LrgGameTemplate *template,
                    gdouble          delta)
{
    /* This runs every frame (variable rate) */
    /* Use for input, audio, rendering prep */
    process_input ();
}
```

### Game State Integration

The template integrates with `LrgGameStateManager`:

```c
/* Push a new state */
lrg_game_template_push_state (template, pause_menu);

/* Pop current state */
lrg_game_template_pop_state (template);

/* Replace current state */
lrg_game_template_replace_state (template, new_level);

/* Access state manager directly */
LrgGameStateManager *manager = lrg_game_template_get_state_manager (template);
```

### Error Handling

The template provides error boundaries via `update_safe()`:

```c
static LrgGameState *
my_game_create_error_state (LrgGameTemplate *template,
                            GError          *error)
{
    /* Return an error screen that displays the error */
    return g_object_new (MY_TYPE_ERROR_STATE,
                         "error-message", error->message,
                         NULL);
}
```

## LrgInputBuffer

Input buffering for action games that require frame-perfect timing.

### Purpose

In action games like fighting games or platformers, players often press buttons slightly before they would be valid. Input buffering stores recent inputs for a few frames, making the game more forgiving.

### Usage

```c
LrgInputBuffer *buffer = lrg_input_buffer_new (6);  /* 6 frames ~100ms at 60fps */

/* In your input handler */
if (action_pressed ("jump"))
{
    lrg_input_buffer_record (buffer, "jump");
}

/* In your game logic */
if (can_jump && lrg_input_buffer_consume (buffer, "jump", LRG_INPUT_CONTEXT_GAMEPLAY))
{
    /* Player pressed jump recently - execute it now */
    perform_jump ();
}

/* Update every frame to expire old inputs */
lrg_input_buffer_update (buffer);
```

### Input Contexts

The buffer uses contexts to prevent stale inputs across different game modes:

```c
/* When entering a menu */
lrg_input_buffer_set_context (buffer, LRG_INPUT_CONTEXT_MENU);
/* Buffer is automatically cleared when context changes */

/* Contexts: GAMEPLAY, MENU, DIALOG, CUTSCENE */
```

### API Reference

| Function | Description |
|----------|-------------|
| `lrg_input_buffer_new(frames)` | Create buffer with N frame window |
| `lrg_input_buffer_record(buffer, action)` | Record an action press |
| `lrg_input_buffer_consume(buffer, action, context)` | Check and consume buffered action |
| `lrg_input_buffer_has_action(buffer, action)` | Check without consuming |
| `lrg_input_buffer_update(buffer)` | Tick frame counters (call each frame) |
| `lrg_input_buffer_clear(buffer)` | Clear all buffered inputs |
| `lrg_input_buffer_set_context(buffer, ctx)` | Change input context |
| `lrg_input_buffer_set_enabled(buffer, enabled)` | Enable/disable buffering |

## Configuration

### Settings Path

With `app-id` set, settings are stored at:
- Linux: `$XDG_CONFIG_HOME/<app-id>/settings.yaml`
- Example: `~/.config/my-game/settings.yaml`

### Debug Output

Enable template debug output with:
```bash
G_MESSAGES_DEBUG=Libregnum-Template ./my-game
```

## Game Feel / "Juice" Systems

The template includes several systems for making games feel responsive and satisfying. These are critical for games that players describe as "feeling good."

### Hit Stop (Freeze Frames)

Brief pauses on impacts make combat feel weighty. Every great action game uses this technique.

```c
/* In your attack callback */
if (hit_enemy)
{
    lrg_game_template_hit_stop (template, 0.05);  /* 50ms freeze */
}

/* For big hits, use longer duration */
if (critical_hit)
{
    lrg_game_template_hit_stop (template, 0.1);  /* 100ms freeze */
}
```

You can also manually control time scale for slow-motion effects:

```c
/* Slow motion for dramatic moments */
lrg_game_template_set_time_scale (template, 0.25);  /* 25% speed */

/* Resume normal speed */
lrg_game_template_set_time_scale (template, 1.0);
```

### Screen Shake

Add screen shake to emphasize impacts, explosions, and other events:

```c
/* Small impact - 0.3 trauma */
lrg_game_template_shake (template, 0.3f);

/* Medium impact - 0.6 trauma */
lrg_game_template_shake (template, 0.6f);

/* Big explosion - full trauma */
lrg_game_template_shake (template, 1.0f);

/* Custom shake parameters */
lrg_game_template_shake_with_params (template,
                                      0.5f,  /* trauma */
                                      0.5f,  /* decay rate */
                                      40.0f); /* frequency */

/* Get shake offset to apply to camera */
gfloat shake_x, shake_y;
lrg_game_template_get_shake_offset (template, &shake_x, &shake_y);
camera_x += shake_x;
camera_y += shake_y;
```

Trauma is squared for shake intensity, creating natural falloff:
- 0.3 trauma = 0.09 (9%) shake intensity
- 0.6 trauma = 0.36 (36%) shake intensity
- 1.0 trauma = 1.0 (100%) shake intensity

### Audio Pitch Variation

Repeated sounds become annoying. Random pitch variation keeps audio fresh:

```c
/* Set up sound bank */
LrgSoundBank *sfx = lrg_sound_bank_new ("sfx");
lrg_sound_bank_load (sfx, "hit", "sounds/hit.wav", NULL);
lrg_game_template_set_sound_bank (template, sfx);

/* Play with default settings */
lrg_game_template_play_sound (template, "hit");

/* Play with pitch/volume variation */
/* ±2 semitones pitch, ±10% volume */
lrg_game_template_play_sound_varied (template, "hit", 2.0f, 0.1f);
```

The pitch variance is in semitones:
- 1 semitone = ~6% pitch change
- 2 semitones = ~12% pitch change
- 12 semitones = 1 octave (2x pitch)

### Camera Juice

#### Zoom Pulse

Quick zoom changes for emphasis:

```c
/* Zoom in slightly on hit */
lrg_game_template_camera_zoom_pulse (template,
                                      0.1f,   /* +10% zoom */
                                      0.2f);  /* 0.2s return */

/* Zoom out for dramatic reveal */
lrg_game_template_camera_zoom_pulse (template,
                                      -0.2f,  /* -20% zoom */
                                      0.5f);  /* 0.5s return */
```

#### Camera Follow

Smooth camera following with deadzone:

```c
/* Enable camera follow with smoothing */
lrg_game_template_set_camera_follow (template, TRUE, 0.1f);

/* Optional: set deadzone (camera won't move until target moves outside) */
lrg_game_template_set_camera_deadzone (template, 50.0f, 30.0f);

/* In your update loop */
lrg_game_template_update_camera_follow_target (template,
                                                player_x,
                                                player_y);

/* Get final camera position (includes follow + shake) */
gfloat cam_x, cam_y;
lrg_game_template_get_camera_position (template, &cam_x, &cam_y);
```

### Combining Effects

For maximum impact, combine multiple systems:

```c
static void
on_critical_hit (LrgGameTemplate *template)
{
    /* Freeze frame */
    lrg_game_template_hit_stop (template, 0.08);

    /* Screen shake */
    lrg_game_template_shake (template, 0.7f);

    /* Play sound with variation */
    lrg_game_template_play_sound_varied (template, "critical", 1.0f, 0.05f);

    /* Camera zoom pulse */
    lrg_game_template_camera_zoom_pulse (template, 0.15f, 0.25f);
}
```

## See Also

- `LrgGameState` - Game state interface
- `LrgGameStateManager` - State stack management
- `LrgSettings` - Settings persistence
- `LrgInputMap` - Input binding system
- `LrgScreenShake` - Screen shake post-effect (used internally)
- `LrgSoundBank` - Sound effect collections
