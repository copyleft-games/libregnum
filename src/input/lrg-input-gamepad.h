/* lrg-input-gamepad.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamepad input source - wraps graylib gamepad functions.
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

#define LRG_TYPE_INPUT_GAMEPAD (lrg_input_gamepad_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputGamepad, lrg_input_gamepad, LRG, INPUT_GAMEPAD, LrgInput)

/**
 * lrg_input_gamepad_new:
 *
 * Creates a new gamepad input source.
 *
 * Returns: (transfer full): A new #LrgInputGamepad
 */
LRG_AVAILABLE_IN_ALL
LrgInput * lrg_input_gamepad_new (void);

G_END_DECLS
