/* lrg-dds-image.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDdsImage - DirectDraw Surface (DDS) reader and CPU decoder.
 *
 * Reads a DDS file's header and mip chain (legacy DDS_PIXELFORMAT
 * headers and the DX10 extension) and decodes any level to tightly
 * packed RGBA8: DXT1 (BC1, with 1-bit alpha), DXT3 (BC2), DXT5 (BC3),
 * BC7 (BPTC, through the DX10 header), and uncompressed RGB, RGBA,
 * luminance and alpha surfaces described by bit masks.  Written from
 * Microsoft's public DDS and block compression documentation and the
 * Khronos Data Format Specification (BPTC).  No GL context is needed;
 * lrg_dds_image_to_image() wraps a decoded level in a #GrlImage for
 * upload, and lrg_dds_decode_bc7() decodes bare BC7 blocks that come
 * without a DDS header (Unity texture assets, for example).
 *
 * Malformed input (bad magic, impossible header, truncated level 0,
 * unsupported formats) fails with an %LRG_DDS_ERROR; a mip chain cut
 * short keeps the levels that are complete.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_DDS_IMAGE_MAX_SIZE:
 *
 * Largest width or height accepted, so a hostile header cannot ask for
 * a gigantic allocation.
 */
#define LRG_DDS_IMAGE_MAX_SIZE (16384)

#define LRG_TYPE_DDS_IMAGE (lrg_dds_image_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDdsImage, lrg_dds_image, LRG, DDS_IMAGE, GObject)

/**
 * lrg_dds_image_new_from_bytes:
 * @data: the contents of a .dds file
 * @error: return location for a #GError
 *
 * Parses the header and locates every mip level.  The image keeps a
 * reference to @data; nothing is decoded until asked.
 *
 * Returns: (transfer full) (nullable): the image, or %NULL with an
 *   %LRG_DDS_ERROR
 */
LRG_AVAILABLE_IN_ALL
LrgDdsImage *
lrg_dds_image_new_from_bytes (GBytes  *data,
                              GError **error);

/**
 * lrg_dds_image_new_from_file:
 * @path: a .dds file
 * @error: return location for a #GError
 *
 * Maps @path and parses it like lrg_dds_image_new_from_bytes().
 *
 * Returns: (transfer full) (nullable): the image, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgDdsImage *
lrg_dds_image_new_from_file (const gchar  *path,
                             GError      **error);

/**
 * lrg_dds_image_get_width:
 * @self: an #LrgDdsImage
 *
 * Returns: the width of level 0 in pixels
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_dds_image_get_width (LrgDdsImage *self);

/**
 * lrg_dds_image_get_height:
 * @self: an #LrgDdsImage
 *
 * Returns: the height of level 0 in pixels
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_dds_image_get_height (LrgDdsImage *self);

/**
 * lrg_dds_image_get_format:
 * @self: an #LrgDdsImage
 *
 * Returns: the stored pixel format
 */
LRG_AVAILABLE_IN_ALL
LrgDdsFormat
lrg_dds_image_get_format (LrgDdsImage *self);

/**
 * lrg_dds_image_has_alpha:
 * @self: an #LrgDdsImage
 *
 * Whether the format can carry transparency (DXT1 always may, through
 * its three-colour blocks; BC7 always may, through its alpha modes).
 *
 * Returns: %TRUE when decoded pixels may have alpha below 255
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_dds_image_has_alpha (LrgDdsImage *self);

/**
 * lrg_dds_image_get_mip_count:
 * @self: an #LrgDdsImage
 *
 * The number of complete mip levels present (at least 1).
 *
 * Returns: the level count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_dds_image_get_mip_count (LrgDdsImage *self);

/**
 * lrg_dds_image_get_mip_size:
 * @self: an #LrgDdsImage
 * @level: a mip level below lrg_dds_image_get_mip_count()
 * @out_width: (out) (optional): the level's width
 * @out_height: (out) (optional): the level's height
 *
 * Returns: %TRUE when @level exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_dds_image_get_mip_size (LrgDdsImage *self,
                            guint        level,
                            guint       *out_width,
                            guint       *out_height);

/**
 * lrg_dds_image_get_mip_data:
 * @self: an #LrgDdsImage
 * @level: a mip level
 *
 * The level's bytes exactly as stored (compressed blocks for DXT), for
 * uploading compressed textures directly.
 *
 * Returns: (transfer full) (nullable): a slice of the file, or %NULL
 *   when @level does not exist
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_dds_image_get_mip_data (LrgDdsImage *self,
                            guint        level);

/**
 * lrg_dds_image_decode:
 * @self: an #LrgDdsImage
 * @level: a mip level
 * @error: return location for a #GError
 *
 * Decodes one level to RGBA8, rows top to bottom, width * height * 4
 * bytes.  Luminance surfaces give grey pixels; alpha-only surfaces give
 * black pixels with their alpha.
 *
 * Returns: (transfer full) (nullable): the pixels, or %NULL with
 *   %LRG_DDS_ERROR_INVALID when @level does not exist
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_dds_image_decode (LrgDdsImage  *self,
                      guint         level,
                      GError      **error);

/**
 * lrg_dds_image_to_image:
 * @self: an #LrgDdsImage
 * @level: a mip level
 * @error: return location for a #GError
 *
 * Decodes @level into a new RGBA8 #GrlImage (CPU only; upload it with
 * grl_texture_new_from_image()).
 *
 * Returns: (transfer full) (nullable): the image, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_dds_image_to_image (LrgDdsImage  *self,
                        guint         level,
                        GError      **error);

/**
 * LRG_DDS_BC7_BLOCK_SIZE:
 *
 * Bytes in one BC7 block, which holds a 4x4 tile of pixels.
 */
#define LRG_DDS_BC7_BLOCK_SIZE (16)

/**
 * lrg_dds_decode_bc7:
 * @blocks: (array length=length): BC7 blocks, row-major, 16 bytes per
 *   4x4 tile
 * @length: the number of bytes in @blocks
 * @width: the image width in pixels (1 .. %LRG_DDS_IMAGE_MAX_SIZE)
 * @height: the image height in pixels (1 .. %LRG_DDS_IMAGE_MAX_SIZE)
 * @error: return location for a #GError
 *
 * Decodes one BC7 (BPTC, DXGI_FORMAT_BC7_UNORM) surface that has no
 * DDS header.  @blocks holds ceil(@width / 4) * ceil(@height / 4)
 * blocks, rows of blocks top to bottom; tiles that overhang a width or
 * height that is not a multiple of 4 are clipped.  Bytes past the last
 * block are ignored.  All eight modes are decoded; a block in the
 * reserved mode (first byte 0) decodes to transparent black, as the
 * format specification requires.  Colour values are returned as stored
 * (no sRGB conversion).
 *
 * Returns: (transfer full) (nullable): @width * @height * 4 bytes of
 *   RGBA8, rows top to bottom, or %NULL with %LRG_DDS_ERROR_INVALID
 *   for a zero or oversized size and %LRG_DDS_ERROR_TRUNCATED when
 *   @length is short of the blocks the size needs
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_dds_decode_bc7 (const guint8  *blocks,
                    gsize          length,
                    guint          width,
                    guint          height,
                    GError       **error);

G_END_DECLS
