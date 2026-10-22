/* test-dds-image.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless unit tests for LrgDdsImage: DXT1 four- and three-colour
 * blocks, DXT3 explicit alpha, DXT5 interpolated alpha (both endpoint
 * orders), clipped blocks on odd sizes, uncompressed 32/24/16-bit
 * masked surfaces, luminance and alpha surfaces, the DX10 header, mip
 * chains (complete and cut short), raw mip data, the GrlImage wrapper,
 * every malformed-input failure, and BC7: one hand-built block per mode
 * (0 .. 7) with the expected pixels worked out from the specification,
 * the reserved mode, sizes that are not multiples of 4, truncated input,
 * and DX10 files (BC7_UNORM with a mip chain, BC7_UNORM_SRGB).
 * Fixtures are built in memory.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

/* ========================================================================== */
/*                                 Helpers                                    */
/* ========================================================================== */

#define PF_ALPHAPIXELS (0x1u)
#define PF_ALPHA       (0x2u)
#define PF_FOURCC      (0x4u)
#define PF_RGB         (0x40u)
#define PF_LUMINANCE   (0x20000u)

static void
put_u32 (guint8  *p,
         guint32  v)
{
    p[0] = (guint8)(v & 0xff);
    p[1] = (guint8)((v >> 8) & 0xff);
    p[2] = (guint8)((v >> 16) & 0xff);
    p[3] = (guint8)((v >> 24) & 0xff);
}

/* dds_build:
 * A DDS file: magic, header (mip count @mips when > 0), optional DX10
 * header (@dxgi != 0), then @payload. */
static GBytes *
dds_build (guint         width,
           guint         height,
           guint         mips,
           guint32       pf_flags,
           const gchar  *fourcc,
           guint32       bit_count,
           const guint32 masks[4],
           guint32       dxgi,
           const guint8 *payload,
           gsize         payload_size)
{
    GByteArray *out = g_byte_array_new ();
    guint8 header[128];
    guint8 dx10[20];

    memset (header, 0, sizeof header);
    memcpy (header, "DDS ", 4);
    put_u32 (header + 4, 124);
    put_u32 (header + 8, 0x1007u | (mips > 0 ? 0x20000u : 0));
    put_u32 (header + 12, height);
    put_u32 (header + 16, width);
    put_u32 (header + 28, mips);
    put_u32 (header + 76, 32);
    put_u32 (header + 80, pf_flags);
    if (fourcc != NULL)
        memcpy (header + 84, fourcc, 4);
    put_u32 (header + 88, bit_count);
    if (masks != NULL)
    {
        put_u32 (header + 92, masks[0]);
        put_u32 (header + 96, masks[1]);
        put_u32 (header + 100, masks[2]);
        put_u32 (header + 104, masks[3]);
    }
    put_u32 (header + 108, 0x1000u);
    g_byte_array_append (out, header, sizeof header);

    if (dxgi != 0)
    {
        memset (dx10, 0, sizeof dx10);
        put_u32 (dx10, dxgi);
        put_u32 (dx10 + 4, 3);  /* Texture2D */
        put_u32 (dx10 + 12, 1);
        g_byte_array_append (out, dx10, sizeof dx10);
    }
    if (payload != NULL)
        g_byte_array_append (out, payload, (guint)payload_size);
    return g_byte_array_free_to_bytes (out);
}

static const guint8 *
pixel (GBytes *rgba,
       guint   width,
       guint   x,
       guint   y)
{
    return (const guint8 *)g_bytes_get_data (rgba, NULL) + ((gsize)y * width + x) * 4;
}

static void
assert_rgba (const guint8 *p,
             guint8        r,
             guint8        g,
             guint8        b,
             guint8        a)
{
    g_assert_cmpuint (p[0], ==, r);
    g_assert_cmpuint (p[1], ==, g);
    g_assert_cmpuint (p[2], ==, b);
    g_assert_cmpuint (p[3], ==, a);
}

/* a BC1 colour block: c0 = red, c1 = blue, every pixel's index given
 * by @indices (2 bits each, pixel 0 in the low bits) */
static void
bc1_block (guint8  *block,
           guint16  c0,
           guint16  c1,
           guint32  indices)
{
    block[0] = (guint8)(c0 & 0xff);
    block[1] = (guint8)(c0 >> 8);
    block[2] = (guint8)(c1 & 0xff);
    block[3] = (guint8)(c1 >> 8);
    put_u32 (block + 4, indices);
}

#define RED565  (0xf800u)
#define BLUE565 (0x001fu)

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

static void
test_dds_dxt1_four_colour (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GError) error = NULL;
    guint8 block[8];

    /* pixel 0 index 0 (red), pixel 1 index 1 (blue), 2 and 3 the
     * interpolated colours; c0 > c1 selects four-colour mode */
    bc1_block (block, RED565, BLUE565, 0x000000e4u);
    file = dds_build (4, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, sizeof block);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    g_assert_nonnull (image);

    g_assert_cmpuint (lrg_dds_image_get_width (image), ==, 4);
    g_assert_cmpuint (lrg_dds_image_get_height (image), ==, 4);
    g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_DXT1);
    g_assert_cmpuint (lrg_dds_image_get_mip_count (image), ==, 1);
    g_assert_true (lrg_dds_image_has_alpha (image));

    rgba = lrg_dds_image_decode (image, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (rgba), ==, 4 * 4 * 4);
    assert_rgba (pixel (rgba, 4, 0, 0), 255, 0, 0, 255);
    assert_rgba (pixel (rgba, 4, 1, 0), 0, 0, 255, 255);
    assert_rgba (pixel (rgba, 4, 2, 0), 170, 0, 85, 255);
    assert_rgba (pixel (rgba, 4, 3, 0), 85, 0, 170, 255);
    /* every other pixel has index 0 */
    assert_rgba (pixel (rgba, 4, 3, 3), 255, 0, 0, 255);
}

static void
test_dds_dxt1_three_colour (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GError) error = NULL;
    guint8 block[8];

    /* c0 <= c1: index 2 is the midpoint, index 3 transparent black */
    bc1_block (block, BLUE565, RED565, 0x000000e4u);
    file = dds_build (4, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, sizeof block);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    rgba = lrg_dds_image_decode (image, 0, &error);
    g_assert_no_error (error);
    assert_rgba (pixel (rgba, 4, 0, 0), 0, 0, 255, 255);
    assert_rgba (pixel (rgba, 4, 1, 0), 255, 0, 0, 255);
    assert_rgba (pixel (rgba, 4, 2, 0), 127, 0, 127, 255);
    assert_rgba (pixel (rgba, 4, 3, 0), 0, 0, 0, 0);
}

static void
test_dds_dxt3_alpha (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GError) error = NULL;
    guint8 block[16];
    guint i;

    /* alpha nibbles 0, 1, 2 .. 15 in pixel order; colour all red */
    for (i = 0; i < 8; i++)
        block[i] = (guint8)((2 * i) | ((2 * i + 1) << 4));
    bc1_block (block + 8, RED565, BLUE565, 0);
    file = dds_build (4, 4, 0, PF_FOURCC, "DXT3", 0, NULL, 0, block, sizeof block);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_DXT3);
    rgba = lrg_dds_image_decode (image, 0, &error);
    g_assert_no_error (error);
    assert_rgba (pixel (rgba, 4, 0, 0), 255, 0, 0, 0);
    assert_rgba (pixel (rgba, 4, 1, 0), 255, 0, 0, 17);
    assert_rgba (pixel (rgba, 4, 3, 3), 255, 0, 0, 255);
}

static void
test_dds_dxt5_alpha (void)
{
    g_autoptr(GError) error = NULL;
    guint8 block[16];
    guint pass;

    for (pass = 0; pass < 2; pass++)
    {
        g_autoptr(GBytes) file = NULL;
        g_autoptr(LrgDdsImage) image = NULL;
        g_autoptr(GBytes) rgba = NULL;
        guint64 bits;
        guint i;

        memset (block, 0, sizeof block);
        /* pass 0: a0 > a1, eight-value ramp; pass 1: a0 <= a1, six
         * values plus 0 and 255 */
        block[0] = pass == 0 ? 210 : 50;
        block[1] = pass == 0 ? 0 : 200;
        /* pixel i uses index i % 8 */
        bits = 0;
        for (i = 0; i < 16; i++)
            bits |= (guint64)(i % 8) << (3 * i);
        for (i = 0; i < 6; i++)
            block[2 + i] = (guint8)((bits >> (8 * i)) & 0xff);
        bc1_block (block + 8, RED565, BLUE565, 0);

        file = dds_build (4, 4, 0, PF_FOURCC, "DXT5", 0, NULL, 0, block, sizeof block);
        image = lrg_dds_image_new_from_bytes (file, &error);
        g_assert_no_error (error);
        rgba = lrg_dds_image_decode (image, 0, &error);
        g_assert_no_error (error);

        if (pass == 0)
        {
            g_assert_cmpuint (pixel (rgba, 4, 0, 0)[3], ==, 210);
            g_assert_cmpuint (pixel (rgba, 4, 1, 0)[3], ==, 0);
            g_assert_cmpuint (pixel (rgba, 4, 2, 0)[3], ==, 180);  /* (6*210)/7 */
            g_assert_cmpuint (pixel (rgba, 4, 3, 1)[3], ==, 30);   /* index 7 */
        }
        else
        {
            g_assert_cmpuint (pixel (rgba, 4, 0, 0)[3], ==, 50);
            g_assert_cmpuint (pixel (rgba, 4, 2, 0)[3], ==, 80);   /* (4*50+200)/5 */
            g_assert_cmpuint (pixel (rgba, 4, 2, 1)[3], ==, 0);    /* index 6 */
            g_assert_cmpuint (pixel (rgba, 4, 3, 1)[3], ==, 255);  /* index 7 */
        }
        assert_rgba (pixel (rgba, 4, 0, 0), 255, 0, 0, pixel (rgba, 4, 0, 0)[3]);
    }
}

static void
test_dds_odd_size_and_mips (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GBytes) raw = NULL;
    g_autoptr(GError) error = NULL;
    guint8 payload[4 * 8 + 8 + 8];
    guint w;
    guint h;
    guint i;

    /* 6 x 5: 2 x 2 blocks at level 0, then 3 x 2 (1 x 1 block .. no,
     * 1 x 1 blocks) down to 1 x 1: levels 6x5, 3x2, 1x1 */
    for (i = 0; i < 4; i++)
        bc1_block (payload + 8 * i, RED565, BLUE565, i == 3 ? 0x55555555u : 0);
    bc1_block (payload + 32, BLUE565 + 1, BLUE565, 0);
    bc1_block (payload + 40, RED565, BLUE565, 0x55555555u);
    file = dds_build (6, 5, 3, PF_FOURCC, "DXT1", 0, NULL, 0, payload, sizeof payload);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_dds_image_get_mip_count (image), ==, 3);
    g_assert_true (lrg_dds_image_get_mip_size (image, 1, &w, &h));
    g_assert_cmpuint (w, ==, 3);
    g_assert_cmpuint (h, ==, 2);
    g_assert_true (lrg_dds_image_get_mip_size (image, 2, &w, &h));
    g_assert_cmpuint (w, ==, 1);
    g_assert_cmpuint (h, ==, 1);
    g_assert_false (lrg_dds_image_get_mip_size (image, 3, NULL, NULL));

    rgba = lrg_dds_image_decode (image, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (rgba), ==, 6 * 5 * 4);
    /* the bottom-right block (index 1 = blue) is clipped to 2 x 1 */
    assert_rgba (pixel (rgba, 6, 5, 4), 0, 0, 255, 255);
    assert_rgba (pixel (rgba, 6, 0, 0), 255, 0, 0, 255);

    g_clear_pointer (&rgba, g_bytes_unref);
    rgba = lrg_dds_image_decode (image, 2, &error);
    g_assert_no_error (error);
    assert_rgba (pixel (rgba, 1, 0, 0), 0, 0, 255, 255);

    raw = lrg_dds_image_get_mip_data (image, 1);
    g_assert_nonnull (raw);
    g_assert_cmpuint (g_bytes_get_size (raw), ==, 8);
    g_assert_cmpmem (g_bytes_get_data (raw, NULL), 8, payload + 32, 8);
    g_assert_null (lrg_dds_image_get_mip_data (image, 3));
}

static void
test_dds_short_mip_chain (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GError) error = NULL;
    guint8 payload[8 * 4 + 4];

    /* 8 x 8 declares 4 levels (32 + 8 + 8 + 8 bytes) but only level 0
     * and part of level 1 are present */
    memset (payload, 0, sizeof payload);
    file = dds_build (8, 8, 4, PF_FOURCC, "DXT1", 0, NULL, 0, payload, sizeof payload);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_dds_image_get_mip_count (image), ==, 1);
    g_assert_null (lrg_dds_image_decode (image, 1, &error));
    g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID);
}

static void
test_dds_uncompressed (void)
{
    g_autoptr(GError) error = NULL;
    static const guint32 bgra[4] = { 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u };
    static const guint32 bgr[4] = { 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0 };
    static const guint32 rgb565[4] = { 0xf800u, 0x07e0u, 0x001fu, 0 };
    static const guint32 lum[4] = { 0xffu, 0, 0, 0 };
    static const guint32 lum_alpha[4] = { 0xffu, 0, 0, 0xff00u };
    static const guint32 alpha[4] = { 0, 0, 0, 0xffu };

    /* 32-bit BGRA: bytes B G R A */
    {
        static const guint8 px[8] = { 10, 20, 30, 40, 1, 2, 3, 4 };
        g_autoptr(GBytes) file = dds_build (2, 1, 0, PF_RGB | PF_ALPHAPIXELS, NULL, 32,
                                            bgra, 0, px, sizeof px);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_RGBA);
        g_assert_true (lrg_dds_image_has_alpha (image));
        rgba = lrg_dds_image_decode (image, 0, &error);
        g_assert_no_error (error);
        assert_rgba (pixel (rgba, 2, 0, 0), 30, 20, 10, 40);
        assert_rgba (pixel (rgba, 2, 1, 0), 3, 2, 1, 4);
    }
    /* 24-bit BGR without alpha */
    {
        static const guint8 px[3] = { 5, 6, 7 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_RGB, NULL, 24, bgr, 0, px, sizeof px);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_RGB);
        g_assert_false (lrg_dds_image_has_alpha (image));
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 7, 6, 5, 255);
    }
    /* 16-bit 565: pure green */
    {
        static const guint8 px[2] = { 0xe0, 0x07 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_RGB, NULL, 16, rgb565, 0, px, sizeof px);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 0, 255, 0, 255);
    }
    /* luminance, luminance + alpha, alpha */
    {
        static const guint8 px[1] = { 99 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_LUMINANCE, NULL, 8, lum, 0, px, 1);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_LUMINANCE);
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 99, 99, 99, 255);
    }
    {
        static const guint8 px[2] = { 50, 60 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_LUMINANCE | PF_ALPHAPIXELS, NULL, 16,
                                            lum_alpha, 0, px, 2);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_LUMINANCE_ALPHA);
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 50, 50, 50, 60);
    }
    {
        static const guint8 px[1] = { 128 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_ALPHA, NULL, 8, alpha, 0, px, 1);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_ALPHA);
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 0, 0, 0, 128);
    }
}

static void
test_dds_dx10 (void)
{
    g_autoptr(GError) error = NULL;
    guint8 block[8];

    /* BC1 through DXGI_FORMAT_BC1_UNORM */
    {
        g_autoptr(GBytes) file = NULL;
        g_autoptr(LrgDdsImage) image = NULL;

        bc1_block (block, RED565, BLUE565, 0);
        file = dds_build (4, 4, 0, PF_FOURCC, "DX10", 0, NULL, 71, block, sizeof block);
        image = lrg_dds_image_new_from_bytes (file, &error);
        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_DXT1);
    }
    /* RGBA8 through DXGI_FORMAT_R8G8B8A8_UNORM */
    {
        static const guint8 px[4] = { 1, 2, 3, 4 };
        g_autoptr(GBytes) file = dds_build (1, 1, 0, PF_FOURCC, "DX10", 0, NULL, 28, px, 4);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);
        g_autoptr(GBytes) rgba = NULL;

        g_assert_no_error (error);
        rgba = lrg_dds_image_decode (image, 0, &error);
        assert_rgba (pixel (rgba, 1, 0, 0), 1, 2, 3, 4);
    }
    /* BC6H (DXGI_FORMAT_BC6H_UF16) is not supported */
    {
        g_autoptr(GBytes) file = dds_build (4, 4, 0, PF_FOURCC, "DX10", 0, NULL, 95,
                                            block, sizeof block);
        g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (file, &error);

        g_assert_null (image);
        g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_UNSUPPORTED);
    }
}

static void
test_dds_to_image (void)
{
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GrlImage) grl = NULL;
    g_autoptr(GError) error = NULL;
    guint8 block[8];

    bc1_block (block, RED565, BLUE565, 0);
    file = dds_build (4, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, sizeof block);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    grl = lrg_dds_image_to_image (image, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (grl);
    g_assert_cmpint (grl_image_get_width (grl), ==, 4);
    g_assert_cmpint (grl_image_get_height (grl), ==, 4);
    g_assert_null (lrg_dds_image_to_image (image, 5, &error));
    g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID);
}

static void
expect_failure (GBytes *file,
                gint    code)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgDdsImage) image = NULL;

    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_null (image);
    g_assert_error (error, LRG_DDS_ERROR, code);
}

static void
test_dds_malformed (void)
{
    static const guint32 bgra[4] = { 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u };
    guint8 block[8];
    gsize cut;

    memset (block, 0, sizeof block);

    /* empty, short and wrong magic */
    {
        g_autoptr(GBytes) empty = g_bytes_new_static ("", 0);
        g_autoptr(GBytes) junk = g_bytes_new_static ("not a dds file at all", 21);

        expect_failure (empty, LRG_DDS_ERROR_INVALID);
        expect_failure (junk, LRG_DDS_ERROR_INVALID);
    }
    /* every truncation of a valid file fails cleanly or keeps level 0 */
    {
        g_autoptr(GBytes) file = dds_build (4, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, 8);

        for (cut = 0; cut < g_bytes_get_size (file); cut++)
        {
            g_autoptr(GBytes) part = g_bytes_new_from_bytes (file, 0, cut);
            g_autoptr(GError) error = NULL;
            g_autoptr(LrgDdsImage) image = lrg_dds_image_new_from_bytes (part, &error);

            g_assert_null (image);
            g_assert_nonnull (error);
        }
    }
    /* zero and oversized dimensions */
    {
        g_autoptr(GBytes) zero = dds_build (0, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, 8);
        g_autoptr(GBytes) huge = dds_build (LRG_DDS_IMAGE_MAX_SIZE + 1, 4, 0, PF_FOURCC, "DXT1",
                                            0, NULL, 0, block, 8);

        expect_failure (zero, LRG_DDS_ERROR_INVALID);
        expect_failure (huge, LRG_DDS_ERROR_INVALID);
    }
    /* level 0 truncated */
    {
        g_autoptr(GBytes) file = dds_build (16, 16, 0, PF_FOURCC, "DXT5", 0, NULL, 0, block, 8);

        expect_failure (file, LRG_DDS_ERROR_TRUNCATED);
    }
    /* unknown FourCC, odd bit count, no layout flags, missing masks */
    {
        g_autoptr(GBytes) ati = dds_build (4, 4, 0, PF_FOURCC, "ATI2", 0, NULL, 0, block, 8);
        g_autoptr(GBytes) bits = dds_build (1, 1, 0, PF_RGB, NULL, 12, bgra, 0, block, 8);
        g_autoptr(GBytes) none = dds_build (1, 1, 0, 0, NULL, 32, bgra, 0, block, 8);
        g_autoptr(GBytes) nomask = dds_build (1, 1, 0, PF_RGB, NULL, 32, NULL, 0, block, 8);

        expect_failure (ati, LRG_DDS_ERROR_UNSUPPORTED);
        expect_failure (bits, LRG_DDS_ERROR_UNSUPPORTED);
        expect_failure (none, LRG_DDS_ERROR_UNSUPPORTED);
        expect_failure (nomask, LRG_DDS_ERROR_INVALID);
    }
    /* a corrupted header size */
    {
        g_autoptr(GBytes) file = dds_build (4, 4, 0, PF_FOURCC, "DXT1", 0, NULL, 0, block, 8);
        guint8 *copy = g_memdup2 (g_bytes_get_data (file, NULL), g_bytes_get_size (file));
        g_autoptr(GBytes) bad = NULL;

        copy[4] = 100;
        bad = g_bytes_new_take (copy, g_bytes_get_size (file));
        expect_failure (bad, LRG_DDS_ERROR_INVALID);
    }
    /* a missing file */
    {
        g_autoptr(GError) error = NULL;

        g_assert_null (lrg_dds_image_new_from_file ("/nonexistent/x.dds", &error));
        g_assert_nonnull (error);
    }
}

/* ========================================================================== */
/*                                   BC7                                      */
/* ========================================================================== */

/*
 * The BC7 blocks below are assembled field by field with a bit writer,
 * in the order the BC7 format description lays them out (mode, partition,
 * rotation, index selection, the endpoint fields channel by channel,
 * p-bits, primary indices, secondary indices; least significant bit
 * first).  Every expected pixel is worked out in the comments from the
 * specification's rules:
 *
 *   expand(v, n)     = (v << (8 - n)) | (v >> (2n - 8))   (n bits -> 8)
 *   p-bit            : appended below the field, n grows by one
 *   interpolate      = ((64 - w) * e0 + w * e1 + 32) >> 6
 *   weights, 2 bits  : 0 21 43 64
 *   weights, 3 bits  : 0 9 18 27 37 46 55 64
 *   weights, 4 bits  : 0 4 9 13 17 21 26 30 34 38 43 47 51 55 60 64
 *   anchor indices   : one bit shorter (their high bit is 0)
 */

typedef struct
{
    guint8 bytes[16];
    guint  pos;
} Bc7Writer;

static void
bc7_writer_init (Bc7Writer *w)
{
    memset (w->bytes, 0, sizeof w->bytes);
    w->pos = 0;
}

static void
bc7_put (Bc7Writer *w,
         guint      value,
         guint      count)
{
    guint i;

    g_assert_cmpuint (w->pos + count, <=, 128);
    for (i = 0; i < count; i++)
    {
        if ((value >> i) & 1)
            w->bytes[(w->pos + i) / 8] |= (guint8)(1u << ((w->pos + i) % 8));
    }
    w->pos += count;
}

/* the mode: @mode zero bits, then a one */
static void
bc7_put_mode (Bc7Writer *w,
              guint      mode)
{
    bc7_put (w, 1u << mode, mode + 1);
}

/* sixteen indices of @bits bits; pixels flagged in @anchors (bit i for
 * pixel i) are written one bit shorter */
static void
bc7_put_indices (Bc7Writer   *w,
                 const guint  indices[16],
                 guint        bits,
                 guint        anchors)
{
    guint i;

    for (i = 0; i < 16; i++)
        bc7_put (w, indices[i], ((anchors >> i) & 1) ? bits - 1 : bits);
}

/* every endpoint field: @n_endpoints endpoints of @n_channels channels,
 * written channel-major as the format stores them */
static void
bc7_put_endpoints (Bc7Writer   *w,
                   const guint  endpoints[][4],
                   guint        n_endpoints,
                   guint        n_channels,
                   guint        color_bits,
                   guint        alpha_bits)
{
    guint c;
    guint e;

    for (c = 0; c < n_channels; c++)
    {
        for (e = 0; e < n_endpoints; e++)
            bc7_put (w, endpoints[e][c], c < 3 ? color_bits : alpha_bits);
    }
}

/* decodes one 4x4 block through the bare-block API */
static GBytes *
bc7_decode_one (const Bc7Writer *w)
{
    g_autoptr(GError) error = NULL;
    GBytes *rgba;

    g_assert_cmpuint (w->pos, ==, 128);
    rgba = lrg_dds_decode_bc7 (w->bytes, sizeof w->bytes, 4, 4, &error);
    g_assert_no_error (error);
    g_assert_nonnull (rgba);
    g_assert_cmpuint (g_bytes_get_size (rgba), ==, 4 * 4 * 4);
    return rgba;
}

/* pixel @i of a 4x4 tile (row-major) */
#define TILE(rgba, i) pixel ((rgba), 4, (i) % 4, (i) / 4)

static void
test_dds_bc7_mode0 (void)
{
    /*
     * Mode 0: 3 subsets, 4-bit partition, 4-bit colour + one p-bit per
     * endpoint (5 bits), 3-bit indices.  Partition 0:
     *   0 0 1 1 / 0 0 1 1 / 0 2 2 1 / 2 2 2 2
     * anchors: pixel 0, pixel 3 (subset 1), pixel 15 (subset 2).
     *
     * endpoints (r, g, b, p) -> 5-bit -> 8-bit:
     *   E0 (15, 0, 0, 1)  -> (31, 1, 1)   -> (255, 8, 8)
     *   E1 (0, 15, 0, 0)  -> (0, 30, 0)   -> (0, 247, 0)
     *   E2 (0, 0, 15, 1)  -> (1, 1, 31)   -> (8, 8, 255)
     *   E3 (8, 8, 8, 0)   -> 16           -> 132
     *   E4 (15, 15, 15, 1)-> 31           -> 255
     *   E5 (0, 0, 0, 0)                   -> 0
     */
    static const guint ep[6][4] = {
        { 15, 0, 0, 1 }, { 0, 15, 0, 0 }, { 0, 0, 15, 1 },
        { 8, 8, 8, 0 }, { 15, 15, 15, 1 }, { 0, 0, 0, 0 }
    };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;
    guint e;

    idx[1] = 7;
    idx[2] = 4;
    idx[3] = 3;
    idx[9] = 7;
    idx[15] = 3;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 0);
    bc7_put (&w, 0, 4);
    bc7_put_endpoints (&w, ep, 6, 3, 4, 0);
    for (e = 0; e < 6; e++)
        bc7_put (&w, ep[e][3], 1);
    bc7_put_indices (&w, idx, 3, (1u << 0) | (1u << 3) | (1u << 15));
    rgba = bc7_decode_one (&w);

    /* pixel 0, subset 0, index 0: E0 */
    assert_rgba (TILE (rgba, 0), 255, 8, 8, 255);
    /* pixel 1, subset 0, index 7: E1 */
    assert_rgba (TILE (rgba, 1), 0, 247, 0, 255);
    /* pixel 2, subset 1, index 4 (w 37): r = (27*8 + 37*132 + 32) >> 6
     * = 5132 >> 6 = 80; b = (27*255 + 37*132 + 32) >> 6 = 11801 >> 6 = 184 */
    assert_rgba (TILE (rgba, 2), 80, 80, 184, 255);
    /* pixel 3, subset 1 anchor, 2-bit index 3 (w 27): r = (37*8 + 27*132
     * + 32) >> 6 = 3892 >> 6 = 60; b = (37*255 + 27*132 + 32) >> 6 = 203 */
    assert_rgba (TILE (rgba, 3), 60, 60, 203, 255);
    /* pixel 6, subset 1, index 0: E2 */
    assert_rgba (TILE (rgba, 6), 8, 8, 255, 255);
    /* pixel 9, subset 2, index 7: E5 */
    assert_rgba (TILE (rgba, 9), 0, 0, 0, 255);
    /* pixel 12, subset 2, index 0: E4 */
    assert_rgba (TILE (rgba, 12), 255, 255, 255, 255);
    /* pixel 15, subset 2 anchor, index 3: (37*255 + 32) >> 6 = 147 */
    assert_rgba (TILE (rgba, 15), 147, 147, 147, 255);
}

static void
test_dds_bc7_mode1 (void)
{
    /*
     * Mode 1: 2 subsets, 6-bit partition, 6-bit colour + one shared
     * p-bit per subset (7 bits), 3-bit indices.  Partition 13:
     *   0 0 0 0 / 0 0 0 0 / 1 1 1 1 / 1 1 1 1, second anchor pixel 15.
     *
     * subset 0, p = 1:  E0 (63, 0, 32) -> (127, 1, 65) -> (255, 2, 131)
     *                   E1 (0, 63, 32) -> (1, 127, 65) -> (2, 255, 131)
     * subset 1, p = 0:  E2 10 -> 20 -> 40;  E3 50 -> 100 -> 201
     */
    static const guint ep[4][4] = {
        { 63, 0, 32, 0 }, { 0, 63, 32, 0 }, { 10, 10, 10, 0 }, { 50, 50, 50, 0 }
    };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;

    idx[3] = 3;
    idx[7] = 7;
    idx[8] = 5;
    idx[15] = 3;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 1);
    bc7_put (&w, 13, 6);
    bc7_put_endpoints (&w, ep, 4, 3, 6, 0);
    bc7_put (&w, 1, 1);
    bc7_put (&w, 0, 1);
    bc7_put_indices (&w, idx, 3, (1u << 0) | (1u << 15));
    rgba = bc7_decode_one (&w);

    assert_rgba (TILE (rgba, 0), 255, 2, 131, 255);
    /* pixel 3, index 3 (w 27): r = (37*255 + 27*2 + 32) >> 6 = 9521 >> 6
     * = 148; g = (37*2 + 27*255 + 32) >> 6 = 6991 >> 6 = 109 */
    assert_rgba (TILE (rgba, 3), 148, 109, 131, 255);
    assert_rgba (TILE (rgba, 7), 2, 255, 131, 255);
    /* pixel 8, subset 1, index 5 (w 46): (18*40 + 46*201 + 32) >> 6
     * = 9998 >> 6 = 156 */
    assert_rgba (TILE (rgba, 8), 156, 156, 156, 255);
    assert_rgba (TILE (rgba, 9), 40, 40, 40, 255);
    /* pixel 15, subset 1 anchor, 2-bit index 3 (w 27): (37*40 + 27*201
     * + 32) >> 6 = 6939 >> 6 = 108 */
    assert_rgba (TILE (rgba, 15), 108, 108, 108, 255);
}

static void
test_dds_bc7_mode2 (void)
{
    /*
     * Mode 2: 3 subsets, 6-bit partition, 5-bit colour, no p-bits,
     * 2-bit indices.  Partition 8:
     *   0 0 0 0 / 0 0 0 0 / 1 1 1 1 / 2 2 2 2, anchors 0, 8 and 15.
     *
     *   E0 (31, 0, 0) -> (255, 0, 0)    E1 (0, 0, 31) -> (0, 0, 255)
     *   E2 (0, 31, 0) -> (0, 255, 0)    E3 16 -> 132
     *   E4 31 -> 255                    E5 0 -> 0
     */
    static const guint ep[6][4] = {
        { 31, 0, 0, 0 }, { 0, 0, 31, 0 }, { 0, 31, 0, 0 },
        { 16, 16, 16, 0 }, { 31, 31, 31, 0 }, { 0, 0, 0, 0 }
    };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;

    idx[0] = 1;
    idx[5] = 2;
    idx[8] = 1;
    idx[9] = 3;
    idx[12] = 3;
    idx[15] = 1;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 2);
    bc7_put (&w, 8, 6);
    bc7_put_endpoints (&w, ep, 6, 3, 5, 0);
    bc7_put_indices (&w, idx, 2, (1u << 0) | (1u << 8) | (1u << 15));
    rgba = bc7_decode_one (&w);

    /* pixel 0, anchor, 1-bit index 1 (w 21): r = (43*255 + 32) >> 6 = 171,
     * b = (21*255 + 32) >> 6 = 84 */
    assert_rgba (TILE (rgba, 0), 171, 0, 84, 255);
    assert_rgba (TILE (rgba, 1), 255, 0, 0, 255);
    /* pixel 5, index 2 (w 43): the mirror image */
    assert_rgba (TILE (rgba, 5), 84, 0, 171, 255);
    /* pixel 8, subset 1 anchor, index 1 (w 21): r = (21*132 + 32) >> 6
     * = 43; g = (43*255 + 21*132 + 32) >> 6 = 13769 >> 6 = 215 */
    assert_rgba (TILE (rgba, 8), 43, 215, 43, 255);
    assert_rgba (TILE (rgba, 9), 132, 132, 132, 255);
    assert_rgba (TILE (rgba, 10), 0, 255, 0, 255);
    assert_rgba (TILE (rgba, 12), 0, 0, 0, 255);
    assert_rgba (TILE (rgba, 13), 255, 255, 255, 255);
    /* pixel 15, subset 2 anchor, index 1: (43*255 + 32) >> 6 = 171 */
    assert_rgba (TILE (rgba, 15), 171, 171, 171, 255);
}

static void
test_dds_bc7_mode3 (void)
{
    /*
     * Mode 3: 2 subsets, 6-bit partition, 7-bit colour + one p-bit per
     * endpoint (8 bits, used as is), 2-bit indices.  Partition 17:
     *   0 1 1 1 / 0 0 0 1 / 0 0 0 0 / 0 0 0 0
     * whose second anchor is pixel 2, not the subset's first pixel (1):
     * pixel 1 keeps both index bits, pixel 2 has one.
     *
     *   E0 (100, 0, 0, p 1)   -> (201, 1, 1)
     *   E1 (0, 100, 0, p 0)   -> (0, 200, 0)
     *   E2 (127, 127, 127, 1) -> 255
     *   E3 (0, 0, 0, p 1)     -> 1
     */
    static const guint ep[4][4] = {
        { 100, 0, 0, 1 }, { 0, 100, 0, 0 }, { 127, 127, 127, 1 }, { 0, 0, 0, 1 }
    };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;
    guint e;

    idx[0] = 1;
    idx[1] = 3;
    idx[2] = 1;
    idx[7] = 2;
    idx[15] = 3;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 3);
    bc7_put (&w, 17, 6);
    bc7_put_endpoints (&w, ep, 4, 3, 7, 0);
    for (e = 0; e < 4; e++)
        bc7_put (&w, ep[e][3], 1);
    bc7_put_indices (&w, idx, 2, (1u << 0) | (1u << 2));
    rgba = bc7_decode_one (&w);

    /* pixel 0, index 1 (w 21): r = (43*201 + 32) >> 6 = 8675 >> 6 = 135;
     * g = (43*1 + 21*200 + 32) >> 6 = 4275 >> 6 = 66; b = 75 >> 6 = 1 */
    assert_rgba (TILE (rgba, 0), 135, 66, 1, 255);
    /* pixel 1, subset 1, full index 3: E3 */
    assert_rgba (TILE (rgba, 1), 1, 1, 1, 255);
    /* pixel 2, subset 1 anchor, 1-bit index 1 (w 21): (43*255 + 21*1 + 32)
     * >> 6 = 11018 >> 6 = 172 */
    assert_rgba (TILE (rgba, 2), 172, 172, 172, 255);
    assert_rgba (TILE (rgba, 3), 255, 255, 255, 255);
    assert_rgba (TILE (rgba, 4), 201, 1, 1, 255);
    /* pixel 7, index 2 (w 43): (21*255 + 43*1 + 32) >> 6 = 5430 >> 6 = 84 */
    assert_rgba (TILE (rgba, 7), 84, 84, 84, 255);
    assert_rgba (TILE (rgba, 15), 0, 200, 0, 255);
}

static void
test_dds_bc7_mode4 (void)
{
    /*
     * Mode 4: 1 subset, 2-bit rotation, index selection bit, 5-bit
     * colour, 6-bit alpha, 2-bit primary and 3-bit secondary indices.
     * Rotation 1 swaps red and alpha after interpolation; the selection
     * bit 1 takes colour from the 3-bit set and alpha from the 2-bit set.
     *
     *   E0 colour (31, 0, 10) -> (255, 0, 82), alpha 63 -> 255
     *   E1 colour (0, 31, 20) -> (0, 255, 165), alpha 32 -> 130
     */
    static const guint ep[2][4] = { { 31, 0, 10, 63 }, { 0, 31, 20, 32 } };
    guint idx2[16] = { 0 };
    guint idx3[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;

    idx2[0] = 1;
    idx2[5] = 3;
    idx3[0] = 3;
    idx3[5] = 7;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 4);
    bc7_put (&w, 1, 2);
    bc7_put (&w, 1, 1);
    bc7_put_endpoints (&w, ep, 2, 4, 5, 6);
    bc7_put_indices (&w, idx2, 2, 1u << 0);
    bc7_put_indices (&w, idx3, 3, 1u << 0);
    rgba = bc7_decode_one (&w);

    /* pixel 0: colour index 3 of 3 bits (w 27): r = (37*255 + 32) >> 6
     * = 147, g = (27*255 + 32) >> 6 = 108, b = (37*82 + 27*165 + 32) >> 6
     * = 7521 >> 6 = 117; alpha index 1 of 2 bits (w 21): (43*255 +
     * 21*130 + 32) >> 6 = 13727 >> 6 = 214.  Rotated: (214, 108, 117, 147) */
    assert_rgba (TILE (rgba, 0), 214, 108, 117, 147);
    /* pixel 5: colour E1, alpha 130; rotated */
    assert_rgba (TILE (rgba, 5), 130, 255, 165, 0);
    /* the rest: colour E0, alpha 255; rotated */
    assert_rgba (TILE (rgba, 1), 255, 0, 82, 255);
    assert_rgba (TILE (rgba, 15), 255, 0, 82, 255);
}

static void
test_dds_bc7_mode5 (void)
{
    /*
     * Mode 5: 1 subset, 2-bit rotation, 7-bit colour, 8-bit alpha,
     * 2-bit colour and 2-bit alpha indices.  Rotation 2 swaps green and
     * alpha.
     *
     *   E0 colour (127, 64, 0) -> (255, 129, 0), alpha 200
     *   E1 colour (0, 0, 127)  -> (0, 0, 255),   alpha 100
     */
    static const guint ep[2][4] = { { 127, 64, 0, 200 }, { 0, 0, 127, 100 } };
    guint idx_color[16] = { 0 };
    guint idx_alpha[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;

    idx_color[0] = 1;
    idx_color[10] = 2;
    idx_alpha[10] = 3;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 5);
    bc7_put (&w, 2, 2);
    bc7_put_endpoints (&w, ep, 2, 4, 7, 8);
    bc7_put_indices (&w, idx_color, 2, 1u << 0);
    bc7_put_indices (&w, idx_alpha, 2, 1u << 0);
    rgba = bc7_decode_one (&w);

    /* pixel 0: colour index 1 (w 21): r = (43*255 + 32) >> 6 = 171,
     * g = (43*129 + 32) >> 6 = 5579 >> 6 = 87, b = (21*255 + 32) >> 6 = 84;
     * alpha index 0: 200.  Rotated: (171, 200, 84, 87) */
    assert_rgba (TILE (rgba, 0), 171, 200, 84, 87);
    /* pixel 10: colour index 2 (w 43): r = 84, g = (21*129 + 32) >> 6
     * = 2741 >> 6 = 42, b = 171; alpha index 3: 100.  Rotated */
    assert_rgba (TILE (rgba, 10), 84, 100, 171, 42);
    assert_rgba (TILE (rgba, 1), 255, 200, 0, 129);
}

static void
test_dds_bc7_mode6 (void)
{
    /*
     * Mode 6: 1 subset, 7-bit colour and alpha + one p-bit per endpoint
     * (8 bits, used as is), 4-bit indices.
     *
     *   E0 (10, 20, 30, 127, p 1) -> (21, 41, 61, 255)
     *   E1 (110, 90, 70, 0, p 0)  -> (220, 180, 140, 0)
     */
    static const guint ep[2][4] = { { 10, 20, 30, 127 }, { 110, 90, 70, 0 } };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;

    idx[0] = 7;
    idx[1] = 15;
    idx[2] = 8;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 6);
    bc7_put_endpoints (&w, ep, 2, 4, 7, 7);
    bc7_put (&w, 1, 1);
    bc7_put (&w, 0, 1);
    bc7_put_indices (&w, idx, 4, 1u << 0);
    rgba = bc7_decode_one (&w);

    /* pixel 0, anchor, 3-bit index 7 (w 30): r = (34*21 + 30*220 + 32) >> 6
     * = 7346 >> 6 = 114, g = 6826 >> 6 = 106, b = 6306 >> 6 = 98,
     * a = (34*255 + 32) >> 6 = 8702 >> 6 = 135 */
    assert_rgba (TILE (rgba, 0), 114, 106, 98, 135);
    assert_rgba (TILE (rgba, 1), 220, 180, 140, 0);
    /* pixel 2, index 8 (w 34): r = (30*21 + 34*220 + 32) >> 6 = 8142 >> 6
     * = 127, g = 7382 >> 6 = 115, b = 6622 >> 6 = 103, a = (30*255 + 32)
     * >> 6 = 120 */
    assert_rgba (TILE (rgba, 2), 127, 115, 103, 120);
    assert_rgba (TILE (rgba, 3), 21, 41, 61, 255);
}

static void
test_dds_bc7_mode7 (void)
{
    /*
     * Mode 7: 2 subsets, 6-bit partition, 5-bit colour and alpha + one
     * p-bit per endpoint (6 bits), 2-bit indices.  Partition 0:
     *   0 0 1 1 / 0 0 1 1 / 0 0 1 1 / 0 0 1 1, second anchor pixel 15.
     *
     *   E0 (31, 0, 0, 31, p 1)  -> (255, 4, 4, 255)
     *   E1 (0, 31, 0, 0, p 0)   -> (0, 251, 0, 0)
     *   E2 (0, 0, 31, 16, p 1)  -> (4, 4, 255, 134)
     *   E3 (16, 16, 16, 8, p 0) -> (130, 130, 130, 65)
     */
    static const guint ep[4][4] = {
        { 31, 0, 0, 31 }, { 0, 31, 0, 0 }, { 0, 0, 31, 16 }, { 16, 16, 16, 8 }
    };
    static const guint pbits[4] = { 1, 0, 1, 0 };
    guint idx[16] = { 0 };
    g_autoptr(GBytes) rgba = NULL;
    Bc7Writer w;
    guint e;

    idx[0] = 1;
    idx[1] = 3;
    idx[2] = 2;
    idx[15] = 1;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 7);
    bc7_put (&w, 0, 6);
    bc7_put_endpoints (&w, ep, 4, 4, 5, 5);
    for (e = 0; e < 4; e++)
        bc7_put (&w, pbits[e], 1);
    bc7_put_indices (&w, idx, 2, (1u << 0) | (1u << 15));
    rgba = bc7_decode_one (&w);

    /* pixel 0, 1-bit index 1 (w 21): r = 171, g = (43*4 + 21*251 + 32)
     * >> 6 = 5475 >> 6 = 85, b = (43*4 + 32) >> 6 = 3, a = 171 */
    assert_rgba (TILE (rgba, 0), 171, 85, 3, 171);
    assert_rgba (TILE (rgba, 1), 0, 251, 0, 0);
    /* pixel 2, subset 1, index 2 (w 43): r = (21*4 + 43*130 + 32) >> 6
     * = 89, b = (21*255 + 43*130 + 32) >> 6 = 10977 >> 6 = 171,
     * a = (21*134 + 43*65 + 32) >> 6 = 5641 >> 6 = 88 */
    assert_rgba (TILE (rgba, 2), 89, 89, 171, 88);
    assert_rgba (TILE (rgba, 3), 4, 4, 255, 134);
    assert_rgba (TILE (rgba, 4), 255, 4, 4, 255);
    /* pixel 15, subset 1 anchor, index 1 (w 21): r = (43*4 + 21*130 + 32)
     * >> 6 = 45, b = (43*255 + 21*130 + 32) >> 6 = 214, a = (43*134 +
     * 21*65 + 32) >> 6 = 7159 >> 6 = 111 */
    assert_rgba (TILE (rgba, 15), 45, 45, 214, 111);
}

/* a solid mode 6 block: both endpoints (r, g, b, a) with p-bit 0, so
 * every pixel is (2r, 2g, 2b, 2a) whatever its index */
static void
bc7_solid (guint8 *out,
           guint   r,
           guint   g,
           guint   b,
           guint   a)
{
    guint ep[2][4];
    guint idx[16] = { 0 };
    Bc7Writer w;

    ep[0][0] = ep[1][0] = r;
    ep[0][1] = ep[1][1] = g;
    ep[0][2] = ep[1][2] = b;
    ep[0][3] = ep[1][3] = a;
    bc7_writer_init (&w);
    bc7_put_mode (&w, 6);
    bc7_put_endpoints (&w, (const guint (*)[4])ep, 2, 4, 7, 7);
    bc7_put (&w, 0, 2);
    bc7_put_indices (&w, idx, 4, 1u << 0);
    memcpy (out, w.bytes, 16);
}

static void
test_dds_bc7_reserved (void)
{
    guint8 block[16];
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    /* first byte 0: no mode bit; the rest is ignored and every pixel is
     * transparent black */
    memset (block, 0xff, sizeof block);
    block[0] = 0;
    rgba = lrg_dds_decode_bc7 (block, sizeof block, 4, 4, &error);
    g_assert_no_error (error);
    for (i = 0; i < 16; i++)
        assert_rgba (TILE (rgba, i), 0, 0, 0, 0);
}

static void
test_dds_bc7_odd_size (void)
{
    guint8 blocks[4 * 16];
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GError) error = NULL;

    /* 5 x 6: 2 x 2 blocks, red / green over blue / white; the right
     * column and bottom row of blocks are clipped */
    bc7_solid (blocks, 127, 0, 0, 127);
    bc7_solid (blocks + 16, 0, 127, 0, 127);
    bc7_solid (blocks + 32, 0, 0, 127, 127);
    bc7_solid (blocks + 48, 127, 127, 127, 64);
    rgba = lrg_dds_decode_bc7 (blocks, sizeof blocks, 5, 6, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (rgba), ==, 5 * 6 * 4);
    assert_rgba (pixel (rgba, 5, 3, 3), 254, 0, 0, 254);
    assert_rgba (pixel (rgba, 5, 4, 0), 0, 254, 0, 254);
    assert_rgba (pixel (rgba, 5, 0, 5), 0, 0, 254, 254);
    assert_rgba (pixel (rgba, 5, 4, 5), 254, 254, 254, 128);
    g_clear_pointer (&rgba, g_bytes_unref);

    /* 1 x 1: the top-left pixel of one block; trailing bytes ignored */
    rgba = lrg_dds_decode_bc7 (blocks + 16, 48, 1, 1, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (rgba), ==, 4);
    assert_rgba (pixel (rgba, 1, 0, 0), 0, 254, 0, 254);
}

static void
test_dds_bc7_errors (void)
{
    guint8 blocks[4 * 16];
    gsize cut;

    memset (blocks, 0x40, sizeof blocks);
    /* every length short of 2 x 2 blocks is truncated */
    for (cut = 0; cut < sizeof blocks; cut++)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GBytes) rgba = lrg_dds_decode_bc7 (blocks, cut, 8, 5, &error);

        g_assert_null (rgba);
        g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_TRUNCATED);
    }
    /* no data, zero and oversized sizes */
    {
        g_autoptr(GError) error = NULL;

        g_assert_null (lrg_dds_decode_bc7 (NULL, 0, 4, 4, &error));
        g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_TRUNCATED);
        g_clear_error (&error);
        g_assert_null (lrg_dds_decode_bc7 (blocks, sizeof blocks, 0, 4, &error));
        g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID);
        g_clear_error (&error);
        g_assert_null (lrg_dds_decode_bc7 (blocks, sizeof blocks, 4,
                                           LRG_DDS_IMAGE_MAX_SIZE + 1, &error));
        g_assert_error (error, LRG_DDS_ERROR, LRG_DDS_ERROR_INVALID);
    }
}

static void
test_dds_bc7_dx10 (void)
{
    guint8 payload[4 * 16 + 16 + 16];
    g_autoptr(GBytes) file = NULL;
    g_autoptr(LrgDdsImage) image = NULL;
    g_autoptr(GBytes) rgba = NULL;
    g_autoptr(GBytes) bare = NULL;
    g_autoptr(GBytes) raw = NULL;
    g_autoptr(GError) error = NULL;
    guint w;
    guint h;

    /* a 6 x 8 DXGI_FORMAT_BC7_UNORM file with three levels: 2 x 2 blocks,
     * then 3 x 4 (one block), then 1 x 2 (one block) */
    bc7_solid (payload, 127, 0, 0, 127);
    bc7_solid (payload + 16, 0, 127, 0, 127);
    bc7_solid (payload + 32, 0, 0, 127, 127);
    bc7_solid (payload + 48, 64, 64, 64, 32);
    bc7_solid (payload + 64, 10, 20, 30, 40);
    payload[80] = 0;  /* reserved-mode last level */
    memset (payload + 81, 0x55, 15);
    file = dds_build (6, 8, 3, PF_FOURCC, "DX10", 0, NULL, 98, payload, sizeof payload);
    image = lrg_dds_image_new_from_bytes (file, &error);
    g_assert_no_error (error);
    g_assert_nonnull (image);
    g_assert_cmpint (lrg_dds_image_get_format (image), ==, LRG_DDS_FORMAT_BC7);
    g_assert_true (lrg_dds_image_has_alpha (image));
    g_assert_cmpuint (lrg_dds_image_get_mip_count (image), ==, 3);
    g_assert_true (lrg_dds_image_get_mip_size (image, 2, &w, &h));
    g_assert_cmpuint (w, ==, 1);
    g_assert_cmpuint (h, ==, 2);
    raw = lrg_dds_image_get_mip_data (image, 0);
    g_assert_cmpuint (g_bytes_get_size (raw), ==, 64);

    /* level 0 through the DDS reader equals the bare-block decode */
    rgba = lrg_dds_image_decode (image, 0, &error);
    g_assert_no_error (error);
    bare = lrg_dds_decode_bc7 (payload, 64, 6, 8, &error);
    g_assert_no_error (error);
    g_assert_true (g_bytes_equal (rgba, bare));
    assert_rgba (pixel (rgba, 6, 0, 0), 254, 0, 0, 254);
    assert_rgba (pixel (rgba, 6, 5, 7), 128, 128, 128, 64);
    g_clear_pointer (&rgba, g_bytes_unref);

    rgba = lrg_dds_image_decode (image, 1, &error);
    g_assert_no_error (error);
    assert_rgba (pixel (rgba, 3, 2, 3), 20, 40, 60, 80);
    g_clear_pointer (&rgba, g_bytes_unref);
    rgba = lrg_dds_image_decode (image, 2, &error);
    g_assert_no_error (error);
    assert_rgba (pixel (rgba, 1, 0, 1), 0, 0, 0, 0);

    /* the sRGB variant reads the same way (values as stored) */
    {
        g_autoptr(GBytes) srgb = dds_build (4, 4, 0, PF_FOURCC, "DX10", 0, NULL, 99,
                                            payload, 16);
        g_autoptr(LrgDdsImage) image2 = lrg_dds_image_new_from_bytes (srgb, &error);
        g_autoptr(GBytes) px = NULL;

        g_assert_no_error (error);
        g_assert_cmpint (lrg_dds_image_get_format (image2), ==, LRG_DDS_FORMAT_BC7);
        px = lrg_dds_image_decode (image2, 0, &error);
        g_assert_no_error (error);
        assert_rgba (pixel (px, 4, 3, 3), 254, 0, 0, 254);
    }
    /* level 0 cut short */
    {
        g_autoptr(GBytes) cut = dds_build (8, 8, 0, PF_FOURCC, "DX10", 0, NULL, 98,
                                           payload, 63);

        expect_failure (cut, LRG_DDS_ERROR_TRUNCATED);
    }
}

gint
main (gint    argc,
      gchar **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/dds-image/dxt1-four-colour", test_dds_dxt1_four_colour);
    g_test_add_func ("/dds-image/dxt1-three-colour", test_dds_dxt1_three_colour);
    g_test_add_func ("/dds-image/dxt3-alpha", test_dds_dxt3_alpha);
    g_test_add_func ("/dds-image/dxt5-alpha", test_dds_dxt5_alpha);
    g_test_add_func ("/dds-image/odd-size-and-mips", test_dds_odd_size_and_mips);
    g_test_add_func ("/dds-image/short-mip-chain", test_dds_short_mip_chain);
    g_test_add_func ("/dds-image/uncompressed", test_dds_uncompressed);
    g_test_add_func ("/dds-image/dx10", test_dds_dx10);
    g_test_add_func ("/dds-image/to-image", test_dds_to_image);
    g_test_add_func ("/dds-image/malformed", test_dds_malformed);
    g_test_add_func ("/dds-image/bc7-mode0", test_dds_bc7_mode0);
    g_test_add_func ("/dds-image/bc7-mode1", test_dds_bc7_mode1);
    g_test_add_func ("/dds-image/bc7-mode2", test_dds_bc7_mode2);
    g_test_add_func ("/dds-image/bc7-mode3", test_dds_bc7_mode3);
    g_test_add_func ("/dds-image/bc7-mode4", test_dds_bc7_mode4);
    g_test_add_func ("/dds-image/bc7-mode5", test_dds_bc7_mode5);
    g_test_add_func ("/dds-image/bc7-mode6", test_dds_bc7_mode6);
    g_test_add_func ("/dds-image/bc7-mode7", test_dds_bc7_mode7);
    g_test_add_func ("/dds-image/bc7-reserved", test_dds_bc7_reserved);
    g_test_add_func ("/dds-image/bc7-odd-size", test_dds_bc7_odd_size);
    g_test_add_func ("/dds-image/bc7-errors", test_dds_bc7_errors);
    g_test_add_func ("/dds-image/bc7-dx10", test_dds_bc7_dx10);

    return g_test_run ();
}
