/* lrg-node-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNodeGraph: a node editor on an infinite, pannable and zoomable
 * canvas, for shader graphs, dialogue trees, behaviour trees and
 * visual scripting.
 *
 * The model is three small GObjects defined here:
 *
 *  - #LrgGraphNode: a card with an id, a title, a position in graph
 *    units, a category/accent colour, input and output ports and an
 *    optional body widget (sliders, previews ...);
 *  - #LrgGraphPort: an input or output on a node with a name, a type
 *    (a #GQuark used for compatibility and colour) and a multi-link
 *    flag;
 *  - #LrgGraphLink: a wire from an output port to an input port.
 *
 * The graph owns its nodes and links; nodes own their ports.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations and errors
 * ========================================================================== */

/**
 * LrgGraphPortDirection:
 * @LRG_GRAPH_PORT_INPUT: the port receives a link (drawn on the left edge)
 * @LRG_GRAPH_PORT_OUTPUT: the port sends links (drawn on the right edge)
 *
 * Which side of a node a port sits on and which end of a link it takes.
 */
typedef enum
{
	LRG_GRAPH_PORT_INPUT,
	LRG_GRAPH_PORT_OUTPUT
} LrgGraphPortDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_graph_port_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GRAPH_PORT_DIRECTION (lrg_graph_port_direction_get_type ())

/**
 * LrgNodeGraphGrid:
 * @LRG_NODE_GRAPH_GRID_NONE: no grid
 * @LRG_NODE_GRAPH_GRID_DOTS: a dot at every grid intersection
 * @LRG_NODE_GRAPH_GRID_LINES: minor and major grid lines
 *
 * How the canvas background grid is drawn. Either way it fades out as
 * the cells get too small on screen.
 */
typedef enum
{
	LRG_NODE_GRAPH_GRID_NONE,
	LRG_NODE_GRAPH_GRID_DOTS,
	LRG_NODE_GRAPH_GRID_LINES
} LrgNodeGraphGrid;

LRG_AVAILABLE_IN_ALL
GType lrg_node_graph_grid_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NODE_GRAPH_GRID (lrg_node_graph_grid_get_type ())

/**
 * LrgNodeGraphError:
 * @LRG_NODE_GRAPH_ERROR_INVALID_PORTS: the ports cannot form a link
 *   (same direction, same node, or not in this graph)
 * @LRG_NODE_GRAPH_ERROR_INCOMPATIBLE: #LrgNodeGraph::can-connect refused
 * @LRG_NODE_GRAPH_ERROR_EXISTS: the link already exists
 * @LRG_NODE_GRAPH_ERROR_PARSE: a YAML document could not be loaded
 *
 * Errors from lrg_node_graph_connect() and lrg_node_graph_load_yaml().
 */
typedef enum
{
	LRG_NODE_GRAPH_ERROR_INVALID_PORTS,
	LRG_NODE_GRAPH_ERROR_INCOMPATIBLE,
	LRG_NODE_GRAPH_ERROR_EXISTS,
	LRG_NODE_GRAPH_ERROR_PARSE
} LrgNodeGraphError;

/**
 * LRG_NODE_GRAPH_ERROR:
 *
 * Error domain of #LrgNodeGraphError.
 */
#define LRG_NODE_GRAPH_ERROR (lrg_node_graph_error_quark ())

/**
 * lrg_node_graph_error_quark:
 *
 * Returns: the #LrgNodeGraphError domain
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_node_graph_error_quark (void);

/* ==========================================================================
 * Types
 * ========================================================================== */

#define LRG_TYPE_GRAPH_PORT (lrg_graph_port_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGraphPort, lrg_graph_port, LRG, GRAPH_PORT, GObject)

#define LRG_TYPE_GRAPH_NODE (lrg_graph_node_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGraphNode, lrg_graph_node, LRG, GRAPH_NODE, GObject)

#define LRG_TYPE_GRAPH_LINK (lrg_graph_link_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGraphLink, lrg_graph_link, LRG, GRAPH_LINK, GObject)

#define LRG_TYPE_NODE_GRAPH (lrg_node_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodeGraph, lrg_node_graph, LRG, NODE_GRAPH, LrgContainer)

/* ==========================================================================
 * LrgGraphPort
 * ========================================================================== */

/**
 * lrg_graph_port_get_name:
 * @self: an #LrgGraphPort
 *
 * Returns: (transfer none): the port name, also its label and its key
 *   within the node (unique per direction)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_graph_port_get_name (LrgGraphPort *self);

/**
 * lrg_graph_port_get_direction:
 * @self: an #LrgGraphPort
 *
 * Returns: whether the port is an input or an output
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPortDirection lrg_graph_port_get_direction (LrgGraphPort *self);

/**
 * lrg_graph_port_get_port_type:
 * @self: an #LrgGraphPort
 *
 * Returns: the port's type quark, 0 for "any" (connects to every type)
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_graph_port_get_port_type (LrgGraphPort *self);

/**
 * lrg_graph_port_get_type_name:
 * @self: an #LrgGraphPort
 *
 * Returns: (transfer none) (nullable): the type name, %NULL for "any"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_graph_port_get_type_name (LrgGraphPort *self);

/**
 * lrg_graph_port_get_node:
 * @self: an #LrgGraphPort
 *
 * Returns: (transfer none) (nullable): the node the port belongs to,
 *   %NULL once removed from it
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_graph_port_get_node (LrgGraphPort *self);

/**
 * lrg_graph_port_get_multi_link:
 * @self: an #LrgGraphPort
 *
 * Returns: %TRUE if the port accepts more than one link (outputs do by
 *   default, inputs do not)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_graph_port_get_multi_link (LrgGraphPort *self);

/**
 * lrg_graph_port_set_multi_link:
 * @self: an #LrgGraphPort
 * @multi_link: whether more than one link may attach
 *
 * When %FALSE, connecting a new link to the port replaces the one it
 * has (emitting #LrgNodeGraph::link-removed for the old one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_port_set_multi_link (LrgGraphPort *self,
                                    gboolean      multi_link);

/**
 * lrg_graph_port_get_color:
 * @self: an #LrgGraphPort
 * @color: (out caller-allocates): the colour the port and its links use
 *
 * Gets the explicit colour, or the colour derived from the port type
 * (a fixed palette keyed by the type name, the theme accent for "any").
 *
 * Returns: %TRUE if an explicit colour is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_graph_port_get_color (LrgGraphPort *self,
                                   GrlColor     *color);

/**
 * lrg_graph_port_set_color:
 * @self: an #LrgGraphPort
 * @color: (nullable): explicit colour, %NULL for the type colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_port_set_color (LrgGraphPort   *self,
                               const GrlColor *color);

/**
 * lrg_graph_port_get_n_links:
 * @self: an #LrgGraphPort
 *
 * Returns: number of links attached to the port
 */
LRG_AVAILABLE_IN_ALL
guint lrg_graph_port_get_n_links (LrgGraphPort *self);

/* ==========================================================================
 * LrgGraphNode
 * ========================================================================== */

/**
 * lrg_graph_node_new:
 * @id: (nullable): unique id within a graph, %NULL to have the graph
 *   assign "node-N" when the node is added
 * @title: (nullable): header text
 *
 * Creates a free-standing node. Add it with lrg_node_graph_append_node()
 * or use lrg_node_graph_add_node() to do both at once.
 *
 * Returns: (transfer full): a new #LrgGraphNode
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_graph_node_new (const gchar *id,
                                   const gchar *title);

/**
 * lrg_graph_node_get_id:
 * @self: an #LrgGraphNode
 *
 * Returns: (transfer none) (nullable): the node id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_graph_node_get_id (LrgGraphNode *self);

/**
 * lrg_graph_node_get_title:
 * @self: an #LrgGraphNode
 *
 * Returns: (transfer none) (nullable): the header text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_graph_node_get_title (LrgGraphNode *self);

/**
 * lrg_graph_node_set_title:
 * @self: an #LrgGraphNode
 * @title: (nullable): header text
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_title (LrgGraphNode *self,
                               const gchar  *title);

/**
 * lrg_graph_node_get_category:
 * @self: an #LrgGraphNode
 *
 * Returns: (transfer none) (nullable): the category name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_graph_node_get_category (LrgGraphNode *self);

/**
 * lrg_graph_node_set_category:
 * @self: an #LrgGraphNode
 * @category: (nullable): category name ("math", "texture" ...); without
 *   an explicit accent the header colour is picked from it
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_category (LrgGraphNode *self,
                                  const gchar  *category);

/**
 * lrg_graph_node_get_accent:
 * @self: an #LrgGraphNode
 * @color: (out caller-allocates) (optional): the explicit accent
 *
 * Returns: %TRUE if an explicit accent colour is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_graph_node_get_accent (LrgGraphNode *self,
                                    GrlColor     *color);

/**
 * lrg_graph_node_set_accent:
 * @self: an #LrgGraphNode
 * @color: (nullable): header colour, %NULL to derive it from the
 *   category (or the theme accent without one)
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_accent (LrgGraphNode   *self,
                                const GrlColor *color);

/**
 * lrg_graph_node_get_x:
 * @self: an #LrgGraphNode
 *
 * Returns: left edge in graph units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_graph_node_get_x (LrgGraphNode *self);

/**
 * lrg_graph_node_get_y:
 * @self: an #LrgGraphNode
 *
 * Returns: top edge in graph units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_graph_node_get_y (LrgGraphNode *self);

/**
 * lrg_graph_node_set_position:
 * @self: an #LrgGraphNode
 * @x: left edge in graph units
 * @y: top edge in graph units
 *
 * Moves the node. Inside a graph this emits #LrgNodeGraph::node-moved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_position (LrgGraphNode *self,
                                  gfloat        x,
                                  gfloat        y);

/**
 * lrg_graph_node_get_min_width:
 * @self: an #LrgGraphNode
 *
 * Returns: the smallest card width in graph units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_graph_node_get_min_width (LrgGraphNode *self);

/**
 * lrg_graph_node_set_min_width:
 * @self: an #LrgGraphNode
 * @width: smallest card width in graph units (the card still grows to
 *   fit its title, port labels and body)
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_min_width (LrgGraphNode *self,
                                   gfloat        width);

/**
 * lrg_graph_node_get_body:
 * @self: an #LrgGraphNode
 *
 * Returns: (transfer none) (nullable): the body widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_graph_node_get_body (LrgGraphNode *self);

/**
 * lrg_graph_node_set_body:
 * @self: an #LrgGraphNode
 * @body: (nullable): widget shown under the ports
 *
 * Embeds a widget in the card, below the port rows. It becomes a child
 * of the graph while the node is in one and is laid out at the card's
 * on-screen size (its text keeps its size; below a zoom of 0.45 it is
 * hidden and gets no input).
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_set_body (LrgGraphNode *self,
                              LrgWidget    *body);

/**
 * lrg_graph_node_get_selected:
 * @self: an #LrgGraphNode
 *
 * Returns: %TRUE if the node is selected in its graph
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_graph_node_get_selected (LrgGraphNode *self);

/**
 * lrg_graph_node_get_graph:
 * @self: an #LrgGraphNode
 *
 * Returns: (transfer none) (nullable): the graph the node is in
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_graph_node_get_graph (LrgGraphNode *self);

/**
 * lrg_graph_node_add_port:
 * @self: an #LrgGraphNode
 * @direction: input or output
 * @name: port name, unique among the node's ports of that direction
 * @type_name: (nullable): type name for compatibility and colour,
 *   %NULL for "any"
 *
 * Appends a port row. Outputs accept several links, inputs one (see
 * lrg_graph_port_set_multi_link()).
 *
 * Returns: (transfer none) (nullable): the new port, or %NULL if the
 *   name is taken
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_graph_node_add_port (LrgGraphNode          *self,
                                        LrgGraphPortDirection  direction,
                                        const gchar           *name,
                                        const gchar           *type_name);

/**
 * lrg_graph_node_remove_port:
 * @self: an #LrgGraphNode
 * @port: one of its ports
 *
 * Removes the port and, inside a graph, every link attached to it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_graph_node_remove_port (LrgGraphNode *self,
                                 LrgGraphPort *port);

/**
 * lrg_graph_node_get_n_ports:
 * @self: an #LrgGraphNode
 * @direction: which side
 *
 * Returns: number of inputs or outputs
 */
LRG_AVAILABLE_IN_ALL
guint lrg_graph_node_get_n_ports (LrgGraphNode          *self,
                                  LrgGraphPortDirection  direction);

/**
 * lrg_graph_node_get_port:
 * @self: an #LrgGraphNode
 * @direction: which side
 * @index: row index
 *
 * Returns: (transfer none) (nullable): the port in that row
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_graph_node_get_port (LrgGraphNode          *self,
                                        LrgGraphPortDirection  direction,
                                        guint                  index);

/**
 * lrg_graph_node_find_port:
 * @self: an #LrgGraphNode
 * @direction: which side
 * @name: port name
 *
 * Returns: (transfer none) (nullable): the port, %NULL if none
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_graph_node_find_port (LrgGraphNode          *self,
                                         LrgGraphPortDirection  direction,
                                         const gchar           *name);

/* ==========================================================================
 * LrgGraphLink
 * ========================================================================== */

/**
 * lrg_graph_link_get_output:
 * @self: an #LrgGraphLink
 *
 * Returns: (transfer none): the output port the link starts at
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_graph_link_get_output (LrgGraphLink *self);

/**
 * lrg_graph_link_get_input:
 * @self: an #LrgGraphLink
 *
 * Returns: (transfer none): the input port the link ends at
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_graph_link_get_input (LrgGraphLink *self);

/* ==========================================================================
 * LrgNodeGraph: model
 * ========================================================================== */

/**
 * lrg_node_graph_new:
 *
 * Returns: (transfer full): a new, empty #LrgNodeGraph
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_node_graph_new (void);

/**
 * lrg_node_graph_add_node:
 * @self: an #LrgNodeGraph
 * @id: (nullable): unique id, %NULL for an automatic "node-N"
 * @title: (nullable): header text
 * @x: left edge in graph units
 * @y: top edge in graph units
 *
 * Creates a node and appends it (on top of the others).
 *
 * Returns: (transfer none) (nullable): the node, %NULL if @id is taken
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_node_graph_add_node (LrgNodeGraph *self,
                                        const gchar  *id,
                                        const gchar  *title,
                                        gfloat        x,
                                        gfloat        y);

/**
 * lrg_node_graph_append_node:
 * @self: an #LrgNodeGraph
 * @node: a node that is in no graph
 *
 * Adds a node built with lrg_graph_node_new() and emits
 * #LrgNodeGraph::node-added. The graph takes a reference.
 *
 * Returns: %TRUE on success, %FALSE if its id is taken
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_append_node (LrgNodeGraph *self,
                                     LrgGraphNode *node);

/**
 * lrg_node_graph_remove_node:
 * @self: an #LrgNodeGraph
 * @node: a node of @self
 *
 * Removes the node's links (#LrgNodeGraph::link-removed each), then the
 * node (#LrgNodeGraph::node-removed).
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_remove_node (LrgNodeGraph *self,
                                 LrgGraphNode *node);

/**
 * lrg_node_graph_clear:
 * @self: an #LrgNodeGraph
 *
 * Removes every link and node, emitting the removal signals.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_clear (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_n_nodes:
 * @self: an #LrgNodeGraph
 *
 * Returns: number of nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_graph_get_n_nodes (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_nth_node:
 * @self: an #LrgNodeGraph
 * @index: index in stacking order (0 is the bottom)
 *
 * Returns: (transfer none) (nullable): the node
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_node_graph_get_nth_node (LrgNodeGraph *self,
                                            guint         index);

/**
 * lrg_node_graph_get_node:
 * @self: an #LrgNodeGraph
 * @id: node id
 *
 * Returns: (transfer none) (nullable): the node with that id
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_node_graph_get_node (LrgNodeGraph *self,
                                        const gchar  *id);

/**
 * lrg_node_graph_get_nodes:
 * @self: an #LrgNodeGraph
 *
 * Returns: (transfer container) (element-type LrgGraphNode): the nodes
 *   in stacking order
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_node_graph_get_nodes (LrgNodeGraph *self);

/**
 * lrg_node_graph_can_connect:
 * @self: an #LrgNodeGraph
 * @output: an output port
 * @input: an input port
 *
 * Checks the structural rules (an output and an input of two different
 * nodes of this graph) and then asks #LrgNodeGraph::can-connect.
 *
 * Returns: %TRUE if lrg_node_graph_connect() would accept the pair
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_can_connect (LrgNodeGraph *self,
                                     LrgGraphPort *output,
                                     LrgGraphPort *input);

/**
 * lrg_node_graph_connect:
 * @self: an #LrgNodeGraph
 * @output: an output port
 * @input: an input port
 * @error: return location for a #LrgNodeGraphError
 *
 * Links @output to @input. A port that does not accept several links
 * loses its existing one first (#LrgNodeGraph::link-removed), then
 * #LrgNodeGraph::link-added is emitted.
 *
 * Returns: (transfer none) (nullable): the new link, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_connect (LrgNodeGraph  *self,
                                       LrgGraphPort  *output,
                                       LrgGraphPort  *input,
                                       GError       **error);

/**
 * lrg_node_graph_connect_by_name:
 * @self: an #LrgNodeGraph
 * @output_node: id of the node with the output
 * @output_port: output port name
 * @input_node: id of the node with the input
 * @input_port: input port name
 * @error: return location for a #LrgNodeGraphError
 *
 * lrg_node_graph_connect() with ids and names.
 *
 * Returns: (transfer none) (nullable): the new link, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_connect_by_name (LrgNodeGraph  *self,
                                               const gchar   *output_node,
                                               const gchar   *output_port,
                                               const gchar   *input_node,
                                               const gchar   *input_port,
                                               GError       **error);

/**
 * lrg_node_graph_disconnect:
 * @self: an #LrgNodeGraph
 * @link: a link of @self
 *
 * Removes the link and emits #LrgNodeGraph::link-removed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_disconnect (LrgNodeGraph *self,
                                LrgGraphLink *link);

/**
 * lrg_node_graph_disconnect_port:
 * @self: an #LrgNodeGraph
 * @port: a port of one of the nodes
 *
 * Removes every link attached to @port.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_disconnect_port (LrgNodeGraph *self,
                                     LrgGraphPort *port);

/**
 * lrg_node_graph_get_n_links:
 * @self: an #LrgNodeGraph
 *
 * Returns: number of links
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_graph_get_n_links (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_nth_link:
 * @self: an #LrgNodeGraph
 * @index: link index (creation order)
 *
 * Returns: (transfer none) (nullable): the link
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_get_nth_link (LrgNodeGraph *self,
                                            guint         index);

/**
 * lrg_node_graph_get_links:
 * @self: an #LrgNodeGraph
 *
 * Returns: (transfer container) (element-type LrgGraphLink): every link
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_node_graph_get_links (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_port_links:
 * @self: an #LrgNodeGraph
 * @port: a port
 *
 * Returns: (transfer container) (element-type LrgGraphLink): the links
 *   attached to @port
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_node_graph_get_port_links (LrgNodeGraph *self,
                                       LrgGraphPort *port);

/**
 * lrg_node_graph_find_link:
 * @self: an #LrgNodeGraph
 * @output: an output port
 * @input: an input port
 *
 * Returns: (transfer none) (nullable): the link between them, if any
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_find_link (LrgNodeGraph *self,
                                         LrgGraphPort *output,
                                         LrgGraphPort *input);

/* ==========================================================================
 * LrgNodeGraph: selection
 * ========================================================================== */

/**
 * lrg_node_graph_select_node:
 * @self: an #LrgNodeGraph
 * @node: a node of @self
 * @extend: %TRUE to add to the selection, %FALSE to select only @node
 *
 * Selects a node (it also becomes the keyboard cursor).
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_select_node (LrgNodeGraph *self,
                                 LrgGraphNode *node,
                                 gboolean      extend);

/**
 * lrg_node_graph_unselect_node:
 * @self: an #LrgNodeGraph
 * @node: a node of @self
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_unselect_node (LrgNodeGraph *self,
                                   LrgGraphNode *node);

/**
 * lrg_node_graph_select_all:
 * @self: an #LrgNodeGraph
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_select_all (LrgNodeGraph *self);

/**
 * lrg_node_graph_unselect_all:
 * @self: an #LrgNodeGraph
 *
 * Clears the node and link selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_unselect_all (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_selected_nodes:
 * @self: an #LrgNodeGraph
 *
 * Returns: (transfer container) (element-type LrgGraphNode): the
 *   selected nodes in stacking order
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_node_graph_get_selected_nodes (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_n_selected:
 * @self: an #LrgNodeGraph
 *
 * Returns: number of selected nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_graph_get_n_selected (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_selected_link:
 * @self: an #LrgNodeGraph
 *
 * Returns: (transfer none) (nullable): the selected link (clicking a
 *   wire selects it), %NULL if none
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_get_selected_link (LrgNodeGraph *self);

/**
 * lrg_node_graph_select_link:
 * @self: an #LrgNodeGraph
 * @link: (nullable): a link of @self, %NULL to clear
 *
 * Selects one link and clears the node selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_select_link (LrgNodeGraph *self,
                                 LrgGraphLink *link);

/**
 * lrg_node_graph_get_cursor_node:
 * @self: an #LrgNodeGraph
 *
 * Returns: (transfer none) (nullable): the node arrow keys move from
 *   and Enter activates
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_node_graph_get_cursor_node (LrgNodeGraph *self);

/**
 * lrg_node_graph_delete_selection:
 * @self: an #LrgNodeGraph
 *
 * Removes the selected link and the selected nodes (what Delete does).
 *
 * Returns: number of nodes and links removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_graph_delete_selection (LrgNodeGraph *self);

/**
 * lrg_node_graph_duplicate_selection:
 * @self: an #LrgNodeGraph
 *
 * Copies the selected nodes (title, category, accent, ports) and the
 * links between them, offset by two grid cells, and selects the copies
 * (what Ctrl+D does). Bodies are not copied: handle
 * #LrgNodeGraph::node-duplicated to give a copy its own.
 *
 * Returns: number of nodes created
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_graph_duplicate_selection (LrgNodeGraph *self);

/* ==========================================================================
 * LrgNodeGraph: view
 * ========================================================================== */

/**
 * lrg_node_graph_get_zoom:
 * @self: an #LrgNodeGraph
 *
 * Returns: the zoom the view is at or gliding to
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_graph_get_zoom (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_zoom:
 * @self: an #LrgNodeGraph
 * @zoom: screen units per graph unit, clamped to the zoom range
 *
 * Zooms about the centre of the widget, without animation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_zoom (LrgNodeGraph *self,
                              gfloat        zoom);

/**
 * lrg_node_graph_get_displayed_zoom:
 * @self: an #LrgNodeGraph
 *
 * Returns: the zoom currently painted (differs from the target while
 *   the view glides)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_graph_get_displayed_zoom (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_center:
 * @self: an #LrgNodeGraph
 * @x: (out) (optional): graph x shown at the widget centre (target)
 * @y: (out) (optional): graph y shown at the widget centre (target)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_get_center (LrgNodeGraph *self,
                                gfloat       *x,
                                gfloat       *y);

/**
 * lrg_node_graph_set_view:
 * @self: an #LrgNodeGraph
 * @center_x: graph x to show at the widget centre
 * @center_y: graph y to show at the widget centre
 * @zoom: zoom, clamped to the zoom range
 * @animate: glide there with springs
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_view (LrgNodeGraph *self,
                              gfloat        center_x,
                              gfloat        center_y,
                              gfloat        zoom,
                              gboolean      animate);

/**
 * lrg_node_graph_zoom_at:
 * @self: an #LrgNodeGraph
 * @x: canvas x of the fixed point
 * @y: canvas y of the fixed point
 * @zoom: new zoom, clamped to the zoom range
 * @animate: glide (the point stays put during the glide)
 *
 * Zooms keeping the graph point under (@x, @y) where it is, like the
 * mouse wheel does about the pointer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_zoom_at (LrgNodeGraph *self,
                             gfloat        x,
                             gfloat        y,
                             gfloat        zoom,
                             gboolean      animate);

/**
 * lrg_node_graph_pan_by:
 * @self: an #LrgNodeGraph
 * @dx: canvas units to move the content right
 * @dy: canvas units to move the content down
 *
 * Scrolls the view like a drag of the canvas.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_pan_by (LrgNodeGraph *self,
                            gfloat        dx,
                            gfloat        dy);

/**
 * lrg_node_graph_frame_all:
 * @self: an #LrgNodeGraph
 * @animate: glide there
 *
 * Fits every node in the view (zooming in at most to 1). Home key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_frame_all (LrgNodeGraph *self,
                               gboolean      animate);

/**
 * lrg_node_graph_frame_selection:
 * @self: an #LrgNodeGraph
 * @animate: glide there
 *
 * Fits the selected nodes in the view, or every node when nothing is
 * selected. F key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_frame_selection (LrgNodeGraph *self,
                                     gboolean      animate);

/**
 * lrg_node_graph_graph_to_screen:
 * @self: an #LrgNodeGraph
 * @graph_x: x in graph units
 * @graph_y: y in graph units
 * @screen_x: (out) (optional): canvas x (as in UI events)
 * @screen_y: (out) (optional): canvas y
 *
 * Maps a graph point through the displayed view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_graph_to_screen (LrgNodeGraph *self,
                                     gfloat        graph_x,
                                     gfloat        graph_y,
                                     gfloat       *screen_x,
                                     gfloat       *screen_y);

/**
 * lrg_node_graph_screen_to_graph:
 * @self: an #LrgNodeGraph
 * @screen_x: canvas x
 * @screen_y: canvas y
 * @graph_x: (out) (optional): x in graph units
 * @graph_y: (out) (optional): y in graph units
 *
 * Inverse of lrg_node_graph_graph_to_screen().
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_screen_to_graph (LrgNodeGraph *self,
                                     gfloat        screen_x,
                                     gfloat        screen_y,
                                     gfloat       *graph_x,
                                     gfloat       *graph_y);

/* ==========================================================================
 * LrgNodeGraph: geometry and hit testing
 * ========================================================================== */

/**
 * lrg_node_graph_get_node_bounds:
 * @self: an #LrgNodeGraph
 * @node: a node of @self
 * @x: (out) (optional): left edge in graph units
 * @y: (out) (optional): top edge
 * @width: (out) (optional): card width
 * @height: (out) (optional): card height
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_get_node_bounds (LrgNodeGraph *self,
                                     LrgGraphNode *node,
                                     gfloat       *x,
                                     gfloat       *y,
                                     gfloat       *width,
                                     gfloat       *height);

/**
 * lrg_node_graph_get_port_position:
 * @self: an #LrgNodeGraph
 * @port: a port of one of the nodes
 * @x: (out) (optional): port centre x in graph units
 * @y: (out) (optional): port centre y in graph units
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_get_port_position (LrgNodeGraph *self,
                                       LrgGraphPort *port,
                                       gfloat       *x,
                                       gfloat       *y);

/**
 * lrg_node_graph_node_at:
 * @self: an #LrgNodeGraph
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: (transfer none) (nullable): the topmost node under the point
 */
LRG_AVAILABLE_IN_ALL
LrgGraphNode * lrg_node_graph_node_at (LrgNodeGraph *self,
                                       gfloat        x,
                                       gfloat        y);

/**
 * lrg_node_graph_port_at:
 * @self: an #LrgNodeGraph
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: (transfer none) (nullable): the port whose dot is within
 *   reach of the point (a few canvas units around it)
 */
LRG_AVAILABLE_IN_ALL
LrgGraphPort * lrg_node_graph_port_at (LrgNodeGraph *self,
                                       gfloat        x,
                                       gfloat        y);

/**
 * lrg_node_graph_link_at:
 * @self: an #LrgNodeGraph
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: (transfer none) (nullable): the topmost link whose wire
 *   passes within reach of the point
 */
LRG_AVAILABLE_IN_ALL
LrgGraphLink * lrg_node_graph_link_at (LrgNodeGraph *self,
                                       gfloat        x,
                                       gfloat        y);

/**
 * lrg_node_graph_get_paint_stats:
 * @self: an #LrgNodeGraph
 * @n_nodes: (out) (optional): nodes painted by the last paint
 * @n_links: (out) (optional): links painted by the last paint
 *
 * Diagnostics for view culling: only nodes and links that intersect
 * the view are painted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_get_paint_stats (LrgNodeGraph *self,
                                     guint        *n_nodes,
                                     guint        *n_links);

/* ==========================================================================
 * LrgNodeGraph: settings
 * ========================================================================== */

/**
 * lrg_node_graph_get_min_zoom:
 * @self: an #LrgNodeGraph
 *
 * Returns: the smallest zoom
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_graph_get_min_zoom (LrgNodeGraph *self);

/**
 * lrg_node_graph_get_max_zoom:
 * @self: an #LrgNodeGraph
 *
 * Returns: the largest zoom
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_graph_get_max_zoom (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_zoom_range:
 * @self: an #LrgNodeGraph
 * @min_zoom: smallest zoom (> 0)
 * @max_zoom: largest zoom (>= @min_zoom)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_zoom_range (LrgNodeGraph *self,
                                    gfloat        min_zoom,
                                    gfloat        max_zoom);

/**
 * lrg_node_graph_get_grid_size:
 * @self: an #LrgNodeGraph
 *
 * Returns: grid cell size in graph units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_graph_get_grid_size (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_grid_size:
 * @self: an #LrgNodeGraph
 * @size: grid cell size in graph units (also the snap step)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_grid_size (LrgNodeGraph *self,
                                   gfloat        size);

/**
 * lrg_node_graph_get_grid_style:
 * @self: an #LrgNodeGraph
 *
 * Returns: how the grid is drawn
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraphGrid lrg_node_graph_get_grid_style (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_grid_style:
 * @self: an #LrgNodeGraph
 * @style: dots, lines or none
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_grid_style (LrgNodeGraph     *self,
                                    LrgNodeGraphGrid  style);

/**
 * lrg_node_graph_get_snap_to_grid:
 * @self: an #LrgNodeGraph
 *
 * Returns: %TRUE if dragged nodes snap to the grid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_get_snap_to_grid (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_snap_to_grid:
 * @self: an #LrgNodeGraph
 * @snap: snap dragged nodes to the grid
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_snap_to_grid (LrgNodeGraph *self,
                                      gboolean      snap);

/**
 * lrg_node_graph_get_show_minimap:
 * @self: an #LrgNodeGraph
 *
 * Returns: %TRUE if the minimap is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_get_show_minimap (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_show_minimap:
 * @self: an #LrgNodeGraph
 * @show: show an overview in the bottom-right corner (click or drag in
 *   it to move the view)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_show_minimap (LrgNodeGraph *self,
                                      gboolean      show);

/**
 * lrg_node_graph_get_animate_links:
 * @self: an #LrgNodeGraph
 *
 * Returns: %TRUE if pulses flow along the links
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_get_animate_links (LrgNodeGraph *self);

/**
 * lrg_node_graph_set_animate_links:
 * @self: an #LrgNodeGraph
 * @animate: draw pulses travelling from outputs to inputs (they move
 *   with the paint list time, so they cost no relayout)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_graph_set_animate_links (LrgNodeGraph *self,
                                       gboolean      animate);

/* ==========================================================================
 * LrgNodeGraph: serialization
 * ========================================================================== */

/**
 * lrg_node_graph_to_yaml:
 * @self: an #LrgNodeGraph
 * @length: (out) (optional): length of the result
 * @error: return location for a #GError
 *
 * Serializes the view, the nodes (id, title, position, category,
 * accent, ports) and the links. Body widgets are not saved.
 *
 * Returns: (transfer full) (nullable): the YAML document
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_node_graph_to_yaml (LrgNodeGraph  *self,
                                gsize         *length,
                                GError       **error);

/**
 * lrg_node_graph_load_yaml:
 * @self: an #LrgNodeGraph
 * @data: a document written by lrg_node_graph_to_yaml()
 * @length: length of @data, or -1 if NUL-terminated
 * @error: return location for a #LrgNodeGraphError
 *
 * Replaces the graph's content with the document. The document's
 * structure (ids, port names, link ends) is validated before anything
 * changes, so a malformed document leaves the graph untouched. Links
 * are then restored with lrg_node_graph_connect(): a link the
 * application's #LrgNodeGraph::can-connect refuses stops the load with
 * that error, leaving the nodes and the links restored before it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_graph_load_yaml (LrgNodeGraph  *self,
                                   const gchar   *data,
                                   gssize         length,
                                   GError       **error);

G_END_DECLS
