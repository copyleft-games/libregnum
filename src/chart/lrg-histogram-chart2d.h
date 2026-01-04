/* lrg-histogram-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHistogramChart2D - 2D Histogram Chart widget.
 *
 * Renders a frequency distribution of data values using bins.
 * Data points' Y values are binned into ranges, X is ignored for binning
 * but can be used as weight if weighted mode is enabled.
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

#define LRG_TYPE_HISTOGRAM_CHART2D (lrg_histogram_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHistogramChart2D, lrg_histogram_chart2d, LRG, HISTOGRAM_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_new:
 *
 * Creates a new histogram chart with default settings.
 *
 * Returns: (transfer full): a new #LrgHistogramChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHistogramChart2D *
lrg_histogram_chart2d_new (void);

/**
 * lrg_histogram_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new histogram chart with specified size.
 *
 * Returns: (transfer full): a new #LrgHistogramChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHistogramChart2D *
lrg_histogram_chart2d_new_with_size (gfloat width,
                                     gfloat height);

/* ==========================================================================
 * Binning Configuration
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_get_bin_count:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the number of bins.
 *
 * Returns: number of bins (0 for auto)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_histogram_chart2d_get_bin_count (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_bin_count:
 * @self: an #LrgHistogramChart2D
 * @count: number of bins (0 for auto)
 *
 * Sets the number of bins. Use 0 for automatic binning
 * using Sturges' formula.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_bin_count (LrgHistogramChart2D *self,
                                     guint                count);

/**
 * lrg_histogram_chart2d_get_bin_width:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the fixed bin width.
 *
 * Returns: bin width (0 for auto)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_histogram_chart2d_get_bin_width (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_bin_width:
 * @self: an #LrgHistogramChart2D
 * @width: bin width (0 for auto)
 *
 * Sets a fixed bin width. Use 0 to calculate from bin count.
 * If both bin_count and bin_width are set, bin_width takes precedence.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_bin_width (LrgHistogramChart2D *self,
                                     gdouble              width);

/**
 * lrg_histogram_chart2d_get_range_min:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the minimum value for binning range.
 *
 * Returns: minimum value (NAN for auto)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_histogram_chart2d_get_range_min (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_range_min:
 * @self: an #LrgHistogramChart2D
 * @min: minimum value (NAN for auto)
 *
 * Sets the minimum value for binning range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_range_min (LrgHistogramChart2D *self,
                                     gdouble              min);

/**
 * lrg_histogram_chart2d_get_range_max:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the maximum value for binning range.
 *
 * Returns: maximum value (NAN for auto)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_histogram_chart2d_get_range_max (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_range_max:
 * @self: an #LrgHistogramChart2D
 * @max: maximum value (NAN for auto)
 *
 * Sets the maximum value for binning range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_range_max (LrgHistogramChart2D *self,
                                     gdouble              max);

/* ==========================================================================
 * Display Mode
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_get_density:
 * @self: an #LrgHistogramChart2D
 *
 * Gets whether the histogram shows density instead of frequency.
 *
 * Returns: %TRUE if density mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_histogram_chart2d_get_density (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_density:
 * @self: an #LrgHistogramChart2D
 * @density: whether to show density
 *
 * Sets whether to show probability density (normalized so area = 1)
 * instead of raw frequency counts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_density (LrgHistogramChart2D *self,
                                   gboolean             density);

/**
 * lrg_histogram_chart2d_get_cumulative:
 * @self: an #LrgHistogramChart2D
 *
 * Gets whether cumulative distribution is shown.
 *
 * Returns: %TRUE if cumulative
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_histogram_chart2d_get_cumulative (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_cumulative:
 * @self: an #LrgHistogramChart2D
 * @cumulative: whether to show cumulative distribution
 *
 * Sets whether to show cumulative distribution.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_cumulative (LrgHistogramChart2D *self,
                                      gboolean             cumulative);

/* ==========================================================================
 * Style
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_get_bar_color:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the bar fill color.
 *
 * Returns: (transfer none): the bar color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_histogram_chart2d_get_bar_color (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_bar_color:
 * @self: an #LrgHistogramChart2D
 * @color: the bar color
 *
 * Sets the bar fill color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_bar_color (LrgHistogramChart2D *self,
                                     GrlColor            *color);

/**
 * lrg_histogram_chart2d_get_border_color:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the bar border color.
 *
 * Returns: (transfer none): the border color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_histogram_chart2d_get_border_color (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_border_color:
 * @self: an #LrgHistogramChart2D
 * @color: the border color
 *
 * Sets the bar border color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_border_color (LrgHistogramChart2D *self,
                                        GrlColor            *color);

/**
 * lrg_histogram_chart2d_get_border_width:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the bar border width.
 *
 * Returns: border width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_histogram_chart2d_get_border_width (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_border_width:
 * @self: an #LrgHistogramChart2D
 * @width: border width in pixels
 *
 * Sets the bar border width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_border_width (LrgHistogramChart2D *self,
                                        gfloat               width);

/**
 * lrg_histogram_chart2d_get_bar_spacing:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the spacing between bars.
 *
 * Returns: spacing as fraction of bar width (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_histogram_chart2d_get_bar_spacing (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_bar_spacing:
 * @self: an #LrgHistogramChart2D
 * @spacing: spacing as fraction of bar width (0.0 to 1.0)
 *
 * Sets the spacing between bars. Use 0 for adjacent bars (typical histogram).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_bar_spacing (LrgHistogramChart2D *self,
                                       gfloat               spacing);

/* ==========================================================================
 * Cumulative Line
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_get_show_cumulative_line:
 * @self: an #LrgHistogramChart2D
 *
 * Gets whether the cumulative distribution line is shown.
 *
 * Returns: %TRUE if line is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_histogram_chart2d_get_show_cumulative_line (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_show_cumulative_line:
 * @self: an #LrgHistogramChart2D
 * @show: whether to show cumulative line
 *
 * Sets whether to overlay a cumulative distribution line.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_show_cumulative_line (LrgHistogramChart2D *self,
                                                gboolean             show);

/**
 * lrg_histogram_chart2d_get_cumulative_line_color:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the cumulative line color.
 *
 * Returns: (transfer none): the line color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_histogram_chart2d_get_cumulative_line_color (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_cumulative_line_color:
 * @self: an #LrgHistogramChart2D
 * @color: the line color
 *
 * Sets the cumulative distribution line color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_cumulative_line_color (LrgHistogramChart2D *self,
                                                 GrlColor            *color);

/**
 * lrg_histogram_chart2d_get_cumulative_line_width:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the cumulative line width.
 *
 * Returns: line width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_histogram_chart2d_get_cumulative_line_width (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_set_cumulative_line_width:
 * @self: an #LrgHistogramChart2D
 * @width: line width in pixels
 *
 * Sets the cumulative distribution line width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_set_cumulative_line_width (LrgHistogramChart2D *self,
                                                 gfloat               width);

/* ==========================================================================
 * Bin Information (computed)
 * ========================================================================== */

/**
 * lrg_histogram_chart2d_get_computed_bin_count:
 * @self: an #LrgHistogramChart2D
 *
 * Gets the actual number of bins after computation.
 *
 * Returns: computed bin count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_histogram_chart2d_get_computed_bin_count (LrgHistogramChart2D *self);

/**
 * lrg_histogram_chart2d_get_bin_frequency:
 * @self: an #LrgHistogramChart2D
 * @bin_index: bin index
 *
 * Gets the frequency (count) for a specific bin.
 *
 * Returns: frequency count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_histogram_chart2d_get_bin_frequency (LrgHistogramChart2D *self,
                                         guint                bin_index);

/**
 * lrg_histogram_chart2d_get_bin_range:
 * @self: an #LrgHistogramChart2D
 * @bin_index: bin index
 * @out_min: (out): bin minimum value
 * @out_max: (out): bin maximum value
 *
 * Gets the value range for a specific bin.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_get_bin_range (LrgHistogramChart2D *self,
                                     guint                bin_index,
                                     gdouble             *out_min,
                                     gdouble             *out_max);

/**
 * lrg_histogram_chart2d_recalculate:
 * @self: an #LrgHistogramChart2D
 *
 * Forces recalculation of bins from data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_histogram_chart2d_recalculate (LrgHistogramChart2D *self);

G_END_DECLS
