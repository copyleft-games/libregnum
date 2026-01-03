/* lrg-game-state.h - Abstract base class for game states
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_GAME_STATE_H
#define LRG_GAME_STATE_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_STATE (lrg_game_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGameState, lrg_game_state, LRG, GAME_STATE, GObject)

/**
 * LrgGameStateClass:
 * @parent_class: the parent class
 * @enter: Called when state becomes active
 * @exit: Called when state is deactivated
 * @pause: Called when state is paused (another state pushed on top)
 * @resume: Called when state is resumed (state above popped)
 * @update: Called each frame with delta time
 * @draw: Called each frame for rendering
 * @handle_input: Called to process input events
 *
 * The virtual function table for #LrgGameState.
 *
 * Subclasses must implement enter(), exit(), update(), and draw().
 * pause(), resume(), and handle_input() have default implementations.
 */
struct _LrgGameStateClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgGameStateClass::enter:
     * @self: an #LrgGameState
     *
     * Called when this state becomes the active state.
     * Initialize resources and start any state-specific logic.
     */
    void (*enter)        (LrgGameState *self);

    /**
     * LrgGameStateClass::exit:
     * @self: an #LrgGameState
     *
     * Called when this state is being removed from the stack.
     * Clean up resources allocated in enter().
     */
    void (*exit)         (LrgGameState *self);

    /**
     * LrgGameStateClass::pause:
     * @self: an #LrgGameState
     *
     * Called when another state is pushed on top of this one.
     * The state remains on the stack but is not active.
     * Default implementation does nothing.
     */
    void (*pause)        (LrgGameState *self);

    /**
     * LrgGameStateClass::resume:
     * @self: an #LrgGameState
     *
     * Called when the state above this one is popped.
     * The state becomes active again.
     * Default implementation does nothing.
     */
    void (*resume)       (LrgGameState *self);

    /**
     * LrgGameStateClass::update:
     * @self: an #LrgGameState
     * @delta: time since last frame in seconds
     *
     * Called each frame to update game logic.
     * Only called when state is active (top of stack).
     */
    void (*update)       (LrgGameState *self,
                          gdouble       delta);

    /**
     * LrgGameStateClass::draw:
     * @self: an #LrgGameState
     *
     * Called each frame to render the state.
     * Only called when state is active (top of stack).
     */
    void (*draw)         (LrgGameState *self);

    /**
     * LrgGameStateClass::handle_input:
     * @self: an #LrgGameState
     * @event: (transfer none): the input event to handle
     *
     * Called to process input events.
     * Default implementation does nothing.
     *
     * Returns: %TRUE if event was handled, %FALSE to propagate
     */
    gboolean (*handle_input) (LrgGameState *self,
                              gpointer      event);

    /**
     * LrgGameStateClass::update_safe:
     * @self: an #LrgGameState
     * @delta: time since last frame in seconds
     * @error: (nullable): return location for error
     *
     * Called each frame to update game logic with error reporting.
     * If not implemented, falls back to calling update().
     * Default implementation calls update() and returns %TRUE.
     *
     * Returns: %TRUE on success, %FALSE on error
     *
     * Since: 1.0
     */
    gboolean (*update_safe) (LrgGameState  *self,
                             gdouble        delta,
                             GError       **error);

    /*< private >*/
    gpointer _reserved[7];
};

/**
 * lrg_game_state_enter:
 * @self: an #LrgGameState
 *
 * Called when this state becomes the active state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_enter (LrgGameState *self);

/**
 * lrg_game_state_exit:
 * @self: an #LrgGameState
 *
 * Called when this state is being removed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_exit (LrgGameState *self);

/**
 * lrg_game_state_pause:
 * @self: an #LrgGameState
 *
 * Called when another state is pushed on top.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_pause (LrgGameState *self);

/**
 * lrg_game_state_resume:
 * @self: an #LrgGameState
 *
 * Called when the state above is popped.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_resume (LrgGameState *self);

/**
 * lrg_game_state_update:
 * @self: an #LrgGameState
 * @delta: time since last frame in seconds
 *
 * Updates game logic for this state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_update (LrgGameState *self,
                       gdouble       delta);

/**
 * lrg_game_state_draw:
 * @self: an #LrgGameState
 *
 * Renders this state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_draw (LrgGameState *self);

/**
 * lrg_game_state_handle_input:
 * @self: an #LrgGameState
 * @event: (transfer none): the input event
 *
 * Processes an input event.
 *
 * Returns: %TRUE if event was handled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_handle_input (LrgGameState *self,
                             gpointer      event);

/**
 * lrg_game_state_update_safe:
 * @self: an #LrgGameState
 * @delta: time since last frame in seconds
 * @error: (nullable): return location for error
 *
 * Updates game logic with error handling. If the state does not
 * implement this method, falls back to regular update().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_update_safe (LrgGameState  *self,
                            gdouble        delta,
                            GError       **error);

/**
 * lrg_game_state_get_name:
 * @self: an #LrgGameState
 *
 * Gets the display name of this state (for debugging).
 *
 * Returns: (transfer none): The state name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_game_state_get_name (LrgGameState *self);

/**
 * lrg_game_state_set_name:
 * @self: an #LrgGameState
 * @name: the display name
 *
 * Sets the display name of this state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_set_name (LrgGameState *self,
                         const gchar  *name);

/**
 * lrg_game_state_is_transparent:
 * @self: an #LrgGameState
 *
 * Gets whether this state allows states below to render.
 * Useful for pause menus that show the game behind.
 *
 * Returns: %TRUE if transparent
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_is_transparent (LrgGameState *self);

/**
 * lrg_game_state_set_transparent:
 * @self: an #LrgGameState
 * @transparent: whether state is transparent
 *
 * Sets whether this state allows states below to render.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_set_transparent (LrgGameState *self,
                                gboolean      transparent);

/**
 * lrg_game_state_is_blocking:
 * @self: an #LrgGameState
 *
 * Gets whether this state blocks updates to states below.
 *
 * Returns: %TRUE if blocking
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_is_blocking (LrgGameState *self);

/**
 * lrg_game_state_set_blocking:
 * @self: an #LrgGameState
 * @blocking: whether state blocks updates
 *
 * Sets whether this state blocks updates to states below.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_set_blocking (LrgGameState *self,
                             gboolean      blocking);

G_END_DECLS

#endif /* LRG_GAME_STATE_H */
