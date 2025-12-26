# Input Action

An `LrgInputAction` is a named input action that can be triggered by multiple input bindings. It's the logical action concept in the action-based input system.

## Overview

An action has:

- A unique name (e.g., "jump", "attack", "move_right")
- Multiple associated bindings that trigger it
- State queries to check if it was pressed, is held down, or was released
- Value queries for analog input from gamepad axes

## Creating Actions

### Basic Creation

```c
g_autoptr(LrgInputAction) action = lrg_input_action_new("jump");
```

### Getting the Name

```c
const gchar *name = lrg_input_action_get_name(action);
g_message("Action: %s", name);
```

## Binding Management

Each action can have multiple bindings. Bindings are copied, so you retain ownership.

### Adding Bindings

```c
LrgInputAction *action = lrg_input_action_new("jump");

/* Add keyboard binding */
LrgInputBinding *space = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                        LRG_INPUT_MODIFIER_NONE);
lrg_input_action_add_binding(action, space);

/* Add gamepad binding */
LrgInputBinding *gamepad_a = lrg_input_binding_new_gamepad_button(
    0,
    GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN
);
lrg_input_action_add_binding(action, gamepad_a);

/* Add another key */
LrgInputBinding *w_key = lrg_input_binding_new_keyboard(GRL_KEY_W,
                                                        LRG_INPUT_MODIFIER_NONE);
lrg_input_action_add_binding(action, w_key);

/* Clean up - action has copied the bindings */
lrg_input_binding_free(space);
lrg_input_binding_free(gamepad_a);
lrg_input_binding_free(w_key);
```

### Querying Bindings

```c
/* Get total count */
guint count = lrg_input_action_get_binding_count(action);
g_message("Action has %u bindings", count);

/* Get specific binding */
const LrgInputBinding *binding = lrg_input_action_get_binding(action, 0);
if (binding != NULL)
{
    /* Check this specific binding */
    if (lrg_input_binding_is_pressed(binding))
    {
        g_message("Binding 0 was pressed");
    }
}

/* Out of range returns NULL */
const LrgInputBinding *invalid = lrg_input_action_get_binding(action, 999);
g_assert_null(invalid);
```

### Removing Bindings

```c
/* Remove by index */
lrg_input_action_remove_binding(action, 0);

/* Clear all bindings */
lrg_input_action_clear_bindings(action);
```

## State Queries

An action's state is the **logical OR** of all its bindings' states. If ANY binding is triggered, the action is triggered.

### Pressed State

Just became active this frame:

```c
if (lrg_input_action_is_pressed(action))
{
    g_message("Action was just pressed");
    player_attack();
}
```

### Down State

Currently held down:

```c
if (lrg_input_action_is_down(action))
{
    g_message("Action is being held");
    charge_attack();
}
```

### Released State

Just became inactive this frame:

```c
if (lrg_input_action_is_released(action))
{
    g_message("Action was just released");
    release_attack();
}
```

### Analog Value

For gamepad axes, get the maximum value from all bindings:

```c
gfloat value = lrg_input_action_get_value(action);
/* Returns 1.0 if any digital binding is down */
/* Returns axis value (0.0-1.0) for axis bindings */
/* Returns maximum absolute value if multiple axes */
```

## Common Patterns

### Remappable Actions

```c
typedef struct
{
    LrgInputAction *action;
    const gchar *name;
} GameAction;

GameAction game_actions[] = {
    { NULL, "jump" },
    { NULL, "attack" },
    { NULL, "move_left" },
    { NULL, "move_right" },
};

void setup_actions(void)
{
    for (guint i = 0; i < G_N_ELEMENTS(game_actions); i++)
    {
        game_actions[i].action = lrg_input_action_new(game_actions[i].name);
    }
}
```

### Dynamic Rebinding

```c
void rebind_action(LrgInputAction *action, LrgInputBinding *new_binding)
{
    /* Clear existing bindings */
    lrg_input_action_clear_bindings(action);

    /* Add new binding */
    lrg_input_action_add_binding(action, new_binding);
}

/* Usage */
LrgInputBinding *user_key = get_key_from_user();
rebind_action(jump_action, user_key);
lrg_input_binding_free(user_key);
```

### Multiple Input Methods

```c
void setup_movement_actions(LrgInputMap *map)
{
    g_autoptr(LrgInputAction) move_right = lrg_input_action_new("move_right");

    /* Keyboard */
    LrgInputBinding *d_key = lrg_input_binding_new_keyboard(GRL_KEY_D,
                                                            LRG_INPUT_MODIFIER_NONE);
    lrg_input_action_add_binding(move_right, d_key);

    /* Arrow key */
    LrgInputBinding *arrow = lrg_input_binding_new_keyboard(GRL_KEY_RIGHT,
                                                            LRG_INPUT_MODIFIER_NONE);
    lrg_input_action_add_binding(move_right, arrow);

    /* Gamepad stick */
    LrgInputBinding *stick = lrg_input_binding_new_gamepad_axis(
        0,
        GRL_GAMEPAD_AXIS_LEFT_X,
        0.2f,
        TRUE
    );
    lrg_input_action_add_binding(move_right, stick);

    lrg_input_binding_free(d_key);
    lrg_input_binding_free(arrow);
    lrg_input_binding_free(stick);

    lrg_input_map_add_action(map, move_right);
}
```

### Iterating Bindings

```c
void list_action_bindings(LrgInputAction *action)
{
    guint count = lrg_input_action_get_binding_count(action);
    g_message("Action '%s' has %u bindings:",
              lrg_input_action_get_name(action), count);

    for (guint i = 0; i < count; i++)
    {
        const LrgInputBinding *binding = lrg_input_action_get_binding(action, i);
        g_autofree gchar *str = lrg_input_binding_to_string(binding);
        g_message("  [%u] %s", i, str);
    }
}
```

## API Reference

### Construction

- `lrg_input_action_new(const gchar *name)` - Create a new action

### Properties

- `lrg_input_action_get_name(LrgInputAction *self)` - Get action name

### Binding Management

- `lrg_input_action_add_binding(LrgInputAction *self, const LrgInputBinding *binding)` - Add binding
- `lrg_input_action_remove_binding(LrgInputAction *self, guint index)` - Remove binding by index
- `lrg_input_action_clear_bindings(LrgInputAction *self)` - Remove all bindings
- `lrg_input_action_get_binding_count(LrgInputAction *self)` - Get binding count
- `lrg_input_action_get_binding(LrgInputAction *self, guint index)` - Get binding by index

### State Query

- `lrg_input_action_is_pressed(LrgInputAction *self)` - Just activated
- `lrg_input_action_is_down(LrgInputAction *self)` - Currently active
- `lrg_input_action_is_released(LrgInputAction *self)` - Just deactivated
- `lrg_input_action_get_value(LrgInputAction *self)` - Get analog value (0.0-1.0)

## Notes

- Actions are reference-counted GObjects
- Bindings are copied when added, not referenced
- State is computed from all bindings (logical OR)
- Analog values use maximum absolute value from all bindings
- Use with `LrgInputMap` for YAML serialization support

## Related

- [Input Binding](input-binding.md) - Individual physical input mapping
- [Input Map](input-map.md) - Container for actions with persistence
- [Input Handling Example](../examples/input-handling.md) - Complete usage example
