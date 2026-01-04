/* lrg-scatter-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scatter-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgScatterChart2D
{
    LrgChart2D parent_instance;

    /* Markers */
    LrgChartMarker default_marker;
    gfloat         marker_size;
    gfloat         marker_opacity;

    /* Bubble mode */
    gboolean       bubble_mode;
    gfloat         min_bubble_size;
    gfloat         max_bubble_size;

    /* Trend line */
    gboolean       show_trend_line;
    gfloat         trend_line_width;

    /* Hit testing */
    gfloat         hit_radius;
};

G_DEFINE_TYPE (LrgScatterChart2D, lrg_scatter_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_DEFAULT_MARKER,
    PROP_MARKER_SIZE,
    PROP_MARKER_OPACITY,
    PROP_BUBBLE_MODE,
    PROP_MIN_BUBBLE_SIZE,
    PROP_MAX_BUBBLE_SIZE,
    PROP_SHOW_TREND_LINE,
    PROP_TREND_LINE_WIDTH,
    PROP_HIT_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static void
draw_marker (gfloat          x,
             gfloat          y,
             gfloat          size,
             LrgChartMarker  marker,
             const GrlColor *color)
{
    gfloat half = size / 2.0f;

    switch (marker)
    {
    case LRG_CHART_MARKER_CIRCLE:
        grl_draw_circle (x, y, half, color);
        break;

    case LRG_CHART_MARKER_SQUARE:
        grl_draw_rectangle (x - half, y - half, size, size, color);
        break;

    case LRG_CHART_MARKER_DIAMOND:
        {
            /* Triangle fan: first point is center, rest form outer vertices */
            GrlVector2 points[6] = {
                { x, y },           /* center */
                { x, y - half },    /* top */
                { x + half, y },    /* right */
                { x, y + half },    /* bottom */
                { x - half, y },    /* left */
                { x, y - half }     /* back to top to close */
            };
            grl_draw_triangle_fan (points, 6, color);
        }
        break;

    case LRG_CHART_MARKER_TRIANGLE:
        grl_draw_triangle (&(GrlVector2){ x, y - half },
                           &(GrlVector2){ x - half, y + half },
                           &(GrlVector2){ x + half, y + half },
                           color);
        break;

    case LRG_CHART_MARKER_CROSS:
        grl_draw_line_ex (&(GrlVector2){ x - half, y },
                          &(GrlVector2){ x + half, y },
                          2.0f, color);
        grl_draw_line_ex (&(GrlVector2){ x, y - half },
                          &(GrlVector2){ x, y + half },
                          2.0f, color);
        break;

    case LRG_CHART_MARKER_X:
        grl_draw_line_ex (&(GrlVector2){ x - half, y - half },
                          &(GrlVector2){ x + half, y + half },
                          2.0f, color);
        grl_draw_line_ex (&(GrlVector2){ x + half, y - half },
                          &(GrlVector2){ x - half, y + half },
                          2.0f, color);
        break;

    case LRG_CHART_MARKER_NONE:
    default:
        break;
    }
}

static gfloat
distance_sq (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    return dx * dx + dy * dy;
}

/*
 * Calculate linear regression coefficients using least squares.
 * Returns FALSE if regression cannot be calculated.
 */
static gboolean
calculate_linear_regression (gdouble *x_vals,
                             gdouble *y_vals,
                             guint    count,
                             gdouble *out_slope,
                             gdouble *out_intercept)
{
    gdouble sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    gdouble mean_x, mean_y;
    gdouble denominator;
    guint i;

    if (count < 2)
        return FALSE;

    for (i = 0; i < count; i++)
    {
        sum_x += x_vals[i];
        sum_y += y_vals[i];
        sum_xy += x_vals[i] * y_vals[i];
        sum_xx += x_vals[i] * x_vals[i];
    }

    mean_x = sum_x / count;
    mean_y = sum_y / count;

    denominator = sum_xx - sum_x * mean_x;
    if (fabs (denominator) < 1e-10)
        return FALSE;

    *out_slope = (sum_xy - sum_x * mean_y) / denominator;
    *out_intercept = mean_y - (*out_slope) * mean_x;

    return TRUE;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_scatter_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgScatterChart2D *self = LRG_SCATTER_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    guint series_count;
    guint i, j;
    gdouble z_min = G_MAXDOUBLE, z_max = -G_MAXDOUBLE;

    series_count = lrg_chart_get_series_count (chart);

    if (series_count == 0)
        return;

    /* For bubble mode, find Z value range */
    if (self->bubble_mode)
    {
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
                gdouble z = lrg_chart_data_point_get_z (point);
                if (z < z_min) z_min = z;
                if (z > z_max) z_max = z;
            }
        }
    }

    /* Draw each series */
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        const GrlColor *base_color;
        LrgChartMarker marker;
        guint point_count;
        gdouble *x_vals = NULL;
        gdouble *y_vals = NULL;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        base_color = lrg_chart_data_series_get_color (series);
        marker = lrg_chart_data_series_get_marker (series);
        if (marker == LRG_CHART_MARKER_NONE)
            marker = self->default_marker;

        point_count = lrg_chart_data_series_get_point_count (series);

        if (point_count == 0)
            continue;

        /* Allocate arrays for trend line calculation */
        if (self->show_trend_line)
        {
            x_vals = g_new (gdouble, point_count);
            y_vals = g_new (gdouble, point_count);
        }

        /* Draw points */
        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble dx = lrg_chart_data_point_get_x (point);
            gdouble dy = lrg_chart_data_point_get_y (point);
            gfloat sx, sy;
            gfloat point_size;
            g_autoptr(GrlColor) point_color = NULL;
            const GrlColor *override_color = NULL;
            const GrlColor *source_color = NULL;
            guint8 r, g, b, a;

            lrg_chart2d_data_to_screen (chart2d, dx, dy, &sx, &sy);

            /* Calculate point size */
            if (self->bubble_mode && z_max > z_min)
            {
                gdouble z = lrg_chart_data_point_get_z (point);
                gdouble t = (z - z_min) / (z_max - z_min);
                point_size = self->min_bubble_size + t * (self->max_bubble_size - self->min_bubble_size);
            }
            else
            {
                gfloat series_size = lrg_chart_data_series_get_marker_size (series);
                point_size = (series_size > 0) ? series_size : self->marker_size;
            }

            /* Check for per-point color override */
            override_color = lrg_chart_data_point_get_color (point);
            source_color = (override_color != NULL) ? override_color : base_color;

            /* Apply opacity */
            r = grl_color_get_r (source_color);
            g = grl_color_get_g (source_color);
            b = grl_color_get_b (source_color);
            a = (guint8)(grl_color_get_a (source_color) * self->marker_opacity);
            point_color = grl_color_new (r, g, b, a);

            draw_marker (sx, sy, point_size, marker, point_color);

            /* Store values for trend line */
            if (self->show_trend_line)
            {
                x_vals[j] = dx;
                y_vals[j] = dy;
            }
        }

        /* Draw trend line */
        if (self->show_trend_line && point_count >= 2)
        {
            gdouble slope, intercept;

            if (calculate_linear_regression (x_vals, y_vals, point_count, &slope, &intercept))
            {
                gdouble x_min = lrg_chart2d_get_x_min (chart2d);
                gdouble x_max = lrg_chart2d_get_x_max (chart2d);
                gdouble y1 = slope * x_min + intercept;
                gdouble y2 = slope * x_max + intercept;
                gfloat sx1, sy1, sx2, sy2;
                GrlColor trend_color = *base_color;
                trend_color.a = (guint8)(trend_color.a * 0.7f);

                lrg_chart2d_data_to_screen (chart2d, x_min, y1, &sx1, &sy1);
                lrg_chart2d_data_to_screen (chart2d, x_max, y2, &sx2, &sy2);

                grl_draw_line_ex (&(GrlVector2){ sx1, sy1 },
                                  &(GrlVector2){ sx2, sy2 },
                                  self->trend_line_width, &trend_color);
            }
        }

        g_free (x_vals);
        g_free (y_vals);
    }
}

static gboolean
lrg_scatter_chart2d_hit_test (LrgChart        *chart,
                              gfloat           x,
                              gfloat           y,
                              LrgChartHitInfo *out_hit)
{
    LrgScatterChart2D *self = LRG_SCATTER_CHART2D (chart);
    LrgChart2D *chart2d = LRG_CHART2D (chart);
    guint series_count;
    guint i, j;
    gfloat best_dist_sq = G_MAXFLOAT;
    gint best_series = -1;
    gint best_point = -1;
    gfloat best_sx = 0, best_sy = 0;
    gfloat hit_radius_sq = self->hit_radius * self->hit_radius;

    if (out_hit != NULL)
        lrg_chart_hit_info_clear (out_hit);

    series_count = lrg_chart_get_series_count (chart);

    /* Find nearest point within hit radius */
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
            gdouble dx = lrg_chart_data_point_get_x (point);
            gdouble dy = lrg_chart_data_point_get_y (point);
            gfloat sx, sy;
            gfloat dist_sq;

            lrg_chart2d_data_to_screen (chart2d, dx, dy, &sx, &sy);
            dist_sq = distance_sq (x, y, sx, sy);

            if (dist_sq < hit_radius_sq && dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best_series = i;
                best_point = j;
                best_sx = sx;
                best_sy = sy;
            }
        }
    }

    if (best_series >= 0)
    {
        if (out_hit != NULL)
        {
            LrgChartDataSeries *series = lrg_chart_get_series (chart, best_series);
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, best_point);
            GrlRectangle bounds;

            lrg_chart_hit_info_set_series_index (out_hit, best_series);
            lrg_chart_hit_info_set_point_index (out_hit, best_point);
            lrg_chart_hit_info_set_screen_x (out_hit, best_sx);
            lrg_chart_hit_info_set_screen_y (out_hit, best_sy);
            lrg_chart_hit_info_set_data_point (out_hit, point);

            /* Create bounds around the hit point */
            bounds.x = best_sx - self->hit_radius;
            bounds.y = best_sy - self->hit_radius;
            bounds.width = self->hit_radius * 2.0f;
            bounds.height = self->hit_radius * 2.0f;
            lrg_chart_hit_info_set_bounds (out_hit, &bounds);
        }
        return TRUE;
    }

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scatter_chart2d_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgScatterChart2D *self = LRG_SCATTER_CHART2D (object);

    switch (prop_id)
    {
    case PROP_DEFAULT_MARKER:
        g_value_set_enum (value, self->default_marker);
        break;
    case PROP_MARKER_SIZE:
        g_value_set_float (value, self->marker_size);
        break;
    case PROP_MARKER_OPACITY:
        g_value_set_float (value, self->marker_opacity);
        break;
    case PROP_BUBBLE_MODE:
        g_value_set_boolean (value, self->bubble_mode);
        break;
    case PROP_MIN_BUBBLE_SIZE:
        g_value_set_float (value, self->min_bubble_size);
        break;
    case PROP_MAX_BUBBLE_SIZE:
        g_value_set_float (value, self->max_bubble_size);
        break;
    case PROP_SHOW_TREND_LINE:
        g_value_set_boolean (value, self->show_trend_line);
        break;
    case PROP_TREND_LINE_WIDTH:
        g_value_set_float (value, self->trend_line_width);
        break;
    case PROP_HIT_RADIUS:
        g_value_set_float (value, self->hit_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_scatter_chart2d_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgScatterChart2D *self = LRG_SCATTER_CHART2D (object);

    switch (prop_id)
    {
    case PROP_DEFAULT_MARKER:
        lrg_scatter_chart2d_set_default_marker (self, g_value_get_enum (value));
        break;
    case PROP_MARKER_SIZE:
        lrg_scatter_chart2d_set_marker_size (self, g_value_get_float (value));
        break;
    case PROP_MARKER_OPACITY:
        lrg_scatter_chart2d_set_marker_opacity (self, g_value_get_float (value));
        break;
    case PROP_BUBBLE_MODE:
        lrg_scatter_chart2d_set_bubble_mode (self, g_value_get_boolean (value));
        break;
    case PROP_MIN_BUBBLE_SIZE:
        lrg_scatter_chart2d_set_min_bubble_size (self, g_value_get_float (value));
        break;
    case PROP_MAX_BUBBLE_SIZE:
        lrg_scatter_chart2d_set_max_bubble_size (self, g_value_get_float (value));
        break;
    case PROP_SHOW_TREND_LINE:
        lrg_scatter_chart2d_set_show_trend_line (self, g_value_get_boolean (value));
        break;
    case PROP_TREND_LINE_WIDTH:
        lrg_scatter_chart2d_set_trend_line_width (self, g_value_get_float (value));
        break;
    case PROP_HIT_RADIUS:
        lrg_scatter_chart2d_set_hit_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_scatter_chart2d_class_init (LrgScatterChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->get_property = lrg_scatter_chart2d_get_property;
    object_class->set_property = lrg_scatter_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_scatter_chart2d_hit_test;

    /* Override chart2d methods */
    chart2d_class->draw_data = lrg_scatter_chart2d_draw_data;

    /* Properties */
    properties[PROP_DEFAULT_MARKER] =
        g_param_spec_enum ("default-marker",
                           "Default Marker",
                           "Default marker style",
                           LRG_TYPE_CHART_MARKER,
                           LRG_CHART_MARKER_CIRCLE,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARKER_SIZE] =
        g_param_spec_float ("marker-size",
                            "Marker Size",
                            "Default marker size",
                            2.0f, 50.0f, 8.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARKER_OPACITY] =
        g_param_spec_float ("marker-opacity",
                            "Marker Opacity",
                            "Marker opacity",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_BUBBLE_MODE] =
        g_param_spec_boolean ("bubble-mode",
                              "Bubble Mode",
                              "Use Z value for marker size",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MIN_BUBBLE_SIZE] =
        g_param_spec_float ("min-bubble-size",
                            "Min Bubble Size",
                            "Minimum bubble size",
                            2.0f, 100.0f, 5.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_BUBBLE_SIZE] =
        g_param_spec_float ("max-bubble-size",
                            "Max Bubble Size",
                            "Maximum bubble size",
                            5.0f, 200.0f, 40.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_TREND_LINE] =
        g_param_spec_boolean ("show-trend-line",
                              "Show Trend Line",
                              "Show linear regression trend line",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_TREND_LINE_WIDTH] =
        g_param_spec_float ("trend-line-width",
                            "Trend Line Width",
                            "Width of trend line",
                            0.5f, 10.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_HIT_RADIUS] =
        g_param_spec_float ("hit-radius",
                            "Hit Radius",
                            "Hit test radius for points",
                            1.0f, 50.0f, 12.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scatter_chart2d_init (LrgScatterChart2D *self)
{
    self->default_marker = LRG_CHART_MARKER_CIRCLE;
    self->marker_size = 8.0f;
    self->marker_opacity = 1.0f;
    self->bubble_mode = FALSE;
    self->min_bubble_size = 5.0f;
    self->max_bubble_size = 40.0f;
    self->show_trend_line = FALSE;
    self->trend_line_width = 2.0f;
    self->hit_radius = 12.0f;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgScatterChart2D *
lrg_scatter_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_SCATTER_CHART2D, NULL);
}

LrgScatterChart2D *
lrg_scatter_chart2d_new_with_size (gfloat width,
                                   gfloat height)
{
    return g_object_new (LRG_TYPE_SCATTER_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Marker Configuration
 * ========================================================================== */

LrgChartMarker
lrg_scatter_chart2d_get_default_marker (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), LRG_CHART_MARKER_NONE);
    return self->default_marker;
}

void
lrg_scatter_chart2d_set_default_marker (LrgScatterChart2D *self,
                                        LrgChartMarker     marker)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->default_marker == marker)
        return;

    self->default_marker = marker;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_MARKER]);
}

gfloat
lrg_scatter_chart2d_get_marker_size (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->marker_size;
}

void
lrg_scatter_chart2d_set_marker_size (LrgScatterChart2D *self,
                                     gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->marker_size == size)
        return;

    self->marker_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_SIZE]);
}

gfloat
lrg_scatter_chart2d_get_marker_opacity (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->marker_opacity;
}

void
lrg_scatter_chart2d_set_marker_opacity (LrgScatterChart2D *self,
                                        gfloat             opacity)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->marker_opacity == opacity)
        return;

    self->marker_opacity = opacity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_OPACITY]);
}

/* ==========================================================================
 * Bubble Mode
 * ========================================================================== */

gboolean
lrg_scatter_chart2d_get_bubble_mode (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), FALSE);
    return self->bubble_mode;
}

void
lrg_scatter_chart2d_set_bubble_mode (LrgScatterChart2D *self,
                                     gboolean           enabled)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    enabled = !!enabled;

    if (self->bubble_mode == enabled)
        return;

    self->bubble_mode = enabled;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUBBLE_MODE]);
}

gfloat
lrg_scatter_chart2d_get_min_bubble_size (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->min_bubble_size;
}

void
lrg_scatter_chart2d_set_min_bubble_size (LrgScatterChart2D *self,
                                         gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->min_bubble_size == size)
        return;

    self->min_bubble_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_BUBBLE_SIZE]);
}

gfloat
lrg_scatter_chart2d_get_max_bubble_size (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->max_bubble_size;
}

void
lrg_scatter_chart2d_set_max_bubble_size (LrgScatterChart2D *self,
                                         gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->max_bubble_size == size)
        return;

    self->max_bubble_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_BUBBLE_SIZE]);
}

/* ==========================================================================
 * Trend Line
 * ========================================================================== */

gboolean
lrg_scatter_chart2d_get_show_trend_line (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), FALSE);
    return self->show_trend_line;
}

void
lrg_scatter_chart2d_set_show_trend_line (LrgScatterChart2D *self,
                                         gboolean           show)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    show = !!show;

    if (self->show_trend_line == show)
        return;

    self->show_trend_line = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_TREND_LINE]);
}

gfloat
lrg_scatter_chart2d_get_trend_line_width (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->trend_line_width;
}

void
lrg_scatter_chart2d_set_trend_line_width (LrgScatterChart2D *self,
                                          gfloat             width)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->trend_line_width == width)
        return;

    self->trend_line_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TREND_LINE_WIDTH]);
}

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

gfloat
lrg_scatter_chart2d_get_hit_radius (LrgScatterChart2D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART2D (self), 0.0f);
    return self->hit_radius;
}

void
lrg_scatter_chart2d_set_hit_radius (LrgScatterChart2D *self,
                                    gfloat             radius)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART2D (self));

    if (self->hit_radius == radius)
        return;

    self->hit_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIT_RADIUS]);
}
