/* lrg-input-binding.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input binding representing a single key/button/axis mapping.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-input-binding.h"
#include "lrg-input-manager.h"
#include "lrg-input-gamepad.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgInputBinding
{
    LrgInputBindingType  type;
    LrgInputModifiers    modifiers;
    gint                 gamepad;       /* 0-3 for gamepad, -1 for KB/mouse */
    union {
        GrlKey           key;
        GrlMouseButton   mouse_button;
        GrlGamepadButton gamepad_button;
        struct {
            GrlGamepadAxis axis;
            gfloat         threshold;
            gboolean       positive;
        } axis;
    } input;
};

G_DEFINE_BOXED_TYPE (LrgInputBinding, lrg_input_binding,
                     lrg_input_binding_copy, lrg_input_binding_free)

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * check_modifiers:
 *
 * Checks if the required modifier keys are currently held.
 */
static gboolean
check_modifiers (LrgInputModifiers required)
{
    LrgInputManager *input;
    gboolean         shift_ok;
    gboolean         ctrl_ok;
    gboolean         alt_ok;

    input = lrg_input_manager_get_default ();

    /* If no modifiers required, or specific modifier is required and held */
    shift_ok = !(required & LRG_INPUT_MODIFIER_SHIFT) ||
               lrg_input_manager_is_key_down (input, GRL_KEY_LEFT_SHIFT) ||
               lrg_input_manager_is_key_down (input, GRL_KEY_RIGHT_SHIFT);

    ctrl_ok = !(required & LRG_INPUT_MODIFIER_CTRL) ||
              lrg_input_manager_is_key_down (input, GRL_KEY_LEFT_CONTROL) ||
              lrg_input_manager_is_key_down (input, GRL_KEY_RIGHT_CONTROL);

    alt_ok = !(required & LRG_INPUT_MODIFIER_ALT) ||
             lrg_input_manager_is_key_down (input, GRL_KEY_LEFT_ALT) ||
             lrg_input_manager_is_key_down (input, GRL_KEY_RIGHT_ALT);

    return shift_ok && ctrl_ok && alt_ok;
}

/*
 * key_to_string:
 *
 * Converts a keyboard key to a display string.
 */
static const gchar *
key_to_string (GrlKey key)
{
    switch (key)
    {
    case GRL_KEY_SPACE: return "Space";
    case GRL_KEY_ESCAPE: return "Escape";
    case GRL_KEY_ENTER: return "Enter";
    case GRL_KEY_TAB: return "Tab";
    case GRL_KEY_BACKSPACE: return "Backspace";
    case GRL_KEY_INSERT: return "Insert";
    case GRL_KEY_DELETE: return "Delete";
    case GRL_KEY_RIGHT: return "Right";
    case GRL_KEY_LEFT: return "Left";
    case GRL_KEY_DOWN: return "Down";
    case GRL_KEY_UP: return "Up";
    case GRL_KEY_PAGE_UP: return "PageUp";
    case GRL_KEY_PAGE_DOWN: return "PageDown";
    case GRL_KEY_HOME: return "Home";
    case GRL_KEY_END: return "End";
    case GRL_KEY_CAPS_LOCK: return "CapsLock";
    case GRL_KEY_SCROLL_LOCK: return "ScrollLock";
    case GRL_KEY_NUM_LOCK: return "NumLock";
    case GRL_KEY_PRINT_SCREEN: return "PrintScreen";
    case GRL_KEY_PAUSE: return "Pause";
    case GRL_KEY_F1: return "F1";
    case GRL_KEY_F2: return "F2";
    case GRL_KEY_F3: return "F3";
    case GRL_KEY_F4: return "F4";
    case GRL_KEY_F5: return "F5";
    case GRL_KEY_F6: return "F6";
    case GRL_KEY_F7: return "F7";
    case GRL_KEY_F8: return "F8";
    case GRL_KEY_F9: return "F9";
    case GRL_KEY_F10: return "F10";
    case GRL_KEY_F11: return "F11";
    case GRL_KEY_F12: return "F12";
    case GRL_KEY_LEFT_SHIFT: return "LeftShift";
    case GRL_KEY_LEFT_CONTROL: return "LeftCtrl";
    case GRL_KEY_LEFT_ALT: return "LeftAlt";
    case GRL_KEY_RIGHT_SHIFT: return "RightShift";
    case GRL_KEY_RIGHT_CONTROL: return "RightCtrl";
    case GRL_KEY_RIGHT_ALT: return "RightAlt";
    default:
        /* For printable characters (A-Z, 0-9, etc.) */
        if (key >= GRL_KEY_A && key <= GRL_KEY_Z)
        {
            static gchar buf[2];
            buf[0] = (gchar)key;
            buf[1] = '\0';
            return buf;
        }
        if (key >= GRL_KEY_ZERO && key <= GRL_KEY_NINE)
        {
            static gchar buf[2];
            buf[0] = (gchar)key;
            buf[1] = '\0';
            return buf;
        }
        return "Unknown";
    }
}

/*
 * mouse_button_to_string:
 *
 * Converts a mouse button to a display string.
 */
static const gchar *
mouse_button_to_string (GrlMouseButton button)
{
    switch (button)
    {
    case GRL_MOUSE_BUTTON_LEFT: return "LeftMouse";
    case GRL_MOUSE_BUTTON_RIGHT: return "RightMouse";
    case GRL_MOUSE_BUTTON_MIDDLE: return "MiddleMouse";
    case GRL_MOUSE_BUTTON_SIDE: return "SideMouse";
    case GRL_MOUSE_BUTTON_EXTRA: return "ExtraMouse";
    case GRL_MOUSE_BUTTON_FORWARD: return "ForwardMouse";
    case GRL_MOUSE_BUTTON_BACK: return "BackMouse";
    default: return "UnknownMouse";
    }
}

/*
 * gamepad_button_to_string:
 *
 * Converts a gamepad button to a display string.
 */
static const gchar *
gamepad_button_to_string (GrlGamepadButton button)
{
    switch (button)
    {
    case GRL_GAMEPAD_BUTTON_LEFT_FACE_UP: return "DPadUp";
    case GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "DPadRight";
    case GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "DPadDown";
    case GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "DPadLeft";
    case GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP: return "Y";
    case GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "B";
    case GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "A";
    case GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "X";
    case GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LB";
    case GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LT";
    case GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RB";
    case GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RT";
    case GRL_GAMEPAD_BUTTON_MIDDLE_LEFT: return "Select";
    case GRL_GAMEPAD_BUTTON_MIDDLE: return "Guide";
    case GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT: return "Start";
    case GRL_GAMEPAD_BUTTON_LEFT_THUMB: return "L3";
    case GRL_GAMEPAD_BUTTON_RIGHT_THUMB: return "R3";
    default: return "UnknownButton";
    }
}

/*
 * gamepad_axis_to_string:
 *
 * Converts a gamepad axis to a display string.
 */
static const gchar *
gamepad_axis_to_string (GrlGamepadAxis axis)
{
    switch (axis)
    {
    case GRL_GAMEPAD_AXIS_LEFT_X: return "LeftStickX";
    case GRL_GAMEPAD_AXIS_LEFT_Y: return "LeftStickY";
    case GRL_GAMEPAD_AXIS_RIGHT_X: return "RightStickX";
    case GRL_GAMEPAD_AXIS_RIGHT_Y: return "RightStickY";
    case GRL_GAMEPAD_AXIS_LEFT_TRIGGER: return "LeftTrigger";
    case GRL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "RightTrigger";
    default: return "UnknownAxis";
    }
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_binding_new_keyboard:
 * @key: the keyboard key
 * @modifiers: modifier keys (Shift, Ctrl, Alt)
 *
 * Creates a new keyboard input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LrgInputBinding *
lrg_input_binding_new_keyboard (GrlKey            key,
                                LrgInputModifiers modifiers)
{
    LrgInputBinding *self;

    self = g_new0 (LrgInputBinding, 1);
    self->type = LRG_INPUT_BINDING_KEYBOARD;
    self->modifiers = modifiers;
    self->gamepad = -1;
    self->input.key = key;

    return self;
}

/**
 * lrg_input_binding_new_mouse_button:
 * @button: the mouse button
 * @modifiers: modifier keys (Shift, Ctrl, Alt)
 *
 * Creates a new mouse button input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LrgInputBinding *
lrg_input_binding_new_mouse_button (GrlMouseButton    button,
                                    LrgInputModifiers modifiers)
{
    LrgInputBinding *self;

    self = g_new0 (LrgInputBinding, 1);
    self->type = LRG_INPUT_BINDING_MOUSE_BUTTON;
    self->modifiers = modifiers;
    self->gamepad = -1;
    self->input.mouse_button = button;

    return self;
}

/**
 * lrg_input_binding_new_gamepad_button:
 * @gamepad: the gamepad index (0-3)
 * @button: the gamepad button
 *
 * Creates a new gamepad button input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LrgInputBinding *
lrg_input_binding_new_gamepad_button (gint             gamepad,
                                      GrlGamepadButton button)
{
    LrgInputBinding *self;

    g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, NULL);

    self = g_new0 (LrgInputBinding, 1);
    self->type = LRG_INPUT_BINDING_GAMEPAD_BUTTON;
    self->modifiers = LRG_INPUT_MODIFIER_NONE;
    self->gamepad = gamepad;
    self->input.gamepad_button = button;

    return self;
}

/**
 * lrg_input_binding_new_gamepad_axis:
 * @gamepad: the gamepad index (0-3)
 * @axis: the gamepad axis
 * @threshold: the threshold for axis activation (0.0-1.0)
 * @positive: whether to trigger on positive axis direction
 *
 * Creates a new gamepad axis input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LrgInputBinding *
lrg_input_binding_new_gamepad_axis (gint           gamepad,
                                    GrlGamepadAxis axis,
                                    gfloat         threshold,
                                    gboolean       positive)
{
    LrgInputBinding *self;

    g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, NULL);
    g_return_val_if_fail (threshold >= 0.0f && threshold <= 1.0f, NULL);

    self = g_new0 (LrgInputBinding, 1);
    self->type = LRG_INPUT_BINDING_GAMEPAD_AXIS;
    self->modifiers = LRG_INPUT_MODIFIER_NONE;
    self->gamepad = gamepad;
    self->input.axis.axis = axis;
    self->input.axis.threshold = threshold;
    self->input.axis.positive = positive;

    return self;
}

/* ==========================================================================
 * Copy/Free
 * ========================================================================== */

/**
 * lrg_input_binding_copy:
 * @self: an #LrgInputBinding
 *
 * Creates a copy of the input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LrgInputBinding *
lrg_input_binding_copy (const LrgInputBinding *self)
{
    LrgInputBinding *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgInputBinding, 1);
    *copy = *self;

    return copy;
}

/**
 * lrg_input_binding_free:
 * @self: an #LrgInputBinding
 *
 * Frees the input binding.
 */
void
lrg_input_binding_free (LrgInputBinding *self)
{
    g_free (self);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_input_binding_get_binding_type:
 * @self: an #LrgInputBinding
 *
 * Gets the type of input this binding represents.
 *
 * Returns: The binding type
 */
LrgInputBindingType
lrg_input_binding_get_binding_type (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, LRG_INPUT_BINDING_KEYBOARD);

    return self->type;
}

/**
 * lrg_input_binding_get_key:
 * @self: an #LrgInputBinding
 *
 * Gets the keyboard key for keyboard bindings.
 *
 * Returns: The key, or %GRL_KEY_NULL if not a keyboard binding
 */
GrlKey
lrg_input_binding_get_key (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, GRL_KEY_NULL);

    if (self->type != LRG_INPUT_BINDING_KEYBOARD)
    {
        return GRL_KEY_NULL;
    }

    return self->input.key;
}

/**
 * lrg_input_binding_get_mouse_button:
 * @self: an #LrgInputBinding
 *
 * Gets the mouse button for mouse button bindings.
 *
 * Returns: The mouse button
 */
GrlMouseButton
lrg_input_binding_get_mouse_button (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, GRL_MOUSE_BUTTON_LEFT);

    if (self->type != LRG_INPUT_BINDING_MOUSE_BUTTON)
    {
        return GRL_MOUSE_BUTTON_LEFT;
    }

    return self->input.mouse_button;
}

/**
 * lrg_input_binding_get_gamepad_button:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad button for gamepad button bindings.
 *
 * Returns: The gamepad button
 */
GrlGamepadButton
lrg_input_binding_get_gamepad_button (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, GRL_GAMEPAD_BUTTON_UNKNOWN);

    if (self->type != LRG_INPUT_BINDING_GAMEPAD_BUTTON)
    {
        return GRL_GAMEPAD_BUTTON_UNKNOWN;
    }

    return self->input.gamepad_button;
}

/**
 * lrg_input_binding_get_gamepad_axis:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad axis for gamepad axis bindings.
 *
 * Returns: The gamepad axis
 */
GrlGamepadAxis
lrg_input_binding_get_gamepad_axis (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, GRL_GAMEPAD_AXIS_LEFT_X);

    if (self->type != LRG_INPUT_BINDING_GAMEPAD_AXIS)
    {
        return GRL_GAMEPAD_AXIS_LEFT_X;
    }

    return self->input.axis.axis;
}

/**
 * lrg_input_binding_get_gamepad:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad index for gamepad bindings.
 *
 * Returns: The gamepad index (0-3), or -1 if not a gamepad binding
 */
gint
lrg_input_binding_get_gamepad (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, -1);

    return self->gamepad;
}

/**
 * lrg_input_binding_get_modifiers:
 * @self: an #LrgInputBinding
 *
 * Gets the modifier keys for keyboard/mouse bindings.
 *
 * Returns: The modifier flags
 */
LrgInputModifiers
lrg_input_binding_get_modifiers (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, LRG_INPUT_MODIFIER_NONE);

    return self->modifiers;
}

/**
 * lrg_input_binding_get_threshold:
 * @self: an #LrgInputBinding
 *
 * Gets the threshold for gamepad axis bindings.
 *
 * Returns: The threshold (0.0-1.0), or 0.0 if not an axis binding
 */
gfloat
lrg_input_binding_get_threshold (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);

    if (self->type != LRG_INPUT_BINDING_GAMEPAD_AXIS)
    {
        return 0.0f;
    }

    return self->input.axis.threshold;
}

/**
 * lrg_input_binding_get_positive:
 * @self: an #LrgInputBinding
 *
 * Gets whether the axis binding triggers on positive direction.
 *
 * Returns: %TRUE for positive direction, %FALSE for negative
 */
gboolean
lrg_input_binding_get_positive (const LrgInputBinding *self)
{
    g_return_val_if_fail (self != NULL, TRUE);

    if (self->type != LRG_INPUT_BINDING_GAMEPAD_AXIS)
    {
        return TRUE;
    }

    return self->input.axis.positive;
}

/* ==========================================================================
 * State Query
 * ========================================================================== */

/**
 * lrg_input_binding_is_pressed:
 * @self: an #LrgInputBinding
 *
 * Checks if this binding was just pressed this frame.
 *
 * Returns: %TRUE if just pressed
 */
gboolean
lrg_input_binding_is_pressed (const LrgInputBinding *self)
{
    LrgInputManager *input;
    gfloat           axis_value;

    g_return_val_if_fail (self != NULL, FALSE);

    input = lrg_input_manager_get_default ();

    switch (self->type)
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        if (!check_modifiers (self->modifiers))
        {
            return FALSE;
        }
        return lrg_input_manager_is_key_pressed (input, self->input.key);

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        if (!check_modifiers (self->modifiers))
        {
            return FALSE;
        }
        return lrg_input_manager_is_mouse_button_pressed (input,
                                                          self->input.mouse_button);

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        return lrg_input_manager_is_gamepad_button_pressed (input,
                                                            self->gamepad,
                                                            self->input.gamepad_button);

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        /*
         * For axes, we can't reliably detect "just pressed" without tracking
         * previous state. Return TRUE if currently past threshold.
         * TODO: Consider adding state tracking for proper press detection.
         */
        axis_value = lrg_input_manager_get_gamepad_axis (input,
                                                         self->gamepad,
                                                         self->input.axis.axis);
        if (self->input.axis.positive)
        {
            return axis_value >= self->input.axis.threshold;
        }
        else
        {
            return axis_value <= -self->input.axis.threshold;
        }

    default:
        return FALSE;
    }
}

/**
 * lrg_input_binding_is_down:
 * @self: an #LrgInputBinding
 *
 * Checks if this binding is currently held down.
 *
 * Returns: %TRUE if held down
 */
gboolean
lrg_input_binding_is_down (const LrgInputBinding *self)
{
    LrgInputManager *input;
    gfloat           axis_value;

    g_return_val_if_fail (self != NULL, FALSE);

    input = lrg_input_manager_get_default ();

    switch (self->type)
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        if (!check_modifiers (self->modifiers))
        {
            return FALSE;
        }
        return lrg_input_manager_is_key_down (input, self->input.key);

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        if (!check_modifiers (self->modifiers))
        {
            return FALSE;
        }
        return lrg_input_manager_is_mouse_button_down (input,
                                                       self->input.mouse_button);

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        return lrg_input_manager_is_gamepad_button_down (input,
                                                         self->gamepad,
                                                         self->input.gamepad_button);

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        axis_value = lrg_input_manager_get_gamepad_axis (input,
                                                         self->gamepad,
                                                         self->input.axis.axis);
        if (self->input.axis.positive)
        {
            return axis_value >= self->input.axis.threshold;
        }
        else
        {
            return axis_value <= -self->input.axis.threshold;
        }

    default:
        return FALSE;
    }
}

/**
 * lrg_input_binding_is_released:
 * @self: an #LrgInputBinding
 *
 * Checks if this binding was just released this frame.
 *
 * Returns: %TRUE if just released
 */
gboolean
lrg_input_binding_is_released (const LrgInputBinding *self)
{
    LrgInputManager *input;
    gfloat           axis_value;

    g_return_val_if_fail (self != NULL, FALSE);

    input = lrg_input_manager_get_default ();

    switch (self->type)
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        return lrg_input_manager_is_key_released (input, self->input.key);

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        return lrg_input_manager_is_mouse_button_released (input,
                                                           self->input.mouse_button);

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        return lrg_input_manager_is_gamepad_button_released (input,
                                                             self->gamepad,
                                                             self->input.gamepad_button);

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        /*
         * For axes, check if we're below threshold (released).
         * TODO: Proper release detection needs state tracking.
         */
        axis_value = lrg_input_manager_get_gamepad_axis (input,
                                                         self->gamepad,
                                                         self->input.axis.axis);
        if (self->input.axis.positive)
        {
            return axis_value < self->input.axis.threshold;
        }
        else
        {
            return axis_value > -self->input.axis.threshold;
        }

    default:
        return FALSE;
    }
}

/**
 * lrg_input_binding_get_axis_value:
 * @self: an #LrgInputBinding
 *
 * Gets the current axis value for gamepad axis bindings.
 *
 * For non-axis bindings, returns 1.0 if down, 0.0 otherwise.
 *
 * Returns: The axis value (-1.0 to 1.0)
 */
gfloat
lrg_input_binding_get_axis_value (const LrgInputBinding *self)
{
    LrgInputManager *input;

    g_return_val_if_fail (self != NULL, 0.0f);

    if (self->type == LRG_INPUT_BINDING_GAMEPAD_AXIS)
    {
        input = lrg_input_manager_get_default ();
        return lrg_input_manager_get_gamepad_axis (input,
                                                   self->gamepad,
                                                   self->input.axis.axis);
    }

    /* For digital inputs, return 1.0 if down, 0.0 otherwise */
    return lrg_input_binding_is_down (self) ? 1.0f : 0.0f;
}

/* ==========================================================================
 * Display
 * ========================================================================== */

/**
 * lrg_input_binding_to_string:
 * @self: an #LrgInputBinding
 *
 * Gets a human-readable string representation of this binding.
 *
 * Returns: (transfer full): A newly allocated string
 */
gchar *
lrg_input_binding_to_string (const LrgInputBinding *self)
{
    GString *str;

    g_return_val_if_fail (self != NULL, NULL);

    str = g_string_new (NULL);

    /* Add modifiers prefix */
    if (self->modifiers & LRG_INPUT_MODIFIER_CTRL)
    {
        g_string_append (str, "Ctrl+");
    }
    if (self->modifiers & LRG_INPUT_MODIFIER_ALT)
    {
        g_string_append (str, "Alt+");
    }
    if (self->modifiers & LRG_INPUT_MODIFIER_SHIFT)
    {
        g_string_append (str, "Shift+");
    }

    switch (self->type)
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        g_string_append (str, key_to_string (self->input.key));
        break;

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        g_string_append (str, mouse_button_to_string (self->input.mouse_button));
        break;

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        g_string_append_printf (str, "Gamepad%d %s",
                                self->gamepad,
                                gamepad_button_to_string (self->input.gamepad_button));
        break;

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        g_string_append_printf (str, "Gamepad%d %s%s",
                                self->gamepad,
                                gamepad_axis_to_string (self->input.axis.axis),
                                self->input.axis.positive ? "+" : "-");
        break;
    }

    return g_string_free (str, FALSE);
}

/**
 * lrg_input_binding_to_display_string:
 * @self: an #LrgInputBinding
 * @gamepad_type: the controller type for button/axis names
 *
 * Gets a human-readable string using controller-specific button names.
 *
 * For keyboard/mouse bindings, this is identical to lrg_input_binding_to_string().
 * For gamepad bindings, uses the appropriate names for the controller type.
 *
 * Returns: (transfer full): A newly allocated string
 */
gchar *
lrg_input_binding_to_display_string (const LrgInputBinding *self,
                                      LrgGamepadType         gamepad_type)
{
    GString     *str;
    const gchar *button_name;
    const gchar *axis_name;

    g_return_val_if_fail (self != NULL, NULL);

    str = g_string_new (NULL);

    /* Add modifiers prefix */
    if (self->modifiers & LRG_INPUT_MODIFIER_CTRL)
    {
        g_string_append (str, "Ctrl+");
    }
    if (self->modifiers & LRG_INPUT_MODIFIER_ALT)
    {
        g_string_append (str, "Alt+");
    }
    if (self->modifiers & LRG_INPUT_MODIFIER_SHIFT)
    {
        g_string_append (str, "Shift+");
    }

    switch (self->type)
    {
    case LRG_INPUT_BINDING_KEYBOARD:
        g_string_append (str, key_to_string (self->input.key));
        break;

    case LRG_INPUT_BINDING_MOUSE_BUTTON:
        g_string_append (str, mouse_button_to_string (self->input.mouse_button));
        break;

    case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
        button_name = lrg_input_gamepad_get_button_display_name_for_type (
            self->input.gamepad_button, gamepad_type);
        g_string_append_printf (str, "Gamepad%d %s",
                                self->gamepad,
                                button_name);
        break;

    case LRG_INPUT_BINDING_GAMEPAD_AXIS:
        axis_name = lrg_input_gamepad_get_axis_display_name_for_type (
            self->input.axis.axis, gamepad_type);
        g_string_append_printf (str, "Gamepad%d %s%s",
                                self->gamepad,
                                axis_name,
                                self->input.axis.positive ? "+" : "-");
        break;
    }

    return g_string_free (str, FALSE);
}
