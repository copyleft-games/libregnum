/* lrg-reel-iris-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelIrisTransition - circular iris reveal between two reel frames.
 */

#include "config.h"
#include "lrg-reel-iris-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include <math.h>

struct _LrgReelIrisTransition
{
    LrgReelTransition parent_instance;
    gboolean          inverse;
};

G_DEFINE_FINAL_TYPE (LrgReelIrisTransition, lrg_reel_iris_transition,
                     LRG_TYPE_REEL_TRANSITION)

enum
{
    PROP_0,
    PROP_INVERSE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

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
lrg_reel_iris_transition_composite (LrgReelTransition *base,
                                      LrgImageCanvas    *canvas,
                                      GrlImage          *from,
                                      GrlImage          *to,
                                      gdouble            progress)
{
    LrgReelIrisTransition *self;
    GrlImage              *canvas_img;
    gint                   w;
    gint                   h;
    gint                   x;
    gint                   y;
    gfloat                 cx;
    gfloat                 cy;
    gfloat                 max_dist;
    gfloat                 radius;
    GrlImage              *reveal_src;
    GrlImage              *base_src;

    self       = LRG_REEL_IRIS_TRANSITION (base);
    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);
    cx         = (gfloat) w * 0.5f;
    cy         = (gfloat) h * 0.5f;

    /*
     * max_dist: half-diagonal, ensures the circle fully covers all corners
     * at radius == max_dist.
     */
    max_dist = 0.5f * (gfloat) sqrt ((double) w * (double) w +
                                      (double) h * (double) h);

    /*
     * When inverse == FALSE: @to grows in (reveal_src=to, base_src=from).
     * When inverse == TRUE:  @from grows in over @to (reveal_src=from,
     *                        base_src=to) — equivalent to closing @to away.
     */
    if (!self->inverse)
    {
        reveal_src = to;
        base_src   = from;
        radius     = (gfloat) progress * max_dist;
    }
    else
    {
        reveal_src = from;
        base_src   = to;
        radius     = (gfloat) (1.0 - progress) * max_dist;
    }

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

    /* Draw the background layer. */
    draw_full (canvas_img, base_src);

    /*
     * Per-pixel reveal: copy pixels from reveal_src where the distance from
     * the centre is less than the current radius.
     */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            gfloat              dx;
            gfloat              dy;
            gfloat              dist;
            g_autoptr(GrlColor) c = NULL;

            dx   = (gfloat) x - cx;
            dy   = (gfloat) y - cy;
            dist = (gfloat) sqrt ((double) dx * (double) dx +
                                  (double) dy * (double) dy);

            if (dist < radius)
            {
                c = grl_image_get_pixel (reveal_src, x, y);
                grl_image_draw_pixel (canvas_img, x, y, c);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_iris_transition_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgReelIrisTransition *self = LRG_REEL_IRIS_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_INVERSE:
        g_value_set_boolean (value, self->inverse);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_iris_transition_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgReelIrisTransition *self = LRG_REEL_IRIS_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_INVERSE:
        lrg_reel_iris_transition_set_inverse (self,
                                               g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_iris_transition_class_init (LrgReelIrisTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_iris_transition_get_property;
    object_class->set_property = lrg_reel_iris_transition_set_property;

    transition_class->composite = lrg_reel_iris_transition_composite;

    /**
     * LrgReelIrisTransition:inverse:
     *
     * When %FALSE (the default) @to is revealed by a growing circle of
     * radius proportional to @progress.  When %TRUE the roles are swapped:
     * @to starts fully visible and a growing circle of @from closes inward.
     *
     * Since: 1.0
     */
    properties[PROP_INVERSE] =
        g_param_spec_boolean ("inverse",
                              "Inverse",
                              "Invert the iris direction so @to closes away",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_iris_transition_init (LrgReelIrisTransition *self)
{
    self->inverse = FALSE;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_iris_transition_new:
 *
 * Creates a new #LrgReelIrisTransition with the default settings
 * (#LrgReelIrisTransition:inverse = %FALSE).
 *
 * Returns: (transfer full): a new #LrgReelIrisTransition
 *
 * Since: 1.0
 */
LrgReelIrisTransition *
lrg_reel_iris_transition_new (void)
{
    return g_object_new (LRG_TYPE_REEL_IRIS_TRANSITION, NULL);
}

/**
 * lrg_reel_iris_transition_get_inverse:
 * @self: an #LrgReelIrisTransition
 *
 * Returns whether the iris direction is inverted.
 *
 * Returns: %TRUE if the iris closes instead of opens
 *
 * Since: 1.0
 */
gboolean
lrg_reel_iris_transition_get_inverse (LrgReelIrisTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_IRIS_TRANSITION (self), FALSE);
    return self->inverse;
}

/**
 * lrg_reel_iris_transition_set_inverse:
 * @self: an #LrgReelIrisTransition
 * @inverse: %TRUE to invert the iris direction
 *
 * When @inverse is %FALSE (the default) @to is revealed by a growing circle.
 * When %TRUE @from is revealed by a growing circle (i.e. @to closes away).
 *
 * Since: 1.0
 */
void
lrg_reel_iris_transition_set_inverse (LrgReelIrisTransition *self,
                                       gboolean               inverse)
{
    g_return_if_fail (LRG_IS_REEL_IRIS_TRANSITION (self));

    inverse = !!inverse;

    if (self->inverse == inverse)
        return;

    self->inverse = inverse;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INVERSE]);
}
