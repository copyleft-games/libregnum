/* lrg-chart2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart2d.h"
#include "lrg-chart-private.h"
#include <math.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    /* Axis configuration */
    LrgChartAxisConfig    *x_axis;
    LrgChartAxisConfig    *y_axis;

    /* Computed data ranges */
    gdouble                x_min;
    gdouble                x_max;
    gdouble                y_min;
    gdouble                y_max;

    /* Legend */
    gboolean               show_legend;
    LrgChartLegendPosition legend_position;
    gfloat                 legend_padding;
    gfloat                 legend_item_spacing;

    /* Font size for labels */
    gint                   font_size;
} LrgChart2DPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgChart2D, lrg_chart2d, LRG_TYPE_CHART)

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_X_AXIS,
    PROP_Y_AXIS,
    PROP_SHOW_LEGEND,
    PROP_LEGEND_POSITION,
    PROP_FONT_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gdouble
compute_nice_number (gdouble value, gboolean round_up)
{
    /*
     * Compute a "nice" number for axis range/step.
     * Nice numbers are 1, 2, 5, or 10 times a power of 10.
     */
    gdouble exponent;
    gdouble fraction;
    gdouble nice_fraction;

    if (value == 0.0)
        return 0.0;

    exponent = floor (log10 (fabs (value)));
    fraction = value / pow (10.0, exponent);

    if (round_up)
    {
        if (fraction <= 1.0)
            nice_fraction = 1.0;
        else if (fraction <= 2.0)
            nice_fraction = 2.0;
        else if (fraction <= 5.0)
            nice_fraction = 5.0;
        else
            nice_fraction = 10.0;
    }
    else
    {
        if (fraction < 1.5)
            nice_fraction = 1.0;
        else if (fraction < 3.0)
            nice_fraction = 2.0;
        else if (fraction < 7.0)
            nice_fraction = 5.0;
        else
            nice_fraction = 10.0;
    }

    return nice_fraction * pow (10.0, exponent);
}

static void
compute_axis_range (gdouble  data_min,
                    gdouble  data_max,
                    gdouble  config_min,
                    gdouble  config_max,
                    gdouble *out_min,
                    gdouble *out_max)
{
    gdouble range;

    /* Use config values if set, otherwise compute from data */
    if (!isnan (config_min))
        *out_min = config_min;
    else
        *out_min = data_min;

    if (!isnan (config_max))
        *out_max = config_max;
    else
        *out_max = data_max;

    /* Ensure some range */
    if (*out_min == *out_max)
    {
        if (*out_min == 0.0)
        {
            *out_min = -1.0;
            *out_max = 1.0;
        }
        else
        {
            *out_min -= fabs (*out_min) * 0.1;
            *out_max += fabs (*out_max) * 0.1;
        }
    }

    /* Make range nice */
    range = *out_max - *out_min;
    if (isnan (config_min))
        *out_min = floor (*out_min / compute_nice_number (range / 10.0, FALSE)) *
                   compute_nice_number (range / 10.0, FALSE);
    if (isnan (config_max))
        *out_max = ceil (*out_max / compute_nice_number (range / 10.0, FALSE)) *
                   compute_nice_number (range / 10.0, FALSE);
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static void
lrg_chart2d_real_draw_background (LrgChart2D *self)
{
    const GrlColor *bg_color;
    GrlRectangle bounds;
    gfloat x, y, width, height;

    bg_color = lrg_chart_get_background_color (LRG_CHART (self));

    x = lrg_widget_get_world_x (LRG_WIDGET (self));
    y = lrg_widget_get_world_y (LRG_WIDGET (self));
    width = lrg_widget_get_width (LRG_WIDGET (self));
    height = lrg_widget_get_height (LRG_WIDGET (self));

    /* Draw background rectangle */
    grl_draw_rectangle (x, y, width, height, bg_color);

    /* Draw content area with slightly different shade */
    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);
}

static void
lrg_chart2d_real_draw_axes (LrgChart2D *self)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    GrlRectangle bounds;
    const GrlColor *axis_color;
    const GrlColor *text_color;
    gdouble step;
    gdouble value;
    gchar label[64];
    gfloat sx, sy;
    const gchar *format;

    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);
    axis_color = lrg_chart_axis_config_get_color (priv->x_axis);
    text_color = lrg_chart_get_text_color (LRG_CHART (self));

    /* Draw X axis line */
    {
        g_autoptr(GrlVector2) x_start = grl_vector2_new (bounds.x, bounds.y + bounds.height);
        g_autoptr(GrlVector2) x_end = grl_vector2_new (bounds.x + bounds.width, bounds.y + bounds.height);
        grl_draw_line_ex (x_start, x_end, 1.0f, axis_color);
    }

    /* Draw Y axis line */
    {
        g_autoptr(GrlVector2) y_start = grl_vector2_new (bounds.x, bounds.y);
        g_autoptr(GrlVector2) y_end = grl_vector2_new (bounds.x, bounds.y + bounds.height);
        grl_draw_line_ex (y_start, y_end, 1.0f, axis_color);
    }

    /* X axis labels */
    step = lrg_chart_axis_config_get_step (priv->x_axis);
    if (isnan (step))
        step = compute_nice_number ((priv->x_max - priv->x_min) / 5.0, FALSE);

    format = lrg_chart_axis_config_get_format (priv->x_axis);
    if (format == NULL)
        format = "%.1f";

    for (value = priv->x_min; value <= priv->x_max; value += step)
    {
        g_autoptr(GrlVector2) tick_start = NULL;
        g_autoptr(GrlVector2) tick_end = NULL;

        lrg_chart2d_data_to_screen (self, value, priv->y_min, &sx, &sy);

        /* Tick mark */
        tick_start = grl_vector2_new (sx, sy);
        tick_end = grl_vector2_new (sx, sy + 5.0f);
        grl_draw_line_ex (tick_start, tick_end, 1.0f, axis_color);

        /* Label - use fixed format as fallback was already set above */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        g_snprintf (label, sizeof (label), format, value);
#pragma GCC diagnostic pop
        grl_draw_text (label, (gint)sx - 10, (gint)sy + 8, priv->font_size, text_color);
    }

    /* Y axis labels */
    step = lrg_chart_axis_config_get_step (priv->y_axis);
    if (isnan (step))
        step = compute_nice_number ((priv->y_max - priv->y_min) / 5.0, FALSE);

    format = lrg_chart_axis_config_get_format (priv->y_axis);
    if (format == NULL)
        format = "%.1f";

    for (value = priv->y_min; value <= priv->y_max; value += step)
    {
        g_autoptr(GrlVector2) tick_start = NULL;
        g_autoptr(GrlVector2) tick_end = NULL;

        lrg_chart2d_data_to_screen (self, priv->x_min, value, &sx, &sy);

        /* Tick mark */
        tick_start = grl_vector2_new (sx - 5.0f, sy);
        tick_end = grl_vector2_new (sx, sy);
        grl_draw_line_ex (tick_start, tick_end, 1.0f, axis_color);

        /* Label */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        g_snprintf (label, sizeof (label), format, value);
#pragma GCC diagnostic pop
        grl_draw_text (label, (gint)sx - 40, (gint)sy - 5, priv->font_size, text_color);
    }

    /* Axis titles */
    if (lrg_chart_axis_config_get_title (priv->x_axis) != NULL)
    {
        const gchar *title = lrg_chart_axis_config_get_title (priv->x_axis);
        gint text_width = grl_measure_text (title, priv->font_size);
        gfloat tx = bounds.x + (bounds.width - text_width) / 2.0f;
        gfloat ty = bounds.y + bounds.height + 30.0f;
        grl_draw_text (title, (gint)tx, (gint)ty, priv->font_size, text_color);
    }

    if (lrg_chart_axis_config_get_title (priv->y_axis) != NULL)
    {
        /* Y axis title would need rotation - simplified here */
        const gchar *title = lrg_chart_axis_config_get_title (priv->y_axis);
        gfloat tx = bounds.x - 45.0f;
        gfloat ty = bounds.y + bounds.height / 2.0f;
        grl_draw_text (title, (gint)tx, (gint)ty, priv->font_size, text_color);
    }
}

static void
lrg_chart2d_real_draw_grid (LrgChart2D *self)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    GrlRectangle bounds;
    const GrlColor *grid_color;
    gdouble step;
    gdouble value;
    gfloat sx, sy;
    gfloat sx2, sy2;

    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);

    /* X axis grid */
    if (lrg_chart_axis_config_get_show_grid (priv->x_axis))
    {
        grid_color = lrg_chart_axis_config_get_grid_color (priv->x_axis);
        step = lrg_chart_axis_config_get_step (priv->x_axis);
        if (isnan (step))
            step = compute_nice_number ((priv->x_max - priv->x_min) / 5.0, FALSE);

        for (value = priv->x_min + step; value < priv->x_max; value += step)
        {
            g_autoptr(GrlVector2) line_start = NULL;
            g_autoptr(GrlVector2) line_end = NULL;

            lrg_chart2d_data_to_screen (self, value, priv->y_min, &sx, &sy);
            lrg_chart2d_data_to_screen (self, value, priv->y_max, &sx2, &sy2);

            line_start = grl_vector2_new (sx, sy);
            line_end = grl_vector2_new (sx2, sy2);
            grl_draw_line_ex (line_start, line_end, 1.0f, grid_color);
        }
    }

    /* Y axis grid */
    if (lrg_chart_axis_config_get_show_grid (priv->y_axis))
    {
        grid_color = lrg_chart_axis_config_get_grid_color (priv->y_axis);
        step = lrg_chart_axis_config_get_step (priv->y_axis);
        if (isnan (step))
            step = compute_nice_number ((priv->y_max - priv->y_min) / 5.0, FALSE);

        for (value = priv->y_min + step; value < priv->y_max; value += step)
        {
            g_autoptr(GrlVector2) line_start = NULL;
            g_autoptr(GrlVector2) line_end = NULL;

            lrg_chart2d_data_to_screen (self, priv->x_min, value, &sx, &sy);
            lrg_chart2d_data_to_screen (self, priv->x_max, value, &sx2, &sy2);

            line_start = grl_vector2_new (sx, sy);
            line_end = grl_vector2_new (sx2, sy2);
            grl_draw_line_ex (line_start, line_end, 1.0f, grid_color);
        }
    }
}

static void
lrg_chart2d_real_draw_data (LrgChart2D *self)
{
    /* Abstract - subclasses must implement */
}

static void
lrg_chart2d_real_draw_legend (LrgChart2D *self)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    const GrlColor *text_color;
    GrlRectangle bounds;
    guint series_count;
    guint i;
    gfloat legend_x, legend_y;
    gfloat item_height;
    gfloat swatch_size;

    if (!priv->show_legend)
        return;

    text_color = lrg_chart_get_text_color (LRG_CHART (self));
    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);
    series_count = lrg_chart_get_series_count (LRG_CHART (self));

    if (series_count == 0)
        return;

    swatch_size = 12.0f;
    item_height = swatch_size + priv->legend_item_spacing;

    /* Position legend based on setting */
    switch (priv->legend_position)
    {
    case LRG_CHART_LEGEND_TOP:
        legend_x = bounds.x + bounds.width / 2.0f - 50.0f;
        legend_y = bounds.y - 25.0f;
        break;
    case LRG_CHART_LEGEND_BOTTOM:
        legend_x = bounds.x + bounds.width / 2.0f - 50.0f;
        legend_y = bounds.y + bounds.height + 20.0f;
        break;
    case LRG_CHART_LEGEND_LEFT:
        legend_x = bounds.x - 80.0f;
        legend_y = bounds.y + bounds.height / 2.0f - (series_count * item_height) / 2.0f;
        break;
    case LRG_CHART_LEGEND_RIGHT:
    default:
        legend_x = bounds.x + bounds.width + 10.0f;
        legend_y = bounds.y + 10.0f;
        break;
    }

    /* Draw each legend item */
    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (LRG_CHART (self), i);
        const GrlColor *series_color;
        const gchar *name;

        if (!lrg_chart_data_series_get_show_in_legend (series))
            continue;

        series_color = lrg_chart_data_series_get_color (series);
        name = lrg_chart_data_series_get_name (series);

        /* Color swatch */
        grl_draw_rectangle (legend_x, legend_y, swatch_size, swatch_size, series_color);

        /* Series name */
        if (name != NULL)
        {
            grl_draw_text (name,
                          (gint)(legend_x + swatch_size + 5.0f),
                          (gint)legend_y,
                          priv->font_size,
                          text_color);
        }

        legend_y += item_height;
    }
}

static void
lrg_chart2d_real_data_to_screen (LrgChart2D *self,
                                  gdouble     data_x,
                                  gdouble     data_y,
                                  gfloat     *screen_x,
                                  gfloat     *screen_y)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    GrlRectangle bounds;
    gdouble x_range;
    gdouble y_range;

    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);

    x_range = priv->x_max - priv->x_min;
    y_range = priv->y_max - priv->y_min;

    /* Linear mapping from data to screen coordinates */
    if (x_range != 0.0 && screen_x != NULL)
        *screen_x = bounds.x + ((data_x - priv->x_min) / x_range) * bounds.width;
    else if (screen_x != NULL)
        *screen_x = bounds.x + bounds.width / 2.0f;

    if (y_range != 0.0 && screen_y != NULL)
        *screen_y = bounds.y + bounds.height - ((data_y - priv->y_min) / y_range) * bounds.height;
    else if (screen_y != NULL)
        *screen_y = bounds.y + bounds.height / 2.0f;
}

static void
lrg_chart2d_real_screen_to_data (LrgChart2D *self,
                                  gfloat      screen_x,
                                  gfloat      screen_y,
                                  gdouble    *data_x,
                                  gdouble    *data_y)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    GrlRectangle bounds;
    gdouble x_range;
    gdouble y_range;

    lrg_chart_get_content_bounds (LRG_CHART (self), &bounds);

    x_range = priv->x_max - priv->x_min;
    y_range = priv->y_max - priv->y_min;

    /* Linear mapping from screen to data coordinates */
    if (bounds.width != 0.0f && data_x != NULL)
        *data_x = priv->x_min + ((screen_x - bounds.x) / bounds.width) * x_range;
    else if (data_x != NULL)
        *data_x = priv->x_min;

    if (bounds.height != 0.0f && data_y != NULL)
        *data_y = priv->y_min + ((bounds.y + bounds.height - screen_y) / bounds.height) * y_range;
    else if (data_y != NULL)
        *data_y = priv->y_min;
}

/* ==========================================================================
 * LrgChart Virtual Method Overrides
 * ========================================================================== */

static void
lrg_chart2d_update_data (LrgChart *chart)
{
    LrgChart2D *self = LRG_CHART2D (chart);
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
    guint series_count;
    guint i;
    gdouble data_x_min = G_MAXDOUBLE;
    gdouble data_x_max = -G_MAXDOUBLE;
    gdouble data_y_min = G_MAXDOUBLE;
    gdouble data_y_max = -G_MAXDOUBLE;

    /* Chain up */
    LRG_CHART_CLASS (lrg_chart2d_parent_class)->update_data (chart);

    /* Compute data ranges from all visible series */
    series_count = lrg_chart_get_series_count (chart);

    for (i = 0; i < series_count; i++)
    {
        LrgChartDataSeries *series = lrg_chart_get_series (chart, i);
        gdouble sx_min, sx_max, sy_min, sy_max;

        if (!lrg_chart_data_series_get_visible (series))
            continue;

        lrg_chart_data_series_get_x_range (series, &sx_min, &sx_max);
        lrg_chart_data_series_get_y_range (series, &sy_min, &sy_max);

        if (sx_min < data_x_min) data_x_min = sx_min;
        if (sx_max > data_x_max) data_x_max = sx_max;
        if (sy_min < data_y_min) data_y_min = sy_min;
        if (sy_max > data_y_max) data_y_max = sy_max;
    }

    /* Handle empty data */
    if (data_x_min == G_MAXDOUBLE)
    {
        data_x_min = 0.0;
        data_x_max = 1.0;
    }
    if (data_y_min == G_MAXDOUBLE)
    {
        data_y_min = 0.0;
        data_y_max = 1.0;
    }

    /* Compute axis ranges */
    compute_axis_range (data_x_min, data_x_max,
                        lrg_chart_axis_config_get_min (priv->x_axis),
                        lrg_chart_axis_config_get_max (priv->x_axis),
                        &priv->x_min, &priv->x_max);

    compute_axis_range (data_y_min, data_y_max,
                        lrg_chart_axis_config_get_min (priv->y_axis),
                        lrg_chart_axis_config_get_max (priv->y_axis),
                        &priv->y_min, &priv->y_max);
}

/* ==========================================================================
 * LrgWidget Virtual Method Overrides
 * ========================================================================== */

static void
lrg_chart2d_draw (LrgWidget *widget)
{
    LrgChart2D *self = LRG_CHART2D (widget);
    LrgChart2DClass *klass = LRG_CHART2D_GET_CLASS (self);

    /* Draw in order: background, grid, data, axes, legend, title */
    if (klass->draw_background != NULL)
        klass->draw_background (self);

    if (klass->draw_grid != NULL)
        klass->draw_grid (self);

    if (klass->draw_data != NULL)
        klass->draw_data (self);

    if (klass->draw_axes != NULL)
        klass->draw_axes (self);

    if (klass->draw_legend != NULL)
        klass->draw_legend (self);

    /* Draw title */
    {
        const gchar *title = lrg_chart_get_title (LRG_CHART (self));
        if (title != NULL)
        {
            LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);
            const GrlColor *text_color = lrg_chart_get_text_color (LRG_CHART (self));
            gfloat x = lrg_widget_get_world_x (widget);
            gfloat width = lrg_widget_get_width (widget);
            gfloat y = lrg_widget_get_world_y (widget);
            gint text_width = grl_measure_text (title, priv->font_size + 2);
            gfloat tx = x + (width - text_width) / 2.0f;
            grl_draw_text (title, (gint)tx, (gint)y + 10, priv->font_size + 2, text_color);
        }
    }
}

static void
lrg_chart2d_measure (LrgWidget *widget,
                     gfloat    *preferred_width,
                     gfloat    *preferred_height)
{
    /* Default preferred size */
    if (preferred_width != NULL)
        *preferred_width = 400.0f;
    if (preferred_height != NULL)
        *preferred_height = 300.0f;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart2d_finalize (GObject *object)
{
    LrgChart2D *self = LRG_CHART2D (object);
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);

    lrg_chart_axis_config_free (priv->x_axis);
    lrg_chart_axis_config_free (priv->y_axis);

    G_OBJECT_CLASS (lrg_chart2d_parent_class)->finalize (object);
}

static void
lrg_chart2d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgChart2D *self = LRG_CHART2D (object);
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X_AXIS:
        g_value_set_boxed (value, priv->x_axis);
        break;
    case PROP_Y_AXIS:
        g_value_set_boxed (value, priv->y_axis);
        break;
    case PROP_SHOW_LEGEND:
        g_value_set_boolean (value, priv->show_legend);
        break;
    case PROP_LEGEND_POSITION:
        g_value_set_enum (value, priv->legend_position);
        break;
    case PROP_FONT_SIZE:
        g_value_set_int (value, priv->font_size);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart2d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgChart2D *self = LRG_CHART2D (object);
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X_AXIS:
        lrg_chart2d_set_x_axis (self, g_value_get_boxed (value));
        break;
    case PROP_Y_AXIS:
        lrg_chart2d_set_y_axis (self, g_value_get_boxed (value));
        break;
    case PROP_SHOW_LEGEND:
        lrg_chart2d_set_show_legend (self, g_value_get_boolean (value));
        break;
    case PROP_LEGEND_POSITION:
        lrg_chart2d_set_legend_position (self, g_value_get_enum (value));
        break;
    case PROP_FONT_SIZE:
        priv->font_size = g_value_get_int (value);
        lrg_chart_mark_layout_dirty (LRG_CHART (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart2d_class_init (LrgChart2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);
    LrgChartClass *chart_class = LRG_CHART_CLASS (klass);

    object_class->finalize = lrg_chart2d_finalize;
    object_class->get_property = lrg_chart2d_get_property;
    object_class->set_property = lrg_chart2d_set_property;

    /* Override widget methods */
    widget_class->draw = lrg_chart2d_draw;
    widget_class->measure = lrg_chart2d_measure;

    /* Override chart methods */
    chart_class->update_data = lrg_chart2d_update_data;

    /* Default virtual method implementations */
    klass->draw_background = lrg_chart2d_real_draw_background;
    klass->draw_axes = lrg_chart2d_real_draw_axes;
    klass->draw_grid = lrg_chart2d_real_draw_grid;
    klass->draw_data = lrg_chart2d_real_draw_data;
    klass->draw_legend = lrg_chart2d_real_draw_legend;
    klass->data_to_screen = lrg_chart2d_real_data_to_screen;
    klass->screen_to_data = lrg_chart2d_real_screen_to_data;

    /* Properties */
    properties[PROP_X_AXIS] =
        g_param_spec_boxed ("x-axis",
                            "X Axis",
                            "X axis configuration",
                            LRG_TYPE_CHART_AXIS_CONFIG,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_Y_AXIS] =
        g_param_spec_boxed ("y-axis",
                            "Y Axis",
                            "Y axis configuration",
                            LRG_TYPE_CHART_AXIS_CONFIG,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHOW_LEGEND] =
        g_param_spec_boolean ("show-legend",
                              "Show Legend",
                              "Whether to show the legend",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_LEGEND_POSITION] =
        g_param_spec_enum ("legend-position",
                           "Legend Position",
                           "Legend position",
                           LRG_TYPE_CHART_LEGEND_POSITION,
                           LRG_CHART_LEGEND_RIGHT,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    properties[PROP_FONT_SIZE] =
        g_param_spec_int ("font-size",
                          "Font Size",
                          "Font size for labels",
                          6, 72, 10,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_chart2d_init (LrgChart2D *self)
{
    LrgChart2DPrivate *priv = lrg_chart2d_get_instance_private (self);

    priv->x_axis = lrg_chart_axis_config_new ();
    priv->y_axis = lrg_chart_axis_config_new ();

    priv->x_min = 0.0;
    priv->x_max = 1.0;
    priv->y_min = 0.0;
    priv->y_max = 1.0;

    priv->show_legend = TRUE;
    priv->legend_position = LRG_CHART_LEGEND_RIGHT;
    priv->legend_padding = 10.0f;
    priv->legend_item_spacing = 4.0f;

    priv->font_size = 10;
}

/* ==========================================================================
 * Axis Configuration
 * ========================================================================== */

LrgChartAxisConfig *
lrg_chart2d_get_x_axis (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), NULL);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->x_axis;
}

void
lrg_chart2d_set_x_axis (LrgChart2D         *self,
                        LrgChartAxisConfig *config)
{
    LrgChart2DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART2D (self));
    g_return_if_fail (config != NULL);

    priv = lrg_chart2d_get_instance_private (self);

    lrg_chart_axis_config_free (priv->x_axis);
    priv->x_axis = lrg_chart_axis_config_copy (config);

    lrg_chart_mark_layout_dirty (LRG_CHART (self));
    lrg_chart_update_data (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X_AXIS]);
}

LrgChartAxisConfig *
lrg_chart2d_get_y_axis (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), NULL);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->y_axis;
}

void
lrg_chart2d_set_y_axis (LrgChart2D         *self,
                        LrgChartAxisConfig *config)
{
    LrgChart2DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART2D (self));
    g_return_if_fail (config != NULL);

    priv = lrg_chart2d_get_instance_private (self);

    lrg_chart_axis_config_free (priv->y_axis);
    priv->y_axis = lrg_chart_axis_config_copy (config);

    lrg_chart_mark_layout_dirty (LRG_CHART (self));
    lrg_chart_update_data (LRG_CHART (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y_AXIS]);
}

/* ==========================================================================
 * Data Ranges
 * ========================================================================== */

gdouble
lrg_chart2d_get_x_min (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), 0.0);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->x_min;
}

gdouble
lrg_chart2d_get_x_max (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), 0.0);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->x_max;
}

gdouble
lrg_chart2d_get_y_min (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), 0.0);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->y_min;
}

gdouble
lrg_chart2d_get_y_max (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), 0.0);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->y_max;
}

/* ==========================================================================
 * Legend
 * ========================================================================== */

gboolean
lrg_chart2d_get_show_legend (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), FALSE);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->show_legend;
}

void
lrg_chart2d_set_show_legend (LrgChart2D *self,
                             gboolean    show)
{
    LrgChart2DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART2D (self));

    priv = lrg_chart2d_get_instance_private (self);

    show = !!show;

    if (priv->show_legend == show)
        return;

    priv->show_legend = show;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_LEGEND]);
}

LrgChartLegendPosition
lrg_chart2d_get_legend_position (LrgChart2D *self)
{
    LrgChart2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART2D (self), LRG_CHART_LEGEND_RIGHT);

    priv = lrg_chart2d_get_instance_private (self);
    return priv->legend_position;
}

void
lrg_chart2d_set_legend_position (LrgChart2D             *self,
                                 LrgChartLegendPosition  position)
{
    LrgChart2DPrivate *priv;

    g_return_if_fail (LRG_IS_CHART2D (self));

    priv = lrg_chart2d_get_instance_private (self);

    if (priv->legend_position == position)
        return;

    priv->legend_position = position;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEGEND_POSITION]);
}

/* ==========================================================================
 * Coordinate Conversion
 * ========================================================================== */

void
lrg_chart2d_data_to_screen (LrgChart2D *self,
                            gdouble     data_x,
                            gdouble     data_y,
                            gfloat     *screen_x,
                            gfloat     *screen_y)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->data_to_screen != NULL)
        klass->data_to_screen (self, data_x, data_y, screen_x, screen_y);
}

void
lrg_chart2d_screen_to_data (LrgChart2D *self,
                            gfloat      screen_x,
                            gfloat      screen_y,
                            gdouble    *data_x,
                            gdouble    *data_y)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->screen_to_data != NULL)
        klass->screen_to_data (self, screen_x, screen_y, data_x, data_y);
}

/* ==========================================================================
 * Drawing Helper Wrappers
 * ========================================================================== */

void
lrg_chart2d_draw_background (LrgChart2D *self)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->draw_background != NULL)
        klass->draw_background (self);
}

void
lrg_chart2d_draw_axes (LrgChart2D *self)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->draw_axes != NULL)
        klass->draw_axes (self);
}

void
lrg_chart2d_draw_grid (LrgChart2D *self)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->draw_grid != NULL)
        klass->draw_grid (self);
}

void
lrg_chart2d_draw_data (LrgChart2D *self)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->draw_data != NULL)
        klass->draw_data (self);
}

void
lrg_chart2d_draw_legend (LrgChart2D *self)
{
    LrgChart2DClass *klass;

    g_return_if_fail (LRG_IS_CHART2D (self));

    klass = LRG_CHART2D_GET_CLASS (self);

    if (klass->draw_legend != NULL)
        klass->draw_legend (self);
}
