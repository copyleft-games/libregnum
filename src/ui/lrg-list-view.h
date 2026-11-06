/* lrg-list-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgListView: A virtualized, scrollable list of rows built from a GListModel.
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

#define LRG_TYPE_LIST_VIEW (lrg_list_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgListView, lrg_list_view, LRG, LIST_VIEW, LrgContainer)

/**
 * LRG_LIST_VIEW_INVALID_POSITION:
 *
 * Returned by lrg_list_view_get_selected() when nothing is selected.
 */
#define LRG_LIST_VIEW_INVALID_POSITION (G_MAXUINT)

/**
 * LrgListViewCreateFunc:
 * @self: the list view
 * @user_data: data given to lrg_list_view_set_factory()
 *
 * Creates an empty row widget. Rows are recycled: the same widget is
 * bound to many items over its life.
 *
 * Returns: (transfer full): a new row widget
 */
typedef LrgWidget * (*LrgListViewCreateFunc) (LrgListView *self,
                                              gpointer     user_data);

/**
 * LrgListViewBindFunc:
 * @self: the list view
 * @row: a row made by the #LrgListViewCreateFunc
 * @item: the model item to show
 * @position: index of @item in the model
 * @user_data: data given to lrg_list_view_set_factory()
 *
 * Fills @row with the contents of @item. Called whenever a row scrolls
 * onto a different item or the item at its position changed.
 */
typedef void (*LrgListViewBindFunc) (LrgListView *self,
                                     LrgWidget   *row,
                                     GObject     *item,
                                     guint        position,
                                     gpointer     user_data);

/**
 * lrg_list_view_new:
 *
 * Creates an empty list view with the default row factory (a label
 * showing each item's "string", "text", "name", "label" or "title"
 * property, else its type name) and single selection.
 *
 * Returns: (transfer full): a new #LrgListView
 */
LRG_AVAILABLE_IN_ALL
LrgListView * lrg_list_view_new (void);

/**
 * lrg_list_view_set_model:
 * @self: an #LrgListView
 * @model: (nullable): the items to show
 *
 * Shows @model. The view follows its #GListModel::items-changed
 * signal; the selection is cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_set_model (LrgListView *self,
                              GListModel  *model);

/**
 * lrg_list_view_get_model:
 * @self: an #LrgListView
 *
 * Returns: (transfer none) (nullable): the model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_list_view_get_model (LrgListView *self);

/**
 * lrg_list_view_set_factory:
 * @self: an #LrgListView
 * @create: (nullable) (scope notified) (closure user_data): makes row
 *   widgets, or %NULL for the default label rows
 * @bind: (nullable) (scope notified) (closure user_data): fills a row
 *   for an item, or %NULL for the default
 * @user_data: data for both functions
 * @destroy: (nullable): frees @user_data when the factory is replaced
 *
 * Sets how rows are made. Existing rows are thrown away and rebuilt.
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_set_factory (LrgListView           *self,
                                LrgListViewCreateFunc  create,
                                LrgListViewBindFunc    bind,
                                gpointer               user_data,
                                GDestroyNotify         destroy);

/**
 * lrg_list_view_get_row_height:
 * @self: an #LrgListView
 *
 * Returns: the configured row height (0 = the theme's ROW_HEIGHT)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_list_view_get_row_height (LrgListView *self);

/**
 * lrg_list_view_set_row_height:
 * @self: an #LrgListView
 * @row_height: height of every row, or 0 to use the theme's ROW_HEIGHT
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_set_row_height (LrgListView *self,
                                   gfloat       row_height);

/**
 * lrg_list_view_get_selection_mode:
 * @self: an #LrgListView
 *
 * Returns: how many rows can be selected
 */
LRG_AVAILABLE_IN_ALL
LrgSelectionMode lrg_list_view_get_selection_mode (LrgListView *self);

/**
 * lrg_list_view_set_selection_mode:
 * @self: an #LrgListView
 * @mode: none, single (the default) or multiple (Ctrl / Shift click)
 *
 * Changing the mode trims the selection to fit it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_set_selection_mode (LrgListView      *self,
                                       LrgSelectionMode  mode);

/**
 * lrg_list_view_get_selected:
 * @self: an #LrgListView
 *
 * Returns: the selected position (the cursor row if it is selected,
 *   otherwise the lowest selected position), or
 *   %LRG_LIST_VIEW_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_list_view_get_selected (LrgListView *self);

/**
 * lrg_list_view_get_n_selected:
 * @self: an #LrgListView
 *
 * Returns: number of selected rows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_list_view_get_n_selected (LrgListView *self);

/**
 * lrg_list_view_is_selected:
 * @self: an #LrgListView
 * @position: a model position
 *
 * Returns: %TRUE if the row at @position is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_list_view_is_selected (LrgListView *self,
                                    guint        position);

/**
 * lrg_list_view_select:
 * @self: an #LrgListView
 * @position: a model position
 *
 * Selects @position and moves the cursor there. In single mode it
 * replaces the selection, in multiple mode it is added; in none mode
 * nothing is selected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_select (LrgListView *self,
                           guint        position);

/**
 * lrg_list_view_unselect:
 * @self: an #LrgListView
 * @position: a model position
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_unselect (LrgListView *self,
                             guint        position);

/**
 * lrg_list_view_unselect_all:
 * @self: an #LrgListView
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_unselect_all (LrgListView *self);

/**
 * lrg_list_view_get_cursor:
 * @self: an #LrgListView
 *
 * Returns: the keyboard cursor row, or %LRG_LIST_VIEW_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_list_view_get_cursor (LrgListView *self);

/**
 * lrg_list_view_scroll_to:
 * @self: an #LrgListView
 * @position: a model position
 * @animate: glide instead of jumping
 *
 * Scrolls by the smallest amount that shows the whole row.
 */
LRG_AVAILABLE_IN_ALL
void lrg_list_view_scroll_to (LrgListView *self,
                              guint        position,
                              gboolean     animate);

/**
 * lrg_list_view_get_scroll_offset:
 * @self: an #LrgListView
 *
 * Returns: the vertical offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_list_view_get_scroll_offset (LrgListView *self);

/**
 * lrg_list_view_get_row_widget:
 * @self: an #LrgListView
 * @position: a model position
 *
 * Returns: (transfer none) (nullable): the row widget currently bound
 *   to @position, or %NULL when that row is not realized (scrolled
 *   away)
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_list_view_get_row_widget (LrgListView *self,
                                          guint        position);

/**
 * lrg_list_view_get_n_rows:
 * @self: an #LrgListView
 *
 * Returns: number of row widgets alive (visible range plus buffer),
 *   independent of the model size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_list_view_get_n_rows (LrgListView *self);

G_END_DECLS
