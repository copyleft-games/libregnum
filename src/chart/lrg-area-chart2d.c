/* lrg-area-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-area-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgAreaChart2D
{
    LrgChart2D parent_instance;

    /* Area mode */
    LrgChartAreaMode mode;

    /* Line style */
    gboolean       show_line;
    gfloat         line_width;

    /* Fill */
    gfloat         fill_opacity;

    /* Markers */
    gboolean       show_markers;
    gfloat         marker_size;

    /* Hit testing */
    gfloat         hit_radius;

    /* Cached stacked values for hit testing */
    GPtrArray     *stacked_values;  /* array of gdouble arrays */
};

G_DEFINE_TYPE (LrgAreaChart2D, lrg_area_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_MODE,
    PROP_SHOW_LINE,
    PROP_LINE_WIDTH,
    PROP_FILL_OPACITY,
    PROP_SHOW_MARKERS,
    PROP_MARKER_SIZE,
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

static void
clear_stacked_values (LrgAreaChart2D *self)
{
    if (self->stacked_values != NULL)
    {
        guint i;
        for (i = 0; i < self->stacked_values->len; i++)
        {
            g_free (g_ptr_array_index (self->stacked_values, i));
        }
        g_ptr_array_unref (self->stacked_values);
        self->stacked_values = NULL;
    }
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_area_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgAreaChart2D *self = LRG_AREA_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    guint series_count;
    guint i, j;
    guint max_points;
    gdouble *cumulative = NULL;
    gdouble *prev_cumulative = NULL;
    gdouble *totals = NULL;
    gdouble y_min;
    LrgChartDataSeries *series = NULL;
    guint point_count;

    series_count = lrg_chart_get_series_count (chart);
    y_min = lrg_chart2d_get_y_min (chart2d);

    if (series_count == 0)
        return;

    /* Clear old stacked values */
    clear_stacked_values (self);
    self->stacked_values = g_ptr_array_new ();

    /* Find maximum number of points across all series */
    max_points = 0;
    for (i = 0; i < series_count; i++)
    {
        series = lrg_chart_get_series (chart, i);
        point_count = lrg_chart_data_series_get_point_count (series);
        if (point_count > max_points)
            max_points = point_count;
    }

    if (max_points == 0)
        return;

    /* Allocate cumulative arrays for stacking */
    cumulative = g_new0 (gdouble, max_points);
    prev_cumulative = g_new0 (gdouble, max_points);

    /* For percent mode, calculate totals first */
    if (self->mode == LRG_CHART_AREA_PERCENT)
    {
        totals = g_new0 (gdouble, max_points);
        for (i = 0; i < series_count; i++)
        {
            series = lrg_chart_get_series (chart, i);
            if (!lrg_chart_data_series_get_visible (series))
                continue;

            point_count = lrg_chart_data_series_get_point_count (series);
            for (j = 0; j < point_count; j++)
            {
                const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
                totals[j] += lrg_chart_data_point_get_y (point);
            }
        }
    }

    /* Draw each series (bottom to top for stacking) */
    for (i = 0; i < series_count; i++)
    {
        const GrlColor *color = NULL;
        LrgChartMarker marker;
        GrlVector2 *top_points = NULL;
        GrlVector2 *bottom_points = NULL;
        gfloat *screen_x = NULL;
        gfloat *screen_y = NULL;
        gdouble *stacked_y = NULL;

        series = lrg_chart_get_series (chart, i);

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        color = lrg_chart_data_series_get_color (series);
        marker = lrg_chart_data_series_get_marker (series);
        point_count = lrg_chart_data_series_get_point_count (series);

        if (point_count == 0)
            continue;

        /* Allocate screen coordinate arrays */
        screen_x = g_new (gfloat, point_count);
        screen_y = g_new (gfloat, point_count);
        top_points = g_new (GrlVector2, point_count);
        bottom_points = g_new (GrlVector2, point_count);
        stacked_y = g_new (gdouble, point_count);

        /* Save previous cumulative for stacking */
        memcpy (prev_cumulative, cumulative, max_points * sizeof (gdouble));

        /* Calculate stacked values and convert to screen coordinates */
        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble dx = lrg_chart_data_point_get_x (point);
            gdouble dy = lrg_chart_data_point_get_y (point);
            gdouble actual_y;
            gfloat bottom_screen_y;

            if (self->mode == LRG_CHART_AREA_STACKED || self->mode == LRG_CHART_AREA_PERCENT)
            {
                /* Stacked: add to cumulative */
                if (self->mode == LRG_CHART_AREA_PERCENT && totals[j] > 0)
                {
                    /* Percent: normalize to 0-100 */
                    cumulative[j] += (dy / totals[j]) * 100.0;
                    actual_y = cumulative[j];
                }
                else
                {
                    cumulative[j] += dy;
                    actual_y = cumulative[j];
                }

                /* Bottom is previous cumulative (or y_min for first series) */
                lrg_chart2d_data_to_screen (chart2d, dx, prev_cumulative[j],
                                            NULL, &bottom_screen_y);
            }
            else
            {
                /* Normal: each series independent */
                actual_y = dy;
                lrg_chart2d_data_to_screen (chart2d, dx, y_min, NULL, &bottom_screen_y);
            }

            stacked_y[j] = actual_y;
            lrg_chart2d_data_to_screen (chart2d, dx, actual_y, &screen_x[j], &screen_y[j]);
            top_points[j].x = screen_x[j];
            top_points[j].y = screen_y[j];
            bottom_points[j].x = screen_x[j];
            bottom_points[j].y = bottom_screen_y;
        }

        /* Store stacked values for hit testing */
        g_ptr_array_add (self->stacked_values, stacked_y);

        /* Draw filled area */
        if (point_count >= 2)
        {
            g_autoptr(GrlColor) fill_color = NULL;
            GrlVector2 *poly = NULL;
            guint k;
            guint8 r, g, b, a;

            r = grl_color_get_r (color);
            g = grl_color_get_g (color);
            b = grl_color_get_b (color);
            a = (guint8)(grl_color_get_a (color) * self->fill_opacity);
            fill_color = grl_color_new (r, g, b, a);

            /* Create polygon: top points + bottom points in reverse */
            poly = g_new (GrlVector2, point_count * 2);

            for (k = 0; k < point_count; k++)
                poly[k] = top_points[k];
            for (k = 0; k < point_count; k++)
                poly[point_count + k] = bottom_points[point_count - 1 - k];

            /* Draw using triangle fan - polygon array already closed */
            grl_draw_triangle_fan (poly, (gint)(point_count * 2), fill_color);

            g_free (poly);
        }

        /* Draw line at top of area */
        if (self->show_line && point_count >= 2)
        {
            for (j = 0; j < point_count - 1; j++)
            {
                grl_draw_line_ex (&top_points[j], &top_points[j + 1], self->line_width, color);
            }
        }

        /* Draw markers */
        if (self->show_markers && marker != LRG_CHART_MARKER_NONE)
        {
            for (j = 0; j < point_count; j++)
            {
                draw_marker (screen_x[j], screen_y[j], self->marker_size, marker, color);
            }
        }

        g_free (top_points);
        g_free (bottom_points);
        g_free (screen_x);
        g_free (screen_y);
        /* Don't free stacked_y - owned by stacked_values now */
    }

    g_free (cumulative);
    g_free (prev_cumulative);
    g_free (totals);
}

static gboolean
lrg_area_chart2d_hit_test (LrgChart        *chart,
                           gfloat           x,
                           gfloat           y,
                           LrgChartHitInfo *out_hit)
{
    LrgAreaChart2D *self = LRG_AREA_CHART2D (chart);
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
            gdouble dy;
            gfloat sx, sy;
            gfloat dist_sq;

            /* Use stacked Y if available */
            if (self->stacked_values != NULL && i < self->stacked_values->len)
            {
                gdouble *stacked_y = g_ptr_array_index (self->stacked_values, i);
                dy = stacked_y[j];
            }
            else
            {
                dy = lrg_chart_data_point_get_y (point);
            }

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
lrg_area_chart2d_finalize (GObject *object)
{
    LrgAreaChart2D *self = LRG_AREA_CHART2D (object);

    clear_stacked_values (self);

    G_OBJECT_CLASS (lrg_area_chart2d_parent_class)->finalize (object);
}

static void
lrg_area_chart2d_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgAreaChart2D *self = LRG_AREA_CHART2D (object);

    switch (prop_id)
    {
    case PROP_MODE:
        g_value_set_enum (value, self->mode);
        break;
    case PROP_SHOW_LINE:
        g_value_set_boolean (value, self->show_line);
        break;
    case PROP_LINE_WIDTH:
        g_value_set_float (value, self->line_width);
        break;
    case PROP_FILL_OPACITY:
        g_value_set_float (value, self->fill_opacity);
        break;
    case PROP_SHOW_MARKERS:
        g_value_set_boolean (value, self->show_markers);
        break;
    case PROP_MARKER_SIZE:
        g_value_set_float (value, self->marker_size);
        break;
    case PROP_HIT_RADIUS:
        g_value_set_float (value, self->hit_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_area_chart2d_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgAreaChart2D *self = LRG_AREA_CHART2D (object);

    switch (prop_id)
    {
    case PROP_MODE:
        lrg_area_chart2d_set_mode (self, g_value_get_enum (value));
        break;
    case PROP_SHOW_LINE:
        lrg_area_chart2d_set_show_line (self, g_value_get_boolean (value));
        break;
    case PROP_LINE_WIDTH:
        lrg_area_chart2d_set_line_width (self, g_value_get_float (value));
        break;
    case PROP_FILL_OPACITY:
        lrg_area_chart2d_set_fill_opacity (self, g_value_get_float (value));
        break;
    case PROP_SHOW_MARKERS:
        lrg_area_chart2d_set_show_markers (self, g_value_get_boolean (value));
        break;
    case PROP_MARKER_SIZE:
        lrg_area_chart2d_set_marker_size (self, g_value_get_float (value));
        break;
    case PROP_HIT_RADIUS:
        lrg_area_chart2d_set_hit_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_area_chart2d_class_init (LrgAreaChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->finalize = lrg_area_chart2d_finalize;
    object_class->get_property = lrg_area_chart2d_get_property;
    object_class->set_property = lrg_area_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_area_chart2d_hit_test;

    /* Override chart2d methods */
    chart2d_class->draw_data = lrg_area_chart2d_draw_data;

    /* Properties */
    properties[PROP_MODE] =
        g_param_spec_enum ("mode",
                           "Mode",
                           "Area stacking mode",
                           LRG_TYPE_CHART_AREA_MODE,
                           LRG_CHART_AREA_NORMAL,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_LINE] =
        g_param_spec_boolean ("show-line",
                              "Show Line",
                              "Show line at top of area",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LINE_WIDTH] =
        g_param_spec_float ("line-width",
                            "Line Width",
                            "Width of the top line",
                            0.5f, 10.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILL_OPACITY] =
        g_param_spec_float ("fill-opacity",
                            "Fill Opacity",
                            "Opacity of area fill",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_MARKERS] =
        g_param_spec_boolean ("show-markers",
                              "Show Markers",
                              "Show markers at data points",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARKER_SIZE] =
        g_param_spec_float ("marker-size",
                            "Marker Size",
                            "Size of markers",
                            2.0f, 20.0f, 6.0f,
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
lrg_area_chart2d_init (LrgAreaChart2D *self)
{
    self->mode = LRG_CHART_AREA_NORMAL;
    self->show_line = TRUE;
    self->line_width = 2.0f;
    self->fill_opacity = 0.5f;
    self->show_markers = FALSE;
    self->marker_size = 6.0f;
    self->hit_radius = 10.0f;
    self->stacked_values = NULL;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgAreaChart2D *
lrg_area_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_AREA_CHART2D, NULL);
}

LrgAreaChart2D *
lrg_area_chart2d_new_with_size (gfloat width,
                                gfloat height)
{
    return g_object_new (LRG_TYPE_AREA_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Area Mode
 * ========================================================================== */

LrgChartAreaMode
lrg_area_chart2d_get_mode (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), LRG_CHART_AREA_NORMAL);
    return self->mode;
}

void
lrg_area_chart2d_set_mode (LrgAreaChart2D   *self,
                           LrgChartAreaMode  mode)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    if (self->mode == mode)
        return;

    self->mode = mode;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

/* ==========================================================================
 * Line Style
 * ========================================================================== */

gboolean
lrg_area_chart2d_get_show_line (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), FALSE);
    return self->show_line;
}

void
lrg_area_chart2d_set_show_line (LrgAreaChart2D *self,
                                gboolean        show)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    show = !!show;

    if (self->show_line == show)
        return;

    self->show_line = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LINE]);
}

gfloat
lrg_area_chart2d_get_line_width (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), 0.0f);
    return self->line_width;
}

void
lrg_area_chart2d_set_line_width (LrgAreaChart2D *self,
                                 gfloat          width)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    if (self->line_width == width)
        return;

    self->line_width = width;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_WIDTH]);
}

/* ==========================================================================
 * Fill Style
 * ========================================================================== */

gfloat
lrg_area_chart2d_get_fill_opacity (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), 0.0f);
    return self->fill_opacity;
}

void
lrg_area_chart2d_set_fill_opacity (LrgAreaChart2D *self,
                                   gfloat          opacity)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->fill_opacity == opacity)
        return;

    self->fill_opacity = opacity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_OPACITY]);
}

/* ==========================================================================
 * Markers
 * ========================================================================== */

gboolean
lrg_area_chart2d_get_show_markers (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), FALSE);
    return self->show_markers;
}

void
lrg_area_chart2d_set_show_markers (LrgAreaChart2D *self,
                                   gboolean        show)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    show = !!show;

    if (self->show_markers == show)
        return;

    self->show_markers = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MARKERS]);
}

gfloat
lrg_area_chart2d_get_marker_size (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), 0.0f);
    return self->marker_size;
}

void
lrg_area_chart2d_set_marker_size (LrgAreaChart2D *self,
                                  gfloat          size)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    if (self->marker_size == size)
        return;

    self->marker_size = size;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_SIZE]);
}

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

gfloat
lrg_area_chart2d_get_hit_radius (LrgAreaChart2D *self)
{
    g_return_val_if_fail (LRG_IS_AREA_CHART2D (self), 0.0f);
    return self->hit_radius;
}

void
lrg_area_chart2d_set_hit_radius (LrgAreaChart2D *self,
                                 gfloat          radius)
{
    g_return_if_fail (LRG_IS_AREA_CHART2D (self));

    if (self->hit_radius == radius)
        return;

    self->hit_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIT_RADIUS]);
}
