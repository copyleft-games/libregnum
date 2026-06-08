/* lrg-reel-flip-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelFlipTransition - card-flip transition between two reel frames.
 */

#include "config.h"
#include "lrg-reel-flip-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include <math.h>

struct _LrgReelFlipTransition
{
    LrgReelTransition          parent_instance;
    LrgReelTransitionDirection direction;
};

G_DEFINE_FINAL_TYPE (LrgReelFlipTransition, lrg_reel_flip_transition,
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
lrg_reel_flip_transition_composite (LrgReelTransition *base,
                                      LrgImageCanvas    *canvas,
                                      GrlImage          *from,
                                      GrlImage          *to,
                                      gdouble            progress)
{
    LrgReelFlipTransition *self;
    GrlImage              *canvas_img;
    GrlRectangle           dst_rect;
    gint                   w;
    gint                   h;
    gboolean               horizontal;
    gfloat                 scale;
    gfloat                 scaled_dim;
    gfloat                 offset;

    self       = LRG_REEL_FLIP_TRANSITION (base);
    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);

    /* Determine axis: LEFT/RIGHT => horizontal; UP/DOWN => vertical. */
    horizontal = (self->direction == LRG_REEL_TRANSITION_DIRECTION_LEFT ||
                  self->direction == LRG_REEL_TRANSITION_DIRECTION_RIGHT);

    /*
     * Clear the canvas to opaque black so that the thin card edge during
     * the mid-flip is not transparent.
     */
    {
        GrlColor bg;
        bg.r = 0;
        bg.g = 0;
        bg.b = 0;
        bg.a = 255;
        lrg_image_canvas_clear (canvas, &bg);
    }

    if (progress < 0.5)
    {
        /*
         * First half: draw @from with a decreasing width/height (scale 1..0).
         * scale = 1 - 2*progress
         */
        scale = (gfloat) (1.0 - 2.0 * progress);

        if (horizontal)
        {
            scaled_dim = (gfloat) w * scale;
            offset     = ((gfloat) w - scaled_dim) * 0.5f;

            dst_rect.x      = offset;
            dst_rect.y      = 0.0f;
            dst_rect.width  = scaled_dim;
            dst_rect.height = (gfloat) h;
        }
        else
        {
            scaled_dim = (gfloat) h * scale;
            offset     = ((gfloat) h - scaled_dim) * 0.5f;

            dst_rect.x      = 0.0f;
            dst_rect.y      = offset;
            dst_rect.width  = (gfloat) w;
            dst_rect.height = scaled_dim;
        }

        /* Skip sub-pixel draws. */
        if (scaled_dim >= 1.0f)
            grl_image_draw_image (canvas_img, from, NULL, &dst_rect, NULL);
    }
    else
    {
        /*
         * Second half: draw @to with an increasing width/height (scale 0..1).
         * scale = 2*progress - 1
         */
        scale = (gfloat) (2.0 * progress - 1.0);

        if (horizontal)
        {
            scaled_dim = (gfloat) w * scale;
            offset     = ((gfloat) w - scaled_dim) * 0.5f;

            dst_rect.x      = offset;
            dst_rect.y      = 0.0f;
            dst_rect.width  = scaled_dim;
            dst_rect.height = (gfloat) h;
        }
        else
        {
            scaled_dim = (gfloat) h * scale;
            offset     = ((gfloat) h - scaled_dim) * 0.5f;

            dst_rect.x      = 0.0f;
            dst_rect.y      = offset;
            dst_rect.width  = (gfloat) w;
            dst_rect.height = scaled_dim;
        }

        if (scale >= 1.0f)
        {
            /* Exactly full size at progress == 1 — use the draw_full path. */
            draw_full (canvas_img, to);
        }
        else if (scaled_dim >= 1.0f)
        {
            grl_image_draw_image (canvas_img, to, NULL, &dst_rect, NULL);
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_flip_transition_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    LrgReelFlipTransition *self = LRG_REEL_FLIP_TRANSITION (object);

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
lrg_reel_flip_transition_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    LrgReelFlipTransition *self = LRG_REEL_FLIP_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        lrg_reel_flip_transition_set_direction (
            self, (LrgReelTransitionDirection) g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_flip_transition_class_init (LrgReelFlipTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_flip_transition_get_property;
    object_class->set_property = lrg_reel_flip_transition_set_property;

    transition_class->composite = lrg_reel_flip_transition_composite;

    /**
     * LrgReelFlipTransition:direction:
     *
     * The axis of the card flip.  LEFT and RIGHT both produce a horizontal
     * flip (the card rotates around its vertical centre line).  UP and DOWN
     * both produce a vertical flip.  The default is
     * %LRG_REEL_TRANSITION_DIRECTION_LEFT.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "Axis of the card flip",
                           LRG_TYPE_REEL_TRANSITION_DIRECTION,
                           LRG_REEL_TRANSITION_DIRECTION_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_flip_transition_init (LrgReelFlipTransition *self)
{
    self->direction = LRG_REEL_TRANSITION_DIRECTION_LEFT;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_flip_transition_new:
 * @direction: axis of the flip; LEFT/RIGHT for horizontal, UP/DOWN for vertical
 *
 * Creates a new #LrgReelFlipTransition.
 *
 * Returns: (transfer full): a new #LrgReelFlipTransition
 *
 * Since: 1.0
 */
LrgReelFlipTransition *
lrg_reel_flip_transition_new (LrgReelTransitionDirection direction)
{
    return g_object_new (LRG_TYPE_REEL_FLIP_TRANSITION,
                         "direction", (gint) direction,
                         NULL);
}

/**
 * lrg_reel_flip_transition_get_direction:
 * @self: an #LrgReelFlipTransition
 *
 * Returns the flip axis direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LrgReelTransitionDirection
lrg_reel_flip_transition_get_direction (LrgReelFlipTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_FLIP_TRANSITION (self),
                          LRG_REEL_TRANSITION_DIRECTION_LEFT);
    return self->direction;
}

/**
 * lrg_reel_flip_transition_set_direction:
 * @self: an #LrgReelFlipTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the flip axis direction.
 *
 * Since: 1.0
 */
void
lrg_reel_flip_transition_set_direction (LrgReelFlipTransition      *self,
                                         LrgReelTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_REEL_FLIP_TRANSITION (self));

    if (self->direction == direction)
        return;

    self->direction = direction;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
}
