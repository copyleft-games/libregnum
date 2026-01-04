/* lrg-chart-data-point.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgChartDataPoint - A single data point for charts.
 *
 * This is a boxed type representing a single data point with
 * coordinates, optional label, and optional color override.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CHART_DATA_POINT (lrg_chart_data_point_get_type ())

/**
 * LrgChartDataPoint:
 *
 * A single data point for chart visualization.
 *
 * #LrgChartDataPoint is a boxed type containing coordinate values,
 * an optional label, and an optional color override. Use
 * lrg_chart_data_point_copy() and lrg_chart_data_point_free()
 * to manage its lifetime.
 *
 * Since: 1.0
 */
typedef struct _LrgChartDataPoint LrgChartDataPoint;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_data_point_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_chart_data_point_new:
 * @x: X coordinate value
 * @y: Y coordinate value
 *
 * Creates a new data point with basic coordinates.
 *
 * Returns: (transfer full): a new #LrgChartDataPoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataPoint *
lrg_chart_data_point_new (gdouble x,
                          gdouble y);

/**
 * lrg_chart_data_point_new_with_z:
 * @x: X coordinate value
 * @y: Y coordinate value
 * @z: Z coordinate value (for 3D charts)
 *
 * Creates a new data point with 3D coordinates.
 *
 * Returns: (transfer full): a new #LrgChartDataPoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataPoint *
lrg_chart_data_point_new_with_z (gdouble x,
                                  gdouble y,
                                  gdouble z);

/**
 * lrg_chart_data_point_new_labeled:
 * @x: X coordinate value
 * @y: Y coordinate value
 * @label: (nullable): point label
 *
 * Creates a new data point with a label.
 *
 * Returns: (transfer full): a new #LrgChartDataPoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataPoint *
lrg_chart_data_point_new_labeled (gdouble      x,
                                   gdouble      y,
                                   const gchar *label);

/**
 * lrg_chart_data_point_new_full:
 * @x: X coordinate value
 * @y: Y coordinate value
 * @z: Z coordinate value
 * @w: W coordinate value (e.g., candlestick high or bubble radius)
 * @label: (nullable): point label
 * @color: (nullable): color override
 *
 * Creates a new data point with all parameters.
 *
 * Returns: (transfer full): a new #LrgChartDataPoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataPoint *
lrg_chart_data_point_new_full (gdouble         x,
                                gdouble         y,
                                gdouble         z,
                                gdouble         w,
                                const gchar    *label,
                                const GrlColor *color);

/**
 * lrg_chart_data_point_copy:
 * @self: an #LrgChartDataPoint
 *
 * Creates a deep copy of the data point.
 *
 * Returns: (transfer full): a new #LrgChartDataPoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartDataPoint *
lrg_chart_data_point_copy (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_free:
 * @self: an #LrgChartDataPoint
 *
 * Frees the data point.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_free (LrgChartDataPoint *self);

/* ==========================================================================
 * Accessors
 * ========================================================================== */

/**
 * lrg_chart_data_point_get_x:
 * @self: an #LrgChartDataPoint
 *
 * Gets the X coordinate.
 *
 * Returns: the X value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_data_point_get_x (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_x:
 * @self: an #LrgChartDataPoint
 * @x: the X value
 *
 * Sets the X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_x (LrgChartDataPoint *self,
                            gdouble            x);

/**
 * lrg_chart_data_point_get_y:
 * @self: an #LrgChartDataPoint
 *
 * Gets the Y coordinate.
 *
 * Returns: the Y value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_data_point_get_y (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_y:
 * @self: an #LrgChartDataPoint
 * @y: the Y value
 *
 * Sets the Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_y (LrgChartDataPoint *self,
                            gdouble            y);

/**
 * lrg_chart_data_point_get_z:
 * @self: an #LrgChartDataPoint
 *
 * Gets the Z coordinate (for 3D charts).
 *
 * Returns: the Z value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_data_point_get_z (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_z:
 * @self: an #LrgChartDataPoint
 * @z: the Z value
 *
 * Sets the Z coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_z (LrgChartDataPoint *self,
                            gdouble            z);

/**
 * lrg_chart_data_point_get_w:
 * @self: an #LrgChartDataPoint
 *
 * Gets the W value (used for candlestick high, bubble radius, etc.).
 *
 * Returns: the W value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_chart_data_point_get_w (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_w:
 * @self: an #LrgChartDataPoint
 * @w: the W value
 *
 * Sets the W value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_w (LrgChartDataPoint *self,
                            gdouble            w);

/**
 * lrg_chart_data_point_get_label:
 * @self: an #LrgChartDataPoint
 *
 * Gets the point label.
 *
 * Returns: (transfer none) (nullable): the label
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_chart_data_point_get_label (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_label:
 * @self: an #LrgChartDataPoint
 * @label: (nullable): the label
 *
 * Sets the point label.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_label (LrgChartDataPoint *self,
                                const gchar       *label);

/**
 * lrg_chart_data_point_get_color:
 * @self: an #LrgChartDataPoint
 *
 * Gets the color override.
 *
 * Returns: (transfer none) (nullable): the color override
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_chart_data_point_get_color (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_color:
 * @self: an #LrgChartDataPoint
 * @color: (nullable): the color override
 *
 * Sets the color override.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_color (LrgChartDataPoint *self,
                                const GrlColor    *color);

/**
 * lrg_chart_data_point_has_color:
 * @self: an #LrgChartDataPoint
 *
 * Checks if the point has a color override.
 *
 * Returns: %TRUE if a color is set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_chart_data_point_has_color (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_clear_color:
 * @self: an #LrgChartDataPoint
 *
 * Clears the color override.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_clear_color (LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_get_user_data:
 * @self: an #LrgChartDataPoint
 *
 * Gets user-defined data associated with the point.
 *
 * Returns: (transfer none) (nullable): the user data
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gpointer
lrg_chart_data_point_get_user_data (const LrgChartDataPoint *self);

/**
 * lrg_chart_data_point_set_user_data:
 * @self: an #LrgChartDataPoint
 * @user_data: (nullable): user-defined data
 * @destroy: (nullable): destroy notify function
 *
 * Sets user-defined data associated with the point.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_chart_data_point_set_user_data (LrgChartDataPoint *self,
                                    gpointer           user_data,
                                    GDestroyNotify     destroy);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgChartDataPoint, lrg_chart_data_point_free)

G_END_DECLS
