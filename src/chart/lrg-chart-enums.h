/* lrg-chart-enums.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Enumerations for the charting system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Chart Marker Styles
 * ========================================================================== */

/**
 * LrgChartMarker:
 * @LRG_CHART_MARKER_NONE: No marker displayed
 * @LRG_CHART_MARKER_CIRCLE: Circular marker
 * @LRG_CHART_MARKER_SQUARE: Square marker
 * @LRG_CHART_MARKER_DIAMOND: Diamond/rhombus marker
 * @LRG_CHART_MARKER_TRIANGLE: Triangle marker (pointing up)
 * @LRG_CHART_MARKER_CROSS: Plus sign (+) marker
 * @LRG_CHART_MARKER_X: X-shaped marker
 *
 * Marker styles for data points in line and scatter charts.
 */
typedef enum
{
    LRG_CHART_MARKER_NONE     = 0,
    LRG_CHART_MARKER_CIRCLE   = 1,
    LRG_CHART_MARKER_SQUARE   = 2,
    LRG_CHART_MARKER_DIAMOND  = 3,
    LRG_CHART_MARKER_TRIANGLE = 4,
    LRG_CHART_MARKER_CROSS    = 5,
    LRG_CHART_MARKER_X        = 6
} LrgChartMarker;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_marker_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_MARKER (lrg_chart_marker_get_type ())

/* ==========================================================================
 * Chart Line Styles
 * ========================================================================== */

/**
 * LrgChartLineStyle:
 * @LRG_CHART_LINE_SOLID: Solid line
 * @LRG_CHART_LINE_DASHED: Dashed line
 * @LRG_CHART_LINE_DOTTED: Dotted line
 * @LRG_CHART_LINE_NONE: No line (markers only)
 *
 * Line styles for line and area charts.
 */
typedef enum
{
    LRG_CHART_LINE_SOLID  = 0,
    LRG_CHART_LINE_DASHED = 1,
    LRG_CHART_LINE_DOTTED = 2,
    LRG_CHART_LINE_NONE   = 3
} LrgChartLineStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_line_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_LINE_STYLE (lrg_chart_line_style_get_type ())

/* ==========================================================================
 * Bar Chart Modes
 * ========================================================================== */

/**
 * LrgChartBarMode:
 * @LRG_CHART_BAR_GROUPED: Side-by-side bars for multiple series
 * @LRG_CHART_BAR_STACKED: Stacked bars (values additive)
 * @LRG_CHART_BAR_PERCENT: 100% stacked bars (normalized)
 *
 * Layout mode for multi-series bar charts.
 */
typedef enum
{
    LRG_CHART_BAR_GROUPED = 0,
    LRG_CHART_BAR_STACKED = 1,
    LRG_CHART_BAR_PERCENT = 2
} LrgChartBarMode;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_bar_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_BAR_MODE (lrg_chart_bar_mode_get_type ())

/* ==========================================================================
 * Chart Animation Types
 * ========================================================================== */

/**
 * LrgChartAnimationType:
 * @LRG_CHART_ANIM_NONE: No animation, instant update
 * @LRG_CHART_ANIM_GROW: Grow from zero/baseline
 * @LRG_CHART_ANIM_FADE: Fade in/out
 * @LRG_CHART_ANIM_SLIDE: Slide from edge
 * @LRG_CHART_ANIM_MORPH: Morph from previous values to new values
 *
 * Animation type for chart data transitions.
 */
typedef enum
{
    LRG_CHART_ANIM_NONE  = 0,
    LRG_CHART_ANIM_GROW  = 1,
    LRG_CHART_ANIM_FADE  = 2,
    LRG_CHART_ANIM_SLIDE = 3,
    LRG_CHART_ANIM_MORPH = 4
} LrgChartAnimationType;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_animation_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_ANIMATION_TYPE (lrg_chart_animation_type_get_type ())

/* ==========================================================================
 * Legend Position
 * ========================================================================== */

/**
 * LrgChartLegendPosition:
 * @LRG_CHART_LEGEND_TOP: Legend above the chart
 * @LRG_CHART_LEGEND_BOTTOM: Legend below the chart
 * @LRG_CHART_LEGEND_LEFT: Legend to the left of the chart
 * @LRG_CHART_LEGEND_RIGHT: Legend to the right of the chart
 * @LRG_CHART_LEGEND_NONE: No legend displayed
 *
 * Position of the chart legend.
 */
typedef enum
{
    LRG_CHART_LEGEND_TOP    = 0,
    LRG_CHART_LEGEND_BOTTOM = 1,
    LRG_CHART_LEGEND_LEFT   = 2,
    LRG_CHART_LEGEND_RIGHT  = 3,
    LRG_CHART_LEGEND_NONE   = 4
} LrgChartLegendPosition;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_legend_position_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_LEGEND_POSITION (lrg_chart_legend_position_get_type ())

/* Shorter aliases for convenience */
typedef LrgChartLegendPosition LrgLegendPosition;
#define LRG_TYPE_LEGEND_POSITION LRG_TYPE_CHART_LEGEND_POSITION
#define LRG_LEGEND_TOP    LRG_CHART_LEGEND_TOP
#define LRG_LEGEND_BOTTOM LRG_CHART_LEGEND_BOTTOM
#define LRG_LEGEND_LEFT   LRG_CHART_LEGEND_LEFT
#define LRG_LEGEND_RIGHT  LRG_CHART_LEGEND_RIGHT
#define LRG_LEGEND_NONE   LRG_CHART_LEGEND_NONE

/* ==========================================================================
 * Legend Orientation
 * ========================================================================== */

/**
 * LrgLegendOrientation:
 * @LRG_LEGEND_HORIZONTAL: Items laid out horizontally
 * @LRG_LEGEND_VERTICAL: Items laid out vertically
 *
 * Orientation/layout direction for legend items.
 */
typedef enum
{
    LRG_LEGEND_HORIZONTAL = 0,
    LRG_LEGEND_VERTICAL   = 1
} LrgLegendOrientation;

LRG_AVAILABLE_IN_ALL
GType lrg_legend_orientation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LEGEND_ORIENTATION (lrg_legend_orientation_get_type ())

/* ==========================================================================
 * Gauge Needle Style
 * ========================================================================== */

/**
 * LrgChartGaugeStyle:
 * @LRG_CHART_GAUGE_NEEDLE: Traditional needle gauge
 * @LRG_CHART_GAUGE_ARC: Arc/progress style gauge
 * @LRG_CHART_GAUGE_DIGITAL: Digital display style
 *
 * Visual style for gauge charts.
 */
typedef enum
{
    LRG_CHART_GAUGE_NEEDLE  = 0,
    LRG_CHART_GAUGE_ARC     = 1,
    LRG_CHART_GAUGE_DIGITAL = 2
} LrgChartGaugeStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_gauge_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_GAUGE_STYLE (lrg_chart_gauge_style_get_type ())

/* ==========================================================================
 * Pie Chart Style
 * ========================================================================== */

/**
 * LrgChartPieStyle:
 * @LRG_CHART_PIE_NORMAL: Standard pie chart
 * @LRG_CHART_PIE_DONUT: Donut chart (hollow center)
 * @LRG_CHART_PIE_EXPLODED: Exploded pie (slices separated)
 *
 * Visual style for pie charts.
 */
typedef enum
{
    LRG_CHART_PIE_NORMAL   = 0,
    LRG_CHART_PIE_DONUT    = 1,
    LRG_CHART_PIE_EXPLODED = 2
} LrgChartPieStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_pie_style_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_PIE_STYLE (lrg_chart_pie_style_get_type ())

/* ==========================================================================
 * Area Chart Fill Mode
 * ========================================================================== */

/**
 * LrgChartAreaMode:
 * @LRG_CHART_AREA_NORMAL: Standard area chart
 * @LRG_CHART_AREA_STACKED: Stacked areas
 * @LRG_CHART_AREA_PERCENT: 100% stacked areas (normalized)
 *
 * Fill mode for area charts with multiple series.
 */
typedef enum
{
    LRG_CHART_AREA_NORMAL  = 0,
    LRG_CHART_AREA_STACKED = 1,
    LRG_CHART_AREA_PERCENT = 2
} LrgChartAreaMode;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_area_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_AREA_MODE (lrg_chart_area_mode_get_type ())

/* ==========================================================================
 * Orientation
 * ========================================================================== */

/**
 * LrgChartOrientation:
 * @LRG_CHART_ORIENTATION_VERTICAL: Vertical bars (default)
 * @LRG_CHART_ORIENTATION_HORIZONTAL: Horizontal bars
 *
 * Orientation for bar and similar charts.
 */
typedef enum
{
    LRG_CHART_ORIENTATION_VERTICAL   = 0,
    LRG_CHART_ORIENTATION_HORIZONTAL = 1
} LrgChartOrientation;

LRG_AVAILABLE_IN_ALL
GType lrg_chart_orientation_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_CHART_ORIENTATION (lrg_chart_orientation_get_type ())

G_END_DECLS
