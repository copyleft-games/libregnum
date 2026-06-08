/* lrg-reel-push-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelPushTransition - simultaneous push-out/push-in between two frames.
 */

#include "config.h"
#include "lrg-reel-push-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include <math.h>

struct _LrgReelPushTransition
{
    LrgReelTransition          parent_instance;
    LrgReelTransitionDirection direction;
};

G_DEFINE_FINAL_TYPE (LrgReelPushTransition, lrg_reel_push_transition,
                     LRG_TYPE_REEL_TRANSITION)

enum
{
    PROP_0,
    PROP_DIRECTION,
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
lrg_reel_push_transition_composite (LrgReelTransition *base,
                                      LrgImageCanvas    *canvas,
                                      GrlImage          *from,
                                      GrlImage          *to,
                                      gdouble            progress)
{
    LrgReelPushTransition *self;
    GrlImage              *canvas_img;
    GrlRectangle           from_rect;
    GrlRectangle           to_rect;
    gint                   w;
    gint                   h;
    gfloat                 offset;

    self       = LRG_REEL_PUSH_TRANSITION (base);
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

    /*
     * Compute destination rectangles for both frames.
     *
     * Direction = the direction @from travels (exits).
     * At progress p:
     *   @from is translated by -p * dimension in the exit direction.
     *   @to   is translated from the opposite edge: starts at +dimension,
     *         moves to 0, so offset = (1-p) * dimension from opposite edge
     *         = dimension - p*dimension.
     */
    switch (self->direction)
    {
    case LRG_REEL_TRANSITION_DIRECTION_LEFT:
        /* @from exits left; @to enters from the right. */
        offset = (gfloat) w * (gfloat) progress;

        from_rect.x      = -offset;
        from_rect.y      = 0.0f;
        from_rect.width  = (gfloat) w;
        from_rect.height = (gfloat) h;

        to_rect.x      = (gfloat) w - offset;
        to_rect.y      = 0.0f;
        to_rect.width  = (gfloat) w;
        to_rect.height = (gfloat) h;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_RIGHT:
        /* @from exits right; @to enters from the left. */
        offset = (gfloat) w * (gfloat) progress;

        from_rect.x      = offset;
        from_rect.y      = 0.0f;
        from_rect.width  = (gfloat) w;
        from_rect.height = (gfloat) h;

        to_rect.x      = offset - (gfloat) w;
        to_rect.y      = 0.0f;
        to_rect.width  = (gfloat) w;
        to_rect.height = (gfloat) h;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_UP:
        /* @from exits upward; @to enters from the bottom. */
        offset = (gfloat) h * (gfloat) progress;

        from_rect.x      = 0.0f;
        from_rect.y      = -offset;
        from_rect.width  = (gfloat) w;
        from_rect.height = (gfloat) h;

        to_rect.x      = 0.0f;
        to_rect.y      = (gfloat) h - offset;
        to_rect.width  = (gfloat) w;
        to_rect.height = (gfloat) h;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_DOWN:
    default:
        /* @from exits downward; @to enters from the top. */
        offset = (gfloat) h * (gfloat) progress;

        from_rect.x      = 0.0f;
        from_rect.y      = offset;
        from_rect.width  = (gfloat) w;
        from_rect.height = (gfloat) h;

        to_rect.x      = 0.0f;
        to_rect.y      = offset - (gfloat) h;
        to_rect.width  = (gfloat) w;
        to_rect.height = (gfloat) h;
        break;
    }

    /*
     * Draw @from first (it may be partially on-canvas), then @to on top.
     * Off-canvas regions are clipped by grl_image_draw_image.
     */
    grl_image_draw_image (canvas_img, from, NULL, &from_rect, NULL);
    grl_image_draw_image (canvas_img, to,   NULL, &to_rect,   NULL);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_push_transition_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgReelPushTransition *self = LRG_REEL_PUSH_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        g_value_set_enum (value, (gint) self->direction);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_push_transition_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgReelPushTransition *self = LRG_REEL_PUSH_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        lrg_reel_push_transition_set_direction (
            self, (LrgReelTransitionDirection) g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_push_transition_class_init (LrgReelPushTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_push_transition_get_property;
    object_class->set_property = lrg_reel_push_transition_set_property;

    transition_class->composite = lrg_reel_push_transition_composite;

    /**
     * LrgReelPushTransition:direction:
     *
     * The direction in which the outgoing frame (@from) is pushed.  The
     * incoming frame (@to) always moves in the opposite direction.  The
     * default is %LRG_REEL_TRANSITION_DIRECTION_LEFT.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "Direction in which the outgoing frame is pushed",
                           LRG_TYPE_REEL_TRANSITION_DIRECTION,
                           LRG_REEL_TRANSITION_DIRECTION_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_push_transition_init (LrgReelPushTransition *self)
{
    self->direction = LRG_REEL_TRANSITION_DIRECTION_LEFT;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_push_transition_new:
 * @direction: the direction @from is pushed out of the frame
 *
 * Creates a new #LrgReelPushTransition.
 *
 * Returns: (transfer full): a new #LrgReelPushTransition
 *
 * Since: 1.0
 */
LrgReelPushTransition *
lrg_reel_push_transition_new (LrgReelTransitionDirection direction)
{
    return g_object_new (LRG_TYPE_REEL_PUSH_TRANSITION,
                         "direction", (gint) direction,
                         NULL);
}

/**
 * lrg_reel_push_transition_get_direction:
 * @self: an #LrgReelPushTransition
 *
 * Returns the push direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LrgReelTransitionDirection
lrg_reel_push_transition_get_direction (LrgReelPushTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_PUSH_TRANSITION (self),
                          LRG_REEL_TRANSITION_DIRECTION_LEFT);
    return self->direction;
}

/**
 * lrg_reel_push_transition_set_direction:
 * @self: an #LrgReelPushTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the push direction.
 *
 * Since: 1.0
 */
void
lrg_reel_push_transition_set_direction (LrgReelPushTransition      *self,
                                         LrgReelTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_REEL_PUSH_TRANSITION (self));

    if (self->direction == direction)
        return;

    self->direction = direction;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
}
