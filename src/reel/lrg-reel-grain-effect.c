/* lrg-reel-grain-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-grain-effect.h"
#include "lrg-reel-context.h"

/* Base seed mixed with the per-frame seed to vary grain each frame. */
#define GRAIN_BASE_SEED 0xA5B6C7D8u

struct _LrgReelGrainEffect
{
    LrgReelEffect parent_instance;

    gdouble amount;
    gdouble frequency;
};

G_DEFINE_FINAL_TYPE (LrgReelGrainEffect, lrg_reel_grain_effect,
                     LRG_TYPE_REEL_EFFECT)

enum
{
    PROP_0,
    PROP_AMOUNT,
    PROP_FREQUENCY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * apply vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_grain_effect_apply (LrgReelEffect  *base,
                               GrlImage       *image,
                               LrgReelContext *ctx)
{
    LrgReelGrainEffect *self;
    guint32             seed;

    self = LRG_REEL_GRAIN_EFFECT (base);

    if (self->amount <= 0.0)
        return;

    /* Vary the seed each frame so grain animates. */
    seed = GRAIN_BASE_SEED ^ (guint32) lrg_reel_context_get_frame (ctx);

    grl_image_apply_noise (image,
                            GRL_NOISE_BLEND_OVERLAY,
                            (gfloat) self->amount,
                            (gfloat) self->frequency,
                            seed);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_grain_effect_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgReelGrainEffect *self = LRG_REEL_GRAIN_EFFECT (object);

    switch (prop_id)
    {
    case PROP_AMOUNT:
        g_value_set_double (value, self->amount);
        break;
    case PROP_FREQUENCY:
        g_value_set_double (value, self->frequency);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_grain_effect_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgReelGrainEffect *self = LRG_REEL_GRAIN_EFFECT (object);

    switch (prop_id)
    {
    case PROP_AMOUNT:
        lrg_reel_grain_effect_set_amount (self, g_value_get_double (value));
        break;
    case PROP_FREQUENCY:
        lrg_reel_grain_effect_set_frequency (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_grain_effect_class_init (LrgReelGrainEffectClass *klass)
{
    GObjectClass       *object_class = G_OBJECT_CLASS (klass);
    LrgReelEffectClass *effect_class = LRG_REEL_EFFECT_CLASS (klass);

    object_class->get_property = lrg_reel_grain_effect_get_property;
    object_class->set_property = lrg_reel_grain_effect_set_property;

    effect_class->apply = lrg_reel_grain_effect_apply;

    /**
     * LrgReelGrainEffect:amount:
     *
     * Grain amplitude [0, 1]. 0 disables the effect.
     *
     * Since: 1.0
     */
    properties[PROP_AMOUNT] =
        g_param_spec_double ("amount",
                             "Amount",
                             "Grain amplitude [0, 1]",
                             0.0, 1.0, 0.15,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelGrainEffect:frequency:
     *
     * Grain spatial frequency (>0). Higher values produce finer-grained noise.
     *
     * Since: 1.0
     */
    properties[PROP_FREQUENCY] =
        g_param_spec_double ("frequency",
                             "Frequency",
                             "Grain spatial frequency (>0)",
                             0.001, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_grain_effect_init (LrgReelGrainEffect *self)
{
    self->amount    = 0.15;
    self->frequency = 1.0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_grain_effect_new:
 *
 * Creates a new #LrgReelGrainEffect with default settings.
 *
 * Returns: (transfer full): a new #LrgReelGrainEffect
 *
 * Since: 1.0
 */
LrgReelGrainEffect *
lrg_reel_grain_effect_new (void)
{
    return g_object_new (LRG_TYPE_REEL_GRAIN_EFFECT, NULL);
}

/**
 * lrg_reel_grain_effect_get_amount:
 * @self: a #LrgReelGrainEffect
 *
 * Returns: the grain amplitude
 *
 * Since: 1.0
 */
gdouble
lrg_reel_grain_effect_get_amount (LrgReelGrainEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GRAIN_EFFECT (self), 0.0);

    return self->amount;
}

/**
 * lrg_reel_grain_effect_set_amount:
 * @self: a #LrgReelGrainEffect
 * @amount: grain amplitude [0, 1]
 *
 * Since: 1.0
 */
void
lrg_reel_grain_effect_set_amount (LrgReelGrainEffect *self,
                                   gdouble             amount)
{
    g_return_if_fail (LRG_IS_REEL_GRAIN_EFFECT (self));

    if (self->amount == amount)
        return;

    self->amount = amount;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AMOUNT]);
}

/**
 * lrg_reel_grain_effect_get_frequency:
 * @self: a #LrgReelGrainEffect
 *
 * Returns: the grain spatial frequency
 *
 * Since: 1.0
 */
gdouble
lrg_reel_grain_effect_get_frequency (LrgReelGrainEffect *self)
{
    g_return_val_if_fail (LRG_IS_REEL_GRAIN_EFFECT (self), 1.0);

    return self->frequency;
}

/**
 * lrg_reel_grain_effect_set_frequency:
 * @self: a #LrgReelGrainEffect
 * @frequency: grain spatial frequency (>0)
 *
 * Since: 1.0
 */
void
lrg_reel_grain_effect_set_frequency (LrgReelGrainEffect *self,
                                      gdouble             frequency)
{
    g_return_if_fail (LRG_IS_REEL_GRAIN_EFFECT (self));

    if (self->frequency == frequency)
        return;

    self->frequency = frequency;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREQUENCY]);
}
