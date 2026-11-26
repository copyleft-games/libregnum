/* test-texture-decode.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless unit tests for the compressed texture fallback: the S3TC
 * decoders (lrg-texture-codec.h) on hand-built blocks whose expected
 * pixels are worked out from the EXT_texture_compression_s3tc rules
 * (DXT1 four- and three-colour blocks with and without alpha, DXT3
 * explicit alpha and its always-four-colour block, DXT5 in both
 * endpoint orders), sizes that are not multiples of 4, square and
 * rectangular mip chains, every malformed input (truncated by one
 * byte, NULL, zero and oversized dimensions, too many levels, a format
 * without a decoder), the RGBA8 to RGB565 / RGBA4444 / RGBA5551
 * repacking with its rounding, the data size table, and the upload
 * decision steered by the capability overrides (lrg-gpu-caps.h,
 * lrg-texture-upload.h), including the no-window failure of the GPU
 * half.  No GL context, no files.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

/* ========================================================================== */
/*                                 Helpers                                    */
/* ========================================================================== */

/* every test leaves the process-wide state as it found it */
static void
reset_state (void)
{
	lrg_gpu_caps_reset_overrides ();
	lrg_texture_set_default_fallback (LRG_TEXTURE_FALLBACK_DEFAULT);
}

/* a BC1 colour block: two 5:6:5 endpoints and one 2-bit index per
 * texel (texel 0 in the low bits) */
static void
put_color_block (guint8        *block,
                 guint16        c0,
                 guint16        c1,
                 const guint8   indices[16])
{
	guint32 bits = 0;
	guint i;

	for (i = 0; i < 16; i++)
		bits |= (guint32)(indices[i] & 3) << (2 * i);
	block[0] = (guint8)(c0 & 0xff);
	block[1] = (guint8)(c0 >> 8);
	block[2] = (guint8)(c1 & 0xff);
	block[3] = (guint8)(c1 >> 8);
	block[4] = (guint8)(bits & 0xff);
	block[5] = (guint8)((bits >> 8) & 0xff);
	block[6] = (guint8)((bits >> 16) & 0xff);
	block[7] = (guint8)((bits >> 24) & 0xff);
}

/* a BC1 block of one colour (index 0 everywhere) */
static void
put_solid_block (guint8  *block,
                 guint16  c0)
{
	static const guint8 zero[16] = { 0 };

	put_color_block (block, c0, 0, zero);
}

/* the eight-byte BC3 alpha block: endpoints and 3-bit indices */
static void
put_alpha_block (guint8        *block,
                 guint8         a0,
                 guint8         a1,
                 const guint8   indices[16])
{
	guint64 bits = 0;
	guint i;

	for (i = 0; i < 16; i++)
		bits |= (guint64)(indices[i] & 7) << (3 * i);
	block[0] = a0;
	block[1] = a1;
	for (i = 0; i < 6; i++)
		block[2 + i] = (guint8)((bits >> (8 * i)) & 0xff);
}

static void
assert_pixel (GBytes *rgba,
              guint   width,
              guint   x,
              guint   y,
              guint8  r,
              guint8  g,
              guint8  b,
              guint8  a)
{
	const guint8 *p = (const guint8 *)g_bytes_get_data (rgba, NULL) + ((gsize)y * width + x) * 4;

	g_assert_cmpuint (p[0], ==, r);
	g_assert_cmpuint (p[1], ==, g);
	g_assert_cmpuint (p[2], ==, b);
	g_assert_cmpuint (p[3], ==, a);
}

/* the 16-bit pixel @i of @data (host order) */
static guint16
pixel16 (GBytes *data,
         gsize   i)
{
	const guint16 *p = g_bytes_get_data (data, NULL);

	return p[i];
}

/* indices 0, 1, 2, 3 across every row */
static const guint8 ramp[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };

#define RED_565   ((guint16)0xF800)
#define GREEN_565 ((guint16)0x07E0)
#define BLUE_565  ((guint16)0x001F)
#define WHITE_565 ((guint16)0xFFFF)

/* ========================================================================== */
/*                                  Sizes                                     */
/* ========================================================================== */

static void
test_texture_max_mipmaps (void)
{
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (1, 1), ==, 1);
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (8, 8), ==, 4);
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (8, 2), ==, 4);
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (5, 3), ==, 3);
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (16384, 1), ==, 15);
	g_assert_cmpuint (lrg_texture_get_max_mipmaps (0, 4), ==, 0);
}

static void
test_texture_data_size (void)
{
	/* whole blocks per level */
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, 4, 1), ==, 8);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 4, 4, 1), ==, 16);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA, 5, 3, 1), ==, 16);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB, 4, 4, 1), ==, 8);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA, 8, 8, 1), ==, 64);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA, 9, 9, 1), ==, 64);
	/* chains: 8x8 + 4x4 + 2x2 + 1x1, and 8x2 + 4x1 + 2x1 + 1x1 */
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 8, 8, 4), ==, 32 + 8 + 8 + 8);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA, 8, 2, 4), ==, 32 + 16 + 16 + 16);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 8, 8, 4), ==,
	                  256 + 64 + 16 + 4);
	/* uncompressed */
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 3, 3, 1), ==, 36);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, 3, 3, 1), ==, 18);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, 3, 3, 1), ==, 27);
	/* impossible: unknown format, zero, too big, too many levels */
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_PVRT_RGB, 4, 4, 1), ==, 0);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_UNCOMPRESSED_R32, 4, 4, 1), ==, 0);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 0, 4, 1), ==, 0);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 16385, 4, 1), ==, 0);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, 4, 4), ==, 0);
	g_assert_cmpuint (lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, 4, 0), ==, 0);
}

static void
test_texture_format_predicates (void)
{
	g_assert_true (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB));
	g_assert_true (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA));
	g_assert_true (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA));
	g_assert_true (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA));
	g_assert_false (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB));
	g_assert_false (lrg_texture_format_is_s3tc (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8));
	g_assert_true (lrg_texture_format_is_compressed (GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA));
	g_assert_true (lrg_texture_format_is_compressed (GRL_PIXELFORMAT_COMPRESSED_ETC1_RGB));
	g_assert_false (lrg_texture_format_is_compressed (GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5));
}

/* ========================================================================== */
/*                                  BC1                                       */
/* ========================================================================== */

/* red (0xF800) > blue (0x001F): four colours; thirds truncate:
 * (2*255+0)/3 = 170, (255+0)/3 = 85 */
static void
test_texture_bc1_four_colour (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	guint8 block[8];
	guint y;

	put_color_block (block, RED_565, BLUE_565, ramp);

	/* RGB and RGBA agree in four-colour mode */
	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA, 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (rgba), ==, 64);
	for (y = 0; y < 4; y++)
	{
		assert_pixel (rgba, 4, 0, y, 255, 0, 0, 255);
		assert_pixel (rgba, 4, 1, y, 0, 0, 255, 255);
		assert_pixel (rgba, 4, 2, y, 170, 0, 85, 255);
		assert_pixel (rgba, 4, 3, y, 85, 0, 170, 255);
	}
	g_clear_pointer (&rgba, g_bytes_unref);

	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_no_error (error);
	assert_pixel (rgba, 4, 3, 2, 85, 0, 170, 255);
}

/* blue (0x001F) <= red (0xF800): three colours, the midpoint
 * (255/2 = 127) and black, transparent only for DXT1 RGBA */
static void
test_texture_bc1_three_colour (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	g_autoptr(GBytes) rgb = NULL;
	guint8 block[8];

	put_color_block (block, BLUE_565, RED_565, ramp);

	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA, 1, &error);
	g_assert_no_error (error);
	assert_pixel (rgba, 4, 0, 0, 0, 0, 255, 255);
	assert_pixel (rgba, 4, 1, 0, 255, 0, 0, 255);
	assert_pixel (rgba, 4, 2, 0, 127, 0, 127, 255);
	assert_pixel (rgba, 4, 3, 0, 0, 0, 0, 0);
	assert_pixel (rgba, 4, 3, 3, 0, 0, 0, 0);

	rgb = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_no_error (error);
	assert_pixel (rgb, 4, 2, 1, 127, 0, 127, 255);
	assert_pixel (rgb, 4, 3, 1, 0, 0, 0, 255);
}

/* equal endpoints count as c0 <= c1: three-colour mode */
static void
test_texture_bc1_equal_endpoints (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	guint8 block[8];

	put_color_block (block, GREEN_565, GREEN_565, ramp);
	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA, 1, &error);
	g_assert_no_error (error);
	assert_pixel (rgba, 4, 0, 0, 0, 255, 0, 255);
	assert_pixel (rgba, 4, 2, 0, 0, 255, 0, 255);
	assert_pixel (rgba, 4, 3, 0, 0, 0, 0, 0);
}

/* ========================================================================== */
/*                                  BC2                                       */
/* ========================================================================== */

/* texel i carries alpha nibble i (i * 17); the colour block has c0 < c1
 * and index 3 everywhere, which BC2 still reads in four-colour mode:
 * (0 + 2 * 255) / 3 = 170, opaque */
static void
test_texture_bc2_alpha (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	static const guint8 threes[16] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
	guint8 block[16];
	guint i;

	for (i = 0; i < 8; i++)
		block[i] = (guint8)((2 * i) | ((2 * i + 1) << 4));
	put_color_block (block + 8, 0x0000, WHITE_565, threes);

	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA, 1, &error);
	g_assert_no_error (error);
	for (i = 0; i < 16; i++)
		assert_pixel (rgba, 4, i % 4, i / 4, 170, 170, 170, (guint8)(i * 17));
}

/* ========================================================================== */
/*                                  BC3                                       */
/* ========================================================================== */

/* a0 = 255 > a1 = 0: eight alphas, ((7 - i) * 255) / 7 truncated:
 * 255, 0, 218, 182, 145, 109, 72, 36 */
static void
test_texture_bc3_eight_alphas (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	static const guint8 want[8] = { 255, 0, 218, 182, 145, 109, 72, 36 };
	guint8 indices[16];
	guint8 block[16];
	guint i;

	for (i = 0; i < 16; i++)
		indices[i] = (guint8)(i % 8);
	put_alpha_block (block, 255, 0, indices);
	put_solid_block (block + 8, WHITE_565);

	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 1, &error);
	g_assert_no_error (error);
	for (i = 0; i < 16; i++)
		assert_pixel (rgba, 4, i % 4, i / 4, 255, 255, 255, want[i % 8]);
}

/* a0 = 0 <= a1 = 255: six alphas plus 0 and 255:
 * 0, 255, 51, 102, 153, 204, 0, 255 */
static void
test_texture_bc3_six_alphas (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	static const guint8 want[8] = { 0, 255, 51, 102, 153, 204, 0, 255 };
	guint8 indices[16];
	guint8 block[16];
	guint i;

	for (i = 0; i < 16; i++)
		indices[i] = (guint8)(7 - (i % 8));
	put_alpha_block (block, 0, 255, indices);
	put_solid_block (block + 8, RED_565);

	rgba = lrg_texture_decode (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 1, &error);
	g_assert_no_error (error);
	for (i = 0; i < 16; i++)
		assert_pixel (rgba, 4, i % 4, i / 4, 255, 0, 0, want[7 - (i % 8)]);
}

/* ========================================================================== */
/*                          Odd sizes and mip chains                          */
/* ========================================================================== */

/* 5x3: two blocks across, one down; only the visible texels are kept */
static void
test_texture_odd_size (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	guint8 blocks[16];
	guint x;
	guint y;

	put_solid_block (blocks, RED_565);
	put_solid_block (blocks + 8, BLUE_565);

	rgba = lrg_texture_decode (blocks, sizeof blocks, 5, 3, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (rgba), ==, 5 * 3 * 4);
	for (y = 0; y < 3; y++)
	{
		for (x = 0; x < 4; x++)
			assert_pixel (rgba, 5, x, y, 255, 0, 0, 255);
		assert_pixel (rgba, 5, 4, y, 0, 0, 255, 255);
	}
}

/* 1x1 and 2x1 levels still read one whole block */
static void
test_texture_tiny_sizes (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) one = NULL;
	g_autoptr(GBytes) two = NULL;
	guint8 block[8];

	put_color_block (block, RED_565, BLUE_565, ramp);
	one = lrg_texture_decode (block, sizeof block, 1, 1, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (one), ==, 4);
	assert_pixel (one, 1, 0, 0, 255, 0, 0, 255);

	two = lrg_texture_decode (block, sizeof block, 2, 1, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_no_error (error);
	assert_pixel (two, 2, 1, 0, 0, 0, 255, 255);
}

/* 8x8 with four levels, each a solid colour: red, green, blue, white */
static void
test_texture_mip_chain (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	guint8 data[56];
	const guint8 *p;
	guint i;

	for (i = 0; i < 4; i++)
		put_solid_block (data + i * 8, RED_565);
	put_solid_block (data + 32, GREEN_565);
	put_solid_block (data + 40, BLUE_565);
	put_solid_block (data + 48, WHITE_565);

	rgba = lrg_texture_decode (data, sizeof data, 8, 8, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (rgba), ==, 256 + 64 + 16 + 4);

	p = g_bytes_get_data (rgba, NULL);
	for (i = 0; i < 64; i++)
		g_assert_cmpuint (p[i * 4], ==, 255);
	p += 256;
	for (i = 0; i < 16; i++)
		g_assert_cmpuint (p[i * 4 + 1], ==, 255);
	g_assert_cmpuint (p[0], ==, 0);
	p += 64;
	for (i = 0; i < 4; i++)
		g_assert_cmpuint (p[i * 4 + 2], ==, 255);
	g_assert_cmpuint (p[0], ==, 0);
	p += 16;
	g_assert_cmpuint (p[0], ==, 255);
	g_assert_cmpuint (p[1], ==, 255);
	g_assert_cmpuint (p[2], ==, 255);
	g_assert_cmpuint (p[3], ==, 255);
}

/* 8x2 DXT5 with four levels (8x2, 4x1, 2x1, 1x1): alpha 255, 0, 255, 0 */
static void
test_texture_rect_mip_chain (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	static const guint8 zero[16] = { 0 };
	guint8 data[80];
	const guint8 *p;
	guint i;

	/* blocks 0 and 1 are level 0 (opaque), block 2 level 1 (clear),
	 * block 3 level 2 (opaque), block 4 level 3 (clear) */
	for (i = 0; i < 5; i++)
	{
		guint8 alpha = (i == 2 || i == 4) ? 0 : 255;

		put_alpha_block (data + i * 16, alpha, alpha, zero);
		put_solid_block (data + i * 16 + 8, WHITE_565);
	}

	rgba = lrg_texture_decode (data, sizeof data, 8, 2, GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 4, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (rgba), ==, (8 * 2 + 4 * 1 + 2 * 1 + 1) * 4);

	p = g_bytes_get_data (rgba, NULL);
	for (i = 0; i < 16; i++)
		g_assert_cmpuint (p[i * 4 + 3], ==, 255);
	p += 16 * 4;
	for (i = 0; i < 4; i++)
		g_assert_cmpuint (p[i * 4 + 3], ==, 0);
	p += 4 * 4;
	for (i = 0; i < 2; i++)
		g_assert_cmpuint (p[i * 4 + 3], ==, 255);
	p += 2 * 4;
	g_assert_cmpuint (p[3], ==, 0);
}

/* ========================================================================== */
/*                               Bad input                                    */
/* ========================================================================== */

static void
test_texture_decode_errors (void)
{
	guint8 data[56];
	GBytes *out;
	GError *error = NULL;

	memset (data, 0, sizeof data);

	/* one byte short of a single block, of a chain, and NULL */
	out = lrg_texture_decode (data, 7, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	out = lrg_texture_decode (data, 55, 8, 8, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	out = lrg_texture_decode (data, 15, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	out = lrg_texture_decode (NULL, 0, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	/* impossible sizes and level counts */
	out = lrg_texture_decode (data, sizeof data, 0, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);

	out = lrg_texture_decode (data, sizeof data, 16385, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);

	out = lrg_texture_decode (data, sizeof data, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);

	out = lrg_texture_decode (data, sizeof data, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 0, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);

	/* no decoder */
	out = lrg_texture_decode (data, sizeof data, 4, 4, GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB, 1, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED);
	g_clear_error (&error);
}

/* the decoder must read exactly the bytes it was promised: an exact-size
 * heap copy lets ASAN catch a read one past the end */
static void
test_texture_decode_exact_buffer (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) rgba = NULL;
	gsize size = lrg_texture_get_data_size (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 13, 7, 4);
	g_autofree guint8 *data = g_malloc0 (size);

	g_assert_cmpuint (size, ==, (4 * 2 + 2 * 1 + 1 + 1) * 16);
	rgba = lrg_texture_decode (data, size, 13, 7, GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 4, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (rgba), ==, (13 * 7 + 6 * 3 + 3 * 1 + 1 * 1) * 4);
}

/* ========================================================================== */
/*                               Conversion                                   */
/* ========================================================================== */

static void
test_texture_convert_rgb565 (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) out = NULL;
	static const guint8 px[] = {
		255, 255, 255, 255,
		255, 0, 0, 0,
		0, 255, 0, 255,
		0, 0, 255, 255,
		128, 128, 128, 255,   /* (128*31+127)/255 = 16, (128*63+127)/255 = 32 */
		8, 4, 0, 255,         /* 375/255 = 1, (4*63+127)/255 = 1 */
	};

	out = lrg_texture_convert_rgba8 (px, sizeof px, 6, 1, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (out), ==, 12);
	g_assert_cmphex (pixel16 (out, 0), ==, 0xFFFF);
	g_assert_cmphex (pixel16 (out, 1), ==, 0xF800);
	g_assert_cmphex (pixel16 (out, 2), ==, 0x07E0);
	g_assert_cmphex (pixel16 (out, 3), ==, 0x001F);
	g_assert_cmphex (pixel16 (out, 4), ==, (16u << 11) | (32u << 5) | 16u);
	g_assert_cmphex (pixel16 (out, 5), ==, (1u << 11) | (1u << 5));
}

static void
test_texture_convert_rgba4444 (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) out = NULL;
	static const guint8 px[] = {
		255, 0, 0, 255,
		128, 128, 128, 128,   /* (128*15+127)/255 = 8 */
		0, 0, 0, 0,
		0, 17, 34, 51,        /* 1, 2, 3 */
	};

	out = lrg_texture_convert_rgba8 (px, sizeof px, 2, 2, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4, &error);
	g_assert_no_error (error);
	g_assert_cmphex (pixel16 (out, 0), ==, 0xF00F);
	g_assert_cmphex (pixel16 (out, 1), ==, 0x8888);
	g_assert_cmphex (pixel16 (out, 2), ==, 0x0000);
	g_assert_cmphex (pixel16 (out, 3), ==, 0x0123);
}

static void
test_texture_convert_rgba5551 (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) out = NULL;
	static const guint8 px[] = {
		255, 255, 255, 255,
		255, 0, 0, 127,       /* alpha below 128: clear */
		0, 0, 255, 128,       /* 128 and above: opaque */
		0, 255, 0, 255,
	};

	out = lrg_texture_convert_rgba8 (px, sizeof px, 4, 1, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1, &error);
	g_assert_no_error (error);
	g_assert_cmphex (pixel16 (out, 0), ==, 0xFFFF);
	g_assert_cmphex (pixel16 (out, 1), ==, 0xF800);
	g_assert_cmphex (pixel16 (out, 2), ==, 0x003F);
	g_assert_cmphex (pixel16 (out, 3), ==, 0x07C1);
}

/* a 2x2 chain (2x2 + 1x1 = 5 pixels) and the RGBA8 copy */
static void
test_texture_convert_mips_and_copy (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) out = NULL;
	g_autoptr(GBytes) copy = NULL;
	guint8 px[5 * 4];
	guint i;

	for (i = 0; i < 4; i++)
	{
		px[i * 4 + 0] = 255;
		px[i * 4 + 1] = 0;
		px[i * 4 + 2] = 0;
		px[i * 4 + 3] = 255;
	}
	px[16] = 0;
	px[17] = 0;
	px[18] = 255;
	px[19] = 255;

	out = lrg_texture_convert_rgba8 (px, sizeof px, 2, 2, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (g_bytes_get_size (out), ==, 10);
	g_assert_cmphex (pixel16 (out, 3), ==, 0xF800);
	g_assert_cmphex (pixel16 (out, 4), ==, 0x001F);

	copy = lrg_texture_convert_rgba8 (px, sizeof px, 2, 2, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, &error);
	g_assert_no_error (error);
	g_assert_cmpmem (g_bytes_get_data (copy, NULL), g_bytes_get_size (copy), px, sizeof px);
}

static void
test_texture_convert_errors (void)
{
	guint8 px[16];
	GBytes *out;
	GError *error = NULL;

	memset (px, 0, sizeof px);

	out = lrg_texture_convert_rgba8 (px, 15, 2, 2, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	out = lrg_texture_convert_rgba8 (px, sizeof px, 2, 2, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	out = lrg_texture_convert_rgba8 (px, sizeof px, 2, 2, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED);
	g_clear_error (&error);

	out = lrg_texture_convert_rgba8 (px, sizeof px, 2, 0, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, &error);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);
}

/* ========================================================================== */
/*                      Capabilities and the upload path                      */
/* ========================================================================== */

static void
test_texture_caps_override (void)
{
	LrgTextureCompression family;

	reset_state ();

	/* headless: no context, so nothing compressed is known to work */
	if (lrg_gpu_caps_get_override (LRG_TEXTURE_COMPRESSION_S3TC) == LRG_GPU_CAP_OVERRIDE_AUTO)
		g_assert_false (lrg_gpu_caps_has_texture_compression (LRG_TEXTURE_COMPRESSION_S3TC));
	g_assert_true (lrg_gpu_caps_supports_pixel_format (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8));
	g_assert_true (lrg_gpu_caps_supports_pixel_format (GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5));

	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_SUPPORTED);
	g_assert_cmpint (lrg_gpu_caps_get_override (LRG_TEXTURE_COMPRESSION_S3TC), ==,
	                 LRG_GPU_CAP_OVERRIDE_SUPPORTED);
	g_assert_true (lrg_gpu_caps_has_texture_compression (LRG_TEXTURE_COMPRESSION_S3TC));
	g_assert_true (lrg_gpu_caps_supports_pixel_format (GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA));
	/* the other families are untouched */
	g_assert_cmpint (lrg_gpu_caps_get_override (LRG_TEXTURE_COMPRESSION_ETC2), ==,
	                 LRG_GPU_CAP_OVERRIDE_AUTO);

	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	g_assert_false (lrg_gpu_caps_has_texture_compression (LRG_TEXTURE_COMPRESSION_S3TC));
	g_assert_false (lrg_gpu_caps_supports_pixel_format (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB));

	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_ASTC, LRG_GPU_CAP_OVERRIDE_SUPPORTED);
	g_assert_true (lrg_gpu_caps_supports_pixel_format (GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA));

	/* families */
	g_assert_true (lrg_gpu_caps_get_texture_compression_family (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, &family));
	g_assert_cmpint (family, ==, LRG_TEXTURE_COMPRESSION_S3TC);
	g_assert_true (lrg_gpu_caps_get_texture_compression_family (GRL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA, &family));
	g_assert_cmpint (family, ==, LRG_TEXTURE_COMPRESSION_ETC2);
	g_assert_true (lrg_gpu_caps_get_texture_compression_family (GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA, NULL));
	g_assert_false (lrg_gpu_caps_get_texture_compression_family (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, NULL));

	reset_state ();
	g_assert_cmpint (lrg_gpu_caps_get_override (LRG_TEXTURE_COMPRESSION_ASTC), ==,
	                 LRG_GPU_CAP_OVERRIDE_AUTO);
}

static void
test_texture_default_fallback (void)
{
	GEnumClass *klass;

	reset_state ();

	/* the hermetic test environment has no LRG_TEXTURE_FALLBACK */
	g_assert_cmpint (lrg_texture_get_default_fallback (), ==, LRG_TEXTURE_FALLBACK_RGBA8);
	g_assert_cmpint (lrg_texture_fallback_get_pixel_format (LRG_TEXTURE_FALLBACK_DEFAULT), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

	lrg_texture_set_default_fallback (LRG_TEXTURE_FALLBACK_RGBA4444);
	g_assert_cmpint (lrg_texture_get_default_fallback (), ==, LRG_TEXTURE_FALLBACK_RGBA4444);
	g_assert_cmpint (lrg_texture_fallback_get_pixel_format (LRG_TEXTURE_FALLBACK_DEFAULT), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4);
	/* an explicit choice beats the default */
	g_assert_cmpint (lrg_texture_fallback_get_pixel_format (LRG_TEXTURE_FALLBACK_RGB565), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5);
	g_assert_cmpint (lrg_texture_fallback_get_pixel_format (LRG_TEXTURE_FALLBACK_RGBA5551), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1);

	klass = g_type_class_ref (LRG_TYPE_TEXTURE_FALLBACK_FORMAT);
	g_assert_cmpint (g_enum_get_value_by_nick (klass, "rgb565")->value, ==, LRG_TEXTURE_FALLBACK_RGB565);
	g_assert_cmpint (g_enum_get_value_by_nick (klass, "rgba4444")->value, ==, LRG_TEXTURE_FALLBACK_RGBA4444);
	g_type_class_unref (klass);

	reset_state ();
}

static void
test_texture_choose_format (void)
{
	reset_state ();

	/* the GPU takes S3TC: upload as it is */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_SUPPORTED);
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA,
	                                                   LRG_TEXTURE_FALLBACK_RGBA4444), ==,
	                 GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA);

	/* it does not: the fallback, explicit or the default */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA,
	                                                   LRG_TEXTURE_FALLBACK_RGBA4444), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4);
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB,
	                                                   LRG_TEXTURE_FALLBACK_DEFAULT), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	lrg_texture_set_default_fallback (LRG_TEXTURE_FALLBACK_RGB565);
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB,
	                                                   LRG_TEXTURE_FALLBACK_DEFAULT), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5);

	/* no decoder for ETC2: the format stays (the upload then fails) */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_ETC2, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB,
	                                                   LRG_TEXTURE_FALLBACK_RGBA8), ==,
	                 GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB);

	/* uncompressed is always itself */
	g_assert_cmpint (lrg_texture_choose_upload_format (GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	                                                   LRG_TEXTURE_FALLBACK_RGB565), ==,
	                 GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

	reset_state ();
}

/* the CPU half end to end: native, each fallback, and the failures */
static void
test_texture_prepare_upload (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) out = NULL;
	GrlPixelFormat format = GRL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
	guint8 block[8];
	guint8 chain[24];
	gboolean ok;

	reset_state ();
	put_color_block (block, RED_565, BLUE_565, ramp);

	/* native: a copy of exactly the chain */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_SUPPORTED);
	ok = lrg_texture_prepare_upload (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                 LRG_TEXTURE_FALLBACK_RGBA8, &out, &format, &error);
	g_assert_no_error (error);
	g_assert_true (ok);
	g_assert_cmpint (format, ==, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB);
	g_assert_cmpmem (g_bytes_get_data (out, NULL), g_bytes_get_size (out), block, sizeof block);
	g_clear_pointer (&out, g_bytes_unref);

	/* fallback to RGBA8: the decoded pixels */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	ok = lrg_texture_prepare_upload (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                 LRG_TEXTURE_FALLBACK_RGBA8, &out, &format, &error);
	g_assert_no_error (error);
	g_assert_true (ok);
	g_assert_cmpint (format, ==, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	assert_pixel (out, 4, 2, 0, 170, 0, 85, 255);
	g_clear_pointer (&out, g_bytes_unref);

	/* fallback to RGBA4444: 170 -> 10, 85 -> 5 */
	ok = lrg_texture_prepare_upload (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                 LRG_TEXTURE_FALLBACK_RGBA4444, &out, &format, &error);
	g_assert_no_error (error);
	g_assert_true (ok);
	g_assert_cmpint (format, ==, GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4);
	g_assert_cmpuint (g_bytes_get_size (out), ==, 32);
	g_assert_cmphex (pixel16 (out, 0), ==, 0xF00F);
	g_assert_cmphex (pixel16 (out, 2), ==, 0xA05F);
	g_clear_pointer (&out, g_bytes_unref);

	/* fallback with mips preserved: 4x4 + 2x2 + 1x1 of DXT1 to RGB565 */
	put_solid_block (chain, RED_565);
	put_solid_block (chain + 8, GREEN_565);
	put_solid_block (chain + 16, BLUE_565);
	ok = lrg_texture_prepare_upload (chain, sizeof chain, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 3,
	                                 LRG_TEXTURE_FALLBACK_RGB565, &out, &format, &error);
	g_assert_no_error (error);
	g_assert_true (ok);
	g_assert_cmpint (format, ==, GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5);
	g_assert_cmpuint (g_bytes_get_size (out), ==, (16 + 4 + 1) * 2);
	g_assert_cmphex (pixel16 (out, 15), ==, RED_565);
	g_assert_cmphex (pixel16 (out, 16), ==, GREEN_565);
	g_assert_cmphex (pixel16 (out, 20), ==, BLUE_565);
	g_clear_pointer (&out, g_bytes_unref);

	/* truncated chain */
	ok = lrg_texture_prepare_upload (chain, sizeof chain - 1, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 3,
	                                 LRG_TEXTURE_FALLBACK_RGB565, &out, NULL, &error);
	g_assert_false (ok);
	g_assert_null (out);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	/* ETC2 on a GPU without it: no CPU decoder */
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_ETC2, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	ok = lrg_texture_prepare_upload (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB, 1,
	                                 LRG_TEXTURE_FALLBACK_RGBA8, &out, NULL, &error);
	g_assert_false (ok);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED);
	g_clear_error (&error);

	/* a format without a size */
	ok = lrg_texture_prepare_upload (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_PVRT_RGB, 1,
	                                 LRG_TEXTURE_FALLBACK_RGBA8, &out, NULL, &error);
	g_assert_false (ok);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UNSUPPORTED);
	g_clear_error (&error);

	/* too many levels */
	ok = lrg_texture_prepare_upload (chain, sizeof chain, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 4,
	                                 LRG_TEXTURE_FALLBACK_RGBA8, &out, NULL, &error);
	g_assert_false (ok);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_INVALID);
	g_clear_error (&error);

	reset_state ();
}

/* the GPU half needs a window: without one it fails cleanly, after the
 * CPU half has validated the data */
static void
test_texture_upload_without_window (void)
{
	g_autoptr(GrlTexture) texture = NULL;
	GError *error = NULL;
	guint8 block[8];
	guint id;

	reset_state ();
	lrg_gpu_caps_set_override (LRG_TEXTURE_COMPRESSION_S3TC, LRG_GPU_CAP_OVERRIDE_UNSUPPORTED);
	put_solid_block (block, RED_565);

	id = lrg_texture_upload_compressed (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                    LRG_TEXTURE_FALLBACK_RGB565, NULL, &error);
	g_assert_cmpuint (id, ==, 0);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD);
	g_clear_error (&error);

	texture = lrg_texture_new_compressed (block, sizeof block, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                      LRG_TEXTURE_FALLBACK_RGBA8, &error);
	g_assert_null (texture);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_UPLOAD);
	g_clear_error (&error);

	/* bad data is reported before the missing window */
	id = lrg_texture_upload_compressed (block, 4, 4, 4, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB, 1,
	                                    LRG_TEXTURE_FALLBACK_RGB565, NULL, &error);
	g_assert_cmpuint (id, ==, 0);
	g_assert_error (error, LRG_TEXTURE_ERROR, LRG_TEXTURE_ERROR_TRUNCATED);
	g_clear_error (&error);

	reset_state ();
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/texture-decode/max-mipmaps", test_texture_max_mipmaps);
	g_test_add_func ("/texture-decode/data-size", test_texture_data_size);
	g_test_add_func ("/texture-decode/format-predicates", test_texture_format_predicates);
	g_test_add_func ("/texture-decode/bc1-four-colour", test_texture_bc1_four_colour);
	g_test_add_func ("/texture-decode/bc1-three-colour", test_texture_bc1_three_colour);
	g_test_add_func ("/texture-decode/bc1-equal-endpoints", test_texture_bc1_equal_endpoints);
	g_test_add_func ("/texture-decode/bc2-alpha", test_texture_bc2_alpha);
	g_test_add_func ("/texture-decode/bc3-eight-alphas", test_texture_bc3_eight_alphas);
	g_test_add_func ("/texture-decode/bc3-six-alphas", test_texture_bc3_six_alphas);
	g_test_add_func ("/texture-decode/odd-size", test_texture_odd_size);
	g_test_add_func ("/texture-decode/tiny-sizes", test_texture_tiny_sizes);
	g_test_add_func ("/texture-decode/mip-chain", test_texture_mip_chain);
	g_test_add_func ("/texture-decode/rect-mip-chain", test_texture_rect_mip_chain);
	g_test_add_func ("/texture-decode/decode-errors", test_texture_decode_errors);
	g_test_add_func ("/texture-decode/decode-exact-buffer", test_texture_decode_exact_buffer);
	g_test_add_func ("/texture-decode/convert-rgb565", test_texture_convert_rgb565);
	g_test_add_func ("/texture-decode/convert-rgba4444", test_texture_convert_rgba4444);
	g_test_add_func ("/texture-decode/convert-rgba5551", test_texture_convert_rgba5551);
	g_test_add_func ("/texture-decode/convert-mips-and-copy", test_texture_convert_mips_and_copy);
	g_test_add_func ("/texture-decode/convert-errors", test_texture_convert_errors);
	g_test_add_func ("/texture-decode/caps-override", test_texture_caps_override);
	g_test_add_func ("/texture-decode/default-fallback", test_texture_default_fallback);
	g_test_add_func ("/texture-decode/choose-format", test_texture_choose_format);
	g_test_add_func ("/texture-decode/prepare-upload", test_texture_prepare_upload);
	g_test_add_func ("/texture-decode/upload-without-window", test_texture_upload_without_window);

	return g_test_run ();
}
