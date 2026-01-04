/* lrg-scatter-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScatterChart3D - 3D Scatter Chart widget.
 *
 * Renders data points as markers in 3D space.
 * Data points use X, Y, Z for position and optional W for size (bubble chart).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart3d.h"
#include "lrg-chart-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCATTER_CHART3D (lrg_scatter_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScatterChart3D, lrg_scatter_chart3d, LRG, SCATTER_CHART3D, LrgChart3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_scatter_chart3d_new:
 *
 * Creates a new 3D scatter chart with default settings.
 *
 * Returns: (transfer full): a new #LrgScatterChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScatterChart3D *
lrg_scatter_chart3d_new (void);

/**
 * lrg_scatter_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D scatter chart with specified size.
 *
 * Returns: (transfer full): a new #LrgScatterChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgScatterChart3D *
lrg_scatter_chart3d_new_with_size (gfloat width,
                                   gfloat height);

/* ==========================================================================
 * Marker Properties
 * ========================================================================== */

/**
 * lrg_scatter_chart3d_get_marker_style:
 * @self: an #LrgScatterChart3D
 *
 * Gets the default marker style.
 *
 * Returns: the marker style
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartMarker
lrg_scatter_chart3d_get_marker_style (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_marker_style:
 * @self: an #LrgScatterChart3D
 * @style: the marker style
 *
 * Sets the default marker style for all points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_marker_style (LrgScatterChart3D *self,
                                      LrgChartMarker     style);

/**
 * lrg_scatter_chart3d_get_marker_size:
 * @self: an #LrgScatterChart3D
 *
 * Gets the default marker size in pixels.
 *
 * Returns: marker size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart3d_get_marker_size (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_marker_size:
 * @self: an #LrgScatterChart3D
 * @size: marker size in pixels
 *
 * Sets the default marker size in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_marker_size (LrgScatterChart3D *self,
                                     gfloat             size);

/**
 * lrg_scatter_chart3d_get_size_by_value:
 * @self: an #LrgScatterChart3D
 *
 * Gets whether marker size is determined by the W value (bubble mode).
 *
 * Returns: %TRUE if size is determined by W value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart3d_get_size_by_value (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_size_by_value:
 * @self: an #LrgScatterChart3D
 * @enabled: whether to use W value for size
 *
 * Sets whether marker size is determined by the W value (bubble mode).
 * When enabled, point's W value scales the marker size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_size_by_value (LrgScatterChart3D *self,
                                       gboolean           enabled);

/**
 * lrg_scatter_chart3d_get_min_marker_size:
 * @self: an #LrgScatterChart3D
 *
 * Gets the minimum marker size in bubble mode.
 *
 * Returns: minimum marker size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart3d_get_min_marker_size (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_min_marker_size:
 * @self: an #LrgScatterChart3D
 * @size: minimum marker size
 *
 * Sets the minimum marker size in bubble mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_min_marker_size (LrgScatterChart3D *self,
                                         gfloat             size);

/**
 * lrg_scatter_chart3d_get_max_marker_size:
 * @self: an #LrgScatterChart3D
 *
 * Gets the maximum marker size in bubble mode.
 *
 * Returns: maximum marker size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_scatter_chart3d_get_max_marker_size (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_max_marker_size:
 * @self: an #LrgScatterChart3D
 * @size: maximum marker size
 *
 * Sets the maximum marker size in bubble mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_max_marker_size (LrgScatterChart3D *self,
                                         gfloat             size);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_scatter_chart3d_get_show_drop_lines:
 * @self: an #LrgScatterChart3D
 *
 * Gets whether drop lines to the floor are shown.
 *
 * Returns: %TRUE if drop lines are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart3d_get_show_drop_lines (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_show_drop_lines:
 * @self: an #LrgScatterChart3D
 * @show: whether to show drop lines
 *
 * Sets whether to show drop lines from points to the floor.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_show_drop_lines (LrgScatterChart3D *self,
                                         gboolean           show);

/**
 * lrg_scatter_chart3d_get_drop_line_color:
 * @self: an #LrgScatterChart3D
 *
 * Gets the drop line color.
 *
 * Returns: (transfer none): the drop line color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_scatter_chart3d_get_drop_line_color (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_drop_line_color:
 * @self: an #LrgScatterChart3D
 * @color: the drop line color
 *
 * Sets the drop line color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_drop_line_color (LrgScatterChart3D *self,
                                         GrlColor          *color);

/**
 * lrg_scatter_chart3d_get_depth_fade:
 * @self: an #LrgScatterChart3D
 *
 * Gets whether distant points fade (alpha decreases with depth).
 *
 * Returns: %TRUE if depth fade is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart3d_get_depth_fade (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_depth_fade:
 * @self: an #LrgScatterChart3D
 * @enabled: whether to enable depth fade
 *
 * Sets whether distant points fade (alpha decreases with depth).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_depth_fade (LrgScatterChart3D *self,
                                    gboolean           enabled);

/**
 * lrg_scatter_chart3d_get_depth_scale:
 * @self: an #LrgScatterChart3D
 *
 * Gets whether distant points are drawn smaller (perspective scale).
 *
 * Returns: %TRUE if depth scale is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_scatter_chart3d_get_depth_scale (LrgScatterChart3D *self);

/**
 * lrg_scatter_chart3d_set_depth_scale:
 * @self: an #LrgScatterChart3D
 * @enabled: whether to enable depth scale
 *
 * Sets whether distant points are drawn smaller (perspective scale).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_scatter_chart3d_set_depth_scale (LrgScatterChart3D *self,
                                     gboolean           enabled);

G_END_DECLS
