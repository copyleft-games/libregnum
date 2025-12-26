/* lrg-input-software.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Software input source for AI and programmatic control.
 *
 * LrgInputSoftware allows AI agents, MCP servers, or other software
 * to control the game by injecting input. Unlike LrgInputMock, this
 * is designed for runtime use rather than testing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-input.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_SOFTWARE (lrg_input_software_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputSoftware, lrg_input_software, LRG, INPUT_SOFTWARE, LrgInput)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_software_new:
 *
 * Creates a new software input source.
 *
 * Returns: (transfer full): A new #LrgInputSoftware
 */
LRG_AVAILABLE_IN_ALL
LrgInputSoftware * lrg_input_software_new (void);

/* ==========================================================================
 * Keyboard Control
 * ========================================================================== */

/**
 * lrg_input_software_press_key:
 * @self: an #LrgInputSoftware
 * @key: the key to press
 *
 * Injects a key press event.
 *
 * The key will be reported as pressed for one frame, then held down
 * until released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_press_key (LrgInputSoftware *self,
                                   GrlKey            key);

/**
 * lrg_input_software_release_key:
 * @self: an #LrgInputSoftware
 * @key: the key to release
 *
 * Injects a key release event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_release_key (LrgInputSoftware *self,
                                     GrlKey            key);

/**
 * lrg_input_software_tap_key:
 * @self: an #LrgInputSoftware
 * @key: the key to tap
 *
 * Injects a quick key press and release.
 *
 * The key will be pressed for one frame then released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_tap_key (LrgInputSoftware *self,
                                 GrlKey            key);

/* ==========================================================================
 * Mouse Control
 * ========================================================================== */

/**
 * lrg_input_software_press_mouse_button:
 * @self: an #LrgInputSoftware
 * @button: the mouse button to press
 *
 * Injects a mouse button press.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_press_mouse_button (LrgInputSoftware *self,
                                            GrlMouseButton    button);

/**
 * lrg_input_software_release_mouse_button:
 * @self: an #LrgInputSoftware
 * @button: the mouse button to release
 *
 * Injects a mouse button release.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_release_mouse_button (LrgInputSoftware *self,
                                              GrlMouseButton    button);

/**
 * lrg_input_software_move_mouse_to:
 * @self: an #LrgInputSoftware
 * @x: the target X coordinate
 * @y: the target Y coordinate
 *
 * Moves the virtual mouse to an absolute position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_move_mouse_to (LrgInputSoftware *self,
                                       gfloat            x,
                                       gfloat            y);

/**
 * lrg_input_software_move_mouse_by:
 * @self: an #LrgInputSoftware
 * @dx: the X delta
 * @dy: the Y delta
 *
 * Moves the virtual mouse by a relative amount.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_move_mouse_by (LrgInputSoftware *self,
                                       gfloat            dx,
                                       gfloat            dy);

/* ==========================================================================
 * Gamepad Control
 * ========================================================================== */

/**
 * lrg_input_software_press_gamepad_button:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @button: the button to press
 *
 * Injects a gamepad button press.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_press_gamepad_button (LrgInputSoftware *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button);

/**
 * lrg_input_software_release_gamepad_button:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @button: the button to release
 *
 * Injects a gamepad button release.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_release_gamepad_button (LrgInputSoftware *self,
                                                gint              gamepad,
                                                GrlGamepadButton  button);

/**
 * lrg_input_software_set_gamepad_axis:
 * @self: an #LrgInputSoftware
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis
 * @value: the axis value (-1.0 to 1.0)
 *
 * Sets a virtual gamepad axis value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_set_gamepad_axis (LrgInputSoftware *self,
                                          gint              gamepad,
                                          GrlGamepadAxis    axis,
                                          gfloat            value);

/* ==========================================================================
 * Frame Management
 * ========================================================================== */

/**
 * lrg_input_software_update:
 * @self: an #LrgInputSoftware
 *
 * Updates the software input state for a new frame.
 *
 * This should be called once per frame (typically by the poll method).
 * It handles transitioning pressed states to down and clearing per-frame
 * events like taps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_update (LrgInputSoftware *self);

/**
 * lrg_input_software_clear_all:
 * @self: an #LrgInputSoftware
 *
 * Releases all currently held keys, buttons, and resets axes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_software_clear_all (LrgInputSoftware *self);

G_END_DECLS
