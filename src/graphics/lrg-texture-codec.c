/* lrg-texture-codec.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * S3TC decoding and RGBA8 repacking, written from the
 * EXT_texture_compression_s3tc extension specification (Khronos
 * registry) and the Direct3D block compression description (BC1 .. BC3).
 * The block decoder is shared with LrgDdsImage
 * (lrg-texture-codec-private.h).
 *
 * Bounds: every public entry point validates the size, computes the
 * chain's byte count with lrg_texture_get_data_size() (gsize arithmetic
 * on dimensions capped at LRG_TEXTURE_MAX_SIZE, so it cannot overflow)
 * and refuses a shorter buffer before touching it.
 */

#include "config.h"

#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-texture-codec.h"
#include "graphics/lrg-texture-codec-private.h"

/* ========================================================================== */
/* Block decoding                                                             */
/* ========================================================================== */

/* a 5:6:5 colour widened to 8 bits per channel (bit replication) */
static void
expand_565 (guint16  v,
            guint8  *out)
{
	guint r = (v >> 11) & 0x1f;
	guint g = (v >> 5) & 0x3f;
	guint b = v & 0x1f;

	out[0] = (guint8)((r << 3) | (r >> 2));
	out[1] = (guint8)((g << 2) | (g >> 4));
	out[2] = (guint8)((b << 3) | (b >> 2));
	out[3] = 255;
}

/*
 * decode_color_block:
 * The 8-byte colour block (two 5:6:5 endpoints, then 2-bit indices,
 * texel 0 in the low bits) into a 4x4 RGBA tile.  @three_colour enables
 * BC1's mode for c0 <= c1: the third entry is the midpoint and the
 * fourth black, transparent when @transparent is set.  BC2 and BC3
 * colour blocks always use the four-colour mode.
 */
static void
decode_color_block (const guint8 *block,
                    gboolean      three_colour,
                    gboolean      transparent,
                    guint8        tile[16][4])
{
	guint8 palette[4][4];
	guint16 c0 = (guint16)(block[0] | (block[1] << 8));
	guint16 c1 = (guint16)(block[2] | (block[3] << 8));
	guint32 bits = (guint32)block[4] | ((guint32)block[5] << 8) |
	               ((guint32)block[6] << 16) | ((guint32)block[7] << 24);
	guint i;
	guint k;

	expand_565 (c0, palette[0]);
	expand_565 (c1, palette[1]);
	if (!three_colour || c0 > c1)
	{
		for (k = 0; k < 3; k++)
		{
			palette[2][k] = (guint8)((2 * palette[0][k] + palette[1][k]) / 3);
			palette[3][k] = (guint8)((palette[0][k] + 2 * palette[1][k]) / 3);
		}
		palette[2][3] = 255;
		palette[3][3] = 255;
	}
	else
	{
		for (k = 0; k < 3; k++)
			palette[2][k] = (guint8)((palette[0][k] + palette[1][k]) / 2);
		palette[2][3] = 255;
		palette[3][0] = 0;
		palette[3][1] = 0;
		palette[3][2] = 0;
		palette[3][3] = transparent ? 0 : 255;
	}

	for (i = 0; i < 16; i++)
		memcpy (tile[i], palette[(bits >> (2 * i)) & 3], 4);
}

/* BC2: sixteen explicit 4-bit alphas, texel 0 in the low nibble */
static void
decode_explicit_alpha (const guint8 *block,
                       guint8        tile[16][4])
{
	guint i;

	for (i = 0; i < 16; i++)
	{
		guint8 nibble = (guint8)((block[i / 2] >> ((i % 2) * 4)) & 0xf);

		tile[i][3] = (guint8)(nibble * 17);
	}
}

/* BC3: two alpha endpoints and sixteen 3-bit indices into 8 alphas
 * (6 interpolated when a0 > a1, else 4 plus 0 and 255) */
static void
decode_interpolated_alpha (const guint8 *block,
                           guint8        tile[16][4])
{
	guint8 alpha[8];
	guint64 bits = 0;
	guint i;

	alpha[0] = block[0];
	alpha[1] = block[1];
	if (alpha[0] > alpha[1])
	{
		for (i = 1; i < 7; i++)
			alpha[i + 1] = (guint8)(((7 - i) * alpha[0] + i * alpha[1]) / 7);
	}
	else
	{
		for (i = 1; i < 5; i++)
			alpha[i + 1] = (guint8)(((5 - i) * alpha[0] + i * alpha[1]) / 5);
		alpha[6] = 0;
		alpha[7] = 255;
	}

	for (i = 0; i < 6; i++)
		bits |= (guint64)block[2 + i] << (8 * i);
	for (i = 0; i < 16; i++)
		tile[i][3] = alpha[(bits >> (3 * i)) & 7];
}

guint
lrg_texture_bc_block_size (LrgBcKind kind)
{
	return (kind == LRG_BC_KIND_BC1_RGB || kind == LRG_BC_KIND_BC1_RGBA) ? 8 : 16;
}

void
lrg_texture_decode_bc_level (LrgBcKind     kind,
                             const guint8 *src,
                             guint         width,
                             guint         height,
                             guint8       *dst)
{
	guint blocks_x = (width + 3) / 4;
	guint blocks_y = (height + 3) / 4;
	guint block_size = lrg_texture_bc_block_size (kind);
	guint bx;
	guint by;

	for (by = 0; by < blocks_y; by++)
	{
		for (bx = 0; bx < blocks_x; bx++)
		{
			const guint8 *block = src + ((gsize)by * blocks_x + bx) * block_size;
			guint8 tile[16][4];
			guint px;
			guint py;

			switch (kind)
			{
			case LRG_BC_KIND_BC1_RGB:
				decode_color_block (block, TRUE, FALSE, tile);
				break;
			case LRG_BC_KIND_BC1_RGBA:
				decode_color_block (block, TRUE, TRUE, tile);
				break;
			case LRG_BC_KIND_BC2:
				decode_color_block (block + 8, FALSE, FALSE, tile);
				decode_explicit_alpha (block, tile);
				break;
			case LRG_BC_KIND_BC3:
			default:
				decode_color_block (block + 8, FALSE, FALSE, tile);
				decode_interpolated_alpha (block, tile);
				break;
			}

			/* blocks overhanging a non-multiple-of-4 edge are clipped */
			for (py = 0; py < 4; py++)
			{
				guint y = by * 4 + py;

				if (y >= height)
					break;
				for (px = 0; px < 4; px++)
				{
					guint x = bx * 4 + px;

					if (x >= width)
						break;
					memcpy (dst + ((gsize)y * width + x) * 4, tile[py * 4 + px], 4);
				}
			}
		}
	}
}

/* ========================================================================== */
/* Formats and sizes                                                          */
/* ========================================================================== */

gboolean
lrg_texture_format_is_s3tc (GrlPixelFormat format)
{
	return format == GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB ||
	       format == GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA ||
	       format == GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA ||
	       format == GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA;
}

gboolean
lrg_texture_format_is_compressed (GrlPixelFormat format)
{
	return format >= GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB &&
	       format <= GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA;
}

guint
lrg_texture_get_max_mipmaps (guint width,
                             guint height)
{
	guint largest = MAX (width, height);
	guint levels = 0;

	if (width == 0 || height == 0)
		return 0;
	while (largest > 0)
	{
		levels++;
		largest >>= 1;
	}
	return levels;
}

/* the bytes of one level, 0 for an unknown format */
static gsize
level_size (GrlPixelFormat format,
            guint          width,
            guint          height)
{
	gsize blocks4 = (((gsize)width + 3) / 4) * (((gsize)height + 3) / 4);

	switch (format)
	{
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
		return (gsize)width * height;
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
		return (gsize)width * height * 2;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
		return (gsize)width * height * 3;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
		return (gsize)width * height * 4;
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB:
		return blocks4 * 8;
	case GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:
		return blocks4 * 16;
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:
		return (((gsize)width + 7) / 8) * (((gsize)height + 7) / 8) * 16;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGBA:
	default:
		return 0;
	}
}

gsize
lrg_texture_get_data_size (GrlPixelFormat format,
                           guint          width,
                           guint          height,
                           guint          mipmaps)
{
	gsize total = 0;
	guint level;

	if (width == 0 || height == 0 || width > LRG_TEXTURE_MAX_SIZE ||
	    height > LRG_TEXTURE_MAX_SIZE || mipmaps == 0 ||
	    mipmaps > lrg_texture_get_max_mipmaps (width, height))
		return 0;

	for (level = 0; level < mipmaps; level++)
	{
		gsize size = level_size (format, width, height);

		if (size == 0)
			return 0;
		total += size;
		width = MAX (1u, width / 2);
		height = MAX (1u, height / 2);
	}
	return total;
}

/*
 * check_chain:
 * The shared validation of decode and convert: a sane size and level
 * count, and @size covering the whole chain of @format.  @what names
 * the operation in messages.
 *
 * Returns: the chain's byte count, or 0 with @error set
 */
static gsize
check_chain (const guint8    *data,
             gsize            size,
             guint            width,
             guint            height,
             GrlPixelFormat   format,
             guint            mipmaps,
             const gchar     *what,
             GError         **error)
{
	gsize needed;

	if (width == 0 || height == 0 || width > LRG_TEXTURE_MAX_SIZE || height > LRG_TEXTURE_MAX_SIZE)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID,
		             "%s: impossible size %u x %u", what, width, height);
		return 0;
	}
	if (mipmaps == 0 || mipmaps > lrg_texture_get_max_mipmaps (width, height))
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID,
		             "%s: %u mip levels for %u x %u (1 .. %u possible)", what, mipmaps,
		             width, height, lrg_texture_get_max_mipmaps (width, height));
		return 0;
	}
	needed = lrg_texture_get_data_size (format, width, height, mipmaps);
	if (data == NULL || size < needed)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED,
		             "%s: %u x %u with %u levels needs %" G_GSIZE_FORMAT " bytes, got %"
		             G_GSIZE_FORMAT, what, width, height, mipmaps, needed,
		             data == NULL ? (gsize)0 : size);
		return 0;
	}
	return needed;
}

/* ========================================================================== */
/* Decode                                                                     */
/* ========================================================================== */

GBytes *
lrg_texture_decode (const guint8    *data,
                    gsize            size,
                    guint            width,
                    guint            height,
                    GrlPixelFormat   format,
                    guint            mipmaps,
                    GError         **error)
{
	LrgBcKind kind;
	gsize out_size;
	guint8 *out;
	guint8 *dst;
	guint level;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	switch (format)
	{
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB:
		kind = LRG_BC_KIND_BC1_RGB;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:
		kind = LRG_BC_KIND_BC1_RGBA;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:
		kind = LRG_BC_KIND_BC2;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:
		kind = LRG_BC_KIND_BC3;
		break;
	default:
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED,
		             "decode: pixel format %d is not S3TC", (gint)format);
		return NULL;
	}

	if (check_chain (data, size, width, height, format, mipmaps, "decode", error) == 0)
		return NULL;

	/* one RGBA8 chain the same shape, level by level */
	out_size = lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, width, height, mipmaps);
	out = g_malloc (out_size);
	dst = out;
	for (level = 0; level < mipmaps; level++)
	{
		lrg_texture_decode_bc_level (kind, data, width, height, dst);
		data += level_size (format, width, height);
		dst += (gsize)width * height * 4;
		width = MAX (1u, width / 2);
		height = MAX (1u, height / 2);
	}
	return g_bytes_new_take (out, out_size);
}

/* ========================================================================== */
/* Convert                                                                    */
/* ========================================================================== */

/* @v (0 .. 255) to the nearest of @max + 1 levels */
static inline guint
quantize (guint8 v,
          guint  max)
{
	return ((guint)v * max + 127) / 255;
}

GBytes *
lrg_texture_convert_rgba8 (const guint8    *rgba,
                           gsize            size,
                           guint            width,
                           guint            height,
                           guint            mipmaps,
                           GrlPixelFormat   target,
                           GError         **error)
{
	gsize needed;
	gsize pixels;
	gsize i;
	guint16 *out;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (target != GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5 &&
	    target != GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4 &&
	    target != GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1 &&
	    target != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
	{
		g_set_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED,
		             "convert: cannot convert RGBA8 to pixel format %d", (gint)target);
		return NULL;
	}

	needed = check_chain (rgba, size, width, height, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	                      mipmaps, "convert", error);
	if (needed == 0)
		return NULL;
	if (target == GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
		return g_bytes_new (rgba, needed);

	/* the levels are back to back in both layouts, so the whole chain
	 * converts as one run of pixels */
	pixels = needed / 4;
	out = g_new (guint16, pixels);
	for (i = 0; i < pixels; i++)
	{
		const guint8 *p = rgba + i * 4;

		switch (target)
		{
		case GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
			out[i] = (guint16)((quantize (p[0], 31) << 11) | (quantize (p[1], 63) << 5) |
			                   quantize (p[2], 31));
			break;
		case GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
			out[i] = (guint16)((quantize (p[0], 15) << 12) | (quantize (p[1], 15) << 8) |
			                   (quantize (p[2], 15) << 4) | quantize (p[3], 15));
			break;
		case GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
		default:
			out[i] = (guint16)((quantize (p[0], 31) << 11) | (quantize (p[1], 31) << 6) |
			                   (quantize (p[2], 31) << 1) | (p[3] >= 128 ? 1u : 0u));
			break;
		}
	}
	return g_bytes_new_take (out, pixels * sizeof (guint16));
}
