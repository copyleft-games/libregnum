/* lrg-input-binding.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input binding representing a single key/button/axis mapping.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_TYPE_INPUT_BINDING:
 *
 * The #GType for #LrgInputBinding.
 */
#define LRG_TYPE_INPUT_BINDING (lrg_input_binding_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_input_binding_get_type (void) G_GNUC_CONST;

/**
 * LrgInputBinding:
 *
 * An opaque structure representing an input binding.
 *
 * A binding maps a physical input (keyboard key, mouse button,
 * gamepad button, or gamepad axis) to a logical action.
 */
typedef struct _LrgInputBinding LrgInputBinding;

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
LRG_AVAILABLE_IN_ALL
LrgInputBinding * lrg_input_binding_new_keyboard (GrlKey            key,
                                                  LrgInputModifiers modifiers);

/**
 * lrg_input_binding_new_mouse_button:
 * @button: the mouse button
 * @modifiers: modifier keys (Shift, Ctrl, Alt)
 *
 * Creates a new mouse button input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LRG_AVAILABLE_IN_ALL
LrgInputBinding * lrg_input_binding_new_mouse_button (GrlMouseButton    button,
                                                      LrgInputModifiers modifiers);

/**
 * lrg_input_binding_new_gamepad_button:
 * @gamepad: the gamepad index (0-3)
 * @button: the gamepad button
 *
 * Creates a new gamepad button input binding.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LRG_AVAILABLE_IN_ALL
LrgInputBinding * lrg_input_binding_new_gamepad_button (gint             gamepad,
                                                        GrlGamepadButton button);

/**
 * lrg_input_binding_new_gamepad_axis:
 * @gamepad: the gamepad index (0-3)
 * @axis: the gamepad axis
 * @threshold: the threshold for axis activation (0.0-1.0)
 * @positive: whether to trigger on positive axis direction
 *
 * Creates a new gamepad axis input binding.
 *
 * The binding triggers when the axis value exceeds the threshold
 * in the specified direction.
 *
 * Returns: (transfer full): A new #LrgInputBinding
 */
LRG_AVAILABLE_IN_ALL
LrgInputBinding * lrg_input_binding_new_gamepad_axis (gint           gamepad,
                                                      GrlGamepadAxis axis,
                                                      gfloat         threshold,
                                                      gboolean       positive);

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
LRG_AVAILABLE_IN_ALL
LrgInputBinding * lrg_input_binding_copy (const LrgInputBinding *self);

/**
 * lrg_input_binding_free:
 * @self: an #LrgInputBinding
 *
 * Frees the input binding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_binding_free (LrgInputBinding *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgInputBinding, lrg_input_binding_free)

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
LRG_AVAILABLE_IN_ALL
LrgInputBindingType lrg_input_binding_get_binding_type (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_key:
 * @self: an #LrgInputBinding
 *
 * Gets the keyboard key for keyboard bindings.
 *
 * Returns: The key, or %GRL_KEY_NULL if not a keyboard binding
 */
LRG_AVAILABLE_IN_ALL
GrlKey lrg_input_binding_get_key (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_mouse_button:
 * @self: an #LrgInputBinding
 *
 * Gets the mouse button for mouse button bindings.
 *
 * Returns: The mouse button
 */
LRG_AVAILABLE_IN_ALL
GrlMouseButton lrg_input_binding_get_mouse_button (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_gamepad_button:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad button for gamepad button bindings.
 *
 * Returns: The gamepad button
 */
LRG_AVAILABLE_IN_ALL
GrlGamepadButton lrg_input_binding_get_gamepad_button (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_gamepad_axis:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad axis for gamepad axis bindings.
 *
 * Returns: The gamepad axis
 */
LRG_AVAILABLE_IN_ALL
GrlGamepadAxis lrg_input_binding_get_gamepad_axis (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_gamepad:
 * @self: an #LrgInputBinding
 *
 * Gets the gamepad index for gamepad bindings.
 *
 * Returns: The gamepad index (0-3), or -1 if not a gamepad binding
 */
LRG_AVAILABLE_IN_ALL
gint lrg_input_binding_get_gamepad (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_modifiers:
 * @self: an #LrgInputBinding
 *
 * Gets the modifier keys for keyboard/mouse bindings.
 *
 * Returns: The modifier flags
 */
LRG_AVAILABLE_IN_ALL
LrgInputModifiers lrg_input_binding_get_modifiers (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_threshold:
 * @self: an #LrgInputBinding
 *
 * Gets the threshold for gamepad axis bindings.
 *
 * Returns: The threshold (0.0-1.0), or 0.0 if not an axis binding
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_binding_get_threshold (const LrgInputBinding *self);

/**
 * lrg_input_binding_get_positive:
 * @self: an #LrgInputBinding
 *
 * Gets whether the axis binding triggers on positive direction.
 *
 * Returns: %TRUE for positive direction, %FALSE for negative
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_binding_get_positive (const LrgInputBinding *self);

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_binding_is_pressed (const LrgInputBinding *self);

/**
 * lrg_input_binding_is_down:
 * @self: an #LrgInputBinding
 *
 * Checks if this binding is currently held down.
 *
 * Returns: %TRUE if held down
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_binding_is_down (const LrgInputBinding *self);

/**
 * lrg_input_binding_is_released:
 * @self: an #LrgInputBinding
 *
 * Checks if this binding was just released this frame.
 *
 * Returns: %TRUE if just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_binding_is_released (const LrgInputBinding *self);

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
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_binding_get_axis_value (const LrgInputBinding *self);

/* ==========================================================================
 * Display
 * ========================================================================== */

/**
 * lrg_input_binding_to_string:
 * @self: an #LrgInputBinding
 *
 * Gets a human-readable string representation of this binding.
 *
 * For gamepad bindings, uses Xbox-style button names (A, B, X, Y, LB, etc.).
 * Use lrg_input_binding_to_display_string() for controller-specific names.
 *
 * Returns: (transfer full): A newly allocated string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_input_binding_to_string (const LrgInputBinding *self);

/**
 * lrg_input_binding_to_display_string:
 * @self: an #LrgInputBinding
 * @gamepad_type: the controller type for button/axis names
 *
 * Gets a human-readable string using controller-specific button names.
 *
 * For keyboard/mouse bindings, this is identical to lrg_input_binding_to_string().
 * For gamepad bindings, uses the appropriate names for the controller type:
 * - Xbox: A, B, X, Y, LB, RB, etc.
 * - PlayStation: Cross, Circle, Square, Triangle, L1, R1, etc.
 * - Switch: B, A, Y, X, L, R, ZL, ZR, etc.
 * - Steam Deck: A, B, X, Y, L1, R1, Steam, etc.
 *
 * Returns: (transfer full): A newly allocated string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_input_binding_to_display_string (const LrgInputBinding *self,
                                              LrgGamepadType         gamepad_type);

G_END_DECLS
