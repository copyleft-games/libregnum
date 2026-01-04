/* lrg-radar-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-radar-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgRadarChart2D
{
    LrgChart2D parent_instance;

    /* Axis configuration */
    GPtrArray *axis_labels;

    /* Grid */
    guint      grid_levels;
    gboolean   show_grid;
    GrlColor  *grid_color;

    /* Data display */
    gfloat     fill_opacity;
    gboolean   show_points;
    gfloat     point_size;
    gfloat     line_width;

    /* Value range */
    gdouble    max_value;
    gboolean   auto_scale;

    /* Labels */
    gboolean   show_labels;
    gint       label_font_size;

    /* Hit testing */
    gfloat     hit_radius;
};

G_DEFINE_TYPE (LrgRadarChart2D, lrg_radar_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_GRID_LEVELS,
    PROP_SHOW_GRID,
    PROP_GRID_COLOR,
    PROP_FILL_OPACITY,
    PROP_SHOW_POINTS,
    PROP_POINT_SIZE,
    PROP_LINE_WIDTH,
    PROP_MAX_VALUE,
    PROP_AUTO_SCALE,
    PROP_SHOW_LABELS,
    PROP_LABEL_FONT_SIZE,
    PROP_HIT_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gfloat
distance_sq (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    return dx * dx + dy * dy;
}

/*
 * Get the position on the radar for a given axis index and value.
 * Returns screen coordinates.
 */
static void
get_radar_position (LrgRadarChart2D *self,
                    guint            axis_index,
                    guint            axis_count,
                    gdouble          value,
                    gdouble          max_val,
                    gfloat           center_x,
                    gfloat           center_y,
                    gfloat           radius,
                    gfloat          *out_x,
                    gfloat          *out_y)
{
    gdouble angle;
    gdouble normalized;
    gfloat dist;

    /* Start from top (negative Y), go clockwise */
    angle = -G_PI / 2.0 + (2.0 * G_PI * axis_index) / axis_count;
    normalized = (max_val > 0) ? (value / max_val) : 0;
    dist = (gfloat)(normalized * radius);

    if (out_x) *out_x = center_x + dist * (gfloat)cos (angle);
    if (out_y) *out_y = center_y + dist * (gfloat)sin (angle);
}

static gdouble
calculate_auto_max (LrgRadarChart2D *self)
{
    LrgChart *chart = LRG_CHART (self);
    guint series_count;
    guint i, j;
    gdouble max_val = 0;

    series_count = lrg_chart_get_series_count (chart);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        guint point_count;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble y = lrg_chart_data_point_get_y (point);
            if (y > max_val) max_val = y;
        }
    }

    /* Round up to a nice number */
    if (max_val > 0)
    {
        gdouble magnitude = pow (10, floor (log10 (max_val)));
        max_val = ceil (max_val / magnitude) * magnitude;
    }

    return (max_val > 0) ? max_val : 100.0;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_radar_chart2d_draw_axes (LrgChart2D *chart2d)
{
    /* Radar chart doesn't use standard axes - handled in draw_data */
}

static void
lrg_radar_chart2d_draw_grid (LrgChart2D *chart2d)
{
    /* Radar chart doesn't use standard grid - handled in draw_data */
}

static void
lrg_radar_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgRadarChart2D *self = LRG_RADAR_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    gfloat center_x, center_y, radius;
    guint axis_count;
    guint series_count;
    guint i, j;
    gdouble max_val;

    lrg_chart_get_content_bounds (chart, &bounds);
    center_x = bounds.x + bounds.width / 2.0f;
    center_y = bounds.y + bounds.height / 2.0f;
    radius = MIN (bounds.width, bounds.height) / 2.0f * 0.8f;  /* 80% to leave room for labels */

    axis_count = (self->axis_labels != NULL) ? self->axis_labels->len : 0;
    if (axis_count < 3)
    {
        /* Need at least 3 axes for a radar chart - try to infer from data */
        series_count = lrg_chart_get_series_count (chart);
        if (series_count > 0)
        {
            LrgChartDataSeries *first_series = lrg_chart_get_series (chart, 0);
            axis_count = lrg_chart_data_series_get_point_count (first_series);
        }
    }

    if (axis_count < 3)
        return;

    /* Calculate max value */
    max_val = self->auto_scale ? calculate_auto_max (self) : self->max_value;
    if (max_val <= 0)
        max_val = 100.0;

    /* Draw grid */
    if (self->show_grid && self->grid_levels > 0)
    {
        /* Draw concentric polygons */
        for (i = 1; i <= self->grid_levels; i++)
        {
            gfloat level_radius = radius * i / self->grid_levels;
            GrlVector2 *grid_points = g_new (GrlVector2, axis_count);

            for (j = 0; j < axis_count; j++)
            {
                gfloat px, py;
                get_radar_position (self, j, axis_count, max_val, max_val,
                                    center_x, center_y, level_radius, &px, &py);
                grid_points[j].x = px;
                grid_points[j].y = py;
            }

            /* Draw polygon outline */
            for (j = 0; j < axis_count; j++)
            {
                grl_draw_line_ex (&grid_points[j],
                                  &grid_points[(j + 1) % axis_count],
                                  1.0f, self->grid_color);
            }

            g_free (grid_points);
        }

        /* Draw axis lines from center to each vertex */
        for (j = 0; j < axis_count; j++)
        {
            gfloat px, py;
            get_radar_position (self, j, axis_count, max_val, max_val,
                                center_x, center_y, radius, &px, &py);
            grl_draw_line_ex (&(GrlVector2){ center_x, center_y },
                              &(GrlVector2){ px, py },
                              1.0f, self->grid_color);
        }
    }

    /* Draw axis labels */
    if (self->show_labels && self->axis_labels != NULL)
    {
        g_autoptr(GrlColor) label_color = grl_color_new (200, 200, 200, 255);

        for (j = 0; j < axis_count && j < self->axis_labels->len; j++)
        {
            const gchar *label = g_ptr_array_index (self->axis_labels, j);
            if (label != NULL)
            {
                gfloat px, py;
                gfloat label_offset = 10.0f;
                gint text_width;

                /* Position label slightly beyond the axis end */
                get_radar_position (self, j, axis_count, max_val, max_val,
                                    center_x, center_y, radius + label_offset, &px, &py);

                text_width = grl_measure_text (label, self->label_font_size);

                /* Adjust position based on quadrant */
                if (px < center_x)
                    px -= text_width;
                else if (fabs (px - center_x) < 10)
                    px -= text_width / 2;

                grl_draw_text (label, (gint)px, (gint)(py - self->label_font_size / 2),
                               self->label_font_size, label_color);
            }
        }
    }

    /* Draw data series */
    series_count = lrg_chart_get_series_count (chart);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        const GrlColor *color = NULL;
        guint point_count;
        guint draw_count;
        GrlVector2 *data_points = NULL;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        color = lrg_chart_data_series_get_color (series);
        point_count = lrg_chart_data_series_get_point_count (series);

        if (point_count == 0)
            continue;

        /* Use minimum of series points and axis count */
        draw_count = MIN (point_count, axis_count);
        data_points = g_new (GrlVector2, draw_count);

        /* Calculate screen positions for each data point */
        for (j = 0; j < draw_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble value = lrg_chart_data_point_get_y (point);
            gfloat px, py;

            get_radar_position (self, j, axis_count, value, max_val,
                                center_x, center_y, radius, &px, &py);
            data_points[j].x = px;
            data_points[j].y = py;
        }

        /* Draw filled polygon */
        if (draw_count >= 3)
        {
            g_autoptr(GrlColor) fill_color = NULL;
            GrlVector2 *fan_points = NULL;
            guint8 r, g, b, a;

            r = grl_color_get_r (color);
            g = grl_color_get_g (color);
            b = grl_color_get_b (color);
            a = (guint8)(grl_color_get_a (color) * self->fill_opacity);
            fill_color = grl_color_new (r, g, b, a);

            /* For triangle fan: first point is center, then polygon vertices */
            fan_points = g_new (GrlVector2, draw_count + 2);
            fan_points[0].x = center_x;
            fan_points[0].y = center_y;
            for (j = 0; j < draw_count; j++)
                fan_points[j + 1] = data_points[j];
            fan_points[draw_count + 1] = data_points[0]; /* Close the polygon */

            grl_draw_triangle_fan (fan_points, (gint)(draw_count + 2), fill_color);
            g_free (fan_points);
        }

        /* Draw polygon outline */
        for (j = 0; j < draw_count; j++)
        {
            grl_draw_line_ex (&data_points[j],
                              &data_points[(j + 1) % draw_count],
                              self->line_width, color);
        }

        /* Draw data points */
        if (self->show_points)
        {
            for (j = 0; j < draw_count; j++)
            {
                grl_draw_circle (data_points[j].x, data_points[j].y,
                                 self->point_size / 2.0f, color);
            }
        }

        g_free (data_points);
    }
}

static gboolean
lrg_radar_chart2d_hit_test (LrgChart        *chart,
                            gfloat           x,
                            gfloat           y,
                            LrgChartHitInfo *out_hit)
{
    LrgRadarChart2D *self = LRG_RADAR_CHART2D (chart);
    GrlRectangle bounds;
    gfloat center_x, center_y, radius;
    guint axis_count;
    guint series_count;
    guint i, j;
    gdouble max_val;
    gfloat best_dist_sq = G_MAXFLOAT;
    gint best_series = -1;
    gint best_point = -1;
    gfloat best_sx = 0, best_sy = 0;
    gfloat hit_radius_sq = self->hit_radius * self->hit_radius;

    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    lrg_chart_get_content_bounds (chart, &bounds);
    center_x = bounds.x + bounds.width / 2.0f;
    center_y = bounds.y + bounds.height / 2.0f;
    radius = MIN (bounds.width, bounds.height) / 2.0f * 0.8f;

    axis_count = (self->axis_labels != NULL) ? self->axis_labels->len : 0;
    if (axis_count < 3)
    {
        series_count = lrg_chart_get_series_count (chart);
        if (series_count > 0)
        {
            LrgChartDataSeries *first_series = lrg_chart_get_series (chart, 0);
            axis_count = lrg_chart_data_series_get_point_count (first_series);
        }
    }

    if (axis_count < 3)
        return FALSE;

    max_val = self->auto_scale ? calculate_auto_max (self) : self->max_value;
    if (max_val <= 0)
        max_val = 100.0;

    series_count = lrg_chart_get_series_count (chart);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        guint point_count;
        guint draw_count;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        point_count = lrg_chart_data_series_get_point_count (series);
        draw_count = MIN (point_count, axis_count);

        for (j = 0; j < draw_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble value = lrg_chart_data_point_get_y (point);
            gfloat px, py;
            gfloat dist_sq;

            get_radar_position (self, j, axis_count, value, max_val,
                                center_x, center_y, radius, &px, &py);
            dist_sq = distance_sq (x, y, px, py);

            if (dist_sq < hit_radius_sq && dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best_series = i;
                best_point = j;
                best_sx = px;
                best_sy = py;
            }
        }
    }

    if (best_series >= 0)
    {
        if (out_hit != NULL)
        {
            LrgChartDataSeries *series = lrg_chart_get_series (chart, best_series);
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, best_point);
            GrlRectangle hit_bounds;

            lrg_chart_hit_info_set_series_index (out_hit, best_series);
            lrg_chart_hit_info_set_point_index (out_hit, best_point);
            lrg_chart_hit_info_set_screen_x (out_hit, best_sx);
            lrg_chart_hit_info_set_screen_y (out_hit, best_sy);
            lrg_chart_hit_info_set_data_point (out_hit, point);

            hit_bounds.x = best_sx - self->hit_radius;
            hit_bounds.y = best_sy - self->hit_radius;
            hit_bounds.width = self->hit_radius * 2.0f;
            hit_bounds.height = self->hit_radius * 2.0f;
            lrg_chart_hit_info_set_bounds (out_hit, &hit_bounds);
        }
        return TRUE;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_radar_chart2d_finalize (GObject *object)
{
    LrgRadarChart2D *self = LRG_RADAR_CHART2D (object);

    if (self->axis_labels != NULL)
        g_ptr_array_unref (self->axis_labels);
    g_clear_pointer (&self->grid_color, grl_color_free);

    G_OBJECT_CLASS (lrg_radar_chart2d_parent_class)->finalize (object);
}

static void
lrg_radar_chart2d_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgRadarChart2D *self = LRG_RADAR_CHART2D (object);

    switch (prop_id)
    {
    case PROP_GRID_LEVELS:
        g_value_set_uint (value, self->grid_levels);
        break;
    case PROP_SHOW_GRID:
        g_value_set_boolean (value, self->show_grid);
        break;
    case PROP_GRID_COLOR:
        g_value_set_boxed (value, self->grid_color);
        break;
    case PROP_FILL_OPACITY:
        g_value_set_float (value, self->fill_opacity);
        break;
    case PROP_SHOW_POINTS:
        g_value_set_boolean (value, self->show_points);
        break;
    case PROP_POINT_SIZE:
        g_value_set_float (value, self->point_size);
        break;
    case PROP_LINE_WIDTH:
        g_value_set_float (value, self->line_width);
        break;
    case PROP_MAX_VALUE:
        g_value_set_double (value, self->max_value);
        break;
    case PROP_AUTO_SCALE:
        g_value_set_boolean (value, self->auto_scale);
        break;
    case PROP_SHOW_LABELS:
        g_value_set_boolean (value, self->show_labels);
        break;
    case PROP_LABEL_FONT_SIZE:
        g_value_set_int (value, self->label_font_size);
        break;
    case PROP_HIT_RADIUS:
        g_value_set_float (value, self->hit_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_radar_chart2d_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgRadarChart2D *self = LRG_RADAR_CHART2D (object);

    switch (prop_id)
    {
    case PROP_GRID_LEVELS:
        lrg_radar_chart2d_set_grid_levels (self, g_value_get_uint (value));
        break;
    case PROP_SHOW_GRID:
        lrg_radar_chart2d_set_show_grid (self, g_value_get_boolean (value));
        break;
    case PROP_GRID_COLOR:
        lrg_radar_chart2d_set_grid_color (self, g_value_get_boxed (value));
        break;
    case PROP_FILL_OPACITY:
        lrg_radar_chart2d_set_fill_opacity (self, g_value_get_float (value));
        break;
    case PROP_SHOW_POINTS:
        lrg_radar_chart2d_set_show_points (self, g_value_get_boolean (value));
        break;
    case PROP_POINT_SIZE:
        lrg_radar_chart2d_set_point_size (self, g_value_get_float (value));
        break;
    case PROP_LINE_WIDTH:
        lrg_radar_chart2d_set_line_width (self, g_value_get_float (value));
        break;
    case PROP_MAX_VALUE:
        lrg_radar_chart2d_set_max_value (self, g_value_get_double (value));
        break;
    case PROP_AUTO_SCALE:
        lrg_radar_chart2d_set_auto_scale (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_LABELS:
        lrg_radar_chart2d_set_show_labels (self, g_value_get_boolean (value));
        break;
    case PROP_LABEL_FONT_SIZE:
        lrg_radar_chart2d_set_label_font_size (self, g_value_get_int (value));
        break;
    case PROP_HIT_RADIUS:
        lrg_radar_chart2d_set_hit_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_radar_chart2d_class_init (LrgRadarChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_radar_chart2d_finalize;
    object_class->get_property = lrg_radar_chart2d_get_property;
    object_class->set_property = lrg_radar_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_radar_chart2d_hit_test;

    /* Override chart2d methods */
    chart2d_class->draw_axes = lrg_radar_chart2d_draw_axes;
    chart2d_class->draw_grid = lrg_radar_chart2d_draw_grid;
    chart2d_class->draw_data = lrg_radar_chart2d_draw_data;

    /* Properties */
    properties[PROP_GRID_LEVELS] =
        g_param_spec_uint ("grid-levels",
                           "Grid Levels",
                           "Number of concentric grid levels",
                           1, 20, 5,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_GRID] =
        g_param_spec_boolean ("show-grid",
                              "Show Grid",
                              "Show the grid",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRID_COLOR] =
        g_param_spec_boxed ("grid-color",
                            "Grid Color",
                            "Color of the grid",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILL_OPACITY] =
        g_param_spec_float ("fill-opacity",
                            "Fill Opacity",
                            "Opacity of polygon fill",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_POINTS] =
        g_param_spec_boolean ("show-points",
                              "Show Points",
                              "Show data points",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_POINT_SIZE] =
        g_param_spec_float ("point-size",
                            "Point Size",
                            "Size of data points",
                            2.0f, 20.0f, 6.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LINE_WIDTH] =
        g_param_spec_float ("line-width",
                            "Line Width",
                            "Width of polygon outline",
                            0.5f, 10.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_VALUE] =
        g_param_spec_double ("max-value",
                             "Max Value",
                             "Maximum value for scaling",
                             0.0, G_MAXDOUBLE, 100.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_AUTO_SCALE] =
        g_param_spec_boolean ("auto-scale",
                              "Auto Scale",
                              "Auto-calculate max value from data",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_LABELS] =
        g_param_spec_boolean ("show-labels",
                              "Show Labels",
                              "Show axis labels",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LABEL_FONT_SIZE] =
        g_param_spec_int ("label-font-size",
                          "Label Font Size",
                          "Font size for labels",
                          6, 48, 12,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_HIT_RADIUS] =
        g_param_spec_float ("hit-radius",
                            "Hit Radius",
                            "Hit test radius for points",
                            1.0f, 50.0f, 10.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_radar_chart2d_init (LrgRadarChart2D *self)
{
    self->axis_labels = NULL;
    self->grid_levels = 5;
    self->show_grid = TRUE;
    self->grid_color = grl_color_new (100, 100, 100, 150);
    self->fill_opacity = 0.3f;
    self->show_points = TRUE;
    self->point_size = 6.0f;
    self->line_width = 2.0f;
    self->max_value = 100.0;
    self->auto_scale = TRUE;
    self->show_labels = TRUE;
    self->label_font_size = 12;
    self->hit_radius = 10.0f;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgRadarChart2D *
lrg_radar_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_RADAR_CHART2D, NULL);
}

LrgRadarChart2D *
lrg_radar_chart2d_new_with_size (gfloat width,
                                 gfloat height)
{
    return g_object_new (LRG_TYPE_RADAR_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Axis Configuration
 * ========================================================================== */

void
lrg_radar_chart2d_set_axis_labels (LrgRadarChart2D  *self,
                                   const gchar     **labels)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->axis_labels != NULL)
        g_ptr_array_unref (self->axis_labels);

    if (labels != NULL)
    {
        self->axis_labels = g_ptr_array_new_with_free_func (g_free);
        while (*labels != NULL)
        {
            g_ptr_array_add (self->axis_labels, g_strdup (*labels));
            labels++;
        }
    }
    else
    {
        self->axis_labels = NULL;
    }
}

guint
lrg_radar_chart2d_get_axis_count (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0);
    return (self->axis_labels != NULL) ? self->axis_labels->len : 0;
}

const gchar *
lrg_radar_chart2d_get_axis_label (LrgRadarChart2D *self,
                                  guint            index)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), NULL);

    if (self->axis_labels == NULL || index >= self->axis_labels->len)
        return NULL;

    return g_ptr_array_index (self->axis_labels, index);
}

/* ==========================================================================
 * Grid Configuration
 * ========================================================================== */

guint
lrg_radar_chart2d_get_grid_levels (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0);
    return self->grid_levels;
}

void
lrg_radar_chart2d_set_grid_levels (LrgRadarChart2D *self,
                                   guint            levels)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->grid_levels == levels)
        return;

    self->grid_levels = levels;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_LEVELS]);
}

gboolean
lrg_radar_chart2d_get_show_grid (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), FALSE);
    return self->show_grid;
}

void
lrg_radar_chart2d_set_show_grid (LrgRadarChart2D *self,
                                 gboolean         show)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    show = !!show;

    if (self->show_grid == show)
        return;

    self->show_grid = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GRID]);
}

GrlColor *
lrg_radar_chart2d_get_grid_color (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), NULL);
    return self->grid_color;
}

void
lrg_radar_chart2d_set_grid_color (LrgRadarChart2D *self,
                                  GrlColor        *color)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    g_clear_pointer (&self->grid_color, grl_color_free);
    if (color != NULL)
        self->grid_color = grl_color_copy (color);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_COLOR]);
}

/* ==========================================================================
 * Data Display
 * ========================================================================== */

gfloat
lrg_radar_chart2d_get_fill_opacity (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0.0f);
    return self->fill_opacity;
}

void
lrg_radar_chart2d_set_fill_opacity (LrgRadarChart2D *self,
                                    gfloat           opacity)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->fill_opacity == opacity)
        return;

    self->fill_opacity = opacity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_OPACITY]);
}

gboolean
lrg_radar_chart2d_get_show_points (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), FALSE);
    return self->show_points;
}

void
lrg_radar_chart2d_set_show_points (LrgRadarChart2D *self,
                                   gboolean         show)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    show = !!show;

    if (self->show_points == show)
        return;

    self->show_points = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_POINTS]);
}

gfloat
lrg_radar_chart2d_get_point_size (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0.0f);
    return self->point_size;
}

void
lrg_radar_chart2d_set_point_size (LrgRadarChart2D *self,
                                  gfloat           size)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->point_size == size)
        return;

    self->point_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINT_SIZE]);
}

gfloat
lrg_radar_chart2d_get_line_width (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0.0f);
    return self->line_width;
}

void
lrg_radar_chart2d_set_line_width (LrgRadarChart2D *self,
                                  gfloat           width)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->line_width == width)
        return;

    self->line_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_WIDTH]);
}

/* ==========================================================================
 * Value Range
 * ========================================================================== */

gdouble
lrg_radar_chart2d_get_max_value (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0.0);
    return self->max_value;
}

void
lrg_radar_chart2d_set_max_value (LrgRadarChart2D *self,
                                 gdouble          max)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->max_value == max)
        return;

    self->max_value = max;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
}

gboolean
lrg_radar_chart2d_get_auto_scale (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), FALSE);
    return self->auto_scale;
}

void
lrg_radar_chart2d_set_auto_scale (LrgRadarChart2D *self,
                                  gboolean         auto_scale)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    auto_scale = !!auto_scale;

    if (self->auto_scale == auto_scale)
        return;

    self->auto_scale = auto_scale;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_SCALE]);
}

/* ==========================================================================
 * Labels
 * ========================================================================== */

gboolean
lrg_radar_chart2d_get_show_labels (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), FALSE);
    return self->show_labels;
}

void
lrg_radar_chart2d_set_show_labels (LrgRadarChart2D *self,
                                   gboolean         show)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    show = !!show;

    if (self->show_labels == show)
        return;

    self->show_labels = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LABELS]);
}

gint
lrg_radar_chart2d_get_label_font_size (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0);
    return self->label_font_size;
}

void
lrg_radar_chart2d_set_label_font_size (LrgRadarChart2D *self,
                                       gint             size)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->label_font_size == size)
        return;

    self->label_font_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LABEL_FONT_SIZE]);
}

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

gfloat
lrg_radar_chart2d_get_hit_radius (LrgRadarChart2D *self)
{
    g_return_val_if_fail (LRG_IS_RADAR_CHART2D (self), 0.0f);
    return self->hit_radius;
}

void
lrg_radar_chart2d_set_hit_radius (LrgRadarChart2D *self,
                                  gfloat           radius)
{
    g_return_if_fail (LRG_IS_RADAR_CHART2D (self));

    if (self->hit_radius == radius)
        return;

    self->hit_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIT_RADIUS]);
}
