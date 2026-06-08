/* lrg-reel-bloom-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-bloom-effect.h"
#include "lrg-reel-context.h"

struct _LrgReelBloomEffect
{
    LrgReelEffect parent_instance;

    guint8  threshold;
    gint    blur_radius;
    gdouble intensity;
};

G_DEFINE_FINAL_TYPE (LrgReelBloomEffect, lrg_reel_bloom_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_THRESHOLD,
    PROP_BLUR_RADIUS,
    PROP_INTENSITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_bloom_effect_apply (LrgReelEffect  *base,
                               GrlImage       *image,
                               LrgReelContext *ctx)
{
    LrgReelBloomEffect *self;

    (void) ctx;

    self = LRG_REEL_BLOOM_EFFECT (base);

    grl_image_apply_bloom (image,
                            self->threshold,
                            self->blur_radius,
                            (gfloat) self->intensity);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_bloom_effect_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgReelBloomEffect *self = LRG_REEL_BLOOM_EFFECT (object);

    switch (prop_id)
    {
    case PROP_THRESHOLD:
        g_value_set_uint (value, self->threshold);
        break;
    case PROP_BLUR_RADIUS:
        g_value_set_int (value, self->blur_radius);
        break;
    case PROP_INTENSITY:
        g_value_set_double (value, self->intensity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_bloom_effect_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgReelBloomEffect *self = LRG_REEL_BLOOM_EFFECT (object);

    switch (prop_id)
    {
    case PROP_THRESHOLD:
        lrg_reel_bloom_effect_set_threshold (self, (guint8) g_value_get_uint (value));
        break;
    case PROP_BLUR_RADIUS:
        lrg_reel_bloom_effect_set_blur_radius (self, g_value_get_int (value));
        break;
    case PROP_INTENSITY:
        lrg_reel_bloom_effect_set_intensity (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_bloom_effect_class_init (LrgReelBloomEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_bloom_effect_get_property;
    object_class->set_property = lrg_reel_bloom_effect_set_property;

    effect_class->apply = lrg_reel_bloom_effect_apply;

    /**
     * LrgReelBloomEffect:threshold:
     *
     * Luminance threshold (0–255). Pixels brighter than this value contribute
     * to the bloom halo.
     *
     * Since: 1.0
     */
    properties[PROP_THRESHOLD] =
        g_param_spec_uint ("threshold",
                           "Threshold",
                           "Luminance threshold for bloom (0-255)",
                           0, 255, 200,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelBloomEffect:blur-radius:
     *
     * Radius of the bloom spread in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BLUR_RADIUS] =
        g_param_spec_int ("blur-radius",
                          "Blur Radius",
                          "Bloom spread radius in pixels",
                          0, G_MAXINT, 8,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelBloomEffect:intensity:
     *
     * Bloom strength multiplier (0 = no bloom, 1 = full).
     *
     * Since: 1.0
     */
    properties[PROP_INTENSITY] =
        g_param_spec_double ("intensity",
                             "Intensity",
                             "Bloom strength multiplier",
                             0.0, G_MAXDOUBLE, 0.8,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_bloom_effect_init (LrgReelBloomEffect *self)
{
    self->threshold   = 200;
    self->blur_radius = 8;
    self->intensity   = 0.8;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_bloom_effect_new:
 * @threshold: luminance threshold (0–255)
 * @blur_radius: bloom blur radius
 * @intensity: bloom strength multiplier
 *
 * Creates a new #LrgReelBloomEffect.
 *
 * Returns: (transfer full): a new #LrgReelBloomEffect
 *
 * Since: 1.0
 */
LrgReelBloomEffect *
lrg_reel_bloom_effect_new (guint8  threshold,
                            gint    blur_radius,
                            gdouble intensity)
{
    return g_object_new (LRG_TYPE_REEL_BLOOM_EFFECT,
                         "threshold",   (guint) threshold,
                         "blur-radius", blur_radius,
                         "intensity",   intensity,
                         NULL);
}

/**
 * lrg_reel_bloom_effect_get_threshold:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the luminance threshold
 *
 * Since: 1.0
 */
guint8
lrg_reel_bloom_effect_get_threshold (LrgReelBloomEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self), 0);

    return self->threshold;
}

/**
 * lrg_reel_bloom_effect_set_threshold:
 * @self: a #LrgReelBloomEffect
 * @threshold: new luminance threshold
 *
 * Since: 1.0
 */
void
lrg_reel_bloom_effect_set_threshold (LrgReelBloomEffect *self,
                                      guint8              threshold)
{
    g_return_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self));

    if (self->threshold == threshold)
        return;

    self->threshold = threshold;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THRESHOLD]);
}

/**
 * lrg_reel_bloom_effect_get_blur_radius:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the bloom blur radius
 *
 * Since: 1.0
 */
gint
lrg_reel_bloom_effect_get_blur_radius (LrgReelBloomEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self), 0);

    return self->blur_radius;
}

/**
 * lrg_reel_bloom_effect_set_blur_radius:
 * @self: a #LrgReelBloomEffect
 * @blur_radius: new blur radius
 *
 * Since: 1.0
 */
void
lrg_reel_bloom_effect_set_blur_radius (LrgReelBloomEffect *self,
                                        gint                blur_radius)
{
    g_return_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self));

    if (self->blur_radius == blur_radius)
        return;

    self->blur_radius = blur_radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLUR_RADIUS]);
}

/**
 * lrg_reel_bloom_effect_get_intensity:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the bloom intensity
 *
 * Since: 1.0
 */
gdouble
lrg_reel_bloom_effect_get_intensity (LrgReelBloomEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self), 0.0);

    return self->intensity;
}

/**
 * lrg_reel_bloom_effect_set_intensity:
 * @self: a #LrgReelBloomEffect
 * @intensity: new intensity value
 *
 * Since: 1.0
 */
void
lrg_reel_bloom_effect_set_intensity (LrgReelBloomEffect *self,
                                      gdouble             intensity)
{
    g_return_if_fail (LRG_IS_REEL_BLOOM_EFFECT (self));

    if (self->intensity == intensity)
        return;

    self->intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}
