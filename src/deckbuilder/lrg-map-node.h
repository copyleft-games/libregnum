/* lrg-map-node.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LRG_MAP_NODE_H
#define LRG_MAP_NODE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_MAP_NODE (lrg_map_node_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMapNode, lrg_map_node, LRG, MAP_NODE, GObject)

/**
 * lrg_map_node_new:
 * @id: unique node identifier
 * @node_type: the type of encounter at this node
 * @row: the row (floor) of this node on the map
 * @column: the column position within the row
 *
 * Creates a new map node.
 *
 * Returns: (transfer full): a new #LrgMapNode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMapNode *
lrg_map_node_new (const gchar   *id,
                  LrgMapNodeType node_type,
                  gint           row,
                  gint           column);

/**
 * lrg_map_node_get_id:
 * @self: a #LrgMapNode
 *
 * Gets the unique identifier of this node.
 *
 * Returns: (transfer none): the node ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_map_node_get_id (LrgMapNode *self);

/**
 * lrg_map_node_get_node_type:
 * @self: a #LrgMapNode
 *
 * Gets the type of encounter at this node.
 *
 * Returns: the node type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMapNodeType
lrg_map_node_get_node_type (LrgMapNode *self);

/**
 * lrg_map_node_get_row:
 * @self: a #LrgMapNode
 *
 * Gets the row (floor) of this node.
 * Row 0 is the starting floor, higher rows are further in the run.
 *
 * Returns: the row number
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_map_node_get_row (LrgMapNode *self);

/**
 * lrg_map_node_get_column:
 * @self: a #LrgMapNode
 *
 * Gets the column position of this node within its row.
 *
 * Returns: the column number
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_map_node_get_column (LrgMapNode *self);

/**
 * lrg_map_node_add_connection:
 * @self: a #LrgMapNode
 * @target: the node to connect to (in the next row)
 *
 * Adds a path connection from this node to a target node.
 * Connections represent valid paths the player can take.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_map_node_add_connection (LrgMapNode *self,
                             LrgMapNode *target);

/**
 * lrg_map_node_remove_connection:
 * @self: a #LrgMapNode
 * @target: the node to disconnect from
 *
 * Removes a connection to a target node.
 *
 * Returns: %TRUE if the connection was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_map_node_remove_connection (LrgMapNode *self,
                                LrgMapNode *target);

/**
 * lrg_map_node_get_connections:
 * @self: a #LrgMapNode
 *
 * Gets all nodes this node connects to.
 *
 * Returns: (transfer none) (element-type LrgMapNode): array of connected nodes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_map_node_get_connections (LrgMapNode *self);

/**
 * lrg_map_node_get_connection_count:
 * @self: a #LrgMapNode
 *
 * Gets the number of outgoing connections from this node.
 *
 * Returns: connection count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_map_node_get_connection_count (LrgMapNode *self);

/**
 * lrg_map_node_is_connected_to:
 * @self: a #LrgMapNode
 * @target: the target node to check
 *
 * Checks if this node has a direct connection to the target.
 *
 * Returns: %TRUE if connected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_map_node_is_connected_to (LrgMapNode *self,
                              LrgMapNode *target);

/**
 * lrg_map_node_get_visited:
 * @self: a #LrgMapNode
 *
 * Checks if this node has been visited.
 *
 * Returns: %TRUE if visited
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_map_node_get_visited (LrgMapNode *self);

/**
 * lrg_map_node_set_visited:
 * @self: a #LrgMapNode
 * @visited: whether the node has been visited
 *
 * Sets the visited state of this node.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_map_node_set_visited (LrgMapNode *self,
                          gboolean    visited);

/**
 * lrg_map_node_get_encounter_id:
 * @self: a #LrgMapNode
 *
 * Gets the encounter ID for this node.
 * This is used to look up the specific combat, event, etc.
 *
 * Returns: (transfer none) (nullable): the encounter ID, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_map_node_get_encounter_id (LrgMapNode *self);

/**
 * lrg_map_node_set_encounter_id:
 * @self: a #LrgMapNode
 * @encounter_id: (nullable): the encounter ID
 *
 * Sets the encounter ID for this node.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_map_node_set_encounter_id (LrgMapNode  *self,
                               const gchar *encounter_id);

/**
 * lrg_map_node_get_x:
 * @self: a #LrgMapNode
 *
 * Gets the X position for rendering this node.
 *
 * Returns: X coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_map_node_get_x (LrgMapNode *self);

/**
 * lrg_map_node_set_x:
 * @self: a #LrgMapNode
 * @x: X coordinate
 *
 * Sets the X position for rendering.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_map_node_set_x (LrgMapNode *self,
                    gfloat      x);

/**
 * lrg_map_node_get_y:
 * @self: a #LrgMapNode
 *
 * Gets the Y position for rendering this node.
 *
 * Returns: Y coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_map_node_get_y (LrgMapNode *self);

/**
 * lrg_map_node_set_y:
 * @self: a #LrgMapNode
 * @y: Y coordinate
 *
 * Sets the Y position for rendering.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_map_node_set_y (LrgMapNode *self,
                    gfloat      y);

G_END_DECLS

#endif /* LRG_MAP_NODE_H */
