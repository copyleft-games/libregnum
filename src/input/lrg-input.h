/* lrg-input.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for input sources.
 *
 * LrgInput provides a unified interface for all input sources (keyboard,
 * mouse, gamepad, mock, software/AI). Subclasses implement only the
 * methods relevant to their input type; base class defaults return
 * FALSE/0 for unimplemented methods.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT (lrg_input_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgInput, lrg_input, LRG, INPUT, GObject)

/**
 * LrgInputClass:
 * @parent_class: The parent class
 * @poll: Virtual method to update input state each frame
 * @is_key_pressed: Check if key was just pressed this frame
 * @is_key_down: Check if key is currently held down
 * @is_key_released: Check if key was just released this frame
 * @is_mouse_button_pressed: Check if mouse button was just pressed
 * @is_mouse_button_down: Check if mouse button is currently held
 * @is_mouse_button_released: Check if mouse button was just released
 * @get_mouse_position: Get current mouse position
 * @get_mouse_delta: Get mouse movement since last frame
 * @is_gamepad_available: Check if gamepad is connected
 * @is_gamepad_button_pressed: Check if gamepad button was just pressed
 * @is_gamepad_button_down: Check if gamepad button is currently held
 * @is_gamepad_button_released: Check if gamepad button was just released
 * @get_gamepad_axis: Get gamepad axis value (-1.0 to 1.0)
 *
 * The class structure for #LrgInput.
 *
 * Subclasses should override the virtual methods for their supported
 * input types. Default implementations return FALSE/0.
 */
struct _LrgInputClass
{
	GObjectClass parent_class;

	/*< public >*/

	/* Virtual methods - subclasses override only what they support */
	void     (*poll)                      (LrgInput       *self);

	gboolean (*is_key_pressed)            (LrgInput       *self,
	                                       GrlKey          key);
	gboolean (*is_key_down)               (LrgInput       *self,
	                                       GrlKey          key);
	gboolean (*is_key_released)           (LrgInput       *self,
	                                       GrlKey          key);

	gboolean (*is_mouse_button_pressed)   (LrgInput       *self,
	                                       GrlMouseButton  button);
	gboolean (*is_mouse_button_down)      (LrgInput       *self,
	                                       GrlMouseButton  button);
	gboolean (*is_mouse_button_released)  (LrgInput       *self,
	                                       GrlMouseButton  button);
	void     (*get_mouse_position)        (LrgInput       *self,
	                                       gfloat         *x,
	                                       gfloat         *y);
	void     (*get_mouse_delta)           (LrgInput       *self,
	                                       gfloat         *dx,
	                                       gfloat         *dy);

	gboolean (*is_gamepad_available)      (LrgInput       *self,
	                                       gint            gamepad);
	gboolean (*is_gamepad_button_pressed) (LrgInput       *self,
	                                       gint            gamepad,
	                                       GrlGamepadButton button);
	gboolean (*is_gamepad_button_down)    (LrgInput       *self,
	                                       gint            gamepad,
	                                       GrlGamepadButton button);
	gboolean (*is_gamepad_button_released)(LrgInput       *self,
	                                       gint            gamepad,
	                                       GrlGamepadButton button);
	gfloat   (*get_gamepad_axis)          (LrgInput       *self,
	                                       gint            gamepad,
	                                       GrlGamepadAxis  axis);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_input_get_name:
 * @self: an #LrgInput
 *
 * Gets the name of this input source.
 *
 * Returns: (transfer none): The input source name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_input_get_name (LrgInput *self);

/**
 * lrg_input_get_enabled:
 * @self: an #LrgInput
 *
 * Gets whether this input source is enabled.
 *
 * Disabled sources are skipped during input queries.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_get_enabled (LrgInput *self);

/**
 * lrg_input_set_enabled:
 * @self: an #LrgInput
 * @enabled: whether to enable this input source
 *
 * Sets whether this input source is enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_set_enabled (LrgInput *self,
                            gboolean  enabled);

/**
 * lrg_input_get_priority:
 * @self: an #LrgInput
 *
 * Gets the priority of this input source.
 *
 * Higher priority sources are queried first. For position queries,
 * the highest-priority enabled source wins.
 *
 * Returns: The priority value
 */
LRG_AVAILABLE_IN_ALL
gint lrg_input_get_priority (LrgInput *self);

/**
 * lrg_input_set_priority:
 * @self: an #LrgInput
 * @priority: the priority value (higher = queried first)
 *
 * Sets the priority of this input source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_set_priority (LrgInput *self,
                             gint      priority);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_input_poll:
 * @self: an #LrgInput
 *
 * Updates the input source state.
 *
 * This should be called once per frame before querying input state.
 * Subclasses override this to update their internal state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_poll (LrgInput *self);

/* Keyboard */

/**
 * lrg_input_is_key_pressed:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key was just pressed this frame.
 *
 * Returns: %TRUE if the key was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_key_pressed (LrgInput *self,
                                   GrlKey    key);

/**
 * lrg_input_is_key_down:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key is currently held down.
 *
 * Returns: %TRUE if the key is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_key_down (LrgInput *self,
                                GrlKey    key);

/**
 * lrg_input_is_key_released:
 * @self: an #LrgInput
 * @key: the key to check
 *
 * Checks if a key was just released this frame.
 *
 * Returns: %TRUE if the key was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_key_released (LrgInput *self,
                                    GrlKey    key);

/* Mouse */

/**
 * lrg_input_is_mouse_button_pressed:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_mouse_button_pressed (LrgInput       *self,
                                            GrlMouseButton  button);

/**
 * lrg_input_is_mouse_button_down:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_mouse_button_down (LrgInput       *self,
                                         GrlMouseButton  button);

/**
 * lrg_input_is_mouse_button_released:
 * @self: an #LrgInput
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_mouse_button_released (LrgInput       *self,
                                             GrlMouseButton  button);

/**
 * lrg_input_get_mouse_position:
 * @self: an #LrgInput
 * @x: (out) (optional): location to store X coordinate
 * @y: (out) (optional): location to store Y coordinate
 *
 * Gets the current mouse position.
 *
 * The position is in screen coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_get_mouse_position (LrgInput *self,
                                   gfloat   *x,
                                   gfloat   *y);

/**
 * lrg_input_get_mouse_delta:
 * @self: an #LrgInput
 * @dx: (out) (optional): location to store X delta
 * @dy: (out) (optional): location to store Y delta
 *
 * Gets the mouse movement since the last frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_get_mouse_delta (LrgInput *self,
                                gfloat   *dx,
                                gfloat   *dy);

/* Gamepad */

/**
 * lrg_input_is_gamepad_available:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 *
 * Checks if a gamepad is connected.
 *
 * Returns: %TRUE if the gamepad is available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_gamepad_available (LrgInput *self,
                                         gint      gamepad);

/**
 * lrg_input_is_gamepad_button_pressed:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_gamepad_button_pressed (LrgInput         *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button);

/**
 * lrg_input_is_gamepad_button_down:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_gamepad_button_down (LrgInput         *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button);

/**
 * lrg_input_is_gamepad_button_released:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_is_gamepad_button_released (LrgInput         *self,
                                               gint              gamepad,
                                               GrlGamepadButton  button);

/**
 * lrg_input_get_gamepad_axis:
 * @self: an #LrgInput
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to query
 *
 * Gets the current value of a gamepad axis.
 *
 * Returns: The axis value (-1.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_get_gamepad_axis (LrgInput       *self,
                                   gint            gamepad,
                                   GrlGamepadAxis  axis);

G_END_DECLS
