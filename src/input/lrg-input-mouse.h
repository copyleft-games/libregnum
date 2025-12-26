/* lrg-input-mouse.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mouse input source - wraps graylib mouse functions.
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

#define LRG_TYPE_INPUT_MOUSE (lrg_input_mouse_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputMouse, lrg_input_mouse, LRG, INPUT_MOUSE, LrgInput)

/**
 * lrg_input_mouse_new:
 *
 * Creates a new mouse input source.
 *
 * Returns: (transfer full): A new #LrgInputMouse
 */
LRG_AVAILABLE_IN_ALL
LrgInput * lrg_input_mouse_new (void);

G_END_DECLS
