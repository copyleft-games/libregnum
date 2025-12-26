/* lrg-input-gamepad.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamepad input source - wraps graylib gamepad functions.
 */

#include "lrg-input-gamepad.h"

#include <graylib.h>

/**
 * LrgInputGamepad:
 *
 * Gamepad input source.
 *
 * This class provides gamepad input by wrapping graylib's gamepad
 * functions. It implements the gamepad-related virtual methods of
 * #LrgInput.
 */
struct _LrgInputGamepad
{
	LrgInput parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgInputGamepad, lrg_input_gamepad, LRG_TYPE_INPUT)

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_input_gamepad_is_gamepad_available (LrgInput *self,
                                        gint      gamepad)
{
	return grl_input_is_gamepad_available (gamepad);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_pressed (LrgInput         *self,
                                             gint              gamepad,
                                             GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_pressed (gamepad, button);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_down (LrgInput         *self,
                                          gint              gamepad,
                                          GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_down (gamepad, button);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_released (LrgInput         *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_released (gamepad, button);
}

static gfloat
lrg_input_gamepad_get_gamepad_axis (LrgInput       *self,
                                    gint            gamepad,
                                    GrlGamepadAxis  axis)
{
	return grl_input_get_gamepad_axis_movement (gamepad, axis);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_gamepad_class_init (LrgInputGamepadClass *klass)
{
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	/* Override gamepad-related virtual methods */
	input_class->is_gamepad_available      = lrg_input_gamepad_is_gamepad_available;
	input_class->is_gamepad_button_pressed = lrg_input_gamepad_is_gamepad_button_pressed;
	input_class->is_gamepad_button_down    = lrg_input_gamepad_is_gamepad_button_down;
	input_class->is_gamepad_button_released = lrg_input_gamepad_is_gamepad_button_released;
	input_class->get_gamepad_axis          = lrg_input_gamepad_get_gamepad_axis;
}

static void
lrg_input_gamepad_init (LrgInputGamepad *self)
{
	/* Set default name for this input source */
	g_object_set (self, "name", "gamepad", NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_input_gamepad_new:
 *
 * Creates a new gamepad input source.
 *
 * Returns: (transfer full): A new #LrgInputGamepad
 */
LrgInput *
lrg_input_gamepad_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_GAMEPAD, NULL);
}
