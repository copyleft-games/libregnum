/* lrg-shader-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderGraph: a node graph (`.rshadergraph`) that generates the
 * fragment stage of an #LrgShaderAsset.  A pure data model: no UI, no
 * GL until the generated asset is compiled.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-shader-asset.h"

G_BEGIN_DECLS

/**
 * LRG_SHADER_GRAPH_ERROR:
 *
 * Error domain of #LrgShaderGraph.  Errors are #LrgShaderGraphError
 * values.
 */
#define LRG_SHADER_GRAPH_ERROR (lrg_shader_graph_error_quark ())

/**
 * lrg_shader_graph_error_quark:
 *
 * The error quark of #LRG_SHADER_GRAPH_ERROR.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_shader_graph_error_quark (void);

/**
 * LrgShaderGraphError:
 * @LRG_SHADER_GRAPH_ERROR_PARSE: the `.rshadergraph` document is
 *   malformed
 * @LRG_SHADER_GRAPH_ERROR_IO: a file could not be read or written
 * @LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE: a node id or node type does not
 *   exist
 * @LRG_SHADER_GRAPH_ERROR_UNKNOWN_PORT: a port name does not exist on
 *   its node
 * @LRG_SHADER_GRAPH_ERROR_DUPLICATE_ID: a node id is already used
 * @LRG_SHADER_GRAPH_ERROR_CYCLE: links form a cycle
 * @LRG_SHADER_GRAPH_ERROR_TYPE_MISMATCH: a link joins incompatible types
 * @LRG_SHADER_GRAPH_ERROR_MISSING_INPUT: a required input has no link
 * @LRG_SHADER_GRAPH_ERROR_NO_OUTPUT: the graph has no output node
 * @LRG_SHADER_GRAPH_ERROR_MULTIPLE_OUTPUTS: the graph has more than one
 *   output node
 * @LRG_SHADER_GRAPH_ERROR_INVALID_PARAM: a node parameter is missing or
 *   invalid (a parameter node's name or type, a swizzle mask)
 * @LRG_SHADER_GRAPH_ERROR_INVALID: validation found problems (the
 *   message lists them; lrg_shader_graph_get_problems() has each)
 *
 * Errors reported by #LrgShaderGraph.
 */
typedef enum
{
	LRG_SHADER_GRAPH_ERROR_PARSE,
	LRG_SHADER_GRAPH_ERROR_IO,
	LRG_SHADER_GRAPH_ERROR_UNKNOWN_NODE,
	LRG_SHADER_GRAPH_ERROR_UNKNOWN_PORT,
	LRG_SHADER_GRAPH_ERROR_DUPLICATE_ID,
	LRG_SHADER_GRAPH_ERROR_CYCLE,
	LRG_SHADER_GRAPH_ERROR_TYPE_MISMATCH,
	LRG_SHADER_GRAPH_ERROR_MISSING_INPUT,
	LRG_SHADER_GRAPH_ERROR_NO_OUTPUT,
	LRG_SHADER_GRAPH_ERROR_MULTIPLE_OUTPUTS,
	LRG_SHADER_GRAPH_ERROR_INVALID_PARAM,
	LRG_SHADER_GRAPH_ERROR_INVALID
} LrgShaderGraphError;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_graph_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_GRAPH_ERROR (lrg_shader_graph_error_get_type ())

/**
 * LrgShaderPortType:
 * @LRG_SHADER_PORT_FLOAT: `float`
 * @LRG_SHADER_PORT_VEC2: `vec2`
 * @LRG_SHADER_PORT_VEC3: `vec3`
 * @LRG_SHADER_PORT_VEC4: `vec4`
 * @LRG_SHADER_PORT_SAMPLER2D: `sampler2D` (only a texture parameter
 *   feeds it)
 * @LRG_SHADER_PORT_DYNAMIC: a node-library declaration only: the port
 *   takes float, vec2, vec3 or vec4 and the node resolves one width for
 *   all its dynamic ports (the widest connected one)
 *
 * The type of a node port.  Links convert a float to any vector (splat)
 * and a wider vector to a narrower one (truncation, `.xyz` of a vec4);
 * widening a vector and anything involving a sampler with a non-sampler
 * is a type mismatch.
 */
typedef enum
{
	LRG_SHADER_PORT_FLOAT,
	LRG_SHADER_PORT_VEC2,
	LRG_SHADER_PORT_VEC3,
	LRG_SHADER_PORT_VEC4,
	LRG_SHADER_PORT_SAMPLER2D,
	LRG_SHADER_PORT_DYNAMIC
} LrgShaderPortType;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_port_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_PORT_TYPE (lrg_shader_port_type_get_type ())

/* ========================================================================== */
/* Node library                                                               */
/* ========================================================================== */

/**
 * lrg_shader_graph_list_node_types:
 *
 * Every node type of the library, in palette order (inputs, maths,
 * texture, procedural, outputs).
 *
 * Returns: (transfer full): a %NULL-terminated array of type names
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_shader_graph_list_node_types (void);

/**
 * lrg_shader_node_type_exists:
 * @type: a node type name
 *
 * Returns: %TRUE when @type is in the library
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_node_type_exists (const gchar *type);

/**
 * lrg_shader_node_type_get_label:
 * @type: a node type name
 *
 * Returns: (transfer none) (nullable): the palette label, %NULL for an
 *   unknown type
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_node_type_get_label (const gchar *type);

/**
 * lrg_shader_node_type_get_category:
 * @type: a node type name
 *
 * Returns: (transfer none) (nullable): `input`, `math`, `texture`,
 *   `procedural` or `output`; %NULL for an unknown type
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_node_type_get_category (const gchar *type);

/**
 * lrg_shader_node_type_get_inputs:
 * @type: a node type name
 *
 * Returns: (transfer full) (nullable): the input port names in order,
 *   %NULL for an unknown type
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_shader_node_type_get_inputs (const gchar *type);

/**
 * lrg_shader_node_type_get_outputs:
 * @type: a node type name
 *
 * The declared output ports.  A `parameter` and a `constant` node have
 * one output, `value`, typed by their parameters.
 *
 * Returns: (transfer full) (nullable): the output port names in order,
 *   %NULL for an unknown type
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_shader_node_type_get_outputs (const gchar *type);

/**
 * lrg_shader_node_type_get_port_type:
 * @type: a node type name
 * @port: a port name
 * @output: %TRUE for an output port, %FALSE for an input
 *
 * The declared type of a port (may be %LRG_SHADER_PORT_DYNAMIC; the
 * value of `parameter` / `constant` outputs depends on their
 * parameters and reads as dynamic here).
 *
 * Returns: the port type, %LRG_SHADER_PORT_DYNAMIC also for an unknown
 *   type or port
 */
LRG_AVAILABLE_IN_ALL
LrgShaderPortType lrg_shader_node_type_get_port_type (const gchar *type,
                                                      const gchar *port,
                                                      gboolean     output);

/* ========================================================================== */
/* LrgShaderGraphProblem                                                      */
/* ========================================================================== */

/**
 * LrgShaderGraphProblem:
 *
 * One validation problem: its code, the node (and port) it concerns and
 * a message.  Boxed and immutable.
 */
typedef struct _LrgShaderGraphProblem LrgShaderGraphProblem;

LRG_AVAILABLE_IN_ALL
GType lrg_shader_graph_problem_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SHADER_GRAPH_PROBLEM (lrg_shader_graph_problem_get_type ())

/**
 * lrg_shader_graph_problem_copy:
 * @self: an #LrgShaderGraphProblem
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraphProblem * lrg_shader_graph_problem_copy (const LrgShaderGraphProblem *self);

/**
 * lrg_shader_graph_problem_free:
 * @self: (nullable): an #LrgShaderGraphProblem
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_graph_problem_free (LrgShaderGraphProblem *self);

/**
 * lrg_shader_graph_problem_get_code:
 * @self: an #LrgShaderGraphProblem
 *
 * Returns: the problem kind
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraphError lrg_shader_graph_problem_get_code (const LrgShaderGraphProblem *self);

/**
 * lrg_shader_graph_problem_get_node_id:
 * @self: an #LrgShaderGraphProblem
 *
 * Returns: (transfer none) (nullable): the node concerned, %NULL for a
 *   graph-wide problem (no output node)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_problem_get_node_id (const LrgShaderGraphProblem *self);

/**
 * lrg_shader_graph_problem_get_port:
 * @self: an #LrgShaderGraphProblem
 *
 * Returns: (transfer none) (nullable): the input port concerned, or
 *   %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_problem_get_port (const LrgShaderGraphProblem *self);

/**
 * lrg_shader_graph_problem_get_message:
 * @self: an #LrgShaderGraphProblem
 *
 * Returns: (transfer none): the message, starting with the node id
 *   (`node 'mul1': input 'b': ...`)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_problem_get_message (const LrgShaderGraphProblem *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgShaderGraphProblem, lrg_shader_graph_problem_free)

/* ========================================================================== */
/* LrgShaderGraph                                                             */
/* ========================================================================== */

#define LRG_TYPE_SHADER_GRAPH (lrg_shader_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShaderGraph, lrg_shader_graph, LRG, SHADER_GRAPH, GObject)

/**
 * lrg_shader_graph_new:
 *
 * Creates an empty graph named `untitled`.
 *
 * Returns: (transfer full): a new #LrgShaderGraph
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraph * lrg_shader_graph_new (void);

/**
 * lrg_shader_graph_new_from_file:
 * @path: a `.rshadergraph` file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a graph.  Structural problems (unknown node types or ports,
 * duplicate ids, links to missing nodes) fail the load; semantic ones
 * (cycles, type mismatches, missing inputs) load and are reported by
 * lrg_shader_graph_validate().
 *
 * Returns: (transfer full) (nullable): the graph, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraph * lrg_shader_graph_new_from_file (const gchar  *path,
                                                 GError      **error);

/**
 * lrg_shader_graph_new_from_data:
 * @data: `.rshadergraph` text
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @error: return location for a #GError, or %NULL
 *
 * Loads a graph from memory.
 *
 * Returns: (transfer full) (nullable): the graph, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraph * lrg_shader_graph_new_from_data (const gchar  *data,
                                                 gssize        length,
                                                 GError      **error);

/**
 * lrg_shader_graph_save_to_data:
 * @self: an #LrgShaderGraph
 * @length: (out) (optional): the length of the text
 * @error: return location for a #GError, or %NULL
 *
 * Writes the graph as a `.rshadergraph` document: nodes in insertion
 * order, links in insertion order, defaults left out, unknown top-level
 * keys of the loaded document kept.  Byte-stable on reload.
 *
 * Returns: (transfer full) (nullable): the document text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_shader_graph_save_to_data (LrgShaderGraph  *self,
                                       gsize           *length,
                                       GError         **error);

/**
 * lrg_shader_graph_save_to_file:
 * @self: an #LrgShaderGraph
 * @path: the file to write (atomically)
 * @error: return location for a #GError, or %NULL
 *
 * Writes lrg_shader_graph_save_to_data() to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_save_to_file (LrgShaderGraph  *self,
                                        const gchar     *path,
                                        GError         **error);

/**
 * lrg_shader_graph_get_name:
 * @self: an #LrgShaderGraph
 *
 * Returns: (transfer none): the name (also the generated asset's name)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_get_name (LrgShaderGraph *self);

/**
 * lrg_shader_graph_set_name:
 * @self: an #LrgShaderGraph
 * @name: the name
 *
 * Sets the name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shader_graph_set_name (LrgShaderGraph *self,
                                const gchar    *name);

/* Nodes */

/**
 * lrg_shader_graph_add_node:
 * @self: an #LrgShaderGraph
 * @type: a node type of the library
 * @id: (nullable): the node id (a GLSL identifier, unique in the graph),
 *   or %NULL to pick `<type><n>`
 * @error: return location for a #GError, or %NULL
 *
 * Adds a node at (0, 0) with no parameters and no input values.
 *
 * Returns: (transfer none) (nullable): the node id, or %NULL with
 *   @error set (unknown type, invalid or duplicate id)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_add_node (LrgShaderGraph  *self,
                                         const gchar     *type,
                                         const gchar     *id,
                                         GError         **error);

/**
 * lrg_shader_graph_remove_node:
 * @self: an #LrgShaderGraph
 * @id: a node id
 *
 * Removes a node and every link touching it.
 *
 * Returns: %TRUE when the node existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_remove_node (LrgShaderGraph *self,
                                       const gchar    *id);

/**
 * lrg_shader_graph_get_n_nodes:
 * @self: an #LrgShaderGraph
 *
 * Returns: the number of nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_graph_get_n_nodes (LrgShaderGraph *self);

/**
 * lrg_shader_graph_get_node_ids:
 * @self: an #LrgShaderGraph
 *
 * Returns: (transfer full): the node ids in insertion order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_shader_graph_get_node_ids (LrgShaderGraph *self);

/**
 * lrg_shader_graph_has_node:
 * @self: an #LrgShaderGraph
 * @id: a node id
 *
 * Returns: %TRUE when the node exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_has_node (LrgShaderGraph *self,
                                    const gchar    *id);

/**
 * lrg_shader_graph_get_node_type:
 * @self: an #LrgShaderGraph
 * @id: a node id
 *
 * Returns: (transfer none) (nullable): the node's type, %NULL for an
 *   unknown id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_get_node_type (LrgShaderGraph *self,
                                              const gchar    *id);

/**
 * lrg_shader_graph_set_node_position:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @x: canvas X
 * @y: canvas Y
 *
 * Sets where an editor draws the node (saved, ignored by generation).
 *
 * Returns: %FALSE for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_set_node_position (LrgShaderGraph *self,
                                             const gchar    *id,
                                             gfloat          x,
                                             gfloat          y);

/**
 * lrg_shader_graph_get_node_position:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @out_x: (out) (optional): canvas X
 * @out_y: (out) (optional): canvas Y
 *
 * Returns: %FALSE for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_get_node_position (LrgShaderGraph *self,
                                             const gchar    *id,
                                             gfloat         *out_x,
                                             gfloat         *out_y);

/**
 * lrg_shader_graph_set_node_param:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @key: `name`, `type`, `hint` or `texture` (parameter nodes), `mask`
 *   (swizzle), or `blend` / `cull` (output nodes: the generated asset's
 *   render state)
 * @value: (nullable): the value, %NULL to clear
 *
 * Sets a string parameter.
 *
 * Returns: %FALSE for an unknown id or a key the node type does not
 *   take
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_set_node_param (LrgShaderGraph *self,
                                          const gchar    *id,
                                          const gchar    *key,
                                          const gchar    *value);

/**
 * lrg_shader_graph_get_node_param:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @key: a string parameter key
 *
 * Returns: (transfer none) (nullable): the value, %NULL when unset
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_shader_graph_get_node_param (LrgShaderGraph *self,
                                               const gchar    *id,
                                               const gchar    *key);

/**
 * lrg_shader_graph_set_node_values:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @key: `default` or `range` (parameter nodes) or `value` (constant)
 * @values: (nullable) (array length=n_values): the numbers
 * @n_values: 0 (clear) .. 4 (`range` takes exactly 2)
 *
 * Sets a numeric parameter.
 *
 * Returns: %FALSE for an unknown id, a key the node does not take or a
 *   wrong count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_set_node_values (LrgShaderGraph *self,
                                           const gchar    *id,
                                           const gchar    *key,
                                           const gfloat   *values,
                                           guint           n_values);

/**
 * lrg_shader_graph_get_node_values:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @key: a numeric parameter key
 * @out_values: (out caller-allocates) (array fixed-size=4): receives the
 *   numbers
 *
 * Returns: the number of values set (0 when unset or unknown)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_graph_get_node_values (LrgShaderGraph *self,
                                        const gchar    *id,
                                        const gchar    *key,
                                        gfloat         *out_values);

/**
 * lrg_shader_graph_set_input_value:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @port: an input port
 * @values: (nullable) (array length=n_values): the constant used while
 *   the input has no link
 * @n_values: 0 (back to the library default) .. 4; one number splats,
 *   more set the width of a dynamic input
 *
 * Sets the value an unconnected input takes.
 *
 * Returns: %FALSE for an unknown node or port, a sampler port, or more
 *   values than the port has components
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_set_input_value (LrgShaderGraph *self,
                                           const gchar    *id,
                                           const gchar    *port,
                                           const gfloat   *values,
                                           guint           n_values);

/**
 * lrg_shader_graph_get_input_value:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @port: an input port
 * @out_values: (out caller-allocates) (array fixed-size=4): receives the
 *   numbers
 *
 * Returns: the number of values set on the input (0: the library
 *   default applies)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_graph_get_input_value (LrgShaderGraph *self,
                                        const gchar    *id,
                                        const gchar    *port,
                                        gfloat         *out_values);

/* Links */

/**
 * lrg_shader_graph_connect:
 * @self: an #LrgShaderGraph
 * @from_node: the source node
 * @from_port: an output port of @from_node
 * @to_node: the target node
 * @to_port: an input port of @to_node
 * @error: return location for a #GError, or %NULL
 *
 * Links an output to an input, replacing the input's previous link (an
 * input has at most one link; an output feeds any number).  Only the
 * ports' existence is checked here; types and cycles are checked by
 * lrg_shader_graph_validate() so that an editor can hold an invalid
 * graph while it is being edited.
 *
 * Returns: %TRUE when linked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_connect (LrgShaderGraph  *self,
                                   const gchar     *from_node,
                                   const gchar     *from_port,
                                   const gchar     *to_node,
                                   const gchar     *to_port,
                                   GError         **error);

/**
 * lrg_shader_graph_disconnect:
 * @self: an #LrgShaderGraph
 * @to_node: the target node
 * @to_port: the input port
 *
 * Removes the link into an input.
 *
 * Returns: %TRUE when a link was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_disconnect (LrgShaderGraph *self,
                                      const gchar    *to_node,
                                      const gchar    *to_port);

/**
 * lrg_shader_graph_get_n_links:
 * @self: an #LrgShaderGraph
 *
 * Returns: the number of links
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shader_graph_get_n_links (LrgShaderGraph *self);

/**
 * lrg_shader_graph_get_link:
 * @self: an #LrgShaderGraph
 * @index: 0 .. lrg_shader_graph_get_n_links() - 1
 * @out_from_node: (out) (optional) (transfer none): the source node
 * @out_from_port: (out) (optional) (transfer none): the output port
 * @out_to_node: (out) (optional) (transfer none): the target node
 * @out_to_port: (out) (optional) (transfer none): the input port
 *
 * Reads a link, in insertion order.
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_get_link (LrgShaderGraph  *self,
                                    guint            index,
                                    const gchar    **out_from_node,
                                    const gchar    **out_from_port,
                                    const gchar    **out_to_node,
                                    const gchar    **out_to_port);

/* Validation and generation */

/**
 * lrg_shader_graph_validate:
 * @self: an #LrgShaderGraph
 * @error: return location for a #GError, or %NULL
 *
 * Checks the whole graph: exactly one output node, no cycles, every link
 * between compatible types (after resolving dynamic ports), every
 * required input linked, valid node parameters.  Every problem is
 * collected (lrg_shader_graph_get_problems()); @error is
 * %LRG_SHADER_GRAPH_ERROR_INVALID with one problem per line.
 *
 * Returns: %TRUE when the graph can generate a shader
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shader_graph_validate (LrgShaderGraph  *self,
                                    GError         **error);

/**
 * lrg_shader_graph_get_problems:
 * @self: an #LrgShaderGraph
 *
 * Returns: (transfer none) (element-type LrgShaderGraphProblem): the
 *   problems found by the last lrg_shader_graph_validate() (or
 *   generation)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_shader_graph_get_problems (LrgShaderGraph *self);

/**
 * lrg_shader_graph_get_resolved_port_type:
 * @self: an #LrgShaderGraph
 * @id: a node id
 * @port: a port name
 * @output: %TRUE for an output port
 *
 * The concrete type of a port after the last validation resolved the
 * dynamic ports (an editor colours ports and links with it).
 *
 * Returns: the type; %LRG_SHADER_PORT_DYNAMIC before validation or for
 *   an unknown node or port
 */
LRG_AVAILABLE_IN_ALL
LrgShaderPortType lrg_shader_graph_get_resolved_port_type (LrgShaderGraph *self,
                                                           const gchar    *id,
                                                           const gchar    *port,
                                                           gboolean        output);

/**
 * lrg_shader_graph_generate_fragment:
 * @self: an #LrgShaderGraph
 * @error: return location for a #GError, or %NULL
 *
 * Validates, then writes the fragment stage body (no preamble): helper
 * functions the nodes need, then `main()` with one statement per
 * reachable node in dependency order (each node computed once, named
 * `sg_<id>`), ending in the output node's code.  Deterministic: the
 * text depends only on the graph's nodes, parameters and links, never
 * on insertion order or memory addresses.
 *
 * Returns: (transfer full) (nullable): the GLSL, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_shader_graph_generate_fragment (LrgShaderGraph  *self,
                                            GError         **error);

/**
 * lrg_shader_graph_generate:
 * @self: an #LrgShaderGraph
 * @error: return location for a #GError, or %NULL
 *
 * Generates the `.rshader`-equivalent asset: the graph's name, `lit`
 * for a `pbr_output` graph, the generated fragment stage inline, the
 * default vertex stage, one uniform per reachable `parameter` node (in
 * generation order, with its default, range and hint), and a render
 * state taken from the output node's `blend` / `cull` params when set.
 * Nothing is compiled.
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_shader_graph_generate (LrgShaderGraph  *self,
                                            GError         **error);

G_END_DECLS
