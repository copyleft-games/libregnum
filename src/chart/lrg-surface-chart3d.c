/* lrg-surface-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSurfaceChart3D - 3D Surface Chart widget implementation.
 *
 * Renders data as a 3D mesh surface. Uses depth sorting for
 * proper rendering of overlapping polygons.
 */

#include "lrg-surface-chart3d.h"
#include "../lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART

/**
 * LrgSurfaceChart3D:
 *
 * A 3D surface chart widget that renders height data as a mesh.
 */
struct _LrgSurfaceChart3D
{
    LrgChart3D parent_instance;

    /* Grid data */
    gdouble *grid_data;
    guint rows;
    guint cols;

    /* Value range */
    gdouble y_min;
    gdouble y_max;

    /* Display options */
    gboolean show_wireframe;
    gboolean show_fill;
    GrlColor *wireframe_color;
    LrgChartColorScale *color_scale;
    gfloat fill_opacity;
};

enum
{
    PROP_0,
    PROP_ROWS,
    PROP_COLS,
    PROP_SHOW_WIREFRAME,
    PROP_SHOW_FILL,
    PROP_WIREFRAME_COLOR,
    PROP_COLOR_SCALE,
    PROP_FILL_OPACITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgSurfaceChart3D, lrg_surface_chart3d, LRG_TYPE_CHART3D)

/* ==========================================================================
 * Internal Types
 * ========================================================================== */

/*
 * QuadInfo:
 *
 * Information about a surface quad for depth-sorted rendering.
 */
typedef struct
{
    gfloat x1, y1, x2, y2, x3, y3, x4, y4;  /* Screen coords */
    GrlColor *fill_color;
    gfloat sort_depth;
    guint row, col;
} QuadInfo;

static gint
compare_quads_by_depth (gconstpointer a,
                        gconstpointer b)
{
    const QuadInfo *qa = a;
    const QuadInfo *qb = b;

    /* Sort back to front */
    if (qa->sort_depth > qb->sort_depth)
        return -1;
    if (qa->sort_depth < qb->sort_depth)
        return 1;
    return 0;
}

static gdouble
get_grid_value (LrgSurfaceChart3D *self,
                guint              row,
                guint              col)
{
    if (self->grid_data == NULL || row >= self->rows || col >= self->cols)
        return 0.0;
    return self->grid_data[row * self->cols + col];
}

/* ==========================================================================
 * Drawing Implementation
 * ========================================================================== */

static void
lrg_surface_chart3d_draw_data_3d (LrgChart3D *chart3d)
{
    LrgSurfaceChart3D *self = LRG_SURFACE_CHART3D (chart3d);
    GArray *quads;
    guint row, col;
    gdouble y_range;
    guint i;

    if (self->grid_data == NULL || self->rows < 2 || self->cols < 2)
        return;

    y_range = self->y_max - self->y_min;
    if (y_range <= 0.0)
        y_range = 1.0;

    /* Collect quads for depth sorting */
    quads = g_array_new (FALSE, FALSE, sizeof (QuadInfo));

    for (row = 0; row < self->rows - 1; row++)
    {
        for (col = 0; col < self->cols - 1; col++)
        {
            QuadInfo quad;
            gdouble v00, v01, v10, v11;
            gdouble nx00, nz00, ny00;
            gdouble nx01, nz01, ny01;
            gdouble nx10, nz10, ny10;
            gdouble nx11, nz11, ny11;
            gfloat d00, d01, d10, d11;
            gdouble avg_y;

            /* Get four corner values */
            v00 = get_grid_value (self, row, col);
            v01 = get_grid_value (self, row, col + 1);
            v10 = get_grid_value (self, row + 1, col);
            v11 = get_grid_value (self, row + 1, col + 1);

            /* Normalize coordinates to 0-1 range */
            nx00 = (gdouble)col / (gdouble)(self->cols - 1);
            nz00 = (gdouble)row / (gdouble)(self->rows - 1);
            ny00 = (v00 - self->y_min) / y_range;

            nx01 = (gdouble)(col + 1) / (gdouble)(self->cols - 1);
            nz01 = nz00;
            ny01 = (v01 - self->y_min) / y_range;

            nx10 = nx00;
            nz10 = (gdouble)(row + 1) / (gdouble)(self->rows - 1);
            ny10 = (v10 - self->y_min) / y_range;

            nx11 = nx01;
            nz11 = nz10;
            ny11 = (v11 - self->y_min) / y_range;

            /* Project to screen */
            lrg_chart3d_project_point (chart3d, nx00, ny00, nz00,
                                       &quad.x1, &quad.y1, &d00);
            lrg_chart3d_project_point (chart3d, nx01, ny01, nz01,
                                       &quad.x2, &quad.y2, &d01);
            lrg_chart3d_project_point (chart3d, nx11, ny11, nz11,
                                       &quad.x3, &quad.y3, &d11);
            lrg_chart3d_project_point (chart3d, nx10, ny10, nz10,
                                       &quad.x4, &quad.y4, &d10);

            quad.sort_depth = (d00 + d01 + d10 + d11) / 4.0f;
            quad.row = row;
            quad.col = col;

            /* Get color from color scale based on average height */
            avg_y = (ny00 + ny01 + ny10 + ny11) / 4.0;
            if (self->color_scale != NULL)
            {
                quad.fill_color = lrg_chart_color_scale_get_color (self->color_scale,
                                                                   avg_y);
                /* Apply opacity */
                if (self->fill_opacity < 1.0f)
                {
                    guint8 alpha = (guint8)(grl_color_get_a (quad.fill_color) *
                                            self->fill_opacity);
                    GrlColor *new_color = grl_color_new (
                        grl_color_get_r (quad.fill_color),
                        grl_color_get_g (quad.fill_color),
                        grl_color_get_b (quad.fill_color),
                        alpha
                    );
                    grl_color_free (quad.fill_color);
                    quad.fill_color = new_color;
                }
            }
            else
            {
                /* Default color based on height */
                guint8 brightness = (guint8)(avg_y * 200 + 55);
                guint8 alpha = (guint8)(255 * self->fill_opacity);
                quad.fill_color = grl_color_new (brightness, brightness, brightness, alpha);
            }

            g_array_append_val (quads, quad);
        }
    }

    /* Sort quads by depth (back to front) */
    g_array_sort (quads, compare_quads_by_depth);

    /* Draw all quads */
    for (i = 0; i < quads->len; i++)
    {
        QuadInfo *q = &g_array_index (quads, QuadInfo, i);

        if (self->show_fill)
        {
            /* Draw quad as two triangles */
            grl_draw_triangle ((gint)q->x1, (gint)q->y1,
                               (gint)q->x2, (gint)q->y2,
                               (gint)q->x3, (gint)q->y3, q->fill_color);
            grl_draw_triangle ((gint)q->x1, (gint)q->y1,
                               (gint)q->x3, (gint)q->y3,
                               (gint)q->x4, (gint)q->y4, q->fill_color);
        }

        grl_color_free (q->fill_color);
    }

    /* Draw wireframe on top if enabled */
    if (self->show_wireframe)
    {
        for (row = 0; row < self->rows - 1; row++)
        {
            for (col = 0; col < self->cols - 1; col++)
            {
                gdouble v00, v01, v10;
                gdouble nx00, nz00, ny00;
                gdouble nx01, nz01, ny01;
                gdouble nx10, nz10, ny10;
                gfloat sx00, sy00, sx01, sy01, sx10, sy10;
                gfloat depth;
                g_autoptr(GrlVector2) p00 = NULL;
                g_autoptr(GrlVector2) p01 = NULL;
                g_autoptr(GrlVector2) p10 = NULL;

                v00 = get_grid_value (self, row, col);
                v01 = get_grid_value (self, row, col + 1);
                v10 = get_grid_value (self, row + 1, col);

                nx00 = (gdouble)col / (gdouble)(self->cols - 1);
                nz00 = (gdouble)row / (gdouble)(self->rows - 1);
                ny00 = (v00 - self->y_min) / y_range;

                nx01 = (gdouble)(col + 1) / (gdouble)(self->cols - 1);
                nz01 = nz00;
                ny01 = (v01 - self->y_min) / y_range;

                nx10 = nx00;
                nz10 = (gdouble)(row + 1) / (gdouble)(self->rows - 1);
                ny10 = (v10 - self->y_min) / y_range;

                lrg_chart3d_project_point (chart3d, nx00, ny00, nz00,
                                           &sx00, &sy00, &depth);
                lrg_chart3d_project_point (chart3d, nx01, ny01, nz01,
                                           &sx01, &sy01, &depth);
                lrg_chart3d_project_point (chart3d, nx10, ny10, nz10,
                                           &sx10, &sy10, &depth);

                p00 = grl_vector2_new (sx00, sy00);
                p01 = grl_vector2_new (sx01, sy01);
                p10 = grl_vector2_new (sx10, sy10);

                /* Draw horizontal and vertical lines */
                grl_draw_line_ex (p00, p01, 1.0f, self->wireframe_color);
                grl_draw_line_ex (p00, p10, 1.0f, self->wireframe_color);
            }
        }

        /* Draw last column vertical lines */
        for (row = 0; row < self->rows - 1; row++)
        {
            gdouble v0, v1;
            gdouble nx0, nz0, ny0;
            gdouble nx1, nz1, ny1;
            gfloat sx0, sy0, sx1, sy1;
            gfloat depth;
            g_autoptr(GrlVector2) p0 = NULL;
            g_autoptr(GrlVector2) p1 = NULL;

            v0 = get_grid_value (self, row, self->cols - 1);
            v1 = get_grid_value (self, row + 1, self->cols - 1);

            nx0 = 1.0;
            nz0 = (gdouble)row / (gdouble)(self->rows - 1);
            ny0 = (v0 - self->y_min) / y_range;

            nx1 = 1.0;
            nz1 = (gdouble)(row + 1) / (gdouble)(self->rows - 1);
            ny1 = (v1 - self->y_min) / y_range;

            lrg_chart3d_project_point (chart3d, nx0, ny0, nz0, &sx0, &sy0, &depth);
            lrg_chart3d_project_point (chart3d, nx1, ny1, nz1, &sx1, &sy1, &depth);

            p0 = grl_vector2_new (sx0, sy0);
            p1 = grl_vector2_new (sx1, sy1);

            grl_draw_line_ex (p0, p1, 1.0f, self->wireframe_color);
        }

        /* Draw last row horizontal lines */
        for (col = 0; col < self->cols - 1; col++)
        {
            gdouble v0, v1;
            gdouble nx0, nz0, ny0;
            gdouble nx1, nz1, ny1;
            gfloat sx0, sy0, sx1, sy1;
            gfloat depth;
            g_autoptr(GrlVector2) p0 = NULL;
            g_autoptr(GrlVector2) p1 = NULL;

            v0 = get_grid_value (self, self->rows - 1, col);
            v1 = get_grid_value (self, self->rows - 1, col + 1);

            nx0 = (gdouble)col / (gdouble)(self->cols - 1);
            nz0 = 1.0;
            ny0 = (v0 - self->y_min) / y_range;

            nx1 = (gdouble)(col + 1) / (gdouble)(self->cols - 1);
            nz1 = 1.0;
            ny1 = (v1 - self->y_min) / y_range;

            lrg_chart3d_project_point (chart3d, nx0, ny0, nz0, &sx0, &sy0, &depth);
            lrg_chart3d_project_point (chart3d, nx1, ny1, nz1, &sx1, &sy1, &depth);

            p0 = grl_vector2_new (sx0, sy0);
            p1 = grl_vector2_new (sx1, sy1);

            grl_draw_line_ex (p0, p1, 1.0f, self->wireframe_color);
        }
    }

    g_array_free (quads, TRUE);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_surface_chart3d_dispose (GObject *object)
{
    LrgSurfaceChart3D *self = LRG_SURFACE_CHART3D (object);

    g_clear_pointer (&self->grid_data, g_free);
    g_clear_pointer (&self->wireframe_color, grl_color_free);
    g_clear_object (&self->color_scale);

    G_OBJECT_CLASS (lrg_surface_chart3d_parent_class)->dispose (object);
}

static void
lrg_surface_chart3d_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgSurfaceChart3D *self = LRG_SURFACE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_ROWS:
        g_value_set_uint (value, self->rows);
        break;
    case PROP_COLS:
        g_value_set_uint (value, self->cols);
        break;
    case PROP_SHOW_WIREFRAME:
        g_value_set_boolean (value, self->show_wireframe);
        break;
    case PROP_SHOW_FILL:
        g_value_set_boolean (value, self->show_fill);
        break;
    case PROP_WIREFRAME_COLOR:
        g_value_set_boxed (value, self->wireframe_color);
        break;
    case PROP_COLOR_SCALE:
        g_value_set_object (value, self->color_scale);
        break;
    case PROP_FILL_OPACITY:
        g_value_set_float (value, self->fill_opacity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_surface_chart3d_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgSurfaceChart3D *self = LRG_SURFACE_CHART3D (object);

    switch (prop_id)
    {
    case PROP_SHOW_WIREFRAME:
        lrg_surface_chart3d_set_show_wireframe (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_FILL:
        lrg_surface_chart3d_set_show_fill (self, g_value_get_boolean (value));
        break;
    case PROP_WIREFRAME_COLOR:
        lrg_surface_chart3d_set_wireframe_color (self, g_value_get_boxed (value));
        break;
    case PROP_COLOR_SCALE:
        lrg_surface_chart3d_set_color_scale (self, g_value_get_object (value));
        break;
    case PROP_FILL_OPACITY:
        lrg_surface_chart3d_set_fill_opacity (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_surface_chart3d_class_init (LrgSurfaceChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgChart3DClass *chart3d_class = LRG_CHART3D_CLASS (klass);

    object_class->dispose = lrg_surface_chart3d_dispose;
    object_class->get_property = lrg_surface_chart3d_get_property;
    object_class->set_property = lrg_surface_chart3d_set_property;

    chart3d_class->draw_data_3d = lrg_surface_chart3d_draw_data_3d;

    /**
     * LrgSurfaceChart3D:rows:
     *
     * The number of rows in the grid.
     *
     * Since: 1.0
     */
    properties[PROP_ROWS] =
        g_param_spec_uint ("rows",
                           "Rows",
                           "Number of rows in the grid",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:cols:
     *
     * The number of columns in the grid.
     *
     * Since: 1.0
     */
    properties[PROP_COLS] =
        g_param_spec_uint ("cols",
                           "Cols",
                           "Number of columns in the grid",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:show-wireframe:
     *
     * Whether to show wireframe lines.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_WIREFRAME] =
        g_param_spec_boolean ("show-wireframe",
                              "Show Wireframe",
                              "Whether to show wireframe lines",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:show-fill:
     *
     * Whether to fill the surface.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_FILL] =
        g_param_spec_boolean ("show-fill",
                              "Show Fill",
                              "Whether to fill the surface",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:wireframe-color:
     *
     * The wireframe color.
     *
     * Since: 1.0
     */
    properties[PROP_WIREFRAME_COLOR] =
        g_param_spec_boxed ("wireframe-color",
                            "Wireframe Color",
                            "The wireframe color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:color-scale:
     *
     * The color scale for height-based coloring.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR_SCALE] =
        g_param_spec_object ("color-scale",
                             "Color Scale",
                             "Color scale for height-based coloring",
                             LRG_TYPE_CHART_COLOR_SCALE,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgSurfaceChart3D:fill-opacity:
     *
     * The fill opacity.
     *
     * Since: 1.0
     */
    properties[PROP_FILL_OPACITY] =
        g_param_spec_float ("fill-opacity",
                            "Fill Opacity",
                            "Fill opacity",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_surface_chart3d_init (LrgSurfaceChart3D *self)
{
    self->grid_data = NULL;
    self->rows = 0;
    self->cols = 0;
    self->y_min = 0.0;
    self->y_max = 1.0;
    self->show_wireframe = TRUE;
    self->show_fill = TRUE;
    self->wireframe_color = grl_color_new (0, 0, 0, 255);
    self->color_scale = NULL;
    self->fill_opacity = 1.0f;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

LrgSurfaceChart3D *
lrg_surface_chart3d_new (void)
{
    return g_object_new (LRG_TYPE_SURFACE_CHART3D, NULL);
}

LrgSurfaceChart3D *
lrg_surface_chart3d_new_with_size (gfloat width,
                                   gfloat height)
{
    return g_object_new (LRG_TYPE_SURFACE_CHART3D,
                         "width", width,
                         "height", height,
                         NULL);
}

/* ==========================================================================
 * Public API - Grid Data
 * ========================================================================== */

void
lrg_surface_chart3d_set_grid_size (LrgSurfaceChart3D *self,
                                   guint              rows,
                                   guint              cols)
{
    gsize size;

    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));
    g_return_if_fail (rows >= 2);
    g_return_if_fail (cols >= 2);

    g_clear_pointer (&self->grid_data, g_free);

    self->rows = rows;
    self->cols = cols;
    size = rows * cols * sizeof (gdouble);
    self->grid_data = g_malloc0 (size);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROWS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLS]);
}

guint
lrg_surface_chart3d_get_rows (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), 0);
    return self->rows;
}

guint
lrg_surface_chart3d_get_cols (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), 0);
    return self->cols;
}

void
lrg_surface_chart3d_set_value (LrgSurfaceChart3D *self,
                               guint              row,
                               guint              col,
                               gdouble            value)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));
    g_return_if_fail (self->grid_data != NULL);
    g_return_if_fail (row < self->rows);
    g_return_if_fail (col < self->cols);

    self->grid_data[row * self->cols + col] = value;
}

gdouble
lrg_surface_chart3d_get_value (LrgSurfaceChart3D *self,
                               guint              row,
                               guint              col)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), 0.0);
    g_return_val_if_fail (self->grid_data != NULL, 0.0);
    g_return_val_if_fail (row < self->rows, 0.0);
    g_return_val_if_fail (col < self->cols, 0.0);

    return self->grid_data[row * self->cols + col];
}

void
lrg_surface_chart3d_set_row (LrgSurfaceChart3D *self,
                             guint              row,
                             const gdouble     *values,
                             guint              count)
{
    guint i;

    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));
    g_return_if_fail (self->grid_data != NULL);
    g_return_if_fail (row < self->rows);
    g_return_if_fail (values != NULL);

    count = MIN (count, self->cols);
    for (i = 0; i < count; i++)
    {
        self->grid_data[row * self->cols + i] = values[i];
    }
}

void
lrg_surface_chart3d_set_from_function (LrgSurfaceChart3D *self,
                                       guint              rows,
                                       guint              cols,
                                       gdouble            x_min,
                                       gdouble            x_max,
                                       gdouble            z_min,
                                       gdouble            z_max,
                                       gdouble          (*func) (gdouble x, gdouble z, gpointer user_data),
                                       gpointer           user_data)
{
    guint row, col;
    gdouble x_step, z_step;

    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));
    g_return_if_fail (rows >= 2);
    g_return_if_fail (cols >= 2);
    g_return_if_fail (func != NULL);

    lrg_surface_chart3d_set_grid_size (self, rows, cols);

    x_step = (x_max - x_min) / (gdouble)(cols - 1);
    z_step = (z_max - z_min) / (gdouble)(rows - 1);

    for (row = 0; row < rows; row++)
    {
        gdouble z = z_min + row * z_step;
        for (col = 0; col < cols; col++)
        {
            gdouble x = x_min + col * x_step;
            gdouble y = func (x, z, user_data);
            self->grid_data[row * cols + col] = y;
        }
    }

    lrg_surface_chart3d_auto_range (self);
}

/* ==========================================================================
 * Public API - Display Options
 * ========================================================================== */

gboolean
lrg_surface_chart3d_get_show_wireframe (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), TRUE);
    return self->show_wireframe;
}

void
lrg_surface_chart3d_set_show_wireframe (LrgSurfaceChart3D *self,
                                        gboolean           show)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (self->show_wireframe != show)
    {
        self->show_wireframe = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_WIREFRAME]);
    }
}

gboolean
lrg_surface_chart3d_get_show_fill (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), TRUE);
    return self->show_fill;
}

void
lrg_surface_chart3d_set_show_fill (LrgSurfaceChart3D *self,
                                   gboolean           show)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (self->show_fill != show)
    {
        self->show_fill = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_FILL]);
    }
}

GrlColor *
lrg_surface_chart3d_get_wireframe_color (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), NULL);
    return self->wireframe_color;
}

void
lrg_surface_chart3d_set_wireframe_color (LrgSurfaceChart3D *self,
                                         GrlColor          *color)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (color == NULL)
        return;

    g_clear_pointer (&self->wireframe_color, grl_color_free);
    self->wireframe_color = grl_color_copy (color);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIREFRAME_COLOR]);
}

LrgChartColorScale *
lrg_surface_chart3d_get_color_scale (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), NULL);
    return self->color_scale;
}

void
lrg_surface_chart3d_set_color_scale (LrgSurfaceChart3D  *self,
                                     LrgChartColorScale *scale)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (g_set_object (&self->color_scale, scale))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_SCALE]);
    }
}

gfloat
lrg_surface_chart3d_get_fill_opacity (LrgSurfaceChart3D *self)
{
    g_return_val_if_fail (LRG_IS_SURFACE_CHART3D (self), 1.0f);
    return self->fill_opacity;
}

void
lrg_surface_chart3d_set_fill_opacity (LrgSurfaceChart3D *self,
                                      gfloat             opacity)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    opacity = CLAMP (opacity, 0.0f, 1.0f);

    if (self->fill_opacity != opacity)
    {
        self->fill_opacity = opacity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_OPACITY]);
    }
}

/* ==========================================================================
 * Public API - Value Range
 * ========================================================================== */

void
lrg_surface_chart3d_auto_range (LrgSurfaceChart3D *self)
{
    guint i;
    gsize count;

    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (self->grid_data == NULL || self->rows == 0 || self->cols == 0)
        return;

    count = self->rows * self->cols;
    self->y_min = self->grid_data[0];
    self->y_max = self->grid_data[0];

    for (i = 1; i < count; i++)
    {
        if (self->grid_data[i] < self->y_min)
            self->y_min = self->grid_data[i];
        if (self->grid_data[i] > self->y_max)
            self->y_max = self->grid_data[i];
    }

    /* Ensure non-zero range */
    if (self->y_max <= self->y_min)
        self->y_max = self->y_min + 1.0;
}

void
lrg_surface_chart3d_set_y_range (LrgSurfaceChart3D *self,
                                 gdouble            min,
                                 gdouble            max)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));
    g_return_if_fail (max > min);

    self->y_min = min;
    self->y_max = max;
}

void
lrg_surface_chart3d_get_y_range (LrgSurfaceChart3D *self,
                                 gdouble           *min,
                                 gdouble           *max)
{
    g_return_if_fail (LRG_IS_SURFACE_CHART3D (self));

    if (min != NULL)
        *min = self->y_min;
    if (max != NULL)
        *max = self->y_max;
}
