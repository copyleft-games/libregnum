/* lrg-radar-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRadarChart2D - 2D Radar/Spider Chart widget.
 *
 * Renders data as a polygon on a radial grid, useful for comparing
 * multiple attributes across different categories.
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

#define LRG_TYPE_RADAR_CHART2D (lrg_radar_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRadarChart2D, lrg_radar_chart2d, LRG, RADAR_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_radar_chart2d_new:
 *
 * Creates a new radar chart with default settings.
 *
 * Returns: (transfer full): a new #LrgRadarChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRadarChart2D *
lrg_radar_chart2d_new (void);

/**
 * lrg_radar_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new radar chart with specified size.
 *
 * Returns: (transfer full): a new #LrgRadarChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRadarChart2D *
lrg_radar_chart2d_new_with_size (gfloat width,
                                 gfloat height);

/* ==========================================================================
 * Axis Configuration
 * ========================================================================== */

/**
 * lrg_radar_chart2d_set_axis_labels:
 * @self: an #LrgRadarChart2D
 * @labels: (array zero-terminated=1): NULL-terminated array of axis labels
 *
 * Sets the labels for each axis of the radar chart.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_axis_labels (LrgRadarChart2D  *self,
                                   const gchar     **labels);

/**
 * lrg_radar_chart2d_get_axis_count:
 * @self: an #LrgRadarChart2D
 *
 * Gets the number of axes.
 *
 * Returns: number of axes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_radar_chart2d_get_axis_count (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_get_axis_label:
 * @self: an #LrgRadarChart2D
 * @index: axis index
 *
 * Gets the label for a specific axis.
 *
 * Returns: (transfer none) (nullable): the axis label
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_radar_chart2d_get_axis_label (LrgRadarChart2D *self,
                                  guint            index);

/* ==========================================================================
 * Grid Configuration
 * ========================================================================== */

/**
 * lrg_radar_chart2d_get_grid_levels:
 * @self: an #LrgRadarChart2D
 *
 * Gets the number of concentric grid levels.
 *
 * Returns: number of grid levels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_radar_chart2d_get_grid_levels (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_grid_levels:
 * @self: an #LrgRadarChart2D
 * @levels: number of grid levels
 *
 * Sets the number of concentric grid levels (rings).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_grid_levels (LrgRadarChart2D *self,
                                   guint            levels);

/**
 * lrg_radar_chart2d_get_show_grid:
 * @self: an #LrgRadarChart2D
 *
 * Gets whether the grid is shown.
 *
 * Returns: %TRUE if grid is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radar_chart2d_get_show_grid (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_show_grid:
 * @self: an #LrgRadarChart2D
 * @show: whether to show grid
 *
 * Sets whether to show the grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_show_grid (LrgRadarChart2D *self,
                                 gboolean         show);

/**
 * lrg_radar_chart2d_get_grid_color:
 * @self: an #LrgRadarChart2D
 *
 * Gets the grid color.
 *
 * Returns: (transfer none): the grid color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_radar_chart2d_get_grid_color (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_grid_color:
 * @self: an #LrgRadarChart2D
 * @color: the grid color
 *
 * Sets the grid color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_grid_color (LrgRadarChart2D *self,
                                  GrlColor        *color);

/* ==========================================================================
 * Data Display
 * ========================================================================== */

/**
 * lrg_radar_chart2d_get_fill_opacity:
 * @self: an #LrgRadarChart2D
 *
 * Gets the opacity of the data polygon fill.
 *
 * Returns: opacity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radar_chart2d_get_fill_opacity (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_fill_opacity:
 * @self: an #LrgRadarChart2D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the opacity of the data polygon fill.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_fill_opacity (LrgRadarChart2D *self,
                                    gfloat           opacity);

/**
 * lrg_radar_chart2d_get_show_points:
 * @self: an #LrgRadarChart2D
 *
 * Gets whether data points are shown.
 *
 * Returns: %TRUE if points are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radar_chart2d_get_show_points (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_show_points:
 * @self: an #LrgRadarChart2D
 * @show: whether to show points
 *
 * Sets whether to show markers at data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_show_points (LrgRadarChart2D *self,
                                   gboolean         show);

/**
 * lrg_radar_chart2d_get_point_size:
 * @self: an #LrgRadarChart2D
 *
 * Gets the data point marker size.
 *
 * Returns: marker size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radar_chart2d_get_point_size (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_point_size:
 * @self: an #LrgRadarChart2D
 * @size: marker size in pixels
 *
 * Sets the data point marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_point_size (LrgRadarChart2D *self,
                                  gfloat           size);

/**
 * lrg_radar_chart2d_get_line_width:
 * @self: an #LrgRadarChart2D
 *
 * Gets the line width for the data polygon.
 *
 * Returns: line width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radar_chart2d_get_line_width (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_line_width:
 * @self: an #LrgRadarChart2D
 * @width: line width in pixels
 *
 * Sets the line width for the data polygon.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_line_width (LrgRadarChart2D *self,
                                  gfloat           width);

/* ==========================================================================
 * Value Range
 * ========================================================================== */

/**
 * lrg_radar_chart2d_get_max_value:
 * @self: an #LrgRadarChart2D
 *
 * Gets the maximum value for the chart.
 *
 * Returns: maximum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_radar_chart2d_get_max_value (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_max_value:
 * @self: an #LrgRadarChart2D
 * @max: maximum value
 *
 * Sets the maximum value for the chart. Values are normalized to this.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_max_value (LrgRadarChart2D *self,
                                 gdouble          max);

/**
 * lrg_radar_chart2d_get_auto_scale:
 * @self: an #LrgRadarChart2D
 *
 * Gets whether auto-scaling is enabled.
 *
 * Returns: %TRUE if auto-scaling is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radar_chart2d_get_auto_scale (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_auto_scale:
 * @self: an #LrgRadarChart2D
 * @auto_scale: whether to auto-scale
 *
 * Sets whether to automatically calculate max value from data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_auto_scale (LrgRadarChart2D *self,
                                  gboolean         auto_scale);

/* ==========================================================================
 * Labels
 * ========================================================================== */

/**
 * lrg_radar_chart2d_get_show_labels:
 * @self: an #LrgRadarChart2D
 *
 * Gets whether axis labels are shown.
 *
 * Returns: %TRUE if labels are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_radar_chart2d_get_show_labels (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_show_labels:
 * @self: an #LrgRadarChart2D
 * @show: whether to show labels
 *
 * Sets whether to show axis labels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_show_labels (LrgRadarChart2D *self,
                                   gboolean         show);

/**
 * lrg_radar_chart2d_get_label_font_size:
 * @self: an #LrgRadarChart2D
 *
 * Gets the label font size.
 *
 * Returns: font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_radar_chart2d_get_label_font_size (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_label_font_size:
 * @self: an #LrgRadarChart2D
 * @size: font size in pixels
 *
 * Sets the label font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_label_font_size (LrgRadarChart2D *self,
                                       gint             size);

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

/**
 * lrg_radar_chart2d_get_hit_radius:
 * @self: an #LrgRadarChart2D
 *
 * Gets the hit test radius for data points.
 *
 * Returns: the hit radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_radar_chart2d_get_hit_radius (LrgRadarChart2D *self);

/**
 * lrg_radar_chart2d_set_hit_radius:
 * @self: an #LrgRadarChart2D
 * @radius: hit radius in pixels
 *
 * Sets the hit test radius for data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_radar_chart2d_set_hit_radius (LrgRadarChart2D *self,
                                  gfloat           radius);

G_END_DECLS
