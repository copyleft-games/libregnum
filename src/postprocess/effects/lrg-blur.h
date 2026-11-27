/* lrg-blur.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBlur - a separable Gaussian blur: a horizontal pass into a private
 * target, then a vertical pass mixed with the source by the base
 * intensity. The same kernel runs on the CPU (lrg_post_effect_apply_cpu()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

/**
 * LRG_BLUR_MAX_RADIUS:
 *
 * The largest #LrgBlur:radius, in pixels (the kernel has at most
 * 2 * 32 + 1 taps per pass).
 *
 * Since: 0.14
 */
#define LRG_BLUR_MAX_RADIUS (32)

#define LRG_TYPE_BLUR (lrg_blur_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBlur, lrg_blur, LRG, BLUR, LrgPostEffect)

/**
 * lrg_blur_new:
 *
 * Creates a Gaussian blur with a radius of 4 pixels and an automatic
 * sigma (radius / 2).
 *
 * Returns: (transfer full): A new #LrgBlur
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgBlur *       lrg_blur_new                (void);

/**
 * lrg_blur_get_radius:
 * @self: A #LrgBlur
 *
 * Gets how many pixels the kernel reaches on each side of the centre.
 *
 * Returns: The radius in pixels (0 is the identity)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_blur_get_radius         (LrgBlur        *self);

/**
 * lrg_blur_set_radius:
 * @self: A #LrgBlur
 * @radius: The radius in pixels, 0 to %LRG_BLUR_MAX_RADIUS (clamped)
 *
 * Sets how many pixels the kernel reaches on each side of the centre;
 * each pass takes 2 * @radius + 1 taps. 0 leaves the image unchanged.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void            lrg_blur_set_radius         (LrgBlur        *self,
                                             guint           radius);

/**
 * lrg_blur_get_sigma:
 * @self: A #LrgBlur
 *
 * Gets the Gaussian's standard deviation as set (0: automatic).
 *
 * Returns: The sigma in pixels, or 0 for radius / 2
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_blur_get_sigma          (LrgBlur        *self);

/**
 * lrg_blur_set_sigma:
 * @self: A #LrgBlur
 * @sigma: The standard deviation in pixels, 0 to %LRG_BLUR_MAX_RADIUS
 *   (clamped); 0 picks radius / 2, which keeps the kernel within two
 *   standard deviations
 *
 * Sets the Gaussian's standard deviation. The kernel is cut at
 * #LrgBlur:radius whatever the sigma, and renormalised.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void            lrg_blur_set_sigma          (LrgBlur        *self,
                                             gfloat          sigma);

/**
 * lrg_blur_get_kernel:
 * @self: A #LrgBlur
 * @n_weights: (out): Return location for the number of weights,
 *   2 * radius + 1
 *
 * Gets the one-dimensional kernel both passes use: weight i (from 0) is
 * the tap at offset i - radius, exp (-d^2 / (2 sigma^2)) normalised so
 * the weights sum to 1. With a radius of 0 it is the single weight 1.
 *
 * Returns: (transfer full) (array length=n_weights): The weights; free
 *   with g_free()
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat *        lrg_blur_get_kernel         (LrgBlur        *self,
                                             guint          *n_weights);

/**
 * lrg_blur_get_fragment_shader:
 *
 * Gets the GLSL 330 fragment shader both passes draw with: `texture0` is
 * the pass's input, `original` the effect's source, `texel` the step
 * between taps, `weights` the half kernel (centre first), `radius` the
 * tap count per side and `intensity` the mix with `original` (1 on the
 * first pass).
 *
 * Returns: (transfer none): The shader source
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_blur_get_fragment_shader (void);

G_END_DECLS
