/* test-music-import.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Recordings into songs (0.10.2, lrg-song-import.h).  The recordings are
 * songs with a known answer, made and rendered here: tempo and phase of a
 * click track; a 110 BPM groove rendered at 44.1 kHz with 0.4 s of
 * silence in front imported at 48 kHz (tempo, bar alignment, the
 * resampled WAV, the re-render matching the original, the key, the
 * transcribed kick and bass); sections from an arrangement change; and
 * the errors.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define PAD_SEC (0.4)

static gchar *tmp_dir = NULL;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* clicks (short noise bursts) every beat from @offset seconds */
static gfloat *
click_track (gdouble  bpm,
             gdouble  offset,
             gdouble  seconds,
             guint    rate,
             gsize   *n)
{
	gsize   frames = (gsize) (seconds * rate), i;
	gfloat *x = g_new0 (gfloat, frames);
	guint32 seed = 1;
	gdouble t;

	for (t = offset; t < seconds; t += 60.0 / bpm)
	{
		gsize start = (gsize) (t * rate);

		for (i = 0; i < rate / 100 && start + i < frames; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			x[start + i] = (gfloat) (((gint32) (seed >> 8) % 2000) / 2000.0) * (gfloat) exp (-(gdouble) i / (rate / 400));
		}
	}
	*n = frames;
	return x;
}

/*
 * groove_song:
 * 110 BPM in A minor: four-on-the-floor kick, backbeat snare, off-beat hat,
 * an octave bass and a pad on Am F C G, @bars bars; with @quiet_bars > 0
 * the first bars are the pad alone (for sections).
 */
static LrgSong *
groove_song (guint bars,
             guint quiet_bars)
{
	static const gchar *roots[4] = { "A1", "F1", "C2", "G1" };
	static const gchar *chords[4][3] = { { "A3", "C4", "E4" }, { "F3", "A3", "C4" }, { "C4", "E4", "G4" },
	                                     { "G3", "B3", "D4" } };
	LrgSong *song = lrg_song_new ();
	guint    i, j;

	g_object_set (song, "bpm", 110.0, "length", (gdouble) bars * 4, NULL);
	g_assert_true (lrg_song_set_instrument (song, "kit", "drums", "808"));
	g_assert_true (lrg_song_set_instrument (song, "bass", "synth", "synthwave-bass"));
	g_assert_true (lrg_song_set_instrument (song, "pad", "synth", "synthwave-pad"));
	g_assert_true (lrg_song_add_pattern (song, "beat", 4));
	g_assert_true (lrg_song_pattern_set_lane (song, "beat", "kick", "X...x...x...x..."));
	g_assert_true (lrg_song_pattern_set_lane (song, "beat", "snare", "....x.......x..."));
	g_assert_true (lrg_song_pattern_set_lane (song, "beat", "closed-hat", "..x...x...x...x."));
	g_assert_true (lrg_song_add_pattern (song, "bass", 16));
	g_assert_true (lrg_song_add_pattern (song, "pad", 16));
	for (i = 0; i < 4; i++)
	{
		gint r = lrg_music_key_from_name (roots[i]);

		for (j = 0; j < 8; j++)
			lrg_song_pattern_add_note (song, "bass", i * 4 + j * 0.5, r + (j % 2 ? 12 : 0), 0.42, j % 2 ? 0.7 : 0.9);
		for (j = 0; j < 3; j++)
			lrg_song_pattern_add_note (song, "pad", i * 4, lrg_music_key_from_name (chords[i][j]), 3.9, 0.6);
	}
	g_assert_true (lrg_song_add_track (song, "pad", LRG_SONG_TRACK_INSTRUMENT, "pad"));
	g_assert_cmpint (lrg_song_add_clip (song, "pad", "pad", 0, bars * 4), >=, 0);
	g_assert_true (lrg_song_add_track (song, "drums", LRG_SONG_TRACK_INSTRUMENT, "kit"));
	g_assert_cmpint (lrg_song_add_clip (song, "drums", "beat", quiet_bars * 4, (bars - quiet_bars) * 4), >=, 0);
	g_assert_true (lrg_song_add_track (song, "bass", LRG_SONG_TRACK_INSTRUMENT, "bass"));
	g_assert_cmpint (lrg_song_add_clip (song, "bass", "bass", quiet_bars * 4, (bars - quiet_bars) * 4), >=, 0);
	return song;
}

/* renders @song at @rate, puts PAD_SEC of silence in front and writes it */
static gchar *
render_to_wav (LrgSong     *song,
               guint        rate,
               gdouble      pad,
               const gchar *name)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autofree gfloat              *pcm = NULL;
	g_autofree gfloat              *padded = NULL;
	gsize                           n = 0, lead = (gsize) (pad * rate) * 2;
	gchar                          *path = g_build_filename (tmp_dir, name, NULL);

	o->sample_rate = rate;
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	pcm = lrg_wave_data_get_samples (wave, &n);
	padded = g_new0 (gfloat, lead + n);
	memcpy (padded + lead, pcm, n * sizeof (gfloat));
	g_assert_true (lrg_audio_encode_samples (padded, lead + n, 2, rate, LRG_AUDIO_FILE_FORMAT_WAV, NULL, path, NULL,
	                                         &error));
	g_assert_no_error (error);
	return path;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void
test_detect_tempo (void)
{
	gsize              n = 0;
	g_autofree gfloat *x = click_track (120.0, 0.25, 20.0, 44100, &n);
	gdouble            bpm = 0, first = -1, conf = 0, beat = 0.5;

	g_assert_true (lrg_music_detect_tempo (x, n, 1, 44100, 0, 0, &bpm, &first, &conf));
	g_assert_cmpfloat_with_epsilon (bpm, 120.0, 0.3);
	/* the first beat lands on a click (modulo the beat) */
	g_assert_cmpfloat (fabs (fmod (first - 0.25 + 10.0 * beat, beat)) < 0.03
	                   || fabs (fmod (first - 0.25 + 10.0 * beat, beat) - beat) < 0.03, ==, TRUE);
	g_assert_cmpfloat (conf, >, 0.5);

	/* too short to go on */
	g_assert_false (lrg_music_detect_tempo (x, 44100, 1, 44100, 0, 0, &bpm, NULL, NULL));
}

static void
test_import_groove (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              source = groove_song (16, 0);
	g_autofree gchar               *wav = render_to_wav (source, 44100, PAD_SEC, "groove.wav");
	g_autofree gchar               *out = g_build_filename (tmp_dir, "groove-song", NULL);
	g_autoptr(LrgSongImportOptions) opts = lrg_song_import_options_new ();
	g_autoptr(LrgSongImportReport)  r = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autoptr(GPtrArray)            issues = NULL;
	g_autoptr(LrgWaveData)          aligned = NULL;
	g_autoptr(LrgWaveData)          rendered = NULL;
	g_autoptr(LrgSongRenderOptions) ro = lrg_song_render_options_new ();
	g_autofree gchar               *aligned_path = NULL;
	gdouble                         bar = 4.0 * 60.0 / 110.0, rem;
	guint                           i, kick_bars = 0, bass_ok = 0, bass_total = 0;
	gint                            muted;

	opts->transcribe = TRUE;
	opts->sample_rate = 48000;
	song = lrg_song_import_audio (wav, out, opts, &r, &error);
	g_assert_no_error (error);
	g_assert_nonnull (song);

	/* tempo, and a bar that starts at beat 0 */
	g_assert_true (r->bpm_detected);
	g_assert_cmpfloat_with_epsilon (r->bpm, 110.0, 0.3);
	g_assert_cmpuint (r->source_rate, ==, 44100);
	rem = fmod (r->lead_in + PAD_SEC, bar);
	g_assert_true (rem < 0.04 || rem > bar - 0.04);
	g_assert_cmpstr (r->key, ==, "A minor");

	/* the aligned WAV: 48 kHz, the lead-in, then the whole recording */
	aligned_path = g_build_filename (out, r->audio_file, NULL);
	aligned = lrg_wave_data_new_from_file (aligned_path, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_wave_data_get_sample_rate (aligned), ==, 48000);
	g_assert_cmpfloat_with_epsilon (lrg_wave_data_get_duration (aligned), r->lead_in + r->duration, 0.01);

	/* the song validates, its transcription tracks are muted */
	g_assert_true (lrg_song_validate (song, &issues));
	muted = 0;
	{
		g_auto(GStrv) ids = lrg_song_list_tracks (song);

		for (i = 0; ids[i] != NULL; i++)
			if (lrg_song_get_track_value (song, ids[i], "mute") != 0.0)
				muted++;
		g_assert_cmpstr (ids[0], ==, "original");
	}
	g_assert_cmpint (muted, ==, 4);

	/* rendering the song plays the recording back: as loud, same length */
	ro->tail = 0.0;
	rendered = lrg_song_render (song, ro, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	{
		g_autoptr(LrgAudioAnalysis) a = lrg_audio_analyze_wave (rendered);
		g_autoptr(LrgAudioAnalysis) b = lrg_audio_analyze_wave (aligned);

		g_assert_cmpfloat_with_epsilon (a->integrated_lufs, b->integrated_lufs, 0.3);
	}

	/* transcription: the kick on every beat, the bass on A F C G */
	for (i = 0; i < 16; i++)
	{
		gint         clip = lrg_song_find_clip (song, "drums", i * 4.0 + 0.01);
		const gchar *pat = NULL;
		gdouble      start = 0, len = 0;
		const gchar *lane;

		if (clip < 0 || !lrg_song_get_clip (song, "drums", (guint) clip, &pat, &start, &len))
			continue;
		lane = lrg_song_pattern_get_lane (song, pat, "kick");
		if (lane != NULL && strlen (lane) >= 16 && lane[0] != '.' && lane[4] != '.' && lane[8] != '.' && lane[12] != '.')
			kick_bars++;
	}
	g_assert_cmpuint (kick_bars, >=, 12);
	{
		g_auto(GStrv) pats = lrg_song_list_patterns (song);

		for (i = 0; pats[i] != NULL; i++)
		{
			guint j, n;

			if (!g_str_has_prefix (pats[i], "bass-"))
				continue;
			n = lrg_song_pattern_get_n_notes (song, pats[i]);
			for (j = 0; j < n; j++)
			{
				gdouble b, l, v;
				gint    k = 0, pc;

				lrg_song_pattern_get_note (song, pats[i], j, &b, &k, &l, &v);
				pc = k % 12;
				bass_total++;
				if (pc == 9 || pc == 5 || pc == 0 || pc == 7)
					bass_ok++;
			}
		}
	}
	g_assert_cmpuint (bass_total, >, 0);
	g_assert_cmpfloat ((gdouble) bass_ok / bass_total, >=, 0.7);
	g_assert_cmpuint (r->drum_hits, >, 0);
	g_assert_cmpuint (r->chords, >, 0);

	/* and it saves and loads again */
	{
		g_autofree gchar   *path = g_build_filename (out, "groove.rsong", NULL);
		g_autoptr(LrgSong)  back = NULL;

		g_assert_true (lrg_song_save_to_file (song, path, &error));
		back = lrg_song_new_from_file (path, &error);
		g_assert_no_error (error);
		g_assert_cmpfloat_with_epsilon (lrg_song_get_duration (back), lrg_song_get_duration (song), 1e-6);
	}
}

/* a given tempo is used as is; the pad-only intro is its own section */
static void
test_sections_and_given_tempo (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              source = groove_song (16, 8);
	g_autofree gchar               *wav = render_to_wav (source, 48000, 0.0, "sections.wav");
	g_autofree gchar               *out = g_build_filename (tmp_dir, "sections-song", NULL);
	g_autoptr(LrgSongImportOptions) opts = lrg_song_import_options_new ();
	g_autoptr(LrgSongImportReport)  r = NULL;
	g_autoptr(LrgSong)              song = NULL;
	guint                           i;
	gboolean                        found = FALSE;

	opts->bpm = 110.0;
	song = lrg_song_import_audio (wav, out, opts, &r, &error);
	g_assert_no_error (error);
	g_assert_false (r->bpm_detected);
	g_assert_cmpfloat (r->bpm, ==, 110.0);
	g_assert_cmpuint (r->sections, >=, 2);
	for (i = 0; i < lrg_song_get_n_markers (song); i++)
	{
		gdouble      beat = 0;
		const gchar *name = NULL;

		lrg_song_get_marker (song, i, &beat, &name);
		if (fabs (beat - 32.0) <= 4.0)
			found = TRUE;
	}
	g_assert_true (found);
}

static void
test_errors (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autofree gchar               *missing = g_build_filename (tmp_dir, "nope.wav", NULL);
	g_autofree gchar               *silent = g_build_filename (tmp_dir, "silent.wav", NULL);
	g_autofree gfloat              *zeros = g_new0 (gfloat, 48000 * 6);
	g_autoptr(LrgSongImportOptions) opts = lrg_song_import_options_new ();

	song = lrg_song_import_audio (missing, tmp_dir, NULL, NULL, &error);
	g_assert_null (song);
	g_assert_nonnull (error);
	g_clear_error (&error);

	/* silence has no tempo; with one given it imports */
	g_assert_true (lrg_audio_encode_samples (zeros, 48000 * 6, 1, 48000, LRG_AUDIO_FILE_FORMAT_WAV, NULL, silent, NULL,
	                                         &error));
	song = lrg_song_import_audio (silent, tmp_dir, NULL, NULL, &error);
	g_assert_null (song);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_clear_error (&error);
	opts->bpm = 90.0;
	song = lrg_song_import_audio (silent, tmp_dir, opts, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (song);
}

static void
rm_rf (const gchar *path)
{
	g_autoptr(GDir) d = g_dir_open (path, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child = g_build_filename (path, name, NULL);

		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			rm_rf (child);
		else
			g_unlink (child);
	}
	g_rmdir (path);
}

int
main (int    argc,
      char **argv)
{
	int status;

	g_test_init (&argc, &argv, NULL);
	tmp_dir = g_dir_make_tmp ("lrg-import-XXXXXX", NULL);

	g_test_add_func ("/music-import/detect-tempo", test_detect_tempo);
	g_test_add_func ("/music-import/groove", test_import_groove);
	g_test_add_func ("/music-import/sections-and-given-tempo", test_sections_and_given_tempo);
	g_test_add_func ("/music-import/errors", test_errors);
	status = g_test_run ();
	rm_rf (tmp_dir);
	g_free (tmp_dir);
	return status;
}
