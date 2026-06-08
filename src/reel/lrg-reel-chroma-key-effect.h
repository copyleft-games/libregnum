/* lrg-reel-chroma-key-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelChromaKeyEffect - color-based transparency keying for a reel clip layer.
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

#define LRG_TYPE_REEL_CHROMA_KEY_EFFECT (lrg_reel_chroma_key_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelChromaKeyEffect, lrg_reel_chroma_key_effect,
                      LRG, REEL_CHROMA_KEY_EFFECT, LrgReelEffect)

/**
 * lrg_reel_chroma_key_effect_new:
 * @key: (not nullable): the key color to make transparent.
 *
 * Creates a new #LrgReelChromaKeyEffect that removes pixels matching @key.
 * Default threshold is 0.3 and smoothness is 0.1.
 *
 * Returns: (transfer full): a new #LrgReelChromaKeyEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelChromaKeyEffect *
lrg_reel_chroma_key_effect_new (const GrlColor *key);

/**
 * lrg_reel_chroma_key_effect_get_key_color:
 * @self: a #LrgReelChromaKeyEffect
 * @out_color: (out caller-allocates): return location for the key color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_chroma_key_effect_get_key_color (LrgReelChromaKeyEffect *self,
                                           GrlColor               *out_color);

/**
 * lrg_reel_chroma_key_effect_set_key_color:
 * @self: a #LrgReelChromaKeyEffect
 * @key: (not nullable): the new key color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_chroma_key_effect_set_key_color (LrgReelChromaKeyEffect *self,
                                           const GrlColor         *key);

/**
 * lrg_reel_chroma_key_effect_get_threshold:
 * @self: a #LrgReelChromaKeyEffect
 *
 * Returns: the color-distance threshold [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_chroma_key_effect_get_threshold (LrgReelChromaKeyEffect *self);

/**
 * lrg_reel_chroma_key_effect_set_threshold:
 * @self: a #LrgReelChromaKeyEffect
 * @threshold: color-distance threshold [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_chroma_key_effect_set_threshold (LrgReelChromaKeyEffect *self,
                                           gdouble                 threshold);

/**
 * lrg_reel_chroma_key_effect_get_smoothness:
 * @self: a #LrgReelChromaKeyEffect
 *
 * Returns: the edge-softening range [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_chroma_key_effect_get_smoothness (LrgReelChromaKeyEffect *self);

/**
 * lrg_reel_chroma_key_effect_set_smoothness:
 * @self: a #LrgReelChromaKeyEffect
 * @smoothness: edge-softening range [0, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_chroma_key_effect_set_smoothness (LrgReelChromaKeyEffect *self,
                                            gdouble                 smoothness);

G_END_DECLS
