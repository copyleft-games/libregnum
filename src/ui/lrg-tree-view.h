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

/**
 * LrgDropPosition:
 * @LRG_DROP_POSITION_BEFORE: insert above the target row, as its
 *   previous sibling
 * @LRG_DROP_POSITION_INTO: drop onto the target row (a tree item becomes
 *   its last child; a list row is the drop target itself)
 * @LRG_DROP_POSITION_AFTER: insert below the target row, as its next
 *   sibling
 *
 * Where a dragged payload lands relative to a row, as computed by
 * lrg_tree_view_get_drop_position() and lrg_list_view_get_drop_position()
 * and shown by their drop indicators (an insertion line for BEFORE and
 * AFTER, a highlighted row for INTO).
 */
typedef enum
{
	LRG_DROP_POSITION_BEFORE,
	LRG_DROP_POSITION_INTO,
	LRG_DROP_POSITION_AFTER
} LrgDropPosition;

LRG_AVAILABLE_IN_ALL
GType lrg_drop_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DROP_POSITION (lrg_drop_position_get_type ())

/**
 * LrgSelectMode:
 * @LRG_SELECT_MODE_REPLACE: the selection becomes exactly the item (a
 *   plain click); the item becomes the range anchor
 * @LRG_SELECT_MODE_TOGGLE: the item is added or removed, the rest is kept
 *   (Ctrl+click); the item becomes the range anchor
 * @LRG_SELECT_MODE_EXTEND: the selection becomes every row between the
 *   anchor and the item (Shift+click, Shift+arrows); the anchor stays
 *
 * How lrg_tree_view_select_item_full(), lrg_list_view_select_full() and
 * lrg_tile_view_select_full() change the selection. TOGGLE and EXTEND
 * need %LRG_SELECTION_MODE_MULTIPLE; in single mode they act as REPLACE
 * (TOGGLE on the selected item unselects it).
 */
typedef enum
{
	LRG_SELECT_MODE_REPLACE,
	LRG_SELECT_MODE_TOGGLE,
	LRG_SELECT_MODE_EXTEND
} LrgSelectMode;

LRG_AVAILABLE_IN_ALL
GType lrg_select_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SELECT_MODE (lrg_select_mode_get_type ())

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
 * LrgTreeViewParentFunc:
 * @self: the tree view
 * @item: an item of the tree
 * @user_data: data given to lrg_tree_view_set_parent_func()
 *
 * Returns the parent of @item, the inverse of the
 * #LrgTreeViewChildrenFunc. Lets lrg_tree_view_reveal() expand exactly
 * the ancestors of an item that was never shown.
 *
 * Returns: (transfer none) (nullable): the parent of @item, or %NULL for
 *   a root item
 */
typedef GObject * (*LrgTreeViewParentFunc) (LrgTreeView *self,
                                            GObject     *item,
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

/* ==========================================================================
 * Revealing items
 * ========================================================================== */

/**
 * lrg_tree_view_set_parent_func:
 * @self: an #LrgTreeView
 * @func: (nullable) (scope notified) (closure user_data): returns the
 *   parent of an item
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Sets how lrg_tree_view_reveal() finds the ancestors of an item. Without
 * a parent function reveal searches the hierarchy depth first (asking the
 * children function for every branch it walks), which is fine for small
 * trees.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_parent_func (LrgTreeView           *self,
                                    LrgTreeViewParentFunc  func,
                                    gpointer               user_data,
                                    GDestroyNotify         destroy);

/**
 * lrg_tree_view_reveal:
 * @self: an #LrgTreeView
 * @item: an item anywhere in the tree
 *
 * Expands every collapsed ancestor of @item (emitting
 * #LrgTreeView::row-expanded for each) and scrolls by the smallest
 * amount that shows its row, gliding with the scroll spring unless
 * motion is off. The selection and the cursor do not change; call
 * lrg_tree_view_select_item_full() as well to select it (the editor's
 * "select in hierarchy" does both).
 *
 * Returns: %TRUE if @item has a row now
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_reveal (LrgTreeView *self,
                               GObject     *item);

/* ==========================================================================
 * Multi-selection
 * ========================================================================== */

/**
 * lrg_tree_view_select_item_full:
 * @self: an #LrgTreeView
 * @item: an item with a row
 * @mode: how the selection changes
 *
 * Changes the selection the way a click does: REPLACE (plain click),
 * TOGGLE (Ctrl+click) or EXTEND (Shift+click: every row between the
 * anchor and @item, in flattened order). The cursor moves to @item; the
 * view does not scroll. #LrgTreeView::selection-changed is emitted once
 * if the set of selected items changed. In %LRG_SELECTION_MODE_NONE
 * nothing is selected; in single mode TOGGLE and EXTEND act as REPLACE
 * (TOGGLE on the selected item clears the selection).
 *
 * Use this rather than lrg_tree_view_select_item(), whose behaviour
 * (add in multiple mode) is kept for compatibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_select_item_full (LrgTreeView   *self,
                                     GObject       *item,
                                     LrgSelectMode  mode);

/**
 * lrg_tree_view_get_selected_items:
 * @self: an #LrgTreeView
 *
 * The selected items: those with a row first, in row order, then the
 * ones hidden inside collapsed branches (they stay selected and
 * reappear selected).
 *
 * Returns: (transfer container) (element-type GObject): a new array of
 *   the selected items
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_tree_view_get_selected_items (LrgTreeView *self);

/**
 * lrg_tree_view_get_anchor_item:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the item EXTEND ranges start
 *   from (the last item selected with REPLACE or TOGGLE)
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_anchor_item (LrgTreeView *self);

/* ==========================================================================
 * Inline rename
 * ========================================================================== */

/**
 * lrg_tree_view_start_editing:
 * @self: an #LrgTreeView
 * @item: an item with a row
 *
 * Shows an #LrgTextInput over the row's label (the first #LrgLabel of the
 * row widget, or the whole row), filled with its text, all selected and
 * focused. F2 does this for the cursor item. Enter emits
 * #LrgTreeView::edit-label and ends the edit when a handler accepts;
 * Escape ends it with #LrgTreeView::edit-cancelled; moving the focus away
 * commits like Enter (and cancels if the text is refused). Collapsing,
 * removing or scrolling the row away keeps editing only while the item
 * still has a row. The view does not change the item itself: the
 * edit-label handler renames the model.
 *
 * Returns: %TRUE if editing started (the view must be on a canvas for
 *   the input to take the keyboard focus)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_start_editing (LrgTreeView *self,
                                      GObject     *item);

/**
 * lrg_tree_view_stop_editing:
 * @self: an #LrgTreeView
 * @cancel: %TRUE to drop the text (emits #LrgTreeView::edit-cancelled),
 *   %FALSE to commit it (emits #LrgTreeView::edit-label)
 *
 * Ends inline editing from code. When committing, a refused text keeps
 * the editor open, like Enter does.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_stop_editing (LrgTreeView *self,
                                 gboolean     cancel);

/**
 * lrg_tree_view_get_editing_item:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the item being renamed, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_editing_item (LrgTreeView *self);

/**
 * lrg_tree_view_get_editor:
 * @self: an #LrgTreeView
 *
 * Returns: (transfer none) (nullable): the text input shown while
 *   editing, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_tree_view_get_editor (LrgTreeView *self);

/* ==========================================================================
 * Geometry and drop helpers
 * ========================================================================== */

/**
 * lrg_tree_view_get_row_rect:
 * @self: an #LrgTreeView
 * @item: an item
 * @x: (out) (optional): left, canvas (world) coordinates
 * @y: (out) (optional): top, canvas coordinates, at the displayed scroll
 *   offset
 * @width: (out) (optional): width of the row (the whole viewport)
 * @height: (out) (optional): row height
 *
 * The rectangle of @item's row, the full width of the viewport. A row
 * scrolled out of the viewport still has a rectangle (outside it).
 *
 * Returns: %TRUE if @item has a row (no collapsed ancestor)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_get_row_rect (LrgTreeView *self,
                                     GObject     *item,
                                     gfloat      *x,
                                     gfloat      *y,
                                     gfloat      *width,
                                     gfloat      *height);

/**
 * lrg_tree_view_get_item_at:
 * @self: an #LrgTreeView
 * @x: canvas x
 * @y: canvas y
 *
 * Returns: (transfer none) (nullable): the item whose row is under the
 *   point, or %NULL (outside the viewport, below the last row)
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_item_at (LrgTreeView *self,
                                     gfloat       x,
                                     gfloat       y);

/**
 * lrg_tree_view_get_drop_position:
 * @self: an #LrgTreeView
 * @y: canvas y of the pointer
 * @item: (out) (optional) (nullable) (transfer none): the target row's
 *   item
 * @pos: (out) (optional): where the payload lands relative to @item
 *
 * Maps a drag position to a drop target. The top quarter of a row means
 * BEFORE, the bottom quarter AFTER, the middle INTO. AFTER an expanded
 * item with children is reported as BEFORE its first child (that is
 * where the line is drawn). Below the last row the target is the last
 * root item, AFTER; an empty tree gives %NULL and INTO (drop into the
 * root). The editor calls this from its drop-target `motion` handler
 * and shows the result with lrg_tree_view_set_drop_indicator().
 *
 * Returns: %TRUE if @y is inside the viewport
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tree_view_get_drop_position (LrgTreeView      *self,
                                          gfloat            y,
                                          GObject         **item,
                                          LrgDropPosition  *pos);

/**
 * lrg_tree_view_set_drop_indicator:
 * @self: an #LrgTreeView
 * @item: (nullable): the target row's item, %NULL to clear
 * @pos: where the payload would land
 *
 * Paints where a drop would land: an accent insertion line at the top
 * (BEFORE) or bottom (AFTER) of the row, starting at the row's
 * indentation, or an accent outline and tint over the row (INTO). The
 * line glides between rows with a spring.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_set_drop_indicator (LrgTreeView     *self,
                                       GObject         *item,
                                       LrgDropPosition  pos);

/**
 * lrg_tree_view_clear_drop_indicator:
 * @self: an #LrgTreeView
 *
 * Hides the drop indicator (on drag leave, drop or cancel).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tree_view_clear_drop_indicator (LrgTreeView *self);

/**
 * lrg_tree_view_get_drop_indicator:
 * @self: an #LrgTreeView
 * @pos: (out) (optional): the indicated position
 *
 * Returns: (transfer none) (nullable): the item the indicator is on,
 *   or %NULL when hidden
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_tree_view_get_drop_indicator (LrgTreeView     *self,
                                            LrgDropPosition *pos);

G_END_DECLS
