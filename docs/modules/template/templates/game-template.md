# LrgGameTemplate

`LrgGameTemplate` is the base class for all game templates. It provides complete engine orchestration, lifecycle management, state management, and common game features out of the box.

## Inheritance Hierarchy

```
GObject
└── LrgGameTemplate (derivable)
    ├── LrgGame2DTemplate
    │   ├── LrgPlatformerTemplate
    │   ├── LrgTopDownTemplate
    │   ├── LrgShooter2DTemplate
    │   │   ├── LrgTwinStickTemplate
    │   │   └── LrgShmupTemplate
    │   ├── LrgRacing2DTemplate
    │   └── LrgTycoonTemplate
    ├── LrgGame3DTemplate
    │   ├── LrgFPSTemplate
    │   ├── LrgThirdPersonTemplate
    │   └── LrgRacing3DTemplate
    ├── LrgIdleTemplate
    └── LrgDeckbuilderTemplate
        ├── LrgDeckbuilderCombatTemplate
        └── LrgDeckbuilderPokerTemplate
```

## Features

- Engine initialization and shutdown
- Game loop with fixed and variable timestep updates
- State management (push/pop/replace states)
- Input mapping and global input handling
- Focus and controller connect/disconnect events
- Auto-save and settings persistence
- Game feel systems (hit stop, screen shake, camera follow)
- Sound bank integration with pitch/volume variation

## Quick Start

### Minimal Game

```c
#include <libregnum.h>

int
main (int argc, char **argv)
{
    g_autoptr(LrgGameTemplate) game = lrg_game_template_new ();

    g_object_set (game,
                  "title", "My Game",
                  "window-width", 1280,
                  "window-height", 720,
                  NULL);

    return lrg_game_template_run (game, argc, argv);
}
```

### Subclassing

```c
/* my-game.h */
#define MY_TYPE_GAME (my_game_get_type ())
G_DECLARE_FINAL_TYPE (MyGame, my_game, MY, GAME, LrgGameTemplate)

/* my-game.c */
struct _MyGame
{
    LrgGameTemplate parent_instance;
    LrgSoundBank   *sounds;
};

G_DEFINE_TYPE (MyGame, my_game, LRG_TYPE_GAME_TEMPLATE)

static void
my_game_configure (LrgGameTemplate *template)
{
    g_object_set (template,
                  "title", "My Awesome Game",
                  "window-width", 1920,
                  "window-height", 1080,
                  NULL);
}

static LrgGameState *
my_game_create_initial_state (LrgGameTemplate *template)
{
    return g_object_new (MY_TYPE_MAIN_MENU_STATE, NULL);
}

static void
my_game_setup_default_input (LrgGameTemplate *template, LrgInputMap *map)
{
    lrg_input_map_add_action (map, "jump", LRG_INPUT_KEY_SPACE);
    lrg_input_map_add_action (map, "attack", LRG_INPUT_MOUSE_LEFT);
    lrg_input_map_add_axis (map, "move_x", LRG_INPUT_GAMEPAD_LEFT_X);
}

static void
my_game_class_init (MyGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    template_class->configure = my_game_configure;
    template_class->create_initial_state = my_game_create_initial_state;
    template_class->setup_default_input = my_game_setup_default_input;
}
```

## Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `title` | `gchararray` | "Libregnum Game" | Window title |
| `window-width` | `gint` | 1280 | Initial window width |
| `window-height` | `gint` | 720 | Initial window height |
| `fullscreen` | `gboolean` | FALSE | Start in fullscreen |
| `vsync` | `gboolean` | TRUE | Enable vertical sync |
| `target-fps` | `gint` | 60 | Target frame rate |
| `fixed-timestep` | `gdouble` | 1.0/60.0 | Physics update interval |
| `auto-save-interval` | `gint` | 300 | Seconds between auto-saves (0 = disabled) |

## Virtual Methods

### Configuration

```c
void (*configure) (LrgGameTemplate *self);
```

Called before window creation. Set window properties and configure subsystems here.

### Lifecycle Hooks

```c
void (*pre_startup)  (LrgGameTemplate *self);  /* Before initial state push */
void (*post_startup) (LrgGameTemplate *self);  /* After initial state push */
void (*shutdown)     (LrgGameTemplate *self);  /* Before engine shutdown */
```

### Frame Hooks

```c
void (*pre_update)   (LrgGameTemplate *self, gdouble delta);  /* Start of frame */
void (*fixed_update) (LrgGameTemplate *self, gdouble fixed_delta);  /* Physics (0-N times) */
void (*post_update)  (LrgGameTemplate *self, gdouble delta);  /* After state update */
void (*pre_draw)     (LrgGameTemplate *self);  /* Before rendering */
void (*post_draw)    (LrgGameTemplate *self);  /* After rendering */
```

### State Factory Methods

```c
LrgGameState * (*create_initial_state)    (LrgGameTemplate *self);
LrgGameState * (*create_pause_state)      (LrgGameTemplate *self);
LrgGameState * (*create_loading_state)    (LrgGameTemplate *self);
LrgGameState * (*create_settings_state)   (LrgGameTemplate *self);
LrgGameState * (*create_error_state)      (LrgGameTemplate *self, const GError *error);
LrgGameState * (*create_controller_disconnect_state) (LrgGameTemplate *self);
```

### Input

```c
void     (*setup_default_input) (LrgGameTemplate *self, LrgInputMap *map);
gboolean (*handle_global_input) (LrgGameTemplate *self);
```

### Events

```c
void (*on_focus_gained)           (LrgGameTemplate *self);
void (*on_focus_lost)             (LrgGameTemplate *self);
void (*on_controller_connected)   (LrgGameTemplate *self, gint gamepad_id);
void (*on_controller_disconnected) (LrgGameTemplate *self, gint gamepad_id);
```

### UI and Persistence

```c
LrgTheme * (*create_theme)      (LrgGameTemplate *self);
gboolean   (*on_auto_save)      (LrgGameTemplate *self, GError **error);
void       (*on_save_completed) (LrgGameTemplate *self, gboolean success);
void       (*register_types)    (LrgGameTemplate *self, LrgRegistry *registry);
```

## State Management

```c
/* Push a new state (old state paused beneath) */
lrg_game_template_push_state (template, new_state);

/* Pop current state (return to previous) */
lrg_game_template_pop_state (template);

/* Replace current state (no return) */
lrg_game_template_replace_state (template, new_state);

/* Query current state */
LrgGameState *current = lrg_game_template_get_current_state (template);

/* Pause/resume helpers */
lrg_game_template_pause (template);
lrg_game_template_resume (template);
gboolean paused = lrg_game_template_is_paused (template);

/* Quit the game */
lrg_game_template_quit (template);
```

## Subsystem Access

```c
LrgEngine           *engine   = lrg_game_template_get_engine (template);
LrgSettings         *settings = lrg_game_template_get_settings (template);
LrgInputMap         *input    = lrg_game_template_get_input_map (template);
LrgGameStateManager *states   = lrg_game_template_get_state_manager (template);
LrgEventBus         *bus      = lrg_game_template_get_event_bus (template);
LrgTheme            *theme    = lrg_game_template_get_theme (template);
```

## Game Feel

### Hit Stop

```c
/* Brief freeze on impact (seconds) */
lrg_game_template_hit_stop (template, 0.05);  /* 50ms freeze */
```

### Screen Shake

```c
/* Add trauma (0.0 to 1.0) - intensity is trauma squared */
lrg_game_template_shake (template, 0.5f);

/* With custom decay and frequency */
lrg_game_template_shake_with_params (template, 0.5f, 0.8f, 30.0f);

/* Get current shake offset for camera */
gfloat shake_x, shake_y;
lrg_game_template_get_shake_offset (template, &shake_x, &shake_y);
```

### Time Scale

```c
/* Slow motion */
lrg_game_template_set_time_scale (template, 0.5);  /* 50% speed */

/* Fast forward */
lrg_game_template_set_time_scale (template, 2.0);  /* 200% speed */

/* Query current scale */
gdouble scale = lrg_game_template_get_time_scale (template);
```

### Camera Effects

```c
/* Zoom pulse (snap zoom, then return to normal) */
lrg_game_template_camera_zoom_pulse (template, 0.1f, 0.2f);

/* Camera follow with smoothing */
lrg_game_template_set_camera_follow (template, TRUE, 0.15f);
lrg_game_template_set_camera_deadzone (template, 64.0f, 48.0f);
lrg_game_template_update_camera_follow_target (template, player_x, player_y);

/* Query camera position */
gfloat cam_x, cam_y;
lrg_game_template_get_camera_position (template, &cam_x, &cam_y);
```

## Audio

```c
/* Set sound bank for convenience methods */
lrg_game_template_set_sound_bank (template, bank);

/* Play sound */
lrg_game_template_play_sound (template, "hit");

/* Play with pitch/volume variation (reduces audio fatigue) */
lrg_game_template_play_sound_varied (template, "hit",
                                      2.0f,   /* ±2 semitones */
                                      0.1f);  /* ±10% volume */
```

## Fixed Timestep

The template uses a semi-fixed timestep for deterministic physics:

```c
/* fixed_update is called 0-N times per frame at fixed intervals */
static void
my_game_fixed_update (LrgGameTemplate *template, gdouble fixed_delta)
{
    /* Physics runs at fixed rate (default 60Hz) */
    update_physics_world (self->physics, fixed_delta);
}

/* For render interpolation */
gdouble alpha = lrg_game_template_get_interpolation_alpha (template);
gfloat render_x = prev_x + (curr_x - prev_x) * alpha;
```

## Window Properties

These functions are available once the window is created, which occurs before `pre_startup()`.
This means you can safely call these functions from `pre_startup()`, `post_startup()`, and
during the main game loop.

```c
/* Get/set title */
const gchar *title = lrg_game_template_get_title (template);
lrg_game_template_set_title (template, "New Title");

/* Get window size */
gint width, height;
lrg_game_template_get_window_size (template, &width, &height);

/* Set window size (windowed mode only) */
lrg_game_template_set_window_size (template, 1920, 1080);

/* Fullscreen */
lrg_game_template_toggle_fullscreen (template);
gboolean fs = lrg_game_template_is_fullscreen (template);

/* Check focus */
gboolean focused = lrg_game_template_has_focus (template);
```

## Signals

### window-size-changed

```c
void
on_window_size_changed (LrgGameTemplate *template,
                        gint             width,
                        gint             height,
                        gpointer         user_data)
{
    /* Respond to programmatic window resize */
}

g_signal_connect (template, "window-size-changed",
                  G_CALLBACK (on_window_size_changed), NULL);
```

Emitted when `lrg_game_template_set_window_size()` is called. This signal fires
immediately, before the window system has processed the resize. Use this for
internal state updates that need to happen synchronously with the resize request.

**Note:** For actual window size changes (including user-initiated resizes),
query the window size in your frame update or use 2D template's `resolution-changed`
signal which handles both cases.

## Related Documentation

- [LrgGame2DTemplate](game-2d-template.md) - 2D games with virtual resolution
- [LrgGame3DTemplate](game-3d-template.md) - 3D games with camera management
- [Input Buffer](../systems/input-buffer.md) - Frame-perfect input buffering
- [Menu States](../systems/menu-states.md) - Built-in menu implementations
- [Game Feel](../systems/game-feel.md) - Hit stop, shake, juice systems
