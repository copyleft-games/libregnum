# Input Module

The Input module provides action-based input handling with support for keyboard, mouse, and gamepad input. Unlike event-based systems, the Input module uses **logical actions** that can be triggered by multiple physical inputs, making it ideal for games and interactive applications.

## Overview

The Input module consists of three main types:

1. **LrgInputBinding** - Maps a single physical input (key, mouse button, gamepad button/axis) to a logical action
2. **LrgInputAction** - A named action that can be triggered by multiple bindings
3. **LrgInputMap** - Container for all actions with YAML serialization support

## Design Philosophy

The Input module uses an action-centric approach rather than raw events. This means:

- You define logical actions like "jump", "attack", or "move_right"
- Each action can have multiple bindings (e.g., Space key, gamepad A button, mouse left click)
- Your game code queries action states, not raw input
- Players can rebind controls without your game code changing

Example: A "jump" action could be bound to:
- Keyboard: Space key
- Gamepad: A button (Xbox), Cross button (PlayStation)
- Mouse: Left click

Your game just queries `is_action_pressed("jump")` and doesn't care which input triggered it.

## Key Features

### Multiple Binding Types

- **Keyboard** - With optional modifiers (Shift, Ctrl, Alt)
- **Mouse Buttons** - With modifier support
- **Gamepad Buttons** - 0-3 gamepads supported
- **Gamepad Axes** - Stick/trigger axes with configurable thresholds

### State Queries

For any action, binding, or input map, you can query:

- `is_pressed()` - Just became active this frame
- `is_down()` - Currently held down
- `is_released()` - Just became inactive this frame
- `get_value()` - Analog value (0.0 to 1.0)

### YAML Serialization

Input maps can be saved and loaded from YAML files, allowing players to customize and persist their control schemes.

## Basic Usage

### Creating an Action Map

```c
g_autoptr(LrgInputMap) input_map = lrg_input_map_new();

/* Create a "jump" action */
g_autoptr(LrgInputAction) jump = lrg_input_action_new("jump");

/* Add bindings */
LrgInputBinding *space_key = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                            LRG_INPUT_MODIFIER_NONE);
lrg_input_action_add_binding(jump, space_key);
lrg_input_binding_free(space_key);

/* Add gamepad binding */
LrgInputBinding *gamepad_a = lrg_input_binding_new_gamepad_button(0,
                                                                   GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
lrg_input_action_add_binding(jump, gamepad_a);
lrg_input_binding_free(gamepad_a);

/* Add action to map */
lrg_input_map_add_action(input_map, jump);
```

### Querying Actions

```c
/* In your update loop */
if (lrg_input_map_is_pressed(input_map, "jump"))
{
    player_jump();
}

if (lrg_input_map_is_down(input_map, "move_right"))
{
    player_move_right();
}

/* Analog input (for axes) */
gfloat move_amount = lrg_input_map_get_value(input_map, "move_right");
```

### Saving and Loading

```c
g_autoptr(GError) error = NULL;

/* Save to file */
if (!lrg_input_map_save_to_file(input_map, "config/input.yaml", &error))
{
    g_warning("Failed to save input map: %s", error->message);
}

/* Load from file */
g_autoptr(LrgInputMap) loaded_map = lrg_input_map_new();
if (!lrg_input_map_load_from_file(loaded_map, "config/input.yaml", &error))
{
    g_warning("Failed to load input map: %s", error->message);
}
```

## Input Binding Types

### Keyboard Bindings

- Supports all keyboard keys
- Optional modifiers: Shift, Ctrl, Alt
- Can be combined (Ctrl+Shift+A)

```c
LrgInputBinding *binding = lrg_input_binding_new_keyboard(
    GRL_KEY_A,
    LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT
);
```

### Mouse Button Bindings

- Supports left, right, middle buttons
- Optional modifiers like keyboard

```c
LrgInputBinding *binding = lrg_input_binding_new_mouse_button(
    GRL_MOUSE_BUTTON_LEFT,
    LRG_INPUT_MODIFIER_NONE
);
```

### Gamepad Button Bindings

- 4 gamepads supported (0-3)
- Standard button layout

```c
LrgInputBinding *binding = lrg_input_binding_new_gamepad_button(
    0,  /* gamepad index */
    GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN  /* A/Cross button */
);
```

### Gamepad Axis Bindings

- Left/right stick axes and triggers
- Configurable threshold and direction

```c
LrgInputBinding *binding = lrg_input_binding_new_gamepad_axis(
    0,                           /* gamepad index */
    GRL_GAMEPAD_AXIS_LEFT_X,     /* axis */
    0.5f,                        /* threshold */
    TRUE                         /* positive direction */
);
```

## YAML Format

Input maps serialize to human-readable YAML:

```yaml
actions:
  - name: jump
    bindings:
      - type: keyboard
        key: SPACE
        modifiers: NONE
      - type: gamepad_button
        gamepad: 0
        button: A

  - name: move_right
    bindings:
      - type: keyboard
        key: D
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_X
        threshold: 0.2
        positive: true
```

## Module Structure

### Files

- `lrg-input-binding.h` - Individual input binding type
- `lrg-input-action.h` - Named action with multiple bindings
- `lrg-input-map.h` - Container for actions with persistence

### Related Types

- `LrgInputBindingType` - Enumeration for binding types
- `LrgInputModifiers` - Flags for keyboard modifiers

## Common Patterns

### Dynamic Binding Creation

```c
/* Create action from config */
g_autoptr(LrgInputAction) action = lrg_input_action_new("custom_action");

/* Add multiple bindings */
for (guint i = 0; i < binding_count; i++)
{
    LrgInputBinding *binding = create_binding_from_config(config[i]);
    lrg_input_action_add_binding(action, binding);
    lrg_input_binding_free(binding);
}
```

### Detecting Binding Changes

```c
/* Query individual bindings */
guint count = lrg_input_action_get_binding_count(action);
for (guint i = 0; i < count; i++)
{
    const LrgInputBinding *binding = lrg_input_action_get_binding(action, i);
    if (lrg_input_binding_is_pressed(binding))
    {
        g_message("Binding %u triggered", i);
    }
}
```

### Control Remapping

```c
/* Find and replace a binding */
LrgInputAction *action = lrg_input_map_get_action(input_map, "jump");
lrg_input_action_clear_bindings(action);

/* Add new binding from player choice */
LrgInputBinding *new_binding = lrg_input_binding_new_keyboard(
    user_chosen_key,
    LRG_INPUT_MODIFIER_NONE
);
lrg_input_action_add_binding(action, new_binding);
lrg_input_binding_free(new_binding);
```

## Integration with Game Loop

Typical integration pattern:

```c
void game_update(void)
{
    /* Input is already polled by graylib/engine */

    /* Query actions */
    if (lrg_input_map_is_pressed(input_map, "attack"))
    {
        player_attack();
    }

    /* Analog input for movement */
    gfloat x_input = lrg_input_map_get_value(input_map, "move_right") -
                     lrg_input_map_get_value(input_map, "move_left");
    player_move(x_input * player_speed);
}
```

## Next Steps

- See [Input Actions](input-action.md) for action management
- See [Input Map](input-map.md) for map/serialization features
- See [Input Bindings](input-binding.md) for binding details
- See [Input Handling Example](../examples/input-handling.md) for complete code sample
