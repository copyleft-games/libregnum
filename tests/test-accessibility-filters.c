/* test-accessibility-filters.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The CPU references of the accessibility and retro post effects: the
 * colorblind filter's matrices on known colours, the palettize effect's
 * nearest colour, lookup table and ordered dither, and the 3D camera's
 * aspect override.  No window is needed (the shaders mirror these).
 */

#define LIBREGNUM_COMPILATION 1

#include <glib.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>
#include "../src/libregnum.h"

#define TOL (0.005f)

static void
assert_rgb (const gfloat *got,
            gfloat        r,
            gfloat        g,
            gfloat        b,
            gfloat        tolerance)
{
    g_assert_cmpfloat_with_epsilon (got[0], r, tolerance);
    g_assert_cmpfloat_with_epsilon (got[1], g, tolerance);
    g_assert_cmpfloat_with_epsilon (got[2], b, tolerance);
}

static gfloat
distance3 (const gfloat *a,
           const gfloat *b)
{
    return sqrtf ((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                  (a[2] - b[2]) * (a[2] - b[2]));
}

/* --- the colorblind filter --- */

static void
test_colorblind_identity (void)
{
    g_autoptr(LrgColorblindFilter) filter = NULL;
    const gfloat colour[3] = { 0.8f, 0.3f, 0.1f };
    gfloat out[3];
    gfloat m[9];

    /* no type: nothing changes */
    filter = lrg_colorblind_filter_new ();
    g_assert_true (lrg_colorblind_filter_is_identity (filter));
    lrg_colorblind_filter_apply_rgb (filter, colour, out);
    assert_rgb (out, 0.8f, 0.3f, 0.1f, 1e-4f);
    lrg_colorblind_filter_get_matrix (filter, m);
    g_assert_cmpfloat (m[0], ==, 1.0f);
    g_assert_cmpfloat (m[1], ==, 0.0f);
    g_assert_cmpfloat (m[4], ==, 1.0f);

    /* a strength of 0 is the identity too */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_DEUTERANOPIA);
    g_assert_false (lrg_colorblind_filter_is_identity (filter));
    lrg_colorblind_filter_set_strength (filter, 0.0f);
    g_assert_true (lrg_colorblind_filter_is_identity (filter));
    lrg_colorblind_filter_apply_rgb (filter, colour, out);
    assert_rgb (out, 0.8f, 0.3f, 0.1f, 1e-4f);

    /* achromatopsia has nothing to correct into */
    lrg_colorblind_filter_set_strength (filter, 1.0f);
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_ACHROMATOPSIA);
    lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_CORRECT);
    g_assert_true (lrg_colorblind_filter_is_identity (filter));

    /* an effect intensity of 0 */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_PROTANOPIA);
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (filter), 0.0f);
    g_assert_true (lrg_colorblind_filter_is_identity (filter));
    lrg_colorblind_filter_apply_rgb (filter, colour, out);
    assert_rgb (out, 0.8f, 0.3f, 0.1f, 1e-4f);
}

/* white and black stay themselves under every simulation */
static void
test_colorblind_neutrals (void)
{
    static const LrgColorblindType types[4] = {
        LRG_COLORBLIND_PROTANOPIA, LRG_COLORBLIND_DEUTERANOPIA,
        LRG_COLORBLIND_TRITANOPIA, LRG_COLORBLIND_ACHROMATOPSIA
    };
    const gfloat white[3] = { 1.0f, 1.0f, 1.0f };
    const gfloat black[3] = { 0.0f, 0.0f, 0.0f };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (types); i++)
    {
        g_autoptr(LrgColorblindFilter) filter = NULL;
        gfloat out[3];

        filter = lrg_colorblind_filter_new_with_type (types[i]);
        lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_SIMULATE);
        lrg_colorblind_filter_apply_rgb (filter, white, out);
        assert_rgb (out, 1.0f, 1.0f, 1.0f, TOL);
        lrg_colorblind_filter_apply_rgb (filter, black, out);
        assert_rgb (out, 0.0f, 0.0f, 0.0f, TOL);
        /* correction keeps them too (the error is 0) */
        lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_CORRECT);
        lrg_colorblind_filter_apply_rgb (filter, white, out);
        assert_rgb (out, 1.0f, 1.0f, 1.0f, TOL);
    }
}

/* the simulations of primaries, worked by hand from Machado et al. */
static void
test_colorblind_simulate_known (void)
{
    g_autoptr(LrgColorblindFilter) filter = NULL;
    const gfloat red[3] = { 1.0f, 0.0f, 0.0f };
    const gfloat green[3] = { 0.0f, 1.0f, 0.0f };
    const gfloat blue[3] = { 0.0f, 0.0f, 1.0f };
    gfloat out[3];

    filter = lrg_colorblind_filter_new_with_type (LRG_COLORBLIND_DEUTERANOPIA);
    lrg_colorblind_filter_set_mode (filter, LRG_COLORBLIND_MODE_SIMULATE);
    /* (0.367, 0.280, -0.012) in linear light: a dull olive */
    lrg_colorblind_filter_apply_rgb (filter, red, out);
    assert_rgb (out, 0.634f, 0.561f, 0.0f, TOL);

    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_PROTANOPIA);
    lrg_colorblind_filter_apply_rgb (filter, red, out);
    assert_rgb (out, 0.425f, 0.373f, 0.0f, TOL);

    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_TRITANOPIA);
    lrg_colorblind_filter_apply_rgb (filter, blue, out);
    assert_rgb (out, 0.0f, 0.419f, 0.582f, TOL);

    /* achromatopsia: green's luminance on every channel */
    lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_ACHROMATOPSIA);
    lrg_colorblind_filter_apply_rgb (filter, green, out);
    assert_rgb (out, 0.859f, 0.859f, 0.859f, TOL);

    /* half strength: the matrix is halfway to the identity */
    {
        gfloat full[9];
        gfloat half[9];
        guint k;

        lrg_colorblind_filter_set_filter_type (filter, LRG_COLORBLIND_DEUTERANOPIA);
        lrg_colorblind_filter_get_matrix (filter, full);
        lrg_colorblind_filter_set_strength (filter, 0.5f);
        lrg_colorblind_filter_get_matrix (filter, half);
        g_assert_cmpfloat_with_epsilon (full[0], 0.367322f, 1e-5f);
        for (k = 0; k < 9; k++)
        {
            gfloat id = (k % 4 == 0) ? 1.0f : 0.0f;

            g_assert_cmpfloat_with_epsilon (half[k], (full[k] + id) * 0.5f, 1e-5f);
        }
    }
}

/* correction pushes colours a viewer confuses apart (as that viewer sees them) */
static void
check_separates (LrgColorblindType type,
                 const gfloat     *a,
                 const gfloat     *b)
{
    g_autoptr(LrgColorblindFilter) sim = NULL;
    g_autoptr(LrgColorblindFilter) fix = NULL;
    gfloat sa[3];
    gfloat sb[3];
    gfloat ca[3];
    gfloat cb[3];
    gfloat before;
    gfloat after;

    sim = lrg_colorblind_filter_new_with_type (type);
    lrg_colorblind_filter_set_mode (sim, LRG_COLORBLIND_MODE_SIMULATE);
    fix = lrg_colorblind_filter_new_with_type (type);
    g_assert_cmpint (lrg_colorblind_filter_get_mode (fix), ==, LRG_COLORBLIND_MODE_CORRECT);

    lrg_colorblind_filter_apply_rgb (sim, a, sa);
    lrg_colorblind_filter_apply_rgb (sim, b, sb);
    before = distance3 (sa, sb);
    lrg_colorblind_filter_apply_rgb (fix, a, ca);
    lrg_colorblind_filter_apply_rgb (fix, b, cb);
    lrg_colorblind_filter_apply_rgb (sim, ca, sa);
    lrg_colorblind_filter_apply_rgb (sim, cb, sb);
    after = distance3 (sa, sb);
    g_assert_cmpfloat (after, >, before + 0.02f);
}

static void
test_colorblind_correct_separates (void)
{
    const gfloat red[3] = { 1.0f, 0.0f, 0.0f };
    const gfloat green[3] = { 0.0f, 1.0f, 0.0f };
    const gfloat blue[3] = { 0.0f, 0.0f, 1.0f };
    const gfloat teal[3] = { 0.0f, 0.5f, 0.5f };

    check_separates (LRG_COLORBLIND_PROTANOPIA, red, green);
    check_separates (LRG_COLORBLIND_DEUTERANOPIA, red, green);
    check_separates (LRG_COLORBLIND_TRITANOPIA, blue, teal);
}

static void
test_colorblind_shader_source (void)
{
    const gchar *fs;

    fs = lrg_colorblind_filter_get_fragment_shader ();
    g_assert_nonnull (strstr (fs, "uniform vec3 row0;"));
    g_assert_nonnull (strstr (fs, "pow("));
}

/* --- palettize --- */

static GBytes *
random_palette (GRand *rand,
                guint  count)
{
    guint8 *rgb;
    guint i;

    rgb = g_new (guint8, count * 3);
    for (i = 0; i < count * 3; i++)
        rgb[i] = (guint8)g_rand_int_range (rand, 0, 256);
    return g_bytes_new_take (rgb, count * 3);
}

/* the search the tests trust: every entry, the lowest index on a tie */
static guint
brute_nearest (const guint8 *pal,
               guint         count,
               gint          r,
               gint          g,
               gint          b)
{
    guint best = 0;
    gint best_d = G_MAXINT;
    guint i;

    for (i = 0; i < count; i++)
    {
        gint d = (r - pal[i * 3]) * (r - pal[i * 3]) + (g - pal[i * 3 + 1]) * (g - pal[i * 3 + 1]) +
                 (b - pal[i * 3 + 2]) * (b - pal[i * 3 + 2]);

        if (d < best_d)
        {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static void
test_palettize_nearest (void)
{
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GRand) rand = NULL;
    g_autoptr(GBytes) palette = NULL;
    g_autoptr(GError) error = NULL;
    const guint8 *pal;
    guint i;

    rand = g_rand_new_with_seed (1234);
    palette = random_palette (rand, 256);
    pal = g_bytes_get_data (palette, NULL);
    fx = lrg_palettize_new ();
    g_assert_true (lrg_palettize_is_identity (fx));
    g_assert_true (lrg_palettize_set_palette (fx, palette, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_palettize_get_palette_size (fx), ==, 256);
    g_assert_false (lrg_palettize_is_identity (fx));

    /* random colours agree with the brute force */
    for (i = 0; i < 4000; i++)
    {
        guint8 r = (guint8)g_rand_int_range (rand, 0, 256);
        guint8 g = (guint8)g_rand_int_range (rand, 0, 256);
        guint8 b = (guint8)g_rand_int_range (rand, 0, 256);

        g_assert_cmpuint (lrg_palettize_find_nearest (fx, r, g, b), ==,
                          brute_nearest (pal, 256, r, g, b));
    }
    /* a palette colour is its own nearest (at the lowest index holding it) */
    for (i = 0; i < 256; i++)
    {
        guint got;
        guint8 r;
        guint8 g;
        guint8 b;

        got = lrg_palettize_find_nearest (fx, pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
        g_assert_cmpuint (got, <=, i);
        g_assert_true (lrg_palettize_get_color (fx, got, &r, &g, &b));
        g_assert_cmpuint (r, ==, pal[i * 3]);
        g_assert_cmpuint (g, ==, pal[i * 3 + 1]);
        g_assert_cmpuint (b, ==, pal[i * 3 + 2]);
    }
    g_assert_false (lrg_palettize_get_color (fx, 256, NULL, NULL, NULL));
}

static void
test_palettize_ties (void)
{
    static const guint8 rgb[12] = { 0, 0, 0,  100, 100, 100,  100, 100, 100,  200, 0, 0 };
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GBytes) palette = NULL;

    palette = g_bytes_new_static (rgb, sizeof (rgb));
    fx = lrg_palettize_new ();
    g_assert_true (lrg_palettize_set_palette (fx, palette, NULL));
    /* the duplicate grey: the lower index */
    g_assert_cmpuint (lrg_palettize_find_nearest (fx, 100, 100, 100), ==, 1);
    g_assert_cmpuint (lrg_palettize_find_nearest (fx, 110, 95, 100), ==, 1);
    /* half way between black (0) and grey (1): equal distance, black */
    g_assert_cmpuint (lrg_palettize_find_nearest (fx, 50, 50, 50), ==, 0);
    g_assert_cmpuint (lrg_palettize_find_nearest (fx, 51, 51, 51), ==, 1);
    g_assert_cmpuint (lrg_palettize_find_nearest (fx, 255, 0, 0), ==, 3);
}

static void
test_palettize_lut (void)
{
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GRand) rand = NULL;
    g_autoptr(GBytes) palette = NULL;
    const guint8 *pal;
    guint r;
    guint g;
    guint b;

    rand = g_rand_new_with_seed (99);
    palette = random_palette (rand, 64);
    pal = g_bytes_get_data (palette, NULL);
    fx = lrg_palettize_new ();
    g_assert_cmpuint (lrg_palettize_get_lut_index (fx, 0, 0, 0), ==, 0);
    g_assert_true (lrg_palettize_set_palette (fx, palette, NULL));

    /* every cell holds the nearest colour to its own colour */
    for (r = 0; r < LRG_PALETTIZE_LUT_SIZE; r++)
        for (g = 0; g < LRG_PALETTIZE_LUT_SIZE; g++)
            for (b = 0; b < LRG_PALETTIZE_LUT_SIZE; b++)
                g_assert_cmpuint (lrg_palettize_get_lut_index (fx, r, g, b), ==,
                                  brute_nearest (pal, 64, (gint)((r * 255 + 15) / 31),
                                                 (gint)((g * 255 + 15) / 31),
                                                 (gint)((b * 255 + 15) / 31)));
    /* out of range */
    g_assert_cmpuint (lrg_palettize_get_lut_index (fx, 32, 0, 0), ==, 0);
}

static void
test_palettize_apply (void)
{
    static const guint8 rgb[9] = { 0, 0, 0,  255, 255, 255,  255, 0, 0 };
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GBytes) palette = NULL;
    const gfloat dark_red[3] = { 0.7f, 0.1f, 0.05f };
    const gfloat light[3] = { 0.9f, 0.85f, 0.8f };
    gfloat out[3];

    fx = lrg_palettize_new ();
    /* no palette: unchanged */
    lrg_palettize_apply_rgb (fx, 0, 0, dark_red, out);
    assert_rgb (out, 0.7f, 0.1f, 0.05f, 1e-6f);

    palette = g_bytes_new_static (rgb, sizeof (rgb));
    g_assert_true (lrg_palettize_set_palette (fx, palette, NULL));
    lrg_palettize_apply_rgb (fx, 3, 5, dark_red, out);
    assert_rgb (out, 1.0f, 0.0f, 0.0f, 1e-6f);
    lrg_palettize_apply_rgb (fx, 3, 5, light, out);
    assert_rgb (out, 1.0f, 1.0f, 1.0f, 1e-6f);

    /* half intensity: half way back to the original */
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (fx), 0.5f);
    lrg_palettize_apply_rgb (fx, 0, 0, dark_red, out);
    assert_rgb (out, 0.85f, 0.05f, 0.025f, 1e-5f);
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (fx), 0.0f);
    g_assert_true (lrg_palettize_is_identity (fx));

    /* clearing the palette */
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (fx), 1.0f);
    lrg_palettize_clear_palette (fx);
    g_assert_true (lrg_palettize_is_identity (fx));
    g_assert_cmpuint (lrg_palettize_get_palette_size (fx), ==, 0);
    lrg_palettize_apply_rgb (fx, 0, 0, dark_red, out);
    assert_rgb (out, 0.7f, 0.1f, 0.05f, 1e-6f);
}

static void
test_palettize_bayer (void)
{
    static const guint first_rows[16] = {
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26
    };
    gboolean seen[64];
    guint x;
    guint y;

    memset (seen, 0, sizeof (seen));
    for (y = 0; y < 8; y++)
    {
        for (x = 0; x < 8; x++)
        {
            guint v = lrg_palettize_get_bayer (x, y);

            g_assert_cmpuint (v, <, 64);
            g_assert_false (seen[v]);
            seen[v] = TRUE;
            /* the tile repeats */
            g_assert_cmpuint (lrg_palettize_get_bayer (x + 8, y + 16), ==, v);
        }
    }
    for (x = 0; x < 16; x++)
        g_assert_cmpuint (lrg_palettize_get_bayer (x % 8, x / 8), ==, first_rows[x]);
    /* each 2 x 2 block spreads its four thresholds evenly: 0, 32, 48, 16 */
    g_assert_cmpuint (lrg_palettize_get_bayer (0, 0) + lrg_palettize_get_bayer (1, 0) +
                      lrg_palettize_get_bayer (0, 1) + lrg_palettize_get_bayer (1, 1), ==, 96);
}

/* with black and white only, a grey of v dithers to a share v of white */
static void
test_palettize_dither_share (void)
{
    static const guint8 rgb[6] = { 0, 0, 0,  255, 255, 255 };
    static const gfloat greys[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GBytes) palette = NULL;
    guint i;

    palette = g_bytes_new_static (rgb, sizeof (rgb));
    fx = lrg_palettize_new ();
    g_assert_true (lrg_palettize_set_palette (fx, palette, NULL));
    g_assert_false (lrg_palettize_get_dither (fx));
    g_assert_cmpfloat_with_epsilon (lrg_palettize_get_dither_strength (fx), 0.1f, 1e-6f);

    /* without the dither a quarter grey is black everywhere */
    {
        const gfloat q[3] = { 0.25f, 0.25f, 0.25f };
        gfloat out[3];

        for (i = 0; i < 64; i++)
        {
            lrg_palettize_apply_rgb (fx, i % 8, i / 8, q, out);
            g_assert_cmpfloat (out[0], ==, 0.0f);
        }
    }

    lrg_palettize_set_dither (fx, TRUE);
    lrg_palettize_set_dither_strength (fx, 1.0f);
    for (i = 0; i < G_N_ELEMENTS (greys); i++)
    {
        const gfloat c[3] = { greys[i], greys[i], greys[i] };
        guint white;
        guint k;

        white = 0;
        for (k = 0; k < 64; k++)
        {
            gfloat out[3];

            lrg_palettize_apply_rgb (fx, k % 8, k / 8, c, out);
            g_assert_true (out[0] == 0.0f || out[0] == 1.0f);
            if (out[0] == 1.0f)
                white++;
        }
        g_assert_cmpfloat_with_epsilon ((gfloat)white / 64.0f, greys[i], 1.0f / 64.0f + 1e-6f);
    }

    /* the strength and the scale are clamped */
    lrg_palettize_set_dither_strength (fx, 3.0f);
    g_assert_cmpfloat (lrg_palettize_get_dither_strength (fx), ==, 1.0f);
    lrg_palettize_set_dither_scale (fx, 0.0f);
    g_assert_cmpfloat (lrg_palettize_get_dither_scale (fx), ==, 1.0f);
    lrg_palettize_set_dither_scale (fx, 4.0f);
    g_assert_cmpfloat (lrg_palettize_get_dither_scale (fx), ==, 4.0f);
}

static void
test_palettize_errors (void)
{
    static const guint8 good[6] = { 1, 2, 3, 4, 5, 6 };
    g_autoptr(LrgPalettize) fx = NULL;
    g_autoptr(GBytes) palette = NULL;
    gsize sizes[3] = { 0, 4, 769 };
    guint i;

    fx = lrg_palettize_new ();
    palette = g_bytes_new_static (good, sizeof (good));
    g_assert_true (lrg_palettize_set_palette (fx, palette, NULL));
    for (i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
        g_autoptr(GBytes) bad = NULL;
        g_autoptr(GError) error = NULL;

        bad = g_bytes_new_take (g_malloc0 (MAX (sizes[i], 1)), sizes[i]);
        g_assert_false (lrg_palettize_set_palette (fx, bad, &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        /* the palette stays */
        g_assert_cmpuint (lrg_palettize_get_palette_size (fx), ==, 2);
        g_assert_cmpuint (lrg_palettize_find_nearest (fx, 4, 5, 6), ==, 1);
    }
    g_assert_nonnull (strstr (lrg_palettize_get_fragment_shader (), "uniform sampler2D lut;"));
}

/* --- the camera's aspect override --- */

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
    (void)object;
    (void)pspec;
    (*(guint *)user_data)++;
}

static void
test_camera_aspect (void)
{
    g_autoptr(LrgCamera3D) camera = NULL;
    guint notified;
    gfloat aspect;

    camera = lrg_camera3d_new ();
    g_assert_cmpfloat (lrg_camera3d_get_aspect (camera), ==, 0.0f);
    notified = 0;
    g_signal_connect (camera, "notify::aspect", G_CALLBACK (on_notify), &notified);

    lrg_camera3d_set_aspect (camera, 4.0f / 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_camera3d_get_aspect (camera), 4.0f / 3.0f, 1e-6f);
    g_assert_cmpuint (notified, ==, 1);
    lrg_camera3d_set_aspect (camera, 4.0f / 3.0f);
    g_assert_cmpuint (notified, ==, 1);

    /* anything not positive and finite: the target's own */
    lrg_camera3d_set_aspect (camera, -2.0f);
    g_assert_cmpfloat (lrg_camera3d_get_aspect (camera), ==, 0.0f);
    lrg_camera3d_set_aspect (camera, NAN);
    g_assert_cmpfloat (lrg_camera3d_get_aspect (camera), ==, 0.0f);
    lrg_camera3d_set_aspect (camera, 100.0f);
    g_assert_cmpfloat (lrg_camera3d_get_aspect (camera), ==, 16.0f);

    g_object_set (camera, "aspect", 1.6f, NULL);
    g_object_get (camera, "aspect", &aspect, NULL);
    g_assert_cmpfloat_with_epsilon (aspect, 1.6f, 1e-6f);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/accessibility/colorblind/identity", test_colorblind_identity);
    g_test_add_func ("/accessibility/colorblind/neutrals", test_colorblind_neutrals);
    g_test_add_func ("/accessibility/colorblind/simulate-known", test_colorblind_simulate_known);
    g_test_add_func ("/accessibility/colorblind/correct-separates", test_colorblind_correct_separates);
    g_test_add_func ("/accessibility/colorblind/shader-source", test_colorblind_shader_source);
    g_test_add_func ("/accessibility/palettize/nearest", test_palettize_nearest);
    g_test_add_func ("/accessibility/palettize/ties", test_palettize_ties);
    g_test_add_func ("/accessibility/palettize/lut", test_palettize_lut);
    g_test_add_func ("/accessibility/palettize/apply", test_palettize_apply);
    g_test_add_func ("/accessibility/palettize/bayer", test_palettize_bayer);
    g_test_add_func ("/accessibility/palettize/dither-share", test_palettize_dither_share);
    g_test_add_func ("/accessibility/palettize/errors", test_palettize_errors);
    g_test_add_func ("/accessibility/camera/aspect", test_camera_aspect);

    return g_test_run ();
}
