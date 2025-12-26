/* lrg-input-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Input manager singleton - aggregates multiple input sources.
 *
 * LrgInputManager collects input from multiple LrgInput sources and
 * provides a unified interface for querying input state. Sources are
 * queried by priority order and results are aggregated:
 *
 * - Button/key queries: OR (any source returning TRUE wins)
 * - Position queries: First enabled source wins
 * - Delta queries: Sum of all sources
 * - Axis queries: Maximum absolute value (preserving sign)
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

#define LRG_TYPE_INPUT_MANAGER (lrg_input_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInputManager, lrg_input_manager, LRG, INPUT_MANAGER, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_input_manager_get_default:
 *
 * Gets the default input manager instance.
 *
 * The default manager is created with keyboard, mouse, and gamepad
 * input sources pre-registered.
 *
 * Returns: (transfer none): The default #LrgInputManager instance
 */
LRG_AVAILABLE_IN_ALL
LrgInputManager * lrg_input_manager_get_default (void);

/* ==========================================================================
 * Source Management
 * ========================================================================== */

/**
 * lrg_input_manager_add_source:
 * @self: an #LrgInputManager
 * @source: (transfer none): the input source to add
 *
 * Adds an input source to the manager.
 *
 * The source is added and the internal list is re-sorted by priority.
 * The manager takes a reference to the source.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_manager_add_source (LrgInputManager *self,
                                   LrgInput        *source);

/**
 * lrg_input_manager_remove_source:
 * @self: an #LrgInputManager
 * @source: the input source to remove
 *
 * Removes an input source from the manager.
 *
 * Returns: %TRUE if the source was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_remove_source (LrgInputManager *self,
                                          LrgInput        *source);

/**
 * lrg_input_manager_get_source:
 * @self: an #LrgInputManager
 * @name: the name of the source to find
 *
 * Gets an input source by name.
 *
 * Returns: (transfer none) (nullable): The source, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgInput * lrg_input_manager_get_source (LrgInputManager *self,
                                         const gchar     *name);

/**
 * lrg_input_manager_get_sources:
 * @self: an #LrgInputManager
 *
 * Gets all registered input sources.
 *
 * Returns: (transfer none) (element-type LrgInput): The list of sources
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_input_manager_get_sources (LrgInputManager *self);

/* ==========================================================================
 * Polling
 * ========================================================================== */

/**
 * lrg_input_manager_poll:
 * @self: an #LrgInputManager
 *
 * Polls all input sources for updated state.
 *
 * This should be called once per frame before querying input.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_manager_poll (LrgInputManager *self);

/* ==========================================================================
 * Keyboard Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_key_pressed:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key was just pressed this frame.
 *
 * Queries all enabled sources and returns %TRUE if any source
 * reports the key as pressed.
 *
 * Returns: %TRUE if the key was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_key_pressed (LrgInputManager *self,
                                           GrlKey           key);

/**
 * lrg_input_manager_is_key_down:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key is currently held down.
 *
 * Queries all enabled sources and returns %TRUE if any source
 * reports the key as held.
 *
 * Returns: %TRUE if the key is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_key_down (LrgInputManager *self,
                                        GrlKey           key);

/**
 * lrg_input_manager_is_key_released:
 * @self: an #LrgInputManager
 * @key: the key to check
 *
 * Checks if a key was just released this frame.
 *
 * Queries all enabled sources and returns %TRUE if any source
 * reports the key as released.
 *
 * Returns: %TRUE if the key was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_key_released (LrgInputManager *self,
                                            GrlKey           key);

/* ==========================================================================
 * Mouse Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_mouse_button_pressed:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_mouse_button_pressed (LrgInputManager *self,
                                                    GrlMouseButton   button);

/**
 * lrg_input_manager_is_mouse_button_down:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_mouse_button_down (LrgInputManager *self,
                                                 GrlMouseButton   button);

/**
 * lrg_input_manager_is_mouse_button_released:
 * @self: an #LrgInputManager
 * @button: the mouse button to check
 *
 * Checks if a mouse button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_mouse_button_released (LrgInputManager *self,
                                                     GrlMouseButton   button);

/**
 * lrg_input_manager_get_mouse_position:
 * @self: an #LrgInputManager
 * @x: (out) (optional): location to store X coordinate
 * @y: (out) (optional): location to store Y coordinate
 *
 * Gets the current mouse position.
 *
 * Returns the position from the highest-priority enabled source
 * that provides mouse position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_manager_get_mouse_position (LrgInputManager *self,
                                           gfloat          *x,
                                           gfloat          *y);

/**
 * lrg_input_manager_get_mouse_delta:
 * @self: an #LrgInputManager
 * @dx: (out) (optional): location to store X delta
 * @dy: (out) (optional): location to store Y delta
 *
 * Gets the mouse movement since the last frame.
 *
 * Returns the sum of deltas from all enabled sources.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_manager_get_mouse_delta (LrgInputManager *self,
                                        gfloat          *dx,
                                        gfloat          *dy);

/* ==========================================================================
 * Gamepad Input
 * ========================================================================== */

/**
 * lrg_input_manager_is_gamepad_available:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 *
 * Checks if a gamepad is connected.
 *
 * Returns: %TRUE if the gamepad is available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_gamepad_available (LrgInputManager *self,
                                                 gint             gamepad);

/**
 * lrg_input_manager_is_gamepad_button_pressed:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just pressed this frame.
 *
 * Returns: %TRUE if the button was just pressed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_gamepad_button_pressed (LrgInputManager  *self,
                                                      gint              gamepad,
                                                      GrlGamepadButton  button);

/**
 * lrg_input_manager_is_gamepad_button_down:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button is currently held down.
 *
 * Returns: %TRUE if the button is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_gamepad_button_down (LrgInputManager  *self,
                                                   gint              gamepad,
                                                   GrlGamepadButton  button);

/**
 * lrg_input_manager_is_gamepad_button_released:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @button: the button to check
 *
 * Checks if a gamepad button was just released this frame.
 *
 * Returns: %TRUE if the button was just released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_is_gamepad_button_released (LrgInputManager  *self,
                                                       gint              gamepad,
                                                       GrlGamepadButton  button);

/**
 * lrg_input_manager_get_gamepad_axis:
 * @self: an #LrgInputManager
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to query
 *
 * Gets the current value of a gamepad axis.
 *
 * Returns the value with maximum absolute magnitude from all sources.
 *
 * Returns: The axis value (-1.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_manager_get_gamepad_axis (LrgInputManager *self,
                                           gint             gamepad,
                                           GrlGamepadAxis   axis);

/* ==========================================================================
 * Global Enable/Disable
 * ========================================================================== */

/**
 * lrg_input_manager_get_enabled:
 * @self: an #LrgInputManager
 *
 * Gets whether the input manager is globally enabled.
 *
 * When disabled, all input queries return FALSE/0.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_manager_get_enabled (LrgInputManager *self);

/**
 * lrg_input_manager_set_enabled:
 * @self: an #LrgInputManager
 * @enabled: whether to enable input
 *
 * Sets whether the input manager is globally enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_manager_set_enabled (LrgInputManager *self,
                                    gboolean         enabled);

G_END_DECLS
