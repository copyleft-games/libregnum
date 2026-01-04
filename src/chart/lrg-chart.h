/* lrg-chart.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChart - Abstract base class for all chart types.
 *
 * LrgChart provides the common foundation for all chart widgets.
 * It extends LrgWidget and adds series management, interactivity
 * (hover, click), and animation support.
 *
 * Subclasses must implement the virtual methods for data layout
 * and hit testing. The intermediate classes LrgChart2D and LrgChart3D
 * provide additional functionality specific to 2D and 3D charting.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../ui/lrg-widget.h"
#include "lrg-chart-enums.h"
#include "lrg-chart-data-series.h"
#include "lrg-chart-hit-info.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART (lrg_chart_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgChart, lrg_chart, LRG, CHART, LrgWidget)

/**
 * LrgChartClass:
 * @parent_class: The parent class
 * @update_data: Virtual method to recalculate data bounds after series changes
 * @rebuild_layout: Virtual method to rebuild internal layout (axis positions, legend, etc.)
 * @hit_test: Virtual method to perform hit testing for interactivity
 * @calculate_bounds: Virtual method to calculate the chart's content bounds
 * @animate_to_data: Virtual method to animate to new data values
 *
 * The class structure for #LrgChart.
 *
 * Subclasses should implement these virtual methods to provide
 * chart-type-specific behavior. The @hit_test method is critical
 * for enabling interactivity (tooltips, click handling).
 */
struct _LrgChartClass
{
    LrgWidgetClass parent_class;

    /* Virtual methods */

    /**
     * LrgChartClass::update_data:
     * @self: the chart
     *
     * Called when series data changes. Subclasses should recalculate
     * data ranges, axis bounds, and any cached values.
     */
    void     (*update_data)      (LrgChart *self);

    /**
     * LrgChartClass::rebuild_layout:
     * @self: the chart
     *
     * Called when layout needs to be recalculated. This happens after
     * size changes, margin adjustments, or when series visibility changes.
     */
    void     (*rebuild_layout)   (LrgChart *self);

    /**
     * LrgChartClass::hit_test:
     * @self: the chart
     * @x: X coordinate in widget space
     * @y: Y coordinate in widget space
     * @out_hit: (out): location to store hit information
     *
     * Performs hit testing to determine which chart element (if any)
     * is at the given coordinates. Used for hover and click handling.
     *
     * Returns: %TRUE if something was hit
     */
    gboolean (*hit_test)         (LrgChart        *self,
                                  gfloat           x,
                                  gfloat           y,
                                  LrgChartHitInfo *out_hit);

    /**
     * LrgChartClass::calculate_bounds:
     * @self: the chart
     *
     * Calculates the bounds of the chart's content area (excluding
     * margins, axis labels, legend, etc.).
     */
    void     (*calculate_bounds) (LrgChart *self);

    /**
     * LrgChartClass::animate_to_data:
     * @self: the chart
     * @animation_type: the type of animation
     * @duration: animation duration in seconds
     *
     * Starts an animation to transition to new data values.
     * Subclasses should implement appropriate visual transitions.
     */
    void     (*animate_to_data)  (LrgChart              *self,
                                  LrgChartAnimationType  animation_type,
                                  gfloat                 duration);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/* LrgChart is abstract - use concrete subclasses */

/* ==========================================================================
 * Title
 * ========================================================================== */

/**
 * lrg_chart_get_title:
 * @self: an #LrgChart
 *
 * Gets the chart title.
 *
 * Returns: (transfer none) (nullable): the title
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_get_title (LrgChart *self);

/**
 * lrg_chart_set_title:
 * @self: an #LrgChart
 * @title: (nullable): the title
 *
 * Sets the chart title displayed above the chart.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_title (LrgChart    *self,
                     const gchar *title);

/* ==========================================================================
 * Series Management
 * ========================================================================== */

/**
 * lrg_chart_get_series_count:
 * @self: an #LrgChart
 *
 * Gets the number of data series.
 *
 * Returns: the series count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_get_series_count (LrgChart *self);

/**
 * lrg_chart_get_series:
 * @self: an #LrgChart
 * @index: series index
 *
 * Gets a data series by index.
 *
 * Returns: (transfer none) (nullable): the #LrgChartDataSeries
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataSeries *
lrg_chart_get_series (LrgChart *self,
                      guint     index);

/**
 * lrg_chart_get_series_list:
 * @self: an #LrgChart
 *
 * Gets the array of all data series.
 *
 * Returns: (transfer none) (element-type LrgChartDataSeries): the series array
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_chart_get_series_list (LrgChart *self);

/**
 * lrg_chart_add_series:
 * @self: an #LrgChart
 * @series: (transfer full): the series to add
 *
 * Adds a data series to the chart. The chart takes ownership.
 *
 * Returns: the index of the added series
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_add_series (LrgChart           *self,
                      LrgChartDataSeries *series);

/**
 * lrg_chart_remove_series:
 * @self: an #LrgChart
 * @index: index of the series to remove
 *
 * Removes a data series from the chart.
 *
 * Returns: %TRUE if the series was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_remove_series (LrgChart *self,
                         guint     index);

/**
 * lrg_chart_clear_series:
 * @self: an #LrgChart
 *
 * Removes all data series from the chart.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_clear_series (LrgChart *self);

/* ==========================================================================
 * Margins and Padding
 * ========================================================================== */

/**
 * lrg_chart_get_margin_top:
 * @self: an #LrgChart
 *
 * Gets the top margin.
 *
 * Returns: the top margin in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_margin_top (LrgChart *self);

/**
 * lrg_chart_get_margin_right:
 * @self: an #LrgChart
 *
 * Gets the right margin.
 *
 * Returns: the right margin in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_margin_right (LrgChart *self);

/**
 * lrg_chart_get_margin_bottom:
 * @self: an #LrgChart
 *
 * Gets the bottom margin.
 *
 * Returns: the bottom margin in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_margin_bottom (LrgChart *self);

/**
 * lrg_chart_get_margin_left:
 * @self: an #LrgChart
 *
 * Gets the left margin.
 *
 * Returns: the left margin in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_margin_left (LrgChart *self);

/**
 * lrg_chart_set_margins:
 * @self: an #LrgChart
 * @top: top margin
 * @right: right margin
 * @bottom: bottom margin
 * @left: left margin
 *
 * Sets all margins at once.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_margins (LrgChart *self,
                       gfloat    top,
                       gfloat    right,
                       gfloat    bottom,
                       gfloat    left);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_chart_get_background_color:
 * @self: an #LrgChart
 *
 * Gets the chart background color.
 *
 * Returns: (transfer none): the background color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_get_background_color (LrgChart *self);

/**
 * lrg_chart_set_background_color:
 * @self: an #LrgChart
 * @color: the background color
 *
 * Sets the chart background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_background_color (LrgChart       *self,
                                const GrlColor *color);

/**
 * lrg_chart_get_text_color:
 * @self: an #LrgChart
 *
 * Gets the default text color for labels.
 *
 * Returns: (transfer none): the text color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_get_text_color (LrgChart *self);

/**
 * lrg_chart_set_text_color:
 * @self: an #LrgChart
 * @color: the text color
 *
 * Sets the default text color for labels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_text_color (LrgChart       *self,
                          const GrlColor *color);

/* ==========================================================================
 * Animation
 * ========================================================================== */

/**
 * lrg_chart_get_animation_type:
 * @self: an #LrgChart
 *
 * Gets the default animation type.
 *
 * Returns: the animation type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAnimationType
lrg_chart_get_animation_type (LrgChart *self);

/**
 * lrg_chart_set_animation_type:
 * @self: an #LrgChart
 * @type: the animation type
 *
 * Sets the default animation type for data transitions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_animation_type (LrgChart              *self,
                              LrgChartAnimationType  type);

/**
 * lrg_chart_get_animation_duration:
 * @self: an #LrgChart
 *
 * Gets the animation duration.
 *
 * Returns: duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_animation_duration (LrgChart *self);

/**
 * lrg_chart_set_animation_duration:
 * @self: an #LrgChart
 * @duration: duration in seconds
 *
 * Sets the animation duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_animation_duration (LrgChart *self,
                                  gfloat    duration);

/**
 * lrg_chart_get_animation_progress:
 * @self: an #LrgChart
 *
 * Gets the current animation progress (0.0 to 1.0).
 *
 * Returns: the animation progress
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_get_animation_progress (LrgChart *self);

/* ==========================================================================
 * Interactivity
 * ========================================================================== */

/**
 * lrg_chart_get_hover_enabled:
 * @self: an #LrgChart
 *
 * Gets whether hover highlighting is enabled.
 *
 * Returns: %TRUE if hover is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_get_hover_enabled (LrgChart *self);

/**
 * lrg_chart_set_hover_enabled:
 * @self: an #LrgChart
 * @enabled: whether to enable hover
 *
 * Sets whether hover highlighting is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_set_hover_enabled (LrgChart *self,
                             gboolean  enabled);

/**
 * lrg_chart_get_current_hover:
 * @self: an #LrgChart
 *
 * Gets the currently hovered element info.
 *
 * Returns: (transfer none) (nullable): the hover info, or %NULL if nothing is hovered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgChartHitInfo *
lrg_chart_get_current_hover (LrgChart *self);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_chart_update_data:
 * @self: an #LrgChart
 *
 * Triggers a data update by calling the virtual update_data() method.
 * Call this after modifying series data programmatically.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_update_data (LrgChart *self);

/**
 * lrg_chart_rebuild_layout:
 * @self: an #LrgChart
 *
 * Triggers a layout rebuild by calling the virtual rebuild_layout() method.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_rebuild_layout (LrgChart *self);

/**
 * lrg_chart_hit_test:
 * @self: an #LrgChart
 * @x: X coordinate in widget space
 * @y: Y coordinate in widget space
 * @out_hit: (out): location to store hit information
 *
 * Performs hit testing by calling the virtual hit_test() method.
 *
 * Returns: %TRUE if something was hit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_hit_test (LrgChart        *self,
                    gfloat           x,
                    gfloat           y,
                    LrgChartHitInfo *out_hit);

/**
 * lrg_chart_animate_to_data:
 * @self: an #LrgChart
 * @animation_type: the type of animation
 * @duration: animation duration in seconds
 *
 * Starts an animation to transition to new data values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_animate_to_data (LrgChart              *self,
                           LrgChartAnimationType  animation_type,
                           gfloat                 duration);

/* ==========================================================================
 * Content Bounds
 * ========================================================================== */

/**
 * lrg_chart_get_content_bounds:
 * @self: an #LrgChart
 * @out_bounds: (out): location to store the bounds
 *
 * Gets the bounds of the chart's content area (the area where
 * data is rendered, excluding margins, axes, legend, etc.).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_get_content_bounds (LrgChart     *self,
                              GrlRectangle *out_bounds);

G_END_DECLS
