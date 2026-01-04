/* lrg-chart-tooltip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-tooltip.h"
#include <graylib.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CHART
#include "../lrg-log.h"

typedef struct
{
    gboolean   visible;

    GrlColor  *background_color;
    GrlColor  *text_color;
    GrlColor  *border_color;

    gfloat     padding;
    gfloat     corner_radius;
    gfloat     border_width;

    gint       font_size;

    gboolean   show_series_name;
    gchar     *value_format;
} LrgChartTooltipPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgChartTooltip, lrg_chart_tooltip, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VISIBLE,
    PROP_BACKGROUND_COLOR,
    PROP_TEXT_COLOR,
    PROP_BORDER_COLOR,
    PROP_PADDING,
    PROP_CORNER_RADIUS,
    PROP_BORDER_WIDTH,
    PROP_FONT_SIZE,
    PROP_SHOW_SERIES_NAME,
    PROP_VALUE_FORMAT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gchar *
lrg_chart_tooltip_real_format_content (LrgChartTooltip       *self,
                                       LrgChartDataSeries    *series,
                                       const LrgChartHitInfo *hit)
{
    LrgChartTooltipPrivate *priv;
    GString *str;
    LrgChartDataPoint *point;
    const gchar *series_name;
    const gchar *point_label;

    priv = lrg_chart_tooltip_get_instance_private (self);
    str = g_string_new (NULL);

    if (hit == NULL || lrg_chart_hit_info_get_point_index (hit) < 0)
    {
        return g_string_free (str, FALSE);
    }

    /* Add series name if enabled */
    if (priv->show_series_name && series != NULL)
    {
        series_name = lrg_chart_data_series_get_name (series);
        if (series_name != NULL && series_name[0] != '\0')
        {
            g_string_append (str, series_name);
            g_string_append_c (str, '\n');
        }
    }

    /* Add point label if present */
    point = (LrgChartDataPoint *)lrg_chart_hit_info_get_data_point (hit);
    if (point != NULL)
    {
        gdouble y_value;

        point_label = lrg_chart_data_point_get_label (point);
        if (point_label != NULL && point_label[0] != '\0')
        {
            g_string_append (str, point_label);
            g_string_append (str, ": ");
        }

        /* Add Y value (primary value for most charts) */
        y_value = lrg_chart_data_point_get_y (point);
        if (priv->value_format != NULL)
        {
            /* User-provided format string - intentionally variable */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            g_string_append_printf (str, priv->value_format, y_value);
#pragma GCC diagnostic pop
        }
        else
        {
            g_string_append_printf (str, "%.2f", y_value);
        }
    }

    return g_string_free (str, FALSE);
}

static void
lrg_chart_tooltip_real_draw (LrgChartTooltip *self,
                             gfloat           x,
                             gfloat           y,
                             const gchar     *content)
{
    LrgChartTooltipPrivate *priv;
    gfloat width;
    gfloat height;
    GrlRectangle *rect;

    if (content == NULL || content[0] == '\0')
    {
        return;
    }

    priv = lrg_chart_tooltip_get_instance_private (self);

    /* Get tooltip size */
    lrg_chart_tooltip_get_size (self, content, &width, &height);

    /* Draw background with border */
    rect = grl_rectangle_new (x, y, width, height);

    if (priv->corner_radius > 0.0f)
    {
        grl_draw_rectangle_rounded (rect, priv->corner_radius, 0,
                                    priv->background_color);

        if (priv->border_width > 0.0f)
        {
            grl_draw_rectangle_rounded_lines_ex (rect, priv->corner_radius, 0,
                                                 priv->border_width,
                                                 priv->border_color);
        }
    }
    else
    {
        grl_draw_rectangle_rec (rect, priv->background_color);

        if (priv->border_width > 0.0f)
        {
            grl_draw_rectangle_lines_ex (rect, priv->border_width,
                                         priv->border_color);
        }
    }

    grl_rectangle_free (rect);

    /* Draw text */
    grl_draw_text (content,
                   (gint)(x + priv->padding),
                   (gint)(y + priv->padding),
                   priv->font_size,
                   priv->text_color);
}

static void
lrg_chart_tooltip_real_get_size (LrgChartTooltip *self,
                                 const gchar     *content,
                                 gfloat          *out_width,
                                 gfloat          *out_height)
{
    LrgChartTooltipPrivate *priv;
    gint text_width;
    gint line_count;
    const gchar *p;

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (content == NULL || content[0] == '\0')
    {
        if (out_width != NULL)
            *out_width = 0.0f;
        if (out_height != NULL)
            *out_height = 0.0f;
        return;
    }

    /* Measure text width */
    text_width = grl_measure_text (content, priv->font_size);

    /* Count lines for height calculation */
    line_count = 1;
    for (p = content; *p != '\0'; p++)
    {
        if (*p == '\n')
            line_count++;
    }

    if (out_width != NULL)
    {
        *out_width = (gfloat)text_width + (priv->padding * 2.0f);
    }

    if (out_height != NULL)
    {
        *out_height = (gfloat)(priv->font_size * line_count) +
                      (priv->padding * 2.0f);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_chart_tooltip_finalize (GObject *object)
{
    LrgChartTooltip *self = LRG_CHART_TOOLTIP (object);
    LrgChartTooltipPrivate *priv = lrg_chart_tooltip_get_instance_private (self);

    g_clear_pointer (&priv->background_color, grl_color_free);
    g_clear_pointer (&priv->text_color, grl_color_free);
    g_clear_pointer (&priv->border_color, grl_color_free);
    g_clear_pointer (&priv->value_format, g_free);

    G_OBJECT_CLASS (lrg_chart_tooltip_parent_class)->finalize (object);
}

static void
lrg_chart_tooltip_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgChartTooltip *self = LRG_CHART_TOOLTIP (object);
    LrgChartTooltipPrivate *priv = lrg_chart_tooltip_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_VISIBLE:
        g_value_set_boolean (value, priv->visible);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, priv->background_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, priv->text_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, priv->border_color);
        break;
    case PROP_PADDING:
        g_value_set_float (value, priv->padding);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, priv->corner_radius);
        break;
    case PROP_BORDER_WIDTH:
        g_value_set_float (value, priv->border_width);
        break;
    case PROP_FONT_SIZE:
        g_value_set_int (value, priv->font_size);
        break;
    case PROP_SHOW_SERIES_NAME:
        g_value_set_boolean (value, priv->show_series_name);
        break;
    case PROP_VALUE_FORMAT:
        g_value_set_string (value, priv->value_format);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_tooltip_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgChartTooltip *self = LRG_CHART_TOOLTIP (object);

    switch (prop_id)
    {
    case PROP_VISIBLE:
        lrg_chart_tooltip_set_visible (self, g_value_get_boolean (value));
        break;
    case PROP_BACKGROUND_COLOR:
        lrg_chart_tooltip_set_background_color (self, g_value_get_boxed (value));
        break;
    case PROP_TEXT_COLOR:
        lrg_chart_tooltip_set_text_color (self, g_value_get_boxed (value));
        break;
    case PROP_BORDER_COLOR:
        lrg_chart_tooltip_set_border_color (self, g_value_get_boxed (value));
        break;
    case PROP_PADDING:
        lrg_chart_tooltip_set_padding (self, g_value_get_float (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_chart_tooltip_set_corner_radius (self, g_value_get_float (value));
        break;
    case PROP_BORDER_WIDTH:
        lrg_chart_tooltip_set_border_width (self, g_value_get_float (value));
        break;
    case PROP_FONT_SIZE:
        lrg_chart_tooltip_set_font_size (self, g_value_get_int (value));
        break;
    case PROP_SHOW_SERIES_NAME:
        lrg_chart_tooltip_set_show_series_name (self, g_value_get_boolean (value));
        break;
    case PROP_VALUE_FORMAT:
        lrg_chart_tooltip_set_value_format (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_chart_tooltip_class_init (LrgChartTooltipClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_chart_tooltip_finalize;
    object_class->get_property = lrg_chart_tooltip_get_property;
    object_class->set_property = lrg_chart_tooltip_set_property;

    /* Virtual methods */
    klass->format_content = lrg_chart_tooltip_real_format_content;
    klass->draw = lrg_chart_tooltip_real_draw;
    klass->get_size = lrg_chart_tooltip_real_get_size;

    /**
     * LrgChartTooltip:visible:
     *
     * Whether the tooltip is currently visible.
     *
     * Since: 1.0
     */
    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:background-color:
     *
     * The tooltip background color.
     *
     * Since: 1.0
     */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:text-color:
     *
     * The tooltip text color.
     *
     * Since: 1.0
     */
    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:border-color:
     *
     * The tooltip border color.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:padding:
     *
     * Internal padding in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_PADDING] =
        g_param_spec_float ("padding", NULL, NULL,
                            0.0f, 100.0f, 8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:corner-radius:
     *
     * Corner radius for rounded corners.
     *
     * Since: 1.0
     */
    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius", NULL, NULL,
                            0.0f, 50.0f, 4.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:border-width:
     *
     * Border line width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_WIDTH] =
        g_param_spec_float ("border-width", NULL, NULL,
                            0.0f, 10.0f, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:font-size:
     *
     * Font size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_int ("font-size", NULL, NULL,
                          6, 72, 14,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:show-series-name:
     *
     * Whether to include the series name in the tooltip.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_SERIES_NAME] =
        g_param_spec_boolean ("show-series-name", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgChartTooltip:value-format:
     *
     * Printf format string for values.
     *
     * Since: 1.0
     */
    properties[PROP_VALUE_FORMAT] =
        g_param_spec_string ("value-format", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_chart_tooltip_init (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv = lrg_chart_tooltip_get_instance_private (self);

    priv->visible = FALSE;
    priv->background_color = grl_color_new (40, 40, 40, 230);
    priv->text_color = grl_color_new (255, 255, 255, 255);
    priv->border_color = grl_color_new (100, 100, 100, 255);
    priv->padding = 8.0f;
    priv->corner_radius = 4.0f;
    priv->border_width = 1.0f;
    priv->font_size = 14;
    priv->show_series_name = TRUE;
    priv->value_format = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgChartTooltip *
lrg_chart_tooltip_new (void)
{
    return g_object_new (LRG_TYPE_CHART_TOOLTIP, NULL);
}

gboolean
lrg_chart_tooltip_get_visible (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), FALSE);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->visible;
}

void
lrg_chart_tooltip_set_visible (LrgChartTooltip *self,
                               gboolean         visible)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    visible = !!visible;
    if (priv->visible != visible)
    {
        priv->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
    }
}

GrlColor *
lrg_chart_tooltip_get_background_color (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), NULL);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->background_color;
}

void
lrg_chart_tooltip_set_background_color (LrgChartTooltip *self,
                                        GrlColor        *color)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    g_clear_pointer (&priv->background_color, grl_color_free);
    if (color != NULL)
    {
        priv->background_color = grl_color_copy (color);
    }
    else
    {
        priv->background_color = grl_color_new (40, 40, 40, 230);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

GrlColor *
lrg_chart_tooltip_get_text_color (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), NULL);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->text_color;
}

void
lrg_chart_tooltip_set_text_color (LrgChartTooltip *self,
                                  GrlColor        *color)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    g_clear_pointer (&priv->text_color, grl_color_free);
    if (color != NULL)
    {
        priv->text_color = grl_color_copy (color);
    }
    else
    {
        priv->text_color = grl_color_new (255, 255, 255, 255);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

GrlColor *
lrg_chart_tooltip_get_border_color (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), NULL);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->border_color;
}

void
lrg_chart_tooltip_set_border_color (LrgChartTooltip *self,
                                    GrlColor        *color)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    g_clear_pointer (&priv->border_color, grl_color_free);
    if (color != NULL)
    {
        priv->border_color = grl_color_copy (color);
    }
    else
    {
        priv->border_color = grl_color_new (100, 100, 100, 255);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

gfloat
lrg_chart_tooltip_get_padding (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), 0.0f);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->padding;
}

void
lrg_chart_tooltip_set_padding (LrgChartTooltip *self,
                               gfloat           padding)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (priv->padding != padding)
    {
        priv->padding = padding;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
    }
}

gfloat
lrg_chart_tooltip_get_corner_radius (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), 0.0f);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->corner_radius;
}

void
lrg_chart_tooltip_set_corner_radius (LrgChartTooltip *self,
                                     gfloat           radius)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (priv->corner_radius != radius)
    {
        priv->corner_radius = radius;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
    }
}

gfloat
lrg_chart_tooltip_get_border_width (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), 0.0f);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->border_width;
}

void
lrg_chart_tooltip_set_border_width (LrgChartTooltip *self,
                                    gfloat           width)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (priv->border_width != width)
    {
        priv->border_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
    }
}

gint
lrg_chart_tooltip_get_font_size (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), 14);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->font_size;
}

void
lrg_chart_tooltip_set_font_size (LrgChartTooltip *self,
                                 gint             size)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (priv->font_size != size)
    {
        priv->font_size = CLAMP (size, 6, 72);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}

gboolean
lrg_chart_tooltip_get_show_series_name (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), TRUE);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->show_series_name;
}

void
lrg_chart_tooltip_set_show_series_name (LrgChartTooltip *self,
                                        gboolean         show)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    show = !!show;
    if (priv->show_series_name != show)
    {
        priv->show_series_name = show;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SERIES_NAME]);
    }
}

const gchar *
lrg_chart_tooltip_get_value_format (LrgChartTooltip *self)
{
    LrgChartTooltipPrivate *priv;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), NULL);

    priv = lrg_chart_tooltip_get_instance_private (self);
    return priv->value_format;
}

void
lrg_chart_tooltip_set_value_format (LrgChartTooltip *self,
                                    const gchar     *format)
{
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);

    if (g_strcmp0 (priv->value_format, format) != 0)
    {
        g_clear_pointer (&priv->value_format, g_free);
        priv->value_format = g_strdup (format);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE_FORMAT]);
    }
}

gchar *
lrg_chart_tooltip_format_content (LrgChartTooltip       *self,
                                  LrgChartDataSeries    *series,
                                  const LrgChartHitInfo *hit)
{
    LrgChartTooltipClass *klass;

    g_return_val_if_fail (LRG_IS_CHART_TOOLTIP (self), NULL);

    klass = LRG_CHART_TOOLTIP_GET_CLASS (self);
    if (klass->format_content != NULL)
    {
        return klass->format_content (self, series, hit);
    }

    return NULL;
}

void
lrg_chart_tooltip_draw (LrgChartTooltip *self,
                        gfloat           x,
                        gfloat           y,
                        const gchar     *content)
{
    LrgChartTooltipClass *klass;
    LrgChartTooltipPrivate *priv;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    priv = lrg_chart_tooltip_get_instance_private (self);
    if (!priv->visible)
    {
        return;
    }

    klass = LRG_CHART_TOOLTIP_GET_CLASS (self);
    if (klass->draw != NULL)
    {
        klass->draw (self, x, y, content);
    }
}

void
lrg_chart_tooltip_get_size (LrgChartTooltip *self,
                            const gchar     *content,
                            gfloat          *out_width,
                            gfloat          *out_height)
{
    LrgChartTooltipClass *klass;

    g_return_if_fail (LRG_IS_CHART_TOOLTIP (self));

    klass = LRG_CHART_TOOLTIP_GET_CLASS (self);
    if (klass->get_size != NULL)
    {
        klass->get_size (self, content, out_width, out_height);
    }
    else
    {
        if (out_width != NULL)
            *out_width = 0.0f;
        if (out_height != NULL)
            *out_height = 0.0f;
    }
}
