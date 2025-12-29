/* lrg-game-state-manager.h - Game state stack management
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_GAME_STATE_MANAGER_H
#define LRG_GAME_STATE_MANAGER_H

#include <glib-object.h>
#include "lrg-game-state.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_STATE_MANAGER (lrg_game_state_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGameStateManager, lrg_game_state_manager, LRG, GAME_STATE_MANAGER, GObject)

/**
 * lrg_game_state_manager_new:
 *
 * Creates a new #LrgGameStateManager.
 *
 * Returns: (transfer full): A new #LrgGameStateManager
 */
LRG_AVAILABLE_IN_ALL
LrgGameStateManager *
lrg_game_state_manager_new (void);

/**
 * lrg_game_state_manager_push:
 * @self: an #LrgGameStateManager
 * @state: (transfer full): the #LrgGameState to push
 *
 * Pushes a new state onto the stack. The current state (if any)
 * will have its pause() method called. The new state becomes
 * active and has its enter() method called.
 *
 * The manager takes ownership of @state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_push (LrgGameStateManager *self,
                             LrgGameState        *state);

/**
 * lrg_game_state_manager_pop:
 * @self: an #LrgGameStateManager
 *
 * Pops the current state from the stack. The state has its
 * exit() method called and is then unreferenced. The state
 * below (if any) becomes active and has its resume() method called.
 *
 * Does nothing if the stack is empty.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_pop (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_replace:
 * @self: an #LrgGameStateManager
 * @state: (transfer full): the #LrgGameState to replace with
 *
 * Replaces the current state with a new one. The current state
 * has its exit() method called and is unreferenced. The new state
 * becomes active and has its enter() method called.
 *
 * If the stack is empty, this is equivalent to push().
 * The manager takes ownership of @state.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_replace (LrgGameStateManager *self,
                                LrgGameState        *state);

/**
 * lrg_game_state_manager_clear:
 * @self: an #LrgGameStateManager
 *
 * Removes all states from the stack, calling exit() on each.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_clear (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_get_current:
 * @self: an #LrgGameStateManager
 *
 * Gets the current (top) state on the stack.
 *
 * Returns: (transfer none) (nullable): The current #LrgGameState, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgGameState *
lrg_game_state_manager_get_current (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_get_state_count:
 * @self: an #LrgGameStateManager
 *
 * Gets the number of states on the stack.
 *
 * Returns: The number of states
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_game_state_manager_get_state_count (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_is_empty:
 * @self: an #LrgGameStateManager
 *
 * Checks if the state stack is empty.
 *
 * Returns: %TRUE if no states are on the stack
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_manager_is_empty (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_update:
 * @self: an #LrgGameStateManager
 * @delta: time since last frame in seconds
 *
 * Updates all states that should be updated. States are updated
 * from bottom to top, but only if no blocking state is above them.
 * The top state is always updated.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_update (LrgGameStateManager *self,
                               gdouble              delta);

/**
 * lrg_game_state_manager_draw:
 * @self: an #LrgGameStateManager
 *
 * Draws all states that should be rendered. States are drawn
 * from bottom to top, but only if all states above them are
 * transparent. The top state is always drawn.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_state_manager_draw (LrgGameStateManager *self);

/**
 * lrg_game_state_manager_handle_input:
 * @self: an #LrgGameStateManager
 * @event: (transfer none): the input event
 *
 * Passes an input event to states from top to bottom until
 * one handles it (returns %TRUE).
 *
 * Returns: %TRUE if the event was handled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_state_manager_handle_input (LrgGameStateManager *self,
                                     gpointer             event);

G_END_DECLS

#endif /* LRG_GAME_STATE_MANAGER_H */
