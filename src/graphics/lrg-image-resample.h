/* lrg-image-resample.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamma-correct, alpha-correct image downsampling.
 *
 * lrg_image_downsample() shrinks an RGBA8 #GrlImage by a whole factor
 * with a box, Mitchell or Lanczos-3 filter.  Unlike grl_image_resize()
 * (raylib's ImageResize(), stb_image_resize on the 8-bit sRGB-encoded
 * values) it decodes sRGB to linear light, premultiplies alpha, filters
 * in floats, clamps the filter's ringing, then unpremultiplies and
 * encodes again: a black / white checkerboard averages to the grey the
 * eye sees (sRGB 188), not 128, and transparent pixels do not bleed
 * their (meaningless) colour into the edges of opaque shapes.
 *
 * This is the resolve step of a supersampled render
 * (lrg_image_canvas_new_supersampled(), lrg_image_canvas_resolve()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_IMAGE_DOWNSAMPLE_MAX_FACTOR:
 *
 * The largest factor lrg_image_downsample() and
 * lrg_image_canvas_new_supersampled() accept.
 *
 * Since: 0.14
 */
#define LRG_IMAGE_DOWNSAMPLE_MAX_FACTOR (16)

/**
 * lrg_image_downsample:
 * @src: the image to shrink
 * @factor: the shrink factor, 1 .. %LRG_IMAGE_DOWNSAMPLE_MAX_FACTOR
 * @filter: the reconstruction filter
 *
 * Shrinks @src by @factor in both directions.  The result is
 * (width / @factor) x (height / @factor) pixels (integer division: a
 * remainder of fewer than @factor columns or rows only reaches the
 * output through the tails of the wider filters).  Output pixel (i, j)
 * is centred on source position ((i + 0.5) @factor, (j + 0.5) @factor);
 * the filter is stretched by @factor and samples beyond the image edge
 * repeat the edge pixel.
 *
 * The arithmetic runs in linear light on premultiplied alpha: sRGB
 * decode, premultiply, separable filter (horizontal, then vertical) in
 * floats, clamp (alpha to 0..1 and colour to 0..alpha, which removes
 * the over- and undershoot of the Mitchell and Lanczos lobes), then
 * unpremultiply and sRGB encode with rounding.  @src is not modified;
 * one that is not RGBA8 is converted on a copy.
 *
 * A @factor of 1 returns a plain copy of @src (grl_image_copy()).
 *
 * Returns: (transfer full) (nullable): a new RGBA8 image, or %NULL when
 *   @src is smaller than @factor in either direction, or @factor is out
 *   of range
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_image_downsample (GrlImage          *src,
                      guint              factor,
                      LrgResampleFilter  filter);

G_END_DECLS
