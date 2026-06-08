/* lrg-reel-bloom-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelBloomEffect - highlight bloom filter for a reel clip layer.
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

#define LRG_TYPE_REEL_BLOOM_EFFECT (lrg_reel_bloom_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelBloomEffect, lrg_reel_bloom_effect,
                      LRG, REEL_BLOOM_EFFECT, LrgReelEffect)

/**
 * lrg_reel_bloom_effect_new:
 * @threshold: luminance threshold (0–255); pixels brighter than this glow.
 * @blur_radius: radius of the bloom spread (>0).
 * @intensity: bloom strength multiplier (0..1 typical).
 *
 * Creates a new #LrgReelBloomEffect.
 *
 * Returns: (transfer full): a new #LrgReelBloomEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelBloomEffect *
lrg_reel_bloom_effect_new (guint8  threshold,
                            gint    blur_radius,
                            gdouble intensity);

/**
 * lrg_reel_bloom_effect_get_threshold:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the luminance threshold (0–255)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint8
lrg_reel_bloom_effect_get_threshold (LrgReelBloomEffect *self);

/**
 * lrg_reel_bloom_effect_set_threshold:
 * @self: a #LrgReelBloomEffect
 * @threshold: new luminance threshold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_bloom_effect_set_threshold (LrgReelBloomEffect *self,
                                      guint8              threshold);

/**
 * lrg_reel_bloom_effect_get_blur_radius:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the bloom blur radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_bloom_effect_get_blur_radius (LrgReelBloomEffect *self);

/**
 * lrg_reel_bloom_effect_set_blur_radius:
 * @self: a #LrgReelBloomEffect
 * @blur_radius: new blur radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_bloom_effect_set_blur_radius (LrgReelBloomEffect *self,
                                        gint                blur_radius);

/**
 * lrg_reel_bloom_effect_get_intensity:
 * @self: a #LrgReelBloomEffect
 *
 * Returns: the bloom intensity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_bloom_effect_get_intensity (LrgReelBloomEffect *self);

/**
 * lrg_reel_bloom_effect_set_intensity:
 * @self: a #LrgReelBloomEffect
 * @intensity: new intensity value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_bloom_effect_set_intensity (LrgReelBloomEffect *self,
                                      gdouble             intensity);

G_END_DECLS
