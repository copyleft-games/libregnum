/* lrg-dialog-runner.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dialog runner for managing conversation flow.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "dialog/lrg-dialog-tree.h"
#include "dialog/lrg-dialog-node.h"
#include "dialog/lrg-dialog-response.h"

G_BEGIN_DECLS

#define LRG_TYPE_DIALOG_RUNNER (lrg_dialog_runner_get_type ())

#pragma GCC visibility push(default)

G_DECLARE_FINAL_TYPE (LrgDialogRunner, lrg_dialog_runner, LRG, DIALOG_RUNNER, GObject)

/**
 * lrg_dialog_runner_new:
 *
 * Creates a new dialog runner.
 *
 * Returns: (transfer full): A new #LrgDialogRunner
 */
LrgDialogRunner   *lrg_dialog_runner_new              (void);

/**
 * lrg_dialog_runner_get_tree:
 * @self: an #LrgDialogRunner
 *
 * Gets the current dialog tree.
 *
 * Returns: (transfer none) (nullable): The current tree
 */
LrgDialogTree     *lrg_dialog_runner_get_tree         (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_set_tree:
 * @self: an #LrgDialogRunner
 * @tree: (nullable): dialog tree to set
 *
 * Sets the dialog tree for this runner.
 */
void               lrg_dialog_runner_set_tree         (LrgDialogRunner *self,
                                                       LrgDialogTree   *tree);

/**
 * lrg_dialog_runner_get_current_node:
 * @self: an #LrgDialogRunner
 *
 * Gets the current dialog node.
 *
 * Returns: (transfer none) (nullable): The current node
 */
LrgDialogNode     *lrg_dialog_runner_get_current_node (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_start:
 * @self: an #LrgDialogRunner
 * @error: (nullable): return location for error
 *
 * Starts the dialog from the tree's start node.
 *
 * Returns: %TRUE on success
 */
gboolean           lrg_dialog_runner_start            (LrgDialogRunner  *self,
                                                       GError          **error);

/**
 * lrg_dialog_runner_start_at:
 * @self: an #LrgDialogRunner
 * @node_id: node ID to start at
 * @error: (nullable): return location for error
 *
 * Starts the dialog at a specific node.
 *
 * Returns: %TRUE on success
 */
gboolean           lrg_dialog_runner_start_at         (LrgDialogRunner  *self,
                                                       const gchar      *node_id,
                                                       GError          **error);

/**
 * lrg_dialog_runner_advance:
 * @self: an #LrgDialogRunner
 * @error: (nullable): return location for error
 *
 * Advances to the next node if auto-advance is set.
 *
 * Returns: %TRUE if advanced, %FALSE if at a choice or terminal node
 */
gboolean           lrg_dialog_runner_advance          (LrgDialogRunner  *self,
                                                       GError          **error);

/**
 * lrg_dialog_runner_select_response:
 * @self: an #LrgDialogRunner
 * @index: response index
 * @error: (nullable): return location for error
 *
 * Selects a response by index and advances to its target node.
 *
 * Returns: %TRUE on success
 */
gboolean           lrg_dialog_runner_select_response  (LrgDialogRunner  *self,
                                                       guint             index,
                                                       GError          **error);

/**
 * lrg_dialog_runner_is_active:
 * @self: an #LrgDialogRunner
 *
 * Checks if a dialog is currently active.
 *
 * Returns: %TRUE if active
 */
gboolean           lrg_dialog_runner_is_active        (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_is_at_choice:
 * @self: an #LrgDialogRunner
 *
 * Checks if at a node that requires a response selection.
 *
 * Returns: %TRUE if at a choice node
 */
gboolean           lrg_dialog_runner_is_at_choice     (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_stop:
 * @self: an #LrgDialogRunner
 *
 * Stops the current dialog.
 */
void               lrg_dialog_runner_stop             (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_get_available_responses:
 * @self: an #LrgDialogRunner
 *
 * Gets available responses for the current node.
 *
 * Only returns responses whose conditions pass.
 *
 * Returns: (transfer container) (element-type LrgDialogResponse): Available responses
 */
GPtrArray         *lrg_dialog_runner_get_available_responses (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_get_context:
 * @self: an #LrgDialogRunner
 *
 * Gets the variable context for conditions and effects.
 *
 * Returns: (transfer none): The context hash table
 */
GHashTable        *lrg_dialog_runner_get_context      (LrgDialogRunner *self);

/**
 * lrg_dialog_runner_set_variable:
 * @self: an #LrgDialogRunner
 * @key: variable name
 * @value: variable value
 *
 * Sets a variable in the context.
 */
void               lrg_dialog_runner_set_variable     (LrgDialogRunner *self,
                                                       const gchar     *key,
                                                       const gchar     *value);

/**
 * lrg_dialog_runner_get_variable:
 * @self: an #LrgDialogRunner
 * @key: variable name
 *
 * Gets a variable from the context.
 *
 * Returns: (transfer none) (nullable): Variable value, or %NULL
 */
const gchar       *lrg_dialog_runner_get_variable     (LrgDialogRunner *self,
                                                       const gchar     *key);

#pragma GCC visibility pop

G_END_DECLS
