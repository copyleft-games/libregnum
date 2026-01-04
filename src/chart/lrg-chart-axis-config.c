/* lrg-chart-axis-config.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-axis-config.h"

/* ==========================================================================
 * Default Colors
 * ========================================================================== */

static const GrlColor DEFAULT_AXIS_COLOR = { 200, 200, 200, 255 };
static const GrlColor DEFAULT_GRID_COLOR = { 100, 100, 100, 128 };

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgChartAxisConfig
{
    gchar    *title;
    gdouble   min;
    gdouble   max;
    gdouble   step;
    gboolean  show_grid;
    gboolean  logarithmic;
    gchar    *format;
    gfloat    label_rotation;
    GrlColor  color;
    GrlColor  grid_color;
};

G_DEFINE_BOXED_TYPE (LrgChartAxisConfig, lrg_chart_axis_config,
                     lrg_chart_axis_config_copy, lrg_chart_axis_config_free)

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgChartAxisConfig *
lrg_chart_axis_config_new (void)
{
    LrgChartAxisConfig *self;

    self = g_new0 (LrgChartAxisConfig, 1);
    self->title = NULL;
    self->min = LRG_CHART_AXIS_AUTO;
    self->max = LRG_CHART_AXIS_AUTO;
    self->step = LRG_CHART_AXIS_AUTO;
    self->show_grid = TRUE;
    self->logarithmic = FALSE;
    self->format = NULL;
    self->label_rotation = 0.0f;
    self->color = DEFAULT_AXIS_COLOR;
    self->grid_color = DEFAULT_GRID_COLOR;

    return self;
}

LrgChartAxisConfig *
lrg_chart_axis_config_new_with_title (const gchar *title)
{
    LrgChartAxisConfig *self;

    self = lrg_chart_axis_config_new ();
    self->title = g_strdup (title);

    return self;
}

LrgChartAxisConfig *
lrg_chart_axis_config_new_with_range (const gchar *title,
                                       gdouble      min,
                                       gdouble      max)
{
    LrgChartAxisConfig *self;

    self = lrg_chart_axis_config_new ();
    self->title = g_strdup (title);
    self->min = min;
    self->max = max;

    return self;
}

LrgChartAxisConfig *
lrg_chart_axis_config_copy (const LrgChartAxisConfig *self)
{
    LrgChartAxisConfig *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgChartAxisConfig, 1);
    copy->title = g_strdup (self->title);
    copy->min = self->min;
    copy->max = self->max;
    copy->step = self->step;
    copy->show_grid = self->show_grid;
    copy->logarithmic = self->logarithmic;
    copy->format = g_strdup (self->format);
    copy->label_rotation = self->label_rotation;
    copy->color = self->color;
    copy->grid_color = self->grid_color;

    return copy;
}

void
lrg_chart_axis_config_free (LrgChartAxisConfig *self)
{
    if (self == NULL)
        return;

    g_free (self->title);
    g_free (self->format);
    g_free (self);
}

/* ==========================================================================
 * Title
 * ========================================================================== */

const gchar *
lrg_chart_axis_config_get_title (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->title;
}

void
lrg_chart_axis_config_set_title (LrgChartAxisConfig *self,
                                  const gchar        *title)
{
    g_return_if_fail (self != NULL);

    g_free (self->title);
    self->title = g_strdup (title);
}

/* ==========================================================================
 * Range
 * ========================================================================== */

gdouble
lrg_chart_axis_config_get_min (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, LRG_CHART_AXIS_AUTO);
    return self->min;
}

void
lrg_chart_axis_config_set_min (LrgChartAxisConfig *self,
                                gdouble             min)
{
    g_return_if_fail (self != NULL);
    self->min = min;
}

gdouble
lrg_chart_axis_config_get_max (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, LRG_CHART_AXIS_AUTO);
    return self->max;
}

void
lrg_chart_axis_config_set_max (LrgChartAxisConfig *self,
                                gdouble             max)
{
    g_return_if_fail (self != NULL);
    self->max = max;
}

gdouble
lrg_chart_axis_config_get_step (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, LRG_CHART_AXIS_AUTO);
    return self->step;
}

void
lrg_chart_axis_config_set_step (LrgChartAxisConfig *self,
                                 gdouble             step)
{
    g_return_if_fail (self != NULL);
    self->step = step;
}

gboolean
lrg_chart_axis_config_is_min_auto (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return isnan (self->min);
}

gboolean
lrg_chart_axis_config_is_max_auto (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return isnan (self->max);
}

gboolean
lrg_chart_axis_config_is_step_auto (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, TRUE);
    return isnan (self->step);
}

/* ==========================================================================
 * Grid and Scale
 * ========================================================================== */

gboolean
lrg_chart_axis_config_get_show_grid (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->show_grid;
}

void
lrg_chart_axis_config_set_show_grid (LrgChartAxisConfig *self,
                                      gboolean            show_grid)
{
    g_return_if_fail (self != NULL);
    self->show_grid = show_grid;
}

gboolean
lrg_chart_axis_config_get_logarithmic (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->logarithmic;
}

void
lrg_chart_axis_config_set_logarithmic (LrgChartAxisConfig *self,
                                        gboolean            logarithmic)
{
    g_return_if_fail (self != NULL);
    self->logarithmic = logarithmic;
}

/* ==========================================================================
 * Formatting
 * ========================================================================== */

const gchar *
lrg_chart_axis_config_get_format (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->format;
}

void
lrg_chart_axis_config_set_format (LrgChartAxisConfig *self,
                                   const gchar        *format)
{
    g_return_if_fail (self != NULL);

    g_free (self->format);
    self->format = g_strdup (format);
}

gfloat
lrg_chart_axis_config_get_label_rotation (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->label_rotation;
}

void
lrg_chart_axis_config_set_label_rotation (LrgChartAxisConfig *self,
                                           gfloat              rotation)
{
    g_return_if_fail (self != NULL);
    self->label_rotation = rotation;
}

/* ==========================================================================
 * Colors
 * ========================================================================== */

const GrlColor *
lrg_chart_axis_config_get_color (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return &self->color;
}

void
lrg_chart_axis_config_set_color (LrgChartAxisConfig *self,
                                  const GrlColor     *color)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (color != NULL);

    self->color = *color;
}

const GrlColor *
lrg_chart_axis_config_get_grid_color (const LrgChartAxisConfig *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return &self->grid_color;
}

void
lrg_chart_axis_config_set_grid_color (LrgChartAxisConfig *self,
                                       const GrlColor     *color)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (color != NULL);

    self->grid_color = *color;
}
