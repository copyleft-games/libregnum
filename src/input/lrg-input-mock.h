/* lrg-input-mock.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mock input source for testing and simulation.
 *
 * LrgInputMock allows programmatically setting input state for unit
 * testing and integration testing. It implements all input types
 * (keyboard, mouse, gamepad) and provides methods to control state.
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

#define LRG_TYPE_INPUT_MOCK (lrg_input_mock_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputMock, lrg_input_mock, LRG, INPUT_MOCK, LrgInput)

/**
 * LrgKeyState:
 * @LRG_KEY_STATE_UP: Key is not pressed
 * @LRG_KEY_STATE_PRESSED: Key was just pressed this frame
 * @LRG_KEY_STATE_DOWN: Key is held down
 * @LRG_KEY_STATE_RELEASED: Key was just released this frame
 *
 * States for simulated key/button input.
 */
typedef enum
{
	LRG_KEY_STATE_UP       = 0,
	LRG_KEY_STATE_PRESSED  = 1,
	LRG_KEY_STATE_DOWN     = 2,
	LRG_KEY_STATE_RELEASED = 3
} LrgKeyState;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_mock_new:
 *
 * Creates a new mock input source.
 *
 * All input is initially in the "up" state.
 *
 * Returns: (transfer full): A new #LrgInputMock
 */
LRG_AVAILABLE_IN_ALL
LrgInputMock * lrg_input_mock_new (void);

/* ==========================================================================
 * Keyboard Control
 * ========================================================================== */

/**
 * lrg_input_mock_set_key_state:
 * @self: an #LrgInputMock
 * @key: the key to set
 * @state: the state to set
 *
 * Sets the state of a keyboard key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_key_state (LrgInputMock *self,
                                   GrlKey        key,
                                   LrgKeyState   state);

/**
 * lrg_input_mock_press_key:
 * @self: an #LrgInputMock
 * @key: the key to press
 *
 * Simulates pressing a key (sets to PRESSED state).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_press_key (LrgInputMock *self,
                               GrlKey        key);

/**
 * lrg_input_mock_release_key:
 * @self: an #LrgInputMock
 * @key: the key to release
 *
 * Simulates releasing a key (sets to RELEASED state).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_release_key (LrgInputMock *self,
                                 GrlKey        key);

/**
 * lrg_input_mock_hold_key:
 * @self: an #LrgInputMock
 * @key: the key to hold
 *
 * Simulates holding a key (sets to DOWN state).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_hold_key (LrgInputMock *self,
                              GrlKey        key);

/* ==========================================================================
 * Mouse Control
 * ========================================================================== */

/**
 * lrg_input_mock_set_mouse_button_state:
 * @self: an #LrgInputMock
 * @button: the mouse button
 * @state: the state to set
 *
 * Sets the state of a mouse button.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_mouse_button_state (LrgInputMock   *self,
                                            GrlMouseButton  button,
                                            LrgKeyState     state);

/**
 * lrg_input_mock_set_mouse_position:
 * @self: an #LrgInputMock
 * @x: the X coordinate
 * @y: the Y coordinate
 *
 * Sets the mock mouse position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_mouse_position (LrgInputMock *self,
                                        gfloat        x,
                                        gfloat        y);

/**
 * lrg_input_mock_set_mouse_delta:
 * @self: an #LrgInputMock
 * @dx: the X delta
 * @dy: the Y delta
 *
 * Sets the mock mouse delta (movement since last frame).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_mouse_delta (LrgInputMock *self,
                                     gfloat        dx,
                                     gfloat        dy);

/* ==========================================================================
 * Gamepad Control
 * ========================================================================== */

/**
 * lrg_input_mock_set_gamepad_available:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @available: whether the gamepad is available
 *
 * Sets whether a gamepad is considered connected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_gamepad_available (LrgInputMock *self,
                                           gint          gamepad,
                                           gboolean      available);

/**
 * lrg_input_mock_set_gamepad_button_state:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @button: the button
 * @state: the state to set
 *
 * Sets the state of a gamepad button.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_gamepad_button_state (LrgInputMock     *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button,
                                              LrgKeyState       state);

/**
 * lrg_input_mock_set_gamepad_axis:
 * @self: an #LrgInputMock
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis
 * @value: the axis value (-1.0 to 1.0)
 *
 * Sets the value of a gamepad axis.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_set_gamepad_axis (LrgInputMock   *self,
                                      gint            gamepad,
                                      GrlGamepadAxis  axis,
                                      gfloat          value);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_input_mock_reset:
 * @self: an #LrgInputMock
 *
 * Resets all input state to defaults (all keys up, mouse at origin, etc.).
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_reset (LrgInputMock *self);

/**
 * lrg_input_mock_advance_frame:
 * @self: an #LrgInputMock
 *
 * Advances the mock input by one frame.
 *
 * This transitions PRESSED states to DOWN and RELEASED states to UP,
 * simulating the normal input lifecycle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_mock_advance_frame (LrgInputMock *self);

G_END_DECLS
