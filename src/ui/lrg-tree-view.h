/* lrg-tree-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTreeView: A virtualized expandable tree of rows.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TREE_VIEW (lrg_tree_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTreeView, lrg_tree_view, LRG, TREE_VIEW, LrgContainer)

/**
 * LRG_TREE_VIEW_INVALID_POSITION:
 *
 * Returned by lrg_tree_view_get_position() for items that are not
 * visible (a collapsed ancestor, or not in the tree).
 */
#define LRG_TREE_VIEW_INVALID_POSITION (G_MAXUINT)

/**
 * LrgTreeViewChildrenFunc:
 * @self: the tree view
 * @item: an item of the tree
 * @user_data: data given to lrg_tree_view_set_children_func()
 *
 * Returns the children of @item. Called once per item, the first time
 * the item is shown; the result is cached (and followed through its
 * #GListModel::items-changed signal) until the root model changes.
 *
 * Returns: (transfer full) (nullable): the children of @item, or %NULL
 *   for a leaf
 */
typedef GListModel * (*LrgTreeViewChildrenFunc) (LrgTreeView *self,
                                                 GObject     *item,
                                                 gpointer     user_data);

/**
 * LrgTreeViewCreateFunc:
 * @self: the tree view
 * @user_data: data given to lrg_tree_view_set_factory()
 *
 * Creates an empty row widget. Rows are recycled.
 *
 * Returns: (transfer full): a new row widget
 */
typedef LrgWidget * (*LrgTreeViewCreateFunc) (LrgTreeView *self,
                                              gpointer     user_data);

/**
 * LrgTreeViewBindFunc:
 * @self: the tree view
 * @row: a row made by the #LrgTreeViewCreateFunc
 * @item: the item to show
 * @depth: nesting depth of @item (0 for root items)
 * @user_data: data given to lrg_tree_view_set_factory()
 *
 * Fills @row for @item (text, icon, ...). The disclosure arrow and the
 * indentation are drawn by the view; @row only covers the rest.
 */
typedef void (*LrgTreeViewBindFunc) (LrgTreeView *self,
                                     LrgWidget   *row,
                                     GObject     *item,
                                     guint        depth,
                                     gpointer     user_data);

/**
 * lrg_tree_view_new:
 *
 * Creates an empty tree view with the default row factory (a label
 * with the item's "string", "text", "name", "label" or "title"
 * property) and single selection.
 *
 * Returns: (transfer full): a new #LrgTreeView
 */
LRG_AVAILABLE_IN_ALL
LrgTreeView * lrg_tree_view_new (void);

/**
 * lrg_tree_view_set_model:
 * @self: an #LrgTreeView
 * @model: (nullable): the root items
 *
 * Shows @model as the top level. Expansion state and the selection
 * are reset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_model (LrgTreeView *self,
                              GListModel  *model);

/**
 * lrg_tree_view_get_model:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the root model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_tree_view_get_model (LrgTreeView *self);

/**
 * lrg_tree_view_set_children_func:
 * @self: an #LrgTreeView
 * @func: (nullable) (scope notified) (closure user_data): returns the
 *   children of an item, or %NULL for a flat list
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Sets how the hierarchy is discovered. Cached children are dropped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_children_func (LrgTreeView             *self,
                                      LrgTreeViewChildrenFunc  func,
                                      gpointer                 user_data,
                                      GDestroyNotify           destroy);

/**
 * lrg_tree_view_set_factory:
 * @self: an #LrgTreeView
 * @create: (nullable) (scope notified) (closure user_data): makes row
 *   widgets, or %NULL for labels
 * @bind: (nullable) (scope notified) (closure user_data): fills a row,
 *   or %NULL for the default
 * @user_data: data for both functions
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Sets how rows are made (e.g. an icon and a label in an #LrgHBox).
 * Existing rows are rebuilt.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_factory (LrgTreeView           *self,
                                LrgTreeViewCreateFunc  create,
                                LrgTreeViewBindFunc    bind,
                                gpointer               user_data,
                                GDestroyNotify         destroy);

/**
 * lrg_tree_view_get_row_height:
 * @self: an #LrgTreeView
 *
 * Returns: the configured row height (0 = the theme's ROW_HEIGHT)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tree_view_get_row_height (LrgTreeView *self);

/**
 * lrg_tree_view_set_row_height:
 * @self: an #LrgTreeView
 * @row_height: height of every row, or 0 for the theme's ROW_HEIGHT
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_row_height (LrgTreeView *self,
                                   gfloat       row_height);

/**
 * lrg_tree_view_get_indent:
 * @self: an #LrgTreeView
 *
 * Returns: horizontal indentation per depth level
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tree_view_get_indent (LrgTreeView *self);

/**
 * lrg_tree_view_set_indent:
 * @self: an #LrgTreeView
 * @indent: indentation per level (default 18)
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_indent (LrgTreeView *self,
                               gfloat       indent);

/**
 * lrg_tree_view_get_selection_mode:
 * @self: an #LrgTreeView
 *
 * Returns: how many items can be selected
 */
LRG_AVAILABLE_IN_ALL
LrgSelectionMode lrg_tree_view_get_selection_mode (LrgTreeView *self);

/**
 * lrg_tree_view_set_selection_mode:
 * @self: an #LrgTreeView
 * @mode: none, single (the default) or multiple (Ctrl click)
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_selection_mode (LrgTreeView      *self,
                                       LrgSelectionMode  mode);

/**
 * lrg_tree_view_expand:
 * @self: an #LrgTreeView
 * @item: an item with children
 *
 * Expands @item (its rows appear below it; the arrow turns with a
 * spring). Does nothing for leaves.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_expand (LrgTreeView *self,
                           GObject     *item);

/**
 * lrg_tree_view_collapse:
 * @self: an #LrgTreeView
 * @item: an item
 *
 * Collapses @item. Expanded descendants stay expanded and reappear
 * that way.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_collapse (LrgTreeView *self,
                             GObject     *item);

/**
 * lrg_tree_view_is_expanded:
 * @self: an #LrgTreeView
 * @item: an item
 *
 * Returns: %TRUE if @item is expanded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_is_expanded (LrgTreeView *self,
                                    GObject     *item);

/**
 * lrg_tree_view_collapse_all:
 * @self: an #LrgTreeView
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_collapse_all (LrgTreeView *self);

/**
 * lrg_tree_view_get_n_visible:
 * @self: an #LrgTreeView
 *
 * Returns: number of rows in the flattened tree (root items plus the
 *   descendants of expanded items)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tree_view_get_n_visible (LrgTreeView *self);

/**
 * lrg_tree_view_get_item:
 * @self: an #LrgTreeView
 * @position: a row of the flattened tree
 *
 * Returns: (transfer none) (nullable): the item shown on that row
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_item (LrgTreeView *self,
                                  guint        position);

/**
 * lrg_tree_view_get_depth:
 * @self: an #LrgTreeView
 * @position: a row of the flattened tree
 *
 * Returns: nesting depth of the row (0 for root items)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tree_view_get_depth (LrgTreeView *self,
                               guint        position);

/**
 * lrg_tree_view_get_position:
 * @self: an #LrgTreeView
 * @item: an item
 *
 * Returns: the row showing @item, or %LRG_TREE_VIEW_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tree_view_get_position (LrgTreeView *self,
                                  GObject     *item);

/**
 * lrg_tree_view_select_item:
 * @self: an #LrgTreeView
 * @item: a visible item
 *
 * Selects @item (replacing the selection in single mode) and moves the
 * cursor there.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_select_item (LrgTreeView *self,
                                GObject     *item);

/**
 * lrg_tree_view_unselect_all:
 * @self: an #LrgTreeView
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_unselect_all (LrgTreeView *self);

/**
 * lrg_tree_view_get_selected_item:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the cursor item if selected,
 *   else any selected item, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_selected_item (LrgTreeView *self);

/**
 * lrg_tree_view_is_item_selected:
 * @self: an #LrgTreeView
 * @item: an item
 *
 * Returns: %TRUE if @item is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_is_item_selected (LrgTreeView *self,
                                         GObject     *item);

/**
 * lrg_tree_view_get_n_selected:
 * @self: an #LrgTreeView
 *
 * Returns: number of selected items
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tree_view_get_n_selected (LrgTreeView *self);

/**
 * lrg_tree_view_get_cursor_item:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the keyboard cursor item
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_cursor_item (LrgTreeView *self);

/**
 * lrg_tree_view_scroll_to:
 * @self: an #LrgTreeView
 * @position: a row of the flattened tree
 * @animate: glide instead of jumping
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_scroll_to (LrgTreeView *self,
                              guint        position,
                              gboolean     animate);

/**
 * lrg_tree_view_get_scroll_offset:
 * @self: an #LrgTreeView
 *
 * Returns: the vertical offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tree_view_get_scroll_offset (LrgTreeView *self);

/**
 * lrg_tree_view_get_row_widget:
 * @self: an #LrgTreeView
 * @position: a row of the flattened tree
 *
 * Returns: (transfer none) (nullable): the realized row widget, or
 *   %NULL when the row is scrolled away
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_tree_view_get_row_widget (LrgTreeView *self,
                                          guint        position);

/**
 * lrg_tree_view_get_n_rows:
 * @self: an #LrgTreeView
 *
 * Returns: number of realized row widgets, independent of the tree size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tree_view_get_n_rows (LrgTreeView *self);

G_END_DECLS
