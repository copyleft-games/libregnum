/* lrg-surface-chart3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSurfaceChart3D - 3D Surface Chart widget.
 *
 * Renders data as a 3D surface mesh. Data is organized as a grid
 * where X determines column, Z determines row, and Y is the height.
 * Typically used to visualize functions of two variables.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart3d.h"
#include "lrg-chart-color-scale.h"

G_BEGIN_DECLS

#define LRG_TYPE_SURFACE_CHART3D (lrg_surface_chart3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSurfaceChart3D, lrg_surface_chart3d, LRG, SURFACE_CHART3D, LrgChart3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_surface_chart3d_new:
 *
 * Creates a new 3D surface chart with default settings.
 *
 * Returns: (transfer full): a new #LrgSurfaceChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSurfaceChart3D *
lrg_surface_chart3d_new (void);

/**
 * lrg_surface_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D surface chart with specified size.
 *
 * Returns: (transfer full): a new #LrgSurfaceChart3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSurfaceChart3D *
lrg_surface_chart3d_new_with_size (gfloat width,
                                   gfloat height);

/* ==========================================================================
 * Grid Data
 * ========================================================================== */

/**
 * lrg_surface_chart3d_set_grid_size:
 * @self: an #LrgSurfaceChart3D
 * @rows: number of rows (Z direction)
 * @cols: number of columns (X direction)
 *
 * Sets the grid dimensions. This allocates the internal grid data.
 * Call this before setting grid values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_grid_size (LrgSurfaceChart3D *self,
                                   guint              rows,
                                   guint              cols);

/**
 * lrg_surface_chart3d_get_rows:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets the number of rows in the grid.
 *
 * Returns: number of rows
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_surface_chart3d_get_rows (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_get_cols:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets the number of columns in the grid.
 *
 * Returns: number of columns
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_surface_chart3d_get_cols (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_value:
 * @self: an #LrgSurfaceChart3D
 * @row: row index (Z direction)
 * @col: column index (X direction)
 * @value: height value (Y direction)
 *
 * Sets a single grid value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_value (LrgSurfaceChart3D *self,
                               guint              row,
                               guint              col,
                               gdouble            value);

/**
 * lrg_surface_chart3d_get_value:
 * @self: an #LrgSurfaceChart3D
 * @row: row index
 * @col: column index
 *
 * Gets a single grid value.
 *
 * Returns: the height value at (row, col)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_surface_chart3d_get_value (LrgSurfaceChart3D *self,
                               guint              row,
                               guint              col);

/**
 * lrg_surface_chart3d_set_row:
 * @self: an #LrgSurfaceChart3D
 * @row: row index
 * @values: (array length=count): array of values
 * @count: number of values (should match cols)
 *
 * Sets an entire row of values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_row (LrgSurfaceChart3D *self,
                             guint              row,
                             const gdouble     *values,
                             guint              count);

/**
 * lrg_surface_chart3d_set_from_function:
 * @self: an #LrgSurfaceChart3D
 * @rows: number of rows
 * @cols: number of columns
 * @x_min: minimum X value
 * @x_max: maximum X value
 * @z_min: minimum Z value
 * @z_max: maximum Z value
 * @func: (scope call): function that computes Y from X and Z
 * @user_data: (closure): data to pass to func
 *
 * Populates the surface from a function f(x,z) = y.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_from_function (LrgSurfaceChart3D *self,
                                       guint              rows,
                                       guint              cols,
                                       gdouble            x_min,
                                       gdouble            x_max,
                                       gdouble            z_min,
                                       gdouble            z_max,
                                       gdouble          (*func) (gdouble x, gdouble z, gpointer user_data),
                                       gpointer           user_data);

/* ==========================================================================
 * Display Options
 * ========================================================================== */

/**
 * lrg_surface_chart3d_get_show_wireframe:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets whether wireframe lines are shown.
 *
 * Returns: %TRUE if wireframe is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_surface_chart3d_get_show_wireframe (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_show_wireframe:
 * @self: an #LrgSurfaceChart3D
 * @show: whether to show wireframe
 *
 * Sets whether to show wireframe lines on the surface.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_show_wireframe (LrgSurfaceChart3D *self,
                                        gboolean           show);

/**
 * lrg_surface_chart3d_get_show_fill:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets whether the surface is filled.
 *
 * Returns: %TRUE if surface is filled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_surface_chart3d_get_show_fill (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_show_fill:
 * @self: an #LrgSurfaceChart3D
 * @show: whether to fill surface
 *
 * Sets whether to fill the surface with colors.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_show_fill (LrgSurfaceChart3D *self,
                                   gboolean           show);

/**
 * lrg_surface_chart3d_get_wireframe_color:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets the wireframe color.
 *
 * Returns: (transfer none): the wireframe color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_surface_chart3d_get_wireframe_color (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_wireframe_color:
 * @self: an #LrgSurfaceChart3D
 * @color: the wireframe color
 *
 * Sets the wireframe color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_wireframe_color (LrgSurfaceChart3D *self,
                                         GrlColor          *color);

/**
 * lrg_surface_chart3d_get_color_scale:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets the color scale used for height-based coloring.
 *
 * Returns: (transfer none) (nullable): the color scale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_surface_chart3d_get_color_scale (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_color_scale:
 * @self: an #LrgSurfaceChart3D
 * @scale: (nullable): the color scale to use
 *
 * Sets the color scale for height-based coloring.
 * Pass %NULL to use the default color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_color_scale (LrgSurfaceChart3D  *self,
                                     LrgChartColorScale *scale);

/**
 * lrg_surface_chart3d_get_fill_opacity:
 * @self: an #LrgSurfaceChart3D
 *
 * Gets the fill opacity.
 *
 * Returns: opacity (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_surface_chart3d_get_fill_opacity (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_fill_opacity:
 * @self: an #LrgSurfaceChart3D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the fill opacity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_fill_opacity (LrgSurfaceChart3D *self,
                                      gfloat             opacity);

/* ==========================================================================
 * Value Range
 * ========================================================================== */

/**
 * lrg_surface_chart3d_auto_range:
 * @self: an #LrgSurfaceChart3D
 *
 * Automatically calculates the Y range from current grid values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_auto_range (LrgSurfaceChart3D *self);

/**
 * lrg_surface_chart3d_set_y_range:
 * @self: an #LrgSurfaceChart3D
 * @min: minimum Y value
 * @max: maximum Y value
 *
 * Sets the Y value range for normalization.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_set_y_range (LrgSurfaceChart3D *self,
                                 gdouble            min,
                                 gdouble            max);

/**
 * lrg_surface_chart3d_get_y_range:
 * @self: an #LrgSurfaceChart3D
 * @min: (out) (nullable): return location for minimum
 * @max: (out) (nullable): return location for maximum
 *
 * Gets the Y value range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_surface_chart3d_get_y_range (LrgSurfaceChart3D *self,
                                 gdouble           *min,
                                 gdouble           *max);

G_END_DECLS
