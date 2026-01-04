/* lrg-chart-legend.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartLegend - Legend display for chart series.
 *
 * Displays a legend showing series names with their colors/markers.
 * Can be positioned at various locations around the chart.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart-enums.h"
#include "lrg-chart-data-series.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_LEGEND (lrg_chart_legend_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgChartLegend, lrg_chart_legend, LRG, CHART_LEGEND, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_legend_new:
 *
 * Creates a new chart legend with default settings.
 *
 * Returns: (transfer full): a new #LrgChartLegend
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartLegend *
lrg_chart_legend_new (void);

/* ==========================================================================
 * Visibility
 * ========================================================================== */

/**
 * lrg_chart_legend_get_visible:
 * @self: an #LrgChartLegend
 *
 * Gets whether the legend is visible.
 *
 * Returns: %TRUE if visible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_legend_get_visible (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_visible:
 * @self: an #LrgChartLegend
 * @visible: whether legend is visible
 *
 * Sets legend visibility.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_visible (LrgChartLegend *self,
                              gboolean        visible);

/* ==========================================================================
 * Position
 * ========================================================================== */

/**
 * lrg_chart_legend_get_position:
 * @self: an #LrgChartLegend
 *
 * Gets the legend position.
 *
 * Returns: the legend position
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLegendPosition
lrg_chart_legend_get_position (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_position:
 * @self: an #LrgChartLegend
 * @position: the legend position
 *
 * Sets the legend position relative to the chart.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_position (LrgChartLegend    *self,
                               LrgLegendPosition  position);

/**
 * lrg_chart_legend_get_orientation:
 * @self: an #LrgChartLegend
 *
 * Gets the legend orientation.
 *
 * Returns: the legend orientation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLegendOrientation
lrg_chart_legend_get_orientation (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_orientation:
 * @self: an #LrgChartLegend
 * @orientation: the legend orientation
 *
 * Sets whether legend items are arranged horizontally or vertically.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_orientation (LrgChartLegend       *self,
                                  LrgLegendOrientation  orientation);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_chart_legend_get_background_color:
 * @self: an #LrgChartLegend
 *
 * Gets the background color.
 *
 * Returns: (transfer none): the background color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_legend_get_background_color (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_background_color:
 * @self: an #LrgChartLegend
 * @color: (nullable): the background color
 *
 * Sets the background color. Use %NULL for transparent.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_background_color (LrgChartLegend *self,
                                       GrlColor       *color);

/**
 * lrg_chart_legend_get_text_color:
 * @self: an #LrgChartLegend
 *
 * Gets the text color.
 *
 * Returns: (transfer none): the text color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_legend_get_text_color (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_text_color:
 * @self: an #LrgChartLegend
 * @color: the text color
 *
 * Sets the text color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_text_color (LrgChartLegend *self,
                                 GrlColor       *color);

/**
 * lrg_chart_legend_get_border_color:
 * @self: an #LrgChartLegend
 *
 * Gets the border color.
 *
 * Returns: (transfer none): the border color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_chart_legend_get_border_color (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_border_color:
 * @self: an #LrgChartLegend
 * @color: (nullable): the border color
 *
 * Sets the border color. Use %NULL for no border.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_border_color (LrgChartLegend *self,
                                   GrlColor       *color);

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

/**
 * lrg_chart_legend_get_padding:
 * @self: an #LrgChartLegend
 *
 * Gets the internal padding.
 *
 * Returns: padding in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_legend_get_padding (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_padding:
 * @self: an #LrgChartLegend
 * @padding: padding in pixels
 *
 * Sets the internal padding.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_padding (LrgChartLegend *self,
                              gfloat          padding);

/**
 * lrg_chart_legend_get_item_spacing:
 * @self: an #LrgChartLegend
 *
 * Gets the spacing between legend items.
 *
 * Returns: spacing in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_legend_get_item_spacing (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_item_spacing:
 * @self: an #LrgChartLegend
 * @spacing: spacing in pixels
 *
 * Sets the spacing between legend items.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_item_spacing (LrgChartLegend *self,
                                   gfloat          spacing);

/**
 * lrg_chart_legend_get_symbol_size:
 * @self: an #LrgChartLegend
 *
 * Gets the size of legend symbols (color boxes/markers).
 *
 * Returns: symbol size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_legend_get_symbol_size (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_symbol_size:
 * @self: an #LrgChartLegend
 * @size: symbol size in pixels
 *
 * Sets the size of legend symbols.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_symbol_size (LrgChartLegend *self,
                                  gfloat          size);

/**
 * lrg_chart_legend_get_symbol_spacing:
 * @self: an #LrgChartLegend
 *
 * Gets the spacing between symbol and text.
 *
 * Returns: spacing in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_legend_get_symbol_spacing (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_symbol_spacing:
 * @self: an #LrgChartLegend
 * @spacing: spacing in pixels
 *
 * Sets the spacing between symbol and text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_symbol_spacing (LrgChartLegend *self,
                                     gfloat          spacing);

/**
 * lrg_chart_legend_get_border_width:
 * @self: an #LrgChartLegend
 *
 * Gets the border width.
 *
 * Returns: border width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_legend_get_border_width (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_border_width:
 * @self: an #LrgChartLegend
 * @width: border width in pixels
 *
 * Sets the border width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_border_width (LrgChartLegend *self,
                                   gfloat          width);

/* ==========================================================================
 * Text Settings
 * ========================================================================== */

/**
 * lrg_chart_legend_get_font_size:
 * @self: an #LrgChartLegend
 *
 * Gets the font size.
 *
 * Returns: font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_chart_legend_get_font_size (LrgChartLegend *self);

/**
 * lrg_chart_legend_set_font_size:
 * @self: an #LrgChartLegend
 * @size: font size in pixels
 *
 * Sets the font size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_set_font_size (LrgChartLegend *self,
                                gint            size);

/* ==========================================================================
 * Operations
 * ========================================================================== */

/**
 * lrg_chart_legend_measure:
 * @self: an #LrgChartLegend
 * @series: (element-type LrgChartDataSeries): array of series to measure
 * @out_width: (out): legend width
 * @out_height: (out): legend height
 *
 * Measures the size needed for the legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_measure (LrgChartLegend *self,
                          GPtrArray      *series,
                          gfloat         *out_width,
                          gfloat         *out_height);

/**
 * lrg_chart_legend_draw:
 * @self: an #LrgChartLegend
 * @series: (element-type LrgChartDataSeries): array of series to draw
 * @x: X position
 * @y: Y position
 *
 * Draws the legend at the given position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_legend_draw (LrgChartLegend *self,
                       GPtrArray      *series,
                       gfloat          x,
                       gfloat          y);

/**
 * lrg_chart_legend_hit_test:
 * @self: an #LrgChartLegend
 * @series: (element-type LrgChartDataSeries): array of series
 * @legend_x: legend X position
 * @legend_y: legend Y position
 * @test_x: X coordinate to test
 * @test_y: Y coordinate to test
 *
 * Tests if a point is over a legend item.
 *
 * Returns: index of hit series or -1 if none
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_chart_legend_hit_test (LrgChartLegend *self,
                           GPtrArray      *series,
                           gfloat          legend_x,
                           gfloat          legend_y,
                           gfloat          test_x,
                           gfloat          test_y);

G_END_DECLS
