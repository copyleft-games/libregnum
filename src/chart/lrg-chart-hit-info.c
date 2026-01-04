/* lrg-chart-hit-info.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-chart-hit-info.h"

/* ==========================================================================
 * Structure Definition
 * ========================================================================== */

struct _LrgChartHitInfo
{
    gint               series_index;
    gint               point_index;
    gfloat             screen_x;
    gfloat             screen_y;
    GrlRectangle       bounds;
    LrgChartDataPoint *data_point;  /* Reference, not owned */
};

G_DEFINE_BOXED_TYPE (LrgChartHitInfo, lrg_chart_hit_info,
                     lrg_chart_hit_info_copy, lrg_chart_hit_info_free)

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgChartHitInfo *
lrg_chart_hit_info_new (void)
{
    LrgChartHitInfo *self;

    self = g_new0 (LrgChartHitInfo, 1);
    self->series_index = -1;
    self->point_index = -1;
    self->screen_x = 0.0f;
    self->screen_y = 0.0f;
    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = 0.0f;
    self->bounds.height = 0.0f;
    self->data_point = NULL;

    return self;
}

LrgChartHitInfo *
lrg_chart_hit_info_new_with_hit (gint   series_index,
                                  gint   point_index,
                                  gfloat screen_x,
                                  gfloat screen_y)
{
    LrgChartHitInfo *self;

    self = lrg_chart_hit_info_new ();
    self->series_index = series_index;
    self->point_index = point_index;
    self->screen_x = screen_x;
    self->screen_y = screen_y;

    return self;
}

LrgChartHitInfo *
lrg_chart_hit_info_copy (const LrgChartHitInfo *self)
{
    LrgChartHitInfo *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgChartHitInfo, 1);
    copy->series_index = self->series_index;
    copy->point_index = self->point_index;
    copy->screen_x = self->screen_x;
    copy->screen_y = self->screen_y;
    copy->bounds = self->bounds;
    copy->data_point = self->data_point;  /* Reference, not copied */

    return copy;
}

void
lrg_chart_hit_info_free (LrgChartHitInfo *self)
{
    if (self == NULL)
        return;

    /* data_point is not owned, so don't free it */
    g_free (self);
}

/* ==========================================================================
 * State
 * ========================================================================== */

gboolean
lrg_chart_hit_info_has_hit (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->series_index >= 0 && self->point_index >= 0;
}

void
lrg_chart_hit_info_clear (LrgChartHitInfo *self)
{
    g_return_if_fail (self != NULL);

    self->series_index = -1;
    self->point_index = -1;
    self->screen_x = 0.0f;
    self->screen_y = 0.0f;
    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = 0.0f;
    self->bounds.height = 0.0f;
    self->data_point = NULL;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

gint
lrg_chart_hit_info_get_series_index (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, -1);
    return self->series_index;
}

void
lrg_chart_hit_info_set_series_index (LrgChartHitInfo *self,
                                     gint             index)
{
    g_return_if_fail (self != NULL);
    self->series_index = index;
}

gint
lrg_chart_hit_info_get_point_index (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, -1);
    return self->point_index;
}

void
lrg_chart_hit_info_set_point_index (LrgChartHitInfo *self,
                                    gint             index)
{
    g_return_if_fail (self != NULL);
    self->point_index = index;
}

gfloat
lrg_chart_hit_info_get_screen_x (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->screen_x;
}

void
lrg_chart_hit_info_set_screen_x (LrgChartHitInfo *self,
                                 gfloat           x)
{
    g_return_if_fail (self != NULL);
    self->screen_x = x;
}

gfloat
lrg_chart_hit_info_get_screen_y (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->screen_y;
}

void
lrg_chart_hit_info_set_screen_y (LrgChartHitInfo *self,
                                 gfloat           y)
{
    g_return_if_fail (self != NULL);
    self->screen_y = y;
}

void
lrg_chart_hit_info_get_bounds (const LrgChartHitInfo *self,
                               GrlRectangle          *out_bounds)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (out_bounds != NULL);

    *out_bounds = self->bounds;
}

void
lrg_chart_hit_info_set_bounds (LrgChartHitInfo    *self,
                               const GrlRectangle *bounds)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (bounds != NULL);

    self->bounds = *bounds;
}

const LrgChartDataPoint *
lrg_chart_hit_info_get_data_point (const LrgChartHitInfo *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->data_point;
}

void
lrg_chart_hit_info_set_data_point (LrgChartHitInfo         *self,
                                   const LrgChartDataPoint *point)
{
    g_return_if_fail (self != NULL);
    /* Cast away const - we store a reference, not a copy */
    self->data_point = (LrgChartDataPoint *)point;
}
