/* lrg-line-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLineChart2D - 2D Line Chart widget.
 *
 * Renders data as connected lines with optional markers at data points.
 * Supports multiple line styles, smooth curves, and area fill.
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

#define LRG_TYPE_LINE_CHART2D (lrg_line_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLineChart2D, lrg_line_chart2d, LRG, LINE_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_line_chart2d_new:
 *
 * Creates a new line chart with default settings.
 *
 * Returns: (transfer full): a new #LrgLineChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLineChart2D *
lrg_line_chart2d_new (void);

/**
 * lrg_line_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new line chart with specified size.
 *
 * Returns: (transfer full): a new #LrgLineChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLineChart2D *
lrg_line_chart2d_new_with_size (gfloat width,
                                gfloat height);

/* ==========================================================================
 * Line Style
 * ========================================================================== */

/**
 * lrg_line_chart2d_get_smooth:
 * @self: an #LrgLineChart2D
 *
 * Gets whether lines are smoothed with bezier curves.
 *
 * Returns: %TRUE if smooth
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart2d_get_smooth (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_smooth:
 * @self: an #LrgLineChart2D
 * @smooth: whether to use smooth curves
 *
 * Sets whether to use bezier curves for smooth lines.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_smooth (LrgLineChart2D *self,
                             gboolean        smooth);

/**
 * lrg_line_chart2d_get_smoothing_tension:
 * @self: an #LrgLineChart2D
 *
 * Gets the smoothing tension factor.
 *
 * Returns: the tension (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart2d_get_smoothing_tension (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_smoothing_tension:
 * @self: an #LrgLineChart2D
 * @tension: smoothing tension (0.0 to 1.0)
 *
 * Sets the bezier curve smoothing tension.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_smoothing_tension (LrgLineChart2D *self,
                                        gfloat          tension);

/* ==========================================================================
 * Area Fill
 * ========================================================================== */

/**
 * lrg_line_chart2d_get_fill_area:
 * @self: an #LrgLineChart2D
 *
 * Gets whether the area under the line is filled.
 *
 * Returns: %TRUE if area is filled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart2d_get_fill_area (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_fill_area:
 * @self: an #LrgLineChart2D
 * @fill: whether to fill area
 *
 * Sets whether to fill the area under the line.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_fill_area (LrgLineChart2D *self,
                                gboolean        fill);

/**
 * lrg_line_chart2d_get_fill_opacity:
 * @self: an #LrgLineChart2D
 *
 * Gets the opacity of the area fill.
 *
 * Returns: opacity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart2d_get_fill_opacity (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_fill_opacity:
 * @self: an #LrgLineChart2D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the opacity of the area fill.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_fill_opacity (LrgLineChart2D *self,
                                   gfloat          opacity);

/* ==========================================================================
 * Point Markers
 * ========================================================================== */

/**
 * lrg_line_chart2d_get_show_markers:
 * @self: an #LrgLineChart2D
 *
 * Gets whether markers are shown at data points.
 *
 * Returns: %TRUE if markers are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart2d_get_show_markers (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_show_markers:
 * @self: an #LrgLineChart2D
 * @show: whether to show markers
 *
 * Sets whether to show markers at data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_show_markers (LrgLineChart2D *self,
                                   gboolean        show);

/**
 * lrg_line_chart2d_get_default_marker:
 * @self: an #LrgLineChart2D
 *
 * Gets the default marker style for series without explicit markers.
 *
 * Returns: the marker style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartMarker
lrg_line_chart2d_get_default_marker (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_default_marker:
 * @self: an #LrgLineChart2D
 * @marker: the marker style
 *
 * Sets the default marker style for series without explicit markers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_default_marker (LrgLineChart2D *self,
                                     LrgChartMarker  marker);

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

/**
 * lrg_line_chart2d_get_hit_radius:
 * @self: an #LrgLineChart2D
 *
 * Gets the hit test radius for data points.
 *
 * Returns: the hit radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart2d_get_hit_radius (LrgLineChart2D *self);

/**
 * lrg_line_chart2d_set_hit_radius:
 * @self: an #LrgLineChart2D
 * @radius: hit radius in pixels
 *
 * Sets the hit test radius for data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart2d_set_hit_radius (LrgLineChart2D *self,
                                 gfloat          radius);

G_END_DECLS
