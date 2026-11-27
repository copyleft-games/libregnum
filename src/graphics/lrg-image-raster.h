/* lrg-image-raster.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * CPU rasterisation of textured triangles into a GrlImage.
 *
 * lrg_image_draw_triangle_textured() fills one triangle of a destination
 * RGBA8 image with bilinear samples of a source image, the way a GPU
 * draws a textured triangle: pixel-centre sampling, the top-left fill
 * rule (so the triangles of a mesh tile the plane without gaps or
 * doubly-blended shared edges), affine (perspective-free) texture
 * coordinates, a tint multiply and source-over blending.  It is the CPU
 * back end of lrg_skinned_sprite_draw_to_canvas() and
 * lrg_skeleton2d_draw_to_canvas(), which draw 2D skeletal characters
 * without a window.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * lrg_image_draw_triangle_textured:
 * @dst: the image drawn into; converted to RGBA8 in place if it is not
 *   already
 * @src: (nullable): the texture sampled, or %NULL to fill the triangle
 *   with @tint alone; converted to RGBA8 in place if it is not already
 *   (convert a texture shared between threads beforehand)
 * @xy: (array fixed-size=6): the three corners in @dst pixel
 *   coordinates, `(x0, y0, x1, y1, x2, y2)`; pixel (i, j) covers the
 *   square from (i, j) to (i + 1, j + 1) and its centre is
 *   (i + 0.5, j + 0.5)
 * @uv: (array fixed-size=6) (nullable): the texture coordinate of each
 *   corner, `(u0, v0, u1, v1, u2, v2)`, with (0, 0) the top left and
 *   (1, 1) the bottom right of @src; may be %NULL when @src is %NULL
 * @tint: (nullable): colour multiplied into every sample (alpha
 *   included), or %NULL for opaque white
 *
 * Draws one textured triangle on the CPU.
 *
 * Coverage: a pixel is drawn when its centre lies inside the triangle.
 * A centre exactly on an edge is drawn only when that edge is a top edge
 * (horizontal, with the triangle below it) or a left edge, so two
 * triangles sharing an edge cover each pixel along it exactly once and a
 * mesh has neither seams nor doubly-blended pixels.  Corners are snapped
 * to 1/256 of a pixel and the coverage test is exact integer arithmetic.
 * The winding does not matter; a triangle with no area draws nothing, as
 * does one with a corner that is not finite or lies more than 2^21 pixels
 * from the origin.
 *
 * Sampling: texture coordinates are interpolated linearly across the
 * triangle (no perspective division) and sampled bilinearly at the pixel
 * centre, in premultiplied alpha so transparent texels do not bleed their
 * colour into the edges of opaque ones.  Coordinates are clamped to the
 * image (clamp to edge, no wrapping).
 *
 * Blending: the tinted sample is always composited source-over onto
 * @dst, whatever blend mode @dst is set to, with straight-alpha
 * destination pixels and premultiplied-correct arithmetic in gamma
 * (sRGB byte) space; the blend colour space of @dst is not consulted.
 * The clip rectangle of @dst (grl_image_set_clip_rect()) is honoured; its
 * transform matrix is not: @xy are final pixel coordinates (callers such
 * as lrg_skinned_sprite_draw_to_canvas() apply the canvas transform
 * themselves).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_draw_triangle_textured (GrlImage       *dst,
                                       GrlImage       *src,
                                       const gfloat   *xy,
                                       const gfloat   *uv,
                                       const GrlColor *tint);

G_END_DECLS
