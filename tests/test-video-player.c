/* test-video-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the FFmpeg/libav decode backend, exercised through the public
 * LrgVideoPlayer API (the internal LrgVideoDecoder is covered indirectly).
 * Only built when FFMPEG=1.  A small fixture clip is generated with the
 * `ffmpeg` binary; if that is unavailable every test skips.
 */

#include <libregnum.h>
#include <glib/gstdio.h>

#define FIXTURE_W 320
#define FIXTURE_H 240
#define FIXTURE_FPS 25
#define FIXTURE_SECONDS 2

static gchar    *g_tmpdir;
static gchar    *g_fixture;     /* path to the generated mp4 */
static gboolean  g_fixture_ok;

#define SKIP_IF_NO_FIXTURE()                                       \
    do {                                                           \
        if (!g_fixture_ok)                                         \
        {                                                          \
            g_test_skip ("ffmpeg not available to build fixture"); \
            return;                                                \
        }                                                          \
    } while (0)

/* Generate a deterministic test clip with the ffmpeg CLI. */
static gboolean
build_fixture (void)
{
    g_autofree gchar *ffmpeg = g_find_program_in_path ("ffmpeg");
    g_autoptr(GError) error = NULL;
    gint status = 0;
    gchar *argv[16];
    int i = 0;

    if (ffmpeg == NULL)
        return FALSE;

    g_tmpdir = g_dir_make_tmp ("lrg-vidtest-XXXXXX", &error);
    if (g_tmpdir == NULL)
        return FALSE;
    g_fixture = g_build_filename (g_tmpdir, "fixture.mp4", NULL);

    argv[i++] = ffmpeg;
    argv[i++] = "-y";
    argv[i++] = "-loglevel";
    argv[i++] = "error";
    argv[i++] = "-f";
    argv[i++] = "lavfi";
    argv[i++] = "-i";
    argv[i++] = "testsrc=duration=" G_STRINGIFY (FIXTURE_SECONDS)
                ":size=" G_STRINGIFY (FIXTURE_W) "x" G_STRINGIFY (FIXTURE_H)
                ":rate=" G_STRINGIFY (FIXTURE_FPS);
    argv[i++] = "-pix_fmt";
    argv[i++] = "yuv420p";
    argv[i++] = g_fixture;
    argv[i] = NULL;

    if (!g_spawn_sync (NULL, argv, NULL,
                       G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL,
                       &status, &error))
        return FALSE;

    if (!g_spawn_check_wait_status (status, NULL))
        return FALSE;

    return g_file_test (g_fixture, G_FILE_TEST_EXISTS);
}

/* Cheap content hash of the current frame, to detect that decoding actually
 * produces (and changes) pixels. */
static guint32
frame_hash (LrgVideoPlayer *player)
{
    LrgVideoTexture *tex = lrg_video_player_get_texture (player);
    const guint8 *data;
    gsize size = 0;
    guint32 h = 2166136261u;
    gsize i;

    if (tex == NULL || !lrg_video_texture_is_valid (tex))
        return 0;
    data = lrg_video_texture_get_data (tex, &size);
    if (data == NULL)
        return 0;
    /* Sample every 257th byte so the hash is cheap but content-sensitive. */
    for (i = 0; i < size; i += 257)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static void
test_open_info (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    LrgVideoTexture *tex;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_video_player_is_open (player));
    g_assert_cmpuint (lrg_video_player_get_width (player), ==, FIXTURE_W);
    g_assert_cmpuint (lrg_video_player_get_height (player), ==, FIXTURE_H);
    g_assert_cmpfloat (lrg_video_player_get_frame_rate (player), >=, 24.0);
    g_assert_cmpfloat (lrg_video_player_get_frame_rate (player), <=, 26.0);
    g_assert_cmpfloat (lrg_video_player_get_duration (player), >=, 1.7);
    g_assert_cmpfloat (lrg_video_player_get_duration (player), <=, 2.3);

    /* The first frame is primed at open. */
    tex = lrg_video_player_get_texture (player);
    g_assert_nonnull (tex);
    g_assert_true (lrg_video_texture_is_valid (tex));
}

static void
test_open_missing (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_false (lrg_video_player_open (player, "/no/such/video.mp4",
                                           &error));
    g_assert_nonnull (error);
    g_assert_false (lrg_video_player_is_open (player));
    g_assert_cmpint (lrg_video_player_get_state (player), ==,
                     LRG_VIDEO_STATE_ERROR);
}

static void
test_frame_advances (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    guint32 h0, h1;
    int i;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));

    h0 = frame_hash (player);
    g_assert_cmpuint (h0, !=, 0);

    /* Advancing playback must change the decoded frame (testsrc animates). */
    lrg_video_player_play (player);
    for (i = 0; i < 10; i++)
        lrg_video_player_update (player, 1.0f / FIXTURE_FPS);

    g_assert_cmpfloat (lrg_video_player_get_position (player), >, 0.0);
    h1 = frame_hash (player);
    g_assert_cmpuint (h1, !=, 0);
    g_assert_cmpuint (h0, !=, h1);
}

static void
test_play_to_end (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    int guard;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));
    lrg_video_player_play (player);

    /* Step in ~frame increments until the player reports it finished. */
    for (guard = 0; guard < 1000; guard++)
    {
        if (lrg_video_player_get_state (player) == LRG_VIDEO_STATE_FINISHED)
            break;
        lrg_video_player_update (player, 1.0f / FIXTURE_FPS);
    }

    g_assert_cmpint (lrg_video_player_get_state (player), ==,
                     LRG_VIDEO_STATE_FINISHED);
    g_assert_cmpfloat (lrg_video_player_get_position (player), >=,
                       lrg_video_player_get_duration (player) - 0.1);
}

static void
test_seek (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    guint32 h_start, h_mid;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));

    h_start = frame_hash (player);

    lrg_video_player_seek (player, 1.0);
    g_assert_cmpfloat (lrg_video_player_get_position (player), >=, 0.95);
    g_assert_cmpfloat (lrg_video_player_get_position (player), <=, 1.05);
    g_assert_true (lrg_video_texture_is_valid (
                       lrg_video_player_get_texture (player)));
    h_mid = frame_hash (player);
    /* A seek to a different time should land on a different frame. */
    g_assert_cmpuint (h_start, !=, h_mid);

    /* Seek back to the start. */
    lrg_video_player_seek (player, 0.0);
    g_assert_cmpfloat (lrg_video_player_get_position (player), <=, 0.1);
}

static void
test_seek_past_end (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));

    /* Clamped to duration; must not crash and the frame stays valid. */
    lrg_video_player_seek (player, 100.0);
    g_assert_cmpfloat (lrg_video_player_get_position (player), <=,
                       lrg_video_player_get_duration (player) + 0.001);
    g_assert_true (lrg_video_texture_is_valid (
                       lrg_video_player_get_texture (player)));
}

static void
test_loop (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;

    SKIP_IF_NO_FIXTURE ();

    player = lrg_video_player_new ();
    g_assert_true (lrg_video_player_open (player, g_fixture, &error));
    lrg_video_player_set_loop (player, TRUE);
    lrg_video_player_play (player);

    /* Advance well past the duration; looping must wrap, not finish. */
    lrg_video_player_update (player, 5.0f);
    g_assert_cmpint (lrg_video_player_get_state (player), ==,
                     LRG_VIDEO_STATE_PLAYING);
    g_assert_cmpfloat (lrg_video_player_get_position (player), <,
                       lrg_video_player_get_duration (player) + 0.001);
}

static void
test_reopen (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    int i;

    SKIP_IF_NO_FIXTURE ();

    /* Open/close many times on one player: catches decoder leaks/dangles
     * under ASan / valgrind. */
    player = lrg_video_player_new ();
    for (i = 0; i < 30; i++)
    {
        g_autoptr(GError) error = NULL;
        g_assert_true (lrg_video_player_open (player, g_fixture, &error));
        lrg_video_player_play (player);
        lrg_video_player_update (player, 0.2f);
        lrg_video_player_close (player);
    }
}

int
main (int argc, char *argv[])
{
    int ret;

    g_test_init (&argc, &argv, NULL);

    g_fixture_ok = build_fixture ();

    g_test_add_func ("/video/player/open-info", test_open_info);
    g_test_add_func ("/video/player/open-missing", test_open_missing);
    g_test_add_func ("/video/player/frame-advances", test_frame_advances);
    g_test_add_func ("/video/player/play-to-end", test_play_to_end);
    g_test_add_func ("/video/player/seek", test_seek);
    g_test_add_func ("/video/player/seek-past-end", test_seek_past_end);
    g_test_add_func ("/video/player/loop", test_loop);
    g_test_add_func ("/video/player/reopen", test_reopen);

    ret = g_test_run ();

    /* Clean up the fixture. */
    if (g_fixture != NULL)
        g_unlink (g_fixture);
    if (g_tmpdir != NULL)
        g_rmdir (g_tmpdir);
    g_free (g_fixture);
    g_free (g_tmpdir);

    return ret;
}
