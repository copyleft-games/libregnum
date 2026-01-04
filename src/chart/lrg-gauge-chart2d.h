/* lrg-gauge-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGaugeChart2D - 2D Gauge/Meter Chart widget.
 *
 * Renders a single value on a dial/meter display.
 * Supports needle, arc, and digital display styles.
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

#define LRG_TYPE_GAUGE_CHART2D (lrg_gauge_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGaugeChart2D, lrg_gauge_chart2d, LRG, GAUGE_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_new:
 *
 * Creates a new gauge chart with default settings.
 *
 * Returns: (transfer full): a new #LrgGaugeChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGaugeChart2D *
lrg_gauge_chart2d_new (void);

/**
 * lrg_gauge_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new gauge chart with specified size.
 *
 * Returns: (transfer full): a new #LrgGaugeChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGaugeChart2D *
lrg_gauge_chart2d_new_with_size (gfloat width,
                                 gfloat height);

/* ==========================================================================
 * Value
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_get_value:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the current gauge value.
 *
 * Returns: the current value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_gauge_chart2d_get_value (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_value:
 * @self: an #LrgGaugeChart2D
 * @value: the value to display
 *
 * Sets the gauge value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_value (LrgGaugeChart2D *self,
                             gdouble          value);

/**
 * lrg_gauge_chart2d_get_min_value:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the minimum value.
 *
 * Returns: minimum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_gauge_chart2d_get_min_value (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_min_value:
 * @self: an #LrgGaugeChart2D
 * @min: minimum value
 *
 * Sets the minimum value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_min_value (LrgGaugeChart2D *self,
                                 gdouble          min);

/**
 * lrg_gauge_chart2d_get_max_value:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the maximum value.
 *
 * Returns: maximum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_gauge_chart2d_get_max_value (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_max_value:
 * @self: an #LrgGaugeChart2D
 * @max: maximum value
 *
 * Sets the maximum value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_max_value (LrgGaugeChart2D *self,
                                 gdouble          max);

/* ==========================================================================
 * Style
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_get_style:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the gauge style.
 *
 * Returns: the gauge style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartGaugeStyle
lrg_gauge_chart2d_get_style (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_style:
 * @self: an #LrgGaugeChart2D
 * @style: the gauge style
 *
 * Sets the gauge style (needle, arc, or digital).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_style (LrgGaugeChart2D    *self,
                             LrgChartGaugeStyle  style);

/**
 * lrg_gauge_chart2d_get_start_angle:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the start angle in degrees.
 *
 * Returns: start angle (0 = right, 90 = bottom, etc.)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_gauge_chart2d_get_start_angle (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_start_angle:
 * @self: an #LrgGaugeChart2D
 * @angle: start angle in degrees
 *
 * Sets the start angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_start_angle (LrgGaugeChart2D *self,
                                   gfloat           angle);

/**
 * lrg_gauge_chart2d_get_sweep_angle:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the sweep angle in degrees.
 *
 * Returns: sweep angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_gauge_chart2d_get_sweep_angle (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_sweep_angle:
 * @self: an #LrgGaugeChart2D
 * @angle: sweep angle in degrees
 *
 * Sets the sweep angle (total arc covered).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_sweep_angle (LrgGaugeChart2D *self,
                                   gfloat           angle);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_get_needle_color:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the needle color.
 *
 * Returns: (transfer none): the needle color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_gauge_chart2d_get_needle_color (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_needle_color:
 * @self: an #LrgGaugeChart2D
 * @color: the needle color
 *
 * Sets the needle color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_needle_color (LrgGaugeChart2D *self,
                                    GrlColor        *color);

/**
 * lrg_gauge_chart2d_get_track_color:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the track (background arc) color.
 *
 * Returns: (transfer none): the track color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_gauge_chart2d_get_track_color (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_track_color:
 * @self: an #LrgGaugeChart2D
 * @color: the track color
 *
 * Sets the track (background arc) color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_track_color (LrgGaugeChart2D *self,
                                   GrlColor        *color);

/**
 * lrg_gauge_chart2d_get_fill_color:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the fill (value arc) color.
 *
 * Returns: (transfer none): the fill color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_gauge_chart2d_get_fill_color (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_fill_color:
 * @self: an #LrgGaugeChart2D
 * @color: the fill color
 *
 * Sets the fill (value arc) color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_fill_color (LrgGaugeChart2D *self,
                                  GrlColor        *color);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_get_arc_width:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the arc width for arc style.
 *
 * Returns: arc width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_gauge_chart2d_get_arc_width (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_arc_width:
 * @self: an #LrgGaugeChart2D
 * @width: arc width in pixels
 *
 * Sets the arc width for arc style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_arc_width (LrgGaugeChart2D *self,
                                 gfloat           width);

/**
 * lrg_gauge_chart2d_get_show_value:
 * @self: an #LrgGaugeChart2D
 *
 * Gets whether the numeric value is displayed.
 *
 * Returns: %TRUE if value is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gauge_chart2d_get_show_value (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_show_value:
 * @self: an #LrgGaugeChart2D
 * @show: whether to show value
 *
 * Sets whether to display the numeric value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_show_value (LrgGaugeChart2D *self,
                                  gboolean         show);

/**
 * lrg_gauge_chart2d_get_value_format:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the printf format for the value.
 *
 * Returns: (transfer none): the format string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_gauge_chart2d_get_value_format (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_value_format:
 * @self: an #LrgGaugeChart2D
 * @format: printf format string
 *
 * Sets the printf format for displaying the value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_value_format (LrgGaugeChart2D *self,
                                    const gchar     *format);

/**
 * lrg_gauge_chart2d_get_show_ticks:
 * @self: an #LrgGaugeChart2D
 *
 * Gets whether tick marks are shown.
 *
 * Returns: %TRUE if ticks are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_gauge_chart2d_get_show_ticks (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_show_ticks:
 * @self: an #LrgGaugeChart2D
 * @show: whether to show ticks
 *
 * Sets whether to display tick marks.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_show_ticks (LrgGaugeChart2D *self,
                                  gboolean         show);

/**
 * lrg_gauge_chart2d_get_tick_count:
 * @self: an #LrgGaugeChart2D
 *
 * Gets the number of major ticks.
 *
 * Returns: tick count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_gauge_chart2d_get_tick_count (LrgGaugeChart2D *self);

/**
 * lrg_gauge_chart2d_set_tick_count:
 * @self: an #LrgGaugeChart2D
 * @count: number of major ticks
 *
 * Sets the number of major tick marks.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_set_tick_count (LrgGaugeChart2D *self,
                                  guint            count);

/* ==========================================================================
 * Color Zones
 * ========================================================================== */

/**
 * lrg_gauge_chart2d_add_zone:
 * @self: an #LrgGaugeChart2D
 * @start: zone start value
 * @end: zone end value
 * @color: zone color
 *
 * Adds a colored zone to the gauge.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_add_zone (LrgGaugeChart2D *self,
                            gdouble          start,
                            gdouble          end,
                            GrlColor        *color);

/**
 * lrg_gauge_chart2d_clear_zones:
 * @self: an #LrgGaugeChart2D
 *
 * Removes all color zones.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_gauge_chart2d_clear_zones (LrgGaugeChart2D *self);

G_END_DECLS
