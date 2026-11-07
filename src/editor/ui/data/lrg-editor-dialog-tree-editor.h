/* lrg-editor-dialog-tree-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDialogTreeEditor: the data editor of #LrgDialogTree files.
 *
 * The tree is drawn as an #LrgNodeGraph: one card per dialog node (id in
 * the header, speaker as the category, a preview of the text as the body,
 * the start node tinted), an input port "in" and one output port per
 * response ("1  Friend", ...) or a single "next" output for a node without
 * responses. Links are the `next` references; the loader's implicit chain
 * (a node with neither `next` nor responses continues with the following
 * node) is drawn dimmed, and a reference to a node that does not exist
 * ends at a red "missing" card.
 *
 * Editing the graph edits the YAML through the #LrgEditorDocument:
 * connecting a port writes its `next`, disconnecting removes it, moving
 * cards writes their position under an editor-only `editor: {x, y}` key
 * of the node (the dialog loader ignores it), deleting a card removes the
 * node. Each gesture is one undo step. The property grid edits the
 * selected node (id with every reference renamed along, speaker, text,
 * next and each response's text and next) or, with nothing selected, the
 * tree's id, title and start node.
 *
 * The play-test pane runs the document through the engine's own
 * #LrgDialogRunner: it shows the speaker and text, offers the responses as
 * buttons (responses whose conditions fail are disabled), follows `next`
 * with Continue, ends on a terminal node with End, highlights the current
 * node in the graph and keeps a short transcript.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../../lrg-version.h"
#include "../../../ui/lrg-node-graph.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_DIALOG_TREE_EDITOR (lrg_editor_dialog_tree_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDialogTreeEditor, lrg_editor_dialog_tree_editor, LRG, EDITOR_DIALOG_TREE_EDITOR, LrgEditorDataEditor)

/**
 * lrg_editor_dialog_tree_editor_new:
 * @app: (nullable): the editor application
 * @document: a dialog tree document
 *
 * Creates the editor of @document. lrg_editor_data_editor_open() is the
 * usual way to get one; this is for embedding and tests.
 *
 * Returns: (transfer full): a new #LrgEditorDialogTreeEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDialogTreeEditor * lrg_editor_dialog_tree_editor_new (LrgEditorApp      *app,
                                                               LrgEditorDocument *document);

/**
 * lrg_editor_dialog_tree_editor_get_graph:
 * @self: an #LrgEditorDialogTreeEditor
 *
 * Returns: (transfer none): the node graph (card ids are the dialog node
 *   ids; a missing target's card is "missing:<id>")
 */
LRG_AVAILABLE_IN_ALL
LrgNodeGraph * lrg_editor_dialog_tree_editor_get_graph (LrgEditorDialogTreeEditor *self);

/**
 * lrg_editor_dialog_tree_editor_get_selected:
 * @self: an #LrgEditorDialogTreeEditor
 *
 * Returns: (nullable): the id of the dialog node shown in the property
 *   grid, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_dialog_tree_editor_get_selected (LrgEditorDialogTreeEditor *self);

/**
 * lrg_editor_dialog_tree_editor_select:
 * @self: an #LrgEditorDialogTreeEditor
 * @node_id: (nullable): a dialog node id, %NULL for the tree itself
 *
 * Returns: %TRUE when @node_id is a node of the document (or %NULL)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_select (LrgEditorDialogTreeEditor *self,
                                               const gchar               *node_id);

/**
 * lrg_editor_dialog_tree_editor_add_node:
 * @self: an #LrgEditorDialogTreeEditor
 * @node_id: (nullable): the id, %NULL for a free "node-N"
 * @x: card position in graph units
 * @y: card position in graph units
 * @error: return location for a #GError
 *
 * Appends a node with placeholder text and selects it (one undo step).
 *
 * Returns: (transfer full) (nullable): the new node's id, %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_dialog_tree_editor_add_node (LrgEditorDialogTreeEditor  *self,
                                                const gchar                *node_id,
                                                gfloat                      x,
                                                gfloat                      y,
                                                GError                    **error);

/**
 * lrg_editor_dialog_tree_editor_add_response:
 * @self: an #LrgEditorDialogTreeEditor
 * @node_id: a dialog node
 * @text: the response text
 * @next: (nullable): its target node id, %NULL for none yet
 * @error: return location for a #GError
 *
 * Appends a response to a node (one undo step). A node that had a `next`
 * keeps it as the new response's target when @next is %NULL.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_add_response (LrgEditorDialogTreeEditor  *self,
                                                     const gchar                *node_id,
                                                     const gchar                *text,
                                                     const gchar                *next,
                                                     GError                    **error);

/**
 * lrg_editor_dialog_tree_editor_link:
 * @self: an #LrgEditorDialogTreeEditor
 * @from: the source node id
 * @response: the response index, or -1 for the node's own `next`
 * @to: (nullable): the target node id, %NULL to remove the link
 * @error: return location for a #GError
 *
 * Writes (or removes) one `next` reference, exactly what wiring the ports
 * in the graph does.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_link (LrgEditorDialogTreeEditor  *self,
                                             const gchar                *from,
                                             gint                        response,
                                             const gchar                *to,
                                             GError                    **error);

/**
 * lrg_editor_dialog_tree_editor_remove_node:
 * @self: an #LrgEditorDialogTreeEditor
 * @node_id: a dialog node
 * @error: return location for a #GError
 *
 * Removes a node (references to it stay and show as missing).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_remove_node (LrgEditorDialogTreeEditor  *self,
                                                    const gchar                *node_id,
                                                    GError                    **error);

/**
 * lrg_editor_dialog_tree_editor_flush:
 * @self: an #LrgEditorDialogTreeEditor
 *
 * Writes the graph edits that wait for the end of the current main-loop
 * iteration (wiring, moves, deletes made in the graph) now.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_dialog_tree_editor_flush (LrgEditorDialogTreeEditor *self);

/**
 * lrg_editor_dialog_tree_editor_play:
 * @self: an #LrgEditorDialogTreeEditor
 * @node_id: (nullable): the node to start at, %NULL for the start node
 * @error: return location for a #GError
 *
 * Starts the play test from the document as it is now.
 *
 * Returns: %TRUE when the dialog started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_play (LrgEditorDialogTreeEditor  *self,
                                             const gchar                *node_id,
                                             GError                    **error);

/**
 * lrg_editor_dialog_tree_editor_play_choose:
 * @self: an #LrgEditorDialogTreeEditor
 * @index: a response index of the current node, or -1 to continue a node
 *   without responses
 *
 * Does what the play-test pane's buttons do.
 *
 * Returns: %TRUE when the dialog moved on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_dialog_tree_editor_play_choose (LrgEditorDialogTreeEditor *self,
                                                    gint                       index);

/**
 * lrg_editor_dialog_tree_editor_get_play_node:
 * @self: an #LrgEditorDialogTreeEditor
 *
 * The last node of a walk stays current (shown with an End button) after
 * the engine's runner finished on it.
 *
 * Returns: (nullable): the id of the node the play test shows, %NULL
 *   before Play and after End
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_dialog_tree_editor_get_play_node (LrgEditorDialogTreeEditor *self);

G_END_DECLS
