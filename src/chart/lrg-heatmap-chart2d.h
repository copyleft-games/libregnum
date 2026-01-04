/* lrg-heatmap-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHeatmapChart2D - 2D Heatmap/Grid Chart widget.
 *
 * Renders a grid of colored cells where color represents value.
 * Uses X for column, Y for row, Z for value.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-chart2d.h"
#include "lrg-chart-color-scale.h"

G_BEGIN_DECLS

#define LRG_TYPE_HEATMAP_CHART2D (lrg_heatmap_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHeatmapChart2D, lrg_heatmap_chart2d, LRG, HEATMAP_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_new:
 *
 * Creates a new heatmap chart with default settings.
 *
 * Returns: (transfer full): a new #LrgHeatmapChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHeatmapChart2D *
lrg_heatmap_chart2d_new (void);

/**
 * lrg_heatmap_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new heatmap chart with specified size.
 *
 * Returns: (transfer full): a new #LrgHeatmapChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHeatmapChart2D *
lrg_heatmap_chart2d_new_with_size (gfloat width,
                                   gfloat height);

/* ==========================================================================
 * Color Scale
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_get_color_scale:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the color scale used for value-to-color mapping.
 *
 * Returns: (transfer none): the color scale
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartColorScale *
lrg_heatmap_chart2d_get_color_scale (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_color_scale:
 * @self: an #LrgHeatmapChart2D
 * @scale: (transfer none): the color scale to use
 *
 * Sets the color scale for value-to-color mapping.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_color_scale (LrgHeatmapChart2D  *self,
                                     LrgChartColorScale *scale);

/* ==========================================================================
 * Grid Style
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_get_cell_spacing:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the spacing between cells.
 *
 * Returns: spacing in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_heatmap_chart2d_get_cell_spacing (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_cell_spacing:
 * @self: an #LrgHeatmapChart2D
 * @spacing: spacing in pixels
 *
 * Sets the spacing between cells.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_cell_spacing (LrgHeatmapChart2D *self,
                                      gfloat             spacing);

/**
 * lrg_heatmap_chart2d_get_cell_radius:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the corner radius of cells.
 *
 * Returns: radius in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_heatmap_chart2d_get_cell_radius (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_cell_radius:
 * @self: an #LrgHeatmapChart2D
 * @radius: corner radius in pixels
 *
 * Sets the corner radius for rounded cells.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_cell_radius (LrgHeatmapChart2D *self,
                                     gfloat             radius);

/**
 * lrg_heatmap_chart2d_get_show_grid:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets whether gridlines are shown.
 *
 * Returns: %TRUE if grid is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_heatmap_chart2d_get_show_grid (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_show_grid:
 * @self: an #LrgHeatmapChart2D
 * @show: whether to show grid
 *
 * Sets whether to display gridlines between cells.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_show_grid (LrgHeatmapChart2D *self,
                                   gboolean           show);

/**
 * lrg_heatmap_chart2d_get_grid_color:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the gridline color.
 *
 * Returns: (transfer none): the grid color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_heatmap_chart2d_get_grid_color (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_grid_color:
 * @self: an #LrgHeatmapChart2D
 * @color: the grid color
 *
 * Sets the gridline color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_grid_color (LrgHeatmapChart2D *self,
                                    GrlColor          *color);

/* ==========================================================================
 * Value Display
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_get_show_values:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets whether values are displayed in cells.
 *
 * Returns: %TRUE if values are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_heatmap_chart2d_get_show_values (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_show_values:
 * @self: an #LrgHeatmapChart2D
 * @show: whether to show values
 *
 * Sets whether to display numeric values in cells.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_show_values (LrgHeatmapChart2D *self,
                                     gboolean           show);

/**
 * lrg_heatmap_chart2d_get_value_format:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the printf format for cell values.
 *
 * Returns: (transfer none): the format string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_heatmap_chart2d_get_value_format (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_value_format:
 * @self: an #LrgHeatmapChart2D
 * @format: printf format string
 *
 * Sets the printf format for displaying cell values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_value_format (LrgHeatmapChart2D *self,
                                      const gchar       *format);

/**
 * lrg_heatmap_chart2d_get_value_font_size:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the font size for cell values.
 *
 * Returns: font size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_heatmap_chart2d_get_value_font_size (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_value_font_size:
 * @self: an #LrgHeatmapChart2D
 * @size: font size in pixels
 *
 * Sets the font size for cell values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_value_font_size (LrgHeatmapChart2D *self,
                                         gfloat             size);

/* ==========================================================================
 * Labels
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_set_row_labels:
 * @self: an #LrgHeatmapChart2D
 * @labels: (array zero-terminated=1) (nullable): array of label strings
 *
 * Sets labels for rows (displayed on Y axis).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_row_labels (LrgHeatmapChart2D  *self,
                                    const gchar *const *labels);

/**
 * lrg_heatmap_chart2d_set_col_labels:
 * @self: an #LrgHeatmapChart2D
 * @labels: (array zero-terminated=1) (nullable): array of label strings
 *
 * Sets labels for columns (displayed on X axis).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_col_labels (LrgHeatmapChart2D  *self,
                                    const gchar *const *labels);

/**
 * lrg_heatmap_chart2d_get_row_label:
 * @self: an #LrgHeatmapChart2D
 * @row: row index
 *
 * Gets the label for a specific row.
 *
 * Returns: (transfer none) (nullable): the label string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_heatmap_chart2d_get_row_label (LrgHeatmapChart2D *self,
                                   guint              row);

/**
 * lrg_heatmap_chart2d_get_col_label:
 * @self: an #LrgHeatmapChart2D
 * @col: column index
 *
 * Gets the label for a specific column.
 *
 * Returns: (transfer none) (nullable): the label string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_heatmap_chart2d_get_col_label (LrgHeatmapChart2D *self,
                                   guint              col);

/* ==========================================================================
 * Color Scale Legend
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_get_show_scale:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets whether the color scale legend is shown.
 *
 * Returns: %TRUE if scale is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_heatmap_chart2d_get_show_scale (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_show_scale:
 * @self: an #LrgHeatmapChart2D
 * @show: whether to show scale
 *
 * Sets whether to display the color scale legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_show_scale (LrgHeatmapChart2D *self,
                                    gboolean           show);

/**
 * lrg_heatmap_chart2d_get_scale_width:
 * @self: an #LrgHeatmapChart2D
 *
 * Gets the width of the color scale legend.
 *
 * Returns: width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_heatmap_chart2d_get_scale_width (LrgHeatmapChart2D *self);

/**
 * lrg_heatmap_chart2d_set_scale_width:
 * @self: an #LrgHeatmapChart2D
 * @width: width in pixels
 *
 * Sets the width of the color scale legend.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_set_scale_width (LrgHeatmapChart2D *self,
                                     gfloat             width);

/* ==========================================================================
 * Data Range
 * ========================================================================== */

/**
 * lrg_heatmap_chart2d_auto_range:
 * @self: an #LrgHeatmapChart2D
 *
 * Automatically sets the color scale range based on data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_heatmap_chart2d_auto_range (LrgHeatmapChart2D *self);

G_END_DECLS
