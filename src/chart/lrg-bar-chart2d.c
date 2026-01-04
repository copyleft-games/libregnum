/* lrg-bar-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bar-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgBarChart2D
{
    LrgChart2D parent_instance;

    LrgChartBarMode      bar_mode;
    LrgChartOrientation  orientation;
    gfloat               bar_spacing;
    gfloat               bar_width_ratio;
    gfloat               corner_radius;
    gboolean             show_values;

    /* Cached bar geometry for hit testing */
    GPtrArray           *bar_rects;  /* Array of GrlRectangle* */
    GPtrArray           *bar_info;   /* Parallel array of bar series/point indices */
};

typedef struct
{
    gint series_index;
    gint point_index;
} BarInfo;

G_DEFINE_TYPE (LrgBarChart2D, lrg_bar_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_BAR_MODE,
    PROP_ORIENTATION,
    PROP_BAR_SPACING,
    PROP_BAR_WIDTH_RATIO,
    PROP_CORNER_RADIUS,
    PROP_SHOW_VALUES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
clear_bar_cache (LrgBarChart2D *self)
{
    if (self->bar_rects != NULL)
    {
        g_ptr_array_set_size (self->bar_rects, 0);
    }
    if (self->bar_info != NULL)
    {
        g_ptr_array_set_size (self->bar_info, 0);
    }
}

static void
cache_bar (LrgBarChart2D    *self,
           gint              series_index,
           gint              point_index,
           const GrlRectangle *rect)
{
    GrlRectangle *rect_copy;
    BarInfo *info;

    rect_copy = g_new (GrlRectangle, 1);
    *rect_copy = *rect;
    g_ptr_array_add (self->bar_rects, rect_copy);

    info = g_new (BarInfo, 1);
    info->series_index = series_index;
    info->point_index = point_index;
    g_ptr_array_add (self->bar_info, info);
}

static gboolean
point_in_rect (gfloat             x,
               gfloat             y,
               const GrlRectangle *rect)
{
    return x >= rect->x && x < rect->x + rect->width &&
           y >= rect->y && y < rect->y + rect->height;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_bar_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    guint series_count;
    guint max_points = 0;
    guint i, j;
    gfloat bar_group_width;
    gfloat bar_width;
    gdouble y_min, y_max;

    lrg_chart_get_content_bounds (chart, &bounds);
    series_count = lrg_chart_get_series_count (chart);

    if (series_count == 0)
        return;

    /* Clear cached bar geometry */
    clear_bar_cache (self);

    /* Find max points across all series */
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        guint point_count = lrg_chart_data_series_get_point_count (series);
        if (point_count > max_points)
            max_points = point_count;
    }

    if (max_points == 0)
        return;

    y_min = lrg_chart2d_get_y_min (chart2d);
    y_max = lrg_chart2d_get_y_max (chart2d);

    /* Calculate bar dimensions based on orientation */
    if (self->orientation == LRG_CHART_ORIENTATION_VERTICAL)
    {
        /* Vertical bars */
        bar_group_width = (bounds.width - self->bar_spacing * (max_points - 1)) / max_points;

        if (self->bar_mode == LRG_CHART_BAR_GROUPED)
        {
            /* Grouped: bars side by side */
            bar_width = (bar_group_width * self->bar_width_ratio) / series_count;

            for (j = 0; j < max_points; j++)
            {
                gfloat group_x = bounds.x + j * (bar_group_width + self->bar_spacing);
                gfloat bar_offset = (bar_group_width - bar_width * series_count) / 2.0f;

                for (i = 0; i < series_count; i++)
                {
                    LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
                    const LrgChartDataPoint *point;
                    const GrlColor *color;
                    gdouble value;
                    gfloat bar_x, bar_y, bar_h;
                    GrlRectangle bar_rect;

                    if (!lrg_chart_data_series_get_visible (series))
                        continue;

                    if (j >= lrg_chart_data_series_get_point_count (series))
                        continue;

                    point = lrg_chart_data_series_get_point (series, j);
                    value = lrg_chart_data_point_get_y (point);
                    color = lrg_chart_data_series_get_color (series);

                    /* Calculate bar position */
                    bar_x = group_x + bar_offset + i * bar_width;
                    bar_h = (gfloat)((value - y_min) / (y_max - y_min) * bounds.height);
                    bar_y = bounds.y + bounds.height - bar_h;

                    /* Clamp to bounds */
                    if (bar_h < 0)
                    {
                        bar_y = bounds.y + bounds.height;
                        bar_h = 0;
                    }

                    bar_rect.x = bar_x;
                    bar_rect.y = bar_y;
                    bar_rect.width = bar_width;
                    bar_rect.height = bar_h;

                    /* Draw bar */
                    if (self->corner_radius > 0)
                    {
                        grl_draw_rectangle_rounded (&bar_rect, self->corner_radius, 4, color);
                    }
                    else
                    {
                        grl_draw_rectangle (bar_x, bar_y, bar_width, bar_h, color);
                    }

                    /* Cache for hit testing */
                    cache_bar (self, i, j, &bar_rect);

                    /* Draw value label */
                    if (self->show_values)
                    {
                        gchar label[32];
                        const GrlColor *text_color = lrg_chart_get_text_color (chart);
                        g_snprintf (label, sizeof (label), "%.1f", value);
                        grl_draw_text (label, (gint)(bar_x + bar_width / 2 - 10),
                                      (gint)(bar_y - 15), 10, text_color);
                    }
                }
            }
        }
        else if (self->bar_mode == LRG_CHART_BAR_STACKED ||
                 self->bar_mode == LRG_CHART_BAR_PERCENT)
        {
            /* Stacked: bars on top of each other */
            bar_width = bar_group_width * self->bar_width_ratio;

            for (j = 0; j < max_points; j++)
            {
                gfloat group_x = bounds.x + j * (bar_group_width + self->bar_spacing);
                gfloat bar_x = group_x + (bar_group_width - bar_width) / 2.0f;
                gfloat stack_top = bounds.y + bounds.height;
                gdouble total = 0.0;

                /* Calculate total for percent mode */
                if (self->bar_mode == LRG_CHART_BAR_PERCENT)
                {
                    for (i = 0; i < series_count; i++)
                    {
                        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
                        if (!lrg_chart_data_series_get_visible (series))
                            continue;
                        if (j < lrg_chart_data_series_get_point_count (series))
                        {
                            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
                            total += lrg_chart_data_point_get_y (point);
                        }
                    }
                    if (total == 0.0) total = 1.0;  /* Avoid division by zero */
                }

                for (i = 0; i < series_count; i++)
                {
                    LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
                    const LrgChartDataPoint *point;
                    const GrlColor *color;
                    gdouble value;
                    gfloat bar_h;
                    GrlRectangle bar_rect;

                    if (!lrg_chart_data_series_get_visible (series))
                        continue;

                    if (j >= lrg_chart_data_series_get_point_count (series))
                        continue;

                    point = lrg_chart_data_series_get_point (series, j);
                    value = lrg_chart_data_point_get_y (point);
                    color = lrg_chart_data_series_get_color (series);

                    /* Calculate height */
                    if (self->bar_mode == LRG_CHART_BAR_PERCENT)
                    {
                        bar_h = (gfloat)((value / total) * bounds.height);
                    }
                    else
                    {
                        bar_h = (gfloat)((value - y_min) / (y_max - y_min) * bounds.height);
                    }

                    if (bar_h < 0) bar_h = 0;

                    stack_top -= bar_h;

                    bar_rect.x = bar_x;
                    bar_rect.y = stack_top;
                    bar_rect.width = bar_width;
                    bar_rect.height = bar_h;

                    /* Draw bar */
                    if (self->corner_radius > 0 && i == series_count - 1)
                    {
                        /* Only round top corners for top bar in stack */
                        grl_draw_rectangle_rounded (&bar_rect, self->corner_radius, 4, color);
                    }
                    else
                    {
                        grl_draw_rectangle (bar_x, stack_top, bar_width, bar_h, color);
                    }

                    /* Cache for hit testing */
                    cache_bar (self, i, j, &bar_rect);
                }
            }
        }
    }
    else
    {
        /* Horizontal bars - similar logic but swapped axes */
        bar_group_width = (bounds.height - self->bar_spacing * (max_points - 1)) / max_points;

        if (self->bar_mode == LRG_CHART_BAR_GROUPED)
        {
            bar_width = (bar_group_width * self->bar_width_ratio) / series_count;

            for (j = 0; j < max_points; j++)
            {
                gfloat group_y = bounds.y + j * (bar_group_width + self->bar_spacing);
                gfloat bar_offset = (bar_group_width - bar_width * series_count) / 2.0f;

                for (i = 0; i < series_count; i++)
                {
                    LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
                    const LrgChartDataPoint *point;
                    const GrlColor *color;
                    gdouble value;
                    gfloat bar_y, bar_w;
                    GrlRectangle bar_rect;

                    if (!lrg_chart_data_series_get_visible (series))
                        continue;

                    if (j >= lrg_chart_data_series_get_point_count (series))
                        continue;

                    point = lrg_chart_data_series_get_point (series, j);
                    value = lrg_chart_data_point_get_y (point);
                    color = lrg_chart_data_series_get_color (series);

                    bar_y = group_y + bar_offset + i * bar_width;
                    bar_w = (gfloat)((value - y_min) / (y_max - y_min) * bounds.width);

                    if (bar_w < 0) bar_w = 0;

                    bar_rect.x = bounds.x;
                    bar_rect.y = bar_y;
                    bar_rect.width = bar_w;
                    bar_rect.height = bar_width;

                    /* Draw bar */
                    if (self->corner_radius > 0)
                    {
                        grl_draw_rectangle_rounded (&bar_rect, self->corner_radius, 4, color);
                    }
                    else
                    {
                        grl_draw_rectangle (bounds.x, bar_y, bar_w, bar_width, color);
                    }

                    cache_bar (self, i, j, &bar_rect);

                    if (self->show_values)
                    {
                        gchar label[32];
                        const GrlColor *text_color = lrg_chart_get_text_color (chart);
                        g_snprintf (label, sizeof (label), "%.1f", value);
                        grl_draw_text (label, (gint)(bounds.x + bar_w + 5),
                                      (gint)(bar_y + bar_width / 2 - 5), 10, text_color);
                    }
                }
            }
        }
        /* Similar stacked mode for horizontal - omitted for brevity */
    }
}

static gboolean
lrg_bar_chart2d_hit_test (LrgChart        *chart,
                          gfloat           x,
                          gfloat           y,
                          LrgChartHitInfo *out_hit)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (chart);
    guint i;

    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    /* Check cached bar rectangles */
    for (i = 0; i < self->bar_rects->len; i++)
    {
        GrlRectangle *rect = g_ptr_array_index (self->bar_rects, i);
        BarInfo *info = g_ptr_array_index (self->bar_info, i);

        if (point_in_rect (x, y, rect))
        {
            if (out_hit != NULL)
            {
                LrgChartDataSeries *series;
                const LrgChartDataPoint *point;

                lrg_chart_hit_info_set_series_index (out_hit, info->series_index);
                lrg_chart_hit_info_set_point_index (out_hit, info->point_index);
                lrg_chart_hit_info_set_screen_x (out_hit, rect->x + rect->width / 2.0f);
                lrg_chart_hit_info_set_screen_y (out_hit, rect->y);
                lrg_chart_hit_info_set_bounds (out_hit, rect);

                series = lrg_chart_get_series (chart, info->series_index);
                point = lrg_chart_data_series_get_point (series, info->point_index);
                lrg_chart_hit_info_set_data_point (out_hit, point);
            }
            return TRUE;
        }
    }

    return FALSE;
}

static void
lrg_bar_chart2d_update_data (LrgChart *chart)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (chart);

    /* Chain up first */
    LRG_CHART_CLASS (lrg_bar_chart2d_parent_class)->update_data (chart);

    /* Clear cached geometry - will be rebuilt on next draw */
    clear_bar_cache (self);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_bar_chart2d_finalize (GObject *object)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (object);

    g_ptr_array_unref (self->bar_rects);
    g_ptr_array_unref (self->bar_info);

    G_OBJECT_CLASS (lrg_bar_chart2d_parent_class)->finalize (object);
}

static void
lrg_bar_chart2d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (object);

    switch (prop_id)
    {
    case PROP_BAR_MODE:
        g_value_set_enum (value, self->bar_mode);
        break;
    case PROP_ORIENTATION:
        g_value_set_enum (value, self->orientation);
        break;
    case PROP_BAR_SPACING:
        g_value_set_float (value, self->bar_spacing);
        break;
    case PROP_BAR_WIDTH_RATIO:
        g_value_set_float (value, self->bar_width_ratio);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    case PROP_SHOW_VALUES:
        g_value_set_boolean (value, self->show_values);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bar_chart2d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgBarChart2D *self = LRG_BAR_CHART2D (object);

    switch (prop_id)
    {
    case PROP_BAR_MODE:
        lrg_bar_chart2d_set_bar_mode (self, g_value_get_enum (value));
        break;
    case PROP_ORIENTATION:
        lrg_bar_chart2d_set_orientation (self, g_value_get_enum (value));
        break;
    case PROP_BAR_SPACING:
        lrg_bar_chart2d_set_bar_spacing (self, g_value_get_float (value));
        break;
    case PROP_BAR_WIDTH_RATIO:
        lrg_bar_chart2d_set_bar_width_ratio (self, g_value_get_float (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_bar_chart2d_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_SHOW_VALUES:
        lrg_bar_chart2d_set_show_values (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bar_chart2d_class_init (LrgBarChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_bar_chart2d_finalize;
    object_class->get_property = lrg_bar_chart2d_get_property;
    object_class->set_property = lrg_bar_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_bar_chart2d_hit_test;
    chart_class->update_data = lrg_bar_chart2d_update_data;

    /* Override chart2d methods */
    chart2d_class->draw_data = lrg_bar_chart2d_draw_data;

    /* Properties */
    properties[PROP_BAR_MODE] =
        g_param_spec_enum ("bar-mode",
                           "Bar Mode",
                           "Bar grouping mode",
                           LRG_TYPE_CHART_BAR_MODE,
                           LRG_CHART_BAR_GROUPED,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_ORIENTATION] =
        g_param_spec_enum ("orientation",
                           "Orientation",
                           "Bar orientation",
                           LRG_TYPE_CHART_ORIENTATION,
                           LRG_CHART_ORIENTATION_VERTICAL,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_BAR_SPACING] =
        g_param_spec_float ("bar-spacing",
                            "Bar Spacing",
                            "Space between bar groups",
                            0.0f, 100.0f, 4.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_BAR_WIDTH_RATIO] =
        g_param_spec_float ("bar-width-ratio",
                            "Bar Width Ratio",
                            "Ratio of bar width to available space",
                            0.1f, 1.0f, 0.8f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "Bar corner radius",
                            0.0f, 50.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_VALUES] =
        g_param_spec_boolean ("show-values",
                              "Show Values",
                              "Show value labels on bars",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_bar_chart2d_init (LrgBarChart2D *self)
{
    self->bar_mode = LRG_CHART_BAR_GROUPED;
    self->orientation = LRG_CHART_ORIENTATION_VERTICAL;
    self->bar_spacing = 4.0f;
    self->bar_width_ratio = 0.8f;
    self->corner_radius = 0.0f;
    self->show_values = FALSE;

    self->bar_rects = g_ptr_array_new_with_free_func (g_free);
    self->bar_info = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgBarChart2D *
lrg_bar_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_BAR_CHART2D, NULL);
}

LrgBarChart2D *
lrg_bar_chart2d_new_with_size (gfloat width,
                               gfloat height)
{
    return g_object_new (LRG_TYPE_BAR_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Bar Mode
 * ========================================================================== */

LrgChartBarMode
lrg_bar_chart2d_get_bar_mode (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), LRG_CHART_BAR_GROUPED);
    return self->bar_mode;
}

void
lrg_bar_chart2d_set_bar_mode (LrgBarChart2D   *self,
                              LrgChartBarMode  mode)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    if (self->bar_mode == mode)
        return;

    self->bar_mode = mode;
    lrg_chart_mark_layout_dirty (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_MODE]);
}

/* ==========================================================================
 * Orientation
 * ========================================================================== */

LrgChartOrientation
lrg_bar_chart2d_get_orientation (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), LRG_CHART_ORIENTATION_VERTICAL);
    return self->orientation;
}

void
lrg_bar_chart2d_set_orientation (LrgBarChart2D       *self,
                                 LrgChartOrientation  orientation)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    if (self->orientation == orientation)
        return;

    self->orientation = orientation;
    lrg_chart_mark_layout_dirty (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORIENTATION]);
}

/* ==========================================================================
 * Bar Appearance
 * ========================================================================== */

gfloat
lrg_bar_chart2d_get_bar_spacing (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), 0.0f);
    return self->bar_spacing;
}

void
lrg_bar_chart2d_set_bar_spacing (LrgBarChart2D *self,
                                 gfloat         spacing)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    if (self->bar_spacing == spacing)
        return;

    self->bar_spacing = spacing;
    lrg_chart_mark_layout_dirty (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_SPACING]);
}

gfloat
lrg_bar_chart2d_get_bar_width_ratio (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), 0.0f);
    return self->bar_width_ratio;
}

void
lrg_bar_chart2d_set_bar_width_ratio (LrgBarChart2D *self,
                                     gfloat         ratio)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    ratio = CLAMP (ratio, 0.1f, 1.0f);

    if (self->bar_width_ratio == ratio)
        return;

    self->bar_width_ratio = ratio;
    lrg_chart_mark_layout_dirty (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_WIDTH_RATIO]);
}

gfloat
lrg_bar_chart2d_get_corner_radius (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), 0.0f);
    return self->corner_radius;
}

void
lrg_bar_chart2d_set_corner_radius (LrgBarChart2D *self,
                                   gfloat         radius)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    if (self->corner_radius == radius)
        return;

    self->corner_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}

/* ==========================================================================
 * Value Labels
 * ========================================================================== */

gboolean
lrg_bar_chart2d_get_show_values (LrgBarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART2D (self), FALSE);
    return self->show_values;
}

void
lrg_bar_chart2d_set_show_values (LrgBarChart2D *self,
                                 gboolean       show)
{
    g_return_if_fail (LRG_IS_BAR_CHART2D (self));

    show = !!show;

    if (self->show_values == show)
        return;

    self->show_values = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_VALUES]);
}
