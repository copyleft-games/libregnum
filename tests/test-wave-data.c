/* test-wave-data.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgWaveData.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>

/* ==========================================================================
 * LrgWaveData Tests
 * ========================================================================== */

static void
test_wave_data_new_procedural (void)
{
    g_autoptr(LrgWaveData) wave = NULL;

    /* Create a 1-second procedural wave at 44100 Hz, mono */
    wave = lrg_wave_data_new_procedural (44100, 1, 1.0f);

    g_assert_nonnull (wave);
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (wave), ==, 44100);
    g_assert_cmpuint (lrg_wave_data_get_channels (wave), ==, 1);
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (wave), 1.0f, 0.01f);
}

static void
test_wave_data_new_procedural_stereo (void)
{
    g_autoptr(LrgWaveData) wave = NULL;

    /* Create a 0.5-second procedural wave at 48000 Hz, stereo */
    wave = lrg_wave_data_new_procedural (48000, 2, 0.5f);

    g_assert_nonnull (wave);
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (wave), ==, 48000);
    g_assert_cmpuint (lrg_wave_data_get_channels (wave), ==, 2);
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (wave), 0.5f, 0.01f);
}

static void
test_wave_data_properties (void)
{
    g_autoptr(LrgWaveData) wave = NULL;

    wave = lrg_wave_data_new_procedural (22050, 1, 2.0f);
    g_assert_nonnull (wave);

    /* Test properties */
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (wave), ==, 22050);
    g_assert_cmpuint (lrg_wave_data_get_channels (wave), ==, 1);
    g_assert_cmpuint (lrg_wave_data_get_sample_size (wave), >=, 8);
    g_assert_cmpuint (lrg_wave_data_get_frame_count (wave), >, 0);
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (wave), 2.0f, 0.01f);
}

static void
test_wave_data_validity (void)
{
    g_autoptr(LrgWaveData) wave = NULL;

    wave = lrg_wave_data_new_procedural (44100, 1, 1.0f);
    g_assert_nonnull (wave);

    /* Valid wave should return TRUE */
    g_assert_true (lrg_wave_data_is_valid (wave));
}

static void
test_wave_data_get_samples (void)
{
    g_autoptr(LrgWaveData) wave = NULL;
    gfloat                *samples;
    gsize                  count;

    wave = lrg_wave_data_new_procedural (44100, 1, 0.1f);
    g_assert_nonnull (wave);

    samples = lrg_wave_data_get_samples (wave, &count);

    /* Should have samples */
    g_assert_nonnull (samples);
    g_assert_cmpuint (count, >, 0);

    g_free (samples);
}

static void
test_wave_data_set_samples (void)
{
    g_autoptr(LrgWaveData) wave = NULL;
    gfloat                 samples[100];
    guint                  i;

    /* Create a simple sine wave pattern */
    for (i = 0; i < 100; i++)
    {
        samples[i] = sinf (2.0f * G_PI * (gfloat)i / 100.0f);
    }

    wave = lrg_wave_data_new_procedural (44100, 1, 0.1f);
    g_assert_nonnull (wave);

    /* Set samples */
    lrg_wave_data_set_samples (wave, samples, 100);

    /* Verify wave is still valid */
    g_assert_true (lrg_wave_data_is_valid (wave));
}

static void
test_wave_data_crop (void)
{
    g_autoptr(LrgWaveData) original = NULL;
    g_autoptr(LrgWaveData) cropped = NULL;
    gfloat                 original_duration;
    gfloat                 cropped_duration;

    original = lrg_wave_data_new_procedural (44100, 1, 2.0f);
    g_assert_nonnull (original);

    original_duration = lrg_wave_data_get_duration (original);
    g_assert_cmpfloat_with_epsilon (original_duration, 2.0f, 0.01f);

    /* Crop from 0.5 to 1.5 seconds */
    cropped = lrg_wave_data_crop (original, 0.5f, 1.5f);
    g_assert_nonnull (cropped);

    cropped_duration = lrg_wave_data_get_duration (cropped);
    g_assert_cmpfloat_with_epsilon (cropped_duration, 1.0f, 0.1f);

    /* Original should be unchanged */
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (original), 2.0f, 0.01f);
}

static void
test_wave_data_resample (void)
{
    g_autoptr(LrgWaveData) original = NULL;
    g_autoptr(LrgWaveData) resampled = NULL;

    original = lrg_wave_data_new_procedural (44100, 1, 1.0f);
    g_assert_nonnull (original);
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (original), ==, 44100);

    /* Resample to 22050 Hz */
    resampled = lrg_wave_data_resample (original, 22050);
    g_assert_nonnull (resampled);
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (resampled), ==, 22050);

    /* Duration should be preserved */
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (resampled),
                                     lrg_wave_data_get_duration (original), 0.1f);
}

static void
test_wave_data_convert (void)
{
    g_autoptr(LrgWaveData) original = NULL;
    g_autoptr(LrgWaveData) converted = NULL;

    original = lrg_wave_data_new_procedural (44100, 1, 1.0f);
    g_assert_nonnull (original);

    /* Convert to stereo at same sample rate */
    converted = lrg_wave_data_convert (original, 44100,
                                        lrg_wave_data_get_sample_size (original), 2);
    g_assert_nonnull (converted);
    g_assert_cmpuint (lrg_wave_data_get_channels (converted), ==, 2);
}

static void
test_wave_data_to_sound (void)
{
    g_autoptr(LrgWaveData) wave = NULL;
    g_autoptr(GrlSound)    sound = NULL;

    wave = lrg_wave_data_new_procedural (44100, 1, 0.5f);
    g_assert_nonnull (wave);

    sound = lrg_wave_data_to_sound (wave);

    /* Sound conversion may fail without audio device, which is OK */
    if (sound != NULL)
    {
        g_assert_true (GRL_IS_SOUND (sound));
    }
}

static void
test_wave_data_copy (void)
{
    g_autoptr(LrgWaveData) original = NULL;
    g_autoptr(LrgWaveData) copy = NULL;

    original = lrg_wave_data_new_procedural (44100, 1, 1.0f);
    g_assert_nonnull (original);

    copy = lrg_wave_data_copy (original);
    g_assert_nonnull (copy);

    /* Properties should match */
    g_assert_cmpuint (lrg_wave_data_get_sample_rate (copy), ==,
                       lrg_wave_data_get_sample_rate (original));
    g_assert_cmpuint (lrg_wave_data_get_channels (copy), ==,
                       lrg_wave_data_get_channels (original));
    g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (copy),
                                     lrg_wave_data_get_duration (original), 0.01f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/wave-data/new-procedural", test_wave_data_new_procedural);
    g_test_add_func ("/wave-data/new-procedural-stereo", test_wave_data_new_procedural_stereo);
    g_test_add_func ("/wave-data/properties", test_wave_data_properties);
    g_test_add_func ("/wave-data/validity", test_wave_data_validity);
    g_test_add_func ("/wave-data/get-samples", test_wave_data_get_samples);
    g_test_add_func ("/wave-data/set-samples", test_wave_data_set_samples);
    g_test_add_func ("/wave-data/crop", test_wave_data_crop);
    g_test_add_func ("/wave-data/resample", test_wave_data_resample);
    g_test_add_func ("/wave-data/convert", test_wave_data_convert);
    g_test_add_func ("/wave-data/to-sound", test_wave_data_to_sound);
    g_test_add_func ("/wave-data/copy", test_wave_data_copy);

    return g_test_run ();
}
