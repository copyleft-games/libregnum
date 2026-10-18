/* test-foley.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the synthesized foley (LrgFoley): every kind makes a valid
 * WAV image of its length, deterministic per variant, varying between
 * variants, with a decaying envelope (or a seamless loop), and the
 * failure paths return errors.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <libregnum.h>

static guint32
rd32 (const guint8 *p)
{
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static gint16
sample_at (GBytes *wav,
           guint   i)
{
    const guint8 *p = (const guint8 *)g_bytes_get_data (wav, NULL) + 44 + i * 2;

    return (gint16)((guint16)p[0] | ((guint16)p[1] << 8));
}

static guint
frame_count (GBytes *wav)
{
    return (guint)((g_bytes_get_size (wav) - 44) / 2);
}

/* RMS of frames [from, to) */
static gdouble
rms (GBytes *wav,
     guint   from,
     guint   to)
{
    gdouble sum = 0.0;
    guint   i;

    for (i = from; i < to; i++)
        sum += (gdouble)sample_at (wav, i) * sample_at (wav, i);
    return sqrt (sum / (gdouble)(to - from)) / 32768.0;
}

static void
test_every_kind (void)
{
    guint kind;

    for (kind = LRG_FOLEY_FOOTSTEP_DIRT; kind <= LRG_FOLEY_RAIN_LOOP; kind++)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GBytes) wav = lrg_foley_synthesize ((LrgFoleyKind)kind, 0, 0, &error);
        const guint8 *d;
        guint         peak = 0;
        guint         i;

        g_assert_no_error (error);
        g_assert_nonnull (wav);
        d = g_bytes_get_data (wav, NULL);

        /* a PCM mono 16-bit image of the kind's length */
        g_assert_cmpint (memcmp (d, "RIFF", 4), ==, 0);
        g_assert_cmpint (memcmp (d + 8, "WAVEfmt ", 8), ==, 0);
        g_assert_cmpuint (d[20], ==, 1);
        g_assert_cmpuint (d[22], ==, 1);
        g_assert_cmpuint (rd32 (d + 24), ==, LRG_FOLEY_DEFAULT_RATE);
        g_assert_cmpuint (d[34], ==, 16);
        g_assert_cmpuint (rd32 (d + 40), ==, frame_count (wav) * 2);
        g_assert_cmpuint (rd32 (d + 4), ==, g_bytes_get_size (wav) - 8);
        g_assert_cmpfloat_with_epsilon ((gdouble)frame_count (wav) / LRG_FOLEY_DEFAULT_RATE,
                                        lrg_foley_get_duration ((LrgFoleyKind)kind), 1e-3);

        /* audible and never clipping: a peak near 0.8 */
        for (i = 0; i < frame_count (wav); i++)
            peak = MAX (peak, (guint)abs (sample_at (wav, i)));
        g_assert_cmpuint (peak, >, 22000);
        g_assert_cmpuint (peak, <=, 26300);
    }
}

static void
test_determinism_and_variants (void)
{
    guint kind;

    for (kind = LRG_FOLEY_FOOTSTEP_DIRT; kind <= LRG_FOLEY_RAIN_LOOP; kind++)
    {
        g_autoptr(GBytes) a = lrg_foley_synthesize ((LrgFoleyKind)kind, 3, 0, NULL);
        g_autoptr(GBytes) b = lrg_foley_synthesize ((LrgFoleyKind)kind, 3, 0, NULL);
        g_autoptr(GBytes) c = lrg_foley_synthesize ((LrgFoleyKind)kind, 4, 0, NULL);

        g_assert_true (g_bytes_equal (a, b));
        g_assert_false (g_bytes_equal (a, c));
    }
}

static void
test_envelopes (void)
{
    guint kind;

    /* a footstep is loud at the start and nearly silent at the end */
    for (kind = LRG_FOLEY_FOOTSTEP_DIRT; kind <= LRG_FOLEY_ARMOR_CLINK; kind++)
    {
        g_autoptr(GBytes) wav = lrg_foley_synthesize ((LrgFoleyKind)kind, 1, 0, NULL);
        guint n = frame_count (wav);

        g_assert_cmpfloat (rms (wav, 0, n / 4), >, rms (wav, n * 3 / 4, n) * 4.0);
        /* the fade: the first and last samples are quiet */
        g_assert_cmpint (abs (sample_at (wav, 0)), <, 600);
        g_assert_cmpint (abs (sample_at (wav, n - 1)), <, 600);
    }
}

static void
test_rain_loops (void)
{
    g_autoptr(GBytes) wav = lrg_foley_synthesize (LRG_FOLEY_RAIN_LOOP, 0, 0, NULL);
    guint   n = frame_count (wav);
    gdouble step = 0.0;
    guint   i;

    /* steady: every second carries about the same energy */
    g_assert_cmpfloat (rms (wav, 0, n / 4), >, 0.05);
    g_assert_cmpfloat (fabs (rms (wav, 0, n / 2) - rms (wav, n / 2, n)), <, 0.06);

    /* the seam is no bigger than a step inside the clip */
    for (i = 1; i < n; i++)
        step = MAX (step, fabs ((gdouble)sample_at (wav, i) - sample_at (wav, i - 1)));
    g_assert_cmpfloat (fabs ((gdouble)sample_at (wav, 0) - sample_at (wav, n - 1)), <=, step * 1.5);
}

static void
test_sample_rates_and_errors (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) low = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_STONE, 0, 8000, NULL);
    g_autoptr(GBytes) high = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_STONE, 0, 48000, NULL);
    GBytes *none;

    g_assert_nonnull (low);
    g_assert_nonnull (high);
    g_assert_cmpuint (rd32 ((const guint8 *)g_bytes_get_data (high, NULL) + 24), ==, 48000);
    g_assert_cmpuint (frame_count (high), ==, frame_count (low) * 6);

    none = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_STONE, 0, 7999, &error);
    g_assert_null (none);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    none = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_STONE, 0, 96001, &error);
    g_assert_null (none);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    none = lrg_foley_synthesize ((LrgFoleyKind)99, 0, 0, &error);
    g_assert_null (none);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_cmpfloat (lrg_foley_get_duration ((LrgFoleyKind)99), ==, 0.0);
}

static void
test_kinds_differ_in_colour (void)
{
    g_autoptr(GBytes) stone = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_STONE, 0, 0, NULL);
    g_autoptr(GBytes) snow = lrg_foley_synthesize (LRG_FOLEY_FOOTSTEP_SNOW, 0, 0, NULL);
    gdouble zc_stone = 0.0;
    gdouble zc_snow = 0.0;
    guint   i;

    /* snow is a hiss of high grains, stone a low knock: crossings per sample */
    for (i = 1; i < frame_count (stone); i++)
        zc_stone += (sample_at (stone, i) >= 0) != (sample_at (stone, i - 1) >= 0);
    for (i = 1; i < frame_count (snow); i++)
        zc_snow += (sample_at (snow, i) >= 0) != (sample_at (snow, i - 1) >= 0);
    zc_stone /= frame_count (stone);
    zc_snow /= frame_count (snow);
    g_assert_cmpfloat (zc_snow, >, zc_stone * 1.5);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/foley/every-kind", test_every_kind);
    g_test_add_func ("/foley/determinism-and-variants", test_determinism_and_variants);
    g_test_add_func ("/foley/envelopes", test_envelopes);
    g_test_add_func ("/foley/rain-loops", test_rain_loops);
    g_test_add_func ("/foley/rates-and-errors", test_sample_rates_and_errors);
    g_test_add_func ("/foley/colour", test_kinds_differ_in_colour);

    return g_test_run ();
}
