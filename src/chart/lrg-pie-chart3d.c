/* lrg-pie-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPieChart3D - 3D Pie Chart widget implementation.
 *
 * Renders data as an extruded 3D pie chart. The pie is rendered
 * with proper depth sorting so slices appear correctly overlapped.
 */

#include "lrg-pie-chart3d.h"
#include "lrg-chart-data-series.h"
#include "../lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * LrgPieChart3D:
 *
 * A 3D pie chart widget that renders data as extruded pie slices.
 */
struct _LrgPieChart3D
{
    LrgChart3D parent_instance;

    /* Pie dimensions */
    gfloat radius;
    gfloat depth;
    gfloat inner_radius;

    /* Display options */
    gfloat start_angle;
    gfloat explode_distance;
    gboolean show_edges;
    GrlColor *edge_color;

    /* Exploded slices tracking (hash table: "series:point" -> TRUE) */
    GHashTable *exploded_slices;
};

enum
{
    PROP_0,
    PROP_RADIUS,
    PROP_DEPTH,
    PROP_INNER_RADIUS,
    PROP_START_ANGLE,
    PROP_EXPLODE_DISTANCE,
    PROP_SHOW_EDGES,
    PROP_EDGE_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgPieChart3D, lrg_pie_chart3d, LRG_TYPE_CHART3D)

/* ==========================================================================
 * Internal Types
 * ========================================================================== */

/*
 * SliceInfo:
 *
 * Information about a single pie slice for rendering.
 */
typedef struct
{
    gfloat start_angle;
    gfloat end_angle;
    gfloat center_angle;
    GrlColor *color;
    GrlColor *side_color;
    gboolean exploded;
    gfloat explode_x;
    gfloat explode_z;
    gfloat sort_depth;
    gint series_index;
    gint point_index;
} SliceInfo;

static gchar *
make_slice_key (gint series_index,
                gint point_index)
{
    return g_strdup_printf ("%d:%d", series_index, point_index);
}

static gint
compare_slices_by_depth (gconstpointer a,
                         gconstpointer b)
{
    const SliceInfo *slice_a = a;
    const SliceInfo *slice_b = b;

    /* Sort back to front */
    if (slice_a->sort_depth > slice_b->sort_depth)
        return -1;
    if (slice_a->sort_depth < slice_b->sort_depth)
        return 1;
    return 0;
}

/* ==========================================================================
 * Drawing Helpers
 * ========================================================================== */

static GrlColor *
darken_color (GrlColor *color,
              gfloat    factor)
{
    guint8 r, g, b, a;

    r = grl_color_get_r (color);
    g = grl_color_get_g (color);
    b = grl_color_get_b (color);
    a = grl_color_get_a (color);

    return grl_color_new (
        (guint8)(r * factor),
        (guint8)(g * factor),
        (guint8)(b * factor),
        a
    );
}

static void
draw_slice_top (LrgPieChart3D *self,
                gfloat         cx,
                gfloat         cy,
                gfloat         outer_r,
                gfloat         inner_r,
                gfloat         start_angle,
                gfloat         end_angle,
                gfloat         offset_x,
                gfloat         offset_z,
                GrlColor      *color)
{
    LrgChart3D *chart3d = LRG_CHART3D (self);
    gint segments;
    gint i;
    gfloat angle_step;
    gfloat prev_x1, prev_y1, prev_x2, prev_y2;
    gfloat depth;

    /* Draw the top face of the slice as triangles */
    segments = MAX (3, (gint)((end_angle - start_angle) / 5.0f));
    angle_step = (end_angle - start_angle) / (gfloat)segments;

    /* Get first outer point */
    {
        gfloat angle = start_angle * (gfloat)M_PI / 180.0f;
        gdouble nx = 0.5 + offset_x + cosf (angle) * outer_r;
        gdouble nz = 0.5 + offset_z + sinf (angle) * outer_r;
        lrg_chart3d_project_point (chart3d, nx, 1.0, nz, &prev_x1, &prev_y1, &depth);
    }

    /* Get first inner point (or center if not donut) */
    if (inner_r > 0.001f)
    {
        gfloat angle = start_angle * (gfloat)M_PI / 180.0f;
        gdouble nx = 0.5 + offset_x + cosf (angle) * inner_r;
        gdouble nz = 0.5 + offset_z + sinf (angle) * inner_r;
        lrg_chart3d_project_point (chart3d, nx, 1.0, nz, &prev_x2, &prev_y2, &depth);
    }
    else
    {
        lrg_chart3d_project_point (chart3d, 0.5 + offset_x, 1.0, 0.5 + offset_z,
                                   &prev_x2, &prev_y2, &depth);
    }

    for (i = 1; i <= segments; i++)
    {
        gfloat angle = (start_angle + i * angle_step) * (gfloat)M_PI / 180.0f;
        gfloat curr_x1, curr_y1, curr_x2, curr_y2;
        gdouble nx1, nz1, nx2, nz2;

        /* Outer point */
        nx1 = 0.5 + offset_x + cosf (angle) * outer_r;
        nz1 = 0.5 + offset_z + sinf (angle) * outer_r;
        lrg_chart3d_project_point (chart3d, nx1, 1.0, nz1, &curr_x1, &curr_y1, &depth);

        /* Inner point */
        if (inner_r > 0.001f)
        {
            nx2 = 0.5 + offset_x + cosf (angle) * inner_r;
            nz2 = 0.5 + offset_z + sinf (angle) * inner_r;
            lrg_chart3d_project_point (chart3d, nx2, 1.0, nz2, &curr_x2, &curr_y2, &depth);
        }
        else
        {
            curr_x2 = prev_x2;
            curr_y2 = prev_y2;
        }

        /* Draw two triangles for the quad (or one triangle for pie wedge) */
        if (inner_r > 0.001f)
        {
            grl_draw_triangle ((gint)prev_x1, (gint)prev_y1,
                               (gint)curr_x1, (gint)curr_y1,
                               (gint)curr_x2, (gint)curr_y2, color);
            grl_draw_triangle ((gint)prev_x1, (gint)prev_y1,
                               (gint)curr_x2, (gint)curr_y2,
                               (gint)prev_x2, (gint)prev_y2, color);
        }
        else
        {
            /* Pie wedge: triangle from center to arc */
            grl_draw_triangle ((gint)prev_x2, (gint)prev_y2,
                               (gint)prev_x1, (gint)prev_y1,
                               (gint)curr_x1, (gint)curr_y1, color);
        }

        prev_x1 = curr_x1;
        prev_y1 = curr_y1;
        if (inner_r > 0.001f)
        {
            prev_x2 = curr_x2;
            prev_y2 = curr_y2;
        }
    }
}

static void
draw_slice_side (LrgPieChart3D *self,
                 gfloat         outer_r,
                 gfloat         start_angle,
                 gfloat         end_angle,
                 gfloat         depth_val,
                 gfloat         offset_x,
                 gfloat         offset_z,
                 GrlColor      *color)
{
    LrgChart3D *chart3d = LRG_CHART3D (self);
    gint segments;
    gint i;
    gfloat angle_step;
    gfloat prev_tx, prev_ty, prev_bx, prev_by;
    gfloat depth;

    /* Draw the curved outer side of the slice */
    segments = MAX (3, (gint)((end_angle - start_angle) / 5.0f));
    angle_step = (end_angle - start_angle) / (gfloat)segments;

    /* Get first point at top and bottom */
    {
        gfloat angle = start_angle * (gfloat)M_PI / 180.0f;
        gdouble nx = 0.5 + offset_x + cosf (angle) * outer_r;
        gdouble nz = 0.5 + offset_z + sinf (angle) * outer_r;
        lrg_chart3d_project_point (chart3d, nx, 1.0, nz, &prev_tx, &prev_ty, &depth);
        lrg_chart3d_project_point (chart3d, nx, 1.0 - depth_val, nz,
                                   &prev_bx, &prev_by, &depth);
    }

    for (i = 1; i <= segments; i++)
    {
        gfloat angle = (start_angle + i * angle_step) * (gfloat)M_PI / 180.0f;
        gfloat curr_tx, curr_ty, curr_bx, curr_by;
        gdouble nx, nz;

        nx = 0.5 + offset_x + cosf (angle) * outer_r;
        nz = 0.5 + offset_z + sinf (angle) * outer_r;

        lrg_chart3d_project_point (chart3d, nx, 1.0, nz, &curr_tx, &curr_ty, &depth);
        lrg_chart3d_project_point (chart3d, nx, 1.0 - depth_val, nz,
                                   &curr_bx, &curr_by, &depth);

        /* Draw quad as two triangles */
        grl_draw_triangle ((gint)prev_tx, (gint)prev_ty,
                           (gint)curr_tx, (gint)curr_ty,
                           (gint)curr_bx, (gint)curr_by, color);
        grl_draw_triangle ((gint)prev_tx, (gint)prev_ty,
                           (gint)curr_bx, (gint)curr_by,
                           (gint)prev_bx, (gint)prev_by, color);

        prev_tx = curr_tx;
        prev_ty = curr_ty;
        prev_bx = curr_bx;
        prev_by = curr_by;
    }
}

static void
draw_slice_flat_side (LrgPieChart3D *self,
                      gfloat         outer_r,
                      gfloat         inner_r,
                      gfloat         angle_deg,
                      gfloat         depth_val,
                      gfloat         offset_x,
                      gfloat         offset_z,
                      GrlColor      *color)
{
    LrgChart3D *chart3d = LRG_CHART3D (self);
    gfloat angle = angle_deg * (gfloat)M_PI / 180.0f;
    gfloat cos_a = cosf (angle);
    gfloat sin_a = sinf (angle);
    gfloat outer_top_x, outer_top_y, outer_bottom_x, outer_bottom_y;
    gfloat inner_top_x, inner_top_y, inner_bottom_x, inner_bottom_y;
    gfloat depth;
    gdouble nx_outer, nz_outer, nx_inner, nz_inner;

    nx_outer = 0.5 + offset_x + cos_a * outer_r;
    nz_outer = 0.5 + offset_z + sin_a * outer_r;

    if (inner_r > 0.001f)
    {
        nx_inner = 0.5 + offset_x + cos_a * inner_r;
        nz_inner = 0.5 + offset_z + sin_a * inner_r;
    }
    else
    {
        nx_inner = 0.5 + offset_x;
        nz_inner = 0.5 + offset_z;
    }

    /* Project all four corners */
    lrg_chart3d_project_point (chart3d, nx_outer, 1.0, nz_outer,
                               &outer_top_x, &outer_top_y, &depth);
    lrg_chart3d_project_point (chart3d, nx_outer, 1.0 - depth_val, nz_outer,
                               &outer_bottom_x, &outer_bottom_y, &depth);
    lrg_chart3d_project_point (chart3d, nx_inner, 1.0, nz_inner,
                               &inner_top_x, &inner_top_y, &depth);
    lrg_chart3d_project_point (chart3d, nx_inner, 1.0 - depth_val, nz_inner,
                               &inner_bottom_x, &inner_bottom_y, &depth);

    /* Draw quad as two triangles */
    grl_draw_triangle ((gint)outer_top_x, (gint)outer_top_y,
                       (gint)outer_bottom_x, (gint)outer_bottom_y,
                       (gint)inner_bottom_x, (gint)inner_bottom_y, color);
    grl_draw_triangle ((gint)outer_top_x, (gint)outer_top_y,
                       (gint)inner_bottom_x, (gint)inner_bottom_y,
                       (gint)inner_top_x, (gint)inner_top_y, color);
}

/* ==========================================================================
 * Drawing Implementation
 * ========================================================================== */

static void
lrg_pie_chart3d_draw_data_3d (LrgChart3D *chart3d)
{
    LrgPieChart3D *self = LRG_PIE_CHART3D (chart3d);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *all_series;
    guint series_count;
    gdouble total;
    GArray *slices;
    guint i, j;
    gfloat current_angle;
    gfloat camera_yaw;

    all_series = lrg_chart_get_all_series (chart);
    if (all_series == NULL || all_series->len == 0)
        return;

    series_count = all_series->len;

    /* Calculate total value across all series */
    total = 0.0;
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = g_ptr_array_index (all_series, i);
        GPtrArray *points;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        points = lrg_chart_data_series_get_points (series);
        if (points == NULL)
            continue;

        for (j = 0; j < points->len; j++)
        {
            LrgChartDataPoint *pt = g_ptr_array_index (points, j);
            if (lrg_chart_data_point_get_y (pt) > 0.0)
                total += lrg_chart_data_point_get_y (pt);
        }
    }

    if (total <= 0.0)
        return;

    /* Collect slice information */
    slices = g_array_new (FALSE, FALSE, sizeof (SliceInfo));
    current_angle = self->start_angle;
    camera_yaw = lrg_chart3d_get_camera_yaw (chart3d);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = g_ptr_array_index (all_series, i);
        GPtrArray *points;
        GrlColor *series_color;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        points = lrg_chart_data_series_get_points (series);
        if (points == NULL)
            continue;

        series_color = lrg_chart_data_series_get_color (series);

        for (j = 0; j < points->len; j++)
        {
            LrgChartDataPoint *pt = g_ptr_array_index (points, j);
            SliceInfo slice;
            gfloat slice_angle;
            g_autofree gchar *key = NULL;
            gfloat center_rad;

            if (lrg_chart_data_point_get_y (pt) <= 0.0)
                continue;

            slice_angle = (gfloat)(lrg_chart_data_point_get_y (pt) / total * 360.0);

            slice.start_angle = current_angle;
            slice.end_angle = current_angle + slice_angle;
            slice.center_angle = current_angle + slice_angle / 2.0f;

            /* Use point color if available, otherwise series color */
            if (pt->color != NULL)
            {
                slice.color = grl_color_copy (pt->color);
            }
            else
            {
                slice.color = grl_color_copy (series_color);
            }
            slice.side_color = darken_color (slice.color, 0.7f);

            /* Check if exploded */
            key = make_slice_key ((gint)i, (gint)j);
            slice.exploded = g_hash_table_contains (self->exploded_slices, key);

            if (slice.exploded)
            {
                center_rad = slice.center_angle * (gfloat)M_PI / 180.0f;
                slice.explode_x = cosf (center_rad) * self->explode_distance * self->radius;
                slice.explode_z = sinf (center_rad) * self->explode_distance * self->radius;
            }
            else
            {
                slice.explode_x = 0.0f;
                slice.explode_z = 0.0f;
            }

            /* Calculate sort depth based on center angle relative to camera */
            {
                gfloat rel_angle = slice.center_angle - camera_yaw;
                /* Slices facing away from camera should be drawn first */
                slice.sort_depth = cosf (rel_angle * (gfloat)M_PI / 180.0f);
            }

            slice.series_index = (gint)i;
            slice.point_index = (gint)j;

            g_array_append_val (slices, slice);

            current_angle = slice.end_angle;
        }
    }

    /* Sort slices by depth (back to front) */
    g_array_sort (slices, compare_slices_by_depth);

    /* Draw all slices */
    for (i = 0; i < slices->len; i++)
    {
        SliceInfo *slice = &g_array_index (slices, SliceInfo, i);
        gfloat outer_r = self->radius * 0.4f;
        gfloat inner_r = self->inner_radius * outer_r;
        gfloat depth_val = self->depth * 0.3f;

        /* Draw side faces first (they're behind the top) */

        /* Outer curved side */
        draw_slice_side (self, outer_r, slice->start_angle, slice->end_angle,
                         depth_val, slice->explode_x, slice->explode_z,
                         slice->side_color);

        /* Flat sides at start and end angles */
        draw_slice_flat_side (self, outer_r, inner_r, slice->start_angle,
                              depth_val, slice->explode_x, slice->explode_z,
                              slice->side_color);
        draw_slice_flat_side (self, outer_r, inner_r, slice->end_angle,
                              depth_val, slice->explode_x, slice->explode_z,
                              slice->side_color);

        /* Draw top face last (it's on top) */
        draw_slice_top (self, 0.5f, 0.5f, outer_r, inner_r,
                        slice->start_angle, slice->end_angle,
                        slice->explode_x, slice->explode_z,
                        slice->color);

        grl_color_free (slice->color);
        grl_color_free (slice->side_color);
    }

    /* Draw edges if enabled */
    if (self->show_edges)
    {
        for (i = 0; i < slices->len; i++)
        {
            SliceInfo *slice = &g_array_index (slices, SliceInfo, i);
            gfloat outer_r = self->radius * 0.4f;
            gfloat start_rad = slice->start_angle * (gfloat)M_PI / 180.0f;
            gfloat end_rad = slice->end_angle * (gfloat)M_PI / 180.0f;
            gfloat depth_val = self->depth * 0.3f;
            gfloat cx_x, cx_y, start_x, start_y, end_x, end_y;
            gfloat depth;
            gdouble cx_3d, cz_3d, start_3d_x, start_3d_z, end_3d_x, end_3d_z;

            cx_3d = 0.5 + slice->explode_x;
            cz_3d = 0.5 + slice->explode_z;
            start_3d_x = cx_3d + cosf (start_rad) * outer_r;
            start_3d_z = cz_3d + sinf (start_rad) * outer_r;
            end_3d_x = cx_3d + cosf (end_rad) * outer_r;
            end_3d_z = cz_3d + sinf (end_rad) * outer_r;

            /* Project points */
            lrg_chart3d_project_point (chart3d, cx_3d, 1.0, cz_3d,
                                       &cx_x, &cx_y, &depth);
            lrg_chart3d_project_point (chart3d, start_3d_x, 1.0, start_3d_z,
                                       &start_x, &start_y, &depth);
            lrg_chart3d_project_point (chart3d, end_3d_x, 1.0, end_3d_z,
                                       &end_x, &end_y, &depth);

            /* Draw edge lines from center to start and end of slice */
            {
                g_autoptr(GrlVector2) v1 = grl_vector2_new (cx_x, cx_y);
                g_autoptr(GrlVector2) v2 = grl_vector2_new (start_x, start_y);
                g_autoptr(GrlVector2) v3 = grl_vector2_new (end_x, end_y);
                grl_draw_line_ex (v1, v2, 1.0f, self->edge_color);
                grl_draw_line_ex (v1, v3, 1.0f, self->edge_color);
            }

            (void)depth_val; /* Suppress unused warning */
        }
    }

    g_array_free (slices, TRUE);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_pie_chart3d_dispose (GObject *object)
{
    LrgPieChart3D *self = LRG_PIE_CHART3D (object);

    g_clear_pointer (&self->edge_color, grl_color_free);
    g_clear_pointer (&self->exploded_slices, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_pie_chart3d_parent_class)->dispose (object);
}

static void
lrg_pie_chart3d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgPieChart3D *self = LRG_PIE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        g_value_set_float (value, self->radius);
        break;
    case PROP_DEPTH:
        g_value_set_float (value, self->depth);
        break;
    case PROP_INNER_RADIUS:
        g_value_set_float (value, self->inner_radius);
        break;
    case PROP_START_ANGLE:
        g_value_set_float (value, self->start_angle);
        break;
    case PROP_EXPLODE_DISTANCE:
        g_value_set_float (value, self->explode_distance);
        break;
    case PROP_SHOW_EDGES:
        g_value_set_boolean (value, self->show_edges);
        break;
    case PROP_EDGE_COLOR:
        g_value_set_boxed (value, self->edge_color);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_pie_chart3d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgPieChart3D *self = LRG_PIE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        lrg_pie_chart3d_set_radius (self, g_value_get_float (value));
        break;
    case PROP_DEPTH:
        lrg_pie_chart3d_set_depth (self, g_value_get_float (value));
        break;
    case PROP_INNER_RADIUS:
        lrg_pie_chart3d_set_inner_radius (self, g_value_get_float (value));
        break;
    case PROP_START_ANGLE:
        lrg_pie_chart3d_set_start_angle (self, g_value_get_float (value));
        break;
    case PROP_EXPLODE_DISTANCE:
        lrg_pie_chart3d_set_explode_distance (self, g_value_get_float (value));
        break;
    case PROP_SHOW_EDGES:
        lrg_pie_chart3d_set_show_edges (self, g_value_get_boolean (value));
        break;
    case PROP_EDGE_COLOR:
        lrg_pie_chart3d_set_edge_color (self, g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_pie_chart3d_class_init (LrgPieChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChart3DClass *chart3d_class = LRG_CHART3D_CLASS (klass);

    object_class->dispose = lrg_pie_chart3d_dispose;
    object_class->get_property = lrg_pie_chart3d_get_property;
    object_class->set_property = lrg_pie_chart3d_set_property;

    chart3d_class->draw_data_3d = lrg_pie_chart3d_draw_data_3d;

    /**
     * LrgPieChart3D:radius:
     *
     * The pie radius as fraction of available space.
     *
     * Since: 1.0
     */
    properties[PROP_RADIUS] =
        g_param_spec_float ("radius",
                            "Radius",
                            "Pie radius as fraction of available space",
                            0.1f, 1.0f, 0.8f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:depth:
     *
     * The extrusion depth as fraction of radius.
     *
     * Since: 1.0
     */
    properties[PROP_DEPTH] =
        g_param_spec_float ("depth",
                            "Depth",
                            "Extrusion depth as fraction of radius",
                            0.0f, 1.0f, 0.3f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:inner-radius:
     *
     * The inner radius for donut mode (0 = solid pie).
     *
     * Since: 1.0
     */
    properties[PROP_INNER_RADIUS] =
        g_param_spec_float ("inner-radius",
                            "Inner Radius",
                            "Inner radius for donut mode (0 = solid pie)",
                            0.0f, 0.9f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:start-angle:
     *
     * The starting angle in degrees.
     *
     * Since: 1.0
     */
    properties[PROP_START_ANGLE] =
        g_param_spec_float ("start-angle",
                            "Start Angle",
                            "Starting angle in degrees",
                            0.0f, 360.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:explode-distance:
     *
     * The explode distance as fraction of radius.
     *
     * Since: 1.0
     */
    properties[PROP_EXPLODE_DISTANCE] =
        g_param_spec_float ("explode-distance",
                            "Explode Distance",
                            "Explode distance as fraction of radius",
                            0.0f, 0.5f, 0.15f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:show-edges:
     *
     * Whether to show slice edges.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_EDGES] =
        g_param_spec_boolean ("show-edges",
                              "Show Edges",
                              "Whether to show slice edges",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgPieChart3D:edge-color:
     *
     * The edge color.
     *
     * Since: 1.0
     */
    properties[PROP_EDGE_COLOR] =
        g_param_spec_boxed ("edge-color",
                            "Edge Color",
                            "The edge color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_pie_chart3d_init (LrgPieChart3D *self)
{
    self->radius = 0.8f;
    self->depth = 0.3f;
    self->inner_radius = 0.0f;
    self->start_angle = 0.0f;
    self->explode_distance = 0.15f;
    self->show_edges = FALSE;
    self->edge_color = grl_color_new (0, 0, 0, 255);
    self->exploded_slices = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_pie_chart3d_new:
 *
 * Creates a new 3D pie chart with default settings.
 *
 * Returns: (transfer full): a new #LrgPieChart3D
 *
 * Since: 1.0
 */
LrgPieChart3D *
lrg_pie_chart3d_new (void)
{
    return g_object_new (LRG_TYPE_PIE_CHART3D, NULL);
}

/**
 * lrg_pie_chart3d_new_with_size:
 * @width: chart width
 * @height: chart height
 *
 * Creates a new 3D pie chart with specified size.
 *
 * Returns: (transfer full): a new #LrgPieChart3D
 *
 * Since: 1.0
 */
LrgPieChart3D *
lrg_pie_chart3d_new_with_size (gfloat width,
                               gfloat height)
{
    return g_object_new (LRG_TYPE_PIE_CHART3D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Public API - Pie Dimensions
 * ========================================================================== */

gfloat
lrg_pie_chart3d_get_radius (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), 0.8f);
    return self->radius;
}

void
lrg_pie_chart3d_set_radius (LrgPieChart3D *self,
                            gfloat         radius)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    radius = CLAMP (radius, 0.1f, 1.0f);

    if (self->radius != radius)
    {
        self->radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
    }
}

gfloat
lrg_pie_chart3d_get_depth (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), 0.3f);
    return self->depth;
}

void
lrg_pie_chart3d_set_depth (LrgPieChart3D *self,
                           gfloat         depth)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    depth = CLAMP (depth, 0.0f, 1.0f);

    if (self->depth != depth)
    {
        self->depth = depth;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH]);
    }
}

gfloat
lrg_pie_chart3d_get_inner_radius (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), 0.0f);
    return self->inner_radius;
}

void
lrg_pie_chart3d_set_inner_radius (LrgPieChart3D *self,
                                  gfloat         radius)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    radius = CLAMP (radius, 0.0f, 0.9f);

    if (self->inner_radius != radius)
    {
        self->inner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INNER_RADIUS]);
    }
}

/* ==========================================================================
 * Public API - Display Options
 * ========================================================================== */

gfloat
lrg_pie_chart3d_get_start_angle (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), 0.0f);
    return self->start_angle;
}

void
lrg_pie_chart3d_set_start_angle (LrgPieChart3D *self,
                                 gfloat         angle)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    /* Normalize to 0-360 */
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;

    if (self->start_angle != angle)
    {
        self->start_angle = angle;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_ANGLE]);
    }
}

gfloat
lrg_pie_chart3d_get_explode_distance (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), 0.15f);
    return self->explode_distance;
}

void
lrg_pie_chart3d_set_explode_distance (LrgPieChart3D *self,
                                      gfloat         distance)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    distance = CLAMP (distance, 0.0f, 0.5f);

    if (self->explode_distance != distance)
    {
        self->explode_distance = distance;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPLODE_DISTANCE]);
    }
}

gboolean
lrg_pie_chart3d_get_show_edges (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), FALSE);
    return self->show_edges;
}

void
lrg_pie_chart3d_set_show_edges (LrgPieChart3D *self,
                                gboolean       show)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    if (self->show_edges != show)
    {
        self->show_edges = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_EDGES]);
    }
}

GrlColor *
lrg_pie_chart3d_get_edge_color (LrgPieChart3D *self)
{
    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), NULL);
    return self->edge_color;
}

void
lrg_pie_chart3d_set_edge_color (LrgPieChart3D *self,
                                GrlColor      *color)
{
    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    if (color == NULL)
        return;

    g_clear_pointer (&self->edge_color, grl_color_free);
    self->edge_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_COLOR]);
}

/* ==========================================================================
 * Public API - Slice Operations
 * ========================================================================== */

/**
 * lrg_pie_chart3d_explode_slice:
 * @self: an #LrgPieChart3D
 * @series_index: which series (use 0 for single series)
 * @point_index: which slice to explode
 * @exploded: whether the slice is exploded
 *
 * Sets whether a specific slice is exploded.
 *
 * Since: 1.0
 */
void
lrg_pie_chart3d_explode_slice (LrgPieChart3D *self,
                               gint           series_index,
                               gint           point_index,
                               gboolean       exploded)
{
    gchar *key;

    g_return_if_fail (LRG_IS_PIE_CHART3D (self));
    g_return_if_fail (series_index >= 0);
    g_return_if_fail (point_index >= 0);

    key = make_slice_key (series_index, point_index);

    if (exploded)
    {
        g_hash_table_insert (self->exploded_slices, key, GINT_TO_POINTER (TRUE));
    }
    else
    {
        g_hash_table_remove (self->exploded_slices, key);
        g_free (key);
    }
}

/**
 * lrg_pie_chart3d_is_slice_exploded:
 * @self: an #LrgPieChart3D
 * @series_index: which series
 * @point_index: which slice
 *
 * Gets whether a specific slice is exploded.
 *
 * Returns: %TRUE if the slice is exploded
 *
 * Since: 1.0
 */
gboolean
lrg_pie_chart3d_is_slice_exploded (LrgPieChart3D *self,
                                   gint           series_index,
                                   gint           point_index)
{
    g_autofree gchar *key = NULL;

    g_return_val_if_fail (LRG_IS_PIE_CHART3D (self), FALSE);
    g_return_val_if_fail (series_index >= 0, FALSE);
    g_return_val_if_fail (point_index >= 0, FALSE);

    key = make_slice_key (series_index, point_index);
    return g_hash_table_contains (self->exploded_slices, key);
}

/**
 * lrg_pie_chart3d_explode_all:
 * @self: an #LrgPieChart3D
 * @exploded: whether all slices are exploded
 *
 * Sets whether all slices are exploded.
 *
 * Since: 1.0
 */
void
lrg_pie_chart3d_explode_all (LrgPieChart3D *self,
                             gboolean       exploded)
{
    LrgChart *chart;
    GPtrArray *all_series;
    guint i, j;

    g_return_if_fail (LRG_IS_PIE_CHART3D (self));

    if (!exploded)
    {
        g_hash_table_remove_all (self->exploded_slices);
        return;
    }

    chart = LRG_CHART (self);
    all_series = lrg_chart_get_all_series (chart);
    if (all_series == NULL)
        return;

    for (i = 0; i < all_series->len; i++)
    {
        LrgChartDataSeries *series = g_ptr_array_index (all_series, i);
        GPtrArray *points = lrg_chart_data_series_get_points (series);

        if (points == NULL)
            continue;

        for (j = 0; j < points->len; j++)
        {
            lrg_pie_chart3d_explode_slice (self, (gint)i, (gint)j, TRUE);
        }
    }
}
