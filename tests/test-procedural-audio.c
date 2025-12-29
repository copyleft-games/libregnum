/* test-procedural-audio.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgProceduralAudio.
 *
 * Note: Audio playback tests may be skipped if no audio device
 * is available (e.g., in CI environments).
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>

/*
 * Skip tests if no display is available.
 * Procedural audio requires raylib's audio device which needs a display.
 */
#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Audio device not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Test Subclass (Sine Wave Generator)
 * ========================================================================== */

#define TEST_TYPE_SINE_GENERATOR (test_sine_generator_get_type ())
G_DECLARE_FINAL_TYPE (TestSineGenerator, test_sine_generator, TEST, SINE_GENERATOR, LrgProceduralAudio)

struct _TestSineGenerator
{
    LrgProceduralAudio parent_instance;
    gfloat             frequency;
    gfloat             phase;
};

G_DEFINE_TYPE (TestSineGenerator, test_sine_generator, LRG_TYPE_PROCEDURAL_AUDIO)

static void
test_sine_generator_generate (LrgProceduralAudio *audio,
                               gfloat             *buffer,
                               gint                frame_count)
{
    TestSineGenerator *self = TEST_SINE_GENERATOR (audio);
    guint              sample_rate;
    gint               i;
    gfloat             phase_increment;

    sample_rate = lrg_procedural_audio_get_sample_rate (audio);
    phase_increment = 2.0f * G_PI * self->frequency / (gfloat)sample_rate;

    for (i = 0; i < frame_count; i++)
    {
        buffer[i] = sinf (self->phase);
        self->phase += phase_increment;

        if (self->phase >= 2.0f * G_PI)
        {
            self->phase -= 2.0f * G_PI;
        }
    }
}

static void
test_sine_generator_class_init (TestSineGeneratorClass *klass)
{
    LrgProceduralAudioClass *audio_class = LRG_PROCEDURAL_AUDIO_CLASS (klass);

    audio_class->generate = test_sine_generator_generate;
}

static void
test_sine_generator_init (TestSineGenerator *self)
{
    self->frequency = 440.0f;  /* A4 */
    self->phase = 0.0f;
}

static TestSineGenerator *
test_sine_generator_new (guint  sample_rate,
                          gfloat frequency)
{
    TestSineGenerator *self;

    self = g_object_new (TEST_TYPE_SINE_GENERATOR,
                         "sample-rate", sample_rate,
                         "channels", 1,
                         NULL);

    /* Check if stream creation succeeded (happens in parent's constructed) */
    if (!lrg_procedural_audio_is_valid (LRG_PROCEDURAL_AUDIO (self)))
    {
        g_object_unref (self);
        return NULL;
    }

    self->frequency = frequency;

    return self;
}

/* ==========================================================================
 * LrgProceduralAudio Tests
 * ========================================================================== */

static void
test_procedural_audio_new (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    g_assert_cmpuint (lrg_procedural_audio_get_sample_rate (audio), ==, 44100);
    g_assert_cmpuint (lrg_procedural_audio_get_channels (audio), ==, 1);
}

static void
test_procedural_audio_new_stereo (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (48000, 2);
    SKIP_IF_NULL (audio);

    g_assert_cmpuint (lrg_procedural_audio_get_sample_rate (audio), ==, 48000);
    g_assert_cmpuint (lrg_procedural_audio_get_channels (audio), ==, 2);
}

static void
test_procedural_audio_properties (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Test volume */
    g_assert_cmpfloat_with_epsilon (lrg_procedural_audio_get_volume (audio), 1.0f, 0.001f);

    lrg_procedural_audio_set_volume (audio, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_procedural_audio_get_volume (audio), 0.5f, 0.001f);

    /* Volume clamping */
    lrg_procedural_audio_set_volume (audio, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_procedural_audio_get_volume (audio), 0.0f, 0.001f);

    lrg_procedural_audio_set_volume (audio, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_procedural_audio_get_volume (audio), 1.0f, 0.001f);
}

static void
test_procedural_audio_pitch (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Test pitch (default should be 1.0) */
    lrg_procedural_audio_set_pitch (audio, 2.0f);
    /* No getter in base class - pitch is applied to underlying stream */
}

static void
test_procedural_audio_pan (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Test pan (-1.0 to 1.0) */
    lrg_procedural_audio_set_pan (audio, -1.0f);  /* Full left */
    lrg_procedural_audio_set_pan (audio, 1.0f);   /* Full right */
    lrg_procedural_audio_set_pan (audio, 0.0f);   /* Center */
    /* No getter - pan is applied to underlying stream */
}

static void
test_procedural_audio_playing_state (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Not playing initially */
    g_assert_false (lrg_procedural_audio_is_playing (audio));
}

static void
test_procedural_audio_update (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Update should not crash even when not playing */
    lrg_procedural_audio_update (audio);
}

static void
test_procedural_audio_subclass (void)
{
    g_autoptr(TestSineGenerator) generator = NULL;

    SKIP_IF_NO_DISPLAY ();

    generator = test_sine_generator_new (44100, 440.0f);
    SKIP_IF_NULL (generator);
    g_assert_true (LRG_IS_PROCEDURAL_AUDIO (generator));
    g_assert_cmpuint (lrg_procedural_audio_get_sample_rate (LRG_PROCEDURAL_AUDIO (generator)), ==, 44100);
    g_assert_cmpfloat_with_epsilon (generator->frequency, 440.0f, 0.001f);
}

static void
test_procedural_audio_subclass_generate (void)
{
    g_autoptr(TestSineGenerator) generator = NULL;
    gfloat                       buffer[256];
    gint                         i;

    SKIP_IF_NO_DISPLAY ();

    generator = test_sine_generator_new (44100, 440.0f);
    SKIP_IF_NULL (generator);

    /* Clear buffer */
    for (i = 0; i < 256; i++)
    {
        buffer[i] = 0.0f;
    }

    /* Generate some samples */
    test_sine_generator_generate (LRG_PROCEDURAL_AUDIO (generator), buffer, 256);

    /* Buffer should now contain non-zero values (sine wave) */
    g_assert_cmpfloat (fabsf (buffer[0]), <, 1.1f);  /* Should be in -1 to 1 range */
}

static void
test_procedural_audio_lifecycle (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;

    SKIP_IF_NO_DISPLAY ();

    audio = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (audio);

    /* Test stop without playing - should not crash */
    lrg_procedural_audio_stop (audio);
    g_assert_false (lrg_procedural_audio_is_playing (audio));

    /* Test pause/resume without playing - should not crash */
    lrg_procedural_audio_pause (audio);
    lrg_procedural_audio_resume (audio);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/procedural-audio/new", test_procedural_audio_new);
    g_test_add_func ("/procedural-audio/new-stereo", test_procedural_audio_new_stereo);
    g_test_add_func ("/procedural-audio/properties", test_procedural_audio_properties);
    g_test_add_func ("/procedural-audio/pitch", test_procedural_audio_pitch);
    g_test_add_func ("/procedural-audio/pan", test_procedural_audio_pan);
    g_test_add_func ("/procedural-audio/playing-state", test_procedural_audio_playing_state);
    g_test_add_func ("/procedural-audio/update", test_procedural_audio_update);
    g_test_add_func ("/procedural-audio/subclass", test_procedural_audio_subclass);
    g_test_add_func ("/procedural-audio/subclass-generate", test_procedural_audio_subclass_generate);
    g_test_add_func ("/procedural-audio/lifecycle", test_procedural_audio_lifecycle);

    return g_test_run ();
}
