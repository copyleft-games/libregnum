/* lrg-chart-hit-info.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartHitInfo - Hit test result for chart interactivity.
 *
 * This is a boxed type containing information about which chart
 * element (bar, point, slice, etc.) was hit during a mouse event.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-chart-data-point.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_HIT_INFO (lrg_chart_hit_info_get_type ())

/**
 * LrgChartHitInfo:
 *
 * Information about a hit test result on a chart.
 *
 * #LrgChartHitInfo is a boxed type containing details about which
 * element of a chart was hit during mouse interaction. Use
 * lrg_chart_hit_info_copy() and lrg_chart_hit_info_free()
 * to manage its lifetime.
 *
 * Since: 1.0
 */
typedef struct _LrgChartHitInfo LrgChartHitInfo;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_hit_info_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_hit_info_new:
 *
 * Creates a new empty hit info (no hit).
 *
 * Returns: (transfer full): a new #LrgChartHitInfo
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartHitInfo *
lrg_chart_hit_info_new (void);

/**
 * lrg_chart_hit_info_new_with_hit:
 * @series_index: index of the hit series
 * @point_index: index of the hit point within the series
 * @screen_x: screen X coordinate of the hit point
 * @screen_y: screen Y coordinate of the hit point
 *
 * Creates a new hit info with hit data.
 *
 * Returns: (transfer full): a new #LrgChartHitInfo
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartHitInfo *
lrg_chart_hit_info_new_with_hit (gint   series_index,
                                  gint   point_index,
                                  gfloat screen_x,
                                  gfloat screen_y);

/**
 * lrg_chart_hit_info_copy:
 * @self: an #LrgChartHitInfo
 *
 * Creates a copy of the hit info.
 *
 * Returns: (transfer full): a new #LrgChartHitInfo
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartHitInfo *
lrg_chart_hit_info_copy (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_free:
 * @self: an #LrgChartHitInfo
 *
 * Frees the hit info.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_free (LrgChartHitInfo *self);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_chart_hit_info_has_hit:
 * @self: an #LrgChartHitInfo
 *
 * Checks if this info represents an actual hit.
 *
 * Returns: %TRUE if there was a hit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_hit_info_has_hit (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_clear:
 * @self: an #LrgChartHitInfo
 *
 * Clears the hit info (sets to no hit).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_clear (LrgChartHitInfo *self);

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_chart_hit_info_get_series_index:
 * @self: an #LrgChartHitInfo
 *
 * Gets the index of the hit series.
 *
 * Returns: series index, or -1 if no hit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_chart_hit_info_get_series_index (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_set_series_index:
 * @self: an #LrgChartHitInfo
 * @index: series index, or -1 for no hit
 *
 * Sets the series index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_series_index (LrgChartHitInfo *self,
                                     gint             index);

/**
 * lrg_chart_hit_info_get_point_index:
 * @self: an #LrgChartHitInfo
 *
 * Gets the index of the hit point within the series.
 *
 * Returns: point index, or -1 if no hit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_chart_hit_info_get_point_index (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_set_point_index:
 * @self: an #LrgChartHitInfo
 * @index: point index, or -1 for no hit
 *
 * Sets the point index.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_point_index (LrgChartHitInfo *self,
                                    gint             index);

/**
 * lrg_chart_hit_info_get_screen_x:
 * @self: an #LrgChartHitInfo
 *
 * Gets the screen X coordinate of the hit element.
 *
 * Returns: the X coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_hit_info_get_screen_x (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_set_screen_x:
 * @self: an #LrgChartHitInfo
 * @x: the X coordinate
 *
 * Sets the screen X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_screen_x (LrgChartHitInfo *self,
                                 gfloat           x);

/**
 * lrg_chart_hit_info_get_screen_y:
 * @self: an #LrgChartHitInfo
 *
 * Gets the screen Y coordinate of the hit element.
 *
 * Returns: the Y coordinate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_hit_info_get_screen_y (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_set_screen_y:
 * @self: an #LrgChartHitInfo
 * @y: the Y coordinate
 *
 * Sets the screen Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_screen_y (LrgChartHitInfo *self,
                                 gfloat           y);

/**
 * lrg_chart_hit_info_get_bounds:
 * @self: an #LrgChartHitInfo
 * @out_bounds: (out): location to store bounds
 *
 * Gets the bounding rectangle of the hit element.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_get_bounds (const LrgChartHitInfo *self,
                               GrlRectangle          *out_bounds);

/**
 * lrg_chart_hit_info_set_bounds:
 * @self: an #LrgChartHitInfo
 * @bounds: the bounds rectangle
 *
 * Sets the bounding rectangle of the hit element.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_bounds (LrgChartHitInfo      *self,
                               const GrlRectangle   *bounds);

/**
 * lrg_chart_hit_info_get_data_point:
 * @self: an #LrgChartHitInfo
 *
 * Gets the data point that was hit.
 *
 * Returns: (transfer none) (nullable): the data point, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgChartDataPoint *
lrg_chart_hit_info_get_data_point (const LrgChartHitInfo *self);

/**
 * lrg_chart_hit_info_set_data_point:
 * @self: an #LrgChartHitInfo
 * @point: (nullable): the data point
 *
 * Sets the data point reference.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_hit_info_set_data_point (LrgChartHitInfo         *self,
                                   const LrgChartDataPoint *point);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgChartHitInfo, lrg_chart_hit_info_free)

G_END_DECLS
