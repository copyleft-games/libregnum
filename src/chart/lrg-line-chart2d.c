/* lrg-line-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-line-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgLineChart2D
{
    LrgChart2D parent_instance;

    /* Line style */
    gboolean       smooth;
    gfloat         smoothing_tension;

    /* Area fill */
    gboolean       fill_area;
    gfloat         fill_opacity;

    /* Markers */
    gboolean       show_markers;
    LrgChartMarker default_marker;

    /* Hit testing */
    gfloat         hit_radius;
};

G_DEFINE_TYPE (LrgLineChart2D, lrg_line_chart2d, LRG_TYPE_CHART2D)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_SMOOTH,
    PROP_SMOOTHING_TENSION,
    PROP_FILL_AREA,
    PROP_FILL_OPACITY,
    PROP_SHOW_MARKERS,
    PROP_DEFAULT_MARKER,
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

static void
draw_dashed_line (gfloat          x1,
                  gfloat          y1,
                  gfloat          x2,
                  gfloat          y2,
                  gfloat          thickness,
                  gfloat          dash_length,
                  gfloat          gap_length,
                  const GrlColor *color)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    gfloat length = sqrtf (dx * dx + dy * dy);
    gfloat nx = dx / length;
    gfloat ny = dy / length;
    gfloat pos = 0.0f;
    gboolean drawing = TRUE;

    while (pos < length)
    {
        gfloat segment = drawing ? dash_length : gap_length;
        if (pos + segment > length)
            segment = length - pos;

        if (drawing)
        {
            grl_draw_line_ex (&(GrlVector2){ x1 + nx * pos, y1 + ny * pos },
                              &(GrlVector2){ x1 + nx * (pos + segment), y1 + ny * (pos + segment) },
                              thickness, color);
        }

        pos += segment;
        drawing = !drawing;
    }
}

static gfloat
distance_sq (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    return dx * dx + dy * dy;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_line_chart2d_draw_data (LrgChart2D *chart2d)
{
    LrgLineChart2D *self = LRG_LINE_CHART2D (chart2d);
    LrgChart *chart = LRG_CHART (chart2d);
    GrlRectangle bounds;
    guint series_count;
    guint i, j;
    gdouble y_min;

    lrg_chart_get_content_bounds (chart, &bounds);
    series_count = lrg_chart_get_series_count (chart);
    y_min = lrg_chart2d_get_y_min (chart2d);

    if (series_count == 0)
        return;

    /* Draw each series */
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        const GrlColor *color;
        LrgChartLineStyle line_style;
        LrgChartMarker marker;
        gfloat line_width;
        gfloat marker_size;
        guint point_count;
        GrlVector2 *points;
        gfloat *screen_x;
        gfloat *screen_y;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        color = lrg_chart_data_series_get_color (series);
        line_style = lrg_chart_data_series_get_line_style (series);
        line_width = lrg_chart_data_series_get_line_width (series);
        marker = lrg_chart_data_series_get_marker (series);
        marker_size = lrg_chart_data_series_get_marker_size (series);
        point_count = lrg_chart_data_series_get_point_count (series);

        if (marker == LRG_CHART_MARKER_NONE && self->show_markers)
            marker = self->default_marker;

        if (point_count == 0)
            continue;

        /* Convert all points to screen coordinates */
        screen_x = g_new (gfloat, point_count);
        screen_y = g_new (gfloat, point_count);
        points = g_new (GrlVector2, point_count);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *point = lrg_chart_data_series_get_point (series, j);
            gdouble dx = lrg_chart_data_point_get_x (point);
            gdouble dy = lrg_chart_data_point_get_y (point);

            lrg_chart2d_data_to_screen (chart2d, dx, dy, &screen_x[j], &screen_y[j]);
            points[j].x = screen_x[j];
            points[j].y = screen_y[j];
        }

        /* Draw area fill if enabled */
        if (self->fill_area && point_count >= 2)
        {
            g_autoptr(GrlColor) fill_color = NULL;
            GrlVector2 *poly = NULL;
            gfloat baseline_y;
            guint8 r, g, b, a;

            r = grl_color_get_r (color);
            g = grl_color_get_g (color);
            b = grl_color_get_b (color);
            a = (guint8)(grl_color_get_a (color) * self->fill_opacity);
            fill_color = grl_color_new (r, g, b, a);

            /* Create polygon vertices: line points + bottom corners */
            poly = g_new (GrlVector2, point_count + 2);

            for (j = 0; j < point_count; j++)
                poly[j] = points[j];

            /* Bottom right and bottom left */
            lrg_chart2d_data_to_screen (chart2d, 0, y_min, NULL, &baseline_y);

            poly[point_count].x = screen_x[point_count - 1];
            poly[point_count].y = baseline_y;
            poly[point_count + 1].x = screen_x[0];
            poly[point_count + 1].y = baseline_y;

            /* Draw filled polygon using triangle fan */
            grl_draw_triangle_fan (poly, (gint)(point_count + 2), fill_color);

            g_free (poly);
        }

        /* Draw lines */
        if (line_style != LRG_CHART_LINE_NONE && point_count >= 2)
        {
            if (self->smooth && point_count >= 3)
            {
                /* Draw smooth bezier curves - simplified as polyline */
                grl_draw_line_strip (points, point_count, color);
            }
            else
            {
                /* Draw straight line segments */
                for (j = 0; j < point_count - 1; j++)
                {
                    if (line_style == LRG_CHART_LINE_SOLID)
                    {
                        grl_draw_line_ex (&points[j], &points[j + 1], line_width, color);
                    }
                    else if (line_style == LRG_CHART_LINE_DASHED)
                    {
                        draw_dashed_line (points[j].x, points[j].y,
                                         points[j + 1].x, points[j + 1].y,
                                         line_width, 8.0f, 4.0f, color);
                    }
                    else if (line_style == LRG_CHART_LINE_DOTTED)
                    {
                        draw_dashed_line (points[j].x, points[j].y,
                                         points[j + 1].x, points[j + 1].y,
                                         line_width, 2.0f, 4.0f, color);
                    }
                }
            }
        }

        /* Draw markers */
        if (marker != LRG_CHART_MARKER_NONE)
        {
            for (j = 0; j < point_count; j++)
            {
                draw_marker (screen_x[j], screen_y[j], marker_size, marker, color);
            }
        }

        g_free (points);
        g_free (screen_x);
        g_free (screen_y);
    }
}

static gboolean
lrg_line_chart2d_hit_test (LrgChart        *chart,
                           gfloat           x,
                           gfloat           y,
                           LrgChartHitInfo *out_hit)
{
    LrgLineChart2D *self = LRG_LINE_CHART2D (chart);
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
lrg_line_chart2d_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgLineChart2D *self = LRG_LINE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_SMOOTH:
        g_value_set_boolean (value, self->smooth);
        break;
    case PROP_SMOOTHING_TENSION:
        g_value_set_float (value, self->smoothing_tension);
        break;
    case PROP_FILL_AREA:
        g_value_set_boolean (value, self->fill_area);
        break;
    case PROP_FILL_OPACITY:
        g_value_set_float (value, self->fill_opacity);
        break;
    case PROP_SHOW_MARKERS:
        g_value_set_boolean (value, self->show_markers);
        break;
    case PROP_DEFAULT_MARKER:
        g_value_set_enum (value, self->default_marker);
        break;
    case PROP_HIT_RADIUS:
        g_value_set_float (value, self->hit_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_line_chart2d_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgLineChart2D *self = LRG_LINE_CHART2D (object);

    switch (prop_id)
    {
    case PROP_SMOOTH:
        lrg_line_chart2d_set_smooth (self, g_value_get_boolean (value));
        break;
    case PROP_SMOOTHING_TENSION:
        lrg_line_chart2d_set_smoothing_tension (self, g_value_get_float (value));
        break;
    case PROP_FILL_AREA:
        lrg_line_chart2d_set_fill_area (self, g_value_get_boolean (value));
        break;
    case PROP_FILL_OPACITY:
        lrg_line_chart2d_set_fill_opacity (self, g_value_get_float (value));
        break;
    case PROP_SHOW_MARKERS:
        lrg_line_chart2d_set_show_markers (self, g_value_get_boolean (value));
        break;
    case PROP_DEFAULT_MARKER:
        lrg_line_chart2d_set_default_marker (self, g_value_get_enum (value));
        break;
    case PROP_HIT_RADIUS:
        lrg_line_chart2d_set_hit_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_line_chart2d_class_init (LrgLineChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart2DClass *chart2d_class = LRG_CHART2D_CLASS (klass);

    object_class->get_property = lrg_line_chart2d_get_property;
    object_class->set_property = lrg_line_chart2d_set_property;

    /* Override chart methods */
    chart_class->hit_test = lrg_line_chart2d_hit_test;

    /* Override chart2d methods */
    chart2d_class->draw_data = lrg_line_chart2d_draw_data;

    /* Properties */
    properties[PROP_SMOOTH] =
        g_param_spec_boolean ("smooth",
                              "Smooth",
                              "Use smooth bezier curves",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SMOOTHING_TENSION] =
        g_param_spec_float ("smoothing-tension",
                            "Smoothing Tension",
                            "Bezier curve tension",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILL_AREA] =
        g_param_spec_boolean ("fill-area",
                              "Fill Area",
                              "Fill area under line",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FILL_OPACITY] =
        g_param_spec_float ("fill-opacity",
                            "Fill Opacity",
                            "Opacity of area fill",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_MARKERS] =
        g_param_spec_boolean ("show-markers",
                              "Show Markers",
                              "Show markers at data points",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_DEFAULT_MARKER] =
        g_param_spec_enum ("default-marker",
                           "Default Marker",
                           "Default marker style",
                           LRG_TYPE_CHART_MARKER,
                           LRG_CHART_MARKER_CIRCLE,
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
lrg_line_chart2d_init (LrgLineChart2D *self)
{
    self->smooth = FALSE;
    self->smoothing_tension = 0.3f;
    self->fill_area = FALSE;
    self->fill_opacity = 0.3f;
    self->show_markers = TRUE;
    self->default_marker = LRG_CHART_MARKER_CIRCLE;
    self->hit_radius = 10.0f;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgLineChart2D *
lrg_line_chart2d_new (void)
{
    return g_object_new (LRG_TYPE_LINE_CHART2D, NULL);
}

LrgLineChart2D *
lrg_line_chart2d_new_with_size (gfloat width,
                                gfloat height)
{
    return g_object_new (LRG_TYPE_LINE_CHART2D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Line Style
 * ========================================================================== */

gboolean
lrg_line_chart2d_get_smooth (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), FALSE);
    return self->smooth;
}

void
lrg_line_chart2d_set_smooth (LrgLineChart2D *self,
                             gboolean        smooth)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    smooth = !!smooth;

    if (self->smooth == smooth)
        return;

    self->smooth = smooth;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTH]);
}

gfloat
lrg_line_chart2d_get_smoothing_tension (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), 0.0f);
    return self->smoothing_tension;
}

void
lrg_line_chart2d_set_smoothing_tension (LrgLineChart2D *self,
                                        gfloat          tension)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    tension = CLAMP (tension, 0.0f, 1.0f);

    if (self->smoothing_tension == tension)
        return;

    self->smoothing_tension = tension;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTHING_TENSION]);
}

/* ==========================================================================
 * Area Fill
 * ========================================================================== */

gboolean
lrg_line_chart2d_get_fill_area (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), FALSE);
    return self->fill_area;
}

void
lrg_line_chart2d_set_fill_area (LrgLineChart2D *self,
                                gboolean        fill)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    fill = !!fill;

    if (self->fill_area == fill)
        return;

    self->fill_area = fill;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_AREA]);
}

gfloat
lrg_line_chart2d_get_fill_opacity (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), 0.0f);
    return self->fill_opacity;
}

void
lrg_line_chart2d_set_fill_opacity (LrgLineChart2D *self,
                                   gfloat          opacity)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->fill_opacity == opacity)
        return;

    self->fill_opacity = opacity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_OPACITY]);
}

/* ==========================================================================
 * Point Markers
 * ========================================================================== */

gboolean
lrg_line_chart2d_get_show_markers (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), FALSE);
    return self->show_markers;
}

void
lrg_line_chart2d_set_show_markers (LrgLineChart2D *self,
                                   gboolean        show)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    show = !!show;

    if (self->show_markers == show)
        return;

    self->show_markers = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MARKERS]);
}

LrgChartMarker
lrg_line_chart2d_get_default_marker (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), LRG_CHART_MARKER_NONE);
    return self->default_marker;
}

void
lrg_line_chart2d_set_default_marker (LrgLineChart2D *self,
                                     LrgChartMarker  marker)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    if (self->default_marker == marker)
        return;

    self->default_marker = marker;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_MARKER]);
}

/* ==========================================================================
 * Hit Testing Configuration
 * ========================================================================== */

gfloat
lrg_line_chart2d_get_hit_radius (LrgLineChart2D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART2D (self), 0.0f);
    return self->hit_radius;
}

void
lrg_line_chart2d_set_hit_radius (LrgLineChart2D *self,
                                 gfloat          radius)
{
    g_return_if_fail (LRG_IS_LINE_CHART2D (self));

    if (self->hit_radius == radius)
        return;

    self->hit_radius = radius;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIT_RADIUS]);
}
