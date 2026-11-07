/* lrg-hdr-image.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrImage - Radiance RGBE (.hdr) reader and writer.
 *
 * A linear floating-point RGB image (three floats per pixel, rows from
 * the top) with a reader and a writer for the Radiance picture format
 * (.hdr, "RGBE"): flat scanlines, the old run-length scheme and the
 * adaptive ("new") run-length scheme are read; the writer emits the
 * adaptive scheme, or flat scanlines for widths it cannot encode.  The
 * reader understands the "-Y H +X W" (top-down) and "+Y H +X W"
 * (bottom-up) orientations and folds the header's EXPOSURE into the
 * pixels.  raylib's own HDR loader is disabled in our build, so the
 * environment (#LrgEnvironment) loads its equirectangular skies here.
 *
 * Everything is headless: no GL context is needed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_HDR_IMAGE_MAX_SIZE:
 *
 * Largest width or height accepted by the reader, so a hostile header
 * cannot ask for a gigantic allocation.
 */
#define LRG_HDR_IMAGE_MAX_SIZE (16384)

/**
 * LRG_HDR_IMAGE_ERROR:
 *
 * Error domain of #LrgHdrImage.  Errors carry an #LrgHdrImageError code.
 */
#define LRG_HDR_IMAGE_ERROR (lrg_hdr_image_error_quark ())

/**
 * LrgHdrImageError:
 * @LRG_HDR_IMAGE_ERROR_FAILED: generic failure
 * @LRG_HDR_IMAGE_ERROR_IO: the file could not be read or written
 * @LRG_HDR_IMAGE_ERROR_FORMAT: not a Radiance picture, or an unsupported
 *   variant (XYZE pixels, rotated orientations)
 * @LRG_HDR_IMAGE_ERROR_TRUNCATED: the pixel data ends early or a
 *   run-length record overflows its scanline
 * @LRG_HDR_IMAGE_ERROR_SIZE: zero or larger than %LRG_HDR_IMAGE_MAX_SIZE
 *
 * Error codes of %LRG_HDR_IMAGE_ERROR.
 */
typedef enum
{
	LRG_HDR_IMAGE_ERROR_FAILED,
	LRG_HDR_IMAGE_ERROR_IO,
	LRG_HDR_IMAGE_ERROR_FORMAT,
	LRG_HDR_IMAGE_ERROR_TRUNCATED,
	LRG_HDR_IMAGE_ERROR_SIZE
} LrgHdrImageError;

#define LRG_TYPE_HDR_IMAGE_ERROR (lrg_hdr_image_error_get_type ())

/**
 * lrg_hdr_image_error_get_type:
 *
 * Returns: the #GType of #LrgHdrImageError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_hdr_image_error_get_type (void) G_GNUC_CONST;

/**
 * lrg_hdr_image_error_quark:
 *
 * Returns: the error quark of %LRG_HDR_IMAGE_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_hdr_image_error_quark (void);

#define LRG_TYPE_HDR_IMAGE (lrg_hdr_image_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHdrImage, lrg_hdr_image, LRG, HDR_IMAGE, GObject)

/**
 * lrg_hdr_image_new:
 * @width: width in pixels (1 .. %LRG_HDR_IMAGE_MAX_SIZE)
 * @height: height in pixels (1 .. %LRG_HDR_IMAGE_MAX_SIZE)
 *
 * Creates a black image.  Sizes out of range are clamped into it.
 *
 * Returns: (transfer full): a new #LrgHdrImage
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_image_new (guint width,
                   guint height);

/**
 * lrg_hdr_image_new_from_data:
 * @width: width in pixels
 * @height: height in pixels
 * @rgb: (array) (element-type gfloat): @width x @height x 3 linear floats,
 *   rows from the top
 *
 * Creates an image holding a copy of @rgb.
 *
 * Returns: (transfer full): a new #LrgHdrImage
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_image_new_from_data (guint         width,
                             guint         height,
                             const gfloat *rgb);

/**
 * lrg_hdr_image_new_from_bytes:
 * @data: the contents of a .hdr file
 * @error: return location for a #GError
 *
 * Decodes a Radiance picture.  The EXPOSURE lines of the header are
 * multiplied together and divided out, so the pixels are the radiance
 * the picture describes (lrg_hdr_image_get_exposure() keeps the product).
 *
 * Returns: (transfer full) (nullable): the image, or %NULL with an
 *   %LRG_HDR_IMAGE_ERROR
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_image_new_from_bytes (GBytes  *data,
                              GError **error);

/**
 * lrg_hdr_image_new_from_file:
 * @path: a .hdr file
 * @error: return location for a #GError
 *
 * Reads @path and decodes it like lrg_hdr_image_new_from_bytes().
 *
 * Returns: (transfer full) (nullable): the image, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_image_new_from_file (const gchar  *path,
                             GError      **error);

/**
 * lrg_hdr_image_get_width:
 * @self: an #LrgHdrImage
 *
 * Returns: the width in pixels
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hdr_image_get_width (LrgHdrImage *self);

/**
 * lrg_hdr_image_get_height:
 * @self: an #LrgHdrImage
 *
 * Returns: the height in pixels
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_hdr_image_get_height (LrgHdrImage *self);

/**
 * lrg_hdr_image_get_exposure:
 * @self: an #LrgHdrImage
 *
 * The product of the EXPOSURE header lines of the file the image was
 * read from (1 when there were none, or for images made in memory).
 * The pixels already have it divided out.
 *
 * Returns: the file's exposure factor
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_hdr_image_get_exposure (LrgHdrImage *self);

/**
 * lrg_hdr_image_get_data:
 * @self: an #LrgHdrImage
 * @n_floats: (out) (optional): the number of floats (width x height x 3)
 *
 * The pixels: linear RGB floats, rows from the top.  The pointer stays
 * valid as long as @self lives; writes through it are allowed.
 *
 * Returns: (transfer none) (array length=n_floats): the pixel floats
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_hdr_image_get_data (LrgHdrImage *self,
                        gsize       *n_floats);

/**
 * lrg_hdr_image_get_pixel:
 * @self: an #LrgHdrImage
 * @x: column (clamped into the image)
 * @y: row from the top (clamped into the image)
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the pixel
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_get_pixel (LrgHdrImage *self,
                         guint        x,
                         guint        y,
                         gfloat      *out_rgb);

/**
 * lrg_hdr_image_set_pixel:
 * @self: an #LrgHdrImage
 * @x: column (ignored when outside the image)
 * @y: row from the top (ignored when outside the image)
 * @rgb: (array fixed-size=3): the new pixel
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_set_pixel (LrgHdrImage  *self,
                         guint         x,
                         guint         y,
                         const gfloat *rgb);

/**
 * lrg_hdr_image_sample:
 * @self: an #LrgHdrImage
 * @u: horizontal coordinate, 0 .. 1 left to right; wraps around
 * @v: vertical coordinate, 0 .. 1 top to bottom; clamped
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the colour
 *
 * Bilinear lookup with pixel centres at (i + 0.5) / size, wrapping
 * horizontally like an equirectangular panorama.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_sample (LrgHdrImage *self,
                      gfloat       u,
                      gfloat       v,
                      gfloat      *out_rgb);

/**
 * lrg_hdr_image_direction_to_uv:
 * @direction: (array fixed-size=3): a direction (need not be normalized)
 * @out_u: (out): horizontal panorama coordinate, 0 .. 1
 * @out_v: (out): vertical panorama coordinate, 0 (up) .. 1 (down)
 *
 * The equirectangular mapping used by the engine: the centre of the
 * panorama (u = 0.5) looks down -Z (the camera's forward), u grows
 * toward +X, v = acos (y) / pi.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_direction_to_uv (const gfloat *direction,
                               gfloat       *out_u,
                               gfloat       *out_v);

/**
 * lrg_hdr_image_sample_direction:
 * @self: an #LrgHdrImage holding an equirectangular panorama
 * @direction: (array fixed-size=3): a world direction
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the radiance
 *
 * Looks @direction up through lrg_hdr_image_direction_to_uv() and
 * lrg_hdr_image_sample().
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_sample_direction (LrgHdrImage  *self,
                                const gfloat *direction,
                                gfloat       *out_rgb);

/**
 * lrg_hdr_image_get_average:
 * @self: an #LrgHdrImage
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the mean pixel
 *
 * The plain (not solid-angle weighted) mean of every pixel.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_get_average (LrgHdrImage *self,
                           gfloat      *out_rgb);

/**
 * lrg_hdr_image_scale:
 * @self: an #LrgHdrImage
 * @factor: multiplier (>= 0)
 *
 * Multiplies every pixel by @factor.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_image_scale (LrgHdrImage *self,
                     gfloat       factor);

/**
 * lrg_hdr_image_resize:
 * @self: an #LrgHdrImage
 * @width: new width (1 .. %LRG_HDR_IMAGE_MAX_SIZE)
 * @height: new height (1 .. %LRG_HDR_IMAGE_MAX_SIZE)
 *
 * A resampled copy: box-filtered when shrinking by whole factors,
 * bilinear otherwise.  Useful to make a small panorama for a fast bake.
 *
 * Returns: (transfer full): the new image
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_hdr_image_resize (LrgHdrImage *self,
                      guint        width,
                      guint        height);

/**
 * lrg_hdr_image_save_to_bytes:
 * @self: an #LrgHdrImage
 *
 * Encodes the image as a Radiance picture: a "#?RADIANCE" header with
 * FORMAT=32-bit_rle_rgbe, the "-Y H +X W" orientation and run-length
 * encoded scanlines (flat for widths below 8 or above 32767).  Negative
 * and non-finite components are written as 0.  RGBE keeps 8 bits of
 * mantissa per channel against a shared exponent, so a round trip is
 * exact to about 1/256 of the brightest channel of each pixel.
 *
 * Returns: (transfer full): the file contents
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_hdr_image_save_to_bytes (LrgHdrImage *self);

/**
 * lrg_hdr_image_save_to_file:
 * @self: an #LrgHdrImage
 * @path: destination .hdr file
 * @error: return location for a #GError
 *
 * Writes lrg_hdr_image_save_to_bytes() to @path (atomically replaced).
 *
 * Returns: %TRUE on success, else %FALSE with an %LRG_HDR_IMAGE_ERROR_IO
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hdr_image_save_to_file (LrgHdrImage  *self,
                            const gchar  *path,
                            GError      **error);

/**
 * lrg_hdr_rgbe_encode:
 * @rgb: (array fixed-size=3): a linear colour
 * @out_rgbe: (out caller-allocates) (array fixed-size=4): the four RGBE
 *   bytes (mantissas and the shared exponent biased by 128)
 *
 * Encodes one pixel the way the writer does (Ward's float2rgbe).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_rgbe_encode (const gfloat *rgb,
                     guint8       *out_rgbe);

/**
 * lrg_hdr_rgbe_decode:
 * @rgbe: (array fixed-size=4): four RGBE bytes
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the colour
 *
 * Decodes one pixel the way the reader does: a zero exponent byte is
 * black, otherwise each non-zero mantissa m gives (m + 0.5) x
 * 2^(e - 136) and a zero mantissa 0.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hdr_rgbe_decode (const guint8 *rgbe,
                     gfloat       *out_rgb);

G_END_DECLS
