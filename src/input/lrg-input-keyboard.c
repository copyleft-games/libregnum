/* lrg-input-keyboard.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keyboard input source - wraps graylib keyboard functions.
 */

#include "lrg-input-keyboard.h"

#include <graylib.h>

/**
 * LrgInputKeyboard:
 *
 * Keyboard input source.
 *
 * This class provides keyboard input by wrapping graylib's keyboard
 * functions. It implements the keyboard-related virtual methods of
 * #LrgInput.
 */
struct _LrgInputKeyboard
{
	LrgInput parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgInputKeyboard, lrg_input_keyboard, LRG_TYPE_INPUT)

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_input_keyboard_is_key_pressed (LrgInput *self,
                                   GrlKey    key)
{
	return grl_input_is_key_pressed (key);
}

static gboolean
lrg_input_keyboard_is_key_down (LrgInput *self,
                                GrlKey    key)
{
	return grl_input_is_key_down (key);
}

static gboolean
lrg_input_keyboard_is_key_released (LrgInput *self,
                                    GrlKey    key)
{
	return grl_input_is_key_released (key);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_keyboard_class_init (LrgInputKeyboardClass *klass)
{
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	/* Override keyboard-related virtual methods */
	input_class->is_key_pressed  = lrg_input_keyboard_is_key_pressed;
	input_class->is_key_down     = lrg_input_keyboard_is_key_down;
	input_class->is_key_released = lrg_input_keyboard_is_key_released;
}

static void
lrg_input_keyboard_init (LrgInputKeyboard *self)
{
	/* Set default name for this input source */
	g_object_set (self, "name", "keyboard", NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_input_keyboard_new:
 *
 * Creates a new keyboard input source.
 *
 * Returns: (transfer full): A new #LrgInputKeyboard
 */
LrgInput *
lrg_input_keyboard_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_KEYBOARD, NULL);
}
