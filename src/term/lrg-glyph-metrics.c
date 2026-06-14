/* lrg-glyph-metrics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-glyph-metrics.h"

struct _LrgGlyphMetrics
{
    guint    page;
    gint     px;
    gint     py;
    gint     w;
    gint     h;
    gint     bearing_x;
    gint     bearing_y;
    gint     advance;
    gboolean is_color;
    gfloat   u0;
    gfloat   v0;
    gfloat   u1;
    gfloat   v1;
};

G_DEFINE_BOXED_TYPE (LrgGlyphMetrics, lrg_glyph_metrics,
                     lrg_glyph_metrics_copy, lrg_glyph_metrics_free)

LrgGlyphMetrics *
lrg_glyph_metrics_new (guint    page,
                       gint     px,
                       gint     py,
                       gint     w,
                       gint     h,
                       gint     bearing_x,
                       gint     bearing_y,
                       gint     advance,
                       gboolean is_color)
{
    LrgGlyphMetrics *self = g_new0 (LrgGlyphMetrics, 1);

    self->page = page;
    self->px = px;
    self->py = py;
    self->w = w;
    self->h = h;
    self->bearing_x = bearing_x;
    self->bearing_y = bearing_y;
    self->advance = advance;
    self->is_color = is_color;

    return self;
}

LrgGlyphMetrics *
lrg_glyph_metrics_copy (const LrgGlyphMetrics *self)
{
    LrgGlyphMetrics *copy;

    if (self == NULL)
        return NULL;

    copy = g_new0 (LrgGlyphMetrics, 1);
    *copy = *self;

    return copy;
}

void
lrg_glyph_metrics_free (LrgGlyphMetrics *self)
{
    g_free (self);
}

void
lrg_glyph_metrics_set_uv (LrgGlyphMetrics *self,
                          gfloat           u0,
                          gfloat           v0,
                          gfloat           u1,
                          gfloat           v1)
{
    g_return_if_fail (self != NULL);

    self->u0 = u0;
    self->v0 = v0;
    self->u1 = u1;
    self->v1 = v1;
}

guint
lrg_glyph_metrics_get_page (const LrgGlyphMetrics *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->page;
}

void
lrg_glyph_metrics_get_rect (const LrgGlyphMetrics *self,
                            gint                  *out_x,
                            gint                  *out_y,
                            gint                  *out_w,
                            gint                  *out_h)
{
    g_return_if_fail (self != NULL);

    if (out_x != NULL)
        *out_x = self->px;
    if (out_y != NULL)
        *out_y = self->py;
    if (out_w != NULL)
        *out_w = self->w;
    if (out_h != NULL)
        *out_h = self->h;
}

void
lrg_glyph_metrics_get_uv (const LrgGlyphMetrics *self,
                          gfloat                *out_u0,
                          gfloat                *out_v0,
                          gfloat                *out_u1,
                          gfloat                *out_v1)
{
    g_return_if_fail (self != NULL);

    if (out_u0 != NULL)
        *out_u0 = self->u0;
    if (out_v0 != NULL)
        *out_v0 = self->v0;
    if (out_u1 != NULL)
        *out_u1 = self->u1;
    if (out_v1 != NULL)
        *out_v1 = self->v1;
}

gint
lrg_glyph_metrics_get_bearing_x (const LrgGlyphMetrics *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->bearing_x;
}

gint
lrg_glyph_metrics_get_bearing_y (const LrgGlyphMetrics *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->bearing_y;
}

gint
lrg_glyph_metrics_get_advance (const LrgGlyphMetrics *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->advance;
}

gboolean
lrg_glyph_metrics_get_is_color (const LrgGlyphMetrics *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->is_color;
}
