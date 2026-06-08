/* lrg-reel-zoom-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelZoomTransition - centre-zoom cross-fade between two reel frames.
 */

#include "config.h"
#include "lrg-reel-zoom-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include <math.h>

/* Minimum scale factor for @to at progress == 0. */
#define ZOOM_MIN_SCALE 0.3f

struct _LrgReelZoomTransition
{
    LrgReelTransition parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgReelZoomTransition, lrg_reel_zoom_transition,
                     LRG_TYPE_REEL_TRANSITION)

/* --------------------------------------------------------------------------
 * Helper: draw src fully onto dst with a non-NULL full-size dst_rect
 * -------------------------------------------------------------------------- */

static void
draw_full (GrlImage *dst,
           GrlImage *src)
{
    GrlRectangle dst_rect;

    dst_rect.x      = 0.0f;
    dst_rect.y      = 0.0f;
    dst_rect.width  = (gfloat) grl_image_get_width (dst);
    dst_rect.height = (gfloat) grl_image_get_height (dst);

    grl_image_draw_image (dst, src, NULL, &dst_rect, NULL);
}

/* --------------------------------------------------------------------------
 * Composite vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_zoom_transition_composite (LrgReelTransition *base,
                                      LrgImageCanvas    *canvas,
                                      GrlImage          *from,
                                      GrlImage          *to,
                                      gdouble            progress)
{
    GrlImage     *canvas_img;
    GrlRectangle  to_rect;
    GrlColor      tint;
    gint          w;
    gint          h;
    gfloat        scale;
    gfloat        scaled_w;
    gfloat        scaled_h;

    /* Suppress unused-parameter warning for the parameter-less struct. */
    (void) base;

    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);

    /* Fast paths. */
    if (progress <= 0.0)
    {
        draw_full (canvas_img, from);
        return;
    }
    if (progress >= 1.0)
    {
        draw_full (canvas_img, to);
        return;
    }

    /* Draw @from as the background at full size. */
    draw_full (canvas_img, from);

    /*
     * Scale @to from ZOOM_MIN_SCALE up to 1.0 as progress goes 0 -> 1.
     * scale = ZOOM_MIN_SCALE + (1 - ZOOM_MIN_SCALE) * progress
     */
    scale    = ZOOM_MIN_SCALE + (1.0f - ZOOM_MIN_SCALE) * (gfloat) progress;
    scaled_w = (gfloat) w * scale;
    scaled_h = (gfloat) h * scale;

    /* Centre the scaled image. */
    to_rect.x      = ((gfloat) w - scaled_w) * 0.5f;
    to_rect.y      = ((gfloat) h - scaled_h) * 0.5f;
    to_rect.width  = scaled_w;
    to_rect.height = scaled_h;

    /*
     * Alpha-tint @to: opacity increases linearly from 0 to 255 as progress
     * goes from 0 to 1.  RGB channels are fully white (255) so only the
     * alpha channel affects the blend.
     */
    tint.r = 255;
    tint.g = 255;
    tint.b = 255;
    tint.a = (guint8) (progress * 255.0);

    grl_image_draw_image (canvas_img, to, NULL, &to_rect, &tint);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_zoom_transition_class_init (LrgReelZoomTransitionClass *klass)
{
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    transition_class->composite = lrg_reel_zoom_transition_composite;
}

static void
lrg_reel_zoom_transition_init (LrgReelZoomTransition *self)
{
    (void) self;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_zoom_transition_new:
 *
 * Creates a new #LrgReelZoomTransition.
 *
 * Returns: (transfer full): a new #LrgReelZoomTransition
 *
 * Since: 1.0
 */
LrgReelZoomTransition *
lrg_reel_zoom_transition_new (void)
{
    return g_object_new (LRG_TYPE_REEL_ZOOM_TRANSITION, NULL);
}
