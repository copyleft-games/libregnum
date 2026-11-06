/* lrg-breadcrumb.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBreadcrumb: A path of clickable segments that collapses to an ellipsis when narrow.
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
 * LRG_BREADCRUMB_ELLIPSIS:
 *
 * Segment index that stands for the "…" segment of a collapsed
 * breadcrumb in lrg_breadcrumb_get_segment_bounds().
 */
#define LRG_BREADCRUMB_ELLIPSIS (-1)

#define LRG_TYPE_BREADCRUMB (lrg_breadcrumb_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBreadcrumb, lrg_breadcrumb, LRG, BREADCRUMB, LrgWidget)

/**
 * lrg_breadcrumb_new:
 *
 * Creates an empty breadcrumb.
 *
 * Returns: (transfer full): a new #LrgBreadcrumb
 */
LRG_AVAILABLE_IN_ALL
LrgBreadcrumb * lrg_breadcrumb_new (void);

/**
 * lrg_breadcrumb_append:
 * @self: an #LrgBreadcrumb
 * @label: text of the new last segment
 *
 * Adds a segment at the end of the path.
 *
 * Returns: the index of the new segment
 */
LRG_AVAILABLE_IN_ALL
guint lrg_breadcrumb_append (LrgBreadcrumb *self,
                             const gchar   *label);

/**
 * lrg_breadcrumb_append_with_data:
 * @self: an #LrgBreadcrumb
 * @label: text of the new last segment
 * @data: (nullable): data to keep with the segment
 * @destroy: (nullable): frees @data when the segment is removed
 *
 * Adds a segment that carries application data (a folder object, a
 * menu id...), read back with lrg_breadcrumb_get_segment_data().
 *
 * Returns: the index of the new segment
 */
LRG_AVAILABLE_IN_ALL
guint lrg_breadcrumb_append_with_data (LrgBreadcrumb  *self,
                                       const gchar    *label,
                                       gpointer        data,
                                       GDestroyNotify  destroy);

/**
 * lrg_breadcrumb_truncate:
 * @self: an #LrgBreadcrumb
 * @n_segments: number of segments to keep
 *
 * Removes every segment from index @n_segments on, the usual reaction
 * to #LrgBreadcrumb::activated ("go up to here").
 */
LRG_AVAILABLE_IN_ALL
void lrg_breadcrumb_truncate (LrgBreadcrumb *self,
                              guint          n_segments);

/**
 * lrg_breadcrumb_remove_all:
 * @self: an #LrgBreadcrumb
 *
 * Removes every segment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_breadcrumb_remove_all (LrgBreadcrumb *self);

/**
 * lrg_breadcrumb_get_n_segments:
 * @self: an #LrgBreadcrumb
 *
 * Returns: the number of segments
 */
LRG_AVAILABLE_IN_ALL
guint lrg_breadcrumb_get_n_segments (LrgBreadcrumb *self);

/**
 * lrg_breadcrumb_get_segment_label:
 * @self: an #LrgBreadcrumb
 * @index: a segment index
 *
 * Returns: (nullable): the segment's label, or %NULL if @index is out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_breadcrumb_get_segment_label (LrgBreadcrumb *self,
                                                guint          index);

/**
 * lrg_breadcrumb_get_segment_data:
 * @self: an #LrgBreadcrumb
 * @index: a segment index
 *
 * Returns: (nullable) (transfer none): the data given to
 *   lrg_breadcrumb_append_with_data()
 */
LRG_AVAILABLE_IN_ALL
gpointer lrg_breadcrumb_get_segment_data (LrgBreadcrumb *self,
                                          guint          index);

/**
 * lrg_breadcrumb_get_collapsed:
 * @self: an #LrgBreadcrumb
 *
 * Whether some middle segments are folded into the "…" segment at the
 * current width.
 *
 * Returns: %TRUE when collapsed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_breadcrumb_get_collapsed (LrgBreadcrumb *self);

/**
 * lrg_breadcrumb_get_hidden_range:
 * @self: an #LrgBreadcrumb
 * @first: (out) (optional): index of the first hidden segment
 * @n_hidden: (out) (optional): number of hidden segments
 *
 * The segments folded into the "…" segment at the current width.
 *
 * Returns: %TRUE when any segment is hidden
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_breadcrumb_get_hidden_range (LrgBreadcrumb *self,
                                          guint         *first,
                                          guint         *n_hidden);

/**
 * lrg_breadcrumb_get_segment_bounds:
 * @self: an #LrgBreadcrumb
 * @index: a segment index, or %LRG_BREADCRUMB_ELLIPSIS
 * @bounds: (out caller-allocates): the segment's rectangle in canvas
 *   coordinates
 *
 * Where a segment is drawn, for hit testing or anchoring your own
 * popups.
 *
 * Returns: %FALSE if the segment is hidden or does not exist
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_breadcrumb_get_segment_bounds (LrgBreadcrumb *self,
                                            gint           index,
                                            GrlRectangle  *bounds);

/**
 * lrg_breadcrumb_get_popup_open:
 * @self: an #LrgBreadcrumb
 *
 * Returns: whether the list of hidden segments is showing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_breadcrumb_get_popup_open (LrgBreadcrumb *self);

/**
 * lrg_breadcrumb_popdown:
 * @self: an #LrgBreadcrumb
 *
 * Closes the list of hidden segments if it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_breadcrumb_popdown (LrgBreadcrumb *self);

G_END_DECLS
