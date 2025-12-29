/* lrg-transition-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manager for scene transitions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRANSITION_MANAGER (lrg_transition_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTransitionManager, lrg_transition_manager, LRG, TRANSITION_MANAGER, GObject)

/**
 * lrg_transition_manager_new:
 *
 * Creates a new transition manager.
 *
 * Returns: (transfer full): A new #LrgTransitionManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionManager *  lrg_transition_manager_new                  (void);

/* Lifecycle */

/**
 * lrg_transition_manager_initialize:
 * @self: A #LrgTransitionManager
 * @width: Viewport width
 * @height: Viewport height
 * @error: (nullable): Return location for error
 *
 * Initializes the transition manager with viewport dimensions.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_transition_manager_initialize           (LrgTransitionManager  *self,
                                                                     guint                  width,
                                                                     guint                  height,
                                                                     GError               **error);

/**
 * lrg_transition_manager_shutdown:
 * @self: A #LrgTransitionManager
 *
 * Shuts down the transition manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_shutdown             (LrgTransitionManager *self);

/**
 * lrg_transition_manager_update:
 * @self: A #LrgTransitionManager
 * @delta_time: Time since last update in seconds
 *
 * Updates the active transition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_update               (LrgTransitionManager *self,
                                                                     gfloat                delta_time);

/**
 * lrg_transition_manager_render:
 * @self: A #LrgTransitionManager
 * @old_scene_texture: Texture ID of old scene
 * @new_scene_texture: Texture ID of new scene
 *
 * Renders the current transition effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_render               (LrgTransitionManager *self,
                                                                     guint                 old_scene_texture,
                                                                     guint                 new_scene_texture);

/* Transition control */

/**
 * lrg_transition_manager_start:
 * @self: A #LrgTransitionManager
 * @transition: (transfer none): The transition to start
 *
 * Starts a transition. The manager takes a reference to the transition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_start                (LrgTransitionManager *self,
                                                                     LrgTransition        *transition);

/**
 * lrg_transition_manager_cancel:
 * @self: A #LrgTransitionManager
 *
 * Cancels the current transition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_cancel               (LrgTransitionManager *self);

/* State queries */

/**
 * lrg_transition_manager_is_active:
 * @self: A #LrgTransitionManager
 *
 * Checks if a transition is currently active.
 *
 * Returns: %TRUE if a transition is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_transition_manager_is_active            (LrgTransitionManager *self);

/**
 * lrg_transition_manager_is_at_midpoint:
 * @self: A #LrgTransitionManager
 *
 * Checks if the current transition is at its midpoint.
 * This is when scene switching should occur.
 *
 * Returns: %TRUE if at midpoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_transition_manager_is_at_midpoint       (LrgTransitionManager *self);

/**
 * lrg_transition_manager_get_current:
 * @self: A #LrgTransitionManager
 *
 * Gets the currently active transition.
 *
 * Returns: (transfer none) (nullable): The current transition, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransition *         lrg_transition_manager_get_current          (LrgTransitionManager *self);

/**
 * lrg_transition_manager_get_state:
 * @self: A #LrgTransitionManager
 *
 * Gets the state of the current transition.
 *
 * Returns: The current #LrgTransitionState, or %LRG_TRANSITION_STATE_IDLE if none
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionState      lrg_transition_manager_get_state            (LrgTransitionManager *self);

/* Viewport */

/**
 * lrg_transition_manager_set_viewport:
 * @self: A #LrgTransitionManager
 * @width: New viewport width
 * @height: New viewport height
 *
 * Updates the viewport dimensions (e.g., on window resize).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_transition_manager_set_viewport         (LrgTransitionManager *self,
                                                                     guint                 width,
                                                                     guint                 height);

/**
 * lrg_transition_manager_get_viewport_width:
 * @self: A #LrgTransitionManager
 *
 * Gets the current viewport width.
 *
 * Returns: The viewport width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_transition_manager_get_viewport_width   (LrgTransitionManager *self);

/**
 * lrg_transition_manager_get_viewport_height:
 * @self: A #LrgTransitionManager
 *
 * Gets the current viewport height.
 *
 * Returns: The viewport height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_transition_manager_get_viewport_height  (LrgTransitionManager *self);

G_END_DECLS
