/* test-video.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for video playback module.
 */

#define LIBREGNUM_COMPILATION 1

#include <glib.h>
#include <glib-object.h>
#include "../src/libregnum.h"

/*
 * ============================================================================
 * Fixtures
 * ============================================================================
 */

/* SubtitleTrack fixture */
typedef struct
{
    LrgVideoSubtitleTrack *track;
} SubtitleTrackFixture;

static void
subtitle_track_fixture_set_up (SubtitleTrackFixture *fixture,
                               gconstpointer         user_data)
{
    (void) user_data;
    fixture->track = lrg_video_subtitle_track_new ();
}

static void
subtitle_track_fixture_tear_down (SubtitleTrackFixture *fixture,
                                  gconstpointer         user_data)
{
    (void) user_data;
    g_clear_object (&fixture->track);
}

/* VideoPlayer fixture */
typedef struct
{
    LrgVideoPlayer *player;
} VideoPlayerFixture;

static void
video_player_fixture_set_up (VideoPlayerFixture *fixture,
                             gconstpointer       user_data)
{
    (void) user_data;
    fixture->player = lrg_video_player_new ();
}

static void
video_player_fixture_tear_down (VideoPlayerFixture *fixture,
                                gconstpointer       user_data)
{
    (void) user_data;
    g_clear_object (&fixture->player);
}

/*
 * ============================================================================
 * LrgSubtitleCue Tests
 * ============================================================================
 */

static void
test_subtitle_cue_new (void)
{
    LrgSubtitleCue *cue = lrg_subtitle_cue_new (1.0, 5.0, "Hello World");

    g_assert_nonnull (cue);
    g_assert_cmpfloat_with_epsilon (lrg_subtitle_cue_get_start_time (cue), 1.0, 0.001);
    g_assert_cmpfloat_with_epsilon (lrg_subtitle_cue_get_end_time (cue), 5.0, 0.001);
    g_assert_cmpstr (lrg_subtitle_cue_get_text (cue), ==, "Hello World");

    lrg_subtitle_cue_free (cue);
}

static void
test_subtitle_cue_copy (void)
{
    LrgSubtitleCue *cue = NULL;
    LrgSubtitleCue *copy = NULL;

    cue = lrg_subtitle_cue_new (2.0, 6.0, "Test");
    copy = lrg_subtitle_cue_copy (cue);

    g_assert_nonnull (copy);
    g_assert_cmpfloat_with_epsilon (lrg_subtitle_cue_get_start_time (copy), 2.0, 0.001);
    g_assert_cmpfloat_with_epsilon (lrg_subtitle_cue_get_end_time (copy), 6.0, 0.001);
    g_assert_cmpstr (lrg_subtitle_cue_get_text (copy), ==, "Test");

    lrg_subtitle_cue_free (cue);
    lrg_subtitle_cue_free (copy);
}

static void
test_subtitle_cue_contains_time (void)
{
    LrgSubtitleCue *cue = lrg_subtitle_cue_new (5.0, 10.0, "Text");

    g_assert_false (lrg_subtitle_cue_contains_time (cue, 4.9));
    g_assert_true (lrg_subtitle_cue_contains_time (cue, 5.0));
    g_assert_true (lrg_subtitle_cue_contains_time (cue, 7.5));
    g_assert_true (lrg_subtitle_cue_contains_time (cue, 9.9));
    g_assert_false (lrg_subtitle_cue_contains_time (cue, 10.0));

    lrg_subtitle_cue_free (cue);
}

/*
 * ============================================================================
 * LrgVideoSubtitleTrack Tests
 * ============================================================================
 */

static void
test_subtitle_track_new (void)
{
    g_autoptr(LrgVideoSubtitleTrack) track = lrg_video_subtitle_track_new ();

    g_assert_nonnull (track);
    g_assert_cmpuint (lrg_video_subtitle_track_get_cue_count (track), ==, 0);
}

static void
test_subtitle_track_add_cue (SubtitleTrackFixture *fixture,
                             gconstpointer         user_data)
{
    LrgSubtitleCue *cue = NULL;

    (void) user_data;
    cue = lrg_subtitle_cue_new (0.0, 3.0, "First");

    lrg_video_subtitle_track_add_cue (fixture->track, cue);
    g_assert_cmpuint (lrg_video_subtitle_track_get_cue_count (fixture->track), ==, 1);
}

static void
test_subtitle_track_clear (SubtitleTrackFixture *fixture,
                           gconstpointer         user_data)
{
    (void) user_data;

    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (0.0, 1.0, "One"));
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (1.0, 2.0, "Two"));
    g_assert_cmpuint (lrg_video_subtitle_track_get_cue_count (fixture->track), ==, 2);

    lrg_video_subtitle_track_clear (fixture->track);
    g_assert_cmpuint (lrg_video_subtitle_track_get_cue_count (fixture->track), ==, 0);
}

static void
test_subtitle_track_get_cue (SubtitleTrackFixture *fixture,
                             gconstpointer         user_data)
{
    const LrgSubtitleCue *cue = NULL;

    (void) user_data;

    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (0.0, 1.0, "First"));
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (1.0, 2.0, "Second"));

    cue = lrg_video_subtitle_track_get_cue (fixture->track, 1);
    g_assert_nonnull (cue);
    g_assert_cmpstr (lrg_subtitle_cue_get_text (cue), ==, "Second");

    /* Invalid index */
    g_assert_null (lrg_video_subtitle_track_get_cue (fixture->track, 10));
}

static void
test_subtitle_track_get_text_at (SubtitleTrackFixture *fixture,
                                 gconstpointer         user_data)
{
    g_autofree gchar *text1 = NULL;
    g_autofree gchar *text2 = NULL;
    g_autofree gchar *text3 = NULL;

    (void) user_data;

    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (0.0, 2.0, "First"));
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (3.0, 5.0, "Second"));

    text1 = lrg_video_subtitle_track_get_text_at (fixture->track, 1.0);
    g_assert_cmpstr (text1, ==, "First");

    text2 = lrg_video_subtitle_track_get_text_at (fixture->track, 4.0);
    g_assert_cmpstr (text2, ==, "Second");

    text3 = lrg_video_subtitle_track_get_text_at (fixture->track, 2.5);
    g_assert_null (text3);
}

static void
test_subtitle_track_get_cues_at (SubtitleTrackFixture *fixture,
                                 gconstpointer         user_data)
{
    GPtrArray *cues = NULL;

    (void) user_data;

    /* Add overlapping cues */
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (0.0, 3.0, "First"));
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (2.0, 5.0, "Second"));

    cues = lrg_video_subtitle_track_get_cues_at (fixture->track, 2.5);
    g_assert_cmpuint (cues->len, ==, 2);
    g_ptr_array_unref (cues);
}

static void
test_subtitle_track_duration (SubtitleTrackFixture *fixture,
                              gconstpointer         user_data)
{
    (void) user_data;

    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (0.0, 5.0, "First"));
    lrg_video_subtitle_track_add_cue (fixture->track, lrg_subtitle_cue_new (6.0, 10.0, "Second"));

    g_assert_cmpfloat_with_epsilon (lrg_video_subtitle_track_get_duration (fixture->track), 10.0, 0.001);
}

static void
test_subtitle_track_language (SubtitleTrackFixture *fixture,
                              gconstpointer         user_data)
{
    (void) user_data;

    g_assert_null (lrg_video_subtitle_track_get_language (fixture->track));

    lrg_video_subtitle_track_set_language (fixture->track, "en");
    g_assert_cmpstr (lrg_video_subtitle_track_get_language (fixture->track), ==, "en");

    lrg_video_subtitle_track_set_language (fixture->track, "es");
    g_assert_cmpstr (lrg_video_subtitle_track_get_language (fixture->track), ==, "es");
}

static void
test_subtitle_track_load_srt_data (SubtitleTrackFixture *fixture,
                                   gconstpointer         user_data)
{
    g_autoptr(GError) error = NULL;
    const gchar *srt_data = NULL;
    gboolean result;

    (void) user_data;

    srt_data =
        "1\n"
        "00:00:01,000 --> 00:00:04,000\n"
        "First subtitle\n"
        "\n"
        "2\n"
        "00:00:05,000 --> 00:00:08,000\n"
        "Second subtitle\n";

    result = lrg_video_subtitle_track_load_data (fixture->track, srt_data, "srt", &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_video_subtitle_track_get_cue_count (fixture->track), ==, 2);
}

/*
 * ============================================================================
 * LrgVideoPlayer Tests
 * ============================================================================
 */

static void
test_video_player_new (void)
{
    g_autoptr(LrgVideoPlayer) player = lrg_video_player_new ();

    g_assert_nonnull (player);
    g_assert_false (lrg_video_player_is_open (player));
    g_assert_cmpint (lrg_video_player_get_state (player), ==, LRG_VIDEO_STATE_STOPPED);
}

static void
test_video_player_state_transitions (VideoPlayerFixture *fixture,
                                     gconstpointer       user_data)
{
    (void) user_data;

    /* Initial state */
    g_assert_cmpint (lrg_video_player_get_state (fixture->player), ==, LRG_VIDEO_STATE_STOPPED);

    /* Without a video open, play/pause/stop should be no-ops or safe */
    lrg_video_player_play (fixture->player);
    lrg_video_player_pause (fixture->player);
    lrg_video_player_stop (fixture->player);

    /* State should still be idle since no video is open */
    g_assert_cmpint (lrg_video_player_get_state (fixture->player), ==, LRG_VIDEO_STATE_STOPPED);
}

static void
test_video_player_volume (VideoPlayerFixture *fixture,
                          gconstpointer       user_data)
{
    (void) user_data;

    /* Default volume */
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_volume (fixture->player), 1.0f, 0.001f);

    /* Set volume */
    lrg_video_player_set_volume (fixture->player, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_volume (fixture->player), 0.5f, 0.001f);

    /* Set to 0 */
    lrg_video_player_set_volume (fixture->player, 0.0f);
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_volume (fixture->player), 0.0f, 0.001f);
}

static void
test_video_player_mute (VideoPlayerFixture *fixture,
                        gconstpointer       user_data)
{
    (void) user_data;

    /* Default not muted */
    g_assert_false (lrg_video_player_get_muted (fixture->player));

    /* Mute */
    lrg_video_player_set_muted (fixture->player, TRUE);
    g_assert_true (lrg_video_player_get_muted (fixture->player));

    /* Unmute */
    lrg_video_player_set_muted (fixture->player, FALSE);
    g_assert_false (lrg_video_player_get_muted (fixture->player));
}

static void
test_video_player_loop (VideoPlayerFixture *fixture,
                        gconstpointer       user_data)
{
    (void) user_data;

    /* Default not looping */
    g_assert_false (lrg_video_player_get_loop (fixture->player));

    /* Enable loop */
    lrg_video_player_set_loop (fixture->player, TRUE);
    g_assert_true (lrg_video_player_get_loop (fixture->player));

    /* Disable loop */
    lrg_video_player_set_loop (fixture->player, FALSE);
    g_assert_false (lrg_video_player_get_loop (fixture->player));
}

static void
test_video_player_playback_rate (VideoPlayerFixture *fixture,
                                 gconstpointer       user_data)
{
    (void) user_data;

    /* Default rate */
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_playback_rate (fixture->player), 1.0f, 0.001f);

    /* Set rate */
    lrg_video_player_set_playback_rate (fixture->player, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_playback_rate (fixture->player), 2.0f, 0.001f);

    /* Half speed */
    lrg_video_player_set_playback_rate (fixture->player, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_playback_rate (fixture->player), 0.5f, 0.001f);
}

static void
test_video_player_update (VideoPlayerFixture *fixture,
                          gconstpointer       user_data)
{
    (void) user_data;

    /* Update without video should not crash */
    lrg_video_player_update (fixture->player, 0.016f);
    lrg_video_player_update (fixture->player, 0.016f);
}

static void
test_video_player_subtitles (VideoPlayerFixture *fixture,
                             gconstpointer       user_data)
{
    LrgVideoSubtitles *subtitles = NULL;

    (void) user_data;

    subtitles = lrg_video_player_get_subtitles (fixture->player);
    g_assert_nonnull (subtitles);
}

static void
test_video_player_seek_no_video (VideoPlayerFixture *fixture,
                                 gconstpointer       user_data)
{
    (void) user_data;

    /* Seek without video should not crash */
    lrg_video_player_seek (fixture->player, 5.0);
    g_assert_cmpfloat_with_epsilon (lrg_video_player_get_position (fixture->player), 0.0, 0.001);
}

static void
test_video_player_error (VideoPlayerFixture *fixture,
                         gconstpointer       user_data)
{
    (void) user_data;

    /* No error initially */
    g_assert_cmpint (lrg_video_player_get_error (fixture->player), ==, LRG_VIDEO_ERROR_NONE);
    g_assert_null (lrg_video_player_get_error_message (fixture->player));
}

static void
test_video_player_close_no_video (VideoPlayerFixture *fixture,
                                  gconstpointer       user_data)
{
    (void) user_data;

    /* Close without video should not crash */
    lrg_video_player_close (fixture->player);
    g_assert_false (lrg_video_player_is_open (fixture->player));
}

/*
 * ============================================================================
 * Main
 * ============================================================================
 */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* LrgSubtitleCue tests */
    g_test_add_func ("/video/subtitle-cue/new", test_subtitle_cue_new);
    g_test_add_func ("/video/subtitle-cue/copy", test_subtitle_cue_copy);
    g_test_add_func ("/video/subtitle-cue/contains-time", test_subtitle_cue_contains_time);

    /* LrgVideoSubtitleTrack tests */
    g_test_add_func ("/video/subtitle-track/new", test_subtitle_track_new);
    g_test_add ("/video/subtitle-track/add-cue", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_add_cue, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/clear", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_clear, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/get-cue", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_get_cue, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/get-text-at", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_get_text_at, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/get-cues-at", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_get_cues_at, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/duration", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_duration, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/language", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_language, subtitle_track_fixture_tear_down);
    g_test_add ("/video/subtitle-track/load-srt-data", SubtitleTrackFixture, NULL,
                subtitle_track_fixture_set_up, test_subtitle_track_load_srt_data, subtitle_track_fixture_tear_down);

    /* LrgVideoPlayer tests */
    g_test_add_func ("/video/player/new", test_video_player_new);
    g_test_add ("/video/player/state-transitions", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_state_transitions, video_player_fixture_tear_down);
    g_test_add ("/video/player/volume", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_volume, video_player_fixture_tear_down);
    g_test_add ("/video/player/mute", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_mute, video_player_fixture_tear_down);
    g_test_add ("/video/player/loop", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_loop, video_player_fixture_tear_down);
    g_test_add ("/video/player/playback-rate", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_playback_rate, video_player_fixture_tear_down);
    g_test_add ("/video/player/update", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_update, video_player_fixture_tear_down);
    g_test_add ("/video/player/subtitles", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_subtitles, video_player_fixture_tear_down);
    g_test_add ("/video/player/seek-no-video", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_seek_no_video, video_player_fixture_tear_down);
    g_test_add ("/video/player/error", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_error, video_player_fixture_tear_down);
    g_test_add ("/video/player/close-no-video", VideoPlayerFixture, NULL,
                video_player_fixture_set_up, test_video_player_close_no_video, video_player_fixture_tear_down);

    return g_test_run ();
}
