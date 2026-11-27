/* lrg-hdr-canvas.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrCanvas - a float, linear-light, premultiplied RGBA canvas.
 *
 * The working surface of an offline "fragment shader" render: shading
 * code writes unbounded linear radiance into it (directly, by source-over
 * blending, by adding light, or through a supersampled per-pixel shade
 * callback), and lrg_hdr_canvas_to_image() tone-maps the result with the
 * same curves as #LrgTonemap, encodes sRGB and dithers to an RGBA8
 * #GrlImage.  Converts to and from #LrgHdrImage for .hdr files.
 * Everything is headless: no GL context is needed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-hdr-image.h"
#include "../postprocess/effects/lrg-tonemap.h"

G_BEGIN_DECLS

/**
 * LRG_HDR_CANVAS_MAX_SIZE:
 *
 * Largest width or height of an #LrgHdrCanvas.
 *
 * Since: 0.14
 */
#define LRG_HDR_CANVAS_MAX_SIZE (16384)

/**
 * LRG_HDR_CANVAS_MAX_SAMPLES:
 *
 * Largest per-axis sample count of lrg_hdr_canvas_shade() (a 16 x 16
 * grid, 256 calls per pixel).
 *
 * Since: 0.14
 */
#define LRG_HDR_CANVAS_MAX_SAMPLES (16)

/**
 * LRG_HDR_CANVAS_WHITE_POINT:
 *
 * The white point lrg_hdr_canvas_to_image() hands the Reinhard and
 * filmic curves: the same default as #LrgTonemap:white-point.
 *
 * Since: 0.14
 */
#define LRG_HDR_CANVAS_WHITE_POINT (4.0f)

/**
 * LrgHdrShadeFunc:
 * @x: sample position, pixels from the left edge (pixel i spans i .. i + 1)
 * @y: sample position, pixels from the top edge
 * @out_rgba: (out caller-allocates) (array fixed-size=4): the colour at
 *   (@x, @y): linear light, straight (not premultiplied) alpha; colour
 *   may exceed 1, alpha is clamped into 0..1
 * @user_data: data given to lrg_hdr_canvas_shade()
 *
 * A per-sample shader for lrg_hdr_canvas_shade(): the CPU counterpart
 * of a fragment shader.  It receives continuous coordinates, so the
 * shader evaluates edges (distance fields, analytic shapes) at the exact
 * sample position.  Return an alpha of 0 to leave the canvas unchanged.
 *
 * Since: 0.14
 */
typedef void (*LrgHdrShadeFunc) (gfloat   x,
                                 gfloat   y,
                                 gfloat  *out_rgba,
                                 gpointer user_data);

#define LRG_TYPE_HDR_CANVAS (lrg_hdr_canvas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHdrCanvas, lrg_hdr_canvas, LRG, HDR_CANVAS, GObject)

/**
 * lrg_hdr_canvas_new:
 * @width: width in pixels, 1 .. %LRG_HDR_CANVAS_MAX_SIZE
 * @height: height in pixels, 1 .. %LRG_HDR_CANVAS_MAX_SIZE
 *
 * Creates a canvas cleared to transparent black (all four channels 0).
 *
 * Returns: (transfer full) (nullable): a new #LrgHdrCanvas, or %NULL
 *   for a size out of range
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgHdrCanvas *
lrg_hdr_canvas_new (guint width,
                    guint height);

/**
 * lrg_hdr_canvas_new_from_hdr_image:
 * @image: an #LrgHdrImage
 *
 * Creates a canvas of the size of @image holding its pixels, fully
 * opaque (alpha 1).
 *
 * Returns: (transfer full): a new #LrgHdrCanvas
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgHdrCanvas *
lrg_hdr_canvas_new_from_hdr_image (LrgHdrImage *image);

/**
 * lrg_hdr_canvas_get_width:
 * @self: an #LrgHdrCanvas
 *
 * Returns: the width in pixels
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hdr_canvas_get_width (LrgHdrCanvas *self);

/**
 * lrg_hdr_canvas_get_height:
 * @self: an #LrgHdrCanvas
 *
 * Returns: the height in pixels
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hdr_canvas_get_height (LrgHdrCanvas *self);

/**
 * lrg_hdr_canvas_get_data:
 * @self: an #LrgHdrCanvas
 * @n_floats: (out) (optional): the number of floats (width x height x 4)
 *
 * The storage: width x height pixels, rows from the top, four floats
 * each (R, G, B, A), linear light with *premultiplied* alpha (colour
 * already multiplied by alpha).  The pointer stays valid as long as
 * @self lives; writes through it are allowed and must stay premultiplied.
 *
 * Returns: (transfer none) (array length=n_floats): the pixel floats
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_hdr_canvas_get_data (LrgHdrCanvas *self,
                         gsize        *n_floats);

/**
 * lrg_hdr_canvas_clear:
 * @self: an #LrgHdrCanvas
 * @r: red, linear
 * @g: green, linear
 * @b: blue, linear
 * @a: alpha (straight), clamped into 0..1
 *
 * Fills every pixel with one straight-alpha colour.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_clear (LrgHdrCanvas *self,
                      gfloat        r,
                      gfloat        g,
                      gfloat        b,
                      gfloat        a);

/**
 * lrg_hdr_canvas_set_pixel:
 * @self: an #LrgHdrCanvas
 * @x: column (ignored when outside the canvas)
 * @y: row from the top (ignored when outside the canvas)
 * @r: red, linear
 * @g: green, linear
 * @b: blue, linear
 * @a: alpha (straight), clamped into 0..1
 *
 * Replaces one pixel with a straight-alpha colour (stored premultiplied).
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_set_pixel (LrgHdrCanvas *self,
                          gint          x,
                          gint          y,
                          gfloat        r,
                          gfloat        g,
                          gfloat        b,
                          gfloat        a);

/**
 * lrg_hdr_canvas_get_pixel:
 * @self: an #LrgHdrCanvas
 * @x: column
 * @y: row from the top
 * @out_rgba: (out caller-allocates) (array fixed-size=4): the pixel,
 *   linear light, straight alpha (colour divided by alpha; 0 where alpha
 *   is 0); all zero outside the canvas
 *
 * Reads one pixel back as a straight-alpha colour.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_get_pixel (LrgHdrCanvas *self,
                          gint          x,
                          gint          y,
                          gfloat       *out_rgba);

/**
 * lrg_hdr_canvas_blend_over:
 * @self: an #LrgHdrCanvas
 * @x: column (ignored when outside the canvas)
 * @y: row from the top (ignored when outside the canvas)
 * @r: red, linear
 * @g: green, linear
 * @b: blue, linear
 * @a: alpha (straight), clamped into 0..1
 *
 * Composites a straight-alpha colour over one pixel (Porter-Duff source
 * over, in premultiplied form): dst = src x a + dst x (1 - a), alpha
 * included.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_blend_over (LrgHdrCanvas *self,
                           gint          x,
                           gint          y,
                           gfloat        r,
                           gfloat        g,
                           gfloat        b,
                           gfloat        a);

/**
 * lrg_hdr_canvas_add:
 * @self: an #LrgHdrCanvas
 * @x: column (ignored when outside the canvas)
 * @y: row from the top (ignored when outside the canvas)
 * @r: red radiance to add, linear
 * @g: green radiance to add, linear
 * @b: blue radiance to add, linear
 *
 * Adds light to one pixel's stored (premultiplied) colour; alpha is
 * unchanged.  Use it for glows, highlights and light accumulation over
 * opaque content: light added where alpha is 0 stays in the storage but
 * is dropped by lrg_hdr_canvas_get_pixel() and
 * lrg_hdr_canvas_to_image().
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_add (LrgHdrCanvas *self,
                    gint          x,
                    gint          y,
                    gfloat        r,
                    gfloat        g,
                    gfloat        b);

/**
 * lrg_hdr_canvas_shade:
 * @self: an #LrgHdrCanvas
 * @func: (scope call) (closure user_data): the per-sample shader
 * @user_data: data passed to @func
 * @samples: samples per axis: 1 samples each pixel centre, n an n x n
 *   grid; 0 counts as 1, more than %LRG_HDR_CANVAS_MAX_SAMPLES as that
 *
 * Runs @func for every pixel at @samples x @samples stratified positions
 * (pixel i, j is sampled at (i + (k + 0.5) / n, j + (l + 0.5) / n) for
 * k, l in 0 .. n - 1), averages the samples in premultiplied form (so a
 * hard edge covering a quarter of the samples gives a quarter of the
 * coverage, with the colour of the covered part), and composites the
 * average over the pixel like lrg_hdr_canvas_blend_over().  @func is
 * called on the calling thread, rows from the top.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_canvas_shade (LrgHdrCanvas    *self,
                      LrgHdrShadeFunc  func,
                      gpointer         user_data,
                      guint            samples);

/**
 * lrg_hdr_canvas_to_image:
 * @self: an #LrgHdrCanvas
 * @op: the tone-mapping curve
 * @exposure: exposure in stops (EV): colour is multiplied by 2^@exposure
 *   before the curve, as #LrgTonemap:exposure
 * @dither: %TRUE to add a per-pixel hash dither of under half a step
 *   before quantising colour, which breaks up banding in smooth gradients
 *   (each value moves by at most one 8-bit step)
 *
 * Tone-maps the canvas into a new RGBA8 #GrlImage.  Per pixel: colour is
 * unpremultiplied, exposed, mapped with lrg_tonemap_operator_apply_rgb()
 * (white point %LRG_HDR_CANVAS_WHITE_POINT), encoded with the exact sRGB
 * curve (IEC 61966-2-1, not gamma 2.2) and quantised; alpha is written
 * straight and never dithered.
 *
 * Returns: (transfer full): a new #GrlImage of the canvas size
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_hdr_canvas_to_image (LrgHdrCanvas       *self,
                         LrgTonemapOperator  op,
                         gfloat              exposure,
                         gboolean            dither);

/**
 * lrg_hdr_canvas_to_hdr_image:
 * @self: an #LrgHdrCanvas
 *
 * Copies the canvas into an #LrgHdrImage (RGB, no alpha).  The stored
 * premultiplied colour is written, which is the canvas composited over
 * black; an opaque canvas round-trips exactly.
 *
 * Returns: (transfer full): a new #LrgHdrImage
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_canvas_to_hdr_image (LrgHdrCanvas *self);

G_END_DECLS
