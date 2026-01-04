/* lrg-bar-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBarChart3D - 3D Bar Chart widget.
 *
 * Renders data as 3D bars with configurable dimensions.
 * Data points use X for column, Z for row, Y for height.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_BAR_CHART3D (lrg_bar_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBarChart3D, lrg_bar_chart3d, LRG, BAR_CHART3D, LrgChart3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_bar_chart3d_new:
 *
 * Creates a new 3D bar chart with default settings.
 *
 * Returns: (transfer full): a new #LrgBarChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBarChart3D *
lrg_bar_chart3d_new (void);

/**
 * lrg_bar_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D bar chart with specified size.
 *
 * Returns: (transfer full): a new #LrgBarChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBarChart3D *
lrg_bar_chart3d_new_with_size (gfloat width,
                               gfloat height);

/* ==========================================================================
 * Bar Dimensions
 * ========================================================================== */

/**
 * lrg_bar_chart3d_get_bar_width:
 * @self: an #LrgBarChart3D
 *
 * Gets the bar width as fraction of available space.
 *
 * Returns: bar width (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart3d_get_bar_width (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_bar_width:
 * @self: an #LrgBarChart3D
 * @width: bar width (0.0 to 1.0)
 *
 * Sets the bar width as fraction of available space.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_bar_width (LrgBarChart3D *self,
                               gfloat         width);

/**
 * lrg_bar_chart3d_get_bar_depth:
 * @self: an #LrgBarChart3D
 *
 * Gets the bar depth as fraction of available space.
 *
 * Returns: bar depth (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart3d_get_bar_depth (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_bar_depth:
 * @self: an #LrgBarChart3D
 * @depth: bar depth (0.0 to 1.0)
 *
 * Sets the bar depth as fraction of available space.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_bar_depth (LrgBarChart3D *self,
                               gfloat         depth);

/**
 * lrg_bar_chart3d_get_bar_spacing:
 * @self: an #LrgBarChart3D
 *
 * Gets the spacing between bars.
 *
 * Returns: spacing as fraction (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart3d_get_bar_spacing (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_bar_spacing:
 * @self: an #LrgBarChart3D
 * @spacing: spacing between bars (0.0 to 1.0)
 *
 * Sets the spacing between bars.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_bar_spacing (LrgBarChart3D *self,
                                 gfloat         spacing);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_bar_chart3d_get_show_edges:
 * @self: an #LrgBarChart3D
 *
 * Gets whether bar edges are drawn.
 *
 * Returns: %TRUE if edges are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bar_chart3d_get_show_edges (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_show_edges:
 * @self: an #LrgBarChart3D
 * @show: whether to show edges
 *
 * Sets whether to draw bar edges/outlines.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_show_edges (LrgBarChart3D *self,
                                gboolean       show);

/**
 * lrg_bar_chart3d_get_edge_color:
 * @self: an #LrgBarChart3D
 *
 * Gets the bar edge color.
 *
 * Returns: (transfer none): the edge color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_bar_chart3d_get_edge_color (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_edge_color:
 * @self: an #LrgBarChart3D
 * @color: the edge color
 *
 * Sets the bar edge color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_edge_color (LrgBarChart3D *self,
                                GrlColor      *color);

/**
 * lrg_bar_chart3d_get_edge_width:
 * @self: an #LrgBarChart3D
 *
 * Gets the bar edge line width.
 *
 * Returns: edge width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart3d_get_edge_width (LrgBarChart3D *self);

/**
 * lrg_bar_chart3d_set_edge_width:
 * @self: an #LrgBarChart3D
 * @width: edge width in pixels
 *
 * Sets the bar edge line width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart3d_set_edge_width (LrgBarChart3D *self,
                                gfloat         width);

G_END_DECLS
