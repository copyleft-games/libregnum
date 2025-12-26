/* test-audio.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the audio module.
 *
 * Note: Many audio tests require actual audio hardware which may not
 * be available in CI environments. Tests that require audio playback
 * are skipped if audio initialization fails.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgSoundBank    *bank;
    LrgAudioManager *manager;
} AudioFixture;

static void
audio_fixture_set_up (AudioFixture *fixture,
                       gconstpointer user_data)
{
    (void)user_data;

    fixture->bank = lrg_sound_bank_new ("test-bank");
    fixture->manager = lrg_audio_manager_get_default ();
}

static void
audio_fixture_tear_down (AudioFixture *fixture,
                          gconstpointer user_data)
{
    (void)user_data;

    g_clear_object (&fixture->bank);
    /* Manager is a singleton, don't unref */
}

/* ==========================================================================
 * LrgSoundBank Tests
 * ========================================================================== */

static void
test_sound_bank_new (void)
{
    g_autoptr(LrgSoundBank) bank = NULL;

    bank = lrg_sound_bank_new ("player-sounds");

    g_assert_nonnull (bank);
    g_assert_cmpstr (lrg_sound_bank_get_name (bank), ==, "player-sounds");
    g_assert_cmpuint (lrg_sound_bank_get_count (bank), ==, 0);
}

static void
test_sound_bank_properties (AudioFixture *fixture,
                             gconstpointer user_data)
{
    (void)user_data;

    /* Test name property */
    g_assert_cmpstr (lrg_sound_bank_get_name (fixture->bank), ==, "test-bank");

    /* Test base path */
    g_assert_null (lrg_sound_bank_get_base_path (fixture->bank));

    lrg_sound_bank_set_base_path (fixture->bank, "/path/to/sounds");
    g_assert_cmpstr (lrg_sound_bank_get_base_path (fixture->bank), ==, "/path/to/sounds");

    /* Test volume */
    g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (fixture->bank), 1.0f, 0.001f);

    lrg_sound_bank_set_volume (fixture->bank, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (fixture->bank), 0.5f, 0.001f);

    /* Test volume clamping */
    lrg_sound_bank_set_volume (fixture->bank, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (fixture->bank), 0.0f, 0.001f);

    lrg_sound_bank_set_volume (fixture->bank, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_sound_bank_get_volume (fixture->bank), 1.0f, 0.001f);
}

static void
test_sound_bank_contains (AudioFixture *fixture,
                           gconstpointer user_data)
{
    (void)user_data;

    /* Initially empty */
    g_assert_false (lrg_sound_bank_contains (fixture->bank, "jump"));
    g_assert_false (lrg_sound_bank_contains (fixture->bank, "land"));
}

static void
test_sound_bank_get_names (AudioFixture *fixture,
                            gconstpointer user_data)
{
    g_autoptr(GList) names = NULL;

    (void)user_data;

    /* Initially empty */
    names = lrg_sound_bank_get_names (fixture->bank);
    g_assert_null (names);
}

static void
test_sound_bank_clear (AudioFixture *fixture,
                        gconstpointer user_data)
{
    (void)user_data;

    /* Clear empty bank should work */
    lrg_sound_bank_clear (fixture->bank);
    g_assert_cmpuint (lrg_sound_bank_get_count (fixture->bank), ==, 0);
}

static void
test_sound_bank_play_missing (AudioFixture *fixture,
                               gconstpointer user_data)
{
    gboolean result;

    (void)user_data;

    /* Playing non-existent sound should fail and log a warning */
    g_test_expect_message ("Libregnum-Audio", G_LOG_LEVEL_WARNING,
                           "*Sound 'nonexistent' not found*");
    result = lrg_sound_bank_play (fixture->bank, "nonexistent");
    g_test_assert_expected_messages ();
    g_assert_false (result);

    g_test_expect_message ("Libregnum-Audio", G_LOG_LEVEL_WARNING,
                           "*Sound 'nonexistent' not found*");
    result = lrg_sound_bank_play_multi (fixture->bank, "nonexistent");
    g_test_assert_expected_messages ();
    g_assert_false (result);
}

static void
test_sound_bank_stop_missing (AudioFixture *fixture,
                               gconstpointer user_data)
{
    gboolean result;

    (void)user_data;

    /* Stopping non-existent sound should fail */
    result = lrg_sound_bank_stop (fixture->bank, "nonexistent");
    g_assert_false (result);
}

static void
test_sound_bank_stop_all (AudioFixture *fixture,
                           gconstpointer user_data)
{
    (void)user_data;

    /* Stopping all on empty bank should work */
    lrg_sound_bank_stop_all (fixture->bank);
    /* No assertion needed - just shouldn't crash */
}

/* ==========================================================================
 * LrgMusicTrack Tests
 * ========================================================================== */

static void
test_music_track_properties (void)
{
    /* We can't create a track without actual audio, but we can test
     * the property system via g_object_new with NULL music */
    g_autoptr(LrgMusicTrack) track = NULL;

    /* Create with NULL music - this is allowed for testing */
    track = g_object_new (LRG_TYPE_MUSIC_TRACK, NULL);
    g_assert_nonnull (track);

    /* Test name property */
    g_assert_null (lrg_music_track_get_name (track));
    lrg_music_track_set_name (track, "Battle Theme");
    g_assert_cmpstr (lrg_music_track_get_name (track), ==, "Battle Theme");

    /* Test volume property */
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_volume (track), 1.0f, 0.001f);
    lrg_music_track_set_volume (track, 0.7f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_volume (track), 0.7f, 0.001f);

    /* Test pitch property */
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_pitch (track), 1.0f, 0.001f);
    lrg_music_track_set_pitch (track, 1.5f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_pitch (track), 1.5f, 0.001f);

    /* Test looping property */
    g_assert_true (lrg_music_track_get_looping (track));
    lrg_music_track_set_looping (track, FALSE);
    g_assert_false (lrg_music_track_get_looping (track));
}

static void
test_music_track_loop_points (void)
{
    g_autoptr(LrgMusicTrack) track = NULL;

    track = g_object_new (LRG_TYPE_MUSIC_TRACK, NULL);
    g_assert_nonnull (track);

    /* No loop points initially */
    g_assert_false (lrg_music_track_has_loop_points (track));
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_loop_start (track), -1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_loop_end (track), -1.0f, 0.001f);

    /* Set loop points */
    lrg_music_track_set_loop_points (track, 5.0f, 60.0f);
    g_assert_true (lrg_music_track_has_loop_points (track));
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_loop_start (track), 5.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_loop_end (track), 60.0f, 0.001f);

    /* Clear loop points */
    lrg_music_track_clear_loop_points (track);
    g_assert_false (lrg_music_track_has_loop_points (track));
}

static void
test_music_track_fade (void)
{
    g_autoptr(LrgMusicTrack) track = NULL;

    track = g_object_new (LRG_TYPE_MUSIC_TRACK, NULL);
    g_assert_nonnull (track);

    /* Default fade values */
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_fade_in (track), 0.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_fade_out (track), 0.0f, 0.001f);

    /* Set fade values */
    lrg_music_track_set_fade_in (track, 2.0f);
    lrg_music_track_set_fade_out (track, 1.5f);

    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_fade_in (track), 2.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_fade_out (track), 1.5f, 0.001f);

    /* Negative values should be clamped to 0 */
    lrg_music_track_set_fade_in (track, -1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_music_track_get_fade_in (track), 0.0f, 0.001f);
}

/* ==========================================================================
 * LrgAudioManager Tests
 * ========================================================================== */

static void
test_audio_manager_singleton (void)
{
    LrgAudioManager *manager1;
    LrgAudioManager *manager2;

    manager1 = lrg_audio_manager_get_default ();
    manager2 = lrg_audio_manager_get_default ();

    g_assert_nonnull (manager1);
    g_assert_true (manager1 == manager2);
}

static void
test_audio_manager_volume (AudioFixture *fixture,
                            gconstpointer user_data)
{
    (void)user_data;

    /* Test master volume */
    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_master_volume (fixture->manager), 1.0f, 0.001f);

    lrg_audio_manager_set_master_volume (fixture->manager, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_master_volume (fixture->manager), 0.5f, 0.001f);

    /* Reset for other tests */
    lrg_audio_manager_set_master_volume (fixture->manager, 1.0f);
}

static void
test_audio_manager_sfx_volume (AudioFixture *fixture,
                                gconstpointer user_data)
{
    (void)user_data;

    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_sfx_volume (fixture->manager), 1.0f, 0.001f);

    lrg_audio_manager_set_sfx_volume (fixture->manager, 0.3f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_sfx_volume (fixture->manager), 0.3f, 0.001f);

    /* Reset */
    lrg_audio_manager_set_sfx_volume (fixture->manager, 1.0f);
}

static void
test_audio_manager_music_volume (AudioFixture *fixture,
                                  gconstpointer user_data)
{
    (void)user_data;

    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_music_volume (fixture->manager), 1.0f, 0.001f);

    lrg_audio_manager_set_music_volume (fixture->manager, 0.8f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_music_volume (fixture->manager), 0.8f, 0.001f);

    /* Reset */
    lrg_audio_manager_set_music_volume (fixture->manager, 1.0f);
}

static void
test_audio_manager_voice_volume (AudioFixture *fixture,
                                  gconstpointer user_data)
{
    (void)user_data;

    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_voice_volume (fixture->manager), 1.0f, 0.001f);

    lrg_audio_manager_set_voice_volume (fixture->manager, 0.9f);
    g_assert_cmpfloat_with_epsilon (lrg_audio_manager_get_voice_volume (fixture->manager), 0.9f, 0.001f);

    /* Reset */
    lrg_audio_manager_set_voice_volume (fixture->manager, 1.0f);
}

static void
test_audio_manager_mute (AudioFixture *fixture,
                          gconstpointer user_data)
{
    (void)user_data;

    g_assert_false (lrg_audio_manager_get_muted (fixture->manager));

    lrg_audio_manager_set_muted (fixture->manager, TRUE);
    g_assert_true (lrg_audio_manager_get_muted (fixture->manager));

    lrg_audio_manager_set_muted (fixture->manager, FALSE);
    g_assert_false (lrg_audio_manager_get_muted (fixture->manager));
}

static void
test_audio_manager_add_bank (AudioFixture *fixture,
                              gconstpointer user_data)
{
    g_autoptr(LrgSoundBank) bank = NULL;
    LrgSoundBank *retrieved;

    (void)user_data;

    bank = lrg_sound_bank_new ("effects");
    lrg_audio_manager_add_bank (fixture->manager, bank);

    retrieved = lrg_audio_manager_get_bank (fixture->manager, "effects");
    g_assert_nonnull (retrieved);
    g_assert_true (retrieved == bank);

    /* Clean up */
    lrg_audio_manager_remove_bank (fixture->manager, "effects");
}

static void
test_audio_manager_remove_bank (AudioFixture *fixture,
                                 gconstpointer user_data)
{
    g_autoptr(LrgSoundBank) bank = NULL;
    gboolean result;

    (void)user_data;

    bank = lrg_sound_bank_new ("temporary");
    lrg_audio_manager_add_bank (fixture->manager, bank);

    result = lrg_audio_manager_remove_bank (fixture->manager, "temporary");
    g_assert_true (result);

    g_assert_null (lrg_audio_manager_get_bank (fixture->manager, "temporary"));

    /* Removing non-existent bank */
    result = lrg_audio_manager_remove_bank (fixture->manager, "nonexistent");
    g_assert_false (result);
}

static void
test_audio_manager_get_bank_names (AudioFixture *fixture,
                                    gconstpointer user_data)
{
    g_autoptr(LrgSoundBank) bank1 = NULL;
    g_autoptr(LrgSoundBank) bank2 = NULL;
    g_autoptr(GList) names = NULL;

    (void)user_data;

    bank1 = lrg_sound_bank_new ("bank-alpha");
    bank2 = lrg_sound_bank_new ("bank-beta");

    lrg_audio_manager_add_bank (fixture->manager, bank1);
    lrg_audio_manager_add_bank (fixture->manager, bank2);

    names = lrg_audio_manager_get_bank_names (fixture->manager);
    g_assert_cmpuint (g_list_length (names), >=, 2);

    /* Clean up */
    lrg_audio_manager_remove_bank (fixture->manager, "bank-alpha");
    lrg_audio_manager_remove_bank (fixture->manager, "bank-beta");
}

static void
test_audio_manager_play_sound_missing (AudioFixture *fixture,
                                        gconstpointer user_data)
{
    gboolean result;

    (void)user_data;

    /* Playing from non-existent bank should fail and log a warning */
    g_test_expect_message ("Libregnum-Audio", G_LOG_LEVEL_WARNING,
                           "*Sound bank 'nonexistent' not found*");
    result = lrg_audio_manager_play_sound (fixture->manager, "nonexistent", "jump");
    g_test_assert_expected_messages ();
    g_assert_false (result);
}

static void
test_audio_manager_no_music (AudioFixture *fixture,
                              gconstpointer user_data)
{
    (void)user_data;

    /* Initially no music */
    g_assert_null (lrg_audio_manager_get_current_music (fixture->manager));
    g_assert_false (lrg_audio_manager_is_music_playing (fixture->manager));
    g_assert_false (lrg_audio_manager_is_crossfading (fixture->manager));
}

static void
test_audio_manager_stop_all (AudioFixture *fixture,
                              gconstpointer user_data)
{
    (void)user_data;

    /* Should not crash even with no sounds */
    lrg_audio_manager_stop_all_sounds (fixture->manager);
    lrg_audio_manager_stop_music (fixture->manager);
}

static void
test_audio_manager_update (AudioFixture *fixture,
                            gconstpointer user_data)
{
    (void)user_data;

    /* Update should work even with nothing playing */
    lrg_audio_manager_update (fixture->manager);
    /* No assertion needed - just shouldn't crash */
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Sound bank tests */
    g_test_add_func ("/audio/sound-bank/new", test_sound_bank_new);
    g_test_add ("/audio/sound-bank/properties", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_properties, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/contains", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_contains, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/get-names", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_get_names, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/clear", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_clear, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/play-missing", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_play_missing, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/stop-missing", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_stop_missing, audio_fixture_tear_down);
    g_test_add ("/audio/sound-bank/stop-all", AudioFixture, NULL,
                audio_fixture_set_up, test_sound_bank_stop_all, audio_fixture_tear_down);

    /* Music track tests */
    g_test_add_func ("/audio/music-track/properties", test_music_track_properties);
    g_test_add_func ("/audio/music-track/loop-points", test_music_track_loop_points);
    g_test_add_func ("/audio/music-track/fade", test_music_track_fade);

    /* Audio manager tests */
    g_test_add_func ("/audio/manager/singleton", test_audio_manager_singleton);
    g_test_add ("/audio/manager/volume", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_volume, audio_fixture_tear_down);
    g_test_add ("/audio/manager/sfx-volume", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_sfx_volume, audio_fixture_tear_down);
    g_test_add ("/audio/manager/music-volume", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_music_volume, audio_fixture_tear_down);
    g_test_add ("/audio/manager/voice-volume", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_voice_volume, audio_fixture_tear_down);
    g_test_add ("/audio/manager/mute", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_mute, audio_fixture_tear_down);
    g_test_add ("/audio/manager/add-bank", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_add_bank, audio_fixture_tear_down);
    g_test_add ("/audio/manager/remove-bank", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_remove_bank, audio_fixture_tear_down);
    g_test_add ("/audio/manager/get-bank-names", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_get_bank_names, audio_fixture_tear_down);
    g_test_add ("/audio/manager/play-sound-missing", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_play_sound_missing, audio_fixture_tear_down);
    g_test_add ("/audio/manager/no-music", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_no_music, audio_fixture_tear_down);
    g_test_add ("/audio/manager/stop-all", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_stop_all, audio_fixture_tear_down);
    g_test_add ("/audio/manager/update", AudioFixture, NULL,
                audio_fixture_set_up, test_audio_manager_update, audio_fixture_tear_down);

    return g_test_run ();
}
