/* lrg-vscript-program.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVscriptProgram: a visual script (`.rvscript`).
 *
 * A program is a set of graphs: the main graph and any number of named
 * subgraphs (macros). A graph holds nodes (#LrgVscriptNode), links
 * (#LrgVscriptLink) from an output pin of one node to an input pin of
 * another, and comment frames (#LrgVscriptComment). Pins are either
 * "exec" pins (the order things happen in: an event node's `then` output
 * leads to the next statement) or typed data pins (values pulled from
 * pure nodes, literals or the cached outputs of statements).
 *
 * Node kinds come from a fixed catalogue (lrg_vscript_node_types_list()):
 * events (start, update, fixed update, custom events, GObject signals,
 * input actions), flow (branch, sequence, loops, for-each, delay and
 * frame waits as coroutines), variables (graph variables and blackboard
 * keys), values, math, logic, comparison, strings, GObject-Introspection
 * calls (any introspected function, method or constructor), property
 * get/set, signal connect/emit and subgraph calls. The pins of a node are
 * computed from its kind and its parameters
 * (lrg_vscript_program_dup_node_pins()).
 *
 * The same program is edited by the node graph editor and by the block
 * editor (`view:` says which one opens it) and run by #LrgScriptingGraph.
 * Loading is strict (errors name the key path, e.g. `nodes[3].type`),
 * keys the format does not know are kept, and saving is byte-stable: a
 * load-save round trip of a saved file writes the same bytes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_VSCRIPT_FORMAT_VERSION:
 *
 * The `version:` written by lrg_vscript_program_to_data().
 */
#define LRG_VSCRIPT_FORMAT_VERSION (1)

/**
 * LRG_VSCRIPT_DEFAULT_STEP_BUDGET:
 *
 * The default number of node executions one event dispatch may take
 * before the interpreter stops it (an endless loop guard).
 */
#define LRG_VSCRIPT_DEFAULT_STEP_BUDGET (100000)

/* ==========================================================================
 * Enumerations and errors
 * ========================================================================== */

/**
 * LrgVscriptError:
 * @LRG_VSCRIPT_ERROR_PARSE: the text is not YAML or has the wrong shape
 * @LRG_VSCRIPT_ERROR_INVALID: a value or reference is invalid
 * @LRG_VSCRIPT_ERROR_IO: the file cannot be read or written
 * @LRG_VSCRIPT_ERROR_NOT_FOUND: no such node, pin, graph or variable
 * @LRG_VSCRIPT_ERROR_RUNTIME: a node failed while running
 * @LRG_VSCRIPT_ERROR_BUDGET: an event dispatch ran out of steps
 * @LRG_VSCRIPT_ERROR_UNSUPPORTED: this build cannot do it (no
 *   GObject-Introspection runtime, an argument type GI calls do not map)
 *
 * Errors of the visual scripting format and runtime.
 */
typedef enum
{
	LRG_VSCRIPT_ERROR_PARSE,
	LRG_VSCRIPT_ERROR_INVALID,
	LRG_VSCRIPT_ERROR_IO,
	LRG_VSCRIPT_ERROR_NOT_FOUND,
	LRG_VSCRIPT_ERROR_RUNTIME,
	LRG_VSCRIPT_ERROR_BUDGET,
	LRG_VSCRIPT_ERROR_UNSUPPORTED
} LrgVscriptError;

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_ERROR (lrg_vscript_error_get_type ())

/**
 * LRG_VSCRIPT_ERROR:
 *
 * The error domain of #LrgVscriptError.
 */
#define LRG_VSCRIPT_ERROR (lrg_vscript_error_quark ())

/**
 * lrg_vscript_error_quark:
 *
 * Returns: the #LrgVscriptError domain
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_vscript_error_quark (void);

/**
 * LrgVscriptPinDirection:
 * @LRG_VSCRIPT_PIN_INPUT: the pin receives a link
 * @LRG_VSCRIPT_PIN_OUTPUT: links start at the pin
 *
 * The side of a node a pin is on.
 */
typedef enum
{
	LRG_VSCRIPT_PIN_INPUT,
	LRG_VSCRIPT_PIN_OUTPUT
} LrgVscriptPinDirection;

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_pin_direction_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_PIN_DIRECTION (lrg_vscript_pin_direction_get_type ())

/**
 * LrgVscriptView:
 * @LRG_VSCRIPT_VIEW_GRAPH: the node graph editor opens the program
 * @LRG_VSCRIPT_VIEW_BLOCKS: the block editor opens the program
 *
 * Which editor a program opens in (the `view:` key). Both edit the same
 * nodes.
 */
typedef enum
{
	LRG_VSCRIPT_VIEW_GRAPH,
	LRG_VSCRIPT_VIEW_BLOCKS
} LrgVscriptView;

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_view_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_VIEW (lrg_vscript_view_get_type ())

/**
 * LrgVscriptSeverity:
 * @LRG_VSCRIPT_SEVERITY_INFO: a note
 * @LRG_VSCRIPT_SEVERITY_WARNING: probably a mistake; the program runs
 * @LRG_VSCRIPT_SEVERITY_ERROR: the program cannot run as written
 *
 * The severity of an #LrgVscriptIssue.
 */
typedef enum
{
	LRG_VSCRIPT_SEVERITY_INFO,
	LRG_VSCRIPT_SEVERITY_WARNING,
	LRG_VSCRIPT_SEVERITY_ERROR
} LrgVscriptSeverity;

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_severity_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_SEVERITY (lrg_vscript_severity_get_type ())

/* ==========================================================================
 * Boxed records: pins, links, comments, issues
 * ========================================================================== */

/**
 * LrgVscriptPin:
 * @name: the pin name, unique per direction on its node ("then", "a")
 * @label: (nullable): a display label, %NULL to show @name
 * @direction: input or output
 * @exec: %TRUE for an exec (control flow) pin
 * @type: the value type of a data pin: "any", "bool", "int", "float",
 *   "string", "object" or "list" (an object pin may name a GType, e.g.
 *   "LrgEntity3D"); "exec" for exec pins
 *
 * One pin of a node (or one entry of a subgraph's interface).
 */
typedef struct _LrgVscriptPin LrgVscriptPin;

struct _LrgVscriptPin
{
	gchar                  *name;
	gchar                  *label;
	LrgVscriptPinDirection  direction;
	gboolean                exec;
	gchar                  *type;

	/*< private >*/
	gpointer                extra;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_pin_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_PIN (lrg_vscript_pin_get_type ())

/**
 * lrg_vscript_pin_new:
 * @name: the pin name
 * @direction: input or output
 * @exec: whether it is an exec pin
 * @type: (nullable): the value type ("any" when %NULL; ignored for exec)
 *
 * Returns: (transfer full): a new pin
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptPin * lrg_vscript_pin_new (const gchar            *name,
                                     LrgVscriptPinDirection  direction,
                                     gboolean                exec,
                                     const gchar            *type);

/**
 * lrg_vscript_pin_copy:
 * @self: a pin
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptPin * lrg_vscript_pin_copy (const LrgVscriptPin *self);

/**
 * lrg_vscript_pin_free:
 * @self: (nullable): a pin
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_pin_free (LrgVscriptPin *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVscriptPin, lrg_vscript_pin_free)

/**
 * LrgVscriptLink:
 * @from_node: the id of the node with the output pin
 * @from_pin: the output pin
 * @to_node: the id of the node with the input pin
 * @to_pin: the input pin
 *
 * A wire from an output pin to an input pin of the same graph.
 */
typedef struct _LrgVscriptLink LrgVscriptLink;

struct _LrgVscriptLink
{
	gchar *from_node;
	gchar *from_pin;
	gchar *to_node;
	gchar *to_pin;

	/*< private >*/
	gpointer extra;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_link_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_LINK (lrg_vscript_link_get_type ())

/**
 * lrg_vscript_link_new:
 * @from_node: the source node id
 * @from_pin: the source (output) pin
 * @to_node: the target node id
 * @to_pin: the target (input) pin
 *
 * Returns: (transfer full): a new link
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptLink * lrg_vscript_link_new (const gchar *from_node,
                                       const gchar *from_pin,
                                       const gchar *to_node,
                                       const gchar *to_pin);

/**
 * lrg_vscript_link_copy:
 * @self: a link
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptLink * lrg_vscript_link_copy (const LrgVscriptLink *self);

/**
 * lrg_vscript_link_free:
 * @self: (nullable): a link
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_link_free (LrgVscriptLink *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVscriptLink, lrg_vscript_link_free)

/**
 * LrgVscriptComment:
 * @id: the comment id, unique in its graph
 * @text: the text shown in the frame's title bar
 * @x: left edge in graph units
 * @y: top edge in graph units
 * @width: width in graph units
 * @height: height in graph units
 * @color: (nullable): a colour ("#rrggbb") or %NULL for the theme's
 *
 * A comment frame. Editors treat the nodes inside a frame as a group
 * (dragging the frame moves them).
 */
typedef struct _LrgVscriptComment LrgVscriptComment;

struct _LrgVscriptComment
{
	gchar   *id;
	gchar   *text;
	gdouble  x;
	gdouble  y;
	gdouble  width;
	gdouble  height;
	gchar   *color;

	/*< private >*/
	gpointer extra;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_comment_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_COMMENT (lrg_vscript_comment_get_type ())

/**
 * lrg_vscript_comment_new:
 * @id: (nullable): the id (a free one is picked when added if %NULL)
 * @text: the text
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Returns: (transfer full): a new comment frame
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptComment * lrg_vscript_comment_new (const gchar *id,
                                             const gchar *text,
                                             gdouble      x,
                                             gdouble      y,
                                             gdouble      width,
                                             gdouble      height);

/**
 * lrg_vscript_comment_copy:
 * @self: a comment
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptComment * lrg_vscript_comment_copy (const LrgVscriptComment *self);

/**
 * lrg_vscript_comment_free:
 * @self: (nullable): a comment
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_comment_free (LrgVscriptComment *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVscriptComment, lrg_vscript_comment_free)

/**
 * LrgVscriptIssue:
 * @severity: how bad it is
 * @graph: the graph name ("" for the main graph)
 * @node: (nullable): the node id, %NULL for program-level issues
 * @path: the key path in the file (`nodes[2].params.name`), or ""
 * @message: what is wrong
 *
 * One problem found by lrg_vscript_program_validate().
 */
typedef struct _LrgVscriptIssue LrgVscriptIssue;

struct _LrgVscriptIssue
{
	LrgVscriptSeverity  severity;
	gchar              *graph;
	gchar              *node;
	gchar              *path;
	gchar              *message;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vscript_issue_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VSCRIPT_ISSUE (lrg_vscript_issue_get_type ())

/**
 * lrg_vscript_issue_copy:
 * @self: an issue
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptIssue * lrg_vscript_issue_copy (const LrgVscriptIssue *self);

/**
 * lrg_vscript_issue_free:
 * @self: (nullable): an issue
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_issue_free (LrgVscriptIssue *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVscriptIssue, lrg_vscript_issue_free)

/* ==========================================================================
 * LrgVscriptNode
 * ========================================================================== */

#define LRG_TYPE_VSCRIPT_NODE (lrg_vscript_node_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVscriptNode, lrg_vscript_node, LRG, VSCRIPT_NODE, GObject)

/**
 * lrg_vscript_node_new:
 * @id: the node id (letters, digits, `_` and `-`)
 * @kind: the node kind, e.g. "flow.branch" (see lrg_vscript_node_types_list())
 *
 * Creates a node outside any graph.
 *
 * Returns: (transfer full): a new node
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptNode * lrg_vscript_node_new (const gchar *id,
                                       const gchar *kind);

/**
 * lrg_vscript_node_get_id:
 * @self: a node
 *
 * Returns: (transfer none): the node id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_get_id (LrgVscriptNode *self);

/**
 * lrg_vscript_node_get_kind:
 * @self: a node
 *
 * Returns: (transfer none): the node kind ("flow.branch")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_get_kind (LrgVscriptNode *self);

/**
 * lrg_vscript_node_get_position:
 * @self: a node
 * @x: (out) (optional): the left edge in graph units
 * @y: (out) (optional): the top edge in graph units
 *
 * Gets where an editor draws the node.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_node_get_position (LrgVscriptNode *self,
                                    gdouble        *x,
                                    gdouble        *y);

/**
 * lrg_vscript_node_set_position:
 * @self: a node
 * @x: the left edge in graph units
 * @y: the top edge in graph units
 *
 * Moves the node (editor layout only; no effect on running).
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_node_set_position (LrgVscriptNode *self,
                                    gdouble         x,
                                    gdouble         y);

/**
 * lrg_vscript_node_get_param:
 * @self: a node
 * @key: a parameter name ("name", "function", "count" ...)
 *
 * Returns: (transfer none) (nullable): the value, %NULL when unset
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_vscript_node_get_param (LrgVscriptNode *self,
                                       const gchar    *key);

/**
 * lrg_vscript_node_get_param_string:
 * @self: a node
 * @key: a parameter name
 *
 * Returns: (transfer none) (nullable): the parameter when it is a
 *   string, else %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_get_param_string (LrgVscriptNode *self,
                                                 const gchar    *key);

/**
 * lrg_vscript_node_set_param:
 * @self: a node
 * @key: a parameter name
 * @value: (nullable): a boolean, integer, double or string variant (a
 *   floating reference is consumed), or %NULL to remove the parameter
 *
 * Sets a node parameter (the constant configuration of the node: a
 * variable name, a function name, a literal value ...).
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_node_set_param (LrgVscriptNode *self,
                                 const gchar    *key,
                                 GVariant       *value);

/**
 * lrg_vscript_node_dup_param_keys:
 * @self: a node
 *
 * Returns: (transfer full) (array zero-terminated=1): the parameter
 *   names in file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_node_dup_param_keys (LrgVscriptNode *self);

/**
 * lrg_vscript_node_get_input:
 * @self: a node
 * @pin: an input data pin name
 *
 * The literal value an unlinked data input takes.
 *
 * Returns: (transfer none) (nullable): the literal, %NULL when unset
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_vscript_node_get_input (LrgVscriptNode *self,
                                       const gchar    *pin);

/**
 * lrg_vscript_node_set_input:
 * @self: a node
 * @pin: an input data pin name
 * @value: (nullable): a boolean, integer, double or string variant (a
 *   floating reference is consumed), or %NULL to remove it
 *
 * Sets the literal an unlinked data input takes. A linked input ignores
 * its literal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_node_set_input (LrgVscriptNode *self,
                                 const gchar    *pin,
                                 GVariant       *value);

/**
 * lrg_vscript_node_dup_input_keys:
 * @self: a node
 *
 * Returns: (transfer full) (array zero-terminated=1): the pins with a
 *   literal, in file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_node_dup_input_keys (LrgVscriptNode *self);

/* ==========================================================================
 * LrgVscriptGraph
 * ========================================================================== */

#define LRG_TYPE_VSCRIPT_GRAPH (lrg_vscript_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVscriptGraph, lrg_vscript_graph, LRG, VSCRIPT_GRAPH, GObject)

/**
 * lrg_vscript_graph_get_name:
 * @self: a graph
 *
 * Returns: (transfer none): the graph name ("" for the main graph)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_graph_get_name (LrgVscriptGraph *self);

/**
 * lrg_vscript_graph_get_n_nodes:
 * @self: a graph
 *
 * Returns: the number of nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vscript_graph_get_n_nodes (LrgVscriptGraph *self);

/**
 * lrg_vscript_graph_get_nth_node:
 * @self: a graph
 * @index: an index below lrg_vscript_graph_get_n_nodes()
 *
 * Returns: (transfer none) (nullable): the node, in file order
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptNode * lrg_vscript_graph_get_nth_node (LrgVscriptGraph *self,
                                                 guint            index);

/**
 * lrg_vscript_graph_get_node:
 * @self: a graph
 * @id: a node id
 *
 * Returns: (transfer none) (nullable): the node with @id
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptNode * lrg_vscript_graph_get_node (LrgVscriptGraph *self,
                                             const gchar     *id);

/**
 * lrg_vscript_graph_add_node:
 * @self: a graph
 * @node: a node not in any graph
 * @error: return location for a #GError
 *
 * Adds @node (the graph takes a reference).
 *
 * Returns: %FALSE when the id is taken or invalid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_add_node (LrgVscriptGraph  *self,
                                     LrgVscriptNode   *node,
                                     GError          **error);

/**
 * lrg_vscript_graph_create_node:
 * @self: a graph
 * @kind: a node kind ("math.add")
 * @id: (nullable): the id, or %NULL for a free one made from @kind
 * @x: left edge
 * @y: top edge
 * @error: return location for a #GError
 *
 * Creates and adds a node of a catalogued kind.
 *
 * Returns: (transfer none) (nullable): the new node, or %NULL with
 *   @error (unknown kind, id taken)
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptNode * lrg_vscript_graph_create_node (LrgVscriptGraph  *self,
                                                const gchar      *kind,
                                                const gchar      *id,
                                                gdouble           x,
                                                gdouble           y,
                                                GError          **error);

/**
 * lrg_vscript_graph_remove_node:
 * @self: a graph
 * @id: a node id
 *
 * Removes the node and every link touching it.
 *
 * Returns: %FALSE when there is no such node
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_remove_node (LrgVscriptGraph *self,
                                        const gchar     *id);

/**
 * lrg_vscript_graph_rename_node:
 * @self: a graph
 * @id: the current id
 * @new_id: the new id
 * @error: return location for a #GError
 *
 * Renames a node and updates the links touching it.
 *
 * Returns: %FALSE with @error (no such node, id taken or invalid)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_rename_node (LrgVscriptGraph  *self,
                                        const gchar      *id,
                                        const gchar      *new_id,
                                        GError          **error);

/**
 * lrg_vscript_graph_dup_free_id:
 * @self: a graph
 * @stem: the id prefix ("branch")
 *
 * Returns: (transfer full): @stem when it is free, else @stem with the
 *   lowest free number appended ("branch2")
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_vscript_graph_dup_free_id (LrgVscriptGraph *self,
                                       const gchar     *stem);

/**
 * lrg_vscript_graph_get_links:
 * @self: a graph
 *
 * Returns: (transfer none) (element-type LrgVscriptLink): the links in
 *   file order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_vscript_graph_get_links (LrgVscriptGraph *self);

/**
 * lrg_vscript_graph_add_link:
 * @self: a graph
 * @from_node: the source node id
 * @from_pin: the source (output) pin
 * @to_node: the target node id
 * @to_pin: the target (input) pin
 *
 * Adds a link without checking pins (lrg_vscript_program_connect() checks
 * them). An identical link is not added twice.
 *
 * Returns: %FALSE when a node is missing or the link already exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_add_link (LrgVscriptGraph *self,
                                     const gchar     *from_node,
                                     const gchar     *from_pin,
                                     const gchar     *to_node,
                                     const gchar     *to_pin);

/**
 * lrg_vscript_graph_remove_link:
 * @self: a graph
 * @from_node: the source node id
 * @from_pin: the source pin
 * @to_node: the target node id
 * @to_pin: the target pin
 *
 * Returns: %FALSE when there is no such link
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_remove_link (LrgVscriptGraph *self,
                                        const gchar     *from_node,
                                        const gchar     *from_pin,
                                        const gchar     *to_node,
                                        const gchar     *to_pin);

/**
 * lrg_vscript_graph_find_link_into:
 * @self: a graph
 * @to_node: a node id
 * @to_pin: an input pin
 *
 * Returns: (transfer none) (nullable): the first link ending on the pin
 */
LRG_AVAILABLE_IN_ALL
const LrgVscriptLink * lrg_vscript_graph_find_link_into (LrgVscriptGraph *self,
                                                         const gchar     *to_node,
                                                         const gchar     *to_pin);

/**
 * lrg_vscript_graph_find_link_from:
 * @self: a graph
 * @from_node: a node id
 * @from_pin: an output pin
 *
 * Returns: (transfer none) (nullable): the first link starting at the pin
 */
LRG_AVAILABLE_IN_ALL
const LrgVscriptLink * lrg_vscript_graph_find_link_from (LrgVscriptGraph *self,
                                                         const gchar     *from_node,
                                                         const gchar     *from_pin);

/**
 * lrg_vscript_graph_get_comments:
 * @self: a graph
 *
 * Returns: (transfer none) (element-type LrgVscriptComment): the comment
 *   frames in file order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_vscript_graph_get_comments (LrgVscriptGraph *self);

/**
 * lrg_vscript_graph_add_comment:
 * @self: a graph
 * @comment: a comment frame (copied; a %NULL or taken id gets a free one)
 *
 * Returns: (transfer none): the id of the added frame
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_graph_add_comment (LrgVscriptGraph         *self,
                                             const LrgVscriptComment *comment);

/**
 * lrg_vscript_graph_get_comment:
 * @self: a graph
 * @id: a comment id
 *
 * Returns: (transfer none) (nullable): the frame, which may be changed
 *   in place
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptComment * lrg_vscript_graph_get_comment (LrgVscriptGraph *self,
                                                   const gchar     *id);

/**
 * lrg_vscript_graph_remove_comment:
 * @self: a graph
 * @id: a comment id
 *
 * Returns: %FALSE when there is no such frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_graph_remove_comment (LrgVscriptGraph *self,
                                           const gchar     *id);

/**
 * lrg_vscript_graph_get_interface:
 * @self: a subgraph
 * @direction: %LRG_VSCRIPT_PIN_INPUT for the parameters,
 *   %LRG_VSCRIPT_PIN_OUTPUT for the results
 *
 * The interface of a subgraph: the data pins of its `subgraph.call`
 * nodes (and of its own `subgraph.input` / `subgraph.output` nodes).
 * The main graph has none.
 *
 * Returns: (transfer none) (element-type LrgVscriptPin): the pins
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_vscript_graph_get_interface (LrgVscriptGraph        *self,
                                             LrgVscriptPinDirection  direction);

/**
 * lrg_vscript_graph_set_interface_pin:
 * @self: a subgraph
 * @direction: parameter or result
 * @name: the pin name
 * @type: (nullable): the value type, %NULL to remove the pin
 *
 * Adds, retypes or (with a %NULL @type) removes an interface pin.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_graph_set_interface_pin (LrgVscriptGraph        *self,
                                          LrgVscriptPinDirection  direction,
                                          const gchar            *name,
                                          const gchar            *type);

/* ==========================================================================
 * LrgVscriptProgram
 * ========================================================================== */

#define LRG_TYPE_VSCRIPT_PROGRAM (lrg_vscript_program_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVscriptProgram, lrg_vscript_program, LRG, VSCRIPT_PROGRAM, GObject)

/**
 * lrg_vscript_program_new:
 *
 * Creates an empty program (an empty main graph, no variables).
 *
 * Returns: (transfer full): a new #LrgVscriptProgram
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_vscript_program_new (void);

/**
 * lrg_vscript_program_new_from_data:
 * @data: (array length=length) (element-type guint8): `.rvscript` text
 * @length: the length of @data, or -1 when nul-terminated
 * @error: return location for a #GError
 *
 * Parses a program. Errors name the key path (`nodes[2].x: expected a
 * number`). Keys the format does not know are kept for saving.
 *
 * Returns: (transfer full) (nullable): the program, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_vscript_program_new_from_data (const gchar  *data,
                                                       gssize        length,
                                                       GError      **error);

/**
 * lrg_vscript_program_new_from_file:
 * @path: (type filename): a `.rvscript` file
 * @error: return location for a #GError
 *
 * Loads a program and remembers its folder (lrg_vscript_program_get_base_dir()).
 *
 * Returns: (transfer full) (nullable): the program, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_vscript_program_new_from_file (const gchar  *path,
                                                       GError      **error);

/**
 * lrg_vscript_program_to_data:
 * @self: a program
 * @error: return location for a #GError
 *
 * Writes the program in its canonical form (byte-stable across round
 * trips).
 *
 * Returns: (transfer full) (nullable): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_vscript_program_to_data (LrgVscriptProgram  *self,
                                     GError            **error);

/**
 * lrg_vscript_program_save_to_file:
 * @self: a program
 * @path: (type filename): the file to write
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_program_save_to_file (LrgVscriptProgram  *self,
                                           const gchar        *path,
                                           GError            **error);

/**
 * lrg_vscript_program_duplicate:
 * @self: a program
 *
 * Returns: (transfer full): a deep copy (through the text form)
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_vscript_program_duplicate (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_get_name:
 * @self: a program
 *
 * Returns: (transfer none) (nullable): the program name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_program_get_name (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_name:
 * @self: a program
 * @name: (nullable): the name
 *
 * Sets the program name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_name (LrgVscriptProgram *self,
                                   const gchar       *name);

/**
 * lrg_vscript_program_get_description:
 * @self: a program
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_program_get_description (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_description:
 * @self: a program
 * @description: (nullable): free text
 *
 * Sets the description.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_description (LrgVscriptProgram *self,
                                          const gchar       *description);

/**
 * lrg_vscript_program_get_view:
 * @self: a program
 *
 * Returns: the editor the program opens in
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptView lrg_vscript_program_get_view (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_view:
 * @self: a program
 * @view: the editor the program opens in
 *
 * Sets the `view:` key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_view (LrgVscriptProgram *self,
                                   LrgVscriptView     view);

/**
 * lrg_vscript_program_get_seed:
 * @self: a program
 *
 * Returns: the seed of the `math.random*` nodes (runs are deterministic)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_vscript_program_get_seed (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_seed:
 * @self: a program
 * @seed: the random seed
 *
 * Sets the seed every run starts its random stream from.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_seed (LrgVscriptProgram *self,
                                   guint32            seed);

/**
 * lrg_vscript_program_get_step_budget:
 * @self: a program
 *
 * Returns: the node executions one event dispatch may take
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vscript_program_get_step_budget (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_step_budget:
 * @self: a program
 * @budget: the step budget (0 for %LRG_VSCRIPT_DEFAULT_STEP_BUDGET)
 *
 * Sets the per-dispatch step budget.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_step_budget (LrgVscriptProgram *self,
                                          guint              budget);

/**
 * lrg_vscript_program_get_fixed_step:
 * @self: a program
 *
 * Returns: the period of `event.fixed_update`, in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_vscript_program_get_fixed_step (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_fixed_step:
 * @self: a program
 * @seconds: the fixed update period (> 0; default 1/60)
 *
 * Sets the period of `event.fixed_update`.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_fixed_step (LrgVscriptProgram *self,
                                         gdouble            seconds);

/**
 * lrg_vscript_program_get_base_dir:
 * @self: a program
 *
 * Returns: (transfer none) (nullable) (type filename): the folder
 *   `subgraph.call` nodes with a `file:` resolve from
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_program_get_base_dir (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_base_dir:
 * @self: a program
 * @dir: (nullable) (type filename): a folder
 *
 * Sets the folder relative `file:` references resolve from.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_base_dir (LrgVscriptProgram *self,
                                       const gchar       *dir);

/**
 * lrg_vscript_program_get_main:
 * @self: a program
 *
 * Returns: (transfer none): the main graph
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptGraph * lrg_vscript_program_get_main (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_get_graph:
 * @self: a program
 * @name: (nullable): a subgraph name, or %NULL / "" for the main graph
 *
 * Returns: (transfer none) (nullable): the graph
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptGraph * lrg_vscript_program_get_graph (LrgVscriptProgram *self,
                                                 const gchar       *name);

/**
 * lrg_vscript_program_add_subgraph:
 * @self: a program
 * @name: the subgraph name
 * @error: return location for a #GError
 *
 * Adds an empty subgraph with a `subgraph.input` and a `subgraph.output`
 * node.
 *
 * Returns: (transfer none) (nullable): the subgraph, or %NULL when the
 *   name is taken or invalid
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptGraph * lrg_vscript_program_add_subgraph (LrgVscriptProgram  *self,
                                                    const gchar        *name,
                                                    GError            **error);

/**
 * lrg_vscript_program_remove_subgraph:
 * @self: a program
 * @name: a subgraph name
 *
 * Returns: %FALSE when there is no such subgraph
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_program_remove_subgraph (LrgVscriptProgram *self,
                                              const gchar       *name);

/**
 * lrg_vscript_program_dup_subgraph_names:
 * @self: a program
 *
 * Returns: (transfer full) (array zero-terminated=1): the subgraph names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_program_dup_subgraph_names (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_set_variable:
 * @self: a program
 * @name: the variable name
 * @type: "bool", "int", "float", "string", "object", "list" or "any"
 * @default_value: (nullable): the initial value (a floating reference is
 *   consumed), %NULL for the type's zero
 *
 * Declares (or changes) a graph variable.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vscript_program_set_variable (LrgVscriptProgram *self,
                                       const gchar       *name,
                                       const gchar       *type,
                                       GVariant          *default_value);

/**
 * lrg_vscript_program_remove_variable:
 * @self: a program
 * @name: a variable name
 *
 * Returns: %FALSE when there is no such variable
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_program_remove_variable (LrgVscriptProgram *self,
                                              const gchar       *name);

/**
 * lrg_vscript_program_dup_variable_names:
 * @self: a program
 *
 * Returns: (transfer full) (array zero-terminated=1): the variables in
 *   file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_program_dup_variable_names (LrgVscriptProgram *self);

/**
 * lrg_vscript_program_get_variable_type:
 * @self: a program
 * @name: a variable name
 *
 * Returns: (transfer none) (nullable): the type, %NULL when undeclared
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_program_get_variable_type (LrgVscriptProgram *self,
                                                     const gchar       *name);

/**
 * lrg_vscript_program_get_variable_default:
 * @self: a program
 * @name: a variable name
 *
 * Returns: (transfer none) (nullable): the initial value, %NULL for the
 *   type's zero
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_vscript_program_get_variable_default (LrgVscriptProgram *self,
                                                     const gchar       *name);

/**
 * lrg_vscript_program_dup_node_pins:
 * @self: a program
 * @graph: (nullable): the graph holding @node (%NULL: the main graph)
 * @node: a node of @graph
 *
 * The pins of a node: fixed by its kind, or computed from its parameters
 * (a sequence's `count`, a custom event's `args`, a format string's
 * placeholders, the signature of a GI function, the interface of a
 * subgraph).
 *
 * Returns: (transfer full) (element-type LrgVscriptPin): the pins, inputs
 *   first
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_vscript_program_dup_node_pins (LrgVscriptProgram *self,
                                               LrgVscriptGraph   *graph,
                                               LrgVscriptNode    *node);

/**
 * lrg_vscript_program_connect:
 * @self: a program
 * @graph: (nullable): the graph (%NULL: the main graph)
 * @from_node: the source node id
 * @from_pin: an output pin of it
 * @to_node: the target node id
 * @to_pin: an input pin of it
 * @error: return location for a #GError
 *
 * Links two pins after checking them: both exist, exec meets exec, data
 * meets data of compatible types, and no data loop forms. A data input
 * and an exec output hold one link: connecting replaces the old one.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_program_connect (LrgVscriptProgram  *self,
                                      LrgVscriptGraph    *graph,
                                      const gchar        *from_node,
                                      const gchar        *from_pin,
                                      const gchar        *to_node,
                                      const gchar        *to_pin,
                                      GError            **error);

/**
 * lrg_vscript_program_validate:
 * @self: a program
 * @issues: (out) (optional) (transfer full) (element-type LrgVscriptIssue):
 *   every problem found, errors first
 *
 * Checks the whole program: node kinds, required parameters, links
 * (pins, directions, types, single links), variables, subgraph
 * references and data cycles.
 *
 * Returns: %TRUE when there is no error-severity issue
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_program_validate (LrgVscriptProgram  *self,
                                       GPtrArray         **issues);

/* ==========================================================================
 * Node catalogue and GI search
 * ========================================================================== */

/**
 * lrg_vscript_node_types_list:
 *
 * Returns: (transfer full) (array zero-terminated=1): every node kind,
 *   grouped by category
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_node_types_list (void);

/**
 * lrg_vscript_node_type_exists:
 * @kind: a node kind
 *
 * Returns: %TRUE when @kind is catalogued
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_node_type_exists (const gchar *kind);

/**
 * lrg_vscript_node_type_get_title:
 * @kind: a node kind
 *
 * Returns: (transfer none) (nullable): the display title ("Branch")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_type_get_title (const gchar *kind);

/**
 * lrg_vscript_node_type_get_category:
 * @kind: a node kind
 *
 * Returns: (transfer none) (nullable): the palette category ("Flow")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_type_get_category (const gchar *kind);

/**
 * lrg_vscript_node_type_get_description:
 * @kind: a node kind
 *
 * Returns: (transfer none) (nullable): one line of help
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_vscript_node_type_get_description (const gchar *kind);

/**
 * lrg_vscript_node_type_is_event:
 * @kind: a node kind
 *
 * Returns: %TRUE for an event (an entry point with no exec input)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_node_type_is_event (const gchar *kind);

/**
 * lrg_vscript_node_type_is_pure:
 * @kind: a node kind
 *
 * Returns: %TRUE for a pure data node (no exec pins; evaluated when a
 *   value is pulled from it)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_node_type_is_pure (const gchar *kind);

/**
 * lrg_vscript_gi_available:
 *
 * Returns: %TRUE when this build has the GObject-Introspection runtime
 *   (`gi.call` nodes can run)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vscript_gi_available (void);

/**
 * lrg_vscript_gi_search:
 * @query: words that must all occur in the name (case-insensitive)
 * @limit: the most results to return (0: 100)
 *
 * Searches the functions, methods and constructors of the loaded
 * typelibs (GLib, GObject, Gio, Libregnum and Graylib are loaded when
 * found) for the `function:` parameter of a `gi.call` node.
 *
 * Returns: (transfer full) (array zero-terminated=1): names like
 *   "GLib.path_get_basename" or "Libregnum.Blackboard.set_int"; empty
 *   without the GI runtime
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vscript_gi_search (const gchar *query,
                                guint        limit);

/* ==========================================================================
 * Library-internal helpers shared with lrg-scripting-graph.c and the
 * editor. Not public API: only visible to files that define
 * LRG_VSCRIPT_PRIVATE before including this header (the GIR scanner and
 * installed-header users never do).
 * ========================================================================== */

#ifdef LRG_VSCRIPT_PRIVATE

/* node kind flags */
#define LRG_VSCRIPT_KIND_EVENT (1u << 0)
#define LRG_VSCRIPT_KIND_PURE  (1u << 1)

G_GNUC_INTERNAL guint     lrg_vscript_priv_kind_flags     (const gchar *kind);
G_GNUC_INTERNAL gboolean  lrg_vscript_priv_types_compatible (const gchar *from_type,
                                                             const gchar *to_type);
G_GNUC_INTERNAL GType     lrg_vscript_priv_list_get_type  (void);
G_GNUC_INTERNAL GPtrArray * lrg_vscript_priv_list_new     (void);
G_GNUC_INTERNAL void      lrg_vscript_priv_list_append    (GPtrArray    *list,
                                                           const GValue *value);
G_GNUC_INTERNAL void      lrg_vscript_priv_variant_to_value (GVariant *variant,
                                                             GValue   *value);
G_GNUC_INTERNAL GVariant * lrg_vscript_priv_value_to_variant (const GValue *value);
G_GNUC_INTERNAL gboolean  lrg_vscript_priv_value_to_bool  (const GValue *value);
G_GNUC_INTERNAL gint64    lrg_vscript_priv_value_to_int   (const GValue *value);
G_GNUC_INTERNAL gdouble   lrg_vscript_priv_value_to_double (const GValue *value);
G_GNUC_INTERNAL gchar *   lrg_vscript_priv_value_to_string (const GValue *value);
G_GNUC_INTERNAL gboolean  lrg_vscript_priv_value_is_number (const GValue *value);
G_GNUC_INTERNAL void      lrg_vscript_priv_zero_value     (const gchar *type,
                                                           GValue      *value);
G_GNUC_INTERNAL gboolean  lrg_vscript_priv_gi_invoke      (const gchar   *function,
                                                           const GValue  *in_values,
                                                           guint          n_in,
                                                           GArray        *out_values,
                                                           GError       **error);
G_GNUC_INTERNAL GVariant * lrg_vscript_priv_parse_literal (const gchar *text);

#endif /* LRG_VSCRIPT_PRIVATE */

G_END_DECLS
