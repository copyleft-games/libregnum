/* lrg-bloom.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bloom post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_BLOOM (lrg_bloom_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBloom, lrg_bloom, LRG, BLOOM, LrgPostEffect)

/**
 * lrg_bloom_new:
 *
 * Creates a new bloom effect with default settings.
 *
 * Returns: (transfer full): A new #LrgBloom
 */
LRG_AVAILABLE_IN_ALL
LrgBloom *          lrg_bloom_new                   (void);

/**
 * lrg_bloom_get_threshold:
 * @self: A #LrgBloom
 *
 * Gets the brightness threshold for bloom extraction.
 *
 * Returns: The threshold value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bloom_get_threshold         (LrgBloom       *self);

/**
 * lrg_bloom_set_threshold:
 * @self: A #LrgBloom
 * @threshold: The brightness threshold (0.0 to 10.0)
 *
 * Sets the brightness threshold. Pixels brighter than this
 * will contribute to bloom.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_threshold         (LrgBloom       *self,
                                                     gfloat          threshold);

/**
 * lrg_bloom_get_intensity:
 * @self: A #LrgBloom
 *
 * Gets the bloom intensity.
 *
 * Returns: The intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bloom_get_intensity         (LrgBloom       *self);

/**
 * lrg_bloom_set_intensity:
 * @self: A #LrgBloom
 * @intensity: The intensity multiplier (0.0 to 5.0)
 *
 * Sets how bright the bloom effect appears.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_intensity         (LrgBloom       *self,
                                                     gfloat          intensity);

/**
 * lrg_bloom_get_blur_size:
 * @self: A #LrgBloom
 *
 * Gets the blur kernel size.
 *
 * Returns: The blur size
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bloom_get_blur_size         (LrgBloom       *self);

/**
 * lrg_bloom_set_blur_size:
 * @self: A #LrgBloom
 * @blur_size: The blur kernel size (1.0 to 20.0)
 *
 * Sets the blur radius for the bloom effect.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_blur_size         (LrgBloom       *self,
                                                     gfloat          blur_size);

/**
 * lrg_bloom_get_iterations:
 * @self: A #LrgBloom
 *
 * Gets the number of blur iterations.
 *
 * Returns: The iteration count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_bloom_get_iterations        (LrgBloom       *self);

/**
 * lrg_bloom_set_iterations:
 * @self: A #LrgBloom
 * @iterations: Number of blur passes (1 to 8)
 *
 * Sets the number of blur iterations. More iterations create
 * a smoother, wider bloom effect.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_iterations        (LrgBloom       *self,
                                                     guint           iterations);

/**
 * lrg_bloom_get_soft_knee:
 * @self: A #LrgBloom
 *
 * Gets the soft knee value.
 *
 * Returns: The soft knee value (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bloom_get_soft_knee         (LrgBloom       *self);

/**
 * lrg_bloom_set_soft_knee:
 * @self: A #LrgBloom
 * @soft_knee: The soft knee value (0.0 to 1.0)
 *
 * Sets the soft knee transition around the threshold.
 * 0 = hard cutoff, 1 = smooth gradient.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_soft_knee         (LrgBloom       *self,
                                                     gfloat          soft_knee);

/**
 * lrg_bloom_get_tint:
 * @self: A #LrgBloom
 * @r: (out) (optional): Red component
 * @g: (out) (optional): Green component
 * @b: (out) (optional): Blue component
 *
 * Gets the bloom tint color.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_get_tint              (LrgBloom       *self,
                                                     gfloat         *r,
                                                     gfloat         *g,
                                                     gfloat         *b);

/**
 * lrg_bloom_set_tint:
 * @self: A #LrgBloom
 * @r: Red component (0.0 to 1.0)
 * @g: Green component (0.0 to 1.0)
 * @b: Blue component (0.0 to 1.0)
 *
 * Sets a color tint for the bloom. Default is white (1, 1, 1).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bloom_set_tint              (LrgBloom       *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

G_END_DECLS
