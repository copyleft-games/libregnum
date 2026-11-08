/* test-music-demo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The committed demo song (examples/data/music/neon-drive.rsong, written
 * by examples/synthwave-song.c) loads, validates, writes back to exactly
 * the same text, and its chorus renders: every track, the foley impact
 * and the mixer layout work from the file alone.  Every other song in
 * that folder (0.10.1, see its 00_index.org) does the same: no warnings,
 * canonical text, the length its index states, and eight bars from a
 * section marker render with sound and without clipping.
 */

#include <math.h>
#include <string.h>
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

typedef struct
{
	const gchar *file;
	gdouble      seconds;
	const gchar *marker;
} SongCase;

static const SongCase songs[] = {
	{ "neon-drive.rsong", 160.0, "Chorus" },
	{ "thunder-grid.rsong", 488.0 * 60.0 / 118.0, "Chorus" },
	{ "steel-horizon.rsong", 192.0, "Overdrive" },
	{ "moonlit-shoreline.rsong", 432.0 * 60.0 / 82.0, "Melody" },
	{ "glass-arcology.rsong", 360.0, "Garden" },
	{ "night-runner.rsong", 150.0, "Final Chorus" },
	{ "iron-choir.rsong", 216.0, "Chorus" },
	{ "rust-belt-boogie.rsong", 180.0, "Guitar Solo" },
	{ "black-monolith.rsong", 240.0, "Riff 2" },
};

static void
test_song (gconstpointer data)
{
	const SongCase                 *c = data;
	g_autoptr(GError)               error = NULL;
	g_autofree gchar               *path = g_test_build_filename (G_TEST_DIST, "..", "examples", "data", "music",
	                                                              c->file, NULL);
	g_autofree gchar               *text = NULL;
	g_autofree gchar               *again = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autoptr(GPtrArray)            issues = NULL;
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autoptr(LrgAudioAnalysis)     a = NULL;
	gdouble                         at;

	song = lrg_song_new_from_file (path, &error);
	g_assert_no_error (error);

	/* valid, without even a warning, and canonical */
	g_assert_true (lrg_song_validate (song, &issues));
	g_assert_cmpuint (issues != NULL ? issues->len : 0, ==, 0);
	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	again = lrg_song_to_data (song, NULL, &error);
	g_assert_cmpstr (again, ==, text);

	/* the length the index states: two to six minutes */
	g_assert_cmpfloat_with_epsilon (lrg_song_get_duration (song), c->seconds, 1e-3);
	g_assert_cmpfloat (c->seconds, >=, 120.0);
	g_assert_cmpfloat (c->seconds, <=, 360.0);

	/* eight bars of the named section sound and do not clip */
	at = lrg_song_find_marker (song, c->marker);
	g_assert_cmpfloat (at, >=, 0.0);
	o->start_beat = at;
	o->end_beat = at + 32.0;
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	a = lrg_audio_analyze_wave (wave);
	g_assert_cmpfloat (a->integrated_lufs, >, -32.0);
	g_assert_cmpuint (a->clipped_samples, ==, 0);
}

int
main (int    argc,
      char **argv)
{
	guint i;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/music-demo/load", test_load);
	g_test_add_func ("/music-demo/render-chorus", test_render_chorus);
	for (i = 0; i < G_N_ELEMENTS (songs); i++)
	{
		g_autofree gchar *name = g_strndup (songs[i].file, strlen (songs[i].file) - 6);
		g_autofree gchar *test = g_strconcat ("/music-demo/songs/", name, NULL);

		g_test_add_data_func (test, &songs[i], test_song);
	}
	return g_test_run ();
}
