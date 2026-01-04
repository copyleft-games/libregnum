/* lrg-scatter-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScatterChart3D - 3D Scatter Chart widget implementation.
 *
 * Renders data points as markers in 3D space with depth sorting.
 * Supports bubble mode where W value determines marker size.
 */

#include "lrg-scatter-chart3d.h"
#include "lrg-chart-data-series.h"
#include "../lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART

/**
 * LrgScatterChart3D:
 *
 * A 3D scatter chart widget that renders data points as markers.
 */
struct _LrgScatterChart3D
{
    LrgChart3D parent_instance;

    /* Marker properties */
    LrgChartMarker marker_style;
    gfloat marker_size;
    gboolean size_by_value;
    gfloat min_marker_size;
    gfloat max_marker_size;

    /* Display options */
    gboolean show_drop_lines;
    GrlColor *drop_line_color;
    gboolean depth_fade;
    gboolean depth_scale;
};

enum
{
    PROP_0,
    PROP_MARKER_STYLE,
    PROP_MARKER_SIZE,
    PROP_SIZE_BY_VALUE,
    PROP_MIN_MARKER_SIZE,
    PROP_MAX_MARKER_SIZE,
    PROP_SHOW_DROP_LINES,
    PROP_DROP_LINE_COLOR,
    PROP_DEPTH_FADE,
    PROP_DEPTH_SCALE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgScatterChart3D, lrg_scatter_chart3d, LRG_TYPE_CHART3D)

/* ==========================================================================
 * Internal Types
 * ========================================================================== */

/*
 * PointInfo:
 *
 * Information about a scatter point for depth-sorted rendering.
 */
typedef struct
{
    gfloat screen_x, screen_y;
    gfloat floor_x, floor_y;
    gfloat sort_depth;
    gfloat size;
    GrlColor *color;
    LrgChartMarker style;
} PointInfo;

static gint
compare_points_by_depth (gconstpointer a,
                         gconstpointer b)
{
    const PointInfo *pa = a;
    const PointInfo *pb = b;

    /* Sort back to front */
    if (pa->sort_depth > pb->sort_depth)
        return -1;
    if (pa->sort_depth < pb->sort_depth)
        return 1;
    return 0;
}

/* ==========================================================================
 * Marker Drawing
 * ========================================================================== */

static void
draw_marker (LrgChartMarker style,
             gfloat         x,
             gfloat         y,
             gfloat         size,
             GrlColor      *color)
{
    gint ix = (gint)x;
    gint iy = (gint)y;
    gint half = (gint)(size / 2.0f);

    switch (style)
    {
    case LRG_CHART_MARKER_CIRCLE:
        grl_draw_circle (ix, iy, size / 2.0f, color);
        break;

    case LRG_CHART_MARKER_SQUARE:
        grl_draw_rectangle (ix - half, iy - half, (gint)size, (gint)size, color);
        break;

    case LRG_CHART_MARKER_DIAMOND:
        {
            grl_draw_triangle (ix, iy - half,
                               ix + half, iy,
                               ix, iy + half, color);
            grl_draw_triangle (ix, iy - half,
                               ix - half, iy,
                               ix, iy + half, color);
        }
        break;

    case LRG_CHART_MARKER_TRIANGLE:
        grl_draw_triangle (ix, iy - half,
                           ix + half, iy + half,
                           ix - half, iy + half, color);
        break;

    case LRG_CHART_MARKER_CROSS:
        {
            g_autoptr(GrlVector2) h1 = grl_vector2_new (x - size / 2.0f, y);
            g_autoptr(GrlVector2) h2 = grl_vector2_new (x + size / 2.0f, y);
            g_autoptr(GrlVector2) v1 = grl_vector2_new (x, y - size / 2.0f);
            g_autoptr(GrlVector2) v2 = grl_vector2_new (x, y + size / 2.0f);
            grl_draw_line_ex (h1, h2, 2.0f, color);
            grl_draw_line_ex (v1, v2, 2.0f, color);
        }
        break;

    case LRG_CHART_MARKER_X:
        {
            g_autoptr(GrlVector2) d1a = grl_vector2_new (x - size / 2.0f, y - size / 2.0f);
            g_autoptr(GrlVector2) d1b = grl_vector2_new (x + size / 2.0f, y + size / 2.0f);
            g_autoptr(GrlVector2) d2a = grl_vector2_new (x + size / 2.0f, y - size / 2.0f);
            g_autoptr(GrlVector2) d2b = grl_vector2_new (x - size / 2.0f, y + size / 2.0f);
            grl_draw_line_ex (d1a, d1b, 2.0f, color);
            grl_draw_line_ex (d2a, d2b, 2.0f, color);
        }
        break;

    case LRG_CHART_MARKER_NONE:
    default:
        break;
    }
}

/* ==========================================================================
 * Drawing Implementation
 * ========================================================================== */

static void
lrg_scatter_chart3d_draw_data_3d (LrgChart3D *chart3d)
{
    LrgScatterChart3D *self = LRG_SCATTER_CHART3D (chart3d);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *all_series;
    guint series_count;
    gdouble x_min, x_max, y_min, y_max, z_min, z_max;
    gdouble w_min, w_max;
    GArray *points;
    guint i, j;

    all_series = lrg_chart_get_all_series (chart);
    if (all_series == NULL || all_series->len == 0)
        return;

    series_count = all_series->len;
    lrg_chart_get_x_range (chart, &x_min, &x_max);
    lrg_chart_get_y_range (chart, &y_min, &y_max);
    lrg_chart_get_z_range (chart, &z_min, &z_max);

    if (x_max <= x_min) x_max = x_min + 1.0;
    if (y_max <= y_min) y_max = y_min + 1.0;
    if (z_max <= z_min) z_max = z_min + 1.0;

    /* Find W range for bubble sizing */
    w_min = G_MAXDOUBLE;
    w_max = -G_MAXDOUBLE;
    if (self->size_by_value)
    {
        for (i = 0; i < series_count; i++)
        {
            LrgChartDataSeries *series = g_ptr_array_index (all_series, i);
            GPtrArray *series_points;

            if (!lrg_chart_data_series_get_visible (series))
                continue;

            series_points = lrg_chart_data_series_get_points (series);
            if (series_points == NULL)
                continue;

            for (j = 0; j < series_points->len; j++)
            {
                LrgChartDataPoint *pt = g_ptr_array_index (series_points, j);
                if (pt->w < w_min) w_min = pt->w;
                if (pt->w > w_max) w_max = pt->w;
            }
        }
        if (w_max <= w_min)
        {
            w_min = 0.0;
            w_max = 1.0;
        }
    }

    /* Collect points for depth sorting */
    points = g_array_new (FALSE, FALSE, sizeof (PointInfo));

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = g_ptr_array_index (all_series, i);
        GPtrArray *series_points;
        GrlColor *series_color;
        LrgChartMarker series_marker;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        series_points = lrg_chart_data_series_get_points (series);
        if (series_points == NULL)
            continue;

        series_color = lrg_chart_data_series_get_color (series);
        series_marker = lrg_chart_data_series_get_marker_style (series);
        if (series_marker == LRG_CHART_MARKER_NONE)
            series_marker = self->marker_style;

        for (j = 0; j < series_points->len; j++)
        {
            LrgChartDataPoint *pt = g_ptr_array_index (series_points, j);
            PointInfo info;
            gdouble nx, ny, nz;
            gfloat floor_depth;

            /* Normalize to 0-1 range */
            nx = (lrg_chart_data_point_get_x (pt) - x_min) / (x_max - x_min);
            ny = (lrg_chart_data_point_get_y (pt) - y_min) / (y_max - y_min);
            nz = (lrg_chart_data_point_get_z (pt) - z_min) / (z_max - z_min);

            /* Project point */
            lrg_chart3d_project_point (chart3d, nx, ny, nz,
                                       &info.screen_x, &info.screen_y,
                                       &info.sort_depth);

            /* Project floor point for drop line */
            if (self->show_drop_lines)
            {
                lrg_chart3d_project_point (chart3d, nx, 0.0, nz,
                                           &info.floor_x, &info.floor_y,
                                           &floor_depth);
            }
            else
            {
                info.floor_x = info.screen_x;
                info.floor_y = info.screen_y;
            }

            /* Calculate size */
            if (self->size_by_value)
            {
                gdouble nw = (pt->w - w_min) / (w_max - w_min);
                info.size = self->min_marker_size +
                            (gfloat)nw * (self->max_marker_size - self->min_marker_size);
            }
            else
            {
                info.size = self->marker_size;
            }

            /* Apply depth scale if enabled */
            if (self->depth_scale)
            {
                /* Scale based on depth (0 = near = full size, 1 = far = half size) */
                gfloat depth_factor = 1.0f - info.sort_depth * 0.5f;
                info.size *= depth_factor;
            }

            /* Get color */
            if (pt->color != NULL)
            {
                info.color = grl_color_copy (pt->color);
            }
            else
            {
                info.color = grl_color_copy (series_color);
            }

            /* Apply depth fade if enabled */
            if (self->depth_fade)
            {
                guint8 alpha = grl_color_get_a (info.color);
                /* Fade based on depth (0 = near = full alpha, 1 = far = low alpha) */
                gfloat fade = 1.0f - info.sort_depth * 0.7f;
                alpha = (guint8)(alpha * fade);

                GrlColor *faded = grl_color_new (
                    grl_color_get_r (info.color),
                    grl_color_get_g (info.color),
                    grl_color_get_b (info.color),
                    alpha
                );
                grl_color_free (info.color);
                info.color = faded;
            }

            info.style = series_marker;

            g_array_append_val (points, info);
        }
    }

    /* Sort by depth (back to front) */
    g_array_sort (points, compare_points_by_depth);

    /* Draw all points */
    for (i = 0; i < points->len; i++)
    {
        PointInfo *info = &g_array_index (points, PointInfo, i);

        /* Draw drop line first (behind point) */
        if (self->show_drop_lines)
        {
            g_autoptr(GrlVector2) p1 = grl_vector2_new (info->screen_x, info->screen_y);
            g_autoptr(GrlVector2) p2 = grl_vector2_new (info->floor_x, info->floor_y);
            grl_draw_line_ex (p1, p2, 1.0f, self->drop_line_color);
        }

        /* Draw marker */
        draw_marker (info->style, info->screen_x, info->screen_y,
                     info->size, info->color);

        grl_color_free (info->color);
    }

    g_array_free (points, TRUE);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scatter_chart3d_dispose (GObject *object)
{
    LrgScatterChart3D *self = LRG_SCATTER_CHART3D (object);

    g_clear_pointer (&self->drop_line_color, grl_color_free);

    G_OBJECT_CLASS (lrg_scatter_chart3d_parent_class)->dispose (object);
}

static void
lrg_scatter_chart3d_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgScatterChart3D *self = LRG_SCATTER_CHART3D (object);

    switch (prop_id)
    {
    case PROP_MARKER_STYLE:
        g_value_set_enum (value, self->marker_style);
        break;
    case PROP_MARKER_SIZE:
        g_value_set_float (value, self->marker_size);
        break;
    case PROP_SIZE_BY_VALUE:
        g_value_set_boolean (value, self->size_by_value);
        break;
    case PROP_MIN_MARKER_SIZE:
        g_value_set_float (value, self->min_marker_size);
        break;
    case PROP_MAX_MARKER_SIZE:
        g_value_set_float (value, self->max_marker_size);
        break;
    case PROP_SHOW_DROP_LINES:
        g_value_set_boolean (value, self->show_drop_lines);
        break;
    case PROP_DROP_LINE_COLOR:
        g_value_set_boxed (value, self->drop_line_color);
        break;
    case PROP_DEPTH_FADE:
        g_value_set_boolean (value, self->depth_fade);
        break;
    case PROP_DEPTH_SCALE:
        g_value_set_boolean (value, self->depth_scale);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_scatter_chart3d_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgScatterChart3D *self = LRG_SCATTER_CHART3D (object);

    switch (prop_id)
    {
    case PROP_MARKER_STYLE:
        lrg_scatter_chart3d_set_marker_style (self, g_value_get_enum (value));
        break;
    case PROP_MARKER_SIZE:
        lrg_scatter_chart3d_set_marker_size (self, g_value_get_float (value));
        break;
    case PROP_SIZE_BY_VALUE:
        lrg_scatter_chart3d_set_size_by_value (self, g_value_get_boolean (value));
        break;
    case PROP_MIN_MARKER_SIZE:
        lrg_scatter_chart3d_set_min_marker_size (self, g_value_get_float (value));
        break;
    case PROP_MAX_MARKER_SIZE:
        lrg_scatter_chart3d_set_max_marker_size (self, g_value_get_float (value));
        break;
    case PROP_SHOW_DROP_LINES:
        lrg_scatter_chart3d_set_show_drop_lines (self, g_value_get_boolean (value));
        break;
    case PROP_DROP_LINE_COLOR:
        lrg_scatter_chart3d_set_drop_line_color (self, g_value_get_boxed (value));
        break;
    case PROP_DEPTH_FADE:
        lrg_scatter_chart3d_set_depth_fade (self, g_value_get_boolean (value));
        break;
    case PROP_DEPTH_SCALE:
        lrg_scatter_chart3d_set_depth_scale (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_scatter_chart3d_class_init (LrgScatterChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChart3DClass *chart3d_class = LRG_CHART3D_CLASS (klass);

    object_class->dispose = lrg_scatter_chart3d_dispose;
    object_class->get_property = lrg_scatter_chart3d_get_property;
    object_class->set_property = lrg_scatter_chart3d_set_property;

    chart3d_class->draw_data_3d = lrg_scatter_chart3d_draw_data_3d;

    /**
     * LrgScatterChart3D:marker-style:
     *
     * The default marker style.
     *
     * Since: 1.0
     */
    properties[PROP_MARKER_STYLE] =
        g_param_spec_enum ("marker-style",
                           "Marker Style",
                           "Default marker style",
                           LRG_TYPE_CHART_MARKER,
                           LRG_CHART_MARKER_CIRCLE,
                           G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:marker-size:
     *
     * The default marker size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_MARKER_SIZE] =
        g_param_spec_float ("marker-size",
                            "Marker Size",
                            "Default marker size in pixels",
                            1.0f, 100.0f, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:size-by-value:
     *
     * Whether marker size is determined by W value (bubble mode).
     *
     * Since: 1.0
     */
    properties[PROP_SIZE_BY_VALUE] =
        g_param_spec_boolean ("size-by-value",
                              "Size By Value",
                              "Whether marker size is determined by W value",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:min-marker-size:
     *
     * Minimum marker size in bubble mode.
     *
     * Since: 1.0
     */
    properties[PROP_MIN_MARKER_SIZE] =
        g_param_spec_float ("min-marker-size",
                            "Min Marker Size",
                            "Minimum marker size in bubble mode",
                            1.0f, 100.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:max-marker-size:
     *
     * Maximum marker size in bubble mode.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_MARKER_SIZE] =
        g_param_spec_float ("max-marker-size",
                            "Max Marker Size",
                            "Maximum marker size in bubble mode",
                            1.0f, 200.0f, 30.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:show-drop-lines:
     *
     * Whether to show drop lines to the floor.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_DROP_LINES] =
        g_param_spec_boolean ("show-drop-lines",
                              "Show Drop Lines",
                              "Whether to show drop lines to floor",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:drop-line-color:
     *
     * The drop line color.
     *
     * Since: 1.0
     */
    properties[PROP_DROP_LINE_COLOR] =
        g_param_spec_boxed ("drop-line-color",
                            "Drop Line Color",
                            "The drop line color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:depth-fade:
     *
     * Whether distant points fade.
     *
     * Since: 1.0
     */
    properties[PROP_DEPTH_FADE] =
        g_param_spec_boolean ("depth-fade",
                              "Depth Fade",
                              "Whether distant points fade",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgScatterChart3D:depth-scale:
     *
     * Whether distant points are drawn smaller.
     *
     * Since: 1.0
     */
    properties[PROP_DEPTH_SCALE] =
        g_param_spec_boolean ("depth-scale",
                              "Depth Scale",
                              "Whether distant points are drawn smaller",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scatter_chart3d_init (LrgScatterChart3D *self)
{
    self->marker_style = LRG_CHART_MARKER_CIRCLE;
    self->marker_size = 8.0f;
    self->size_by_value = FALSE;
    self->min_marker_size = 4.0f;
    self->max_marker_size = 30.0f;
    self->show_drop_lines = FALSE;
    self->drop_line_color = grl_color_new (128, 128, 128, 128);
    self->depth_fade = FALSE;
    self->depth_scale = TRUE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

LrgScatterChart3D *
lrg_scatter_chart3d_new (void)
{
    return g_object_new (LRG_TYPE_SCATTER_CHART3D, NULL);
}

LrgScatterChart3D *
lrg_scatter_chart3d_new_with_size (gfloat width,
                                   gfloat height)
{
    return g_object_new (LRG_TYPE_SCATTER_CHART3D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Public API - Marker Properties
 * ========================================================================== */

LrgChartMarker
lrg_scatter_chart3d_get_marker_style (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), LRG_CHART_MARKER_CIRCLE);
    return self->marker_style;
}

void
lrg_scatter_chart3d_set_marker_style (LrgScatterChart3D *self,
                                      LrgChartMarker     style)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (self->marker_style != style)
    {
        self->marker_style = style;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_STYLE]);
    }
}

gfloat
lrg_scatter_chart3d_get_marker_size (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), 8.0f);
    return self->marker_size;
}

void
lrg_scatter_chart3d_set_marker_size (LrgScatterChart3D *self,
                                     gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    size = CLAMP (size, 1.0f, 100.0f);

    if (self->marker_size != size)
    {
        self->marker_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_SIZE]);
    }
}

gboolean
lrg_scatter_chart3d_get_size_by_value (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), FALSE);
    return self->size_by_value;
}

void
lrg_scatter_chart3d_set_size_by_value (LrgScatterChart3D *self,
                                       gboolean           enabled)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (self->size_by_value != enabled)
    {
        self->size_by_value = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIZE_BY_VALUE]);
    }
}

gfloat
lrg_scatter_chart3d_get_min_marker_size (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), 4.0f);
    return self->min_marker_size;
}

void
lrg_scatter_chart3d_set_min_marker_size (LrgScatterChart3D *self,
                                         gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    size = CLAMP (size, 1.0f, 100.0f);

    if (self->min_marker_size != size)
    {
        self->min_marker_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_MARKER_SIZE]);
    }
}

gfloat
lrg_scatter_chart3d_get_max_marker_size (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), 30.0f);
    return self->max_marker_size;
}

void
lrg_scatter_chart3d_set_max_marker_size (LrgScatterChart3D *self,
                                         gfloat             size)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    size = CLAMP (size, 1.0f, 200.0f);

    if (self->max_marker_size != size)
    {
        self->max_marker_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_MARKER_SIZE]);
    }
}

/* ==========================================================================
 * Public API - Display Options
 * ========================================================================== */

gboolean
lrg_scatter_chart3d_get_show_drop_lines (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), FALSE);
    return self->show_drop_lines;
}

void
lrg_scatter_chart3d_set_show_drop_lines (LrgScatterChart3D *self,
                                         gboolean           show)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (self->show_drop_lines != show)
    {
        self->show_drop_lines = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_DROP_LINES]);
    }
}

GrlColor *
lrg_scatter_chart3d_get_drop_line_color (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), NULL);
    return self->drop_line_color;
}

void
lrg_scatter_chart3d_set_drop_line_color (LrgScatterChart3D *self,
                                         GrlColor          *color)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (color == NULL)
        return;

    g_clear_pointer (&self->drop_line_color, grl_color_free);
    self->drop_line_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_LINE_COLOR]);
}

gboolean
lrg_scatter_chart3d_get_depth_fade (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), FALSE);
    return self->depth_fade;
}

void
lrg_scatter_chart3d_set_depth_fade (LrgScatterChart3D *self,
                                    gboolean           enabled)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (self->depth_fade != enabled)
    {
        self->depth_fade = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH_FADE]);
    }
}

gboolean
lrg_scatter_chart3d_get_depth_scale (LrgScatterChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SCATTER_CHART3D (self), TRUE);
    return self->depth_scale;
}

void
lrg_scatter_chart3d_set_depth_scale (LrgScatterChart3D *self,
                                     gboolean           enabled)
{
    g_return_if_fail (LRG_IS_SCATTER_CHART3D (self));

    if (self->depth_scale != enabled)
    {
        self->depth_scale = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH_SCALE]);
    }
}
