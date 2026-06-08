/* lrg-reel-fade-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelFadeTransition - cross-fade between two reel frames.
 */

#include "config.h"
#include "lrg-reel-fade-transition.h"
#include "lrg-reel-transition.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelFadeTransition
{
    LrgReelTransition parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgReelFadeTransition, lrg_reel_fade_transition,
                     LRG_TYPE_REEL_TRANSITION)

/* --------------------------------------------------------------------------
 * Helper: draw src fully onto canvas image with REPLACE blend
 * -------------------------------------------------------------------------- */

static void
draw_full (GrlImage *dst,
           GrlImage *src)
{
    GrlRectangle dst_rect;

    dst_rect.x = 0.0f;
    dst_rect.y = 0.0f;
    dst_rect.width = (gfloat) grl_image_get_width (dst);
    dst_rect.height = (gfloat) grl_image_get_height (dst);

    grl_image_draw_image (dst, src, NULL, &dst_rect, NULL);
}

/* --------------------------------------------------------------------------
 * Composite vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_fade_transition_composite (LrgReelTransition *base,
                                     LrgImageCanvas    *canvas,
                                     GrlImage          *from,
                                     GrlImage          *to,
                                     gdouble            progress)
{
    GrlImage *canvas_img;
    GrlLayer *layer;
    gint      w;
    gint      h;

    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);

    /* Draw the outgoing frame fully. */
    draw_full (canvas_img, from);

    /* Composite the incoming frame at opacity = progress. */
    layer = grl_layer_new (w, h);
    draw_full (grl_layer_get_image (layer), to);
    grl_image_composite_layer (canvas_img, layer,
                                0, 0,
                                GRL_LAYER_BLEND_NORMAL,
                                (gfloat) progress);
    grl_layer_unref (layer);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_fade_transition_class_init (LrgReelFadeTransitionClass *klass)
{
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    transition_class->composite = lrg_reel_fade_transition_composite;
}

static void
lrg_reel_fade_transition_init (LrgReelFadeTransition *self)
{
    (void) self;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_fade_transition_new:
 *
 * Creates a new #LrgReelFadeTransition.
 *
 * Returns: (transfer full): a new #LrgReelFadeTransition
 *
 * Since: 1.0
 */
LrgReelFadeTransition *
lrg_reel_fade_transition_new (void)
{
    return g_object_new (LRG_TYPE_REEL_FADE_TRANSITION, NULL);
}
