/* lrg-candlestick-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCandlestickChart2D - 2D Candlestick Chart widget.
 *
 * Renders OHLC (Open, High, Low, Close) financial data as candlesticks.
 * Uses X for time, Y for open, Z for close, W for high (low derived).
 * Alternative: use LrgChartDataPoint's label to store "open,high,low,close".
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

#define LRG_TYPE_CANDLESTICK_CHART2D (lrg_candlestick_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCandlestickChart2D, lrg_candlestick_chart2d, LRG, CANDLESTICK_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_candlestick_chart2d_new:
 *
 * Creates a new candlestick chart with default settings.
 *
 * Returns: (transfer full): a new #LrgCandlestickChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCandlestickChart2D *
lrg_candlestick_chart2d_new (void);

/**
 * lrg_candlestick_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new candlestick chart with specified size.
 *
 * Returns: (transfer full): a new #LrgCandlestickChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCandlestickChart2D *
lrg_candlestick_chart2d_new_with_size (gfloat width,
                                       gfloat height);

/* ==========================================================================
 * Candlestick Style
 * ========================================================================== */

/**
 * lrg_candlestick_chart2d_get_up_color:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the color for up (bullish) candles.
 *
 * Returns: (transfer none): the up color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_candlestick_chart2d_get_up_color (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_up_color:
 * @self: an #LrgCandlestickChart2D
 * @color: the up color
 *
 * Sets the color for up (bullish) candles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_up_color (LrgCandlestickChart2D *self,
                                      GrlColor              *color);

/**
 * lrg_candlestick_chart2d_get_down_color:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the color for down (bearish) candles.
 *
 * Returns: (transfer none): the down color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_candlestick_chart2d_get_down_color (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_down_color:
 * @self: an #LrgCandlestickChart2D
 * @color: the down color
 *
 * Sets the color for down (bearish) candles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_down_color (LrgCandlestickChart2D *self,
                                        GrlColor              *color);

/**
 * lrg_candlestick_chart2d_get_candle_width:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the candle body width.
 *
 * Returns: width as fraction of available space (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_candlestick_chart2d_get_candle_width (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_candle_width:
 * @self: an #LrgCandlestickChart2D
 * @width: width as fraction of available space (0.0 to 1.0)
 *
 * Sets the candle body width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_candle_width (LrgCandlestickChart2D *self,
                                          gfloat                 width);

/**
 * lrg_candlestick_chart2d_get_wick_width:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the wick (shadow) width.
 *
 * Returns: wick width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_candlestick_chart2d_get_wick_width (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_wick_width:
 * @self: an #LrgCandlestickChart2D
 * @width: wick width in pixels
 *
 * Sets the wick (shadow) width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_wick_width (LrgCandlestickChart2D *self,
                                        gfloat                 width);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_candlestick_chart2d_get_filled_candles:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets whether candle bodies are filled.
 *
 * Returns: %TRUE if filled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_candlestick_chart2d_get_filled_candles (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_filled_candles:
 * @self: an #LrgCandlestickChart2D
 * @filled: whether to fill candle bodies
 *
 * Sets whether candle bodies are filled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_filled_candles (LrgCandlestickChart2D *self,
                                            gboolean               filled);

/**
 * lrg_candlestick_chart2d_get_hollow_up:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets whether up candles are hollow.
 *
 * Returns: %TRUE if up candles are hollow
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_candlestick_chart2d_get_hollow_up (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_hollow_up:
 * @self: an #LrgCandlestickChart2D
 * @hollow: whether up candles are hollow
 *
 * Sets whether up candles are drawn hollow (outline only).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_hollow_up (LrgCandlestickChart2D *self,
                                       gboolean               hollow);

/* ==========================================================================
 * Volume Bars (optional)
 * ========================================================================== */

/**
 * lrg_candlestick_chart2d_get_show_volume:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets whether volume bars are shown.
 *
 * Returns: %TRUE if volume is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_candlestick_chart2d_get_show_volume (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_show_volume:
 * @self: an #LrgCandlestickChart2D
 * @show: whether to show volume
 *
 * Sets whether to show volume bars below the chart.
 * Uses the W value of data points as volume.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_show_volume (LrgCandlestickChart2D *self,
                                         gboolean               show);

/**
 * lrg_candlestick_chart2d_get_volume_height:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the height of the volume area.
 *
 * Returns: height as fraction of chart (0.0 to 0.5)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_candlestick_chart2d_get_volume_height (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_volume_height:
 * @self: an #LrgCandlestickChart2D
 * @height: height as fraction of chart (0.0 to 0.5)
 *
 * Sets the height of the volume area.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_volume_height (LrgCandlestickChart2D *self,
                                           gfloat                 height);

/* ==========================================================================
 * Hit Testing
 * ========================================================================== */

/**
 * lrg_candlestick_chart2d_get_hit_tolerance:
 * @self: an #LrgCandlestickChart2D
 *
 * Gets the hit tolerance for selecting candles.
 *
 * Returns: tolerance in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_candlestick_chart2d_get_hit_tolerance (LrgCandlestickChart2D *self);

/**
 * lrg_candlestick_chart2d_set_hit_tolerance:
 * @self: an #LrgCandlestickChart2D
 * @tolerance: tolerance in pixels
 *
 * Sets the hit tolerance for selecting candles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_candlestick_chart2d_set_hit_tolerance (LrgCandlestickChart2D *self,
                                           gfloat                 tolerance);

G_END_DECLS
