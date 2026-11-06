/* lrg-editor-node-model.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeModel: the node tree of a level as GListModels.
 *
 * The model presents an #LrgLevel's #LrgNode tree the way #LrgTreeView
 * (and any other GListModel consumer) wants it: one #GListStore of
 * children per parent node, the level root's store being the top level.
 * The stores are created lazily (when a view first asks for a node's
 * children) and are kept in sync incrementally: #LrgLevel::node-added,
 * #LrgLevel::node-removed and #LrgLevel::node-changed, property
 * notifications of the shown nodes (name, visibility, lock, visual,
 * prefab link) and, for re-parenting and reordering (which the level
 * does not signal), #LrgEditor::changed whenever the node tree serial
 * moved. #LrgEditor::level-replaced swaps everything for the new level.
 *
 * A filter (a case-insensitive substring of the name, or of the GUID)
 * narrows every store to the matching nodes and their ancestors.
 *
 * The model is headless: it needs no window, canvas or editor UI.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../ui/lrg-tree-view.h"
#include "lrg-editor.h"
#include "lrg-level.h"
#include "lrg-node.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_NODE_MODEL (lrg_editor_node_model_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorNodeModel, lrg_editor_node_model, LRG, EDITOR_NODE_MODEL, GObject)

/**
 * lrg_editor_node_model_new:
 * @editor: (nullable): the editor whose level to present, or %NULL for a
 *   model without a level (see lrg_editor_node_model_set_level())
 *
 * Creates a model over the editor's current level. It follows the
 * editor: a replaced level (new, open, reload) swaps the stores (see
 * #LrgEditorNodeModel:root-model), and every #LrgEditor::changed after a
 * structural edit reconciles the stores with the tree.
 *
 * Returns: (transfer full): a new #LrgEditorNodeModel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeModel * lrg_editor_node_model_new (LrgEditor *editor);

/**
 * lrg_editor_node_model_new_for_level:
 * @level: the level to present
 *
 * Creates a model over a level without an editor. Additions, removals and
 * changes the level signals are followed; after re-parenting or
 * reordering nodes directly call lrg_editor_node_model_sync().
 *
 * Returns: (transfer full): a new #LrgEditorNodeModel
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNodeModel * lrg_editor_node_model_new_for_level (LrgLevel *level);

/**
 * lrg_editor_node_model_get_editor:
 * @self: an #LrgEditorNodeModel
 *
 * Returns: (transfer none) (nullable): the editor the model follows
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_node_model_get_editor (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_get_level:
 * @self: an #LrgEditorNodeModel
 *
 * Returns: (transfer none) (nullable): the level presented
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_editor_node_model_get_level (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_set_level:
 * @self: an #LrgEditorNodeModel
 * @level: (nullable): the level to present
 *
 * Presents another level. Every store is dropped and a new
 * #LrgEditorNodeModel:root-model is made (views must call
 * lrg_tree_view_set_model() again; lrg_editor_node_model_bind_tree_view()
 * does that for the views it bound). A model made with an editor calls
 * this itself on #LrgEditor::level-replaced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_model_set_level (LrgEditorNodeModel *self,
                                      LrgLevel           *level);

/**
 * lrg_editor_node_model_get_root_model:
 * @self: an #LrgEditorNodeModel
 *
 * The top level: the shown children of the level root, a #GListStore
 * of #LrgNode. The object changes only when the level is replaced
 * (#GObject::notify for #LrgEditorNodeModel:root-model).
 *
 * Returns: (transfer none): the root model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_node_model_get_root_model (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_get_children:
 * @self: an #LrgEditorNodeModel
 * @node: (nullable): a node of the level, or %NULL (or the level root)
 *   for the top level
 *
 * The shown children of @node as a #GListStore of #LrgNode, created on
 * the first call and kept in sync from then on (also while @node is
 * detached from the level, e.g. deleted and waiting in the undo
 * history). The same object is returned for @node until the level is
 * replaced. A leaf gets an empty store, which fills when children are
 * added.
 *
 * Returns: (transfer none): the children model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_node_model_get_children (LrgEditorNodeModel *self,
                                                 LrgNode            *node);

/**
 * lrg_editor_node_model_get_parent:
 * @self: an #LrgEditorNodeModel
 * @node: a node of the level
 *
 * The inverse of lrg_editor_node_model_get_children(), for
 * lrg_tree_view_set_parent_func(): the node's parent, or %NULL for a
 * top-level node (a child of the level root) and for a detached node.
 *
 * Returns: (transfer none) (nullable): the parent of @node
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_node_model_get_parent (LrgEditorNodeModel *self,
                                            LrgNode            *node);

/**
 * lrg_editor_node_model_get_position:
 * @self: an #LrgEditorNodeModel
 * @node: a node of the level
 * @position: (out) (optional): the index of @node in its parent's store
 *
 * Returns: (transfer none) (nullable): the store currently showing
 *   @node, or %NULL when @node is filtered out, detached or its parent's
 *   children were never asked for
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_editor_node_model_get_position (LrgEditorNodeModel *self,
                                                 LrgNode            *node,
                                                 guint              *position);

/**
 * lrg_editor_node_model_set_filter:
 * @self: an #LrgEditorNodeModel
 * @filter: (nullable): a case-insensitive substring of node names or
 *   GUIDs, or %NULL / "" to show everything
 *
 * Narrows every store to the nodes that match @filter and their
 * ancestors (so a match is reachable from the top level). The filter is
 * re-applied as nodes are added, removed, renamed or moved.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_model_set_filter (LrgEditorNodeModel *self,
                                       const gchar        *filter);

/**
 * lrg_editor_node_model_get_filter:
 * @self: an #LrgEditorNodeModel
 *
 * Returns: (transfer none) (nullable): the filter text, %NULL when none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_node_model_get_filter (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_is_match:
 * @self: an #LrgEditorNodeModel
 * @node: a node
 *
 * Whether @node itself matches the filter (as opposed to being shown
 * only as the ancestor of a match). Every node matches while there is
 * no filter.
 *
 * Returns: %TRUE if @node matches
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_model_is_match (LrgEditorNodeModel *self,
                                         LrgNode            *node);

/**
 * lrg_editor_node_model_is_shown:
 * @self: an #LrgEditorNodeModel
 * @node: a node
 *
 * Returns: %TRUE if @node passes the filter (a match or an ancestor of
 *   one); always %TRUE for attached nodes without a filter
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_model_is_shown (LrgEditorNodeModel *self,
                                         LrgNode            *node);

/**
 * lrg_editor_node_model_get_n_matches:
 * @self: an #LrgEditorNodeModel
 *
 * Returns: the number of nodes matching the filter, or the number of
 *   nodes in the level when there is no filter
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_node_model_get_n_matches (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_sync:
 * @self: an #LrgEditorNodeModel
 *
 * Reconciles every store with the node tree now (re-applying the
 * filter). Each store changes by the smallest splice, so views keep
 * their rows, expansion and selection. Models made with an editor do
 * this on #LrgEditor::changed when the tree changed; call it yourself
 * after moving nodes outside the editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_model_sync (LrgEditorNodeModel *self);

/**
 * lrg_editor_node_model_bind_tree_view:
 * @self: an #LrgEditorNodeModel
 * @view: a tree view
 *
 * Shows the model in @view: sets the root model, a children function
 * (lrg_editor_node_model_get_children()) and a parent function
 * (lrg_editor_node_model_get_parent(), so lrg_tree_view_reveal() opens
 * exactly the ancestors of a node), and keeps the root model current
 * when the level is replaced. The row factory is left to the caller.
 * The view holds a reference on the model until it is given other
 * children and parent functions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_model_bind_tree_view (LrgEditorNodeModel *self,
                                           LrgTreeView        *view);

G_END_DECLS
