/* lrg-run-manager.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_RUN_MANAGER_H
#define LRG_RUN_MANAGER_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-run.h"

G_BEGIN_DECLS

#define LRG_TYPE_RUN_MANAGER (lrg_run_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRunManager, lrg_run_manager, LRG, RUN_MANAGER, GObject)

/**
 * lrg_run_manager_get_default:
 *
 * Gets the default run manager singleton.
 *
 * Returns: (transfer none): the default run manager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRunManager *
lrg_run_manager_get_default (void);

/**
 * lrg_run_manager_new:
 *
 * Creates a new run manager instance.
 * Use lrg_run_manager_get_default() for the singleton.
 *
 * Returns: (transfer full): a new #LrgRunManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRunManager *
lrg_run_manager_new (void);

/**
 * lrg_run_manager_get_current_run:
 * @self: a #LrgRunManager
 *
 * Gets the current active run.
 *
 * Returns: (transfer none) (nullable): the current run
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRun *
lrg_run_manager_get_current_run (LrgRunManager *self);

/**
 * lrg_run_manager_start_run:
 * @self: a #LrgRunManager
 * @character_id: the character class to use
 * @seed: random seed (0 for random)
 *
 * Starts a new run.
 *
 * Returns: (transfer none): the new run
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRun *
lrg_run_manager_start_run (LrgRunManager *self,
                           const gchar   *character_id,
                           guint64        seed);

/**
 * lrg_run_manager_end_run:
 * @self: a #LrgRunManager
 * @victory: whether the run was won
 *
 * Ends the current run.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_end_run (LrgRunManager *self,
                         gboolean       victory);

/**
 * lrg_run_manager_abandon_run:
 * @self: a #LrgRunManager
 *
 * Abandons the current run without completing it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_abandon_run (LrgRunManager *self);

/**
 * lrg_run_manager_has_active_run:
 * @self: a #LrgRunManager
 *
 * Checks if there's an active run in progress.
 *
 * Returns: %TRUE if a run is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_manager_has_active_run (LrgRunManager *self);

/**
 * lrg_run_manager_generate_map:
 * @self: a #LrgRunManager
 *
 * Generates a new map for the current act.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_generate_map (LrgRunManager *self);

/**
 * lrg_run_manager_select_node:
 * @self: a #LrgRunManager
 * @node: the node to select
 *
 * Selects a node to travel to.
 *
 * Returns: %TRUE if the node was valid and selected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_manager_select_node (LrgRunManager *self,
                             LrgMapNode    *node);

/**
 * lrg_run_manager_complete_node:
 * @self: a #LrgRunManager
 *
 * Marks the current node as completed and returns to map.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_complete_node (LrgRunManager *self);

/**
 * lrg_run_manager_get_valid_moves:
 * @self: a #LrgRunManager
 *
 * Gets the nodes the player can currently move to.
 *
 * Returns: (transfer none) (element-type LrgMapNode) (nullable): valid nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_manager_get_valid_moves (LrgRunManager *self);

/* Configuration */

/**
 * lrg_run_manager_set_map_rows:
 * @self: a #LrgRunManager
 * @rows: number of rows per act
 *
 * Sets how many rows (floors) each act's map has.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_set_map_rows (LrgRunManager *self,
                              gint           rows);

/**
 * lrg_run_manager_get_map_rows:
 * @self: a #LrgRunManager
 *
 * Gets the number of rows per act.
 *
 * Returns: number of rows
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_manager_get_map_rows (LrgRunManager *self);

/**
 * lrg_run_manager_set_map_width:
 * @self: a #LrgRunManager
 * @min_columns: minimum nodes per row
 * @max_columns: maximum nodes per row
 *
 * Sets the width range for generated maps.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_manager_set_map_width (LrgRunManager *self,
                               gint           min_columns,
                               gint           max_columns);

G_END_DECLS

#endif /* LRG_RUN_MANAGER_H */
