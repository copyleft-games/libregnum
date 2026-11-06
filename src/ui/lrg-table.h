/* lrg-table.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTable: A virtualized data table with resizable, sortable columns.
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

#define LRG_TYPE_TABLE (lrg_table_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTable, lrg_table, LRG, TABLE, LrgContainer)

/**
 * LRG_TABLE_INVALID_POSITION:
 *
 * Returned by lrg_table_get_selected() and lrg_table_get_cursor() when
 * there is no such row.
 */
#define LRG_TABLE_INVALID_POSITION (G_MAXUINT)

/**
 * LrgTableColumnFlags:
 * @LRG_TABLE_COLUMN_NONE: a fixed, unsortable column
 * @LRG_TABLE_COLUMN_RESIZABLE: the right border of the header cell can
 *   be dragged to change the column width
 * @LRG_TABLE_COLUMN_SORTABLE: clicking the header cell sorts by the
 *   column (emits #LrgTable::sort-changed)
 * @LRG_TABLE_COLUMN_EXPAND: the column shares the width left over when
 *   all columns fit in the viewport
 *
 * Behaviour of one table column.
 */
typedef enum /*< flags >*/
{
	LRG_TABLE_COLUMN_NONE      = 0,
	LRG_TABLE_COLUMN_RESIZABLE = 1 << 0,
	LRG_TABLE_COLUMN_SORTABLE  = 1 << 1,
	LRG_TABLE_COLUMN_EXPAND    = 1 << 2
} LrgTableColumnFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_table_column_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TABLE_COLUMN_FLAGS (lrg_table_column_flags_get_type ())

/**
 * LrgTableCellFunc:
 * @self: the table
 * @cell: (nullable): the column's widget for this row when the column
 *   has a widget factory (lrg_table_set_column_widget_factory()),
 *   else %NULL
 * @item: the model item of the row
 * @position: index of @item in the model
 * @user_data: data given to lrg_table_add_column()
 *
 * Fills one cell. Text columns return the text to draw; widget columns
 * bind @cell to @item and return %NULL. Called only when a row scrolls
 * onto a different item (rows are virtualized and recycled), never per
 * frame.
 *
 * Returns: (transfer full) (nullable): text drawn in the cell, or %NULL
 */
typedef gchar * (*LrgTableCellFunc) (LrgTable  *self,
                                     LrgWidget *cell,
                                     GObject   *item,
                                     guint      position,
                                     gpointer   user_data);

/**
 * LrgTableCellCreateFunc:
 * @self: the table
 * @column: index of the column
 * @user_data: data given to lrg_table_set_column_widget_factory()
 *
 * Creates an empty cell widget for @column. Cells are recycled like
 * rows: the column's #LrgTableCellFunc binds them to items.
 *
 * Returns: (transfer full): a new cell widget
 */
typedef LrgWidget * (*LrgTableCellCreateFunc) (LrgTable *self,
                                               guint     column,
                                               gpointer  user_data);

/**
 * lrg_table_new:
 *
 * Creates an empty table with no columns, single selection and
 * striped rows.
 *
 * Returns: (transfer full): a new #LrgTable
 */
LRG_AVAILABLE_IN_ALL
LrgTable * lrg_table_new (void);

/**
 * lrg_table_set_model:
 * @self: an #LrgTable
 * @model: (nullable): the rows to show
 *
 * Shows @model, one row per item. The table follows its
 * #GListModel::items-changed signal; the selection is cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_model (LrgTable   *self,
                          GListModel *model);

/**
 * lrg_table_get_model:
 * @self: an #LrgTable
 *
 * Returns: (transfer none) (nullable): the model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_table_get_model (LrgTable *self);

/**
 * lrg_table_add_column:
 * @self: an #LrgTable
 * @title: header text
 * @cell_func: (scope notified) (closure user_data): fills the cells of
 *   the column
 * @user_data: data for @cell_func
 * @destroy: (nullable): frees @user_data with the column
 * @width: initial width in canvas units
 * @flags: resizable / sortable / expand
 *
 * Appends a column. The rows are rebuilt.
 *
 * Returns: the index of the new column
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_add_column (LrgTable            *self,
                            const gchar         *title,
                            LrgTableCellFunc     cell_func,
                            gpointer             user_data,
                            GDestroyNotify       destroy,
                            gfloat               width,
                            LrgTableColumnFlags  flags);

/**
 * lrg_table_add_property_column:
 * @self: an #LrgTable
 * @title: header text
 * @property: name of an item property shown as text (any type that
 *   transforms to a string)
 * @width: initial width in canvas units
 * @flags: resizable / sortable / expand
 *
 * Appends a text column showing one property of every item. When the
 * column is sortable and the model is a #GListStore, clicking its
 * header sorts the store by the property (numbers numerically, strings
 * with g_utf8_collate()) unless lrg_table_set_sort_func() replaced
 * that.
 *
 * Returns: the index of the new column
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_add_property_column (LrgTable            *self,
                                     const gchar         *title,
                                     const gchar         *property,
                                     gfloat               width,
                                     LrgTableColumnFlags  flags);

/**
 * lrg_table_set_column_widget_factory:
 * @self: an #LrgTable
 * @column: a column index
 * @create: (nullable) (scope notified) (closure user_data): makes cell
 *   widgets for the column, or %NULL for a text column
 * @user_data: data for @create
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Turns @column into a widget column: every realized row gets a cell
 * widget from @create, bound by the column's #LrgTableCellFunc. The
 * rows are rebuilt.
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_column_widget_factory (LrgTable               *self,
                                          guint                   column,
                                          LrgTableCellCreateFunc  create,
                                          gpointer                user_data,
                                          GDestroyNotify          destroy);

/**
 * lrg_table_get_n_columns:
 * @self: an #LrgTable
 *
 * Returns: the number of columns
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_get_n_columns (LrgTable *self);

/**
 * lrg_table_get_column_title:
 * @self: an #LrgTable
 * @column: a column index
 *
 * Returns: (transfer none) (nullable): the header text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_table_get_column_title (LrgTable *self,
                                          guint     column);

/**
 * lrg_table_set_column_title:
 * @self: an #LrgTable
 * @column: a column index
 * @title: new header text
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_column_title (LrgTable    *self,
                                 guint        column,
                                 const gchar *title);

/**
 * lrg_table_get_column_width:
 * @self: an #LrgTable
 * @column: a column index
 *
 * Returns: the width the column was given (by the application or by
 *   dragging its border), before any %LRG_TABLE_COLUMN_EXPAND share
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_table_get_column_width (LrgTable *self,
                                   guint     column);

/**
 * lrg_table_set_column_width:
 * @self: an #LrgTable
 * @column: a column index
 * @width: new width (at least 24 units)
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_column_width (LrgTable *self,
                                 guint     column,
                                 gfloat    width);

/**
 * lrg_table_get_column_flags:
 * @self: an #LrgTable
 * @column: a column index
 *
 * Returns: the column's flags
 */
LRG_AVAILABLE_IN_ALL
LrgTableColumnFlags lrg_table_get_column_flags (LrgTable *self,
                                                guint     column);

/**
 * lrg_table_set_sort_func:
 * @self: an #LrgTable
 * @column: a column index
 * @func: (nullable) (scope notified) (closure user_data): compares two
 *   items in ascending order, or %NULL to only emit the signal
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Convenience for tables over a #GListStore: when the sort column or
 * direction changes to @column, the store is sorted with @func
 * (reversed for descending). The selection follows its items. With
 * any other model only #LrgTable::sort-changed is emitted and sorting
 * is the application's job (e.g. through a sorted model).
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_sort_func (LrgTable         *self,
                              guint             column,
                              GCompareDataFunc  func,
                              gpointer          user_data,
                              GDestroyNotify    destroy);

/**
 * lrg_table_set_sort:
 * @self: an #LrgTable
 * @column: a column index, or -1 for unsorted
 * @ascending: direction
 *
 * Sets the sort indicator, emits #LrgTable::sort-changed and sorts a
 * #GListStore model when the column has a sort function.
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_sort (LrgTable *self,
                         gint      column,
                         gboolean  ascending);

/**
 * lrg_table_get_sort_column:
 * @self: an #LrgTable
 *
 * Returns: the sorted column, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_table_get_sort_column (LrgTable *self);

/**
 * lrg_table_get_sort_ascending:
 * @self: an #LrgTable
 *
 * Returns: %TRUE when sorted ascending
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_table_get_sort_ascending (LrgTable *self);

/**
 * lrg_table_get_row_height:
 * @self: an #LrgTable
 *
 * Returns: the configured row height (0 = the theme's ROW_HEIGHT)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_table_get_row_height (LrgTable *self);

/**
 * lrg_table_set_row_height:
 * @self: an #LrgTable
 * @row_height: height of every row and the header, or 0 for the
 *   theme's ROW_HEIGHT
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_row_height (LrgTable *self,
                               gfloat    row_height);

/**
 * lrg_table_get_striped:
 * @self: an #LrgTable
 *
 * Returns: %TRUE when odd rows are tinted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_table_get_striped (LrgTable *self);

/**
 * lrg_table_set_striped:
 * @self: an #LrgTable
 * @striped: tint odd rows
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_striped (LrgTable *self,
                            gboolean  striped);

/**
 * lrg_table_get_selection_mode:
 * @self: an #LrgTable
 *
 * Returns: how many rows can be selected
 */
LRG_AVAILABLE_IN_ALL
LrgSelectionMode lrg_table_get_selection_mode (LrgTable *self);

/**
 * lrg_table_set_selection_mode:
 * @self: an #LrgTable
 * @mode: none, single (the default) or multiple (Ctrl / Shift click)
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_set_selection_mode (LrgTable         *self,
                                   LrgSelectionMode  mode);

/**
 * lrg_table_get_selected:
 * @self: an #LrgTable
 *
 * Returns: the cursor row if it is selected, else the lowest selected
 *   row, or %LRG_TABLE_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_get_selected (LrgTable *self);

/**
 * lrg_table_get_n_selected:
 * @self: an #LrgTable
 *
 * Returns: number of selected rows
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_get_n_selected (LrgTable *self);

/**
 * lrg_table_is_selected:
 * @self: an #LrgTable
 * @position: a model position
 *
 * Returns: %TRUE if the row is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_table_is_selected (LrgTable *self,
                                guint     position);

/**
 * lrg_table_select:
 * @self: an #LrgTable
 * @position: a model position
 *
 * Selects the row (replacing the selection in single mode, adding to
 * it in multiple mode) and moves the cursor there.
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_select (LrgTable *self,
                       guint     position);

/**
 * lrg_table_unselect_all:
 * @self: an #LrgTable
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_unselect_all (LrgTable *self);

/**
 * lrg_table_get_cursor:
 * @self: an #LrgTable
 *
 * Returns: the keyboard cursor row, or %LRG_TABLE_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_get_cursor (LrgTable *self);

/**
 * lrg_table_scroll_to:
 * @self: an #LrgTable
 * @position: a model position
 * @animate: glide instead of jumping
 *
 * Scrolls vertically by the smallest amount that shows the whole row.
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_scroll_to (LrgTable *self,
                          guint     position,
                          gboolean  animate);

/**
 * lrg_table_get_scroll_offset:
 * @self: an #LrgTable
 * @x: (out) (optional): horizontal offset currently displayed
 * @y: (out) (optional): vertical offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
void lrg_table_get_scroll_offset (LrgTable *self,
                                  gfloat   *x,
                                  gfloat   *y);

/**
 * lrg_table_get_n_rows:
 * @self: an #LrgTable
 *
 * Returns: number of realized row widgets (visible range plus buffer),
 *   independent of the model size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_table_get_n_rows (LrgTable *self);

/**
 * lrg_table_get_cell_text:
 * @self: an #LrgTable
 * @position: a model position
 * @column: a column index
 *
 * Returns: (transfer none) (nullable): the text bound to a realized
 *   cell, or %NULL when the row is not realized or the cell has no text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_table_get_cell_text (LrgTable *self,
                                       guint     position,
                                       guint     column);

/**
 * lrg_table_get_cell_widget:
 * @self: an #LrgTable
 * @position: a model position
 * @column: a column index
 *
 * Returns: (transfer none) (nullable): the widget of a realized cell
 *   in a widget column, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_table_get_cell_widget (LrgTable *self,
                                       guint     position,
                                       guint     column);

G_END_DECLS
