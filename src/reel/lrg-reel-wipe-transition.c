/* lrg-reel-wipe-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelWipeTransition - hard-edge wipe between two reel frames.
 */

#include "config.h"
#include "lrg-reel-wipe-transition.h"
#include "lrg-reel-transition.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelWipeTransition
{
    LrgReelTransition          parent_instance;
    LrgReelTransitionDirection direction;
};

G_DEFINE_FINAL_TYPE (LrgReelWipeTransition, lrg_reel_wipe_transition,
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
lrg_reel_wipe_transition_composite (LrgReelTransition *base,
                                      LrgImageCanvas    *canvas,
                                      GrlImage          *from,
                                      GrlImage          *to,
                                      gdouble            progress)
{
    LrgReelWipeTransition *self;
    GrlImage              *canvas_img;
    GrlRectangle           src_rect;
    GrlRectangle           dst_rect;
    gint                   w;
    gint                   h;
    gfloat                 reveal_w;
    gfloat                 reveal_h;
    gfloat                 origin_x;
    gfloat                 origin_y;

    self       = LRG_REEL_WIPE_TRANSITION (base);
    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);

    /* Draw the outgoing frame fully first. */
    draw_full (canvas_img, from);

    /* Compute the sub-rectangle of @to that is revealed so far.
     * src_rect == dst_rect (no scaling; partial blit from the same region). */
    switch (self->direction)
    {
    case LRG_REEL_TRANSITION_DIRECTION_RIGHT:
        /* Reveal grows leftward → rightward. */
        reveal_w = (gfloat) w * (gfloat) progress;
        reveal_h = (gfloat) h;
        origin_x = 0.0f;
        origin_y = 0.0f;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_LEFT:
        /* Reveal grows from the right edge leftward. */
        reveal_w = (gfloat) w * (gfloat) progress;
        reveal_h = (gfloat) h;
        origin_x = (gfloat) w - reveal_w;
        origin_y = 0.0f;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_DOWN:
        /* Reveal grows from the top edge downward. */
        reveal_w = (gfloat) w;
        reveal_h = (gfloat) h * (gfloat) progress;
        origin_x = 0.0f;
        origin_y = 0.0f;
        break;

    case LRG_REEL_TRANSITION_DIRECTION_UP:
    default:
        /* Reveal grows from the bottom edge upward. */
        reveal_w = (gfloat) w;
        reveal_h = (gfloat) h * (gfloat) progress;
        origin_x = 0.0f;
        origin_y = (gfloat) h - reveal_h;
        break;
    }

    /* Guard: skip if the revealed region is sub-pixel. */
    if (reveal_w < 1.0f || reveal_h < 1.0f)
        return;

    src_rect.x      = origin_x;
    src_rect.y      = origin_y;
    src_rect.width  = reveal_w;
    src_rect.height = reveal_h;

    dst_rect.x      = origin_x;
    dst_rect.y      = origin_y;
    dst_rect.width  = reveal_w;
    dst_rect.height = reveal_h;

    grl_image_draw_image (canvas_img, to, &src_rect, &dst_rect, NULL);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_wipe_transition_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgReelWipeTransition *self = LRG_REEL_WIPE_TRANSITION (object);

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
lrg_reel_wipe_transition_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgReelWipeTransition *self = LRG_REEL_WIPE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        lrg_reel_wipe_transition_set_direction (
            self, (LrgReelTransitionDirection) g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_wipe_transition_class_init (LrgReelWipeTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_wipe_transition_get_property;
    object_class->set_property = lrg_reel_wipe_transition_set_property;

    transition_class->composite = lrg_reel_wipe_transition_composite;

    /**
     * LrgReelWipeTransition:direction:
     *
     * The direction from which the incoming frame is revealed.  The default
     * is %LRG_REEL_TRANSITION_DIRECTION_RIGHT.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "Edge from which the incoming frame is revealed",
                           LRG_TYPE_REEL_TRANSITION_DIRECTION,
                           LRG_REEL_TRANSITION_DIRECTION_RIGHT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_wipe_transition_init (LrgReelWipeTransition *self)
{
    self->direction = LRG_REEL_TRANSITION_DIRECTION_RIGHT;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_wipe_transition_new:
 * @direction: the edge from which the incoming frame is revealed
 *
 * Creates a new #LrgReelWipeTransition.
 *
 * Returns: (transfer full): a new #LrgReelWipeTransition
 *
 * Since: 1.0
 */
LrgReelWipeTransition *
lrg_reel_wipe_transition_new (LrgReelTransitionDirection direction)
{
    return g_object_new (LRG_TYPE_REEL_WIPE_TRANSITION,
                         "direction", (gint) direction,
                         NULL);
}

/**
 * lrg_reel_wipe_transition_get_direction:
 * @self: an #LrgReelWipeTransition
 *
 * Returns the wipe direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LrgReelTransitionDirection
lrg_reel_wipe_transition_get_direction (LrgReelWipeTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_WIPE_TRANSITION (self),
                          LRG_REEL_TRANSITION_DIRECTION_RIGHT);
    return self->direction;
}

/**
 * lrg_reel_wipe_transition_set_direction:
 * @self: an #LrgReelWipeTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the wipe direction.
 *
 * Since: 1.0
 */
void
lrg_reel_wipe_transition_set_direction (LrgReelWipeTransition      *self,
                                         LrgReelTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_REEL_WIPE_TRANSITION (self));

    if (self->direction == direction)
        return;

    self->direction = direction;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
}
