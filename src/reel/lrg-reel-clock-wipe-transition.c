/* lrg-reel-clock-wipe-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelClockWipeTransition - angular sweep transition between two reel frames.
 */

#include "config.h"
#include "lrg-reel-clock-wipe-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include <math.h>

/* M_PI may not be defined under strict C89 — provide a fallback. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct _LrgReelClockWipeTransition
{
    LrgReelTransition parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgReelClockWipeTransition, lrg_reel_clock_wipe_transition,
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
lrg_reel_clock_wipe_transition_composite (LrgReelTransition *base,
                                            LrgImageCanvas    *canvas,
                                            GrlImage          *from,
                                            GrlImage          *to,
                                            gdouble            progress)
{
    GrlImage *canvas_img;
    gint      w;
    gint      h;
    gint      x;
    gint      y;
    gfloat    cx;
    gfloat    cy;
    gfloat    prog_f;

    /* Suppress unused-parameter warning for the parameter-less struct. */
    (void) base;

    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);
    cx         = (gfloat) w * 0.5f;
    cy         = (gfloat) h * 0.5f;
    prog_f     = (gfloat) progress;

    /* Fast paths at the boundaries. */
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

    /* Draw @from as the base layer. */
    draw_full (canvas_img, from);

    /*
     * Per-pixel sweep: for each pixel, compute the clockwise angle from
     * 12 o'clock, normalised to [0, 1).  If that angle < progress, copy
     * the corresponding pixel from @to.
     *
     * atan2 returns a value in (-pi, pi].
     *   - Straight up   (dx=0, dy<0) => atan2(-dy, dx) = atan2(+, 0) = pi/2
     *
     * We need clockwise from up, so:
     *   angle_rad = atan2(dx, -dy)    (rotates coordinate system 90 deg CCW)
     * This gives (-pi, pi] with 0 at top.  Normalise to [0, 1):
     *   norm = (angle_rad + pi) / (2*pi)
     */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            gfloat              dx;
            gfloat              dy;
            gfloat              angle_rad;
            gfloat              norm;
            g_autoptr(GrlColor) c = NULL;

            dx        = (gfloat) x - cx;
            dy        = (gfloat) y - cy;
            angle_rad = (gfloat) atan2 ((double) dx, (double) (-dy));
            norm      = (angle_rad + (gfloat) M_PI) / (2.0f * (gfloat) M_PI);

            if (norm < prog_f)
            {
                c = grl_image_get_pixel (to, x, y);
                grl_image_draw_pixel (canvas_img, x, y, c);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_clock_wipe_transition_class_init (LrgReelClockWipeTransitionClass *klass)
{
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    transition_class->composite = lrg_reel_clock_wipe_transition_composite;
}

static void
lrg_reel_clock_wipe_transition_init (LrgReelClockWipeTransition *self)
{
    (void) self;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_clock_wipe_transition_new:
 *
 * Creates a new #LrgReelClockWipeTransition.
 *
 * Returns: (transfer full): a new #LrgReelClockWipeTransition
 *
 * Since: 1.0
 */
LrgReelClockWipeTransition *
lrg_reel_clock_wipe_transition_new (void)
{
    return g_object_new (LRG_TYPE_REEL_CLOCK_WIPE_TRANSITION, NULL);
}
