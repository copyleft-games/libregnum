/* test-video-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the FFmpeg/libav decode backend, exercised through the public
 * LrgVideoPlayer API (the internal LrgVideoDecoder is covered indirectly).
 * Only built when FFMPEG=1.  Small fixture clips are generated with the
 * `ffmpeg` binary; if that is unavailable every test skips.  The audio tests
 * also need its libmp3lame encoder: their fixtures mirror World of Warcraft's
 * movies (AVI, MPEG-4 part 2 video, MP3 audio) and they skip without it.
 */

#include <libregnum.h>
#include <glib/gstdio.h>
#include <math.h>

#define FIXTURE_W 320
#define FIXTURE_H 240
#define FIXTURE_FPS 25
#define FIXTURE_SECONDS 2

#define AUDIO_RATE 44100
#define AUDIO_TONE 440
/* The player's lead bound (LRG_VIDEO_PLAYER_AUDIO_LEAD) and the frames the
 * tests pull per read. */
#define AUDIO_LEAD 0.3
#define AUDIO_CHUNK 1024

static gchar    *g_tmpdir;
static gchar    *g_fixture;     /* path to the generated mp4 */
static gboolean  g_fixture_ok;
static gchar    *g_avi;         /* mpeg4 + mp3 stereo AVI */
static gchar    *g_avi_mono;    /* mpeg4 + mp3 mono AVI   */
static gboolean  g_avi_ok;

#define SKIP_IF_NO_AVI()                                                   \
    do {                                                                   \
        if (!g_avi_ok)                                                     \
        {                                                                  \
            g_test_skip ("ffmpeg with libmp3lame not available to build "  \
                         "the audio fixtures");                            \
            return;                                                        \
        }                                                                  \
    } while (0)

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

/* Generate an AVI with mpeg4 video and libmp3lame audio (a 440 Hz sine) at
 * PATH, with CHANNELS audio channels. */
static gboolean
build_avi (const gchar *path,
           guint        channels)
{
    g_autofree gchar *ffmpeg = g_find_program_in_path ("ffmpeg");
    g_autofree gchar *ac = g_strdup_printf ("%u", channels);
    g_autoptr(GError) error = NULL;
    gint status = 0;
    gchar *argv[32];
    int i = 0;

    if (ffmpeg == NULL || g_tmpdir == NULL)
        return FALSE;

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
    argv[i++] = "-f";
    argv[i++] = "lavfi";
    argv[i++] = "-i";
    argv[i++] = "sine=frequency=" G_STRINGIFY (AUDIO_TONE)
                ":sample_rate=" G_STRINGIFY (AUDIO_RATE)
                ":duration=" G_STRINGIFY (FIXTURE_SECONDS);
    argv[i++] = "-c:v";
    argv[i++] = "mpeg4";
    argv[i++] = "-c:a";
    argv[i++] = "libmp3lame";
    argv[i++] = "-ac";
    argv[i++] = ac;
    argv[i++] = "-ar";
    argv[i++] = G_STRINGIFY (AUDIO_RATE);
    argv[i++] = (gchar *) path;
    argv[i] = NULL;

    if (!g_spawn_sync (NULL, argv, NULL,
                       G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL,
                       &status, &error))
        return FALSE;

    if (!g_spawn_check_wait_status (status, NULL))
        return FALSE;

    return g_file_test (path, G_FILE_TEST_EXISTS);
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

/* ---- audio ------------------------------------------------------------ */

/* What one pull run delivered. */
typedef struct
{
    guint64  frames;      /* frames delivered in total             */
    gdouble  rms;         /* RMS over every sample (s16 units)     */
    guint    crossings;   /* zero crossings of the first channel   */
    gboolean all_zero;    /* every sample was 0                    */
    gboolean lead_ok;     /* the lead bound held after every read  */
    gboolean finished;    /* the player reached FINISHED           */
} PullStats;

/* Open PATH with audio enabled and the given gain settings, play it to the
 * end in 1/60 s updates, pulling AUDIO_CHUNK frames at a time after each
 * update until nothing is due, and collect statistics. */
static void
pull_run (const gchar *path,
          gfloat       volume,
          gboolean     muted,
          PullStats   *st)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gint16 *buf = NULL;
    gdouble sum_sq = 0.0;
    guint64 n_samples = 0;
    guint channels;
    guint rate;
    gint16 prev = 0;
    gboolean have_prev = FALSE;
    int guard;

    memset (st, 0, sizeof (*st));
    st->all_zero = TRUE;
    st->lead_ok = TRUE;

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, path, &error));
    g_assert_no_error (error);
    lrg_video_player_set_volume (player, volume);
    lrg_video_player_set_muted (player, muted);

    channels = lrg_video_player_get_audio_channels (player);
    rate = lrg_video_player_get_audio_sample_rate (player);
    g_assert_cmpuint (channels, >=, 1);
    g_assert_cmpuint (rate, >, 0);
    buf = g_new0 (gint16, (gsize) AUDIO_CHUNK * channels);

    lrg_video_player_play (player);
    for (guard = 0; guard < 2000; guard++)
    {
        guint got;

        if (lrg_video_player_get_state (player) == LRG_VIDEO_STATE_FINISHED)
        {
            st->finished = TRUE;
            break;
        }
        lrg_video_player_update (player, 1.0f / 60.0f);

        while ((got = lrg_video_player_read_audio (player, buf,
                                                   AUDIO_CHUNK)) > 0)
        {
            gsize i;

            for (i = 0; i < (gsize) got * channels; i++)
            {
                sum_sq += (gdouble) buf[i] * (gdouble) buf[i];
                if (buf[i] != 0)
                    st->all_zero = FALSE;
            }
            n_samples += (guint64) got * channels;

            for (i = 0; i < got; i++)
            {
                gint16 v = buf[i * channels];

                if (have_prev && ((prev < 0 && v >= 0) || (prev >= 0 && v < 0)))
                    st->crossings++;
                prev = v;
                have_prev = TRUE;
            }
            st->frames += got;

            /* The audio clock starts at 0, so the frames delivered so far
             * may not exceed (position + lead) seconds' worth by more than
             * one chunk. */
            if ((gdouble) st->frames >
                (lrg_video_player_get_position (player) + AUDIO_LEAD) * rate +
                AUDIO_CHUNK)
                st->lead_ok = FALSE;
        }
    }

    st->rms = (n_samples > 0) ? sqrt (sum_sq / (gdouble) n_samples) : 0.0;
    g_test_message ("%s: volume %.2f muted %d: %" G_GUINT64_FORMAT
                    " frames, rms %.1f, %u crossings, lead %s",
                    path, (gdouble) volume, muted, st->frames, st->rms,
                    st->crossings, st->lead_ok ? "ok" : "exceeded");
}

static void
test_audio_info (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    g_assert_false (lrg_video_player_has_audio (player));
    g_assert_cmpuint (lrg_video_player_get_audio_sample_rate (player), ==, 0);

    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_video_player_get_width (player), ==, FIXTURE_W);
    g_assert_true (lrg_video_player_has_audio (player));
    g_assert_cmpuint (lrg_video_player_get_audio_sample_rate (player), ==,
                      AUDIO_RATE);
    g_assert_cmpuint (lrg_video_player_get_audio_channels (player), ==, 2);

    /* The video-only clip has no audio. */
    if (g_fixture_ok)
    {
        g_assert_true (lrg_video_player_open (player, g_fixture, &error));
        g_assert_no_error (error);
        g_assert_false (lrg_video_player_has_audio (player));
        g_assert_cmpuint (lrg_video_player_get_audio_sample_rate (player),
                          ==, 0);
        g_assert_cmpuint (lrg_video_player_get_audio_channels (player),
                          ==, 0);
        lrg_video_player_set_audio_enabled (player, TRUE);
        lrg_video_player_play (player);
        lrg_video_player_update (player, 0.1f);
        g_assert_cmpfloat (lrg_video_player_get_audio_time (player), ==, -1.0);
    }

    lrg_video_player_close (player);
    g_assert_false (lrg_video_player_has_audio (player));
}

static void
test_audio_disabled (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    gint16 buf[AUDIO_CHUNK * 2];
    gboolean enabled = TRUE;
    int i;

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    g_object_get (player, "audio-enabled", &enabled, NULL);
    g_assert_false (enabled);
    g_assert_false (lrg_video_player_get_audio_enabled (player));

    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    lrg_video_player_play (player);
    for (i = 0; i < 30; i++)
    {
        lrg_video_player_update (player, 1.0f / 30.0f);
        g_assert_cmpuint (lrg_video_player_read_audio (player, buf,
                                                       AUDIO_CHUNK), ==, 0);
    }
    g_assert_cmpfloat (lrg_video_player_get_audio_time (player), ==, -1.0);
    bytes = lrg_video_player_read_audio_bytes (player, AUDIO_CHUNK);
    g_assert_nonnull (bytes);
    g_assert_cmpuint (g_bytes_get_size (bytes), ==, 0);

    /* The property turns it on. */
    g_object_set (player, "audio-enabled", TRUE, NULL);
    g_assert_true (lrg_video_player_get_audio_enabled (player));
    lrg_video_player_update (player, 1.0f / 30.0f);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);
}

static void
test_audio_pull (void)
{
    PullStats st;
    gdouble seconds;
    gdouble expected = (gdouble) FIXTURE_SECONDS * AUDIO_RATE;

    SKIP_IF_NO_AVI ();

    pull_run (g_avi, 1.0f, FALSE, &st);

    g_assert_true (st.finished);
    /* About the whole clip's worth of audio (MP3 framing adds a little). */
    g_assert_cmpfloat ((gdouble) st.frames, >=, expected * 0.9);
    g_assert_cmpfloat ((gdouble) st.frames, <=, expected * 1.1);
    /* A real tone: lavfi's sine is 1/8 full scale, RMS ~2900. */
    g_assert_false (st.all_zero);
    g_assert_cmpfloat (st.rms, >, 1500.0);
    /* 440 Hz crosses zero 880 times a second. */
    seconds = (gdouble) st.frames / AUDIO_RATE;
    g_assert_cmpfloat ((gdouble) st.crossings / seconds, >=, 2 * AUDIO_TONE * 0.9);
    g_assert_cmpfloat ((gdouble) st.crossings / seconds, <=, 2 * AUDIO_TONE * 1.1);
    g_assert_true (st.lead_ok);
}

static void
test_audio_muted (void)
{
    PullStats st;
    PullStats loud;

    SKIP_IF_NO_AVI ();

    pull_run (g_avi, 1.0f, FALSE, &loud);
    pull_run (g_avi, 1.0f, TRUE, &st);

    /* Muted audio is consumed in step (same frame count) but silent. */
    g_assert_true (st.all_zero);
    g_assert_cmpfloat ((gdouble) st.frames, >=, (gdouble) loud.frames * 0.95);
    g_assert_cmpfloat ((gdouble) st.frames, <=, (gdouble) loud.frames * 1.05);
}

static void
test_audio_volume (void)
{
    PullStats full;
    PullStats half;
    gdouble ratio;

    SKIP_IF_NO_AVI ();

    pull_run (g_avi, 1.0f, FALSE, &full);
    pull_run (g_avi, 0.5f, FALSE, &half);

    g_assert_cmpfloat (full.rms, >, 0.0);
    ratio = half.rms / full.rms;
    g_assert_cmpfloat (ratio, >=, 0.45);
    g_assert_cmpfloat (ratio, <=, 0.55);
}

static void
test_audio_paused (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    gint16 buf[AUDIO_CHUNK * 2];

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi, &error));

    /* Stopped: nothing. */
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      ==, 0);

    lrg_video_player_play (player);
    lrg_video_player_update (player, 0.1f);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);

    lrg_video_player_pause (player);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      ==, 0);

    /* Resuming delivers again. */
    lrg_video_player_play (player);
    lrg_video_player_update (player, 0.1f);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);
}

static void
test_audio_seek (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    gint16 buf[AUDIO_CHUNK * 2];
    gdouble t;

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    lrg_video_player_play (player);
    lrg_video_player_update (player, 0.1f);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);

    /* Forward: audio resumes at the target. */
    lrg_video_player_seek (player, 1.0);
    t = lrg_video_player_get_audio_time (player);
    g_assert_cmpfloat (t, >=, 0.95);
    g_assert_cmpfloat (t, <=, 1.1);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);

    /* Back to the start. */
    lrg_video_player_seek (player, 0.0);
    t = lrg_video_player_get_audio_time (player);
    g_assert_cmpfloat (t, >=, 0.0);
    g_assert_cmpfloat (t, <=, 0.1);

    /* Stop rewinds the audio too. */
    lrg_video_player_seek (player, 1.5);
    lrg_video_player_stop (player);
    t = lrg_video_player_get_audio_time (player);
    g_assert_cmpfloat (t, <=, 0.1);
}

static void
test_audio_catch_up (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    gint16 buf[AUDIO_CHUNK * 2];
    gdouble pos;
    gdouble t;
    int i;

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    lrg_video_player_play (player);

    /* One second of video without pulling any audio. */
    for (i = 0; i < 50; i++)
        lrg_video_player_update (player, 0.02f);
    pos = lrg_video_player_get_position (player);
    g_assert_cmpfloat (pos, >=, 0.95);

    /* The first read drops the stale audio: it delivers audio that is due
     * now, not audio from the start. */
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);
    t = lrg_video_player_get_audio_time (player);
    g_assert_cmpfloat (t, >=, pos - 0.05);
    g_assert_cmpfloat (t, <=, pos + AUDIO_LEAD + 0.05);
}

static void
test_audio_close (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    gint16 buf[AUDIO_CHUNK * 2];

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    lrg_video_player_play (player);
    lrg_video_player_update (player, 0.1f);
    lrg_video_player_close (player);

    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      ==, 0);
    g_assert_cmpfloat (lrg_video_player_get_audio_time (player), ==, -1.0);
    /* The setting survives close. */
    g_assert_true (lrg_video_player_get_audio_enabled (player));
}

static void
test_audio_mono (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    PullStats st;

    SKIP_IF_NO_AVI ();

    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi_mono, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_video_player_has_audio (player));
    g_assert_cmpuint (lrg_video_player_get_audio_channels (player), ==, 1);
    g_assert_cmpuint (lrg_video_player_get_audio_sample_rate (player), ==,
                      AUDIO_RATE);

    /* The GBytes variant: size = frames * channels * 2. */
    lrg_video_player_play (player);
    lrg_video_player_update (player, 0.1f);
    bytes = lrg_video_player_read_audio_bytes (player, 100);
    g_assert_cmpuint (g_bytes_get_size (bytes), ==, 100 * 1 * 2);

    pull_run (g_avi_mono, 1.0f, FALSE, &st);
    g_assert_cmpfloat (st.rms, >, 1500.0);
    g_assert_cmpfloat ((gdouble) st.frames, >=,
                       (gdouble) FIXTURE_SECONDS * AUDIO_RATE * 0.9);
}

static void
test_audio_video_unchanged (void)
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    gint16 buf[AUDIO_CHUNK * 2];
    guint32 h0;
    guint32 h1;
    int i;

    SKIP_IF_NO_AVI ();

    /* With audio enabled and pulled, the picture still advances and the
     * clip still finishes, and looping keeps the audio going. */
    player = lrg_video_player_new ();
    lrg_video_player_set_audio_enabled (player, TRUE);
    g_assert_true (lrg_video_player_open (player, g_avi, &error));
    h0 = frame_hash (player);
    lrg_video_player_set_loop (player, TRUE);
    lrg_video_player_play (player);
    for (i = 0; i < 10; i++)
    {
        lrg_video_player_update (player, 1.0f / FIXTURE_FPS);
        while (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK) > 0)
            ;
    }
    h1 = frame_hash (player);
    g_assert_cmpuint (h0, !=, h1);

    /* Run across the loop point. */
    for (i = 0; i < 3 * FIXTURE_FPS; i++)
    {
        lrg_video_player_update (player, 1.0f / FIXTURE_FPS);
        while (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK) > 0)
            ;
    }
    g_assert_cmpint (lrg_video_player_get_state (player), ==,
                     LRG_VIDEO_STATE_PLAYING);
    lrg_video_player_update (player, 1.0f / FIXTURE_FPS);
    g_assert_cmpuint (lrg_video_player_read_audio (player, buf, AUDIO_CHUNK),
                      >, 0);
}

int
main (int argc, char *argv[])
{
    int ret;

    g_test_init (&argc, &argv, NULL);

    g_fixture_ok = build_fixture ();
    if (g_tmpdir != NULL)
    {
        g_avi = g_build_filename (g_tmpdir, "audio.avi", NULL);
        g_avi_mono = g_build_filename (g_tmpdir, "mono.avi", NULL);
        g_avi_ok = build_avi (g_avi, 2) && build_avi (g_avi_mono, 1);
    }

    g_test_add_func ("/video/player/open-info", test_open_info);
    g_test_add_func ("/video/player/open-missing", test_open_missing);
    g_test_add_func ("/video/player/frame-advances", test_frame_advances);
    g_test_add_func ("/video/player/play-to-end", test_play_to_end);
    g_test_add_func ("/video/player/seek", test_seek);
    g_test_add_func ("/video/player/seek-past-end", test_seek_past_end);
    g_test_add_func ("/video/player/loop", test_loop);
    g_test_add_func ("/video/player/reopen", test_reopen);
    g_test_add_func ("/video/audio/info", test_audio_info);
    g_test_add_func ("/video/audio/disabled", test_audio_disabled);
    g_test_add_func ("/video/audio/pull", test_audio_pull);
    g_test_add_func ("/video/audio/muted", test_audio_muted);
    g_test_add_func ("/video/audio/volume", test_audio_volume);
    g_test_add_func ("/video/audio/paused", test_audio_paused);
    g_test_add_func ("/video/audio/seek", test_audio_seek);
    g_test_add_func ("/video/audio/catch-up", test_audio_catch_up);
    g_test_add_func ("/video/audio/close", test_audio_close);
    g_test_add_func ("/video/audio/mono", test_audio_mono);
    g_test_add_func ("/video/audio/video-unchanged",
                     test_audio_video_unchanged);

    ret = g_test_run ();

    /* Clean up the fixture. */
    if (g_fixture != NULL)
        g_unlink (g_fixture);
    if (g_avi != NULL)
        g_unlink (g_avi);
    if (g_avi_mono != NULL)
        g_unlink (g_avi_mono);
    if (g_tmpdir != NULL)
        g_rmdir (g_tmpdir);
    g_free (g_fixture);
    g_free (g_avi);
    g_free (g_avi_mono);
    g_free (g_tmpdir);

    return ret;
}
