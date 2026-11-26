/* lrg-texture-codec.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * CPU texture codecs for GPUs that lack a compressed format: decode S3TC
 * (BC1 / DXT1 with and without 1-bit alpha, BC2 / DXT3, BC3 / DXT5) mip
 * chains to RGBA8, and repack RGBA8 as RGB565, RGBA4444 or RGBA5551 to
 * save memory.  Pure functions on byte buffers: no GL context, every
 * read bounds checked; a buffer too short for the size and mip count
 * asked for fails with %LRG_TEXTURE_ERROR_TRUNCATED, never a read past
 * its end.  lrg-texture-upload.h builds the upload fallback on them.
 *
 * Mip levels are laid out back to back, largest first, each dimension
 * halved and clamped to 1, as raylib and graylib expect.  16-bit
 * output is in host byte order, the layout of GL_UNSIGNED_SHORT_5_6_5,
 * _4_4_4_4 and _5_5_5_1 (raylib's R5G6B5, R4G4B4A4 and R5G5B5A1).
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
 * LRG_TEXTURE_MAX_SIZE:
 *
 * Largest width or height the codecs accept, so a hostile size cannot
 * ask for a gigantic allocation.
 */
#define LRG_TEXTURE_MAX_SIZE (16384)

/**
 * lrg_texture_format_is_s3tc:
 * @format: a pixel format
 *
 * Whether @format is one of the S3TC formats lrg_texture_decode()
 * handles: %GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, _DXT1_RGBA, _DXT3_RGBA
 * or _DXT5_RGBA.
 *
 * Returns: %TRUE for an S3TC format
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_texture_format_is_s3tc (GrlPixelFormat format);

/**
 * lrg_texture_format_is_compressed:
 * @format: a pixel format
 *
 * Returns: %TRUE for any block-compressed format (S3TC, ETC1, ETC2,
 *   PVRTC, ASTC)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_texture_format_is_compressed (GrlPixelFormat format);

/**
 * lrg_texture_get_max_mipmaps:
 * @width: base width in pixels
 * @height: base height in pixels
 *
 * The length of a full mip chain: levels down to and including 1x1.
 *
 * Returns: the level count, 0 when a size is 0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_texture_get_max_mipmaps (guint width,
                             guint height);

/**
 * lrg_texture_get_data_size:
 * @format: a pixel format
 * @width: base width in pixels
 * @height: base height in pixels
 * @mipmaps: number of levels, including the base
 *
 * The bytes a mip chain of @format occupies: whole 4x4 blocks per level
 * for the block formats (8 bytes for DXT1, ETC1, ETC2 RGB; 16 for DXT3,
 * DXT5, ETC2 EAC, ASTC 4x4; 16 per 8x8 for ASTC 8x8), width * height *
 * bytes per pixel for the 8-bit-per-channel and 16-bit packed formats.
 *
 * Returns: the size, or 0 for a size of 0 or above
 *   %LRG_TEXTURE_MAX_SIZE, more levels than lrg_texture_get_max_mipmaps(),
 *   or a format this does not know (PVRTC, floating point)
 */
LRG_AVAILABLE_IN_ALL
gsize
lrg_texture_get_data_size (GrlPixelFormat format,
                           guint          width,
                           guint          height,
                           guint          mipmaps);

/**
 * lrg_texture_decode:
 * @data: (array length=size): the compressed mip chain
 * @size: bytes in @data; more than needed is fine
 * @width: base width in pixels, 1 .. %LRG_TEXTURE_MAX_SIZE
 * @height: base height in pixels, 1 .. %LRG_TEXTURE_MAX_SIZE
 * @format: an S3TC format (lrg_texture_format_is_s3tc())
 * @mipmaps: levels in @data, 1 .. lrg_texture_get_max_mipmaps()
 * @error: return location for a #GError
 *
 * Decodes an S3TC mip chain to tightly packed RGBA8 levels, laid out the
 * same way.  DXT1 RGB decodes the fourth colour of a three-colour block
 * (c0 <= c1) as opaque black, DXT1 RGBA as transparent black; DXT3 and
 * DXT5 colour blocks always use four colours.  Sizes that are not a
 * multiple of 4 decode the visible part of their edge blocks.
 *
 * Returns: (transfer full) (nullable): the RGBA8 chain, or %NULL with
 *   %LRG_TEXTURE_ERROR_INVALID (bad size or mip count),
 *   %LRG_TEXTURE_ERROR_UNSUPPORTED (not S3TC) or
 *   %LRG_TEXTURE_ERROR_TRUNCATED (@data too short)
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_texture_decode (const guint8    *data,
                    gsize            size,
                    guint            width,
                    guint            height,
                    GrlPixelFormat   format,
                    guint            mipmaps,
                    GError         **error);

/**
 * lrg_texture_convert_rgba8:
 * @rgba: (array length=size): a tightly packed RGBA8 mip chain
 * @size: bytes in @rgba; more than needed is fine
 * @width: base width in pixels, 1 .. %LRG_TEXTURE_MAX_SIZE
 * @height: base height in pixels, 1 .. %LRG_TEXTURE_MAX_SIZE
 * @mipmaps: levels in @rgba, 1 .. lrg_texture_get_max_mipmaps()
 * @target: %GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, _R4G4B4A4, _R5G5B5A1,
 *   or _R8G8B8A8 (a copy)
 * @error: return location for a #GError
 *
 * Repacks every level as 16-bit pixels in host byte order.  Each
 * channel is rounded to the nearest representable value
 * (`(v * max + 127) / 255`); RGB565 drops alpha, RGBA5551 keeps alpha
 * of 128 and above as opaque.
 *
 * Returns: (transfer full) (nullable): the converted chain, or %NULL
 *   with %LRG_TEXTURE_ERROR_INVALID, _UNSUPPORTED (another @target) or
 *   _TRUNCATED
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_texture_convert_rgba8 (const guint8    *rgba,
                           gsize            size,
                           guint            width,
                           guint            height,
                           guint            mipmaps,
                           GrlPixelFormat   target,
                           GError         **error);

G_END_DECLS
