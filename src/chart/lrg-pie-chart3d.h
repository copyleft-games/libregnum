/* lrg-pie-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPieChart3D - 3D Pie Chart widget.
 *
 * Renders data as an extruded 3D pie chart with configurable depth.
 * Data points use Y for value (slice size). X is ignored.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_PIE_CHART3D (lrg_pie_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPieChart3D, lrg_pie_chart3d, LRG, PIE_CHART3D, LrgChart3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_pie_chart3d_new:
 *
 * Creates a new 3D pie chart with default settings.
 *
 * Returns: (transfer full): a new #LrgPieChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPieChart3D *
lrg_pie_chart3d_new (void);

/**
 * lrg_pie_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D pie chart with specified size.
 *
 * Returns: (transfer full): a new #LrgPieChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPieChart3D *
lrg_pie_chart3d_new_with_size (gfloat width,
                               gfloat height);

/* ==========================================================================
 * Pie Dimensions
 * ========================================================================== */

/**
 * lrg_pie_chart3d_get_radius:
 * @self: an #LrgPieChart3D
 *
 * Gets the pie radius as fraction of available space.
 *
 * Returns: radius (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart3d_get_radius (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_radius:
 * @self: an #LrgPieChart3D
 * @radius: pie radius (0.0 to 1.0)
 *
 * Sets the pie radius as fraction of available space.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_radius (LrgPieChart3D *self,
                            gfloat         radius);

/**
 * lrg_pie_chart3d_get_depth:
 * @self: an #LrgPieChart3D
 *
 * Gets the extrusion depth as fraction of radius.
 *
 * Returns: depth fraction (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart3d_get_depth (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_depth:
 * @self: an #LrgPieChart3D
 * @depth: extrusion depth (0.0 to 1.0)
 *
 * Sets the extrusion depth as fraction of radius.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_depth (LrgPieChart3D *self,
                           gfloat         depth);

/**
 * lrg_pie_chart3d_get_inner_radius:
 * @self: an #LrgPieChart3D
 *
 * Gets the inner radius for donut mode (0 = solid pie).
 *
 * Returns: inner radius (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart3d_get_inner_radius (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_inner_radius:
 * @self: an #LrgPieChart3D
 * @radius: inner radius (0.0 = solid pie, >0 = donut)
 *
 * Sets the inner radius for donut mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_inner_radius (LrgPieChart3D *self,
                                  gfloat         radius);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_pie_chart3d_get_start_angle:
 * @self: an #LrgPieChart3D
 *
 * Gets the starting angle in degrees.
 *
 * Returns: start angle (0-360)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart3d_get_start_angle (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_start_angle:
 * @self: an #LrgPieChart3D
 * @angle: start angle in degrees
 *
 * Sets the starting angle in degrees.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_start_angle (LrgPieChart3D *self,
                                 gfloat         angle);

/**
 * lrg_pie_chart3d_get_explode_distance:
 * @self: an #LrgPieChart3D
 *
 * Gets the explode distance as fraction of radius.
 *
 * Returns: explode distance (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_pie_chart3d_get_explode_distance (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_explode_distance:
 * @self: an #LrgPieChart3D
 * @distance: explode distance (0.0 to 1.0)
 *
 * Sets the explode distance for exploded slices.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_explode_distance (LrgPieChart3D *self,
                                      gfloat         distance);

/**
 * lrg_pie_chart3d_get_show_edges:
 * @self: an #LrgPieChart3D
 *
 * Gets whether slice edges are drawn.
 *
 * Returns: %TRUE if edges are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_pie_chart3d_get_show_edges (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_show_edges:
 * @self: an #LrgPieChart3D
 * @show: whether to show edges
 *
 * Sets whether to draw slice edges.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_show_edges (LrgPieChart3D *self,
                                gboolean       show);

/**
 * lrg_pie_chart3d_get_edge_color:
 * @self: an #LrgPieChart3D
 *
 * Gets the edge color.
 *
 * Returns: (transfer none): the edge color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_pie_chart3d_get_edge_color (LrgPieChart3D *self);

/**
 * lrg_pie_chart3d_set_edge_color:
 * @self: an #LrgPieChart3D
 * @color: the edge color
 *
 * Sets the edge color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_set_edge_color (LrgPieChart3D *self,
                                GrlColor      *color);

/* ==========================================================================
 * Slice Operations
 * ========================================================================== */

/**
 * lrg_pie_chart3d_explode_slice:
 * @self: an #LrgPieChart3D
 * @series_index: which series (use 0 for single series)
 * @point_index: which slice to explode
 * @exploded: whether the slice is exploded
 *
 * Sets whether a specific slice is exploded.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_explode_slice (LrgPieChart3D *self,
                               gint           series_index,
                               gint           point_index,
                               gboolean       exploded);

/**
 * lrg_pie_chart3d_is_slice_exploded:
 * @self: an #LrgPieChart3D
 * @series_index: which series
 * @point_index: which slice
 *
 * Gets whether a specific slice is exploded.
 *
 * Returns: %TRUE if the slice is exploded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_pie_chart3d_is_slice_exploded (LrgPieChart3D *self,
                                   gint           series_index,
                                   gint           point_index);

/**
 * lrg_pie_chart3d_explode_all:
 * @self: an #LrgPieChart3D
 * @exploded: whether all slices are exploded
 *
 * Sets whether all slices are exploded.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_pie_chart3d_explode_all (LrgPieChart3D *self,
                             gboolean       exploded);

G_END_DECLS
