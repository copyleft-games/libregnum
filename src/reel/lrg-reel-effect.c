/* lrg-reel-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-effect.h"

typedef struct
{
    gboolean enabled;
} LrgReelEffectPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgReelEffect, lrg_reel_effect, G_TYPE_OBJECT)

static void
lrg_reel_effect_class_init (LrgReelEffectClass *klass)
{
    klass->apply = NULL;
}

static void
lrg_reel_effect_init (LrgReelEffect *self)
{
    LrgReelEffectPrivate *priv = lrg_reel_effect_get_instance_private (self);

    priv->enabled = TRUE;
}

void
lrg_reel_effect_apply (LrgReelEffect  *self,
                       GrlImage       *image,
                       LrgReelContext *ctx)
{
    LrgReelEffectPrivate *priv;
    LrgReelEffectClass   *klass;

    g_return_if_fail (LRG_IS_REEL_EFFECT (self));
    g_return_if_fail (GRL_IS_IMAGE (image));

    priv = lrg_reel_effect_get_instance_private (self);
    if (!priv->enabled)
        return;

    klass = LRG_REEL_EFFECT_GET_CLASS (self);
    if (klass->apply != NULL)
        klass->apply (self, image, ctx);
}

gboolean
lrg_reel_effect_get_enabled (LrgReelEffect *self)
{
    LrgReelEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_EFFECT (self), FALSE);

    priv = lrg_reel_effect_get_instance_private (self);
    return priv->enabled;
}

void
lrg_reel_effect_set_enabled (LrgReelEffect *self,
                             gboolean       enabled)
{
    LrgReelEffectPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_EFFECT (self));

    priv = lrg_reel_effect_get_instance_private (self);
    priv->enabled = !!enabled;
}
