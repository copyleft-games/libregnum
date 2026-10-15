/* test-audio-positional.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for positional sound: LrgAudioAttenuation, the pure
 * lrg_audio_compute_spatial() and the listener/voice API of
 * LrgAudioManager.  Everything except the "device" case runs without
 * an audio device; that case opens the default device only when
 * LRG_TEST_AUDIO_DEVICE=1 is set (CI machines usually have none).
 */

#include <math.h>
#include <glib.h>
#include <libregnum.h>

#define EPS (1e-4f)

static const gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
static const gfloat look_z[3] = { 0.0f, 0.0f, 1.0f };

/* ==========================================================================
 * LrgAudioAttenuation
 * ========================================================================== */

static void
test_attenuation_boxed (void)
{
    g_autoptr(LrgAudioAttenuation) att = NULL;
    g_autoptr(LrgAudioAttenuation) copy = NULL;
    LrgAudioAttenuation defaults;

    att = lrg_audio_attenuation_new (2.0f, 20.0f, 0.0f, 0.5f);
    g_assert_cmpfloat (att->min_distance, ==, 2.0f);
    g_assert_cmpfloat (att->max_distance, ==, 20.0f);
    g_assert_cmpfloat (att->rolloff, ==, 0.0f);
    g_assert_cmpfloat (att->spatial_blend, ==, 0.5f);

    copy = lrg_audio_attenuation_copy (att);
    g_assert_true (copy != att);
    g_assert_cmpfloat (copy->max_distance, ==, 20.0f);

    lrg_audio_attenuation_init_default (&defaults);
    g_assert_cmpfloat (defaults.min_distance, ==, LRG_AUDIO_DEFAULT_MIN_DISTANCE);
    g_assert_cmpfloat (defaults.max_distance, ==, LRG_AUDIO_DEFAULT_MAX_DISTANCE);
    g_assert_cmpfloat (defaults.rolloff, ==, LRG_AUDIO_DEFAULT_ROLLOFF);
    g_assert_cmpfloat (defaults.spatial_blend, ==, 1.0f);

    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_AUDIO_ATTENUATION));
    lrg_audio_attenuation_free (NULL);
}

/* ==========================================================================
 * lrg_audio_compute_spatial
 * ========================================================================== */

/* linear rolloff: 1 inside min, straight line to 0 at max, 0 beyond */
static void
test_spatial_linear (void)
{
    LrgAudioAttenuation att = { 2.0f, 12.0f, 0.0f, 1.0f };
    gfloat src[3] = { 0.0f, 0.0f, 0.0f };
    gfloat gain;
    gfloat pan;

    src[2] = 1.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (gain, 1.0f, EPS);

    src[2] = 7.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (gain, 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    src[2] = 12.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
    g_assert_cmpfloat_with_epsilon (gain, 0.0f, EPS);

    src[2] = 100.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
    g_assert_cmpfloat_with_epsilon (gain, 0.0f, EPS);
}

/* inverse rolloff: monotonic, 1 at min, 0 at max, below linear midway */
static void
test_spatial_inverse (void)
{
    LrgAudioAttenuation att = { 1.0f, 30.0f, 1.0f, 1.0f };
    gfloat src[3] = { 0.0f, 0.0f, 0.0f };
    gfloat previous;
    gfloat gain;
    gfloat d;

    previous = 2.0f;
    for (d = 0.0f; d <= 31.0f; d += 0.5f)
    {
        src[2] = d;
        lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
        g_assert_cmpfloat (gain, >=, 0.0f);
        g_assert_cmpfloat (gain, <=, 1.0f);
        g_assert_cmpfloat (gain, <=, previous + EPS);
        previous = gain;
    }

    src[2] = 1.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
    g_assert_cmpfloat_with_epsilon (gain, 1.0f, EPS);
    src[2] = 30.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
    g_assert_cmpfloat_with_epsilon (gain, 0.0f, EPS);

    /* inverse falls faster than linear near the source */
    src[2] = 5.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, &gain, NULL);
    g_assert_cmpfloat (gain, <, (30.0f - 5.0f) / 29.0f);
    g_assert_cmpfloat (gain, >, 0.0f);

    /* a %NULL attenuation is the default curve */
    {
        LrgAudioAttenuation defaults;
        gfloat with_null;
        gfloat with_defaults;

        lrg_audio_attenuation_init_default (&defaults);
        src[2] = 10.0f;
        lrg_audio_compute_spatial (origin, look_z, NULL, src, NULL, &with_null, NULL);
        lrg_audio_compute_spatial (origin, look_z, NULL, src, &defaults, &with_defaults, NULL);
        g_assert_cmpfloat_with_epsilon (with_null, with_defaults, EPS);
    }
}

/*
 * pan: forward x up is the right.  Looking along +Z with +Y up the
 * right is -X (right-handed, raylib); looking along +X it is +Z.
 */
static void
test_spatial_pan (void)
{
    LrgAudioAttenuation att = { 1.0f, 50.0f, 1.0f, 1.0f };
    gfloat look_x[3] = { 2.0f, 0.0f, 0.0f };
    gfloat src[3];
    gfloat pan;

    src[0] = -5.0f; src[1] = 0.0f; src[2] = 0.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 1.0f, EPS);

    src[0] = 5.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, -1.0f, EPS);

    /* in front and behind: centred */
    src[0] = 0.0f; src[2] = -5.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    /* 45 degrees to the right */
    src[0] = -5.0f; src[2] = 5.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 0.70710678f, 1e-3f);

    /* turned to +X (forward need not be normalized): +Z is the right */
    src[0] = 0.0f; src[2] = 5.0f;
    lrg_audio_compute_spatial (origin, look_x, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 1.0f, EPS);

    /* upside down (up = -Y) mirrors it */
    {
        gfloat down[3] = { 0.0f, -1.0f, 0.0f };

        lrg_audio_compute_spatial (origin, look_x, down, src, &att, NULL, &pan);
        g_assert_cmpfloat_with_epsilon (pan, -1.0f, EPS);
    }

    /* inside min distance the pan fades towards the centre */
    src[0] = -0.5f; src[2] = 0.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &att, NULL, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 0.5f, EPS);

    /* the listener moved: only the relative position counts */
    {
        gfloat ears[3] = { 10.0f, 3.0f, 10.0f };

        src[0] = 5.0f; src[1] = 3.0f; src[2] = 10.0f;
        lrg_audio_compute_spatial (ears, look_z, NULL, src, &att, NULL, &pan);
        g_assert_cmpfloat_with_epsilon (pan, 1.0f, EPS);
    }
}

/* degenerate inputs never produce NaN */
static void
test_spatial_degenerate (void)
{
    LrgAudioAttenuation att = { 1.0f, 10.0f, 1.0f, 1.0f };
    LrgAudioAttenuation broken = { -3.0f, -5.0f, 2.0f, 7.0f };
    gfloat zero[3] = { 0.0f, 0.0f, 0.0f };
    gfloat up[3] = { 0.0f, 1.0f, 0.0f };
    gfloat src[3] = { 0.0f, 4.0f, 0.0f };
    gfloat gain;
    gfloat pan;

    /* source at the ears */
    lrg_audio_compute_spatial (origin, look_z, NULL, origin, &att, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (gain, 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    /* zero forward means +Z; looking straight up has no right: centred */
    lrg_audio_compute_spatial (origin, zero, zero, src, &att, &gain, &pan);
    g_assert_false (isnan (gain));
    g_assert_false (isnan (pan));
    lrg_audio_compute_spatial (origin, up, NULL, src, &att, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    /* nonsense curves are clamped */
    src[0] = 3.0f;
    lrg_audio_compute_spatial (origin, look_z, NULL, src, &broken, &gain, &pan);
    g_assert_false (isnan (gain));
    g_assert_cmpfloat (gain, >=, 0.0f);
    g_assert_cmpfloat (gain, <=, 1.0f);
    g_assert_cmpfloat (pan, >=, -1.0f);
    g_assert_cmpfloat (pan, <=, 1.0f);
}

/* spatial blend mixes towards flat playback */
static void
test_spatial_blend (void)
{
    LrgAudioAttenuation flat = { 1.0f, 10.0f, 0.0f, 0.0f };
    LrgAudioAttenuation half = { 1.0f, 10.0f, 0.0f, 0.5f };
    gfloat src[3] = { -100.0f, 0.0f, 0.0f };
    gfloat gain;
    gfloat pan;

    lrg_audio_compute_spatial (origin, look_z, NULL, src, &flat, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (gain, 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    lrg_audio_compute_spatial (origin, look_z, NULL, src, &half, &gain, &pan);
    g_assert_cmpfloat_with_epsilon (gain, 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 0.5f, EPS);
}

/* ==========================================================================
 * LrgAudioManager listener and voices (no device)
 * ========================================================================== */

static void
test_manager_listener (void)
{
    g_autoptr(LrgAudioManager) manager = NULL;
    gfloat position[3];
    gfloat forward[3];
    gfloat up[3];
    gfloat ears[3] = { 1.0f, 2.0f, 3.0f };
    gfloat look[3] = { 1.0f, 0.0f, 0.0f };
    gfloat tilt[3] = { 0.0f, 0.0f, 1.0f };

    manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);

    /* defaults: origin, +Z, +Y */
    lrg_audio_manager_get_listener (manager, position, forward, up);
    g_assert_cmpfloat (position[0], ==, 0.0f);
    g_assert_cmpfloat (forward[2], ==, 1.0f);
    g_assert_cmpfloat (up[1], ==, 1.0f);

    lrg_audio_manager_set_listener (manager, ears, look, tilt);
    lrg_audio_manager_get_listener (manager, position, forward, up);
    g_assert_cmpfloat (position[1], ==, 2.0f);
    g_assert_cmpfloat (forward[0], ==, 1.0f);
    g_assert_cmpfloat (up[2], ==, 1.0f);

    /* a NULL up resets to +Y; NULL outs are allowed */
    lrg_audio_manager_set_listener (manager, ears, look, NULL);
    lrg_audio_manager_get_listener (manager, NULL, NULL, up);
    g_assert_cmpfloat (up[1], ==, 1.0f);
    g_assert_cmpfloat (up[2], ==, 0.0f);
}

static void
test_manager_voices_without_device (void)
{
    g_autoptr(LrgAudioManager) manager = NULL;
    g_autoptr(LrgSoundBank) bank = NULL;
    gfloat where[3] = { 1.0f, 0.0f, 1.0f };
    gfloat gain;

    manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);
    bank = lrg_sound_bank_new ("sfx");
    lrg_audio_manager_add_bank (manager, bank);

    /* unknown bank or sound: nothing plays, no warning */
    g_assert_cmpuint (lrg_audio_manager_play_sound_at (manager, "nope", "x", where, 1.0f, NULL), ==, 0);
    g_assert_cmpuint (lrg_audio_manager_play_sound_at (manager, "sfx", "x", NULL, 1.0f, NULL), ==, 0);
    g_assert_cmpuint (lrg_audio_manager_get_voice_count (manager), ==, 0);

    /* unknown voices */
    g_assert_false (lrg_audio_manager_is_voice_playing (manager, 0));
    g_assert_false (lrg_audio_manager_is_voice_playing (manager, 42));
    g_assert_false (lrg_audio_manager_stop_voice (manager, 0));
    g_assert_false (lrg_audio_manager_stop_voice (manager, 42));
    g_assert_false (lrg_audio_manager_set_voice_position (manager, 42, where));
    g_assert_false (lrg_audio_manager_get_voice_spatial (manager, 42, &gain, NULL));

    /* update and stop with no voices are harmless */
    lrg_audio_manager_update (manager);
    lrg_audio_manager_stop_all_voices (manager);
    lrg_audio_manager_stop_all_sounds (manager);
    g_assert_cmpuint (lrg_audio_manager_get_voice_count (manager), ==, 0);
}

/* ==========================================================================
 * With an audio device (opt-in)
 * ========================================================================== */

static void
test_manager_voices_device (void)
{
    g_autoptr(LrgAudioManager) manager = NULL;
    g_autoptr(LrgSoundBank) bank = NULL;
    g_autoptr(LrgWaveData) wave = NULL;
    LrgAudioAttenuation att = { 1.0f, 10.0f, 0.0f, 1.0f };
    gfloat ears[3] = { 0.0f, 0.0f, 0.0f };
    gfloat right[3] = { -5.5f, 0.0f, 0.0f };
    gfloat far[3] = { 0.0f, 0.0f, 50.0f };
    gfloat gain;
    gfloat pan;
    guint v1;
    guint v2;
    guint8 samples[11025];
    gsize i;

    if (g_strcmp0 (g_getenv ("LRG_TEST_AUDIO_DEVICE"), "1") != 0)
    {
        g_test_skip ("set LRG_TEST_AUDIO_DEVICE=1 to test with an audio device");
        return;
    }
    if (!grl_audio_device_is_ready (grl_audio_device_get_default ()))
    {
        g_test_skip ("no audio device");
        return;
    }
    /* checked, not heard */
    grl_audio_device_set_master_volume (grl_audio_device_get_default (), 0.0f);

    /* one second of 8-bit square wave, quiet */
    for (i = 0; i < sizeof (samples); i++)
        samples[i] = (guint8)(((i / 25) % 2) != 0 ? 136 : 120);
    wave = lrg_wave_data_new_from_samples (11025, 8, 1, samples, sizeof (samples));
    g_assert_nonnull (wave);

    manager = g_object_new (LRG_TYPE_AUDIO_MANAGER, NULL);
    bank = lrg_sound_bank_new ("sfx");
    g_assert_true (lrg_sound_bank_add_from_wave (bank, "beep", wave));
    lrg_audio_manager_add_bank (manager, bank);
    lrg_audio_manager_set_listener (manager, ears, look_z, NULL);

    /* two overlapping voices of one sound */
    v1 = lrg_audio_manager_play_sound_at (manager, "sfx", "beep", right, 0.2f, &att);
    v2 = lrg_audio_manager_play_sound_at (manager, "sfx", "beep", NULL, 0.2f, NULL);
    g_assert_cmpuint (v1, >, 0);
    g_assert_cmpuint (v2, >, 0);
    g_assert_cmpuint (v1, !=, v2);
    g_assert_cmpuint (lrg_audio_manager_get_voice_count (manager), ==, 2);
    g_assert_true (lrg_audio_manager_is_voice_playing (manager, v1));

    g_assert_true (lrg_audio_manager_get_voice_spatial (manager, v1, &gain, &pan));
    g_assert_cmpfloat_with_epsilon (gain, 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 1.0f, EPS);
    g_assert_true (lrg_audio_manager_get_voice_spatial (manager, v2, &gain, &pan));
    g_assert_cmpfloat_with_epsilon (gain, 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (pan, 0.0f, EPS);

    /* moving the emitter re-spatialises at once */
    g_assert_true (lrg_audio_manager_set_voice_position (manager, v1, far));
    g_assert_true (lrg_audio_manager_get_voice_spatial (manager, v1, &gain, NULL));
    g_assert_cmpfloat_with_epsilon (gain, 0.0f, EPS);

    /* stopping releases; the bank may go while voices live */
    g_assert_true (lrg_audio_manager_stop_voice (manager, v1));
    g_assert_false (lrg_audio_manager_stop_voice (manager, v1));
    g_assert_true (lrg_audio_manager_remove_bank (manager, "sfx"));
    lrg_audio_manager_update (manager);
    lrg_audio_manager_stop_all_sounds (manager);
    g_assert_cmpuint (lrg_audio_manager_get_voice_count (manager), ==, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/audio/positional/attenuation-boxed", test_attenuation_boxed);
    g_test_add_func ("/audio/positional/spatial-linear", test_spatial_linear);
    g_test_add_func ("/audio/positional/spatial-inverse", test_spatial_inverse);
    g_test_add_func ("/audio/positional/spatial-pan", test_spatial_pan);
    g_test_add_func ("/audio/positional/spatial-degenerate", test_spatial_degenerate);
    g_test_add_func ("/audio/positional/spatial-blend", test_spatial_blend);
    g_test_add_func ("/audio/positional/manager-listener", test_manager_listener);
    g_test_add_func ("/audio/positional/manager-voices", test_manager_voices_without_device);
    g_test_add_func ("/audio/positional/manager-voices-device", test_manager_voices_device);

    return g_test_run ();
}
