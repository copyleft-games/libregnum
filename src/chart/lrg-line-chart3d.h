/* lrg-line-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLineChart3D - 3D Line Chart widget.
 *
 * Renders data as 3D lines connecting points in 3D space.
 * Data points use X for position along line, Z for depth row, Y for height.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_LINE_CHART3D (lrg_line_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLineChart3D, lrg_line_chart3d, LRG, LINE_CHART3D, LrgChart3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_line_chart3d_new:
 *
 * Creates a new 3D line chart with default settings.
 *
 * Returns: (transfer full): a new #LrgLineChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLineChart3D *
lrg_line_chart3d_new (void);

/**
 * lrg_line_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D line chart with specified size.
 *
 * Returns: (transfer full): a new #LrgLineChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLineChart3D *
lrg_line_chart3d_new_with_size (gfloat width,
                                gfloat height);

/* ==========================================================================
 * Line Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_line_width:
 * @self: an #LrgLineChart3D
 *
 * Gets the line width in pixels.
 *
 * Returns: line width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart3d_get_line_width (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_line_width:
 * @self: an #LrgLineChart3D
 * @width: line width in pixels
 *
 * Sets the line width in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_line_width (LrgLineChart3D *self,
                                 gfloat          width);

/* ==========================================================================
 * Marker Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_show_markers:
 * @self: an #LrgLineChart3D
 *
 * Gets whether markers are shown at data points.
 *
 * Returns: %TRUE if markers are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart3d_get_show_markers (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_show_markers:
 * @self: an #LrgLineChart3D
 * @show: whether to show markers
 *
 * Sets whether to show markers at data points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_show_markers (LrgLineChart3D *self,
                                   gboolean        show);

/**
 * lrg_line_chart3d_get_marker_size:
 * @self: an #LrgLineChart3D
 *
 * Gets the marker size in pixels.
 *
 * Returns: marker size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart3d_get_marker_size (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_marker_size:
 * @self: an #LrgLineChart3D
 * @size: marker size in pixels
 *
 * Sets the marker size in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_marker_size (LrgLineChart3D *self,
                                  gfloat          size);

/* ==========================================================================
 * Fill Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_fill_to_floor:
 * @self: an #LrgLineChart3D
 *
 * Gets whether lines are filled down to the floor (ribbon style).
 *
 * Returns: %TRUE if fill is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart3d_get_fill_to_floor (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_fill_to_floor:
 * @self: an #LrgLineChart3D
 * @fill: whether to fill to floor
 *
 * Sets whether to fill lines down to the floor (ribbon style).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_fill_to_floor (LrgLineChart3D *self,
                                    gboolean        fill);

/**
 * lrg_line_chart3d_get_fill_opacity:
 * @self: an #LrgLineChart3D
 *
 * Gets the fill opacity (0.0 to 1.0).
 *
 * Returns: fill opacity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_line_chart3d_get_fill_opacity (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_fill_opacity:
 * @self: an #LrgLineChart3D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the fill opacity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_fill_opacity (LrgLineChart3D *self,
                                   gfloat          opacity);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_smooth:
 * @self: an #LrgLineChart3D
 *
 * Gets whether lines are smoothed (bezier curves).
 *
 * Returns: %TRUE if smoothing is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart3d_get_smooth (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_smooth:
 * @self: an #LrgLineChart3D
 * @smooth: whether to smooth lines
 *
 * Sets whether to use bezier smoothing for lines.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_smooth (LrgLineChart3D *self,
                             gboolean        smooth);

/**
 * lrg_line_chart3d_get_drop_lines:
 * @self: an #LrgLineChart3D
 *
 * Gets whether drop lines are drawn from points to the floor.
 *
 * Returns: %TRUE if drop lines are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_line_chart3d_get_drop_lines (LrgLineChart3D *self);

/**
 * lrg_line_chart3d_set_drop_lines:
 * @self: an #LrgLineChart3D
 * @show: whether to show drop lines
 *
 * Sets whether to draw drop lines from points to the floor.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_line_chart3d_set_drop_lines (LrgLineChart3D *self,
                                 gboolean        show);

G_END_DECLS
