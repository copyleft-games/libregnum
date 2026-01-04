/* lrg-chart-data-series.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartDataSeries - A series of data points for charts.
 *
 * A data series represents a collection of data points with
 * associated styling (color, line style, marker) and metadata.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-chart-enums.h"
#include "lrg-chart-data-point.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_DATA_SERIES (lrg_chart_data_series_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgChartDataSeries, lrg_chart_data_series, LRG, CHART_DATA_SERIES, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_data_series_new:
 * @name: (nullable): series name for legend
 *
 * Creates a new empty data series.
 *
 * Returns: (transfer full): a new #LrgChartDataSeries
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataSeries *
lrg_chart_data_series_new (const gchar *name);

/**
 * lrg_chart_data_series_new_with_color:
 * @name: (nullable): series name for legend
 * @color: series color
 *
 * Creates a new data series with a specified color.
 *
 * Returns: (transfer full): a new #LrgChartDataSeries
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataSeries *
lrg_chart_data_series_new_with_color (const gchar    *name,
                                       const GrlColor *color);

/* ==========================================================================
 * Name
 * ========================================================================== */

/**
 * lrg_chart_data_series_get_name:
 * @self: an #LrgChartDataSeries
 *
 * Gets the series name (for legend display).
 *
 * Returns: (transfer none) (nullable): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_data_series_get_name (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_name:
 * @self: an #LrgChartDataSeries
 * @name: (nullable): the name
 *
 * Sets the series name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_name (LrgChartDataSeries *self,
                                 const gchar        *name);

/* ==========================================================================
 * Styling
 * ========================================================================== */

/**
 * lrg_chart_data_series_get_color:
 * @self: an #LrgChartDataSeries
 *
 * Gets the series color.
 *
 * Returns: (transfer none): the color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_data_series_get_color (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_color:
 * @self: an #LrgChartDataSeries
 * @color: the color
 *
 * Sets the series color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_color (LrgChartDataSeries *self,
                                  const GrlColor     *color);

/**
 * lrg_chart_data_series_get_line_width:
 * @self: an #LrgChartDataSeries
 *
 * Gets the line width for line charts.
 *
 * Returns: the line width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_data_series_get_line_width (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_line_width:
 * @self: an #LrgChartDataSeries
 * @width: the line width
 *
 * Sets the line width for line charts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_line_width (LrgChartDataSeries *self,
                                       gfloat              width);

/**
 * lrg_chart_data_series_get_line_style:
 * @self: an #LrgChartDataSeries
 *
 * Gets the line style.
 *
 * Returns: the line style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartLineStyle
lrg_chart_data_series_get_line_style (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_line_style:
 * @self: an #LrgChartDataSeries
 * @style: the line style
 *
 * Sets the line style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_line_style (LrgChartDataSeries *self,
                                       LrgChartLineStyle   style);

/**
 * lrg_chart_data_series_get_marker:
 * @self: an #LrgChartDataSeries
 *
 * Gets the marker style.
 *
 * Returns: the marker style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartMarker
lrg_chart_data_series_get_marker (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_marker:
 * @self: an #LrgChartDataSeries
 * @marker: the marker style
 *
 * Sets the marker style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_marker (LrgChartDataSeries *self,
                                   LrgChartMarker      marker);

/**
 * lrg_chart_data_series_get_marker_size:
 * @self: an #LrgChartDataSeries
 *
 * Gets the marker size.
 *
 * Returns: the marker size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_data_series_get_marker_size (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_marker_size:
 * @self: an #LrgChartDataSeries
 * @size: the marker size
 *
 * Sets the marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_marker_size (LrgChartDataSeries *self,
                                        gfloat              size);

/* ==========================================================================
 * Visibility
 * ========================================================================== */

/**
 * lrg_chart_data_series_get_visible:
 * @self: an #LrgChartDataSeries
 *
 * Gets whether the series is visible.
 *
 * Returns: %TRUE if visible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_data_series_get_visible (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_visible:
 * @self: an #LrgChartDataSeries
 * @visible: whether to show the series
 *
 * Sets whether the series is visible.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_visible (LrgChartDataSeries *self,
                                    gboolean            visible);

/**
 * lrg_chart_data_series_get_show_in_legend:
 * @self: an #LrgChartDataSeries
 *
 * Gets whether the series appears in the legend.
 *
 * Returns: %TRUE if shown in legend
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_data_series_get_show_in_legend (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_show_in_legend:
 * @self: an #LrgChartDataSeries
 * @show: whether to show in legend
 *
 * Sets whether the series appears in the legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_show_in_legend (LrgChartDataSeries *self,
                                           gboolean            show);

/* ==========================================================================
 * Data Points
 * ========================================================================== */

/**
 * lrg_chart_data_series_get_point_count:
 * @self: an #LrgChartDataSeries
 *
 * Gets the number of data points.
 *
 * Returns: the point count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_data_series_get_point_count (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_get_point:
 * @self: an #LrgChartDataSeries
 * @index: point index
 *
 * Gets a data point by index.
 *
 * Returns: (transfer none) (nullable): the data point
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgChartDataPoint *
lrg_chart_data_series_get_point (LrgChartDataSeries *self,
                                  guint               index);

/**
 * lrg_chart_data_series_add_point:
 * @self: an #LrgChartDataSeries
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Adds a new data point.
 *
 * Returns: the index of the new point
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_data_series_add_point (LrgChartDataSeries *self,
                                  gdouble             x,
                                  gdouble             y);

/**
 * lrg_chart_data_series_add_point_labeled:
 * @self: an #LrgChartDataSeries
 * @x: X coordinate
 * @y: Y coordinate
 * @label: (nullable): point label
 *
 * Adds a new data point with a label.
 *
 * Returns: the index of the new point
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_data_series_add_point_labeled (LrgChartDataSeries *self,
                                          gdouble             x,
                                          gdouble             y,
                                          const gchar        *label);

/**
 * lrg_chart_data_series_add_point_full:
 * @self: an #LrgChartDataSeries
 * @point: (transfer full): the data point to add
 *
 * Adds an existing data point (takes ownership).
 *
 * Returns: the index of the new point
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_data_series_add_point_full (LrgChartDataSeries *self,
                                       LrgChartDataPoint  *point);

/**
 * lrg_chart_data_series_insert_point:
 * @self: an #LrgChartDataSeries
 * @index: position to insert at
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Inserts a new data point at a specific position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_insert_point (LrgChartDataSeries *self,
                                     guint               index,
                                     gdouble             x,
                                     gdouble             y);

/**
 * lrg_chart_data_series_remove_point:
 * @self: an #LrgChartDataSeries
 * @index: point index to remove
 *
 * Removes a data point.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_data_series_remove_point (LrgChartDataSeries *self,
                                     guint               index);

/**
 * lrg_chart_data_series_clear:
 * @self: an #LrgChartDataSeries
 *
 * Removes all data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_clear (LrgChartDataSeries *self);

/**
 * lrg_chart_data_series_set_point_value:
 * @self: an #LrgChartDataSeries
 * @index: point index
 * @x: new X value
 * @y: new Y value
 *
 * Updates the value of an existing point.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_set_point_value (LrgChartDataSeries *self,
                                        guint               index,
                                        gdouble             x,
                                        gdouble             y);

/* ==========================================================================
 * Data Range
 * ========================================================================== */

/**
 * lrg_chart_data_series_get_x_range:
 * @self: an #LrgChartDataSeries
 * @min: (out) (nullable): minimum X value
 * @max: (out) (nullable): maximum X value
 *
 * Gets the X value range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_get_x_range (LrgChartDataSeries *self,
                                    gdouble            *min,
                                    gdouble            *max);

/**
 * lrg_chart_data_series_get_y_range:
 * @self: an #LrgChartDataSeries
 * @min: (out) (nullable): minimum Y value
 * @max: (out) (nullable): maximum Y value
 *
 * Gets the Y value range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_series_get_y_range (LrgChartDataSeries *self,
                                    gdouble            *min,
                                    gdouble            *max);

/**
 * lrg_chart_data_series_get_y_sum:
 * @self: an #LrgChartDataSeries
 *
 * Gets the sum of all Y values (for pie charts).
 *
 * Returns: the sum
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_data_series_get_y_sum (LrgChartDataSeries *self);

G_END_DECLS
