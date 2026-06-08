/* lrg-reel-grain-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelGrainEffect - animated film-grain noise overlay for a reel clip layer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-reel-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_GRAIN_EFFECT (lrg_reel_grain_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelGrainEffect, lrg_reel_grain_effect,
                      LRG, REEL_GRAIN_EFFECT, LrgReelEffect)

/**
 * lrg_reel_grain_effect_new:
 *
 * Creates a new #LrgReelGrainEffect with default settings
 * (amount=0.15, frequency=1.0).
 *
 * Returns: (transfer full): a new #LrgReelGrainEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelGrainEffect *
lrg_reel_grain_effect_new (void);

/**
 * lrg_reel_grain_effect_get_amount:
 * @self: a #LrgReelGrainEffect
 *
 * Returns: the grain amplitude [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_grain_effect_get_amount (LrgReelGrainEffect *self);

/**
 * lrg_reel_grain_effect_set_amount:
 * @self: a #LrgReelGrainEffect
 * @amount: grain amplitude [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_grain_effect_set_amount (LrgReelGrainEffect *self,
                                   gdouble             amount);

/**
 * lrg_reel_grain_effect_get_frequency:
 * @self: a #LrgReelGrainEffect
 *
 * Returns: the grain spatial frequency (default 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_grain_effect_get_frequency (LrgReelGrainEffect *self);

/**
 * lrg_reel_grain_effect_set_frequency:
 * @self: a #LrgReelGrainEffect
 * @frequency: grain spatial frequency (>0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_grain_effect_set_frequency (LrgReelGrainEffect *self,
                                      gdouble             frequency);

G_END_DECLS
