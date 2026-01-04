/* lrg-pie-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPieChart2D - 2D Pie/Donut Chart widget.
 *
 * Renders data as pie slices or a donut chart. Each data point's Y value
 * determines the slice size relative to the total.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart2d.h"
#include "lrg-chart-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_PIE_CHART2D (lrg_pie_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPieChart2D, lrg_pie_chart2d, LRG, PIE_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_pie_chart2d_new:
 *
 * Creates a new pie chart with default settings.
 *
 * Returns: (transfer full): a new #LrgPieChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPieChart2D *
lrg_pie_chart2d_new (void);

/**
 * lrg_pie_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new pie chart with specified size.
 *
 * Returns: (transfer full): a new #LrgPieChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPieChart2D *
lrg_pie_chart2d_new_with_size (gfloat width,
                               gfloat height);

/* ==========================================================================
 * Pie Style
 * ========================================================================== */

/**
 * lrg_pie_chart2d_get_pie_style:
 * @self: an #LrgPieChart2D
 *
 * Gets the pie style.
 *
 * Returns: the pie style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartPieStyle
lrg_pie_chart2d_get_pie_style (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_pie_style:
 * @self: an #LrgPieChart2D
 * @style: the pie style
 *
 * Sets the pie style (pie, donut, or exploded).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_pie_style (LrgPieChart2D    *self,
                               LrgChartPieStyle  style);

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

/**
 * lrg_pie_chart2d_get_inner_radius:
 * @self: an #LrgPieChart2D
 *
 * Gets the inner radius (for donut charts).
 *
 * Returns: the inner radius as ratio of outer radius (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart2d_get_inner_radius (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_inner_radius:
 * @self: an #LrgPieChart2D
 * @radius: inner radius ratio (0.0 to 1.0)
 *
 * Sets the inner radius for donut charts. Use 0.0 for solid pie.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_inner_radius (LrgPieChart2D *self,
                                  gfloat         radius);

/**
 * lrg_pie_chart2d_get_explode_offset:
 * @self: an #LrgPieChart2D
 *
 * Gets the explode offset for exploded pie style.
 *
 * Returns: the explode offset in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart2d_get_explode_offset (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_explode_offset:
 * @self: an #LrgPieChart2D
 * @offset: offset in pixels
 *
 * Sets the offset for exploded slices.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_explode_offset (LrgPieChart2D *self,
                                    gfloat         offset);

/* ==========================================================================
 * Angles
 * ========================================================================== */

/**
 * lrg_pie_chart2d_get_start_angle:
 * @self: an #LrgPieChart2D
 *
 * Gets the starting angle for the first slice.
 *
 * Returns: the start angle in degrees
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart2d_get_start_angle (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_start_angle:
 * @self: an #LrgPieChart2D
 * @angle: start angle in degrees
 *
 * Sets the starting angle for the first slice. 0 is right, 90 is bottom.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_start_angle (LrgPieChart2D *self,
                                 gfloat         angle);

/* ==========================================================================
 * Labels
 * ========================================================================== */

/**
 * lrg_pie_chart2d_get_show_labels:
 * @self: an #LrgPieChart2D
 *
 * Gets whether labels are shown on slices.
 *
 * Returns: %TRUE if labels are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_pie_chart2d_get_show_labels (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_show_labels:
 * @self: an #LrgPieChart2D
 * @show: whether to show labels
 *
 * Sets whether to show labels on slices.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_show_labels (LrgPieChart2D *self,
                                 gboolean       show);

/**
 * lrg_pie_chart2d_get_show_percentages:
 * @self: an #LrgPieChart2D
 *
 * Gets whether percentage values are shown on labels.
 *
 * Returns: %TRUE if percentages are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_pie_chart2d_get_show_percentages (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_show_percentages:
 * @self: an #LrgPieChart2D
 * @show: whether to show percentages
 *
 * Sets whether to show percentage values on slice labels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_show_percentages (LrgPieChart2D *self,
                                      gboolean       show);

/* ==========================================================================
 * Visual
 * ========================================================================== */

/**
 * lrg_pie_chart2d_get_slice_gap:
 * @self: an #LrgPieChart2D
 *
 * Gets the gap between slices.
 *
 * Returns: the gap in degrees
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart2d_get_slice_gap (LrgPieChart2D *self);

/**
 * lrg_pie_chart2d_set_slice_gap:
 * @self: an #LrgPieChart2D
 * @gap: gap in degrees
 *
 * Sets the gap between slices.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart2d_set_slice_gap (LrgPieChart2D *self,
                               gfloat         gap);

G_END_DECLS
