/* lrg-editor-shader-graph-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorShaderGraphEditor: the data editor of `.rshadergraph` shader
 * graphs.
 *
 * The graph is drawn on an #LrgNodeGraph canvas: a palette of the node
 * library grouped by category on the left (click a type to add it), the
 * canvas in the middle with typed, coloured ports, and on the right a
 * live preview sphere drawing the generated shader over the editor's
 * preview lighting, a property grid for the selected node (parameters
 * and the values of unconnected inputs) and the generated GLSL.
 * Validation runs after every edit: nodes with problems turn red and
 * carry the message, and the issues list names them. Saving writes the
 * graph and, when it is valid, the generated `.rshader` next to it.
 *
 * The graph lives in an #LrgEditorDocument: every edit (wire, move,
 * add, delete, a parameter) is one undoable step of that document.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../graphics/lrg-shader-graph.h"
#include "../../graphics/lrg-shader-asset.h"
#include "../../ui/lrg-node-graph.h"
#include "data/lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SHADER_GRAPH_EDITOR (lrg_editor_shader_graph_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorShaderGraphEditor, lrg_editor_shader_graph_editor, LRG, EDITOR_SHADER_GRAPH_EDITOR,
                      LrgEditorDataEditor)

/**
 * lrg_editor_shader_graph_editor_new:
 * @app: (nullable): the editor application
 * @document: a shader graph document
 *   (%LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH)
 *
 * Creates the editor without docking it; lrg_editor_data_editor_open()
 * (or lrg_editor_shader_graph_editor_open()) is what the editor uses.
 *
 * Returns: (transfer full): a new #LrgEditorShaderGraphEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorShaderGraphEditor * lrg_editor_shader_graph_editor_new (LrgEditorApp      *app,
                                                                 LrgEditorDocument *document);

/**
 * lrg_editor_shader_graph_editor_install:
 * @app: the editor application
 *
 * Registers the `shadergraph.*` IPC methods on @app. Safe to call more
 * than once; the assets panel calls it when it is created.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_shader_graph_editor_install (LrgEditorApp *app);

/**
 * lrg_editor_shader_graph_editor_open:
 * @app: the editor application
 * @path: a `.rshadergraph` file, absolute or project-relative
 * @error: return location for a #GError
 *
 * Opens @path in a shader graph editor tab (or presents its open tab).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error
 *   set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorShaderGraphEditor * lrg_editor_shader_graph_editor_open (LrgEditorApp  *app,
                                                                  const gchar   *path,
                                                                  GError       **error);

/**
 * lrg_editor_shader_graph_editor_create:
 * @app: (nullable): the editor application (relative paths resolve
 *   against its project directory)
 * @path: the new `.rshadergraph` file
 * @master: (nullable): "pbr" (default) or "unlit": the output node of
 *   the starter graph
 * @error: return location for a #GError
 *
 * Writes a small, valid starter graph: a `tint` colour parameter wired
 * to the output's colour, plus (for "pbr") a fresnel rim glow on the
 * emission. Fails with `G_IO_ERROR_EXISTS` when @path exists.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_create (LrgEditorApp  *app,
                                                const gchar   *path,
                                                const gchar   *master,
                                                GError       **error);

/* ==========================================================================
 * The graph
 * ========================================================================== */

/**
 * lrg_editor_shader_graph_editor_get_graph:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * The graph model the canvas shows. Edit it through the editor's
 * functions (they keep the canvas and the document in step); it is
 * replaced after an undo, a redo or a reload.
 *
 * Returns: (transfer none): the graph
 */
LRG_AVAILABLE_IN_ALL
LrgShaderGraph * lrg_editor_shader_graph_editor_get_graph (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_get_canvas:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (transfer none): the #LrgNodeGraph widget showing the graph
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_editor_shader_graph_editor_get_canvas (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_add_node:
 * @self: an #LrgEditorShaderGraphEditor
 * @type: a node type of the library (lrg_shader_graph_list_node_types())
 * @id: (nullable): the node id, %NULL to pick one
 * @x: canvas X of the card
 * @y: canvas Y of the card
 * @error: return location for a #GError
 *
 * Adds a node (one undo step) and selects it. A `parameter` node gets a
 * free uniform name and the `float` type so it validates at once.
 *
 * Returns: (transfer none) (nullable): the new node's id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_shader_graph_editor_add_node (LrgEditorShaderGraphEditor  *self,
                                                       const gchar                 *type,
                                                       const gchar                 *id,
                                                       gfloat                       x,
                                                       gfloat                       y,
                                                       GError                     **error);

/**
 * lrg_editor_shader_graph_editor_remove_node:
 * @self: an #LrgEditorShaderGraphEditor
 * @id: a node id
 *
 * Removes a node and its links (one undo step).
 *
 * Returns: %TRUE when the node existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_remove_node (LrgEditorShaderGraphEditor *self,
                                                     const gchar                *id);

/**
 * lrg_editor_shader_graph_editor_connect:
 * @self: an #LrgEditorShaderGraphEditor
 * @from_node: the source node
 * @from_port: one of its outputs
 * @to_node: the target node
 * @to_port: one of its inputs (its previous link is replaced)
 * @error: return location for a #GError
 *
 * Wires an output to an input (one undo step). Type problems do not
 * fail here; they show on the nodes after validation.
 *
 * Returns: %TRUE when linked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_connect (LrgEditorShaderGraphEditor  *self,
                                                 const gchar                 *from_node,
                                                 const gchar                 *from_port,
                                                 const gchar                 *to_node,
                                                 const gchar                 *to_port,
                                                 GError                     **error);

/**
 * lrg_editor_shader_graph_editor_disconnect:
 * @self: an #LrgEditorShaderGraphEditor
 * @to_node: the target node
 * @to_port: the input whose link goes
 *
 * Returns: %TRUE when a link was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_disconnect (LrgEditorShaderGraphEditor *self,
                                                    const gchar                *to_node,
                                                    const gchar                *to_port);

/**
 * lrg_editor_shader_graph_editor_set_param:
 * @self: an #LrgEditorShaderGraphEditor
 * @id: a node id
 * @key: a string parameter (`name`, `type`, `hint`, `texture`, `mask`,
 *   `blend`, `cull`)
 * @value: (nullable): the value, %NULL to clear
 *
 * Sets a string parameter of a node (one undo step).
 *
 * Returns: %FALSE for an unknown node or a key its type does not take
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_set_param (LrgEditorShaderGraphEditor *self,
                                                   const gchar                *id,
                                                   const gchar                *key,
                                                   const gchar                *value);

/**
 * lrg_editor_shader_graph_editor_set_values:
 * @self: an #LrgEditorShaderGraphEditor
 * @id: a node id
 * @key: a numeric parameter (`default`, `range`, `value`)
 * @values: (nullable) (array length=n_values): the numbers
 * @n_values: 0 (clear) .. 4
 *
 * Sets a numeric parameter of a node (one undo step).
 *
 * Returns: %FALSE for an unknown node, a key its type does not take or
 *   a wrong count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_set_values (LrgEditorShaderGraphEditor *self,
                                                    const gchar                *id,
                                                    const gchar                *key,
                                                    const gfloat               *values,
                                                    guint                       n_values);

/**
 * lrg_editor_shader_graph_editor_set_input:
 * @self: an #LrgEditorShaderGraphEditor
 * @id: a node id
 * @port: an input port
 * @values: (nullable) (array length=n_values): the constant the input
 *   takes while it has no link
 * @n_values: 0 (back to the library default) .. 4
 *
 * Sets the value of an unconnected input (one undo step).
 *
 * Returns: %FALSE for an unknown node or port, a sampler port or too
 *   many values
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_set_input (LrgEditorShaderGraphEditor *self,
                                                   const gchar                *id,
                                                   const gchar                *port,
                                                   const gfloat               *values,
                                                   guint                       n_values);

/**
 * lrg_editor_shader_graph_editor_select_node:
 * @self: an #LrgEditorShaderGraphEditor
 * @id: (nullable): a node id, %NULL to clear the selection
 *
 * Selects a node on the canvas and shows it in the property grid.
 *
 * Returns: %FALSE for an unknown id
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_select_node (LrgEditorShaderGraphEditor *self,
                                                     const gchar                *id);

/**
 * lrg_editor_shader_graph_editor_get_selected:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (nullable): the node shown in the property grid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_shader_graph_editor_get_selected (LrgEditorShaderGraphEditor *self);

/* ==========================================================================
 * Validation, code, preview
 * ========================================================================== */

/**
 * lrg_editor_shader_graph_editor_get_problems:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (transfer none) (element-type LrgShaderGraphProblem): the
 *   problems of the last validation (it runs after every edit)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_shader_graph_editor_get_problems (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_get_glsl:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (transfer none) (nullable): the generated fragment stage
 *   (what the GLSL tab shows), %NULL while the graph is invalid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_shader_graph_editor_get_glsl (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_get_asset:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (transfer none) (nullable): the shader asset generated from
 *   the last valid state of the graph (the preview draws it, so an
 *   invalid edit keeps showing the last good shader), %NULL when the
 *   graph was never valid
 */
LRG_AVAILABLE_IN_ALL
LrgShaderAsset * lrg_editor_shader_graph_editor_get_asset (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_get_shader_path:
 * @self: an #LrgEditorShaderGraphEditor
 *
 * Returns: (transfer full) (nullable): where saving writes the
 *   generated `.rshader` (the graph's path with the extension
 *   replaced), %NULL for an untitled graph
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_shader_graph_editor_get_shader_path (LrgEditorShaderGraphEditor *self);

/**
 * lrg_editor_shader_graph_editor_export:
 * @self: an #LrgEditorShaderGraphEditor
 * @path: (nullable): the `.rshader` to write, %NULL for
 *   lrg_editor_shader_graph_editor_get_shader_path()
 * @error: return location for a #GError
 *
 * Generates the shader asset from the graph and writes it (saving the
 * graph does this too when the graph is valid).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_shader_graph_editor_export (LrgEditorShaderGraphEditor  *self,
                                                const gchar                 *path,
                                                GError                     **error);

/**
 * lrg_editor_shader_graph_editor_render_preview:
 * @self: an #LrgEditorShaderGraphEditor
 * @size: the edge of the square image in pixels
 *
 * Renders the preview sphere with the generated shader. Needs a window;
 * call it outside BeginDrawing().
 *
 * Returns: (transfer full) (nullable): the image, or %NULL without a
 *   graphics context
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_shader_graph_editor_render_preview (LrgEditorShaderGraphEditor *self,
                                                          guint                       size);

G_END_DECLS
