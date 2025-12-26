# Input Binding

An `LrgInputBinding` represents a single physical input mapping - a key, mouse button, gamepad button, or gamepad axis. Bindings are the lowest level of the input system and are used by actions.

## Overview

Bindings support four types of input:

1. **Keyboard** - Any key with optional modifiers
2. **Mouse Button** - Left, right, middle with optional modifiers
3. **Gamepad Button** - 4 gamepads (0-3), standard buttons
4. **Gamepad Axis** - Stick/trigger axes with configurable thresholds

## Creating Bindings

### Keyboard Bindings

```c
/* Simple key */
LrgInputBinding *space = lrg_input_binding_new_keyboard(
    GRL_KEY_SPACE,
    LRG_INPUT_MODIFIER_NONE
);

/* With modifiers */
LrgInputBinding *ctrl_a = lrg_input_binding_new_keyboard(
    GRL_KEY_A,
    LRG_INPUT_MODIFIER_CTRL
);

/* Multiple modifiers */
LrgInputBinding *ctrl_shift_s = lrg_input_binding_new_keyboard(
    GRL_KEY_S,
    LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT
);

/* Clean up when done */
lrg_input_binding_free(space);
lrg_input_binding_free(ctrl_a);
lrg_input_binding_free(ctrl_shift_s);
```

### Mouse Button Bindings

```c
/* Left click */
LrgInputBinding *left = lrg_input_binding_new_mouse_button(
    GRL_MOUSE_BUTTON_LEFT,
    LRG_INPUT_MODIFIER_NONE
);

/* Right click with Shift */
LrgInputBinding *shift_right = lrg_input_binding_new_mouse_button(
    GRL_MOUSE_BUTTON_RIGHT,
    LRG_INPUT_MODIFIER_SHIFT
);

lrg_input_binding_free(left);
lrg_input_binding_free(shift_right);
```

### Gamepad Button Bindings

```c
/* Gamepad 0, A button */
LrgInputBinding *gamepad_a = lrg_input_binding_new_gamepad_button(
    0,  /* gamepad index 0-3 */
    GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN  /* A / Cross button */
);

/* Gamepad 1, X button */
LrgInputBinding *gamepad_x = lrg_input_binding_new_gamepad_button(
    1,
    GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT  /* X / Square button */
);

lrg_input_binding_free(gamepad_a);
lrg_input_binding_free(gamepad_x);
```

### Gamepad Axis Bindings

```c
/* Left stick right (positive X) */
LrgInputBinding *stick_right = lrg_input_binding_new_gamepad_axis(
    0,                       /* gamepad 0 */
    GRL_GAMEPAD_AXIS_LEFT_X, /* left stick X axis */
    0.5f,                    /* threshold: 50% */
    TRUE                     /* positive direction */
);

/* Left stick down (negative Y) */
LrgInputBinding *stick_down = lrg_input_binding_new_gamepad_axis(
    0,
    GRL_GAMEPAD_AXIS_LEFT_Y,
    0.5f,
    FALSE  /* negative direction */
);

/* Trigger threshold (RT > 0.2) */
LrgInputBinding *trigger = lrg_input_binding_new_gamepad_axis(
    0,
    GRL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    0.2f,
    TRUE
);

lrg_input_binding_free(stick_right);
lrg_input_binding_free(stick_down);
lrg_input_binding_free(trigger);
```

## Copying and Freeing

```c
LrgInputBinding *original = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                           LRG_INPUT_MODIFIER_NONE);

/* Make a copy */
LrgInputBinding *copy = lrg_input_binding_copy(original);

/* Both are independent */
g_assert_true(original != copy);
g_assert_cmpint(lrg_input_binding_get_key(copy), ==,
                lrg_input_binding_get_key(original));

/* Clean up */
lrg_input_binding_free(original);
lrg_input_binding_free(copy);
```

## Querying Binding Properties

### Binding Type

```c
LrgInputBinding *binding = lrg_input_binding_new_keyboard(GRL_KEY_A,
                                                          LRG_INPUT_MODIFIER_NONE);

LrgInputBindingType type = lrg_input_binding_get_binding_type(binding);

switch (type)
{
    case LRG_INPUT_BINDING_KEYBOARD:
        g_message("Keyboard binding");
        break;
    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        g_message("Mouse button binding");
        break;
    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        g_message("Gamepad button binding");
        break;
    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        g_message("Gamepad axis binding");
        break;
    default:
        break;
}
```

### Keyboard Properties

```c
LrgInputBinding *binding = lrg_input_binding_new_keyboard(
    GRL_KEY_S,
    LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT
);

GrlKey key = lrg_input_binding_get_key(binding);
LrgInputModifiers mods = lrg_input_binding_get_modifiers(binding);

if (mods & LRG_INPUT_MODIFIER_CTRL)
    g_message("Ctrl is part of binding");

if (mods & LRG_INPUT_MODIFIER_SHIFT)
    g_message("Shift is part of binding");

if (mods & LRG_INPUT_MODIFIER_ALT)
    g_message("Alt is part of binding");
```

### Mouse Button Properties

```c
LrgInputBinding *binding = lrg_input_binding_new_mouse_button(
    GRL_MOUSE_BUTTON_RIGHT,
    LRG_INPUT_MODIFIER_SHIFT
);

GrlMouseButton button = lrg_input_binding_get_mouse_button(binding);
LrgInputModifiers mods = lrg_input_binding_get_modifiers(binding);
```

### Gamepad Button Properties

```c
LrgInputBinding *binding = lrg_input_binding_new_gamepad_button(
    1,
    GRL_GAMEPAD_BUTTON_LEFT_SHOULDER
);

gint gamepad = lrg_input_binding_get_gamepad(binding);  /* 0-3 or -1 */
GrlGamepadButton button = lrg_input_binding_get_gamepad_button(binding);
```

### Gamepad Axis Properties

```c
LrgInputBinding *binding = lrg_input_binding_new_gamepad_axis(
    0,
    GRL_GAMEPAD_AXIS_LEFT_X,
    0.3f,
    TRUE
);

gint gamepad = lrg_input_binding_get_gamepad(binding);
GrlGamepadAxis axis = lrg_input_binding_get_gamepad_axis(binding);
gfloat threshold = lrg_input_binding_get_threshold(binding);
gboolean positive = lrg_input_binding_get_positive(binding);
```

## State Queries

### Frame-Based State

```c
LrgInputBinding *binding = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                          LRG_INPUT_MODIFIER_NONE);

/* Just became active this frame */
if (lrg_input_binding_is_pressed(binding))
{
    g_message("Binding was just pressed");
}

/* Currently held down */
if (lrg_input_binding_is_down(binding))
{
    g_message("Binding is held down");
}

/* Just became inactive this frame */
if (lrg_input_binding_is_released(binding))
{
    g_message("Binding was just released");
}
```

### Analog Value

```c
LrgInputBinding *binding = lrg_input_binding_new_gamepad_axis(
    0,
    GRL_GAMEPAD_AXIS_LEFT_X,
    0.2f,
    TRUE
);

/* Get raw axis value */
gfloat value = lrg_input_binding_get_axis_value(binding);
/* Returns -1.0 to 1.0 for axes */
/* Returns 1.0 if down, 0.0 if up for digital bindings */

/* Apply to movement */
player_x += value * player_speed;
```

## Display and Debug

### Human-Readable Names

```c
LrgInputBinding *space = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                        LRG_INPUT_MODIFIER_NONE);
g_autofree gchar *str = lrg_input_binding_to_string(space);
g_message("Binding: %s", str);  /* Prints "SPACE" */

LrgInputBinding *ctrl_a = lrg_input_binding_new_keyboard(
    GRL_KEY_A,
    LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT
);
g_autofree gchar *str2 = lrg_input_binding_to_string(ctrl_a);
g_message("Binding: %s", str2);  /* Prints something like "Ctrl+Shift+A" */

LrgInputBinding *gamepad = lrg_input_binding_new_gamepad_button(
    0,
    GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN
);
g_autofree gchar *str3 = lrg_input_binding_to_string(gamepad);
g_message("Binding: %s", str3);  /* Prints something like "Gamepad0 A" */
```

## Common Patterns

### Detecting Any Input

```c
void detect_input_binding(void)
{
    /* Create bindings for common keys */
    LrgInputBinding *bindings[] = {
        lrg_input_binding_new_keyboard(GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE),
        lrg_input_binding_new_keyboard(GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE),
        lrg_input_binding_new_gamepad_button(0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN),
    };

    for (guint i = 0; i < G_N_ELEMENTS(bindings); i++)
    {
        if (lrg_input_binding_is_pressed(bindings[i]))
        {
            g_autofree gchar *str = lrg_input_binding_to_string(bindings[i]);
            g_message("Pressed: %s", str);
        }
        lrg_input_binding_free(bindings[i]);
    }
}
```

### Binding Remapping UI

```c
typedef struct
{
    const gchar *action_name;
    LrgInputBinding **binding;
} RemappableAction;

void display_rebind_ui(RemappableAction *actions, guint count)
{
    for (guint i = 0; i < count; i++)
    {
        g_autofree gchar *str = lrg_input_binding_to_string(*actions[i].binding);
        g_message("%s: %s [Press a key to rebind]",
                  actions[i].action_name, str);
    }
}
```

### Axis-Based Movement

```c
void handle_movement(LrgInputMap *input_map)
{
    gfloat x = 0.0f;
    gfloat y = 0.0f;

    /* Get values from multiple movement actions */
    x += lrg_input_map_get_value(input_map, "move_right");
    x -= lrg_input_map_get_value(input_map, "move_left");
    y += lrg_input_map_get_value(input_map, "move_down");
    y -= lrg_input_map_get_value(input_map, "move_up");

    /* Clamp combined input */
    gfloat magnitude = sqrtf(x*x + y*y);
    if (magnitude > 1.0f)
    {
        x /= magnitude;
        y /= magnitude;
    }

    player_move(x * player_speed, y * player_speed);
}
```

## API Reference

### Construction

- `lrg_input_binding_new_keyboard(GrlKey key, LrgInputModifiers modifiers)`
- `lrg_input_binding_new_mouse_button(GrlMouseButton button, LrgInputModifiers modifiers)`
- `lrg_input_binding_new_gamepad_button(gint gamepad, GrlGamepadButton button)`
- `lrg_input_binding_new_gamepad_axis(gint gamepad, GrlGamepadAxis axis, gfloat threshold, gboolean positive)`

### Copy/Free

- `lrg_input_binding_copy(const LrgInputBinding *self)` - Make a copy
- `lrg_input_binding_free(LrgInputBinding *self)` - Free memory

### Properties

- `lrg_input_binding_get_binding_type(const LrgInputBinding *self)`
- `lrg_input_binding_get_key(const LrgInputBinding *self)`
- `lrg_input_binding_get_mouse_button(const LrgInputBinding *self)`
- `lrg_input_binding_get_gamepad_button(const LrgInputBinding *self)`
- `lrg_input_binding_get_gamepad_axis(const LrgInputBinding *self)`
- `lrg_input_binding_get_gamepad(const LrgInputBinding *self)`
- `lrg_input_binding_get_modifiers(const LrgInputBinding *self)`
- `lrg_input_binding_get_threshold(const LrgInputBinding *self)`
- `lrg_input_binding_get_positive(const LrgInputBinding *self)`

### State Query

- `lrg_input_binding_is_pressed(const LrgInputBinding *self)`
- `lrg_input_binding_is_down(const LrgInputBinding *self)`
- `lrg_input_binding_is_released(const LrgInputBinding *self)`
- `lrg_input_binding_get_axis_value(const LrgInputBinding *self)`

### Display

- `lrg_input_binding_to_string(const LrgInputBinding *self)` - Human-readable string

## Notes

- Bindings are value types (not reference-counted)
- Must be explicitly freed with `lrg_input_binding_free()`
- Use `g_autoptr()` for automatic cleanup
- Bindings are typically used within actions
- State is queried from the underlying input system (via graylib)

## Related

- [Input Action](input-action.md) - Logical action with multiple bindings
- [Input Map](input-map.md) - Container for actions
- [Input Index](index.md) - Input module overview
