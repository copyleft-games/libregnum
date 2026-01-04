/* lrg-area-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAreaChart2D - 2D Area Chart widget.
 *
 * Renders data as filled areas with support for stacking multiple series.
 * Useful for showing cumulative totals or comparing parts to whole.
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

#define LRG_TYPE_AREA_CHART2D (lrg_area_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAreaChart2D, lrg_area_chart2d, LRG, AREA_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_area_chart2d_new:
 *
 * Creates a new area chart with default settings.
 *
 * Returns: (transfer full): a new #LrgAreaChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAreaChart2D *
lrg_area_chart2d_new (void);

/**
 * lrg_area_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new area chart with specified size.
 *
 * Returns: (transfer full): a new #LrgAreaChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAreaChart2D *
lrg_area_chart2d_new_with_size (gfloat width,
                                gfloat height);

/* ==========================================================================
 * Area Mode
 * ========================================================================== */

/**
 * lrg_area_chart2d_get_mode:
 * @self: an #LrgAreaChart2D
 *
 * Gets the area stacking mode.
 *
 * Returns: the area mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAreaMode
lrg_area_chart2d_get_mode (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_mode:
 * @self: an #LrgAreaChart2D
 * @mode: the area mode
 *
 * Sets the area stacking mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_mode (LrgAreaChart2D   *self,
                           LrgChartAreaMode  mode);

/* ==========================================================================
 * Line Style
 * ========================================================================== */

/**
 * lrg_area_chart2d_get_show_line:
 * @self: an #LrgAreaChart2D
 *
 * Gets whether lines are shown at the top of areas.
 *
 * Returns: %TRUE if lines are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_area_chart2d_get_show_line (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_show_line:
 * @self: an #LrgAreaChart2D
 * @show: whether to show lines
 *
 * Sets whether to show lines at the top of areas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_show_line (LrgAreaChart2D *self,
                                gboolean        show);

/**
 * lrg_area_chart2d_get_line_width:
 * @self: an #LrgAreaChart2D
 *
 * Gets the line width.
 *
 * Returns: line width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_area_chart2d_get_line_width (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_line_width:
 * @self: an #LrgAreaChart2D
 * @width: line width in pixels
 *
 * Sets the line width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_line_width (LrgAreaChart2D *self,
                                 gfloat          width);

/* ==========================================================================
 * Fill Style
 * ========================================================================== */

/**
 * lrg_area_chart2d_get_fill_opacity:
 * @self: an #LrgAreaChart2D
 *
 * Gets the opacity of the area fill.
 *
 * Returns: opacity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_area_chart2d_get_fill_opacity (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_fill_opacity:
 * @self: an #LrgAreaChart2D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the opacity of the area fill.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_fill_opacity (LrgAreaChart2D *self,
                                   gfloat          opacity);

/* ==========================================================================
 * Markers
 * ========================================================================== */

/**
 * lrg_area_chart2d_get_show_markers:
 * @self: an #LrgAreaChart2D
 *
 * Gets whether markers are shown at data points.
 *
 * Returns: %TRUE if markers are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_area_chart2d_get_show_markers (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_show_markers:
 * @self: an #LrgAreaChart2D
 * @show: whether to show markers
 *
 * Sets whether to show markers at data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_show_markers (LrgAreaChart2D *self,
                                   gboolean        show);

/**
 * lrg_area_chart2d_get_marker_size:
 * @self: an #LrgAreaChart2D
 *
 * Gets the default marker size.
 *
 * Returns: marker size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_area_chart2d_get_marker_size (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_marker_size:
 * @self: an #LrgAreaChart2D
 * @size: marker size in pixels
 *
 * Sets the default marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_marker_size (LrgAreaChart2D *self,
                                  gfloat          size);

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

/**
 * lrg_area_chart2d_get_hit_radius:
 * @self: an #LrgAreaChart2D
 *
 * Gets the hit test radius for data points.
 *
 * Returns: the hit radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_area_chart2d_get_hit_radius (LrgAreaChart2D *self);

/**
 * lrg_area_chart2d_set_hit_radius:
 * @self: an #LrgAreaChart2D
 * @radius: hit radius in pixels
 *
 * Sets the hit test radius for data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_area_chart2d_set_hit_radius (LrgAreaChart2D *self,
                                 gfloat          radius);

G_END_DECLS
