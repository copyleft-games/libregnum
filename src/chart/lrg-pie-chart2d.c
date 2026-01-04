/* lrg-pie-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-pie-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgPieChart2D
{
    LrgChart2D parent_instance;

    LrgChartPieStyle  pie_style;
    gfloat            inner_radius;
    gfloat            explode_offset;
    gfloat            start_angle;
    gboolean          show_labels;
    gboolean          show_percentages;
    gfloat            slice_gap;

    /* Cached slice geometry for hit testing */
    GPtrArray        *slice_info;  /* SliceGeometry* */
};

typedef struct
{
    gint    series_index;
    gint    point_index;
    gfloat  center_x;
    gfloat  center_y;
    gfloat  inner_radius;
    gfloat  outer_radius;
    gfloat  start_angle;  /* radians */
    gfloat  end_angle;    /* radians */
} SliceGeometry;

G_DEFINE_TYPE (LrgPieChart2D, lrg_pie_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_PIE_STYLE,
    PROP_INNER_RADIUS,
    PROP_EXPLODE_OFFSET,
    PROP_START_ANGLE,
    PROP_SHOW_LABELS,
    PROP_SHOW_PERCENTAGES,
    PROP_SLICE_GAP,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
clear_slice_cache (LrgPieChart2D *self)
{
    if (self->slice_info != NULL)
        g_ptr_array_set_size (self->slice_info, 0);
}

static void
cache_slice (LrgPieChart2D *self,
             gint           series_index,
             gint           point_index,
             gfloat         center_x,
             gfloat         center_y,
             gfloat         inner_r,
             gfloat         outer_r,
             gfloat         start_angle,
             gfloat         end_angle)
{
    SliceGeometry *geom = g_new (SliceGeometry, 1);

    geom->series_index = series_index;
    geom->point_index = point_index;
    geom->center_x = center_x;
    geom->center_y = center_y;
    geom->inner_radius = inner_r;
    geom->outer_radius = outer_r;
    geom->start_angle = start_angle;
    geom->end_angle = end_angle;

    g_ptr_array_add (self->slice_info, geom);
}

static gboolean
point_in_slice (gfloat                x,
                gfloat                y,
                const SliceGeometry *slice)
{
    gfloat dx = x - slice->center_x;
    gfloat dy = y - slice->center_y;
    gfloat dist = sqrtf (dx * dx + dy * dy);
    gfloat angle;

    /* Check radius */
    if (dist < slice->inner_radius || dist > slice->outer_radius)
        return FALSE;

    /* Check angle */
    angle = atan2f (dy, dx);
    if (angle < 0)
        angle += 2.0f * G_PI;

    /* Handle angle wrap-around */
    if (slice->end_angle > slice->start_angle)
    {
        return angle >= slice->start_angle && angle <= slice->end_angle;
    }
    else
    {
        /* Slice wraps around 0 */
        return angle >= slice->start_angle || angle <= slice->end_angle;
    }
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_pie_chart2d_draw_axes (LrgChart2D *chart2d)
{
    /* Pie charts don't have axes - do nothing */
}

static void
lrg_pie_chart2d_draw_grid (LrgChart2D *chart2d)
{
    /* Pie charts don't have grid - do nothing */
}

static void
lrg_pie_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    guint series_count;
    gdouble total = 0.0;
    gfloat center_x, center_y;
    gfloat radius;
    gfloat current_angle;
    guint i;
    LrgChartDataSeries *series = NULL;
    guint point_count;

    lrg_chart_get_content_bounds (chart, &bounds);
    series_count = lrg_chart_get_series_count (chart);

    if (series_count == 0)
        return;

    /* Clear cached geometry */
    clear_slice_cache (self);

    /* Calculate center and radius */
    center_x = bounds.x + bounds.width / 2.0f;
    center_y = bounds.y + bounds.height / 2.0f;
    radius = fminf (bounds.width, bounds.height) / 2.0f - 10.0f;

    if (radius <= 0)
        return;

    /* For pie charts, we use all series combined or just the first series */
    /* Most pie charts use a single series with multiple points */
    series = lrg_chart_get_series (chart, 0);

    if (!lrg_chart_data_series_get_visible (series))
        return;

    point_count = lrg_chart_data_series_get_point_count (series);
    if (point_count == 0)
        return;

    /* Calculate total */
    for (i = 0; i < point_count; i++)
    {
        const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, i);
        gdouble value = lrg_chart_data_point_get_y (point);
        if (value > 0)
            total += value;
    }

    if (total <= 0)
        return;

    /* Convert start angle to radians */
    current_angle = self->start_angle * G_PI / 180.0f;

    /* Draw each slice */
    for (i = 0; i < point_count; i++)
    {
        const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, i);
        const GrlColor *point_color = lrg_chart_data_point_get_color (point);
        const GrlColor *color;
        gdouble value = lrg_chart_data_point_get_y (point);
        gfloat slice_angle;
        gfloat gap_radians;
        gfloat start_rad, end_rad;
        gfloat slice_center_x, slice_center_y;
        gfloat inner_r, outer_r;

        if (value <= 0)
            continue;

        /* Use point color if set, otherwise generate from index */
        if (point_color != NULL)
        {
            color = point_color;
        }
        else
        {
            /* Generate color based on index */
            static const GrlColor palette[] = {
                { 100, 149, 237, 255 },  /* Cornflower blue */
                { 255, 127, 80, 255 },   /* Coral */
                { 50, 205, 50, 255 },    /* Lime green */
                { 255, 215, 0, 255 },    /* Gold */
                { 147, 112, 219, 255 },  /* Medium purple */
                { 255, 99, 71, 255 },    /* Tomato */
                { 64, 224, 208, 255 },   /* Turquoise */
                { 255, 182, 193, 255 },  /* Light pink */
            };
            color = &palette[i % G_N_ELEMENTS (palette)];
        }

        /* Calculate slice angle */
        slice_angle = (gfloat)(value / total * 2.0 * G_PI);
        gap_radians = self->slice_gap * G_PI / 180.0f;

        start_rad = current_angle + gap_radians / 2.0f;
        end_rad = current_angle + slice_angle - gap_radians / 2.0f;

        /* Handle exploded slices */
        slice_center_x = center_x;
        slice_center_y = center_y;

        if (self->pie_style == LRG_CHART_PIE_EXPLODED && self->explode_offset > 0)
        {
            gfloat mid_angle = (start_rad + end_rad) / 2.0f;
            slice_center_x += cosf (mid_angle) * self->explode_offset;
            slice_center_y += sinf (mid_angle) * self->explode_offset;
        }

        /* Calculate radii */
        outer_r = radius;
        inner_r = (self->pie_style == LRG_CHART_PIE_DONUT) ?
                  radius * self->inner_radius : 0;

        /* Draw slice */
        if (inner_r > 0)
        {
            /* Draw donut slice (ring sector) */
            g_autoptr(GrlVector2) center_vec = grl_vector2_new (slice_center_x, slice_center_y);
            grl_draw_ring (center_vec,
                          inner_r, outer_r,
                          start_rad * 180.0f / G_PI,
                          end_rad * 180.0f / G_PI,
                          32, color);
        }
        else
        {
            /* Draw solid pie slice */
            g_autoptr(GrlVector2) center_vec = grl_vector2_new (slice_center_x, slice_center_y);
            grl_draw_circle_sector (center_vec,
                                    outer_r,
                                    start_rad * 180.0f / G_PI,
                                    end_rad * 180.0f / G_PI,
                                    32, color);
        }

        /* Cache for hit testing */
        cache_slice (self, 0, i, slice_center_x, slice_center_y,
                    inner_r, outer_r, start_rad, end_rad);

        /* Draw labels */
        if (self->show_labels)
        {
            const gchar *label = lrg_chart_data_point_get_label (point);
            const GrlColor *text_color = lrg_chart_get_text_color (chart);
            gfloat mid_angle = (start_rad + end_rad) / 2.0f;
            gfloat label_radius = (inner_r + outer_r) / 2.0f;
            gfloat label_x = slice_center_x + cosf (mid_angle) * label_radius;
            gfloat label_y = slice_center_y + sinf (mid_angle) * label_radius;
            gchar text[64];

            if (self->show_percentages)
            {
                gdouble percent = value / total * 100.0;
                if (label != NULL)
                    g_snprintf (text, sizeof (text), "%s (%.1f%%)", label, percent);
                else
                    g_snprintf (text, sizeof (text), "%.1f%%", percent);
            }
            else if (label != NULL)
            {
                g_snprintf (text, sizeof (text), "%s", label);
            }
            else
            {
                g_snprintf (text, sizeof (text), "%.1f", value);
            }

            grl_draw_text (text, (gint)(label_x - 20), (gint)(label_y - 5), 10, text_color);
        }

        current_angle += slice_angle;
    }
}

static gboolean
lrg_pie_chart2d_hit_test (LrgChart        *chart,
                          gfloat           x,
                          gfloat           y,
                          LrgChartHitInfo *out_hit)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (chart);
    guint i;

    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    /* Check each cached slice */
    for (i = 0; i < self->slice_info->len; i++)
    {
        SliceGeometry *geom = g_ptr_array_index (self->slice_info, i);

        if (point_in_slice (x, y, geom))
        {
            if (out_hit != NULL)
            {
                LrgChartDataSeries *series;
                const LrgChartDataPoint *point;
                GrlRectangle bounds;
                gfloat mid_angle;

                lrg_chart_hit_info_set_series_index (out_hit, geom->series_index);
                lrg_chart_hit_info_set_point_index (out_hit, geom->point_index);

                /* Calculate center of slice for tooltip position */
                mid_angle = (geom->start_angle + geom->end_angle) / 2.0f;
                lrg_chart_hit_info_set_screen_x (out_hit,
                    geom->center_x + cosf (mid_angle) * (geom->inner_radius + geom->outer_radius) / 2.0f);
                lrg_chart_hit_info_set_screen_y (out_hit,
                    geom->center_y + sinf (mid_angle) * (geom->inner_radius + geom->outer_radius) / 2.0f);

                series = lrg_chart_get_series (chart, geom->series_index);
                point = lrg_chart_data_series_get_point (series, geom->point_index);
                lrg_chart_hit_info_set_data_point (out_hit, point);

                /* Rough bounds */
                bounds.x = geom->center_x - geom->outer_radius;
                bounds.y = geom->center_y - geom->outer_radius;
                bounds.width = geom->outer_radius * 2.0f;
                bounds.height = geom->outer_radius * 2.0f;
                lrg_chart_hit_info_set_bounds (out_hit, &bounds);
            }
            return TRUE;
        }
    }

    return FALSE;
}

static void
lrg_pie_chart2d_update_data (LrgChart *chart)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (chart);

    /* Chain up */
    LRG_CHART_CLASS (lrg_pie_chart2d_parent_class)->update_data (chart);

    /* Clear cached geometry */
    clear_slice_cache (self);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_pie_chart2d_finalize (GObject *object)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (object);

    g_ptr_array_unref (self->slice_info);

    G_OBJECT_CLASS (lrg_pie_chart2d_parent_class)->finalize (object);
}

static void
lrg_pie_chart2d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_PIE_STYLE:
        g_value_set_enum (value, self->pie_style);
        break;
    case PROP_INNER_RADIUS:
        g_value_set_float (value, self->inner_radius);
        break;
    case PROP_EXPLODE_OFFSET:
        g_value_set_float (value, self->explode_offset);
        break;
    case PROP_START_ANGLE:
        g_value_set_float (value, self->start_angle);
        break;
    case PROP_SHOW_LABELS:
        g_value_set_boolean (value, self->show_labels);
        break;
    case PROP_SHOW_PERCENTAGES:
        g_value_set_boolean (value, self->show_percentages);
        break;
    case PROP_SLICE_GAP:
        g_value_set_float (value, self->slice_gap);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pie_chart2d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgPieChart2D *self = LRG_PIE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_PIE_STYLE:
        lrg_pie_chart2d_set_pie_style (self, g_value_get_enum (value));
        break;
    case PROP_INNER_RADIUS:
        lrg_pie_chart2d_set_inner_radius (self, g_value_get_float (value));
        break;
    case PROP_EXPLODE_OFFSET:
        lrg_pie_chart2d_set_explode_offset (self, g_value_get_float (value));
        break;
    case PROP_START_ANGLE:
        lrg_pie_chart2d_set_start_angle (self, g_value_get_float (value));
        break;
    case PROP_SHOW_LABELS:
        lrg_pie_chart2d_set_show_labels (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_PERCENTAGES:
        lrg_pie_chart2d_set_show_percentages (self, g_value_get_boolean (value));
        break;
    case PROP_SLICE_GAP:
        lrg_pie_chart2d_set_slice_gap (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pie_chart2d_class_init (LrgPieChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_pie_chart2d_finalize;
    object_class->get_property = lrg_pie_chart2d_get_property;
    object_class->set_property = lrg_pie_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_pie_chart2d_hit_test;
    chart_class->update_data = lrg_pie_chart2d_update_data;

    /* Override chart2d methods - no axes/grid for pie */
    chart2d_class->draw_axes = lrg_pie_chart2d_draw_axes;
    chart2d_class->draw_grid = lrg_pie_chart2d_draw_grid;
    chart2d_class->draw_data = lrg_pie_chart2d_draw_data;

    /* Properties */
    properties[PROP_PIE_STYLE] =
        g_param_spec_enum ("pie-style",
                           "Pie Style",
                           "Pie chart style",
                           LRG_TYPE_CHART_PIE_STYLE,
                           LRG_CHART_PIE_NORMAL,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_INNER_RADIUS] =
        g_param_spec_float ("inner-radius",
                            "Inner Radius",
                            "Inner radius ratio for donut",
                            0.0f, 0.9f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_EXPLODE_OFFSET] =
        g_param_spec_float ("explode-offset",
                            "Explode Offset",
                            "Offset for exploded slices",
                            0.0f, 100.0f, 15.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_START_ANGLE] =
        g_param_spec_float ("start-angle",
                            "Start Angle",
                            "Starting angle in degrees (270 = top, 0 = right)",
                            0.0f, 360.0f, 270.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_LABELS] =
        g_param_spec_boolean ("show-labels",
                              "Show Labels",
                              "Show slice labels",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_PERCENTAGES] =
        g_param_spec_boolean ("show-percentages",
                              "Show Percentages",
                              "Show percentage values",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SLICE_GAP] =
        g_param_spec_float ("slice-gap",
                            "Slice Gap",
                            "Gap between slices in degrees",
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_pie_chart2d_init (LrgPieChart2D *self)
{
    self->pie_style = LRG_CHART_PIE_NORMAL;
    self->inner_radius = 0.5f;
    self->explode_offset = 15.0f;
    self->start_angle = 270.0f;  /* Start at top (12 o'clock position) */
    self->show_labels = TRUE;
    self->show_percentages = TRUE;
    self->slice_gap = 1.0f;

    self->slice_info = g_ptr_array_new_with_free_func (g_free);

    /* Disable legend by default - pie uses labels */
    lrg_chart2d_set_show_legend (LRG_CHART2D (self), FALSE);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgPieChart2D *
lrg_pie_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_PIE_CHART2D, NULL);
}

LrgPieChart2D *
lrg_pie_chart2d_new_with_size (gfloat width,
                               gfloat height)
{
    return g_object_new (LRG_TYPE_PIE_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Pie Style
 * ========================================================================== */

LrgChartPieStyle
lrg_pie_chart2d_get_pie_style (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), LRG_CHART_PIE_NORMAL);
    return self->pie_style;
}

void
lrg_pie_chart2d_set_pie_style (LrgPieChart2D    *self,
                               LrgChartPieStyle  style)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    if (self->pie_style == style)
        return;

    self->pie_style = style;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PIE_STYLE]);
}

/* ==========================================================================
 * Dimensions
 * ========================================================================== */

gfloat
lrg_pie_chart2d_get_inner_radius (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), 0.0f);
    return self->inner_radius;
}

void
lrg_pie_chart2d_set_inner_radius (LrgPieChart2D *self,
                                  gfloat         radius)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    radius = CLAMP (radius, 0.0f, 0.9f);

    if (self->inner_radius == radius)
        return;

    self->inner_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INNER_RADIUS]);
}

gfloat
lrg_pie_chart2d_get_explode_offset (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), 0.0f);
    return self->explode_offset;
}

void
lrg_pie_chart2d_set_explode_offset (LrgPieChart2D *self,
                                    gfloat         offset)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    if (self->explode_offset == offset)
        return;

    self->explode_offset = offset;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPLODE_OFFSET]);
}

/* ==========================================================================
 * Angles
 * ========================================================================== */

gfloat
lrg_pie_chart2d_get_start_angle (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), 0.0f);
    return self->start_angle;
}

void
lrg_pie_chart2d_set_start_angle (LrgPieChart2D *self,
                                 gfloat         angle)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    if (self->start_angle == angle)
        return;

    self->start_angle = angle;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_ANGLE]);
}

/* ==========================================================================
 * Labels
 * ========================================================================== */

gboolean
lrg_pie_chart2d_get_show_labels (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), FALSE);
    return self->show_labels;
}

void
lrg_pie_chart2d_set_show_labels (LrgPieChart2D *self,
                                 gboolean       show)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    show = !!show;

    if (self->show_labels == show)
        return;

    self->show_labels = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LABELS]);
}

gboolean
lrg_pie_chart2d_get_show_percentages (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), FALSE);
    return self->show_percentages;
}

void
lrg_pie_chart2d_set_show_percentages (LrgPieChart2D *self,
                                      gboolean       show)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    show = !!show;

    if (self->show_percentages == show)
        return;

    self->show_percentages = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_PERCENTAGES]);
}

/* ==========================================================================
 * Visual
 * ========================================================================== */

gfloat
lrg_pie_chart2d_get_slice_gap (LrgPieChart2D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART2D (self), 0.0f);
    return self->slice_gap;
}

void
lrg_pie_chart2d_set_slice_gap (LrgPieChart2D *self,
                               gfloat         gap)
{
    g_return_if_fail (LRG_IS_PIE_CHART2D (self));

    if (self->slice_gap == gap)
        return;

    self->slice_gap = gap;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLICE_GAP]);
}
