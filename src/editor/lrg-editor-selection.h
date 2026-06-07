/* lrg-editor-selection.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The set of nodes currently selected in an #LrgEditor.
 *
 * LrgEditorSelection holds an ordered set of selected #LrgNode objects with a
 * distinguished "primary" selection (the most recently added). It emits
 * #LrgEditorSelection::changed whenever the set changes so panels can refresh.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_SELECTION (lrg_editor_selection_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSelection, lrg_editor_selection, LRG, EDITOR_SELECTION, GObject)

/**
 * lrg_editor_selection_new:
 *
 * Returns: (transfer full): a new, empty #LrgEditorSelection
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSelection * lrg_editor_selection_new (void);

/**
 * lrg_editor_selection_add:
 * @self: an #LrgEditorSelection
 * @node: the node to select
 *
 * Adds @node to the selection and makes it the primary. No-op if already
 * present (but still promotes it to primary).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_selection_add (LrgEditorSelection *self,
                               LrgNode            *node);

/**
 * lrg_editor_selection_set:
 * @self: an #LrgEditorSelection
 * @node: (nullable): the sole node to select, or %NULL to clear
 *
 * Replaces the selection with just @node (or clears it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_selection_set (LrgEditorSelection *self,
                               LrgNode            *node);

/**
 * lrg_editor_selection_remove:
 * @self: an #LrgEditorSelection
 * @node: the node to deselect
 *
 * Returns: %TRUE if @node was selected and is now removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_selection_remove (LrgEditorSelection *self,
                                      LrgNode            *node);

/**
 * lrg_editor_selection_clear:
 * @self: an #LrgEditorSelection
 *
 * Clears the selection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_selection_clear (LrgEditorSelection *self);

/**
 * lrg_editor_selection_contains:
 * @self: an #LrgEditorSelection
 * @node: the node to test
 *
 * Returns: %TRUE if @node is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_selection_contains (LrgEditorSelection *self,
                                        LrgNode            *node);

/**
 * lrg_editor_selection_get_primary:
 * @self: an #LrgEditorSelection
 *
 * Returns: (transfer none) (nullable): the primary selected node, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_selection_get_primary (LrgEditorSelection *self);

/**
 * lrg_editor_selection_get_nodes:
 * @self: an #LrgEditorSelection
 *
 * Returns: (transfer none) (element-type LrgNode): the selected nodes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_selection_get_nodes (LrgEditorSelection *self);

/**
 * lrg_editor_selection_get_count:
 * @self: an #LrgEditorSelection
 *
 * Returns: the number of selected nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_selection_get_count (LrgEditorSelection *self);

G_END_DECLS
