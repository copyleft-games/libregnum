/* lrg-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChart2D - Intermediate class for 2D charts.
 *
 * LrgChart2D extends LrgChart with functionality specific to 2D charting:
 * - Axis configuration and rendering
 * - Grid line drawing
 * - Legend display
 * - Coordinate transformations between data space and screen space
 *
 * Concrete 2D chart types (bar, line, pie, etc.) should extend this class.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-chart.h"
#include "lrg-chart-axis-config.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART2D (lrg_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgChart2D, lrg_chart2d, LRG, CHART2D, LrgChart)

/**
 * LrgChart2DClass:
 * @parent_class: The parent class
 * @draw_background: Virtual method to draw the chart background
 * @draw_axes: Virtual method to draw the axes
 * @draw_grid: Virtual method to draw the grid lines
 * @draw_data: Virtual method to draw the actual data (bars, lines, etc.)
 * @draw_legend: Virtual method to draw the legend
 * @data_to_screen: Virtual method to convert data coordinates to screen coordinates
 * @screen_to_data: Virtual method to convert screen coordinates to data coordinates
 *
 * The class structure for #LrgChart2D.
 *
 * Subclasses must implement @draw_data to render their specific chart type.
 * The other methods have default implementations that work for most cases.
 */
struct _LrgChart2DClass
{
    LrgChartClass parent_class;

    /* Virtual methods */

    /**
     * LrgChart2DClass::draw_background:
     * @self: the chart
     *
     * Draws the chart background. The default implementation fills with
     * the background color.
     */
    void (*draw_background) (LrgChart2D *self);

    /**
     * LrgChart2DClass::draw_axes:
     * @self: the chart
     *
     * Draws the X and Y axes with labels and tick marks.
     * Some chart types (like pie charts) may override to do nothing.
     */
    void (*draw_axes) (LrgChart2D *self);

    /**
     * LrgChart2DClass::draw_grid:
     * @self: the chart
     *
     * Draws the grid lines based on axis configuration.
     */
    void (*draw_grid) (LrgChart2D *self);

    /**
     * LrgChart2DClass::draw_data:
     * @self: the chart
     *
     * Draws the actual chart data. Subclasses must implement this
     * to render their specific visualization (bars, lines, slices, etc.).
     */
    void (*draw_data) (LrgChart2D *self);

    /**
     * LrgChart2DClass::draw_legend:
     * @self: the chart
     *
     * Draws the chart legend showing series names and colors.
     */
    void (*draw_legend) (LrgChart2D *self);

    /**
     * LrgChart2DClass::data_to_screen:
     * @self: the chart
     * @data_x: X value in data coordinates
     * @data_y: Y value in data coordinates
     * @screen_x: (out): X value in screen coordinates
     * @screen_y: (out): Y value in screen coordinates
     *
     * Converts data coordinates to screen coordinates. Subclasses
     * may override for non-Cartesian coordinate systems.
     */
    void (*data_to_screen) (LrgChart2D *self,
                            gdouble     data_x,
                            gdouble     data_y,
                            gfloat     *screen_x,
                            gfloat     *screen_y);

    /**
     * LrgChart2DClass::screen_to_data:
     * @self: the chart
     * @screen_x: X value in screen coordinates
     * @screen_y: Y value in screen coordinates
     * @data_x: (out): X value in data coordinates
     * @data_y: (out): Y value in data coordinates
     *
     * Converts screen coordinates to data coordinates.
     */
    void (*screen_to_data) (LrgChart2D *self,
                            gfloat      screen_x,
                            gfloat      screen_y,
                            gdouble    *data_x,
                            gdouble    *data_y);

    /*< private >*/
    gpointer _reserved[4];
};

/* ==========================================================================
 * Axis Configuration
 * ========================================================================== */

/**
 * lrg_chart2d_get_x_axis:
 * @self: an #LrgChart2D
 *
 * Gets the X axis configuration.
 *
 * Returns: (transfer none): the X axis config
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart2d_get_x_axis (LrgChart2D *self);

/**
 * lrg_chart2d_set_x_axis:
 * @self: an #LrgChart2D
 * @config: the X axis config
 *
 * Sets the X axis configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_set_x_axis (LrgChart2D         *self,
                        LrgChartAxisConfig *config);

/**
 * lrg_chart2d_get_y_axis:
 * @self: an #LrgChart2D
 *
 * Gets the Y axis configuration.
 *
 * Returns: (transfer none): the Y axis config
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart2d_get_y_axis (LrgChart2D *self);

/**
 * lrg_chart2d_set_y_axis:
 * @self: an #LrgChart2D
 * @config: the Y axis config
 *
 * Sets the Y axis configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_set_y_axis (LrgChart2D         *self,
                        LrgChartAxisConfig *config);

/* ==========================================================================
 * Data Ranges
 * ========================================================================== */

/**
 * lrg_chart2d_get_x_min:
 * @self: an #LrgChart2D
 *
 * Gets the effective minimum X value (computed from data or manual setting).
 *
 * Returns: the minimum X value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart2d_get_x_min (LrgChart2D *self);

/**
 * lrg_chart2d_get_x_max:
 * @self: an #LrgChart2D
 *
 * Gets the effective maximum X value.
 *
 * Returns: the maximum X value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart2d_get_x_max (LrgChart2D *self);

/**
 * lrg_chart2d_get_y_min:
 * @self: an #LrgChart2D
 *
 * Gets the effective minimum Y value.
 *
 * Returns: the minimum Y value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart2d_get_y_min (LrgChart2D *self);

/**
 * lrg_chart2d_get_y_max:
 * @self: an #LrgChart2D
 *
 * Gets the effective maximum Y value.
 *
 * Returns: the maximum Y value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart2d_get_y_max (LrgChart2D *self);

/* ==========================================================================
 * Legend
 * ========================================================================== */

/**
 * lrg_chart2d_get_show_legend:
 * @self: an #LrgChart2D
 *
 * Gets whether the legend is shown.
 *
 * Returns: %TRUE if legend is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart2d_get_show_legend (LrgChart2D *self);

/**
 * lrg_chart2d_set_show_legend:
 * @self: an #LrgChart2D
 * @show: whether to show the legend
 *
 * Sets whether the legend is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_set_show_legend (LrgChart2D *self,
                             gboolean    show);

/**
 * lrg_chart2d_get_legend_position:
 * @self: an #LrgChart2D
 *
 * Gets the legend position.
 *
 * Returns: the legend position
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartLegendPosition
lrg_chart2d_get_legend_position (LrgChart2D *self);

/**
 * lrg_chart2d_set_legend_position:
 * @self: an #LrgChart2D
 * @position: the legend position
 *
 * Sets the legend position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_set_legend_position (LrgChart2D             *self,
                                 LrgChartLegendPosition  position);

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

/**
 * lrg_chart2d_data_to_screen:
 * @self: an #LrgChart2D
 * @data_x: X value in data coordinates
 * @data_y: Y value in data coordinates
 * @screen_x: (out): X value in screen coordinates
 * @screen_y: (out): Y value in screen coordinates
 *
 * Converts data coordinates to screen (pixel) coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_data_to_screen (LrgChart2D *self,
                            gdouble     data_x,
                            gdouble     data_y,
                            gfloat     *screen_x,
                            gfloat     *screen_y);

/**
 * lrg_chart2d_screen_to_data:
 * @self: an #LrgChart2D
 * @screen_x: X value in screen coordinates
 * @screen_y: Y value in screen coordinates
 * @data_x: (out): X value in data coordinates
 * @data_y: (out): Y value in data coordinates
 *
 * Converts screen (pixel) coordinates to data coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_screen_to_data (LrgChart2D *self,
                            gfloat      screen_x,
                            gfloat      screen_y,
                            gdouble    *data_x,
                            gdouble    *data_y);

/* ==========================================================================
 * Drawing Helper Wrappers
 * ========================================================================== */

/**
 * lrg_chart2d_draw_background:
 * @self: an #LrgChart2D
 *
 * Draws the chart background.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_draw_background (LrgChart2D *self);

/**
 * lrg_chart2d_draw_axes:
 * @self: an #LrgChart2D
 *
 * Draws the chart axes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_draw_axes (LrgChart2D *self);

/**
 * lrg_chart2d_draw_grid:
 * @self: an #LrgChart2D
 *
 * Draws the grid lines.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_draw_grid (LrgChart2D *self);

/**
 * lrg_chart2d_draw_data:
 * @self: an #LrgChart2D
 *
 * Draws the chart data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_draw_data (LrgChart2D *self);

/**
 * lrg_chart2d_draw_legend:
 * @self: an #LrgChart2D
 *
 * Draws the legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart2d_draw_legend (LrgChart2D *self);

G_END_DECLS
