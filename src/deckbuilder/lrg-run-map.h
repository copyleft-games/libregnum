/* lrg-run-map.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_RUN_MAP_H
#define LRG_RUN_MAP_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-map-node.h"

G_BEGIN_DECLS

#define LRG_TYPE_RUN_MAP (lrg_run_map_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRunMap, lrg_run_map, LRG, RUN_MAP, GObject)

/**
 * lrg_run_map_new:
 * @act: the act number (1-based)
 * @seed: random seed for map generation
 *
 * Creates a new run map for the specified act.
 * The map is not generated until lrg_run_map_generate() is called.
 *
 * Returns: (transfer full): a new #LrgRunMap
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRunMap *
lrg_run_map_new (gint   act,
                 guint64 seed);

/**
 * lrg_run_map_get_act:
 * @self: a #LrgRunMap
 *
 * Gets the act number this map represents.
 *
 * Returns: act number (1-based)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_map_get_act (LrgRunMap *self);

/**
 * lrg_run_map_get_seed:
 * @self: a #LrgRunMap
 *
 * Gets the seed used to generate this map.
 *
 * Returns: the random seed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_run_map_get_seed (LrgRunMap *self);

/**
 * lrg_run_map_generate:
 * @self: a #LrgRunMap
 * @num_rows: number of rows (floors) in the map
 * @min_columns: minimum nodes per row
 * @max_columns: maximum nodes per row
 *
 * Generates the map layout with nodes and connections.
 * This creates a procedurally generated map structure.
 *
 * The map follows Slay the Spire style rules:
 * - Multiple starting nodes in row 0
 * - Each node connects to 1-3 nodes in the next row
 * - Paths don't cross (connections are ordered)
 * - Boss node at the final row
 * - Special floors for elite, rest, shop, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_map_generate (LrgRunMap *self,
                      gint       num_rows,
                      gint       min_columns,
                      gint       max_columns);

/**
 * lrg_run_map_get_row_count:
 * @self: a #LrgRunMap
 *
 * Gets the number of rows (floors) in the map.
 *
 * Returns: number of rows
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_run_map_get_row_count (LrgRunMap *self);

/**
 * lrg_run_map_get_nodes_in_row:
 * @self: a #LrgRunMap
 * @row: the row number
 *
 * Gets all nodes in the specified row.
 *
 * Returns: (transfer none) (element-type LrgMapNode) (nullable): nodes in the row
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_map_get_nodes_in_row (LrgRunMap *self,
                              gint       row);

/**
 * lrg_run_map_get_all_nodes:
 * @self: a #LrgRunMap
 *
 * Gets all nodes in the map.
 *
 * Returns: (transfer none) (element-type LrgMapNode): all nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_map_get_all_nodes (LrgRunMap *self);

/**
 * lrg_run_map_get_node_count:
 * @self: a #LrgRunMap
 *
 * Gets the total number of nodes in the map.
 *
 * Returns: node count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_run_map_get_node_count (LrgRunMap *self);

/**
 * lrg_run_map_get_node_by_id:
 * @self: a #LrgRunMap
 * @id: the node ID
 *
 * Finds a node by its unique ID.
 *
 * Returns: (transfer none) (nullable): the node, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMapNode *
lrg_run_map_get_node_by_id (LrgRunMap   *self,
                            const gchar *id);

/**
 * lrg_run_map_get_starting_nodes:
 * @self: a #LrgRunMap
 *
 * Gets the starting nodes (row 0) that the player can choose from.
 *
 * Returns: (transfer none) (element-type LrgMapNode): starting nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_run_map_get_starting_nodes (LrgRunMap *self);

/**
 * lrg_run_map_get_boss_node:
 * @self: a #LrgRunMap
 *
 * Gets the boss node (final floor).
 *
 * Returns: (transfer none) (nullable): the boss node
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMapNode *
lrg_run_map_get_boss_node (LrgRunMap *self);

/**
 * lrg_run_map_is_generated:
 * @self: a #LrgRunMap
 *
 * Checks if the map has been generated.
 *
 * Returns: %TRUE if generated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_run_map_is_generated (LrgRunMap *self);

/**
 * lrg_run_map_add_node:
 * @self: a #LrgRunMap
 * @node: (transfer full): the node to add
 *
 * Manually adds a node to the map.
 * This is primarily for custom map construction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_map_add_node (LrgRunMap  *self,
                      LrgMapNode *node);

/**
 * lrg_run_map_clear:
 * @self: a #LrgRunMap
 *
 * Removes all nodes from the map.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_map_clear (LrgRunMap *self);

/**
 * lrg_run_map_calculate_positions:
 * @self: a #LrgRunMap
 * @width: total width for layout
 * @height: total height for layout
 * @padding: padding around edges
 *
 * Calculates x/y positions for all nodes for rendering.
 * Distributes nodes evenly across the specified dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_run_map_calculate_positions (LrgRunMap *self,
                                 gfloat     width,
                                 gfloat     height,
                                 gfloat     padding);

G_END_DECLS

#endif /* LRG_RUN_MAP_H */
