/* lrg-bar-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-bar-chart3d.h"
#include "lrg-chart-data-series.h"
#include "lrg-chart-data-point.h"
#include "../lrg-log.h"

struct _LrgBarChart3D
{
    LrgChart3D parent_instance;

    /* Bar dimensions */
    gfloat bar_width;       /* Fraction of space (0-1) */
    gfloat bar_depth;       /* Fraction of space (0-1) */
    gfloat bar_spacing;     /* Fraction between bars */

    /* Display options */
    gboolean  show_edges;
    GrlColor *edge_color;
    gfloat    edge_width;
};

enum
{
    PROP_0,
    PROP_BAR_WIDTH,
    PROP_BAR_DEPTH,
    PROP_BAR_SPACING,
    PROP_SHOW_EDGES,
    PROP_EDGE_COLOR,
    PROP_EDGE_WIDTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgBarChart3D, lrg_bar_chart3d, LRG_TYPE_CHART3D)

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

/*
 * Represents a 3D bar for depth sorting.
 */
typedef struct
{
    gdouble x, y, z;        /* Normalized center position */
    gdouble width, height, depth;  /* Dimensions */
    GrlColor *color;        /* Bar color (not owned, cast from const) */
    gfloat sort_depth;      /* For painter's algorithm */
} Bar3DInfo;

static gint
compare_bar_depth (gconstpointer a, gconstpointer b)
{
    const Bar3DInfo *bar_a = a;
    const Bar3DInfo *bar_b = b;

    /* Sort back-to-front (larger depth first) */
    if (bar_a->sort_depth > bar_b->sort_depth)
        return -1;
    else if (bar_a->sort_depth < bar_b->sort_depth)
        return 1;
    return 0;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_bar_chart3d_draw_data_3d (LrgChart3D *chart3d)
{
    LrgBarChart3D *self = LRG_BAR_CHART3D (chart3d);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    GArray *bars;
    guint i, j;
    gdouble x_min, x_max, y_min, y_max, z_min, z_max;
    gdouble x_range, y_range, z_range;

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL || series_list->len == 0)
        return;

    /* Get data ranges */
    lrg_chart3d_get_x_range (chart3d, &x_min, &x_max);
    lrg_chart3d_get_y_range (chart3d, &y_min, &y_max);
    lrg_chart3d_get_z_range (chart3d, &z_min, &z_max);

    x_range = x_max - x_min;
    y_range = y_max - y_min;
    z_range = z_max - z_min;

    if (x_range <= 0.0) x_range = 1.0;
    if (y_range <= 0.0) y_range = 1.0;
    if (z_range <= 0.0) z_range = 1.0;

    /* Collect all bars with their depths for sorting */
    bars = g_array_new (FALSE, FALSE, sizeof (Bar3DInfo));

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        const GrlColor *color;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        color = lrg_chart_data_series_get_color (series);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            Bar3DInfo bar;
            gdouble nx, ny, nz;

            pt = lrg_chart_data_series_get_point (series, j);

            /* Normalize position to -1..1 range */
            nx = (lrg_chart_data_point_get_x (pt) - x_min) / x_range * 2.0 - 1.0;
            nz = (lrg_chart_data_point_get_z (pt) - z_min) / z_range * 2.0 - 1.0;

            /* Y is the bar height, normalized from base (-1) */
            ny = (lrg_chart_data_point_get_y (pt) - y_min) / y_range * 2.0 - 1.0;

            bar.x = nx;
            bar.y = -1.0;  /* Base of bar */
            bar.z = nz;
            bar.width = self->bar_width * (2.0 / 10.0);  /* Scale factor */
            bar.depth = self->bar_depth * (2.0 / 10.0);
            bar.height = ny + 1.0;  /* Height from base */
            bar.color = (GrlColor *)((lrg_chart_data_point_get_color (pt) != NULL) ?
                         lrg_chart_data_point_get_color (pt) : color);

            /* Get depth for sorting */
            bar.sort_depth = lrg_chart3d_get_depth (chart3d, bar.x, bar.y + bar.height / 2.0, bar.z);

            g_array_append_val (bars, bar);
        }
    }

    /* Sort bars by depth (back to front) */
    g_array_sort (bars, compare_bar_depth);

    /* Draw bars */
    for (i = 0; i < bars->len; i++)
    {
        Bar3DInfo *bar = &g_array_index (bars, Bar3DInfo, i);

        lrg_chart3d_draw_box_3d (chart3d,
                                  bar->x, bar->y, bar->z,
                                  bar->width, bar->height, bar->depth,
                                  bar->color);
    }

    g_array_unref (bars);
}

static gboolean
lrg_bar_chart3d_hit_test (LrgChart      *chart,
                          gfloat         screen_x,
                          gfloat         screen_y,
                          LrgChartHitInfo *out_hit)
{
    /* Basic hit testing - check if click is within projected bar bounds */
    /* For full implementation, would need to project each bar and check containment */
    /* Simplified version - no hit testing for 3D charts */
    (void)chart;
    (void)screen_x;
    (void)screen_y;
    (void)out_hit;

    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_bar_chart3d_finalize (GObject *object)
{
    LrgBarChart3D *self = LRG_BAR_CHART3D (object);

    g_clear_pointer (&self->edge_color, grl_color_free);

    G_OBJECT_CLASS (lrg_bar_chart3d_parent_class)->finalize (object);
}

static void
lrg_bar_chart3d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgBarChart3D *self = LRG_BAR_CHART3D (object);

    switch (prop_id)
    {
    case PROP_BAR_WIDTH:
        g_value_set_float (value, self->bar_width);
        break;
    case PROP_BAR_DEPTH:
        g_value_set_float (value, self->bar_depth);
        break;
    case PROP_BAR_SPACING:
        g_value_set_float (value, self->bar_spacing);
        break;
    case PROP_SHOW_EDGES:
        g_value_set_boolean (value, self->show_edges);
        break;
    case PROP_EDGE_COLOR:
        g_value_set_boxed (value, self->edge_color);
        break;
    case PROP_EDGE_WIDTH:
        g_value_set_float (value, self->edge_width);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bar_chart3d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgBarChart3D *self = LRG_BAR_CHART3D (object);

    switch (prop_id)
    {
    case PROP_BAR_WIDTH:
        lrg_bar_chart3d_set_bar_width (self, g_value_get_float (value));
        break;
    case PROP_BAR_DEPTH:
        lrg_bar_chart3d_set_bar_depth (self, g_value_get_float (value));
        break;
    case PROP_BAR_SPACING:
        lrg_bar_chart3d_set_bar_spacing (self, g_value_get_float (value));
        break;
    case PROP_SHOW_EDGES:
        lrg_bar_chart3d_set_show_edges (self, g_value_get_boolean (value));
        break;
    case PROP_EDGE_COLOR:
        lrg_bar_chart3d_set_edge_color (self, g_value_get_boxed (value));
        break;
    case PROP_EDGE_WIDTH:
        lrg_bar_chart3d_set_edge_width (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bar_chart3d_class_init (LrgBarChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);
    LrgChart3DClass *chart3d_class = LRG_CHART3D_CLASS (klass);

    object_class->finalize = lrg_bar_chart3d_finalize;
    object_class->get_property = lrg_bar_chart3d_get_property;
    object_class->set_property = lrg_bar_chart3d_set_property;

    chart_class->hit_test = lrg_bar_chart3d_hit_test;
    chart3d_class->draw_data_3d = lrg_bar_chart3d_draw_data_3d;

    properties[PROP_BAR_WIDTH] =
        g_param_spec_float ("bar-width",
                            "Bar Width",
                            "Bar width as fraction of space",
                            0.1f, 1.0f, 0.7f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BAR_DEPTH] =
        g_param_spec_float ("bar-depth",
                            "Bar Depth",
                            "Bar depth as fraction of space",
                            0.1f, 1.0f, 0.7f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BAR_SPACING] =
        g_param_spec_float ("bar-spacing",
                            "Bar Spacing",
                            "Spacing between bars",
                            0.0f, 0.9f, 0.2f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_EDGES] =
        g_param_spec_boolean ("show-edges",
                              "Show Edges",
                              "Draw bar edges",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_EDGE_COLOR] =
        g_param_spec_boxed ("edge-color",
                            "Edge Color",
                            "Color for bar edges",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_EDGE_WIDTH] =
        g_param_spec_float ("edge-width",
                            "Edge Width",
                            "Width of bar edges",
                            0.5f, 5.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_bar_chart3d_init (LrgBarChart3D *self)
{
    self->bar_width = 0.7f;
    self->bar_depth = 0.7f;
    self->bar_spacing = 0.2f;
    self->show_edges = TRUE;
    self->edge_color = grl_color_new (30, 30, 30, 255);
    self->edge_width = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgBarChart3D *
lrg_bar_chart3d_new (void)
{
    return g_object_new (LRG_TYPE_BAR_CHART3D, NULL);
}

LrgBarChart3D *
lrg_bar_chart3d_new_with_size (gfloat width,
                               gfloat height)
{
    return g_object_new (LRG_TYPE_BAR_CHART3D,
                         "width", width,
                         "height", height,
                         NULL);
}

gfloat
lrg_bar_chart3d_get_bar_width (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), 0.7f);

    return self->bar_width;
}

void
lrg_bar_chart3d_set_bar_width (LrgBarChart3D *self,
                               gfloat         width)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    width = CLAMP (width, 0.1f, 1.0f);
    if (self->bar_width != width)
    {
        self->bar_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_WIDTH]);
    }
}

gfloat
lrg_bar_chart3d_get_bar_depth (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), 0.7f);

    return self->bar_depth;
}

void
lrg_bar_chart3d_set_bar_depth (LrgBarChart3D *self,
                               gfloat         depth)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    depth = CLAMP (depth, 0.1f, 1.0f);
    if (self->bar_depth != depth)
    {
        self->bar_depth = depth;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_DEPTH]);
    }
}

gfloat
lrg_bar_chart3d_get_bar_spacing (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), 0.2f);

    return self->bar_spacing;
}

void
lrg_bar_chart3d_set_bar_spacing (LrgBarChart3D *self,
                                 gfloat         spacing)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    spacing = CLAMP (spacing, 0.0f, 0.9f);
    if (self->bar_spacing != spacing)
    {
        self->bar_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BAR_SPACING]);
    }
}

gboolean
lrg_bar_chart3d_get_show_edges (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), TRUE);

    return self->show_edges;
}

void
lrg_bar_chart3d_set_show_edges (LrgBarChart3D *self,
                                gboolean       show)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    show = !!show;
    if (self->show_edges != show)
    {
        self->show_edges = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_EDGES]);
    }
}

GrlColor *
lrg_bar_chart3d_get_edge_color (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), NULL);

    return self->edge_color;
}

void
lrg_bar_chart3d_set_edge_color (LrgBarChart3D *self,
                                GrlColor      *color)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    g_clear_pointer (&self->edge_color, grl_color_free);
    if (color != NULL)
        self->edge_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_COLOR]);
}

gfloat
lrg_bar_chart3d_get_edge_width (LrgBarChart3D *self)
{
    g_return_val_if_fail (LRG_IS_BAR_CHART3D (self), 1.0f);

    return self->edge_width;
}

void
lrg_bar_chart3d_set_edge_width (LrgBarChart3D *self,
                                gfloat         width)
{
    g_return_if_fail (LRG_IS_BAR_CHART3D (self));

    if (self->edge_width != width)
    {
        self->edge_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_WIDTH]);
    }
}
