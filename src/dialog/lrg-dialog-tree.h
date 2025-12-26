/* lrg-dialog-tree.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dialog tree containing interconnected dialog nodes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "dialog/lrg-dialog-node.h"

G_BEGIN_DECLS

#define LRG_TYPE_DIALOG_TREE (lrg_dialog_tree_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_FINAL_TYPE (LrgDialogTree, lrg_dialog_tree, LRG, DIALOG_TREE, GObject)

/**
 * lrg_dialog_tree_new:
 * @id: unique identifier for the tree
 *
 * Creates a new dialog tree.
 *
 * Returns: (transfer full): A new #LrgDialogTree
 */
LrgDialogTree *lrg_dialog_tree_new            (const gchar   *id);

/**
 * lrg_dialog_tree_get_id:
 * @self: an #LrgDialogTree
 *
 * Gets the tree identifier.
 *
 * Returns: (transfer none): The tree ID
 */
const gchar   *lrg_dialog_tree_get_id         (LrgDialogTree *self);

/**
 * lrg_dialog_tree_get_start_node_id:
 * @self: an #LrgDialogTree
 *
 * Gets the starting node ID.
 *
 * Returns: (transfer none) (nullable): The start node ID
 */
const gchar   *lrg_dialog_tree_get_start_node_id (LrgDialogTree *self);

/**
 * lrg_dialog_tree_set_start_node_id:
 * @self: an #LrgDialogTree
 * @start_node_id: (nullable): start node ID
 *
 * Sets the starting node ID.
 */
void           lrg_dialog_tree_set_start_node_id (LrgDialogTree *self,
                                                  const gchar   *start_node_id);

/**
 * lrg_dialog_tree_add_node:
 * @self: an #LrgDialogTree
 * @node: (transfer full): node to add
 *
 * Adds a node to the tree.
 *
 * If a node with the same ID already exists, it will be replaced.
 */
void           lrg_dialog_tree_add_node       (LrgDialogTree *self,
                                               LrgDialogNode *node);

/**
 * lrg_dialog_tree_get_node:
 * @self: an #LrgDialogTree
 * @node_id: node identifier
 *
 * Gets a node by ID.
 *
 * Returns: (transfer none) (nullable): The node, or %NULL if not found
 */
LrgDialogNode *lrg_dialog_tree_get_node       (LrgDialogTree *self,
                                               const gchar   *node_id);

/**
 * lrg_dialog_tree_get_start_node:
 * @self: an #LrgDialogTree
 *
 * Gets the starting node.
 *
 * Returns: (transfer none) (nullable): The start node, or %NULL
 */
LrgDialogNode *lrg_dialog_tree_get_start_node (LrgDialogTree *self);

/**
 * lrg_dialog_tree_remove_node:
 * @self: an #LrgDialogTree
 * @node_id: node identifier
 *
 * Removes a node from the tree.
 *
 * Returns: %TRUE if the node was removed
 */
gboolean       lrg_dialog_tree_remove_node    (LrgDialogTree *self,
                                               const gchar   *node_id);

/**
 * lrg_dialog_tree_get_node_count:
 * @self: an #LrgDialogTree
 *
 * Gets the number of nodes in the tree.
 *
 * Returns: Node count
 */
guint          lrg_dialog_tree_get_node_count (LrgDialogTree *self);

/**
 * lrg_dialog_tree_get_node_ids:
 * @self: an #LrgDialogTree
 *
 * Gets all node IDs in the tree.
 *
 * Returns: (transfer container) (element-type utf8): List of node IDs
 */
GList         *lrg_dialog_tree_get_node_ids   (LrgDialogTree *self);

/**
 * lrg_dialog_tree_get_title:
 * @self: an #LrgDialogTree
 *
 * Gets the tree title.
 *
 * Returns: (transfer none) (nullable): The title
 */
const gchar   *lrg_dialog_tree_get_title      (LrgDialogTree *self);

/**
 * lrg_dialog_tree_set_title:
 * @self: an #LrgDialogTree
 * @title: (nullable): tree title
 *
 * Sets the tree title.
 */
void           lrg_dialog_tree_set_title      (LrgDialogTree *self,
                                               const gchar   *title);

/**
 * lrg_dialog_tree_get_description:
 * @self: an #LrgDialogTree
 *
 * Gets the tree description.
 *
 * Returns: (transfer none) (nullable): The description
 */
const gchar   *lrg_dialog_tree_get_description (LrgDialogTree *self);

/**
 * lrg_dialog_tree_set_description:
 * @self: an #LrgDialogTree
 * @description: (nullable): tree description
 *
 * Sets the tree description.
 */
void           lrg_dialog_tree_set_description (LrgDialogTree *self,
                                                const gchar   *description);

/**
 * lrg_dialog_tree_validate:
 * @self: an #LrgDialogTree
 * @error: (nullable): return location for error
 *
 * Validates the dialog tree structure.
 *
 * Checks that all node references are valid and there are no orphan nodes.
 *
 * Returns: %TRUE if the tree is valid
 */
gboolean       lrg_dialog_tree_validate       (LrgDialogTree  *self,
                                               GError        **error);

#pragma GCC visibility pop

G_END_DECLS
