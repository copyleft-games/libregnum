/* lrg-chart-color-scale.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartColorScale - Maps numeric values to colors.
 *
 * Used primarily by heatmap charts to convert data values into
 * visual colors. Supports color stops with linear interpolation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_COLOR_SCALE (lrg_chart_color_scale_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgChartColorScale, lrg_chart_color_scale, LRG, CHART_COLOR_SCALE, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_color_scale_new:
 *
 * Creates a new empty color scale.
 *
 * Returns: (transfer full): a new #LrgChartColorScale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_chart_color_scale_new (void);

/**
 * lrg_chart_color_scale_new_gradient:
 * @min_color: color for minimum value
 * @max_color: color for maximum value
 *
 * Creates a new two-color gradient scale.
 *
 * Returns: (transfer full): a new #LrgChartColorScale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_chart_color_scale_new_gradient (GrlColor *min_color,
                                    GrlColor *max_color);

/**
 * lrg_chart_color_scale_new_heat:
 *
 * Creates a preset heat color scale (blue -> cyan -> green -> yellow -> red).
 *
 * Returns: (transfer full): a new #LrgChartColorScale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_chart_color_scale_new_heat (void);

/**
 * lrg_chart_color_scale_new_cool:
 *
 * Creates a preset cool color scale (purple -> blue -> cyan).
 *
 * Returns: (transfer full): a new #LrgChartColorScale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_chart_color_scale_new_cool (void);

/**
 * lrg_chart_color_scale_new_viridis:
 *
 * Creates a viridis-inspired color scale (purple -> teal -> yellow).
 *
 * Returns: (transfer full): a new #LrgChartColorScale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_chart_color_scale_new_viridis (void);

/* ==========================================================================
 * Color Stops
 * ========================================================================== */

/**
 * lrg_chart_color_scale_add_stop:
 * @self: an #LrgChartColorScale
 * @position: stop position (0.0 to 1.0)
 * @color: color at this position
 *
 * Adds a color stop at the specified position.
 * Stops are automatically sorted by position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_add_stop (LrgChartColorScale *self,
                                gdouble             position,
                                GrlColor           *color);

/**
 * lrg_chart_color_scale_clear_stops:
 * @self: an #LrgChartColorScale
 *
 * Removes all color stops.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_clear_stops (LrgChartColorScale *self);

/**
 * lrg_chart_color_scale_get_stop_count:
 * @self: an #LrgChartColorScale
 *
 * Gets the number of color stops.
 *
 * Returns: number of stops
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_chart_color_scale_get_stop_count (LrgChartColorScale *self);

/* ==========================================================================
 * Value Range
 * ========================================================================== */

/**
 * lrg_chart_color_scale_get_min_value:
 * @self: an #LrgChartColorScale
 *
 * Gets the minimum data value.
 *
 * Returns: minimum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_color_scale_get_min_value (LrgChartColorScale *self);

/**
 * lrg_chart_color_scale_set_min_value:
 * @self: an #LrgChartColorScale
 * @min: minimum value
 *
 * Sets the minimum data value (maps to position 0.0).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_set_min_value (LrgChartColorScale *self,
                                     gdouble             min);

/**
 * lrg_chart_color_scale_get_max_value:
 * @self: an #LrgChartColorScale
 *
 * Gets the maximum data value.
 *
 * Returns: maximum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_color_scale_get_max_value (LrgChartColorScale *self);

/**
 * lrg_chart_color_scale_set_max_value:
 * @self: an #LrgChartColorScale
 * @max: maximum value
 *
 * Sets the maximum data value (maps to position 1.0).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_set_max_value (LrgChartColorScale *self,
                                     gdouble             max);

/**
 * lrg_chart_color_scale_set_range:
 * @self: an #LrgChartColorScale
 * @min: minimum value
 * @max: maximum value
 *
 * Sets both minimum and maximum data values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_set_range (LrgChartColorScale *self,
                                 gdouble             min,
                                 gdouble             max);

/* ==========================================================================
 * Color Mapping
 * ========================================================================== */

/**
 * lrg_chart_color_scale_get_color:
 * @self: an #LrgChartColorScale
 * @value: the data value to map
 *
 * Gets the interpolated color for a data value.
 * Values are clamped to the min/max range.
 *
 * Returns: (transfer full): the interpolated color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_color_scale_get_color (LrgChartColorScale *self,
                                 gdouble             value);

/**
 * lrg_chart_color_scale_get_color_at:
 * @self: an #LrgChartColorScale
 * @position: normalized position (0.0 to 1.0)
 *
 * Gets the interpolated color at a normalized position.
 *
 * Returns: (transfer full): the interpolated color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_color_scale_get_color_at (LrgChartColorScale *self,
                                    gdouble             position);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_chart_color_scale_get_discrete:
 * @self: an #LrgChartColorScale
 *
 * Gets whether the scale uses discrete steps instead of interpolation.
 *
 * Returns: %TRUE if discrete
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_color_scale_get_discrete (LrgChartColorScale *self);

/**
 * lrg_chart_color_scale_set_discrete:
 * @self: an #LrgChartColorScale
 * @discrete: whether to use discrete steps
 *
 * Sets whether the scale uses discrete steps instead of smooth interpolation.
 * When discrete, values snap to the nearest color stop instead of blending.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_color_scale_set_discrete (LrgChartColorScale *self,
                                    gboolean            discrete);

G_END_DECLS
