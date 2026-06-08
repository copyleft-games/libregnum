/* lrg-reel-slide-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSlideTransition - incoming frame slides in over the outgoing frame.
 */

#include "config.h"
#include "lrg-reel-slide-transition.h"
#include "lrg-reel-transition.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelSlideTransition
{
    LrgReelTransition          parent_instance;
    LrgReelTransitionDirection direction;
};

G_DEFINE_FINAL_TYPE (LrgReelSlideTransition, lrg_reel_slide_transition,
                     LRG_TYPE_REEL_TRANSITION)

enum
{
    PROP_0,
    PROP_DIRECTION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Helper: draw src fully onto dst with REPLACE blend
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
lrg_reel_slide_transition_composite (LrgReelTransition *base,
                                       LrgImageCanvas    *canvas,
                                       GrlImage          *from,
                                       GrlImage          *to,
                                       gdouble            progress)
{
    LrgReelSlideTransition *self;
    GrlImage               *canvas_img;
    GrlRectangle            dst_rect;
    gint                    w;
    gint                    h;
    gfloat                  dx;
    gfloat                  dy;

    self       = LRG_REEL_SLIDE_TRANSITION (base);
    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);

    /* Draw the outgoing frame as the background. */
    draw_full (canvas_img, from);

    /* Compute the translation for @to.
     *
     * Direction = the edge @to ENTERS FROM.
     * At progress 0 @to is fully off-screen; at progress 1 it is at (0,0).
     */
    switch (self->direction)
    {
    case LRG_REEL_TRANSITION_DIRECTION_LEFT:
        /* Enters from the left: starts at x=-(w) and moves right to x=0. */
        dx = -((gfloat) w) * (1.0f - (gfloat) progress);
        dy = 0.0f;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_RIGHT:
        /* Enters from the right: starts at x=+w and moves left to x=0. */
        dx = ((gfloat) w) * (1.0f - (gfloat) progress);
        dy = 0.0f;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_UP:
        /* Enters from the top: starts at y=-(h) and moves down to y=0. */
        dx = 0.0f;
        dy = -((gfloat) h) * (1.0f - (gfloat) progress);
        break;

    case LRG_REEL_TRANSITION_DIRECTION_DOWN:
    default:
        /* Enters from the bottom: starts at y=+h and moves up to y=0. */
        dx = 0.0f;
        dy = ((gfloat) h) * (1.0f - (gfloat) progress);
        break;
    }

    /* dst_rect: full-size @to placed at the computed offset.
     * Off-canvas portions are clipped by grl_image_draw_image. */
    dst_rect.x      = dx;
    dst_rect.y      = dy;
    dst_rect.width  = (gfloat) w;
    dst_rect.height = (gfloat) h;

    grl_image_draw_image (canvas_img, to, NULL, &dst_rect, NULL);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_slide_transition_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    LrgReelSlideTransition *self = LRG_REEL_SLIDE_TRANSITION (object);

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
lrg_reel_slide_transition_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
    LrgReelSlideTransition *self = LRG_REEL_SLIDE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        lrg_reel_slide_transition_set_direction (
            self, (LrgReelTransitionDirection) g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_slide_transition_class_init (LrgReelSlideTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_slide_transition_get_property;
    object_class->set_property = lrg_reel_slide_transition_set_property;

    transition_class->composite = lrg_reel_slide_transition_composite;

    /**
     * LrgReelSlideTransition:direction:
     *
     * The edge from which the incoming frame slides in.  The default is
     * %LRG_REEL_TRANSITION_DIRECTION_LEFT.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "Edge from which the incoming frame slides in",
                           LRG_TYPE_REEL_TRANSITION_DIRECTION,
                           LRG_REEL_TRANSITION_DIRECTION_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_slide_transition_init (LrgReelSlideTransition *self)
{
    self->direction = LRG_REEL_TRANSITION_DIRECTION_LEFT;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_slide_transition_new:
 * @direction: the edge from which the incoming frame slides in
 *
 * Creates a new #LrgReelSlideTransition.
 *
 * Returns: (transfer full): a new #LrgReelSlideTransition
 *
 * Since: 1.0
 */
LrgReelSlideTransition *
lrg_reel_slide_transition_new (LrgReelTransitionDirection direction)
{
    return g_object_new (LRG_TYPE_REEL_SLIDE_TRANSITION,
                         "direction", (gint) direction,
                         NULL);
}

/**
 * lrg_reel_slide_transition_get_direction:
 * @self: an #LrgReelSlideTransition
 *
 * Returns the slide direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LrgReelTransitionDirection
lrg_reel_slide_transition_get_direction (LrgReelSlideTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_SLIDE_TRANSITION (self),
                          LRG_REEL_TRANSITION_DIRECTION_LEFT);
    return self->direction;
}

/**
 * lrg_reel_slide_transition_set_direction:
 * @self: an #LrgReelSlideTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the slide direction.
 *
 * Since: 1.0
 */
void
lrg_reel_slide_transition_set_direction (LrgReelSlideTransition     *self,
                                          LrgReelTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_REEL_SLIDE_TRANSITION (self));

    if (self->direction == direction)
        return;

    self->direction = direction;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
}
