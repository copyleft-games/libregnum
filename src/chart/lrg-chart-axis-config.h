/* lrg-chart-axis-config.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartAxisConfig - Configuration for a chart axis.
 *
 * This is a boxed type containing axis settings such as
 * title, range, grid, and styling options.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include <math.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_AXIS_CONFIG (lrg_chart_axis_config_get_type ())

/**
 * LRG_CHART_AXIS_AUTO:
 *
 * Special value for min/max/step that indicates automatic calculation.
 */
#define LRG_CHART_AXIS_AUTO (NAN)

/**
 * LrgChartAxisConfig:
 *
 * Configuration for a chart axis.
 *
 * #LrgChartAxisConfig is a boxed type containing settings for
 * an axis including title, range, grid lines, and formatting.
 * Use lrg_chart_axis_config_copy() and lrg_chart_axis_config_free()
 * to manage its lifetime.
 *
 * Since: 1.0
 */
typedef struct _LrgChartAxisConfig LrgChartAxisConfig;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_axis_config_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_axis_config_new:
 *
 * Creates a new axis config with default values.
 *
 * Returns: (transfer full): a new #LrgChartAxisConfig
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart_axis_config_new (void);

/**
 * lrg_chart_axis_config_new_with_title:
 * @title: axis title
 *
 * Creates a new axis config with a title.
 *
 * Returns: (transfer full): a new #LrgChartAxisConfig
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart_axis_config_new_with_title (const gchar *title);

/**
 * lrg_chart_axis_config_new_with_range:
 * @title: (nullable): axis title
 * @min: minimum value (or LRG_CHART_AXIS_AUTO)
 * @max: maximum value (or LRG_CHART_AXIS_AUTO)
 *
 * Creates a new axis config with title and range.
 *
 * Returns: (transfer full): a new #LrgChartAxisConfig
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart_axis_config_new_with_range (const gchar *title,
                                       gdouble      min,
                                       gdouble      max);

/**
 * lrg_chart_axis_config_copy:
 * @self: an #LrgChartAxisConfig
 *
 * Creates a deep copy of the axis config.
 *
 * Returns: (transfer full): a new #LrgChartAxisConfig
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartAxisConfig *
lrg_chart_axis_config_copy (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_free:
 * @self: an #LrgChartAxisConfig
 *
 * Frees the axis config.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_free (LrgChartAxisConfig *self);

/* ==========================================================================
 * Title
 * ========================================================================== */

/**
 * lrg_chart_axis_config_get_title:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the axis title.
 *
 * Returns: (transfer none) (nullable): the title
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_axis_config_get_title (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_title:
 * @self: an #LrgChartAxisConfig
 * @title: (nullable): the title
 *
 * Sets the axis title.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_title (LrgChartAxisConfig *self,
                                  const gchar        *title);

/* ==========================================================================
 * Range
 * ========================================================================== */

/**
 * lrg_chart_axis_config_get_min:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the minimum value. Returns NAN if auto.
 *
 * Returns: the minimum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_axis_config_get_min (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_min:
 * @self: an #LrgChartAxisConfig
 * @min: minimum value (or LRG_CHART_AXIS_AUTO)
 *
 * Sets the minimum value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_min (LrgChartAxisConfig *self,
                                gdouble             min);

/**
 * lrg_chart_axis_config_get_max:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the maximum value. Returns NAN if auto.
 *
 * Returns: the maximum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_axis_config_get_max (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_max:
 * @self: an #LrgChartAxisConfig
 * @max: maximum value (or LRG_CHART_AXIS_AUTO)
 *
 * Sets the maximum value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_max (LrgChartAxisConfig *self,
                                gdouble             max);

/**
 * lrg_chart_axis_config_get_step:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the tick step value. Returns NAN if auto.
 *
 * Returns: the step value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_axis_config_get_step (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_step:
 * @self: an #LrgChartAxisConfig
 * @step: step value (or LRG_CHART_AXIS_AUTO)
 *
 * Sets the tick step value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_step (LrgChartAxisConfig *self,
                                 gdouble             step);

/**
 * lrg_chart_axis_config_is_min_auto:
 * @self: an #LrgChartAxisConfig
 *
 * Checks if minimum is set to auto.
 *
 * Returns: %TRUE if auto
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_axis_config_is_min_auto (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_is_max_auto:
 * @self: an #LrgChartAxisConfig
 *
 * Checks if maximum is set to auto.
 *
 * Returns: %TRUE if auto
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_axis_config_is_max_auto (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_is_step_auto:
 * @self: an #LrgChartAxisConfig
 *
 * Checks if step is set to auto.
 *
 * Returns: %TRUE if auto
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_axis_config_is_step_auto (const LrgChartAxisConfig *self);

/* ==========================================================================
 * Grid and Scale
 * ========================================================================== */

/**
 * lrg_chart_axis_config_get_show_grid:
 * @self: an #LrgChartAxisConfig
 *
 * Gets whether grid lines are shown.
 *
 * Returns: %TRUE if grid is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_axis_config_get_show_grid (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_show_grid:
 * @self: an #LrgChartAxisConfig
 * @show_grid: whether to show grid
 *
 * Sets whether grid lines are shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_show_grid (LrgChartAxisConfig *self,
                                      gboolean            show_grid);

/**
 * lrg_chart_axis_config_get_logarithmic:
 * @self: an #LrgChartAxisConfig
 *
 * Gets whether logarithmic scale is used.
 *
 * Returns: %TRUE if logarithmic
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_axis_config_get_logarithmic (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_logarithmic:
 * @self: an #LrgChartAxisConfig
 * @logarithmic: whether to use logarithmic scale
 *
 * Sets whether logarithmic scale is used.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_logarithmic (LrgChartAxisConfig *self,
                                        gboolean            logarithmic);

/* ==========================================================================
 * Formatting
 * ========================================================================== */

/**
 * lrg_chart_axis_config_get_format:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the printf format string for labels.
 *
 * Returns: (transfer none) (nullable): the format string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_axis_config_get_format (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_format:
 * @self: an #LrgChartAxisConfig
 * @format: (nullable): printf format string (e.g., "%.2f")
 *
 * Sets the printf format string for labels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_format (LrgChartAxisConfig *self,
                                   const gchar        *format);

/**
 * lrg_chart_axis_config_get_label_rotation:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the label rotation angle in degrees.
 *
 * Returns: the rotation angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_chart_axis_config_get_label_rotation (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_label_rotation:
 * @self: an #LrgChartAxisConfig
 * @rotation: rotation in degrees
 *
 * Sets the label rotation angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_label_rotation (LrgChartAxisConfig *self,
                                           gfloat              rotation);

/* ==========================================================================
 * Colors
 * ========================================================================== */

/**
 * lrg_chart_axis_config_get_color:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the axis line color.
 *
 * Returns: (transfer none): the color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_axis_config_get_color (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_color:
 * @self: an #LrgChartAxisConfig
 * @color: the color
 *
 * Sets the axis line color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_color (LrgChartAxisConfig *self,
                                  const GrlColor     *color);

/**
 * lrg_chart_axis_config_get_grid_color:
 * @self: an #LrgChartAxisConfig
 *
 * Gets the grid line color.
 *
 * Returns: (transfer none): the color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_axis_config_get_grid_color (const LrgChartAxisConfig *self);

/**
 * lrg_chart_axis_config_set_grid_color:
 * @self: an #LrgChartAxisConfig
 * @color: the color
 *
 * Sets the grid line color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_axis_config_set_grid_color (LrgChartAxisConfig *self,
                                       const GrlColor     *color);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgChartAxisConfig, lrg_chart_axis_config_free)

G_END_DECLS
