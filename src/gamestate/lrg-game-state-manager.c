/* lrg-game-state-manager.c - Game state stack management
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-game-state-manager.h"

/**
 * SECTION:lrg-game-state-manager
 * @title: LrgGameStateManager
 * @short_description: Game state stack management
 *
 * #LrgGameStateManager maintains a stack of #LrgGameState objects,
 * allowing for layered game states like pause menus on top of gameplay.
 *
 * The manager handles state transitions by calling the appropriate
 * lifecycle methods (enter, exit, pause, resume) on states as they
 * are pushed, popped, or replaced.
 *
 * The update() and draw() methods respect the #LrgGameState:blocking
 * and #LrgGameState:transparent properties to determine which states
 * should receive updates and be rendered.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgGameStateManager *manager = lrg_game_state_manager_new ();
 *
 * // Push the main menu
 * lrg_game_state_manager_push (manager, my_main_menu_new ());
 *
 * // In game loop
 * lrg_game_state_manager_update (manager, delta_time);
 * lrg_game_state_manager_draw (manager);
 *
 * // Later, push a pause menu (transparent overlay)
 * lrg_game_state_manager_push (manager, my_pause_menu_new ());
 * ]|
 */

struct _LrgGameStateManager
{
    GObject    parent_instance;

    GPtrArray *states;  /* Stack of LrgGameState, index 0 = bottom */
};

G_DEFINE_TYPE (LrgGameStateManager, lrg_game_state_manager, G_TYPE_OBJECT)

enum
{
    SIGNAL_STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_game_state_manager_finalize (GObject *object)
{
    LrgGameStateManager *self = LRG_GAME_STATE_MANAGER (object);

    /* Clear all states, calling exit() on each */
    lrg_game_state_manager_clear (self);

    g_ptr_array_unref (self->states);

    G_OBJECT_CLASS (lrg_game_state_manager_parent_class)->finalize (object);
}

static void
lrg_game_state_manager_class_init (LrgGameStateManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_game_state_manager_finalize;

    /**
     * LrgGameStateManager::state-changed:
     * @self: the #LrgGameStateManager
     * @current: (nullable): the new current state, or %NULL if empty
     *
     * Emitted when the current (top) state changes due to push, pop,
     * replace, or clear operations.
     */
    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_GAME_STATE);
}

static void
lrg_game_state_manager_init (LrgGameStateManager *self)
{
    self->states = g_ptr_array_new_with_free_func (g_object_unref);
}

/**
 * lrg_game_state_manager_new:
 *
 * Creates a new #LrgGameStateManager with an empty state stack.
 *
 * Returns: (transfer full): A new #LrgGameStateManager
 */
LrgGameStateManager *
lrg_game_state_manager_new (void)
{
    return g_object_new (LRG_TYPE_GAME_STATE_MANAGER, NULL);
}

/**
 * lrg_game_state_manager_push:
 * @self: an #LrgGameStateManager
 * @state: (transfer full): the #LrgGameState to push
 *
 * Pushes a new state onto the stack.
 */
void
lrg_game_state_manager_push (LrgGameStateManager *self,
                             LrgGameState        *state)
{
    LrgGameState *current;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));
    g_return_if_fail (LRG_IS_GAME_STATE (state));

    /* Pause the current state if there is one */
    current = lrg_game_state_manager_get_current (self);
    if (current != NULL)
        lrg_game_state_pause (current);

    /* Add new state to stack (takes ownership) */
    g_ptr_array_add (self->states, state);

    /* Enter the new state */
    lrg_game_state_enter (state);

    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, state);
}

/**
 * lrg_game_state_manager_pop:
 * @self: an #LrgGameStateManager
 *
 * Pops the current state from the stack.
 */
void
lrg_game_state_manager_pop (LrgGameStateManager *self)
{
    LrgGameState *current;
    LrgGameState *next;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));

    if (self->states->len == 0)
        return;

    /* Get current state and call exit */
    current = g_ptr_array_index (self->states, self->states->len - 1);
    lrg_game_state_exit (current);

    /* Remove from stack (will unref) */
    g_ptr_array_remove_index (self->states, self->states->len - 1);

    /* Resume the state below if there is one */
    next = lrg_game_state_manager_get_current (self);
    if (next != NULL)
        lrg_game_state_resume (next);

    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, next);
}

/**
 * lrg_game_state_manager_replace:
 * @self: an #LrgGameStateManager
 * @state: (transfer full): the #LrgGameState to replace with
 *
 * Replaces the current state with a new one.
 */
void
lrg_game_state_manager_replace (LrgGameStateManager *self,
                                LrgGameState        *state)
{
    LrgGameState *current;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));
    g_return_if_fail (LRG_IS_GAME_STATE (state));

    if (self->states->len > 0)
    {
        /* Exit and remove current state */
        current = g_ptr_array_index (self->states, self->states->len - 1);
        lrg_game_state_exit (current);
        g_ptr_array_remove_index (self->states, self->states->len - 1);
    }

    /* Add new state (takes ownership) */
    g_ptr_array_add (self->states, state);

    /* Enter the new state */
    lrg_game_state_enter (state);

    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, state);
}

/**
 * lrg_game_state_manager_clear:
 * @self: an #LrgGameStateManager
 *
 * Removes all states from the stack.
 */
void
lrg_game_state_manager_clear (LrgGameStateManager *self)
{
    gint i;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));

    /* Exit all states from top to bottom */
    for (i = (gint)self->states->len - 1; i >= 0; i--)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i);
        lrg_game_state_exit (state);
    }

    /* Remove all states (will unref each) */
    g_ptr_array_set_size (self->states, 0);

    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, NULL);
}

/**
 * lrg_game_state_manager_get_current:
 * @self: an #LrgGameStateManager
 *
 * Gets the current (top) state.
 *
 * Returns: (transfer none) (nullable): The current #LrgGameState
 */
LrgGameState *
lrg_game_state_manager_get_current (LrgGameStateManager *self)
{
    g_return_val_if_fail (LRG_IS_GAME_STATE_MANAGER (self), NULL);

    if (self->states->len == 0)
        return NULL;

    return g_ptr_array_index (self->states, self->states->len - 1);
}

/**
 * lrg_game_state_manager_get_state_count:
 * @self: an #LrgGameStateManager
 *
 * Gets the number of states on the stack.
 *
 * Returns: The number of states
 */
guint
lrg_game_state_manager_get_state_count (LrgGameStateManager *self)
{
    g_return_val_if_fail (LRG_IS_GAME_STATE_MANAGER (self), 0);

    return self->states->len;
}

/**
 * lrg_game_state_manager_is_empty:
 * @self: an #LrgGameStateManager
 *
 * Checks if the state stack is empty.
 *
 * Returns: %TRUE if empty
 */
gboolean
lrg_game_state_manager_is_empty (LrgGameStateManager *self)
{
    g_return_val_if_fail (LRG_IS_GAME_STATE_MANAGER (self), TRUE);

    return self->states->len == 0;
}

/**
 * lrg_game_state_manager_update:
 * @self: an #LrgGameStateManager
 * @delta: time since last frame in seconds
 *
 * Updates states respecting the blocking property.
 *
 * States are updated from bottom to top. A blocking state
 * prevents all states below it from being updated.
 */
void
lrg_game_state_manager_update (LrgGameStateManager *self,
                               gdouble              delta)
{
    guint i;
    guint start_index;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));

    if (self->states->len == 0)
        return;

    /*
     * Find the lowest blocking state - only states from there up get updated.
     * Start from the top and work down to find the first blocking state.
     */
    start_index = 0;
    for (i = self->states->len; i > 0; i--)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i - 1);
        if (lrg_game_state_is_blocking (state))
        {
            start_index = i - 1;
            break;
        }
    }

    /* Update from start_index to top */
    for (i = start_index; i < self->states->len; i++)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i);
        lrg_game_state_update (state, delta);
    }
}

/**
 * lrg_game_state_manager_draw:
 * @self: an #LrgGameStateManager
 *
 * Draws states respecting the transparent property.
 *
 * States are drawn from bottom to top. Only states that are
 * visible (all states above them are transparent) are drawn.
 */
void
lrg_game_state_manager_draw (LrgGameStateManager *self)
{
    guint i;
    guint start_index;

    g_return_if_fail (LRG_IS_GAME_STATE_MANAGER (self));

    if (self->states->len == 0)
        return;

    /*
     * Find the lowest visible state - only states from there up get drawn.
     * Start from the top and work down while states are transparent.
     */
    start_index = self->states->len - 1;
    for (i = self->states->len; i > 0; i--)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i - 1);
        start_index = i - 1;
        if (!lrg_game_state_is_transparent (state))
            break;
    }

    /* Draw from start_index to top */
    for (i = start_index; i < self->states->len; i++)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i);
        lrg_game_state_draw (state);
    }
}

/**
 * lrg_game_state_manager_handle_input:
 * @self: an #LrgGameStateManager
 * @event: (transfer none): the input event
 *
 * Passes input to states from top to bottom until handled.
 *
 * Returns: %TRUE if the event was handled
 */
gboolean
lrg_game_state_manager_handle_input (LrgGameStateManager *self,
                                     gpointer             event)
{
    gint i;

    g_return_val_if_fail (LRG_IS_GAME_STATE_MANAGER (self), FALSE);

    /* Try states from top to bottom */
    for (i = (gint)self->states->len - 1; i >= 0; i--)
    {
        LrgGameState *state = g_ptr_array_index (self->states, i);
        if (lrg_game_state_handle_input (state, event))
            return TRUE;
    }

    return FALSE;
}
