/* lrg-scatter-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScatterChart2D - 2D Scatter Plot widget.
 *
 * Renders data as individual points with various marker styles.
 * Supports bubble charts (variable point size) and optional trend lines.
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

#define LRG_TYPE_SCATTER_CHART2D (lrg_scatter_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScatterChart2D, lrg_scatter_chart2d, LRG, SCATTER_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_scatter_chart2d_new:
 *
 * Creates a new scatter chart with default settings.
 *
 * Returns: (transfer full): a new #LrgScatterChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScatterChart2D *
lrg_scatter_chart2d_new (void);

/**
 * lrg_scatter_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new scatter chart with specified size.
 *
 * Returns: (transfer full): a new #LrgScatterChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScatterChart2D *
lrg_scatter_chart2d_new_with_size (gfloat width,
                                   gfloat height);

/* ==========================================================================
 * Marker Configuration
 * ========================================================================== */

/**
 * lrg_scatter_chart2d_get_default_marker:
 * @self: an #LrgScatterChart2D
 *
 * Gets the default marker style.
 *
 * Returns: the marker style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartMarker
lrg_scatter_chart2d_get_default_marker (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_default_marker:
 * @self: an #LrgScatterChart2D
 * @marker: the marker style
 *
 * Sets the default marker style.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_default_marker (LrgScatterChart2D *self,
                                        LrgChartMarker     marker);

/**
 * lrg_scatter_chart2d_get_marker_size:
 * @self: an #LrgScatterChart2D
 *
 * Gets the default marker size.
 *
 * Returns: marker size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_marker_size (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_marker_size:
 * @self: an #LrgScatterChart2D
 * @size: marker size in pixels
 *
 * Sets the default marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_marker_size (LrgScatterChart2D *self,
                                     gfloat             size);

/**
 * lrg_scatter_chart2d_get_marker_opacity:
 * @self: an #LrgScatterChart2D
 *
 * Gets the marker opacity.
 *
 * Returns: opacity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_marker_opacity (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_marker_opacity:
 * @self: an #LrgScatterChart2D
 * @opacity: opacity (0.0 to 1.0)
 *
 * Sets the marker opacity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_marker_opacity (LrgScatterChart2D *self,
                                        gfloat             opacity);

/* ==========================================================================
 * Bubble Mode
 * ========================================================================== */

/**
 * lrg_scatter_chart2d_get_bubble_mode:
 * @self: an #LrgScatterChart2D
 *
 * Gets whether bubble mode is enabled.
 * In bubble mode, the Z value of data points controls marker size.
 *
 * Returns: %TRUE if bubble mode is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart2d_get_bubble_mode (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_bubble_mode:
 * @self: an #LrgScatterChart2D
 * @enabled: whether to enable bubble mode
 *
 * Sets whether to use bubble mode.
 * In bubble mode, the Z value of data points controls marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_bubble_mode (LrgScatterChart2D *self,
                                     gboolean           enabled);

/**
 * lrg_scatter_chart2d_get_min_bubble_size:
 * @self: an #LrgScatterChart2D
 *
 * Gets the minimum bubble size.
 *
 * Returns: minimum size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_min_bubble_size (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_min_bubble_size:
 * @self: an #LrgScatterChart2D
 * @size: minimum size in pixels
 *
 * Sets the minimum bubble size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_min_bubble_size (LrgScatterChart2D *self,
                                         gfloat             size);

/**
 * lrg_scatter_chart2d_get_max_bubble_size:
 * @self: an #LrgScatterChart2D
 *
 * Gets the maximum bubble size.
 *
 * Returns: maximum size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_max_bubble_size (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_max_bubble_size:
 * @self: an #LrgScatterChart2D
 * @size: maximum size in pixels
 *
 * Sets the maximum bubble size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_max_bubble_size (LrgScatterChart2D *self,
                                         gfloat             size);

/* ==========================================================================
 * Trend Line
 * ========================================================================== */

/**
 * lrg_scatter_chart2d_get_show_trend_line:
 * @self: an #LrgScatterChart2D
 *
 * Gets whether trend lines are shown.
 *
 * Returns: %TRUE if trend lines are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart2d_get_show_trend_line (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_show_trend_line:
 * @self: an #LrgScatterChart2D
 * @show: whether to show trend lines
 *
 * Sets whether to show trend lines (linear regression).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_show_trend_line (LrgScatterChart2D *self,
                                         gboolean           show);

/**
 * lrg_scatter_chart2d_get_trend_line_width:
 * @self: an #LrgScatterChart2D
 *
 * Gets the trend line width.
 *
 * Returns: line width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_trend_line_width (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_trend_line_width:
 * @self: an #LrgScatterChart2D
 * @width: line width in pixels
 *
 * Sets the trend line width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_trend_line_width (LrgScatterChart2D *self,
                                          gfloat             width);

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

/**
 * lrg_scatter_chart2d_get_hit_radius:
 * @self: an #LrgScatterChart2D
 *
 * Gets the hit test radius.
 *
 * Returns: the hit radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart2d_get_hit_radius (LrgScatterChart2D *self);

/**
 * lrg_scatter_chart2d_set_hit_radius:
 * @self: an #LrgScatterChart2D
 * @radius: hit radius in pixels
 *
 * Sets the hit test radius.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart2d_set_hit_radius (LrgScatterChart2D *self,
                                    gfloat             radius);

G_END_DECLS
