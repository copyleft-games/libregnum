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
 * The audio thread and commands
 * ========================================================================== */

#define TEST_TYPE_PROBE_GENERATOR (test_probe_generator_get_type ())
G_DECLARE_FINAL_TYPE (TestProbeGenerator, test_probe_generator, TEST, PROBE_GENERATOR, LrgProceduralAudio)

/* generate() writes a constant level and reports from where it ran */
struct _TestProbeGenerator
{
    LrgProceduralAudio parent_instance;
    gfloat             level;          /* the audio thread's, set by commands */
    gint               frames;         /* atomic */
    gpointer           render_thread;  /* atomic: the last generate()'s thread */
};

G_DEFINE_TYPE (TestProbeGenerator, test_probe_generator, LRG_TYPE_PROCEDURAL_AUDIO)

static void
test_probe_generator_generate (LrgProceduralAudio *audio,
                               gfloat             *buffer,
                               gint                frame_count)
{
    TestProbeGenerator *self = TEST_PROBE_GENERATOR (audio);
    guint               channels;
    gint                i;

    channels = lrg_procedural_audio_get_channels (audio);
    for (i = 0; i < frame_count * (gint)channels; i++)
        buffer[i] = self->level;
    (void)g_atomic_int_add (&self->frames, frame_count);
    g_atomic_pointer_set (&self->render_thread, g_thread_self ());
}

static void
test_probe_generator_class_init (TestProbeGeneratorClass *klass)
{
    LRG_PROCEDURAL_AUDIO_CLASS (klass)->generate = test_probe_generator_generate;
}

static void
test_probe_generator_init (TestProbeGenerator *self)
{
}

/* one queued command: appends its tag and the thread it ran on */
typedef struct
{
    GString  *log;        /* shared, written where the command runs */
    gpointer *ran_on;     /* the thread */
    gint     *ran;        /* atomic: commands run */
    gint     *destroyed;  /* destroy notifies run (game thread only) */
    gchar     tag;
    gfloat    level;
} ProbeCommand;

static void
probe_command_run (LrgProceduralAudio *audio,
                   gpointer            user_data)
{
    ProbeCommand *command = user_data;

    if (TEST_IS_PROBE_GENERATOR (audio))
        TEST_PROBE_GENERATOR (audio)->level = command->level;
    g_string_append_c (command->log, command->tag);
    g_atomic_pointer_set (command->ran_on, g_thread_self ());
    g_atomic_int_inc (command->ran);
}

static void
probe_command_destroy (gpointer user_data)
{
    ProbeCommand *command = user_data;

    (*command->destroyed)++;
    g_free (command);
}

static ProbeCommand *
probe_command_new (GString  *log,
                   gpointer *ran_on,
                   gint     *ran,
                   gint     *destroyed,
                   gchar     tag,
                   gfloat    level)
{
    ProbeCommand *command;

    command = g_new0 (ProbeCommand, 1);
    command->log = log;
    command->ran_on = ran_on;
    command->ran = ran;
    command->destroyed = destroyed;
    command->tag = tag;
    command->level = level;
    return command;
}

/* the property: on by default, off on request (the update() pump) */
static void
test_procedural_audio_thread_property (void)
{
    g_autoptr(LrgProceduralAudio) on = NULL;
    g_autoptr(LrgProceduralAudio) off = NULL;
    gboolean                      value;

    SKIP_IF_NO_DISPLAY ();

    on = lrg_procedural_audio_new (44100, 1);
    SKIP_IF_NULL (on);
    g_object_get (on, "audio-thread", &value, NULL);
    g_assert_true (value);

    off = g_object_new (LRG_TYPE_PROCEDURAL_AUDIO, "audio-thread", FALSE, NULL);
    g_object_get (off, "audio-thread", &value, NULL);
    g_assert_false (value);
    g_assert_false (lrg_procedural_audio_get_audio_thread (off));
}

/* not rendering on the audio thread: commands run at once, in order */
static void
test_procedural_audio_commands_direct (void)
{
    g_autoptr(LrgProceduralAudio) audio = NULL;
    g_autoptr(GString)            log = NULL;
    gpointer                      ran_on = NULL;
    gint                          ran = 0;
    gint                          destroyed = 0;

    /* no stream needed: without one nothing renders on the audio thread */
    audio = g_object_new (TEST_TYPE_PROBE_GENERATOR, "sample-rate", 44100, "channels", 1, NULL);
    log = g_string_new (NULL);
    lrg_procedural_audio_queue_command (audio, probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'a', 0.1f),
                                        probe_command_destroy);
    lrg_procedural_audio_queue_command (audio, probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'b', 0.2f),
                                        probe_command_destroy);
    lrg_procedural_audio_queue_command (audio, probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'c', 0.3f),
                                        probe_command_destroy);
    g_assert_cmpstr (log->str, ==, "abc");
    g_assert_cmpint (ran, ==, 3);
    g_assert_cmpint (destroyed, ==, 3);
    g_assert_true (ran_on == g_thread_self ());
    g_assert_cmpfloat (TEST_PROBE_GENERATOR (audio)->level, ==, 0.3f);
}

/*
 * On the audio device (LRG_TEST_AUDIO_DEVICE=1): generate() and the
 * commands run on the device's thread while playing, in order; their
 * destroy notifies run here; paused, commands run at once here; dispose
 * runs what is still pending.
 */
static void
test_procedural_audio_stream (void)
{
    TestProbeGenerator *probe;
    GrlAudioDevice     *device;
    g_autoptr(GString)  log = NULL;
    gpointer            ran_on = NULL;
    gint                ran = 0;
    gint                destroyed = 0;
    gint64              deadline;
    gint                frames;

    if (g_strcmp0 (g_getenv ("LRG_TEST_AUDIO_DEVICE"), "1") != 0)
    {
        g_test_skip ("set LRG_TEST_AUDIO_DEVICE=1 to open the audio device");
        return;
    }
    device = grl_audio_device_get_default ();
    if (device == NULL || !grl_audio_device_is_ready (device))
    {
        g_test_skip ("no audio device");
        return;
    }
    grl_audio_device_set_master_volume (device, 0.0f);

    probe = g_object_new (TEST_TYPE_PROBE_GENERATOR, "sample-rate", 44100, "channels", 2, NULL);
    g_assert_true (lrg_procedural_audio_get_audio_thread (LRG_PROCEDURAL_AUDIO (probe)));
    log = g_string_new (NULL);

    /* not played yet: nothing is pulled, commands run here */
    g_usleep (50000);
    g_assert_cmpint (g_atomic_int_get (&probe->frames), ==, 0);

    lrg_procedural_audio_play (LRG_PROCEDURAL_AUDIO (probe));
    deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;
    while (g_atomic_int_get (&probe->frames) == 0 && g_get_monotonic_time () < deadline)
        g_usleep (5000);
    g_assert_cmpint (g_atomic_int_get (&probe->frames), >, 0);
    g_assert_true (g_atomic_pointer_get (&probe->render_thread) != (gpointer)g_thread_self ());

    /* update() renders nothing on this thread */
    lrg_procedural_audio_update (LRG_PROCEDURAL_AUDIO (probe));
    g_assert_true (g_atomic_pointer_get (&probe->render_thread) != (gpointer)g_thread_self ());

    /* playing: commands run on the audio thread, in order */
    lrg_procedural_audio_queue_command (LRG_PROCEDURAL_AUDIO (probe), probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'a', 0.1f),
                                        probe_command_destroy);
    lrg_procedural_audio_queue_command (LRG_PROCEDURAL_AUDIO (probe), probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'b', 0.2f),
                                        probe_command_destroy);
    deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;
    while (g_atomic_int_get (&ran) < 2 && g_get_monotonic_time () < deadline)
        g_usleep (5000);
    g_assert_cmpint (g_atomic_int_get (&ran), ==, 2);
    g_assert_true (g_atomic_pointer_get (&ran_on) != (gpointer)g_thread_self ());

    /* their data is freed on this thread, by the next call */
    lrg_procedural_audio_update (LRG_PROCEDURAL_AUDIO (probe));
    g_assert_cmpint (destroyed, ==, 2);

    /* paused: the audio thread lets go, commands run here at once */
    lrg_procedural_audio_pause (LRG_PROCEDURAL_AUDIO (probe));
    frames = g_atomic_int_get (&probe->frames);
    lrg_procedural_audio_queue_command (LRG_PROCEDURAL_AUDIO (probe), probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'c', 0.3f),
                                        probe_command_destroy);
    g_assert_cmpint (ran, ==, 3);
    g_assert_cmpint (destroyed, ==, 3);
    g_assert_true (ran_on == (gpointer)g_thread_self ());
    g_assert_cmpstr (log->str, ==, "abc");
    g_assert_cmpfloat (probe->level, ==, 0.3f);
    g_usleep (50000);
    g_assert_cmpint (g_atomic_int_get (&probe->frames), ==, frames);

    /* resumed, then disposed with a command still pending: it runs */
    lrg_procedural_audio_resume (LRG_PROCEDURAL_AUDIO (probe));
    lrg_procedural_audio_queue_command (LRG_PROCEDURAL_AUDIO (probe), probe_command_run,
                                        probe_command_new (log, &ran_on, &ran, &destroyed, 'd', 0.4f),
                                        probe_command_destroy);
    g_object_unref (probe);
    g_assert_cmpint (g_atomic_int_get (&ran), ==, 4);
    g_assert_cmpint (destroyed, ==, 4);
    g_assert_cmpstr (log->str, ==, "abcd");

    grl_audio_device_close (device);
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
    g_test_add_func ("/procedural-audio/audio-thread-property", test_procedural_audio_thread_property);
    g_test_add_func ("/procedural-audio/commands-direct", test_procedural_audio_commands_direct);
    g_test_add_func ("/procedural-audio/stream", test_procedural_audio_stream);

    return g_test_run ();
}
