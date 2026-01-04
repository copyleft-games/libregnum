/* lrg-bar-chart2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBarChart2D - 2D Bar Chart widget.
 *
 * Renders data as vertical or horizontal bars. Supports grouped,
 * stacked, and percent-stacked modes for multiple series.
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

#define LRG_TYPE_BAR_CHART2D (lrg_bar_chart2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBarChart2D, lrg_bar_chart2d, LRG, BAR_CHART2D, LrgChart2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_bar_chart2d_new:
 *
 * Creates a new bar chart with default settings.
 *
 * Returns: (transfer full): a new #LrgBarChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBarChart2D *
lrg_bar_chart2d_new (void);

/**
 * lrg_bar_chart2d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new bar chart with specified size.
 *
 * Returns: (transfer full): a new #LrgBarChart2D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBarChart2D *
lrg_bar_chart2d_new_with_size (gfloat width,
                               gfloat height);

/* ==========================================================================
 * Bar Mode
 * ========================================================================== */

/**
 * lrg_bar_chart2d_get_bar_mode:
 * @self: an #LrgBarChart2D
 *
 * Gets the bar grouping mode.
 *
 * Returns: the bar mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartBarMode
lrg_bar_chart2d_get_bar_mode (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_bar_mode:
 * @self: an #LrgBarChart2D
 * @mode: the bar mode
 *
 * Sets the bar grouping mode (grouped, stacked, or percent).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_bar_mode (LrgBarChart2D   *self,
                              LrgChartBarMode  mode);

/* ==========================================================================
 * Orientation
 * ========================================================================== */

/**
 * lrg_bar_chart2d_get_orientation:
 * @self: an #LrgBarChart2D
 *
 * Gets the bar orientation.
 *
 * Returns: the orientation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgChartOrientation
lrg_bar_chart2d_get_orientation (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_orientation:
 * @self: an #LrgBarChart2D
 * @orientation: the orientation
 *
 * Sets the bar orientation (vertical or horizontal).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_orientation (LrgBarChart2D       *self,
                                 LrgChartOrientation  orientation);

/* ==========================================================================
 * Bar Appearance
 * ========================================================================== */

/**
 * lrg_bar_chart2d_get_bar_spacing:
 * @self: an #LrgBarChart2D
 *
 * Gets the spacing between bar groups.
 *
 * Returns: the spacing in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart2d_get_bar_spacing (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_bar_spacing:
 * @self: an #LrgBarChart2D
 * @spacing: spacing in pixels
 *
 * Sets the spacing between bar groups.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_bar_spacing (LrgBarChart2D *self,
                                 gfloat         spacing);

/**
 * lrg_bar_chart2d_get_bar_width_ratio:
 * @self: an #LrgBarChart2D
 *
 * Gets the bar width ratio (0.0 to 1.0, where 1.0 means bars touch).
 *
 * Returns: the width ratio
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart2d_get_bar_width_ratio (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_bar_width_ratio:
 * @self: an #LrgBarChart2D
 * @ratio: width ratio (0.0 to 1.0)
 *
 * Sets the bar width ratio.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_bar_width_ratio (LrgBarChart2D *self,
                                     gfloat         ratio);

/**
 * lrg_bar_chart2d_get_corner_radius:
 * @self: an #LrgBarChart2D
 *
 * Gets the bar corner radius.
 *
 * Returns: the corner radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_bar_chart2d_get_corner_radius (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_corner_radius:
 * @self: an #LrgBarChart2D
 * @radius: corner radius in pixels
 *
 * Sets the bar corner radius for rounded bars.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_corner_radius (LrgBarChart2D *self,
                                   gfloat         radius);

/* ==========================================================================
 * Value Labels
 * ========================================================================== */

/**
 * lrg_bar_chart2d_get_show_values:
 * @self: an #LrgBarChart2D
 *
 * Gets whether value labels are shown on bars.
 *
 * Returns: %TRUE if values are shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_bar_chart2d_get_show_values (LrgBarChart2D *self);

/**
 * lrg_bar_chart2d_set_show_values:
 * @self: an #LrgBarChart2D
 * @show: whether to show values
 *
 * Sets whether to display value labels on bars.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_bar_chart2d_set_show_values (LrgBarChart2D *self,
                                 gboolean       show);

G_END_DECLS
