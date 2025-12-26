/* lrg-input-keyboard.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keyboard input source - wraps graylib keyboard functions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-input.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_KEYBOARD (lrg_input_keyboard_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputKeyboard, lrg_input_keyboard, LRG, INPUT_KEYBOARD, LrgInput)

/**
 * lrg_input_keyboard_new:
 *
 * Creates a new keyboard input source.
 *
 * Returns: (transfer full): A new #LrgInputKeyboard
 */
LRG_AVAILABLE_IN_ALL
LrgInput * lrg_input_keyboard_new (void);

G_END_DECLS
