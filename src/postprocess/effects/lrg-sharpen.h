/* lrg-sharpen.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSharpen - a contrast-adaptive sharpening pass: the centre pixel is
 * pushed away from its four neighbours by an amount that shrinks where
 * the neighbourhood is already near black or white (so edges do not ring
 * and noise is not boosted).  Meant for the upscale of a reduced render
 * target; the CPU reference lrg_sharpen_pixel() is the same arithmetic
 * as the shader.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHARPEN (lrg_sharpen_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSharpen, lrg_sharpen, LRG, SHARPEN, LrgPostEffect)

/**
 * lrg_sharpen_new:
 *
 * Creates a sharpening pass with a sharpness of 0.5.
 *
 * Returns: (transfer full): A new #LrgSharpen
 */
LRG_AVAILABLE_IN_ALL
LrgSharpen *lrg_sharpen_new (void);

/**
 * lrg_sharpen_get_sharpness:
 * @self: an #LrgSharpen
 *
 * Returns: the sharpness, 0 (the plain picture) to 1 (strongest)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sharpen_get_sharpness (LrgSharpen *self);

/**
 * lrg_sharpen_set_sharpness:
 * @self: an #LrgSharpen
 * @sharpness: 0 .. 1 (clamped)
 *
 * Sets how hard the centre is pushed from its neighbours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sharpen_set_sharpness (LrgSharpen *self,
                                gfloat      sharpness);

/**
 * lrg_sharpen_is_identity:
 * @self: an #LrgSharpen
 *
 * Returns: whether the pass changes nothing (sharpness or intensity 0)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sharpen_is_identity (LrgSharpen *self);

/**
 * lrg_sharpen_pixel:
 * @sharpness: 0 .. 1
 * @center: (array fixed-size=3): the pixel's RGB, 0 .. 1
 * @up: (array fixed-size=3): the pixel above
 * @down: (array fixed-size=3): the pixel below
 * @left: (array fixed-size=3): the pixel to the left
 * @right: (array fixed-size=3): the pixel to the right
 * @out: (out caller-allocates) (array fixed-size=3): the sharpened pixel
 *
 * The shader's arithmetic on the CPU.  Per channel: the neighbourhood's
 * minimum and maximum give an amplitude sqrt (min (mn, 1 - mx) / mx),
 * the neighbours get the negative weight amplitude x peak (peak from
 * -1/8 at sharpness 0 to -1/5 at 1) and the sum is renormalised, so a
 * flat area is unchanged and a full-range edge is left alone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sharpen_pixel (gfloat        sharpness,
                        const gfloat *center,
                        const gfloat *up,
                        const gfloat *down,
                        const gfloat *left,
                        const gfloat *right,
                        gfloat       *out);

/**
 * lrg_sharpen_get_fragment_shader:
 *
 * Returns: (transfer none): the GLSL 330 fragment source
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_sharpen_get_fragment_shader (void);

/**
 * lrg_sharpen_draw_texture:
 * @self: an #LrgSharpen
 * @texture: the picture
 * @source: the part of @texture to draw (a negative height flips)
 * @dest: where to draw it
 *
 * Draws @source of @texture into @dest through the pass (plainly when the
 * pass is the identity or its shader is unavailable).  The neighbours are
 * one texel of @texture away.
 *
 * Returns: %TRUE when the picture was drawn through the sharpening shader
 *   or needed none
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sharpen_draw_texture (LrgSharpen         *self,
                                   GrlTexture         *texture,
                                   const GrlRectangle *source,
                                   const GrlRectangle *dest);

G_END_DECLS
