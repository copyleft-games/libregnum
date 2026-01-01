/* lrg-run-manager.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-run-manager.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgRunManager:
 *
 * Manages the lifecycle of deckbuilder runs.
 *
 * The run manager handles:
 * - Starting new runs
 * - Ending runs (victory or defeat)
 * - Map generation
 * - Node traversal logic
 * - Run configuration
 *
 * Use lrg_run_manager_get_default() for the singleton instance.
 *
 * Since: 1.0
 */
struct _LrgRunManager
{
    GObject parent_instance;

    LrgRun *current_run;

    /* Map generation settings */
    gint    map_rows;
    gint    min_columns;
    gint    max_columns;
};

G_DEFINE_FINAL_TYPE (LrgRunManager, lrg_run_manager, G_TYPE_OBJECT)

/* Signals */
enum
{
    SIGNAL_RUN_STARTED,
    SIGNAL_RUN_ENDED,
    SIGNAL_NODE_ENTERED,
    SIGNAL_NODE_COMPLETED,
    SIGNAL_ACT_COMPLETED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static LrgRunManager *default_manager = NULL;

static void
lrg_run_manager_finalize (GObject *object)
{
    LrgRunManager *self = LRG_RUN_MANAGER (object);

    g_clear_object (&self->current_run);

    G_OBJECT_CLASS (lrg_run_manager_parent_class)->finalize (object);
}

static void
lrg_run_manager_class_init (LrgRunManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_run_manager_finalize;

    /**
     * LrgRunManager::run-started:
     * @self: the manager
     * @run: the new run
     *
     * Emitted when a new run starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RUN_STARTED] =
        g_signal_new ("run-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_RUN);

    /**
     * LrgRunManager::run-ended:
     * @self: the manager
     * @run: the ended run
     * @victory: whether the run was won
     *
     * Emitted when a run ends.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RUN_ENDED] =
        g_signal_new ("run-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_RUN, G_TYPE_BOOLEAN);

    /**
     * LrgRunManager::node-entered:
     * @self: the manager
     * @node: the node entered
     *
     * Emitted when the player enters a new node.
     *
     * Since: 1.0
     */
    signals[SIGNAL_NODE_ENTERED] =
        g_signal_new ("node-entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_MAP_NODE);

    /**
     * LrgRunManager::node-completed:
     * @self: the manager
     * @node: the completed node
     *
     * Emitted when a node encounter is completed.
     *
     * Since: 1.0
     */
    signals[SIGNAL_NODE_COMPLETED] =
        g_signal_new ("node-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_MAP_NODE);

    /**
     * LrgRunManager::act-completed:
     * @self: the manager
     * @act: the completed act number
     *
     * Emitted when an act is completed (boss defeated).
     *
     * Since: 1.0
     */
    signals[SIGNAL_ACT_COMPLETED] =
        g_signal_new ("act-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_INT);
}

static void
lrg_run_manager_init (LrgRunManager *self)
{
    /* Default Slay the Spire-style map configuration */
    self->map_rows = 15;
    self->min_columns = 2;
    self->max_columns = 4;
}

LrgRunManager *
lrg_run_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = lrg_run_manager_new ();
    }

    return default_manager;
}

LrgRunManager *
lrg_run_manager_new (void)
{
    return g_object_new (LRG_TYPE_RUN_MANAGER, NULL);
}

LrgRun *
lrg_run_manager_get_current_run (LrgRunManager *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), NULL);

    return self->current_run;
}

LrgRun *
lrg_run_manager_start_run (LrgRunManager *self,
                           const gchar   *character_id,
                           guint64        seed)
{
    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), NULL);
    g_return_val_if_fail (character_id != NULL, NULL);

    /* End any existing run */
    if (self->current_run != NULL)
    {
        lrg_run_manager_abandon_run (self);
    }

    /* Generate seed if not provided */
    if (seed == 0)
    {
        seed = g_random_int ();
        seed = (seed << 32) | g_random_int ();
    }

    /* Create new run */
    self->current_run = lrg_run_new (character_id, seed);
    lrg_run_set_state (self->current_run, LRG_RUN_STATE_MAP);

    /* Generate first map */
    lrg_run_manager_generate_map (self);

    g_signal_emit (self, signals[SIGNAL_RUN_STARTED], 0, self->current_run);

    lrg_debug (LRG_LOG_DOMAIN,
               "Started run with character %s, seed %" G_GUINT64_FORMAT,
               character_id, seed);

    return self->current_run;
}

void
lrg_run_manager_end_run (LrgRunManager *self,
                         gboolean       victory)
{
    LrgRun *run;

    g_return_if_fail (LRG_IS_RUN_MANAGER (self));

    if (self->current_run == NULL)
        return;

    run = self->current_run;

    if (victory)
    {
        lrg_run_set_state (run, LRG_RUN_STATE_VICTORY);
    }
    else
    {
        lrg_run_set_state (run, LRG_RUN_STATE_DEFEAT);
    }

    g_signal_emit (self, signals[SIGNAL_RUN_ENDED], 0, run, victory);

    lrg_debug (LRG_LOG_DOMAIN,
               "Run ended: %s",
               victory ? "VICTORY" : "DEFEAT");

    g_clear_object (&self->current_run);
}

void
lrg_run_manager_abandon_run (LrgRunManager *self)
{
    g_return_if_fail (LRG_IS_RUN_MANAGER (self));

    if (self->current_run == NULL)
        return;

    lrg_debug (LRG_LOG_DOMAIN, "Run abandoned");

    g_signal_emit (self, signals[SIGNAL_RUN_ENDED], 0,
                   self->current_run, FALSE);

    g_clear_object (&self->current_run);
}

gboolean
lrg_run_manager_has_active_run (LrgRunManager *self)
{
    LrgRunState state;

    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), FALSE);

    if (self->current_run == NULL)
        return FALSE;

    state = lrg_run_get_state (self->current_run);
    return state != LRG_RUN_STATE_VICTORY &&
           state != LRG_RUN_STATE_DEFEAT &&
           state != LRG_RUN_STATE_NOT_STARTED;
}

void
lrg_run_manager_generate_map (LrgRunManager *self)
{
    LrgRunMap *map;
    guint64 map_seed;

    g_return_if_fail (LRG_IS_RUN_MANAGER (self));
    g_return_if_fail (self->current_run != NULL);

    /* Create seed for this act's map */
    map_seed = lrg_run_get_seed (self->current_run);
    map_seed ^= lrg_run_get_current_act (self->current_run) * 12345;

    map = lrg_run_map_new (lrg_run_get_current_act (self->current_run),
                           map_seed);
    lrg_run_map_generate (map, self->map_rows,
                          self->min_columns, self->max_columns);

    lrg_run_set_map (self->current_run, map);

    lrg_debug (LRG_LOG_DOMAIN,
               "Generated map for act %d with %u nodes",
               lrg_run_map_get_act (map),
               lrg_run_map_get_node_count (map));
}

gboolean
lrg_run_manager_select_node (LrgRunManager *self,
                             LrgMapNode    *node)
{
    GPtrArray *valid_moves;
    gboolean valid = FALSE;
    guint i;

    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_MAP_NODE (node), FALSE);
    g_return_val_if_fail (self->current_run != NULL, FALSE);

    /* Check if this is a valid move */
    valid_moves = lrg_run_manager_get_valid_moves (self);
    if (valid_moves == NULL)
        return FALSE;

    for (i = 0; i < valid_moves->len; i++)
    {
        if (g_ptr_array_index (valid_moves, i) == node)
        {
            valid = TRUE;
            break;
        }
    }

    if (!valid)
    {
        lrg_debug (LRG_LOG_DOMAIN,
                   "Cannot select node %s: not a valid move",
                   lrg_map_node_get_id (node));
        return FALSE;
    }

    /* Move to the node */
    lrg_run_set_current_node (self->current_run, node);

    /* Update run state based on node type */
    switch (lrg_map_node_get_node_type (node))
    {
    case LRG_MAP_NODE_COMBAT:
    case LRG_MAP_NODE_ELITE:
    case LRG_MAP_NODE_BOSS:
        lrg_run_set_state (self->current_run, LRG_RUN_STATE_COMBAT);
        break;
    case LRG_MAP_NODE_EVENT:
    case LRG_MAP_NODE_MYSTERY:
        lrg_run_set_state (self->current_run, LRG_RUN_STATE_EVENT);
        break;
    case LRG_MAP_NODE_SHOP:
        lrg_run_set_state (self->current_run, LRG_RUN_STATE_SHOP);
        break;
    case LRG_MAP_NODE_REST:
        lrg_run_set_state (self->current_run, LRG_RUN_STATE_REST);
        break;
    case LRG_MAP_NODE_TREASURE:
        lrg_run_set_state (self->current_run, LRG_RUN_STATE_TREASURE);
        break;
    }

    g_signal_emit (self, signals[SIGNAL_NODE_ENTERED], 0, node);

    lrg_debug (LRG_LOG_DOMAIN,
               "Entered node %s (type %d)",
               lrg_map_node_get_id (node),
               lrg_map_node_get_node_type (node));

    return TRUE;
}

void
lrg_run_manager_complete_node (LrgRunManager *self)
{
    LrgMapNode *node;
    LrgMapNodeType node_type;
    gint act;

    g_return_if_fail (LRG_IS_RUN_MANAGER (self));
    g_return_if_fail (self->current_run != NULL);

    node = lrg_run_get_current_node (self->current_run);
    if (node == NULL)
        return;

    node_type = lrg_map_node_get_node_type (node);

    g_signal_emit (self, signals[SIGNAL_NODE_COMPLETED], 0, node);

    /* Handle boss completion (act transition) */
    if (node_type == LRG_MAP_NODE_BOSS)
    {
        act = lrg_run_get_current_act (self->current_run);
        g_signal_emit (self, signals[SIGNAL_ACT_COMPLETED], 0, act);

        /* Advance to next act or end game */
        if (act >= 3)  /* Typically 3 acts */
        {
            lrg_run_manager_end_run (self, TRUE);
            return;
        }

        lrg_run_advance_act (self->current_run);
        lrg_run_manager_generate_map (self);
    }

    /* Return to map state */
    lrg_run_set_state (self->current_run, LRG_RUN_STATE_MAP);

    lrg_debug (LRG_LOG_DOMAIN,
               "Completed node %s",
               lrg_map_node_get_id (node));
}

GPtrArray *
lrg_run_manager_get_valid_moves (LrgRunManager *self)
{
    LrgMapNode *current;
    LrgRunMap *map;

    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), NULL);

    if (self->current_run == NULL)
        return NULL;

    map = lrg_run_get_map (self->current_run);
    if (map == NULL)
        return NULL;

    current = lrg_run_get_current_node (self->current_run);

    /* If at start of act, can choose any starting node */
    if (current == NULL)
    {
        return lrg_run_map_get_starting_nodes (map);
    }

    /* Otherwise, can move to any connected node */
    return lrg_map_node_get_connections (current);
}

void
lrg_run_manager_set_map_rows (LrgRunManager *self,
                              gint           rows)
{
    g_return_if_fail (LRG_IS_RUN_MANAGER (self));
    g_return_if_fail (rows > 0);

    self->map_rows = rows;
}

gint
lrg_run_manager_get_map_rows (LrgRunManager *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MANAGER (self), 0);

    return self->map_rows;
}

void
lrg_run_manager_set_map_width (LrgRunManager *self,
                               gint           min_columns,
                               gint           max_columns)
{
    g_return_if_fail (LRG_IS_RUN_MANAGER (self));
    g_return_if_fail (min_columns > 0);
    g_return_if_fail (max_columns >= min_columns);

    self->min_columns = min_columns;
    self->max_columns = max_columns;
}
