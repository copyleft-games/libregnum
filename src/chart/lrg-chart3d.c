/* lrg-chart3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-chart3d.h"
#include "lrg-chart-data-series.h"
#include "lrg-chart-data-point.h"
#include "lrg-chart-enums.h"
#include "../lrg-log.h"
#include <math.h>

typedef struct
{
    /* Camera settings */
    gfloat camera_yaw;       /* Horizontal rotation in degrees */
    gfloat camera_pitch;     /* Vertical rotation in degrees */
    gfloat camera_distance;  /* Distance from center */
    gfloat field_of_view;    /* FOV in degrees */

    /* Axis configs */
    LrgChartAxisConfig *x_axis;
    LrgChartAxisConfig *y_axis;
    LrgChartAxisConfig *z_axis;

    /* Display options */
    gboolean show_legend;
    gboolean show_axes;
    gboolean show_grid;
    gboolean enable_rotation;
    LrgChartLegendPosition legend_position;

    /* Cached projection data */
    gfloat cos_yaw, sin_yaw;
    gfloat cos_pitch, sin_pitch;

    /* Data ranges (cached) */
    gdouble x_min, x_max;
    gdouble y_min, y_max;
    gdouble z_min, z_max;
    gboolean ranges_valid;
} LrgChart3DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgChart3D, lrg_chart3d, LRG_TYPE_CHART)

enum
{
    PROP_0,
    PROP_CAMERA_YAW,
    PROP_CAMERA_PITCH,
    PROP_CAMERA_DISTANCE,
    PROP_FIELD_OF_VIEW,
    PROP_SHOW_LEGEND,
    PROP_SHOW_AXES,
    PROP_SHOW_GRID,
    PROP_ENABLE_ROTATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static void
update_trig_cache (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    gfloat yaw_rad, pitch_rad;

    yaw_rad = priv->camera_yaw * G_PI / 180.0f;
    pitch_rad = priv->camera_pitch * G_PI / 180.0f;

    priv->cos_yaw = cosf (yaw_rad);
    priv->sin_yaw = sinf (yaw_rad);
    priv->cos_pitch = cosf (pitch_rad);
    priv->sin_pitch = sinf (pitch_rad);
}

static void
calculate_data_ranges (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    guint i, j;
    gboolean found;

    priv->x_min = priv->y_min = priv->z_min = G_MAXDOUBLE;
    priv->x_max = priv->y_max = priv->z_max = -G_MAXDOUBLE;
    found = FALSE;

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL)
    {
        priv->ranges_valid = TRUE;
        return;
    }

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        guint point_count;

        series = g_ptr_array_index (series_list, i);
        point_count = lrg_chart_data_series_get_point_count (series);

        for (j = 0; j < point_count; j++)
        {
            const LrgChartDataPoint *pt;
            gdouble px, py, pz;

            pt = lrg_chart_data_series_get_point (series, j);
            px = lrg_chart_data_point_get_x (pt);
            py = lrg_chart_data_point_get_y (pt);
            pz = lrg_chart_data_point_get_z (pt);

            if (px < priv->x_min) priv->x_min = px;
            if (px > priv->x_max) priv->x_max = px;
            if (py < priv->y_min) priv->y_min = py;
            if (py > priv->y_max) priv->y_max = py;
            if (pz < priv->z_min) priv->z_min = pz;
            if (pz > priv->z_max) priv->z_max = pz;
            found = TRUE;
        }
    }

    if (!found)
    {
        priv->x_min = priv->y_min = priv->z_min = 0.0;
        priv->x_max = priv->y_max = priv->z_max = 1.0;
    }

    /* Apply axis config overrides */
    if (priv->x_axis != NULL)
    {
        gdouble axis_min = lrg_chart_axis_config_get_min (priv->x_axis);
        gdouble axis_max = lrg_chart_axis_config_get_max (priv->x_axis);
        if (!isnan (axis_min))
            priv->x_min = axis_min;
        if (!isnan (axis_max))
            priv->x_max = axis_max;
    }

    if (priv->y_axis != NULL)
    {
        gdouble axis_min = lrg_chart_axis_config_get_min (priv->y_axis);
        gdouble axis_max = lrg_chart_axis_config_get_max (priv->y_axis);
        if (!isnan (axis_min))
            priv->y_min = axis_min;
        if (!isnan (axis_max))
            priv->y_max = axis_max;
    }

    if (priv->z_axis != NULL)
    {
        gdouble axis_min = lrg_chart_axis_config_get_min (priv->z_axis);
        gdouble axis_max = lrg_chart_axis_config_get_max (priv->z_axis);
        if (!isnan (axis_min))
            priv->z_min = axis_min;
        if (!isnan (axis_max))
            priv->z_max = axis_max;
    }

    priv->ranges_valid = TRUE;
}

/*
 * Project 3D normalized coordinates to 2D screen.
 * Uses isometric-style projection with perspective.
 */
static void
project_point (LrgChart3D *self,
               gdouble     nx,  /* normalized -1 to 1 */
               gdouble     ny,
               gdouble     nz,
               gfloat     *screen_x,
               gfloat     *screen_y,
               gfloat     *depth)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    gfloat width, height;
    gfloat cx, cy;
    gfloat scale;
    gdouble rx, ry, rz;  /* Rotated coordinates */
    gdouble px, py;      /* Projected coordinates */
    gfloat dist_factor;

    g_object_get (self, "width", &width, "height", &height, NULL);
    cx = width / 2.0f;
    cy = height / 2.0f;
    scale = MIN (width, height) * 0.3f;

    /* Rotate around Y axis (yaw) */
    rx = nx * priv->cos_yaw - nz * priv->sin_yaw;
    rz = nx * priv->sin_yaw + nz * priv->cos_yaw;

    /* Rotate around X axis (pitch) */
    ry = ny * priv->cos_pitch - rz * priv->sin_pitch;
    rz = ny * priv->sin_pitch + rz * priv->cos_pitch;

    /* Apply perspective (simple z-based scaling) */
    dist_factor = priv->camera_distance / (priv->camera_distance + (gfloat)rz);

    /* Project to 2D */
    px = rx * dist_factor;
    py = -ry * dist_factor;  /* Flip Y for screen coordinates */

    *screen_x = cx + (gfloat)px * scale;
    *screen_y = cy + (gfloat)py * scale;

    if (depth)
        *depth = (gfloat)rz;
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_chart3d_real_draw_background (LrgChart3D *self)
{
    gfloat width, height;
    const GrlColor *bg_color;

    g_object_get (self, "width", &width, "height", &height, NULL);
    bg_color = lrg_chart_get_background_color (LRG_CHART (self));

    if (bg_color != NULL)
        grl_draw_rectangle (0, 0, width, height, bg_color);
}

static void
lrg_chart3d_real_draw_axes_3d (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    g_autoptr(GrlColor) red = NULL;
    g_autoptr(GrlColor) green = NULL;
    g_autoptr(GrlColor) blue = NULL;

    if (!priv->show_axes)
        return;

    red = grl_color_new (255, 80, 80, 255);
    green = grl_color_new (80, 255, 80, 255);
    blue = grl_color_new (80, 80, 255, 255);

    /* Draw X axis (red) */
    lrg_chart3d_draw_line_3d (self, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, red, 2.0f);

    /* Draw Y axis (green) - up */
    lrg_chart3d_draw_line_3d (self, 0.0, -1.0, 0.0, 0.0, 1.0, 0.0, green, 2.0f);

    /* Draw Z axis (blue) */
    lrg_chart3d_draw_line_3d (self, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0, blue, 2.0f);
}

static void
lrg_chart3d_real_draw_grid_3d (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    g_autoptr(GrlColor) grid_color = NULL;
    gint i;
    gint grid_lines;
    gdouble step;

    if (!priv->show_grid)
        return;

    grid_color = grl_color_new (80, 80, 80, 128);
    grid_lines = 10;
    step = 2.0 / (gdouble)grid_lines;

    /* Draw grid on XZ plane (Y = -1) */
    for (i = 0; i <= grid_lines; i++)
    {
        gdouble pos;

        pos = -1.0 + i * step;

        /* Lines parallel to X */
        lrg_chart3d_draw_line_3d (self, -1.0, -1.0, pos, 1.0, -1.0, pos,
                                   grid_color, 1.0f);

        /* Lines parallel to Z */
        lrg_chart3d_draw_line_3d (self, pos, -1.0, -1.0, pos, -1.0, 1.0,
                                   grid_color, 1.0f);
    }
}

static void
lrg_chart3d_real_draw_data_3d (LrgChart3D *self)
{
    /* Default implementation does nothing - subclasses override */
    (void)self;
}

static void
lrg_chart3d_real_draw_legend (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);
    LrgChart *chart = LRG_CHART (self);
    GPtrArray *series_list;
    gfloat width, height;
    gfloat x, y;
    guint i;
    g_autoptr(GrlColor) text_color = NULL;

    if (!priv->show_legend)
        return;

    series_list = lrg_chart_get_series_list (chart);
    if (series_list == NULL || series_list->len == 0)
        return;

    g_object_get (self, "width", &width, "height", &height, NULL);

    text_color = grl_color_new (200, 200, 200, 255);

    /* Draw legend at top-right */
    x = width - 120.0f;
    y = 10.0f;

    for (i = 0; i < series_list->len; i++)
    {
        LrgChartDataSeries *series;
        const GrlColor *color;
        const gchar *name;

        series = g_ptr_array_index (series_list, i);
        color = lrg_chart_data_series_get_color (series);
        name = lrg_chart_data_series_get_name (series);

        /* Color box */
        if (color != NULL)
            grl_draw_rectangle (x, y, 12.0f, 12.0f, color);

        /* Name */
        if (name != NULL)
            grl_draw_text (name, (gint)(x + 18.0f), (gint)y, 10, text_color);

        y += 18.0f;
    }
}

static void
lrg_chart3d_real_rotate_view (LrgChart3D *self,
                               gfloat      delta_yaw,
                               gfloat      delta_pitch)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    priv->camera_yaw += delta_yaw;
    priv->camera_pitch += delta_pitch;

    /* Clamp pitch to prevent gimbal lock */
    priv->camera_pitch = CLAMP (priv->camera_pitch, -89.0f, 89.0f);

    /* Normalize yaw */
    while (priv->camera_yaw > 180.0f)
        priv->camera_yaw -= 360.0f;
    while (priv->camera_yaw < -180.0f)
        priv->camera_yaw += 360.0f;

    update_trig_cache (self);
}

static void
lrg_chart3d_real_zoom_view (LrgChart3D *self,
                            gfloat      delta)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    priv->camera_distance -= delta * 0.5f;
    priv->camera_distance = CLAMP (priv->camera_distance, 1.0f, 20.0f);
}

/* ==========================================================================
 * LrgChart Virtual Method Overrides
 * ========================================================================== */

static void
lrg_chart3d_draw (LrgWidget *widget)
{
    LrgChart3D *self = LRG_CHART3D (widget);
    LrgChart3DClass *klass = LRG_CHART3D_GET_CLASS (self);
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    if (klass->draw_background)
        klass->draw_background (self);

    if (klass->draw_grid_3d)
        klass->draw_grid_3d (self);

    if (klass->draw_axes_3d)
        klass->draw_axes_3d (self);

    if (klass->draw_data_3d)
        klass->draw_data_3d (self);

    if (klass->draw_legend)
        klass->draw_legend (self);
}

static void
lrg_chart3d_update_data (LrgChart *chart)
{
    LrgChart3D *self = LRG_CHART3D (chart);
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    priv->ranges_valid = FALSE;

    /* Chain up */
    if (LRG_CHART_CLASS (lrg_chart3d_parent_class)->update_data)
        LRG_CHART_CLASS (lrg_chart3d_parent_class)->update_data (chart);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart3d_finalize (GObject *object)
{
    LrgChart3D *self = LRG_CHART3D (object);
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    g_clear_pointer (&priv->x_axis, lrg_chart_axis_config_free);
    g_clear_pointer (&priv->y_axis, lrg_chart_axis_config_free);
    g_clear_pointer (&priv->z_axis, lrg_chart_axis_config_free);

    G_OBJECT_CLASS (lrg_chart3d_parent_class)->finalize (object);
}

static void
lrg_chart3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgChart3D *self = LRG_CHART3D (object);
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CAMERA_YAW:
        g_value_set_float (value, priv->camera_yaw);
        break;
    case PROP_CAMERA_PITCH:
        g_value_set_float (value, priv->camera_pitch);
        break;
    case PROP_CAMERA_DISTANCE:
        g_value_set_float (value, priv->camera_distance);
        break;
    case PROP_FIELD_OF_VIEW:
        g_value_set_float (value, priv->field_of_view);
        break;
    case PROP_SHOW_LEGEND:
        g_value_set_boolean (value, priv->show_legend);
        break;
    case PROP_SHOW_AXES:
        g_value_set_boolean (value, priv->show_axes);
        break;
    case PROP_SHOW_GRID:
        g_value_set_boolean (value, priv->show_grid);
        break;
    case PROP_ENABLE_ROTATION:
        g_value_set_boolean (value, priv->enable_rotation);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgChart3D *self = LRG_CHART3D (object);

    switch (prop_id)
    {
    case PROP_CAMERA_YAW:
        lrg_chart3d_set_camera_yaw (self, g_value_get_float (value));
        break;
    case PROP_CAMERA_PITCH:
        lrg_chart3d_set_camera_pitch (self, g_value_get_float (value));
        break;
    case PROP_CAMERA_DISTANCE:
        lrg_chart3d_set_camera_distance (self, g_value_get_float (value));
        break;
    case PROP_FIELD_OF_VIEW:
        lrg_chart3d_set_field_of_view (self, g_value_get_float (value));
        break;
    case PROP_SHOW_LEGEND:
        lrg_chart3d_set_show_legend (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_AXES:
        lrg_chart3d_set_show_axes (self, g_value_get_boolean (value));
        break;
    case PROP_SHOW_GRID:
        lrg_chart3d_set_show_grid (self, g_value_get_boolean (value));
        break;
    case PROP_ENABLE_ROTATION:
        lrg_chart3d_set_enable_rotation (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart3d_class_init (LrgChart3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);

    object_class->finalize = lrg_chart3d_finalize;
    object_class->get_property = lrg_chart3d_get_property;
    object_class->set_property = lrg_chart3d_set_property;

    widget_class->draw = lrg_chart3d_draw;

    chart_class->update_data = lrg_chart3d_update_data;

    /* Default implementations */
    klass->draw_background = lrg_chart3d_real_draw_background;
    klass->draw_axes_3d = lrg_chart3d_real_draw_axes_3d;
    klass->draw_grid_3d = lrg_chart3d_real_draw_grid_3d;
    klass->draw_data_3d = lrg_chart3d_real_draw_data_3d;
    klass->draw_legend = lrg_chart3d_real_draw_legend;
    klass->rotate_view = lrg_chart3d_real_rotate_view;
    klass->zoom_view = lrg_chart3d_real_zoom_view;

    properties[PROP_CAMERA_YAW] =
        g_param_spec_float ("camera-yaw",
                            "Camera Yaw",
                            "Horizontal camera rotation",
                            -180.0f, 180.0f, 45.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_PITCH] =
        g_param_spec_float ("camera-pitch",
                            "Camera Pitch",
                            "Vertical camera rotation",
                            -89.0f, 89.0f, 30.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_CAMERA_DISTANCE] =
        g_param_spec_float ("camera-distance",
                            "Camera Distance",
                            "Distance from center",
                            1.0f, 20.0f, 5.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_FIELD_OF_VIEW] =
        g_param_spec_float ("field-of-view",
                            "Field of View",
                            "Camera FOV in degrees",
                            10.0f, 120.0f, 60.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_LEGEND] =
        g_param_spec_boolean ("show-legend",
                              "Show Legend",
                              "Display the legend",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_AXES] =
        g_param_spec_boolean ("show-axes",
                              "Show Axes",
                              "Display 3D axes",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_GRID] =
        g_param_spec_boolean ("show-grid",
                              "Show Grid",
                              "Display base grid",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    properties[PROP_ENABLE_ROTATION] =
        g_param_spec_boolean ("enable-rotation",
                              "Enable Rotation",
                              "Allow interactive rotation",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_chart3d_init (LrgChart3D *self)
{
    LrgChart3DPrivate *priv = lrg_chart3d_get_instance_private (self);

    priv->camera_yaw = 45.0f;
    priv->camera_pitch = 30.0f;
    priv->camera_distance = 5.0f;
    priv->field_of_view = 60.0f;

    priv->x_axis = NULL;
    priv->y_axis = NULL;
    priv->z_axis = NULL;

    priv->show_legend = TRUE;
    priv->show_axes = TRUE;
    priv->show_grid = TRUE;
    priv->enable_rotation = TRUE;
    priv->legend_position = LRG_CHART_LEGEND_RIGHT;

    priv->ranges_valid = FALSE;

    update_trig_cache (self);
}

/* ==========================================================================
 * Public API - Camera Control
 * ========================================================================== */

gfloat
lrg_chart3d_get_camera_yaw (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), 0.0f);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->camera_yaw;
}

void
lrg_chart3d_set_camera_yaw (LrgChart3D *self,
                            gfloat      yaw)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    if (priv->camera_yaw != yaw)
    {
        priv->camera_yaw = yaw;
        update_trig_cache (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA_YAW]);
    }
}

gfloat
lrg_chart3d_get_camera_pitch (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), 0.0f);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->camera_pitch;
}

void
lrg_chart3d_set_camera_pitch (LrgChart3D *self,
                              gfloat      pitch)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    pitch = CLAMP (pitch, -89.0f, 89.0f);

    if (priv->camera_pitch != pitch)
    {
        priv->camera_pitch = pitch;
        update_trig_cache (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA_PITCH]);
    }
}

gfloat
lrg_chart3d_get_camera_distance (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), 5.0f);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->camera_distance;
}

void
lrg_chart3d_set_camera_distance (LrgChart3D *self,
                                 gfloat      distance)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    distance = CLAMP (distance, 1.0f, 20.0f);

    if (priv->camera_distance != distance)
    {
        priv->camera_distance = distance;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA_DISTANCE]);
    }
}

gfloat
lrg_chart3d_get_field_of_view (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), 60.0f);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->field_of_view;
}

void
lrg_chart3d_set_field_of_view (LrgChart3D *self,
                               gfloat      fov)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    fov = CLAMP (fov, 10.0f, 120.0f);

    if (priv->field_of_view != fov)
    {
        priv->field_of_view = fov;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIELD_OF_VIEW]);
    }
}

void
lrg_chart3d_set_camera_angle (LrgChart3D *self,
                              gfloat      yaw,
                              gfloat      pitch)
{
    g_return_if_fail (LRG_IS_CHART3D (self));

    lrg_chart3d_set_camera_yaw (self, yaw);
    lrg_chart3d_set_camera_pitch (self, pitch);
}

/* ==========================================================================
 * Public API - Axis Configuration
 * ========================================================================== */

LrgChartAxisConfig *
lrg_chart3d_get_x_axis (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), NULL);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->x_axis;
}

void
lrg_chart3d_set_x_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    g_clear_pointer (&priv->x_axis, lrg_chart_axis_config_free);
    if (config != NULL)
        priv->x_axis = lrg_chart_axis_config_copy (config);

    priv->ranges_valid = FALSE;
}

LrgChartAxisConfig *
lrg_chart3d_get_y_axis (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), NULL);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->y_axis;
}

void
lrg_chart3d_set_y_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    g_clear_pointer (&priv->y_axis, lrg_chart_axis_config_free);
    if (config != NULL)
        priv->y_axis = lrg_chart_axis_config_copy (config);

    priv->ranges_valid = FALSE;
}

LrgChartAxisConfig *
lrg_chart3d_get_z_axis (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), NULL);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->z_axis;
}

void
lrg_chart3d_set_z_axis (LrgChart3D         *self,
                        LrgChartAxisConfig *config)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    g_clear_pointer (&priv->z_axis, lrg_chart_axis_config_free);
    if (config != NULL)
        priv->z_axis = lrg_chart_axis_config_copy (config);

    priv->ranges_valid = FALSE;
}

/* ==========================================================================
 * Public API - Data Ranges
 * ========================================================================== */

void
lrg_chart3d_get_x_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    if (out_min)
        *out_min = priv->x_min;
    if (out_max)
        *out_max = priv->x_max;
}

void
lrg_chart3d_get_y_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    if (out_min)
        *out_min = priv->y_min;
    if (out_max)
        *out_max = priv->y_max;
}

void
lrg_chart3d_get_z_range (LrgChart3D *self,
                         gdouble    *out_min,
                         gdouble    *out_max)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    if (out_min)
        *out_min = priv->z_min;
    if (out_max)
        *out_max = priv->z_max;
}

/* ==========================================================================
 * Public API - Display Options
 * ========================================================================== */

gboolean
lrg_chart3d_get_show_legend (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), FALSE);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->show_legend;
}

void
lrg_chart3d_set_show_legend (LrgChart3D *self,
                             gboolean    show)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    show = !!show;
    if (priv->show_legend != show)
    {
        priv->show_legend = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LEGEND]);
    }
}

gboolean
lrg_chart3d_get_show_axes (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), FALSE);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->show_axes;
}

void
lrg_chart3d_set_show_axes (LrgChart3D *self,
                           gboolean    show)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    show = !!show;
    if (priv->show_axes != show)
    {
        priv->show_axes = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_AXES]);
    }
}

gboolean
lrg_chart3d_get_show_grid (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), FALSE);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->show_grid;
}

void
lrg_chart3d_set_show_grid (LrgChart3D *self,
                           gboolean    show)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    show = !!show;
    if (priv->show_grid != show)
    {
        priv->show_grid = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_GRID]);
    }
}

gboolean
lrg_chart3d_get_enable_rotation (LrgChart3D *self)
{
    LrgChart3DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART3D (self), FALSE);

    priv = lrg_chart3d_get_instance_private (self);
    return priv->enable_rotation;
}

void
lrg_chart3d_set_enable_rotation (LrgChart3D *self,
                                 gboolean    enable)
{
    LrgChart3DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    enable = !!enable;
    if (priv->enable_rotation != enable)
    {
        priv->enable_rotation = enable;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_ROTATION]);
    }
}

/* ==========================================================================
 * Public API - Coordinate Conversion
 * ========================================================================== */

void
lrg_chart3d_data_to_screen (LrgChart3D *self,
                            gdouble     data_x,
                            gdouble     data_y,
                            gdouble     data_z,
                            gfloat     *screen_x,
                            gfloat     *screen_y)
{
    LrgChart3DPrivate *priv;
    gdouble nx, ny, nz;
    gdouble x_range, y_range, z_range;

    g_return_if_fail (LRG_IS_CHART3D (self));

    priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    /* Normalize data to -1..1 range */
    x_range = priv->x_max - priv->x_min;
    y_range = priv->y_max - priv->y_min;
    z_range = priv->z_max - priv->z_min;

    if (x_range <= 0.0) x_range = 1.0;
    if (y_range <= 0.0) y_range = 1.0;
    if (z_range <= 0.0) z_range = 1.0;

    nx = (data_x - priv->x_min) / x_range * 2.0 - 1.0;
    ny = (data_y - priv->y_min) / y_range * 2.0 - 1.0;
    nz = (data_z - priv->z_min) / z_range * 2.0 - 1.0;

    project_point (self, nx, ny, nz, screen_x, screen_y, NULL);
}

gfloat
lrg_chart3d_get_depth (LrgChart3D *self,
                       gdouble     data_x,
                       gdouble     data_y,
                       gdouble     data_z)
{
    LrgChart3DPrivate *priv;
    gdouble nx, ny, nz;
    gdouble x_range, y_range, z_range;
    gfloat sx, sy, depth;

    g_return_val_if_fail (LRG_IS_CHART3D (self), 0.0f);

    priv = lrg_chart3d_get_instance_private (self);

    if (!priv->ranges_valid)
        calculate_data_ranges (self);

    x_range = priv->x_max - priv->x_min;
    y_range = priv->y_max - priv->y_min;
    z_range = priv->z_max - priv->z_min;

    if (x_range <= 0.0) x_range = 1.0;
    if (y_range <= 0.0) y_range = 1.0;
    if (z_range <= 0.0) z_range = 1.0;

    nx = (data_x - priv->x_min) / x_range * 2.0 - 1.0;
    ny = (data_y - priv->y_min) / y_range * 2.0 - 1.0;
    nz = (data_z - priv->z_min) / z_range * 2.0 - 1.0;

    project_point (self, nx, ny, nz, &sx, &sy, &depth);

    return depth;
}

/* ==========================================================================
 * Public API - Drawing Helpers
 * ========================================================================== */

void
lrg_chart3d_draw_background (LrgChart3D *self)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->draw_background)
        klass->draw_background (self);
}

void
lrg_chart3d_draw_axes_3d (LrgChart3D *self)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->draw_axes_3d)
        klass->draw_axes_3d (self);
}

void
lrg_chart3d_draw_grid_3d (LrgChart3D *self)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->draw_grid_3d)
        klass->draw_grid_3d (self);
}

void
lrg_chart3d_draw_data_3d (LrgChart3D *self)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->draw_data_3d)
        klass->draw_data_3d (self);
}

void
lrg_chart3d_draw_legend (LrgChart3D *self)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->draw_legend)
        klass->draw_legend (self);
}

void
lrg_chart3d_rotate_view (LrgChart3D *self,
                         gfloat      delta_yaw,
                         gfloat      delta_pitch)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->rotate_view)
        klass->rotate_view (self, delta_yaw, delta_pitch);
}

void
lrg_chart3d_zoom_view (LrgChart3D *self,
                       gfloat      delta)
{
    LrgChart3DClass *klass;

    g_return_if_fail (LRG_IS_CHART3D (self));

    klass = LRG_CHART3D_GET_CLASS (self);
    if (klass->zoom_view)
        klass->zoom_view (self, delta);
}

/* ==========================================================================
 * Public API - 3D Primitive Drawing
 * ========================================================================== */

void
lrg_chart3d_draw_line_3d (LrgChart3D *self,
                          gdouble     x1,
                          gdouble     y1,
                          gdouble     z1,
                          gdouble     x2,
                          gdouble     y2,
                          gdouble     z2,
                          GrlColor   *color,
                          gfloat      width)
{
    gfloat sx1, sy1, sx2, sy2;

    g_return_if_fail (LRG_IS_CHART3D (self));

    /* Project both points using raw normalized coords */
    project_point (self, x1, y1, z1, &sx1, &sy1, NULL);
    project_point (self, x2, y2, z2, &sx2, &sy2, NULL);

    grl_draw_line_ex (
        &(GrlVector2){ sx1, sy1 },
        &(GrlVector2){ sx2, sy2 },
        width, color);
}

void
lrg_chart3d_draw_box_3d (LrgChart3D *self,
                         gdouble     x,
                         gdouble     y,
                         gdouble     z,
                         gdouble     width_x,
                         gdouble     height,
                         gdouble     depth_z,
                         GrlColor   *color)
{
    gdouble hw, hd;
    gfloat corners[8][2];  /* Screen coords for 8 corners */
    gint i;

    /* Corner coordinates (normalized) */
    typedef struct { gdouble x, y, z; } Vec3;
    Vec3 c[8];

    g_return_if_fail (LRG_IS_CHART3D (self));

    hw = width_x / 2.0;
    hd = depth_z / 2.0;

    /* Bottom face (y = base) */
    c[0] = (Vec3){ x - hw, y, z - hd };
    c[1] = (Vec3){ x + hw, y, z - hd };
    c[2] = (Vec3){ x + hw, y, z + hd };
    c[3] = (Vec3){ x - hw, y, z + hd };

    /* Top face (y = base + height) */
    c[4] = (Vec3){ x - hw, y + height, z - hd };
    c[5] = (Vec3){ x + hw, y + height, z - hd };
    c[6] = (Vec3){ x + hw, y + height, z + hd };
    c[7] = (Vec3){ x - hw, y + height, z + hd };

    /* Project all corners */
    for (i = 0; i < 8; i++)
        project_point (self, c[i].x, c[i].y, c[i].z,
                       &corners[i][0], &corners[i][1], NULL);

    /* Draw faces (simplified - draw as filled quads using triangles) */
    /* This is a simplified solid box rendering */

    /* Draw top face */
    grl_draw_triangle (
        &(GrlVector2){ corners[4][0], corners[4][1] },
        &(GrlVector2){ corners[5][0], corners[5][1] },
        &(GrlVector2){ corners[6][0], corners[6][1] },
        color);
    grl_draw_triangle (
        &(GrlVector2){ corners[4][0], corners[4][1] },
        &(GrlVector2){ corners[6][0], corners[6][1] },
        &(GrlVector2){ corners[7][0], corners[7][1] },
        color);

    /* Draw front face (z positive side) */
    grl_draw_triangle (
        &(GrlVector2){ corners[2][0], corners[2][1] },
        &(GrlVector2){ corners[6][0], corners[6][1] },
        &(GrlVector2){ corners[7][0], corners[7][1] },
        color);
    grl_draw_triangle (
        &(GrlVector2){ corners[2][0], corners[2][1] },
        &(GrlVector2){ corners[7][0], corners[7][1] },
        &(GrlVector2){ corners[3][0], corners[3][1] },
        color);

    /* Draw right face (x positive side) */
    grl_draw_triangle (
        &(GrlVector2){ corners[1][0], corners[1][1] },
        &(GrlVector2){ corners[5][0], corners[5][1] },
        &(GrlVector2){ corners[6][0], corners[6][1] },
        color);
    grl_draw_triangle (
        &(GrlVector2){ corners[1][0], corners[1][1] },
        &(GrlVector2){ corners[6][0], corners[6][1] },
        &(GrlVector2){ corners[2][0], corners[2][1] },
        color);
}

void
lrg_chart3d_draw_point_3d (LrgChart3D *self,
                           gdouble     x,
                           gdouble     y,
                           gdouble     z,
                           gfloat      radius,
                           GrlColor   *color)
{
    gfloat sx, sy;

    g_return_if_fail (LRG_IS_CHART3D (self));

    project_point (self, x, y, z, &sx, &sy, NULL);
    grl_draw_circle (sx, sy, radius, color);
}
