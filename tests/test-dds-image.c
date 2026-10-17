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
 * and every malformed-input failure.  Fixtures are built in memory.
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
    /* BC7 is not supported */
    {
        g_autoptr(GBytes) file = dds_build (4, 4, 0, PF_FOURCC, "DX10", 0, NULL, 98,
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

    return g_test_run ();
}
