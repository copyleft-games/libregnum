/* lrg-reel-blur-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-blur-effect.h"
#include "lrg-reel-context.h"

struct _LrgReelBlurEffect
{
    LrgReelEffect parent_instance;

    gint radius;
};

G_DEFINE_FINAL_TYPE (LrgReelBlurEffect, lrg_reel_blur_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_RADIUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_blur_effect_apply (LrgReelEffect  *base,
                              GrlImage       *image,
                              LrgReelContext *ctx)
{
    LrgReelBlurEffect *self;

    (void) ctx;

    self = LRG_REEL_BLUR_EFFECT (base);

    if (self->radius <= 0)
        return;

    grl_image_blur_box (image, self->radius);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_blur_effect_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgReelBlurEffect *self = LRG_REEL_BLUR_EFFECT (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        g_value_set_int (value, self->radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_blur_effect_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgReelBlurEffect *self = LRG_REEL_BLUR_EFFECT (object);

    switch (prop_id)
    {
    case PROP_RADIUS:
        lrg_reel_blur_effect_set_radius (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_blur_effect_class_init (LrgReelBlurEffectClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_blur_effect_get_property;
    object_class->set_property = lrg_reel_blur_effect_set_property;

    effect_class->apply = lrg_reel_blur_effect_apply;

    /**
     * LrgReelBlurEffect:radius:
     *
     * The box-blur radius in pixels. Values <= 0 disable the effect.
     *
     * Since: 1.0
     */
    properties[PROP_RADIUS] =
        g_param_spec_int ("radius",
                          "Radius",
                          "Box-blur radius in pixels (<=0 is a no-op)",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_blur_effect_init (LrgReelBlurEffect *self)
{
    self->radius = 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_blur_effect_new:
 * @radius: blur radius in pixels (<=0 is a no-op)
 *
 * Creates a new #LrgReelBlurEffect.
 *
 * Returns: (transfer full): a new #LrgReelBlurEffect
 *
 * Since: 1.0
 */
LrgReelBlurEffect *
lrg_reel_blur_effect_new (gint radius)
{
    return g_object_new (LRG_TYPE_REEL_BLUR_EFFECT,
                         "radius", radius,
                         NULL);
}

/**
 * lrg_reel_blur_effect_get_radius:
 * @self: a #LrgReelBlurEffect
 *
 * Returns: the blur radius
 *
 * Since: 1.0
 */
gint
lrg_reel_blur_effect_get_radius (LrgReelBlurEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_BLUR_EFFECT (self), 0);

    return self->radius;
}

/**
 * lrg_reel_blur_effect_set_radius:
 * @self: a #LrgReelBlurEffect
 * @radius: new blur radius
 *
 * Since: 1.0
 */
void
lrg_reel_blur_effect_set_radius (LrgReelBlurEffect *self,
                                  gint               radius)
{
    g_return_if_fail (LRG_IS_REEL_BLUR_EFFECT (self));

    if (self->radius == radius)
        return;

    self->radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}
