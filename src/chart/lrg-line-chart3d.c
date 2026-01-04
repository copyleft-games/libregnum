/* lrg-line-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLineChart3D - 3D Line Chart widget implementation.
 *
 * Renders data series as 3D lines with optional markers and fill.
 * Uses depth sorting to render lines from back to front.
 */

#include "lrg-line-chart3d.h"
#include "lrg-chart-data-series.h"
#include "../lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART

/**
 * LrgLineChart3D:
 *
 * A 3D line chart widget that renders data as lines in 3D space.
 * Each series is rendered as a separate line at different Z depths.
 */
struct _LrgLineChart3D
{
    LrgChart3D parent_instance;

    /* Line properties */
    gfloat line_width;

    /* Marker properties */
    gboolean show_markers;
    gfloat marker_size;

    /* Fill properties */
    gboolean fill_to_floor;
    gfloat fill_opacity;

    /* Display options */
    gboolean smooth;
    gboolean drop_lines;
};

enum
{
    PROP_0,
    PROP_LINE_WIDTH,
    PROP_SHOW_MARKERS,
    PROP_MARKER_SIZE,
    PROP_FILL_TO_FLOOR,
    PROP_FILL_OPACITY,
    PROP_SMOOTH,
    PROP_DROP_LINES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgLineChart3D, lrg_line_chart3d, LRG_TYPE_CHART3D)

/* ==========================================================================
 * Internal Types
 * ========================================================================== */

/*
 * LineSegment3D:
 *
 * Represents a line segment in 3D space for depth-sorted rendering.
 */
typedef struct
{
    gfloat x1, y1, z1;
    gfloat x2, y2, z2;
    gfloat screen_x1, screen_y1;
    gfloat screen_x2, screen_y2;
    gfloat sort_depth;
    GrlColor *color;
    gfloat width;
} LineSegment3D;

/*
 * Point3D:
 *
 * Represents a point in 3D space for marker rendering.
 */
typedef struct
{
    gfloat x, y, z;
    gfloat screen_x, screen_y;
    gfloat sort_depth;
    GrlColor *color;
    gfloat size;
} Point3D;

static gint
compare_segments_by_depth (gconstpointer a,
                           gconstpointer b)
{
    const LineSegment3D *seg_a = a;
    const LineSegment3D *seg_b = b;

    /* Sort back to front (higher depth = further away = draw first) */
    if (seg_a->sort_depth > seg_b->sort_depth)
        return -1;
    if (seg_a->sort_depth < seg_b->sort_depth)
        return 1;
    return 0;
}

static gint
compare_points_by_depth (gconstpointer a,
                         gconstpointer b)
{
    const Point3D *pt_a = a;
    const Point3D *pt_b = b;

    /* Sort back to front */
    if (pt_a->sort_depth > pt_b->sort_depth)
        return -1;
    if (pt_a->sort_depth < pt_b->sort_depth)
        return 1;
    return 0;
}

/* ==========================================================================
 * Drawing Implementation
 * ========================================================================== */

static void
lrg_line_chart3d_draw_data_3d (LrgChart3D *chart3d)
{
    LrgLineChart3D *self = LRG_LINE_CHART3D (chart3d);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *all_series;
    guint series_count;
    gdouble x_min, x_max, y_min, y_max;
    GArray *segments;
    GArray *markers;
    guint i, j;

    all_series = lrg_chart_get_all_series (chart);
    if (all_series == NULL || all_series->len == 0)
        return;

    series_count = all_series->len;
    lrg_chart_get_x_range (chart, &x_min, &x_max);
    lrg_chart_get_y_range (chart, &y_min, &y_max);

    if (x_max <= x_min)
        x_max = x_min + 1.0;
    if (y_max <= y_min)
        y_max = y_min + 1.0;

    /* Collect line segments and markers for depth sorting */
    segments = g_array_new (FALSE, FALSE, sizeof (LineSegment3D));
    markers = g_array_new (FALSE, FALSE, sizeof (Point3D));

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series;
        GPtrArray *points;
        GrlColor *series_color;
        guint point_count;
        gfloat series_z;

        series = g_ptr_array_index (all_series, i);
        if (!lrg_chart_data_series_get_visible (series))
            continue;

        points = lrg_chart_data_series_get_points (series);
        if (points == NULL || points->len < 1)
            continue;

        point_count = points->len;
        series_color = lrg_chart_data_series_get_color (series);

        /* Each series gets its own Z position (depth) */
        series_z = (gfloat)i / (gfloat)MAX (1, series_count - 1);

        /* Create line segments between consecutive points */
        for (j = 0; j < point_count - 1; j++)
        {
            LrgChartDataPoint *p1 = g_ptr_array_index (points, j);
            LrgChartDataPoint *p2 = g_ptr_array_index (points, j + 1);
            LineSegment3D seg;
            gfloat depth1, depth2;
            gdouble nx1, ny1, nx2, ny2;

            /* Normalize coordinates to 0-1 range */
            nx1 = (lrg_chart_data_point_get_x (p1) - x_min) / (x_max - x_min);
            ny1 = (lrg_chart_data_point_get_y (p1) - y_min) / (y_max - y_min);
            nx2 = (lrg_chart_data_point_get_x (p2) - x_min) / (x_max - x_min);
            ny2 = (lrg_chart_data_point_get_y (p2) - y_min) / (y_max - y_min);

            /* Project to screen */
            lrg_chart3d_project_point (chart3d, nx1, ny1, (gdouble)series_z,
                                       &seg.screen_x1, &seg.screen_y1, &depth1);
            lrg_chart3d_project_point (chart3d, nx2, ny2, (gdouble)series_z,
                                       &seg.screen_x2, &seg.screen_y2, &depth2);

            seg.x1 = (gfloat)nx1;
            seg.y1 = (gfloat)ny1;
            seg.z1 = series_z;
            seg.x2 = (gfloat)nx2;
            seg.y2 = (gfloat)ny2;
            seg.z2 = series_z;
            seg.sort_depth = (depth1 + depth2) / 2.0f;
            seg.color = grl_color_copy (series_color);
            seg.width = self->line_width;

            g_array_append_val (segments, seg);
        }

        /* Collect markers if enabled */
        if (self->show_markers)
        {
            for (j = 0; j < point_count; j++)
            {
                LrgChartDataPoint *pt = g_ptr_array_index (points, j);
                Point3D marker;
                gdouble nx, ny;

                nx = (lrg_chart_data_point_get_x (pt) - x_min) / (x_max - x_min);
                ny = (lrg_chart_data_point_get_y (pt) - y_min) / (y_max - y_min);

                lrg_chart3d_project_point (chart3d, nx, ny, (gdouble)series_z,
                                           &marker.screen_x, &marker.screen_y,
                                           &marker.sort_depth);

                marker.x = (gfloat)nx;
                marker.y = (gfloat)ny;
                marker.z = series_z;
                marker.color = grl_color_copy (series_color);
                marker.size = self->marker_size;

                g_array_append_val (markers, marker);
            }
        }

        /* Draw drop lines if enabled */
        if (self->drop_lines)
        {
            for (j = 0; j < point_count; j++)
            {
                LrgChartDataPoint *pt = g_ptr_array_index (points, j);
                LineSegment3D drop;
                gfloat depth_top, depth_bottom;
                gdouble nx, ny;
                g_autoptr(GrlColor) drop_color = NULL;

                nx = (lrg_chart_data_point_get_x (pt) - x_min) / (x_max - x_min);
                ny = (lrg_chart_data_point_get_y (pt) - y_min) / (y_max - y_min);

                lrg_chart3d_project_point (chart3d, nx, ny, (gdouble)series_z,
                                           &drop.screen_x1, &drop.screen_y1, &depth_top);
                lrg_chart3d_project_point (chart3d, nx, 0.0, (gdouble)series_z,
                                           &drop.screen_x2, &drop.screen_y2, &depth_bottom);

                drop.x1 = (gfloat)nx;
                drop.y1 = (gfloat)ny;
                drop.z1 = series_z;
                drop.x2 = (gfloat)nx;
                drop.y2 = 0.0f;
                drop.z2 = series_z;
                drop.sort_depth = (depth_top + depth_bottom) / 2.0f;

                /* Make drop lines semi-transparent */
                drop_color = grl_color_new (
                    grl_color_get_r (series_color),
                    grl_color_get_g (series_color),
                    grl_color_get_b (series_color),
                    128
                );
                drop.color = grl_color_copy (drop_color);
                drop.width = 1.0f;

                g_array_append_val (segments, drop);
            }
        }

        /* Draw fill to floor if enabled */
        if (self->fill_to_floor && point_count >= 2)
        {
            guint8 fill_alpha;

            fill_alpha = (guint8)(self->fill_opacity * 255.0f);

            for (j = 0; j < point_count - 1; j++)
            {
                LrgChartDataPoint *p1 = g_ptr_array_index (points, j);
                LrgChartDataPoint *p2 = g_ptr_array_index (points, j + 1);
                gfloat sx1, sy1, sx2, sy2, sx3, sy3, sx4, sy4;
                gfloat d1, d2, d3, d4, avg_depth;
                gdouble nx1, ny1, nx2, ny2;
                g_autoptr(GrlColor) fill_color = NULL;

                nx1 = (lrg_chart_data_point_get_x (p1) - x_min) / (x_max - x_min);
                ny1 = (lrg_chart_data_point_get_y (p1) - y_min) / (y_max - y_min);
                nx2 = (lrg_chart_data_point_get_x (p2) - x_min) / (x_max - x_min);
                ny2 = (lrg_chart_data_point_get_y (p2) - y_min) / (y_max - y_min);

                /* Get four corners of the fill quad */
                lrg_chart3d_project_point (chart3d, nx1, ny1, (gdouble)series_z,
                                           &sx1, &sy1, &d1);
                lrg_chart3d_project_point (chart3d, nx2, ny2, (gdouble)series_z,
                                           &sx2, &sy2, &d2);
                lrg_chart3d_project_point (chart3d, nx2, 0.0, (gdouble)series_z,
                                           &sx3, &sy3, &d3);
                lrg_chart3d_project_point (chart3d, nx1, 0.0, (gdouble)series_z,
                                           &sx4, &sy4, &d4);

                avg_depth = (d1 + d2 + d3 + d4) / 4.0f;

                fill_color = grl_color_new (
                    grl_color_get_r (series_color),
                    grl_color_get_g (series_color),
                    grl_color_get_b (series_color),
                    fill_alpha
                );

                /* Draw as two triangles for the quad (immediate draw for fill) */
                /* Note: This draws immediately, which may have depth issues.
                 * For proper depth sorting, we'd need to collect quads too. */
                grl_draw_triangle ((gint)sx1, (gint)sy1,
                                   (gint)sx2, (gint)sy2,
                                   (gint)sx3, (gint)sy3, fill_color);
                grl_draw_triangle ((gint)sx1, (gint)sy1,
                                   (gint)sx3, (gint)sy3,
                                   (gint)sx4, (gint)sy4, fill_color);

                (void)avg_depth; /* Suppress unused warning */
            }
        }
    }

    /* Sort segments by depth (back to front) */
    g_array_sort (segments, compare_segments_by_depth);

    /* Draw all line segments */
    for (i = 0; i < segments->len; i++)
    {
        LineSegment3D *seg = &g_array_index (segments, LineSegment3D, i);
        g_autoptr(GrlVector2) start = NULL;
        g_autoptr(GrlVector2) end = NULL;

        start = grl_vector2_new (seg->screen_x1, seg->screen_y1);
        end = grl_vector2_new (seg->screen_x2, seg->screen_y2);

        grl_draw_line_ex (start, end, seg->width, seg->color);

        grl_color_free (seg->color);
    }

    /* Sort markers by depth and draw */
    if (markers->len > 0)
    {
        g_array_sort (markers, compare_points_by_depth);

        for (i = 0; i < markers->len; i++)
        {
            Point3D *pt = &g_array_index (markers, Point3D, i);

            grl_draw_circle ((gint)pt->screen_x, (gint)pt->screen_y,
                             pt->size, pt->color);

            grl_color_free (pt->color);
        }
    }

    g_array_free (segments, TRUE);
    g_array_free (markers, TRUE);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_line_chart3d_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgLineChart3D *self = LRG_LINE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_LINE_WIDTH:
        g_value_set_float (value, self->line_width);
        break;
    case PROP_SHOW_MARKERS:
        g_value_set_boolean (value, self->show_markers);
        break;
    case PROP_MARKER_SIZE:
        g_value_set_float (value, self->marker_size);
        break;
    case PROP_FILL_TO_FLOOR:
        g_value_set_boolean (value, self->fill_to_floor);
        break;
    case PROP_FILL_OPACITY:
        g_value_set_float (value, self->fill_opacity);
        break;
    case PROP_SMOOTH:
        g_value_set_boolean (value, self->smooth);
        break;
    case PROP_DROP_LINES:
        g_value_set_boolean (value, self->drop_lines);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_line_chart3d_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgLineChart3D *self = LRG_LINE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_LINE_WIDTH:
        lrg_line_chart3d_set_line_width (self, g_value_get_float (value));
        break;
    case PROP_SHOW_MARKERS:
        lrg_line_chart3d_set_show_markers (self, g_value_get_boolean (value));
        break;
    case PROP_MARKER_SIZE:
        lrg_line_chart3d_set_marker_size (self, g_value_get_float (value));
        break;
    case PROP_FILL_TO_FLOOR:
        lrg_line_chart3d_set_fill_to_floor (self, g_value_get_boolean (value));
        break;
    case PROP_FILL_OPACITY:
        lrg_line_chart3d_set_fill_opacity (self, g_value_get_float (value));
        break;
    case PROP_SMOOTH:
        lrg_line_chart3d_set_smooth (self, g_value_get_boolean (value));
        break;
    case PROP_DROP_LINES:
        lrg_line_chart3d_set_drop_lines (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_line_chart3d_class_init (LrgLineChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChart3DClass *chart3d_class = LRG_CHART3D_CLASS (klass);

    object_class->get_property = lrg_line_chart3d_get_property;
    object_class->set_property = lrg_line_chart3d_set_property;

    /* Override draw_data_3d virtual method */
    chart3d_class->draw_data_3d = lrg_line_chart3d_draw_data_3d;

    /**
     * LrgLineChart3D:line-width:
     *
     * The line width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_LINE_WIDTH] =
        g_param_spec_float ("line-width",
                            "Line Width",
                            "Line width in pixels",
                            0.5f, 20.0f, 2.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:show-markers:
     *
     * Whether to show markers at data points.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_MARKERS] =
        g_param_spec_boolean ("show-markers",
                              "Show Markers",
                              "Whether to show markers at data points",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:marker-size:
     *
     * The marker size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_MARKER_SIZE] =
        g_param_spec_float ("marker-size",
                            "Marker Size",
                            "Marker size in pixels",
                            1.0f, 30.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:fill-to-floor:
     *
     * Whether to fill lines down to the floor (ribbon style).
     *
     * Since: 1.0
     */
    properties[PROP_FILL_TO_FLOOR] =
        g_param_spec_boolean ("fill-to-floor",
                              "Fill to Floor",
                              "Whether to fill lines down to the floor",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:fill-opacity:
     *
     * The fill opacity (0.0 to 1.0).
     *
     * Since: 1.0
     */
    properties[PROP_FILL_OPACITY] =
        g_param_spec_float ("fill-opacity",
                            "Fill Opacity",
                            "Fill opacity",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:smooth:
     *
     * Whether to smooth lines with bezier curves.
     *
     * Since: 1.0
     */
    properties[PROP_SMOOTH] =
        g_param_spec_boolean ("smooth",
                              "Smooth",
                              "Whether to smooth lines with bezier curves",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgLineChart3D:drop-lines:
     *
     * Whether to draw drop lines from points to the floor.
     *
     * Since: 1.0
     */
    properties[PROP_DROP_LINES] =
        g_param_spec_boolean ("drop-lines",
                              "Drop Lines",
                              "Whether to draw drop lines from points to floor",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_line_chart3d_init (LrgLineChart3D *self)
{
    self->line_width = 2.0f;
    self->show_markers = TRUE;
    self->marker_size = 4.0f;
    self->fill_to_floor = FALSE;
    self->fill_opacity = 0.3f;
    self->smooth = FALSE;
    self->drop_lines = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_line_chart3d_new:
 *
 * Creates a new 3D line chart with default settings.
 *
 * Returns: (transfer full): a new #LrgLineChart3D
 *
 * Since: 1.0
 */
LrgLineChart3D *
lrg_line_chart3d_new (void)
{
    return g_object_new (LRG_TYPE_LINE_CHART3D, NULL);
}

/**
 * lrg_line_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D line chart with specified size.
 *
 * Returns: (transfer full): a new #LrgLineChart3D
 *
 * Since: 1.0
 */
LrgLineChart3D *
lrg_line_chart3d_new_with_size (gfloat width,
                                gfloat height)
{
    return g_object_new (LRG_TYPE_LINE_CHART3D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Public API - Line Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_line_width:
 * @self: an #LrgLineChart3D
 *
 * Gets the line width in pixels.
 *
 * Returns: line width
 *
 * Since: 1.0
 */
gfloat
lrg_line_chart3d_get_line_width (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), 2.0f);
    return self->line_width;
}

/**
 * lrg_line_chart3d_set_line_width:
 * @self: an #LrgLineChart3D
 * @width: line width in pixels
 *
 * Sets the line width in pixels.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_line_width (LrgLineChart3D *self,
                                 gfloat          width)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    width = CLAMP (width, 0.5f, 20.0f);

    if (self->line_width != width)
    {
        self->line_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_WIDTH]);
    }
}

/* ==========================================================================
 * Public API - Marker Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_show_markers:
 * @self: an #LrgLineChart3D
 *
 * Gets whether markers are shown at data points.
 *
 * Returns: %TRUE if markers are shown
 *
 * Since: 1.0
 */
gboolean
lrg_line_chart3d_get_show_markers (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), TRUE);
    return self->show_markers;
}

/**
 * lrg_line_chart3d_set_show_markers:
 * @self: an #LrgLineChart3D
 * @show: whether to show markers
 *
 * Sets whether to show markers at data points.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_show_markers (LrgLineChart3D *self,
                                   gboolean        show)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    if (self->show_markers != show)
    {
        self->show_markers = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MARKERS]);
    }
}

/**
 * lrg_line_chart3d_get_marker_size:
 * @self: an #LrgLineChart3D
 *
 * Gets the marker size in pixels.
 *
 * Returns: marker size
 *
 * Since: 1.0
 */
gfloat
lrg_line_chart3d_get_marker_size (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), 4.0f);
    return self->marker_size;
}

/**
 * lrg_line_chart3d_set_marker_size:
 * @self: an #LrgLineChart3D
 * @size: marker size in pixels
 *
 * Sets the marker size in pixels.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_marker_size (LrgLineChart3D *self,
                                  gfloat          size)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    size = CLAMP (size, 1.0f, 30.0f);

    if (self->marker_size != size)
    {
        self->marker_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKER_SIZE]);
    }
}

/* ==========================================================================
 * Public API - Fill Properties
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_fill_to_floor:
 * @self: an #LrgLineChart3D
 *
 * Gets whether lines are filled down to the floor (ribbon style).
 *
 * Returns: %TRUE if fill is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_line_chart3d_get_fill_to_floor (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), FALSE);
    return self->fill_to_floor;
}

/**
 * lrg_line_chart3d_set_fill_to_floor:
 * @self: an #LrgLineChart3D
 * @fill: whether to fill to floor
 *
 * Sets whether to fill lines down to the floor (ribbon style).
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_fill_to_floor (LrgLineChart3D *self,
                                    gboolean        fill)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    if (self->fill_to_floor != fill)
    {
        self->fill_to_floor = fill;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_TO_FLOOR]);
    }
}

/**
 * lrg_line_chart3d_get_fill_opacity:
 * @self: an #LrgLineChart3D
 *
 * Gets the fill opacity (0.0 to 1.0).
 *
 * Returns: fill opacity
 *
 * Since: 1.0
 */
gfloat
lrg_line_chart3d_get_fill_opacity (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), 0.3f);
    return self->fill_opacity;
}

/**
 * lrg_line_chart3d_set_fill_opacity:
 * @self: an #LrgLineChart3D
 * @opacity: fill opacity (0.0 to 1.0)
 *
 * Sets the fill opacity.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_fill_opacity (LrgLineChart3D *self,
                                   gfloat          opacity)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->fill_opacity != opacity)
    {
        self->fill_opacity = opacity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_OPACITY]);
    }
}

/* ==========================================================================
 * Public API - Display Options
 * ========================================================================== */

/**
 * lrg_line_chart3d_get_smooth:
 * @self: an #LrgLineChart3D
 *
 * Gets whether lines are smoothed (bezier curves).
 *
 * Returns: %TRUE if smoothing is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_line_chart3d_get_smooth (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), FALSE);
    return self->smooth;
}

/**
 * lrg_line_chart3d_set_smooth:
 * @self: an #LrgLineChart3D
 * @smooth: whether to smooth lines
 *
 * Sets whether to use bezier smoothing for lines.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_smooth (LrgLineChart3D *self,
                             gboolean        smooth)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    if (self->smooth != smooth)
    {
        self->smooth = smooth;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTH]);
    }
}

/**
 * lrg_line_chart3d_get_drop_lines:
 * @self: an #LrgLineChart3D
 *
 * Gets whether drop lines are drawn from points to the floor.
 *
 * Returns: %TRUE if drop lines are shown
 *
 * Since: 1.0
 */
gboolean
lrg_line_chart3d_get_drop_lines (LrgLineChart3D *self)
{
    g_return_val_if_fail (LRG_IS_LINE_CHART3D (self), FALSE);
    return self->drop_lines;
}

/**
 * lrg_line_chart3d_set_drop_lines:
 * @self: an #LrgLineChart3D
 * @show: whether to show drop lines
 *
 * Sets whether to draw drop lines from points to the floor.
 *
 * Since: 1.0
 */
void
lrg_line_chart3d_set_drop_lines (LrgLineChart3D *self,
                                 gboolean        show)
{
    g_return_if_fail (LRG_IS_LINE_CHART3D (self));

    if (self->drop_lines != show)
    {
        self->drop_lines = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_LINES]);
    }
}
