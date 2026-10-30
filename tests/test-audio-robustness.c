/* test-audio-robustness.c
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The audio backend choice and the audio lock, through libregnum.
 *
 * Headless: LrgAudioSettings:audio-backend (names, round trip, apply,
 * LRG_AUDIO_BACKEND winning over it) and a game without an audio device
 * (backend 'none'): the synth, sound bed and audio manager keep working
 * silently.  With an audio server (a PipeWire or PulseAudio socket in
 * XDG_RUNTIME_DIR, or LRG_TEST_AUDIO_DEVICE=1; =0 skips):
 *
 * - /audio-robustness/init-stress: the device opened and closed again
 *   and again through the default order (ALSA first; miniaudio's
 *   PulseAudio backend crashed in InitAudioDevice now and then), a MIDI
 *   synth streaming in between.
 * - /audio-robustness/game-thread-calls: sound calls timed on the test
 *   thread while a MIDI synth renders in the device callback (inside
 *   raylib's audio lock) and a sound bed crossfades: none may wait for
 *   the lock.
 */

#include <math.h>
#include <time.h>
#include <glib.h>
#include <libregnum.h>
#include "lrg-test-midi.h"

#define TONE_RATE     (44100)
#define STRESS_CYCLES (8)
#define HAMMER_ROUNDS (600)
#define N_SOUNDS      (6)

/* ---- helpers ------------------------------------------------------------ */

static gint64
now_ns (void)
{
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (gint64)ts.tv_sec * G_GINT64_CONSTANT (1000000000) + ts.tv_nsec;
}

static int
compare_gint64 (const void *a,
                const void *b)
{
    gint64 da = *(const gint64 *)a;
    gint64 db = *(const gint64 *)b;

    return (da > db) - (da < db);
}

/* whether an audio server looks reachable (or the caller insists) */
static gboolean
audio_server_expected (void)
{
    const gchar      *forced;
    const gchar      *runtime;
    g_autofree gchar *pipewire = NULL;
    g_autofree gchar *pulse = NULL;

    forced = g_getenv ("LRG_TEST_AUDIO_DEVICE");
    if (g_strcmp0 (forced, "1") == 0)
        return TRUE;
    if (g_strcmp0 (forced, "0") == 0)
        return FALSE;
    runtime = g_getenv ("PIPEWIRE_RUNTIME_DIR");
    if (runtime == NULL)
        runtime = g_getenv ("XDG_RUNTIME_DIR");
    if (runtime == NULL)
        return FALSE;
    pipewire = g_build_filename (runtime, "pipewire-0", NULL);
    pulse = g_build_filename (runtime, "pulse", "native", NULL);
    return g_file_test (pipewire, G_FILE_TEST_EXISTS) || g_file_test (pulse, G_FILE_TEST_EXISTS);
}

/* opens the device through the default order, or skips */
static gboolean
open_device_or_skip (GrlAudioDevice **out)
{
    GrlAudioDevice *device;

    if (!audio_server_expected ())
    {
        g_test_skip ("no audio server (set LRG_TEST_AUDIO_DEVICE=1 to force)");
        return FALSE;
    }
    grl_audio_device_set_preferred_backend (GRL_AUDIO_BACKEND_AUTO);
    device = grl_audio_device_get_default ();
    grl_audio_device_init_audio (device);
    if (!grl_audio_device_is_ready (device))
    {
        g_test_skip ("the audio device did not open");
        return FALSE;
    }
    grl_audio_device_set_master_volume (device, 0.0f);
    *out = device;
    return TRUE;
}

static GrlSound *
make_tone (gdouble seconds,
           gdouble hz)
{
    g_autoptr(GrlWave) wave = NULL;
    g_autofree gint16 *pcm = NULL;
    guint              frames;
    guint              i;

    frames = (guint)(TONE_RATE * seconds);
    pcm = g_new (gint16, frames);
    for (i = 0; i < frames; i++)
        pcm[i] = (gint16)(2000.0 * sin (2.0 * G_PI * hz * i / TONE_RATE));
    wave = grl_wave_new_from_samples (TONE_RATE, 16, 1, (const guint8 *)pcm, frames * sizeof (gint16));
    g_assert_nonnull (wave);
    return grl_sound_new_from_grl_wave (wave);
}

/* a synth playing a busy song (on the device callback once streaming) */
static LrgMidiSynth *
make_synth (void)
{
    LrgMidiSynth      *synth;
    g_autoptr(GBytes)  sf2 = NULL;
    g_autoptr(GBytes)  song = NULL;

    synth = lrg_midi_synth_new (TEST_MIDI_RATE);
    sf2 = test_midi_build_sf2 ();
    song = test_midi_build_busy_song (12, 48);
    g_assert_true (lrg_midi_synth_load_soundfont_bytes (synth, sf2, NULL));
    g_assert_true (lrg_midi_synth_play (synth, song, TRUE, 0.0, NULL));
    return synth;
}

/* ---- headless ----------------------------------------------------------- */

static void
test_settings_backend (void)
{
    g_autoptr(LrgAudioSettings) settings = NULL;
    g_autoptr(LrgAudioSettings) copy = NULL;
    g_autoptr(GVariant)         data = NULL;
    g_autoptr(GError)           error = NULL;
    g_autofree gchar           *saved_lrg = NULL;
    g_autofree gchar           *saved_grl = NULL;
    g_autofree gchar           *prop = NULL;

    saved_lrg = g_strdup (g_getenv ("LRG_AUDIO_BACKEND"));
    saved_grl = g_strdup (g_getenv ("GRL_AUDIO_BACKEND"));
    g_unsetenv ("LRG_AUDIO_BACKEND");
    g_unsetenv ("GRL_AUDIO_BACKEND");

    settings = lrg_audio_settings_new ();
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (settings), ==, "auto");

    /* names as GRL_AUDIO_BACKEND takes them, stored lower-case */
    lrg_audio_settings_set_audio_backend (settings, "PipeWire");
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (settings), ==, "pipewire");
    g_object_set (settings, "audio-backend", "pulseaudio", NULL);
    g_object_get (settings, "audio-backend", &prop, NULL);
    g_assert_cmpstr (prop, ==, "pulseaudio");
    /* unknown or NULL: auto */
    lrg_audio_settings_set_audio_backend (settings, "jack");
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (settings), ==, "auto");
    lrg_audio_settings_set_audio_backend (settings, "alsa");
    lrg_audio_settings_set_audio_backend (settings, NULL);
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (settings), ==, "auto");

    /* round trip */
    lrg_audio_settings_set_audio_backend (settings, "none");
    data = lrg_settings_group_serialize (LRG_SETTINGS_GROUP (settings), &error);
    g_assert_no_error (error);
    copy = lrg_audio_settings_new ();
    g_assert_true (lrg_settings_group_deserialize (LRG_SETTINGS_GROUP (copy), data, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (copy), ==, "none");

    /* apply hands it to graylib ... */
    lrg_audio_settings_set_audio_backend (settings, "alsa");
    lrg_settings_group_apply (LRG_SETTINGS_GROUP (settings));
    g_assert_cmpint (grl_audio_device_get_preferred_backend (), ==, GRL_AUDIO_BACKEND_ALSA);

    /* ... unless LRG_AUDIO_BACKEND says otherwise */
    g_setenv ("LRG_AUDIO_BACKEND", "pulse", TRUE);
    lrg_settings_group_apply (LRG_SETTINGS_GROUP (settings));
    g_assert_cmpint (grl_audio_device_get_preferred_backend (), ==, GRL_AUDIO_BACKEND_PULSEAUDIO);
    g_unsetenv ("LRG_AUDIO_BACKEND");

    /* reset: auto */
    lrg_settings_group_reset (LRG_SETTINGS_GROUP (settings));
    g_assert_cmpstr (lrg_audio_settings_get_audio_backend (settings), ==, "auto");
    lrg_settings_group_apply (LRG_SETTINGS_GROUP (settings));
    g_assert_cmpint (grl_audio_device_get_preferred_backend (), ==, GRL_AUDIO_BACKEND_AUTO);

    if (saved_lrg != NULL)
        g_setenv ("LRG_AUDIO_BACKEND", saved_lrg, TRUE);
    if (saved_grl != NULL)
        g_setenv ("GRL_AUDIO_BACKEND", saved_grl, TRUE);
}

/*
 * No device (backend 'none', as when no backend opens one): the game's
 * audio calls fail softly and the game keeps running.
 */
static void
test_no_device_degrades (void)
{
    GrlAudioDevice           *device;
    g_autoptr(LrgMidiSynth)   synth = NULL;
    g_autoptr(LrgSoundBed)    bed = NULL;
    g_autoptr(GError)         error = NULL;
    LrgAudioManager          *manager;
    gfloat                    samples[2 * 256];
    guint                     i;

    grl_audio_device_set_preferred_backend (GRL_AUDIO_BACKEND_NONE);
    device = grl_audio_device_get_default ();
    grl_audio_device_close (device);
    g_assert_false (grl_audio_device_init_audio (device));
    g_assert_false (grl_audio_device_is_ready (device));

    /* the synth: no stream, but it still renders offline */
    synth = make_synth ();
    g_assert_false (lrg_midi_synth_start_stream (synth, &error));
    g_assert_error (error, LRG_MIDI_SYNTH_ERROR, LRG_MIDI_SYNTH_ERROR_NO_DEVICE);
    g_clear_error (&error);
    lrg_midi_synth_update (synth);
    (void)lrg_midi_synth_render (synth, samples, 256);

    /* the sound bed: no stream either */
    bed = lrg_sound_bed_new (TONE_RATE);
    for (i = 0; i < G_N_ELEMENTS (samples); i++)
        samples[i] = 0.1f * sinf ((gfloat)i * 0.05f);
    g_assert_true (lrg_sound_bed_add_layer_samples (bed, "hum", samples, G_N_ELEMENTS (samples),
                                                    2, TONE_RATE, NULL));
    g_assert_false (lrg_sound_bed_start_stream (bed, NULL));
    lrg_sound_bed_update (bed);

    /* the audio manager: volumes and mute still work */
    manager = lrg_audio_manager_get_default ();
    lrg_audio_manager_set_master_volume (manager, 0.5f);
    lrg_audio_manager_set_muted (manager, TRUE);
    lrg_audio_manager_set_muted (manager, FALSE);

    grl_audio_device_set_preferred_backend (GRL_AUDIO_BACKEND_AUTO);
}

/* ---- with a device ------------------------------------------------------ */

static void
test_init_stress (void)
{
    GrlAudioDevice *device;
    guint           cycle;

    if (!open_device_or_skip (&device))
        return;
    grl_audio_device_close (device);

    for (cycle = 0; cycle < STRESS_CYCLES; cycle++)
    {
        g_autoptr(LrgMidiSynth) synth = NULL;
        g_autoptr(GError)       error = NULL;

        g_assert_true (grl_audio_device_init_audio (device));
        grl_audio_device_set_master_volume (device, 0.0f);

        synth = make_synth ();
        g_assert_true (lrg_midi_synth_start_stream (synth, &error));
        g_assert_no_error (error);
        g_usleep (20000);
        lrg_midi_synth_update (synth);
        lrg_midi_synth_stop_stream (synth);
        g_clear_object (&synth);

        grl_audio_device_close (device);
        g_assert_false (grl_audio_device_is_ready (device));
    }
}

/*
 * Sound calls on the game thread while the synth renders inside raylib's
 * lock: graylib queues them, so none waits for a render.
 */
static void
test_game_thread_calls (void)
{
    GrlAudioDevice          *device;
    g_autoptr(LrgMidiSynth)  synth = NULL;
    GrlSound                *sounds[N_SOUNDS];
    GArray                  *lat;
    gint64                  *v;
    guint                    round;
    guint                    i;
    guint                    n;

    if (!open_device_or_skip (&device))
        return;

    synth = make_synth ();
    g_assert_true (lrg_midi_synth_start_stream (synth, NULL));
    for (i = 0; i < N_SOUNDS; i++)
        sounds[i] = make_tone (0.1, 300.0 + 50.0 * i);

    lat = g_array_new (FALSE, FALSE, sizeof (gint64));
    for (round = 0; round < HAMMER_ROUNDS; round++)
    {
        for (i = 0; i < N_SOUNDS; i++)
        {
            gint64 t0;
            gint64 d;

            t0 = now_ns ();
            grl_sound_set_volume (sounds[i], (round % 4) / 4.0f);
            grl_sound_set_pan (sounds[i], ((round + i) % 3) - 1.0f);
            grl_sound_play (sounds[i]);
            g_assert_true (grl_sound_is_playing (sounds[i]));
            d = now_ns () - t0;
            g_array_append_val (lat, d);
        }
        lrg_midi_synth_update (synth);
        g_usleep (500);
    }
    n = lat->len;
    v = (gint64 *)lat->data;
    qsort (v, n, sizeof (gint64), compare_gint64);
    g_test_message ("%u groups of 4 sound calls beside a streaming synth: median %.2f us, p99 %.2f us, max %.2f us",
                    n, v[n / 2] / 1e3, v[(n * 99) / 100] / 1e3, v[n - 1] / 1e3);
    /* 4 calls, never a render's wait (a loaded machine may preempt one) */
    g_assert_cmpint (v[(n * 99) / 100], <, 500000);
    g_array_unref (lat);

    for (i = 0; i < N_SOUNDS; i++)
        g_object_unref (sounds[i]);
    lrg_midi_synth_stop_stream (synth);
    grl_audio_device_close (device);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/audio-robustness/settings-backend", test_settings_backend);
    g_test_add_func ("/audio-robustness/no-device", test_no_device_degrades);
    g_test_add_func ("/audio-robustness/init-stress", test_init_stress);
    g_test_add_func ("/audio-robustness/game-thread-calls", test_game_thread_calls);

    return g_test_run ();
}
