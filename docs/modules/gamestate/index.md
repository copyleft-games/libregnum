# Game State Management

The Game State module provides a state stack for managing different game screens (main menu, gameplay, pause, loading, etc.) with proper lifecycle management.

## Overview

- **LrgGameState**: Abstract base class for game states
- **LrgGameStateManager**: Manages the state stack with push/pop/replace operations

## Architecture

The state manager maintains a stack of states. Only the top state receives updates, but states below can optionally receive updates/draws based on `blocking` and `transparent` properties.

```
Stack (top to bottom):
┌─────────────────┐
│  Pause Menu     │ ← Active (receives input, update, draw)
├─────────────────┤
│  Gameplay       │ ← Paused (may receive draw if above is transparent)
├─────────────────┤
│  Main Menu      │ ← Paused
└─────────────────┘
```

## Basic Usage

```c
#include <libregnum.h>

/* Create the state manager */
LrgGameStateManager *manager = lrg_game_state_manager_new ();

/* Push the main menu as the first state */
LrgGameState *main_menu = my_main_menu_state_new ();
lrg_game_state_manager_push (manager, main_menu);

/* In game loop */
while (running)
{
    /* Process input */
    lrg_game_state_manager_handle_input (manager, event);

    /* Update (delta time in seconds) */
    lrg_game_state_manager_update (manager, delta_time);

    /* Render */
    lrg_game_state_manager_draw (manager);
}

/* Cleanup */
lrg_game_state_manager_clear (manager);
```

## State Transitions

### Push

Pushes a new state onto the stack. The current state is paused.

```c
/* Pause the game */
LrgGameState *pause_state = my_pause_menu_new ();
lrg_game_state_manager_push (manager, pause_state);
```

### Pop

Removes the current state and resumes the one below.

```c
/* Resume from pause */
lrg_game_state_manager_pop (manager);
```

### Replace

Replaces the current state without pausing the one below.

```c
/* Switch levels */
LrgGameState *level2 = my_gameplay_state_new ("level2");
lrg_game_state_manager_replace (manager, level2);
```

## Creating Custom States

Subclass `LrgGameState` and implement the virtual methods:

```c
#define MY_TYPE_MAIN_MENU (my_main_menu_get_type ())
G_DECLARE_FINAL_TYPE (MyMainMenu, my_main_menu, MY, MAIN_MENU, LrgGameState)

struct _MyMainMenu
{
    LrgGameState parent_instance;

    GrlTexture *background;
    /* UI elements... */
};

static void
my_main_menu_enter (LrgGameState *state)
{
    MyMainMenu *self = MY_MAIN_MENU (state);

    /* Initialize resources */
    self->background = load_texture ("menu_bg.png");

    /* Start menu music */
    play_music ("menu_theme.ogg");
}

static void
my_main_menu_exit (LrgGameState *state)
{
    MyMainMenu *self = MY_MAIN_MENU (state);

    /* Cleanup resources */
    g_clear_object (&self->background);
    stop_music ();
}

static void
my_main_menu_update (LrgGameState *state,
                     gdouble       delta)
{
    MyMainMenu *self = MY_MAIN_MENU (state);

    /* Update UI animations, etc. */
}

static void
my_main_menu_draw (LrgGameState *state)
{
    MyMainMenu *self = MY_MAIN_MENU (state);

    /* Draw background */
    draw_texture (self->background, 0, 0);

    /* Draw UI elements */
}

static gboolean
my_main_menu_handle_input (LrgGameState *state,
                           gpointer      event)
{
    MyMainMenu *self = MY_MAIN_MENU (state);

    /* Handle menu input */
    if (is_start_button_pressed (event))
    {
        /* Start game */
        LrgGameState *gameplay = my_gameplay_state_new ();
        lrg_game_state_manager_replace (
            get_state_manager (),
            gameplay
        );
        return TRUE;  /* Event handled */
    }

    return FALSE;  /* Event not handled */
}

static void
my_main_menu_class_init (MyMainMenuClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = my_main_menu_enter;
    state_class->exit = my_main_menu_exit;
    state_class->update = my_main_menu_update;
    state_class->draw = my_main_menu_draw;
    state_class->handle_input = my_main_menu_handle_input;
}
```

## Transparent States

Transparent states allow states below them to render:

```c
/* Create a pause menu that shows the game behind it */
LrgGameState *pause = my_pause_menu_new ();
lrg_game_state_set_transparent (pause, TRUE);
lrg_game_state_manager_push (manager, pause);

/* Now both pause menu AND gameplay will be drawn */
```

## Non-blocking States

Non-blocking states allow states below them to update:

```c
/* Create an overlay that doesn't pause the game */
LrgGameState *overlay = my_notification_overlay_new ();
lrg_game_state_set_transparent (overlay, TRUE);
lrg_game_state_set_blocking (overlay, FALSE);
lrg_game_state_manager_push (manager, overlay);

/* Both overlay AND gameplay will update and draw */
```

## API Reference

### LrgGameStateManager

| Method | Description |
|--------|-------------|
| `lrg_game_state_manager_new()` | Create new manager |
| `lrg_game_state_manager_push()` | Push state onto stack |
| `lrg_game_state_manager_pop()` | Pop top state |
| `lrg_game_state_manager_replace()` | Replace top state |
| `lrg_game_state_manager_clear()` | Clear all states |
| `lrg_game_state_manager_get_current()` | Get active state |
| `lrg_game_state_manager_get_state_count()` | Get stack depth |
| `lrg_game_state_manager_is_empty()` | Check if stack empty |
| `lrg_game_state_manager_update()` | Update active states |
| `lrg_game_state_manager_draw()` | Draw visible states |
| `lrg_game_state_manager_handle_input()` | Process input event |

### LrgGameState (Virtual Methods)

| Method | Called When | Required |
|--------|-------------|----------|
| `enter()` | State becomes active | Yes |
| `exit()` | State is removed | Yes |
| `pause()` | Another state pushed on top | No (default: no-op) |
| `resume()` | State above popped | No (default: no-op) |
| `update(delta)` | Each frame (if not blocked) | Yes |
| `update_safe(delta, error)` | Each frame with error reporting | No (default: calls update) |
| `draw()` | Each frame (if visible) | Yes |
| `handle_input(event)` | Input event received | No (default: no-op) |

### LrgGameState Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `name` | string | NULL | Debug name for state |
| `transparent` | bool | FALSE | Draw states below |
| `blocking` | bool | TRUE | Block updates to states below |

## Lifecycle Diagram

```
                    ┌─────────────────┐
                    │  State Created  │
                    └────────┬────────┘
                             │ push()
                             ▼
                    ┌─────────────────┐
     pause() ◄─────│     enter()     │
        │          └────────┬────────┘
        │                   │
        │                   ▼
        │          ┌─────────────────┐
        └─────────►│  Active Loop    │◄──── resume()
                   │  update/draw    │           │
                   └────────┬────────┘           │
                            │ pop()              │
                            │ or replace()       │
                            ▼                    │
                   ┌─────────────────┐           │
                   │     exit()      ├───────────┘
                   └────────┬────────┘   (state below)
                            │
                            ▼
                   ┌─────────────────┐
                   │ State Destroyed │
                   └─────────────────┘
```

## Safe Update with Error Handling

The `update_safe()` virtual method provides error reporting for update operations. This is useful for states that perform operations that can fail (loading resources, network operations, etc.).

### Default Behavior

By default, `update_safe()` delegates to the regular `update()` method and returns `TRUE`:

```c
/* Default implementation (you don't need to implement this) */
static gboolean
lrg_game_state_real_update_safe (LrgGameState  *self,
                                  gdouble        delta,
                                  GError       **error)
{
    lrg_game_state_update (self, delta);
    return TRUE;
}
```

### Implementing update_safe()

Override `update_safe()` when your state needs error reporting:

```c
static gboolean
my_network_state_update_safe (LrgGameState  *state,
                               gdouble        delta,
                               GError       **error)
{
    MyNetworkState *self = MY_NETWORK_STATE (state);

    /* Try to process network data */
    if (!process_network_messages (self, error))
    {
        /* Error is already set by process_network_messages */
        return FALSE;
    }

    /* Update game logic */
    update_game_world (self, delta);

    return TRUE;
}

static void
my_network_state_class_init (MyNetworkStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter = my_network_state_enter;
    state_class->exit = my_network_state_exit;
    state_class->update = my_network_state_update;        /* Regular update */
    state_class->update_safe = my_network_state_update_safe;  /* Safe update */
    state_class->draw = my_network_state_draw;
}
```

### Using update_safe() from the Manager

The game state manager can use `update_safe()` to detect and handle errors:

```c
g_autoptr(GError) error = NULL;
LrgGameState *current = lrg_game_state_manager_get_current (manager);

if (!lrg_game_state_update_safe (current, delta, &error))
{
    g_warning ("State update failed: %s", error->message);

    /* Handle error - maybe transition to error state */
    LrgGameState *error_state = my_error_state_new (error->message);
    lrg_game_state_manager_replace (manager, error_state);
}
```

### When to Use update_safe()

Use `update_safe()` when your state:

- Loads resources dynamically during updates
- Communicates with network services
- Processes external data that could be malformed
- Needs to report recoverable errors to the game loop

For simple states without failure modes, just implement `update()` - the default `update_safe()` will delegate to it automatically.
