/* lrg-input-gamepad.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamepad input source - wraps graylib gamepad functions with
 * controller type detection, display names, and dead zone support.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-input.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_GAMEPAD (lrg_input_gamepad_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputGamepad, lrg_input_gamepad, LRG, INPUT_GAMEPAD, LrgInput)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_gamepad_new:
 *
 * Creates a new gamepad input source with default dead zone of 0.1.
 *
 * Returns: (transfer full): A new #LrgInputGamepad
 */
LRG_AVAILABLE_IN_ALL
LrgInput * lrg_input_gamepad_new (void);

/* ==========================================================================
 * Controller Type Detection
 * ========================================================================== */

/**
 * lrg_input_gamepad_detect_type:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 *
 * Detects the type of controller connected at the specified index.
 *
 * Detection is based on parsing the controller name string returned
 * by the system. Supports Xbox, PlayStation, Nintendo Switch, and
 * Steam Deck controllers. Unrecognized controllers return
 * %LRG_GAMEPAD_TYPE_GENERIC.
 *
 * Returns: The detected #LrgGamepadType
 */
LRG_AVAILABLE_IN_ALL
LrgGamepadType lrg_input_gamepad_detect_type (LrgInputGamepad *self,
                                               gint             gamepad);

/**
 * lrg_input_gamepad_get_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 *
 * Gets the raw name string of the connected controller.
 *
 * Returns: (transfer none) (nullable): The controller name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_gamepad_get_name (LrgInputGamepad *self,
                                           gint             gamepad);

/* ==========================================================================
 * Display Names
 * ========================================================================== */

/**
 * lrg_input_gamepad_get_button_display_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 * @button: the button to get the name for
 *
 * Gets the display name for a button based on the connected controller type.
 *
 * For example, %GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN returns:
 * - "A" for Xbox controllers
 * - "Cross" for PlayStation controllers
 * - "B" for Nintendo Switch controllers
 *
 * Returns: (transfer none): The button display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_gamepad_get_button_display_name (LrgInputGamepad  *self,
                                                          gint              gamepad,
                                                          GrlGamepadButton  button);

/**
 * lrg_input_gamepad_get_button_display_name_for_type:
 * @button: the button to get the name for
 * @gamepad_type: the controller type
 *
 * Gets the display name for a button for a specific controller type.
 *
 * This is a static helper that doesn't require a connected controller.
 * Useful for settings menus where you want to preview different layouts.
 *
 * Returns: (transfer none): The button display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_gamepad_get_button_display_name_for_type (GrlGamepadButton button,
                                                                   LrgGamepadType   gamepad_type);

/**
 * lrg_input_gamepad_get_axis_display_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to get the name for
 *
 * Gets the display name for an axis based on the connected controller type.
 *
 * Returns: (transfer none): The axis display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_gamepad_get_axis_display_name (LrgInputGamepad *self,
                                                        gint             gamepad,
                                                        GrlGamepadAxis   axis);

/**
 * lrg_input_gamepad_get_axis_display_name_for_type:
 * @axis: the axis to get the name for
 * @gamepad_type: the controller type
 *
 * Gets the display name for an axis for a specific controller type.
 *
 * Returns: (transfer none): The axis display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_gamepad_get_axis_display_name_for_type (GrlGamepadAxis axis,
                                                                 LrgGamepadType gamepad_type);

/* ==========================================================================
 * Dead Zone Configuration
 * ========================================================================== */

/**
 * lrg_input_gamepad_set_dead_zone:
 * @self: an #LrgInputGamepad
 * @dead_zone: the dead zone threshold (0.0 to 1.0)
 *
 * Sets the dead zone for analog sticks and triggers.
 *
 * Values within the dead zone are treated as 0.0. The remaining range
 * is rescaled so there's no jump at the dead zone boundary.
 *
 * Default: 0.1
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_gamepad_set_dead_zone (LrgInputGamepad *self,
                                       gfloat           dead_zone);

/**
 * lrg_input_gamepad_get_dead_zone:
 * @self: an #LrgInputGamepad
 *
 * Gets the current dead zone threshold.
 *
 * Returns: The dead zone value (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_gamepad_get_dead_zone (LrgInputGamepad *self);

G_END_DECLS
