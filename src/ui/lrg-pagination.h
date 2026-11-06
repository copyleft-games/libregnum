/* lrg-pagination.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPagination: a page picker, "< 1 ... 4 5 6 ... 20 >".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/**
 * LRG_PAGINATION_GAP:
 *
 * The value lrg_pagination_compute_items() stores for an ellipsis gap
 * ("...") between page numbers.
 */
#define LRG_PAGINATION_GAP (-1)

/**
 * LrgPaginationItem:
 * @LRG_PAGINATION_ITEM_NONE: no button
 * @LRG_PAGINATION_ITEM_PAGE: a page number
 * @LRG_PAGINATION_ITEM_GAP: an ellipsis gap ("...")
 * @LRG_PAGINATION_ITEM_PREVIOUS: the previous-page arrow
 * @LRG_PAGINATION_ITEM_NEXT: the next-page arrow
 *
 * The kinds of button a pagination shows (lrg_pagination_get_item_at()).
 */
typedef enum
{
	LRG_PAGINATION_ITEM_NONE,
	LRG_PAGINATION_ITEM_PAGE,
	LRG_PAGINATION_ITEM_GAP,
	LRG_PAGINATION_ITEM_PREVIOUS,
	LRG_PAGINATION_ITEM_NEXT
} LrgPaginationItem;

LRG_AVAILABLE_IN_ALL
GType lrg_pagination_item_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PAGINATION_ITEM (lrg_pagination_item_get_type ())

#define LRG_TYPE_PAGINATION (lrg_pagination_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPagination, lrg_pagination, LRG, PAGINATION, LrgWidget)

/**
 * lrg_pagination_new:
 * @n_pages: number of pages
 *
 * Creates a pagination showing page 0 (labelled "1") of @n_pages.
 *
 * Returns: (transfer full): a new #LrgPagination
 */
LRG_AVAILABLE_IN_ALL
LrgPagination * lrg_pagination_new (guint n_pages);

/**
 * lrg_pagination_get_n_pages:
 * @self: an #LrgPagination
 *
 * Returns: the number of pages
 */
LRG_AVAILABLE_IN_ALL
guint lrg_pagination_get_n_pages (LrgPagination *self);

/**
 * lrg_pagination_set_n_pages:
 * @self: an #LrgPagination
 * @n_pages: number of pages; the current page is clamped to the new
 *   range (emitting #LrgPagination::page-changed if it moves)
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_n_pages (LrgPagination *self,
                                 guint          n_pages);

/**
 * lrg_pagination_get_page:
 * @self: an #LrgPagination
 *
 * Returns: the current page, 0-based (shown as @page + 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_pagination_get_page (LrgPagination *self);

/**
 * lrg_pagination_set_page:
 * @self: an #LrgPagination
 * @page: the page to show, 0-based; clamped to the last page
 *
 * Selects a page. The accent pill slides to it and
 * #LrgPagination::page-changed is emitted when the page changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_page (LrgPagination *self,
                              guint          page);

/**
 * lrg_pagination_get_siblings:
 * @self: an #LrgPagination
 *
 * Returns: how many pages are shown on each side of the current page
 */
LRG_AVAILABLE_IN_ALL
guint lrg_pagination_get_siblings (LrgPagination *self);

/**
 * lrg_pagination_set_siblings:
 * @self: an #LrgPagination
 * @siblings: pages shown on each side of the current one (default 1)
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_siblings (LrgPagination *self,
                                  guint          siblings);

/**
 * lrg_pagination_get_boundary:
 * @self: an #LrgPagination
 *
 * Returns: how many pages are always shown at each end
 */
LRG_AVAILABLE_IN_ALL
guint lrg_pagination_get_boundary (LrgPagination *self);

/**
 * lrg_pagination_set_boundary:
 * @self: an #LrgPagination
 * @boundary: pages always shown at the start and at the end (default 1)
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_boundary (LrgPagination *self,
                                  guint          boundary);

/**
 * lrg_pagination_get_show_prev_next:
 * @self: an #LrgPagination
 *
 * Returns: %TRUE if the previous / next arrow buttons are shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pagination_get_show_prev_next (LrgPagination *self);

/**
 * lrg_pagination_set_show_prev_next:
 * @self: an #LrgPagination
 * @show: show the arrow buttons at both ends (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_show_prev_next (LrgPagination *self,
                                        gboolean       show);

/**
 * lrg_pagination_get_jump_on_gap:
 * @self: an #LrgPagination
 *
 * Returns: %TRUE if clicking an ellipsis gap jumps pages
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pagination_get_jump_on_gap (LrgPagination *self);

/**
 * lrg_pagination_set_jump_on_gap:
 * @self: an #LrgPagination
 * @jump: let a click on a "..." gap jump siblings * 2 + 1 pages towards
 *   it (default %TRUE); when %FALSE gaps are inert
 */
LRG_AVAILABLE_IN_ALL
void lrg_pagination_set_jump_on_gap (LrgPagination *self,
                                     gboolean       jump);

/**
 * lrg_pagination_next:
 * @self: an #LrgPagination
 *
 * Moves to the next page, if any.
 *
 * Returns: %TRUE if the page changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pagination_next (LrgPagination *self);

/**
 * lrg_pagination_previous:
 * @self: an #LrgPagination
 *
 * Moves to the previous page, if any.
 *
 * Returns: %TRUE if the page changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pagination_previous (LrgPagination *self);

/**
 * lrg_pagination_get_item_at:
 * @self: an #LrgPagination
 * @x: world X
 * @y: world Y
 * @page: (out) (optional): the page the button shows, the page a
 *   gap would jump to, or the page an arrow leads to (-1 when it is
 *   disabled)
 *
 * Hit test for apps and tests: which button is under a point.
 *
 * Returns: the kind of button at the point
 */
LRG_AVAILABLE_IN_ALL
LrgPaginationItem lrg_pagination_get_item_at (LrgPagination *self,
                                              gfloat         x,
                                              gfloat         y,
                                              gint          *page);

/**
 * lrg_pagination_get_page_rect:
 * @self: an #LrgPagination
 * @page: a page, 0-based
 * @x: (out) (optional): world left edge of its button
 * @y: (out) (optional): world top edge
 * @width: (out) (optional): button width
 * @height: (out) (optional): button height
 *
 * Where the button of @page is, after mirroring for right-to-left.
 *
 * Returns: %TRUE if @page currently has a button (it may be hidden
 *   behind a gap)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pagination_get_page_rect (LrgPagination *self,
                                       guint          page,
                                       gfloat        *x,
                                       gfloat        *y,
                                       gfloat        *width,
                                       gfloat        *height);

/**
 * lrg_pagination_get_indicator_x:
 * @self: an #LrgPagination
 *
 * Returns: the world X of the left edge of the accent pill right now
 *   (it follows a spring towards the current page's button)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_pagination_get_indicator_x (LrgPagination *self);

/**
 * lrg_pagination_compute_items:
 * @n_pages: number of pages
 * @page: the current page, 0-based (clamped to the last page)
 * @siblings: pages shown on each side of @page
 * @boundary: pages always shown at each end
 * @items: (out caller-allocates) (array length=max_items) (nullable):
 *   where to store the items: page numbers (0-based) in display order,
 *   %LRG_PAGINATION_GAP for an ellipsis
 * @max_items: room in @items
 *
 * The page buttons a pagination shows, as a pure function so apps can
 * build their own pager (or test the logic) without a widget. While
 * all pages fit, every page is listed; beyond that the count is fixed
 * at @boundary * 2 + @siblings * 2 + 3, so the row keeps its width
 * while the current page moves: a gap only ever replaces two or more
 * pages, never a single one.
 *
 * |[<!-- language="C" -->
 * gint  items[16];
 * guint n = lrg_pagination_compute_items (20, 9, 1, 1, items, 16);
 * // n = 7: 0 GAP 8 9 10 GAP 19, shown as "1 ... 9 10 11 ... 20"
 * ]|
 *
 * Returns: the number of items (may exceed @max_items; only
 *   @max_items are written)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_pagination_compute_items (guint  n_pages,
                                    guint  page,
                                    guint  siblings,
                                    guint  boundary,
                                    gint  *items,
                                    guint  max_items);

G_END_DECLS
