/* lrg-chart-enums.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GType registration for chart enumerations.
 */

#include "config.h"
#include "lrg-chart-enums.h"

/* ==========================================================================
 * LrgChartMarker
 * ========================================================================== */

GType
lrg_chart_marker_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_MARKER_NONE,     "LRG_CHART_MARKER_NONE",     "none" },
            { LRG_CHART_MARKER_CIRCLE,   "LRG_CHART_MARKER_CIRCLE",   "circle" },
            { LRG_CHART_MARKER_SQUARE,   "LRG_CHART_MARKER_SQUARE",   "square" },
            { LRG_CHART_MARKER_DIAMOND,  "LRG_CHART_MARKER_DIAMOND",  "diamond" },
            { LRG_CHART_MARKER_TRIANGLE, "LRG_CHART_MARKER_TRIANGLE", "triangle" },
            { LRG_CHART_MARKER_CROSS,    "LRG_CHART_MARKER_CROSS",    "cross" },
            { LRG_CHART_MARKER_X,        "LRG_CHART_MARKER_X",        "x" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartMarker"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartLineStyle
 * ========================================================================== */

GType
lrg_chart_line_style_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_LINE_SOLID,  "LRG_CHART_LINE_SOLID",  "solid" },
            { LRG_CHART_LINE_DASHED, "LRG_CHART_LINE_DASHED", "dashed" },
            { LRG_CHART_LINE_DOTTED, "LRG_CHART_LINE_DOTTED", "dotted" },
            { LRG_CHART_LINE_NONE,   "LRG_CHART_LINE_NONE",   "none" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartLineStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartBarMode
 * ========================================================================== */

GType
lrg_chart_bar_mode_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_BAR_GROUPED, "LRG_CHART_BAR_GROUPED", "grouped" },
            { LRG_CHART_BAR_STACKED, "LRG_CHART_BAR_STACKED", "stacked" },
            { LRG_CHART_BAR_PERCENT, "LRG_CHART_BAR_PERCENT", "percent" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartBarMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartAnimationType
 * ========================================================================== */

GType
lrg_chart_animation_type_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_ANIM_NONE,  "LRG_CHART_ANIM_NONE",  "none" },
            { LRG_CHART_ANIM_GROW,  "LRG_CHART_ANIM_GROW",  "grow" },
            { LRG_CHART_ANIM_FADE,  "LRG_CHART_ANIM_FADE",  "fade" },
            { LRG_CHART_ANIM_SLIDE, "LRG_CHART_ANIM_SLIDE", "slide" },
            { LRG_CHART_ANIM_MORPH, "LRG_CHART_ANIM_MORPH", "morph" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartAnimationType"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartLegendPosition
 * ========================================================================== */

GType
lrg_chart_legend_position_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_LEGEND_TOP,    "LRG_CHART_LEGEND_TOP",    "top" },
            { LRG_CHART_LEGEND_BOTTOM, "LRG_CHART_LEGEND_BOTTOM", "bottom" },
            { LRG_CHART_LEGEND_LEFT,   "LRG_CHART_LEGEND_LEFT",   "left" },
            { LRG_CHART_LEGEND_RIGHT,  "LRG_CHART_LEGEND_RIGHT",  "right" },
            { LRG_CHART_LEGEND_NONE,   "LRG_CHART_LEGEND_NONE",   "none" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartLegendPosition"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartGaugeStyle
 * ========================================================================== */

GType
lrg_chart_gauge_style_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_GAUGE_NEEDLE,  "LRG_CHART_GAUGE_NEEDLE",  "needle" },
            { LRG_CHART_GAUGE_ARC,     "LRG_CHART_GAUGE_ARC",     "arc" },
            { LRG_CHART_GAUGE_DIGITAL, "LRG_CHART_GAUGE_DIGITAL", "digital" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartGaugeStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartPieStyle
 * ========================================================================== */

GType
lrg_chart_pie_style_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_PIE_NORMAL,   "LRG_CHART_PIE_NORMAL",   "normal" },
            { LRG_CHART_PIE_DONUT,    "LRG_CHART_PIE_DONUT",    "donut" },
            { LRG_CHART_PIE_EXPLODED, "LRG_CHART_PIE_EXPLODED", "exploded" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartPieStyle"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartAreaMode
 * ========================================================================== */

GType
lrg_chart_area_mode_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_AREA_NORMAL,  "LRG_CHART_AREA_NORMAL",  "normal" },
            { LRG_CHART_AREA_STACKED, "LRG_CHART_AREA_STACKED", "stacked" },
            { LRG_CHART_AREA_PERCENT, "LRG_CHART_AREA_PERCENT", "percent" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartAreaMode"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgChartOrientation
 * ========================================================================== */

GType
lrg_chart_orientation_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_CHART_ORIENTATION_VERTICAL,   "LRG_CHART_ORIENTATION_VERTICAL",   "vertical" },
            { LRG_CHART_ORIENTATION_HORIZONTAL, "LRG_CHART_ORIENTATION_HORIZONTAL", "horizontal" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgChartOrientation"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}

/* ==========================================================================
 * LrgLegendOrientation
 * ========================================================================== */

GType
lrg_legend_orientation_get_type (void)
{
    static gsize g_define_type_id__volatile = 0;

    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        static const GEnumValue values[] = {
            { LRG_LEGEND_HORIZONTAL, "LRG_LEGEND_HORIZONTAL", "horizontal" },
            { LRG_LEGEND_VERTICAL,   "LRG_LEGEND_VERTICAL",   "vertical" },
            { 0, NULL, NULL }
        };
        GType g_define_type_id =
            g_enum_register_static (g_intern_static_string ("LrgLegendOrientation"), values);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }

    return g_define_type_id__volatile;
}
