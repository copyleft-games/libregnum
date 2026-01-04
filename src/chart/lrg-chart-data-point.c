/* lrg-chart-data-point.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-data-point.h"

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgChartDataPoint
{
    gdouble         x;
    gdouble         y;
    gdouble         z;
    gdouble         w;
    gchar          *label;
    GrlColor       *color;      /* NULL means no override */
    gpointer        user_data;
    GDestroyNotify  user_data_destroy;
};

G_DEFINE_BOXED_TYPE (LrgChartDataPoint, lrg_chart_data_point,
                     lrg_chart_data_point_copy, lrg_chart_data_point_free)

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgChartDataPoint *
lrg_chart_data_point_new (gdouble x,
                          gdouble y)
{
    LrgChartDataPoint *self;

    self = g_new0 (LrgChartDataPoint, 1);
    self->x = x;
    self->y = y;
    self->z = 0.0;
    self->w = 0.0;
    self->label = NULL;
    self->color = NULL;
    self->user_data = NULL;
    self->user_data_destroy = NULL;

    return self;
}

LrgChartDataPoint *
lrg_chart_data_point_new_with_z (gdouble x,
                                  gdouble y,
                                  gdouble z)
{
    LrgChartDataPoint *self;

    self = lrg_chart_data_point_new (x, y);
    self->z = z;

    return self;
}

LrgChartDataPoint *
lrg_chart_data_point_new_labeled (gdouble      x,
                                   gdouble      y,
                                   const gchar *label)
{
    LrgChartDataPoint *self;

    self = lrg_chart_data_point_new (x, y);
    self->label = g_strdup (label);

    return self;
}

LrgChartDataPoint *
lrg_chart_data_point_new_full (gdouble         x,
                                gdouble         y,
                                gdouble         z,
                                gdouble         w,
                                const gchar    *label,
                                const GrlColor *color)
{
    LrgChartDataPoint *self;

    self = g_new0 (LrgChartDataPoint, 1);
    self->x = x;
    self->y = y;
    self->z = z;
    self->w = w;
    self->label = g_strdup (label);
    self->color = (color != NULL) ? grl_color_copy (color) : NULL;
    self->user_data = NULL;
    self->user_data_destroy = NULL;

    return self;
}

LrgChartDataPoint *
lrg_chart_data_point_copy (const LrgChartDataPoint *self)
{
    LrgChartDataPoint *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgChartDataPoint, 1);
    copy->x = self->x;
    copy->y = self->y;
    copy->z = self->z;
    copy->w = self->w;
    copy->label = g_strdup (self->label);
    copy->color = (self->color != NULL) ? grl_color_copy (self->color) : NULL;
    /* Note: user_data is NOT copied - it's caller's responsibility */
    copy->user_data = NULL;
    copy->user_data_destroy = NULL;

    return copy;
}

void
lrg_chart_data_point_free (LrgChartDataPoint *self)
{
    if (self == NULL)
        return;

    g_free (self->label);
    g_clear_pointer (&self->color, grl_color_free);

    if (self->user_data != NULL && self->user_data_destroy != NULL)
    {
        self->user_data_destroy (self->user_data);
    }

    g_free (self);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

gdouble
lrg_chart_data_point_get_x (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->x;
}

void
lrg_chart_data_point_set_x (LrgChartDataPoint *self,
                            gdouble            x)
{
    g_return_if_fail (self != NULL);
    self->x = x;
}

gdouble
lrg_chart_data_point_get_y (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->y;
}

void
lrg_chart_data_point_set_y (LrgChartDataPoint *self,
                            gdouble            y)
{
    g_return_if_fail (self != NULL);
    self->y = y;
}

gdouble
lrg_chart_data_point_get_z (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->z;
}

void
lrg_chart_data_point_set_z (LrgChartDataPoint *self,
                            gdouble            z)
{
    g_return_if_fail (self != NULL);
    self->z = z;
}

gdouble
lrg_chart_data_point_get_w (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->w;
}

void
lrg_chart_data_point_set_w (LrgChartDataPoint *self,
                            gdouble            w)
{
    g_return_if_fail (self != NULL);
    self->w = w;
}

const gchar *
lrg_chart_data_point_get_label (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->label;
}

void
lrg_chart_data_point_set_label (LrgChartDataPoint *self,
                                const gchar       *label)
{
    g_return_if_fail (self != NULL);

    g_free (self->label);
    self->label = g_strdup (label);
}

const GrlColor *
lrg_chart_data_point_get_color (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->color;
}

void
lrg_chart_data_point_set_color (LrgChartDataPoint *self,
                                const GrlColor    *color)
{
    g_return_if_fail (self != NULL);

    g_clear_pointer (&self->color, grl_color_free);
    if (color != NULL)
    {
        self->color = grl_color_copy (color);
    }
}

gboolean
lrg_chart_data_point_has_color (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->color != NULL;
}

void
lrg_chart_data_point_clear_color (LrgChartDataPoint *self)
{
    g_return_if_fail (self != NULL);
    g_clear_pointer (&self->color, grl_color_free);
}

gpointer
lrg_chart_data_point_get_user_data (const LrgChartDataPoint *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->user_data;
}

void
lrg_chart_data_point_set_user_data (LrgChartDataPoint *self,
                                    gpointer           user_data,
                                    GDestroyNotify     destroy)
{
    g_return_if_fail (self != NULL);

    /* Free old user data if present */
    if (self->user_data != NULL && self->user_data_destroy != NULL)
    {
        self->user_data_destroy (self->user_data);
    }

    self->user_data = user_data;
    self->user_data_destroy = destroy;
}
