/* test-music-demo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The committed demo song (examples/data/music/neon-drive.rsong, written
 * by examples/synthwave-song.c) loads, validates, writes back to exactly
 * the same text, and its chorus renders: every track, the foley impact
 * and the mixer layout work from the file alone.
 */

#include <math.h>
#include <glib.h>
#include <libregnum.h>

static gchar *
demo_path (void)
{
	return g_test_build_filename (G_TEST_DIST, "..", "examples", "data", "music", "neon-drive.rsong", NULL);
}

static void
test_load (void)
{
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *path = demo_path ();
	g_autofree gchar    *text = NULL;
	g_autofree gchar    *again = NULL;
	g_autoptr(LrgSong)   song = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_auto(GStrv)        tracks = NULL;

	song = lrg_song_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_song_validate (song, &issues));
	tracks = lrg_song_list_tracks (song);
	g_assert_cmpuint (g_strv_length (tracks), ==, 10);
	g_assert_cmpfloat_with_epsilon (lrg_song_get_duration (song), 160.0, 1e-6);
	g_assert_cmpfloat (lrg_song_find_marker (song, "Chorus"), ==, 128.0);

	/* the file is canonical: written again, byte for byte the same */
	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	again = lrg_song_to_data (song, NULL, &error);
	g_assert_cmpstr (again, ==, text);
}

static void
test_render_chorus (void)
{
	g_autoptr(GError)               error = NULL;
	g_autofree gchar               *path = demo_path ();
	g_autoptr(LrgSong)              song = lrg_song_new_from_file (path, NULL);
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autoptr(LrgAudioAnalysis)     a = NULL;

	g_assert_nonnull (song);
	o->start_beat = 128;
	o->end_beat = 136;
	o->preroll = FALSE;
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	a = lrg_audio_analyze_wave (wave);
	g_assert_cmpfloat_with_epsilon (a->duration, 8.0 * 60.0 / 108.0, 1e-3);
	g_assert_cmpfloat (a->integrated_lufs, >, -30.0);
	g_assert_cmpfloat (a->peak_db, <, 0.5);
	g_assert_cmpuint (a->clipped_samples, ==, 0);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/music-demo/load", test_load);
	g_test_add_func ("/music-demo/render-chorus", test_render_chorus);
	return g_test_run ();
}
