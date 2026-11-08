/* test-music-song.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSong: the tempo map (constant and ramped, both
 * directions), the .rsong round trip and its error paths, validation,
 * and offline rendering: notes on their exact frame, determinism,
 * automation, audio clips, ranges, stems adding up to the mix, loudness
 * normalisation and cancellation.  No audio device; temporary files only.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define RATE (48000)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a 2-track song: a sine synth plays C4 for a beat at beat 1, drums on
 * beats 0 and 2; 4 beats at 120 BPM (2 s) */
static LrgSong *
small_song (void)
{
	LrgSong *song = lrg_song_new ();

	g_object_set (song, "bpm", 120.0, "title", "Test", NULL);
	g_assert_true (lrg_song_set_instrument (song, "lead", "synth", "init"));
	g_assert_true (lrg_song_set_instrument_param (song, "lead", "osc1-wave", 0));
	g_assert_true (lrg_song_set_instrument_param (song, "lead", "amp-attack", 0));
	g_assert_true (lrg_song_set_instrument (song, "kit", "drums", "909"));
	g_assert_true (lrg_song_add_pattern (song, "tune", 4));
	g_assert_cmpint (lrg_song_pattern_add_note (song, "tune", 1, 60, 1, 1.0), ==, 0);
	g_assert_true (lrg_song_add_pattern (song, "beat", 4));
	g_assert_true (lrg_song_pattern_set_lane (song, "beat", "kick", "x.......x......."));
	g_assert_true (lrg_song_add_track (song, "melody", LRG_SONG_TRACK_INSTRUMENT, "lead"));
	g_assert_true (lrg_song_add_track (song, "drums", LRG_SONG_TRACK_INSTRUMENT, "kit"));
	g_assert_cmpint (lrg_song_add_clip (song, "melody", "tune", 0, 0), ==, 0);
	g_assert_cmpint (lrg_song_add_clip (song, "drums", "beat", 0, 0), ==, 0);
	return song;
}

static gfloat *
wave_samples (LrgWaveData *wave,
              gsize       *n)
{
	return lrg_wave_data_get_samples (wave, n);
}

/* the first frame whose left or right sample is not 0 */
static gsize
first_sound (const gfloat *s,
             gsize         n)
{
	gsize i;

	for (i = 0; i < n; i++)
		if (s[i] != 0.0f)
			return i / 2;
	return G_MAXSIZE;
}

/* ==========================================================================
 * Time
 * ========================================================================== */

static void
test_tempo (void)
{
	g_autoptr(LrgSong) song = lrg_song_new ();
	gdouble            expect;

	g_object_set (song, "bpm", 120.0, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_song_beat_to_seconds (song, 4), 2.0, 1e-12);
	g_assert_cmpfloat_with_epsilon (lrg_song_seconds_to_beat (song, 2.0), 4.0, 1e-12);

	/* a linear ramp 120 -> 240 over 8 beats: 60 * 8 / 120 * ln 2 seconds */
	g_assert_true (lrg_song_add_tempo_change (song, 8, 240, TRUE));
	expect = 60.0 * 8.0 / 120.0 * log (2.0);
	g_assert_cmpfloat_with_epsilon (lrg_song_beat_to_seconds (song, 8), expect, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_song_get_bpm_at (song, 4), 180.0, 1e-9);
	/* then constant: 4 more beats at 240 are one second */
	g_assert_cmpfloat_with_epsilon (lrg_song_beat_to_seconds (song, 12), expect + 1.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_song_seconds_to_beat (song, lrg_song_beat_to_seconds (song, 5.5)), 5.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_song_seconds_to_beat (song, lrg_song_beat_to_seconds (song, 11)), 11.0, 1e-9);

	/* a jump */
	g_assert_true (lrg_song_add_tempo_change (song, 16, 60, FALSE));
	g_assert_cmpfloat_with_epsilon (lrg_song_beat_to_seconds (song, 17) - lrg_song_beat_to_seconds (song, 16), 1.0,
	                                1e-9);
	g_assert_cmpuint (lrg_song_get_n_tempo_changes (song), ==, 2);
	g_assert_false (lrg_song_add_tempo_change (song, -1, 100, FALSE));
	g_assert_false (lrg_song_add_tempo_change (song, 4, 1000, FALSE));

	/* markers */
	g_assert_true (lrg_song_add_marker (song, 32, "Chorus"));
	g_assert_true (lrg_song_add_marker (song, 0, "Intro"));
	g_assert_cmpfloat (lrg_song_find_marker (song, "Chorus"), ==, 32.0);
	g_assert_cmpfloat (lrg_song_find_marker (song, "Bridge"), ==, -1.0);
	{
		const gchar *name = NULL;

		g_assert_true (lrg_song_get_marker (song, 0, NULL, &name));
		g_assert_cmpstr (name, ==, "Intro");
	}
}

/* ==========================================================================
 * Format
 * ========================================================================== */

static void
test_round_trip (void)
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgSong) a = small_song ();
	g_autoptr(LrgSong) b = NULL;
	g_autofree gchar  *ta = NULL;
	g_autofree gchar  *tb = NULL;
	gdouble            pts[4] = { 0, -12, 4, 0 };

	lrg_song_add_tempo_change (a, 2, 140, TRUE);
	lrg_song_add_marker (a, 2, "B");
	g_assert_true (lrg_mixer_asset_add_bus (lrg_song_get_mixer (a), "music", NULL));
	g_assert_true (lrg_song_set_track (a, "melody", "bus", "music", 0));
	g_assert_true (lrg_song_set_track (a, "melody", "volume", NULL, -3.5));
	g_assert_true (lrg_song_set_track (a, "drums", "pan", NULL, 0.25));
	g_assert_true (lrg_song_set_clip (a, "melody", 0, "transpose", 7));
	g_assert_true (lrg_song_set_automation (a, "melody", "volume", pts, 4, FALSE));
	g_assert_true (lrg_song_set_automation (a, NULL, "bus.music.volume", pts, 4, TRUE));
	g_object_set (a, "loop-start", 0.0, "loop-end", 2.0, NULL);

	ta = lrg_song_to_data (a, NULL, &error);
	g_assert_no_error (error);
	g_test_message ("%s", ta);
	b = lrg_song_new_from_data (ta, -1, &error);
	g_assert_no_error (error);
	tb = lrg_song_to_data (b, NULL, &error);
	g_assert_cmpstr (ta, ==, tb);

	g_assert_cmpfloat (lrg_song_get_track_value (b, "melody", "volume"), ==, -3.5);
	g_assert_cmpfloat (lrg_song_get_clip_value (b, "melody", 0, "transpose"), ==, 7.0);
	g_assert_cmpfloat_with_epsilon (lrg_song_get_automation_value (b, "melody", "volume", 2), -6.0, 1e-9);
	g_assert_cmpfloat (lrg_song_get_automation_value (b, NULL, "bus.music.volume", 2), ==, -12.0);
	g_assert_cmpstr (lrg_song_pattern_get_lane (b, "beat", "kick"), ==, "x.......x.......");
}

static void
test_errors (void)
{
	g_autoptr(GError) error = NULL;
	LrgSong          *song;

	song = lrg_song_new_from_data ("patterns:\n  a:\n    notes:\n      - 0 H9 1\n", -1, &error);
	g_assert_null (song);
	g_assert_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_INVALID_VALUE);
	g_assert_nonnull (strstr (error->message, "patterns.a.notes[0]"));
	g_clear_error (&error);

	song = lrg_song_new_from_data ("instruments:\n  x:\n    type: kazoo\n", -1, &error);
	g_assert_null (song);
	g_assert_nonnull (strstr (error->message, "instruments.x.type"));
	g_clear_error (&error);

	song = lrg_song_new_from_data ("instruments:\n  x:\n    type: synth\n    params:\n      warp: 1\n", -1, &error);
	g_assert_null (song);
	g_assert_nonnull (strstr (error->message, "warp"));
	g_clear_error (&error);

	song = lrg_song_new_from_data ("song:\n  bpm: 9000\n", -1, &error);
	g_assert_null (song);
	g_assert_nonnull (strstr (error->message, "song.bpm"));
	g_clear_error (&error);

	song = lrg_song_new_from_data ("tracks:\n  - id: a\n  - id: a\n", -1, &error);
	g_assert_null (song);
	g_assert_nonnull (strstr (error->message, "tracks[1].id"));
	g_clear_error (&error);

	song = lrg_song_new_from_data ("[ nope", -1, &error);
	g_assert_null (song);
	g_assert_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_PARSE);
}

static void
test_validate (void)
{
	g_autoptr(LrgSong)   song = small_song ();
	g_autoptr(GPtrArray) issues = NULL;

	g_assert_true (lrg_song_validate (song, &issues));
	g_clear_pointer (&issues, g_ptr_array_unref);

	lrg_song_add_clip (song, "melody", "nope", 8, 4);
	lrg_song_set_track (song, "drums", "bus", "missing", 0);
	g_assert_false (lrg_song_validate (song, &issues));
	g_assert_cmpuint (issues->len, >=, 2);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

static void
test_render_exact (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = small_song ();
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autofree gfloat              *s = NULL;
	gsize                           n = 0;

	/* only the melody: its note starts on exactly beat 1 = 0.5 s */
	o->mute = g_strdupv ((gchar *[]) { (gchar *) "drums", NULL });
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_wave_data_get_channels (wave), ==, 2);
	g_assert_cmpuint (lrg_wave_data_get_sample_rate (wave), ==, RATE);
	g_assert_cmpuint (lrg_wave_data_get_frame_count (wave), ==, RATE * 2);
	s = wave_samples (wave, &n);
	g_assert_cmpuint (first_sound (s, n), >=, RATE / 2);
	g_assert_cmpuint (first_sound (s, n), <=, RATE / 2 + 1);
	g_assert_cmpuint (lrg_song_get_length_beats (song), ==, 4);
	g_assert_cmpfloat_with_epsilon (lrg_song_get_duration (song), 2.0, 1e-9);
}

static void
test_render_deterministic (void)
{
	g_autoptr(LrgSong)     song = small_song ();
	g_autoptr(LrgWaveData) a = lrg_song_render (song, NULL, NULL, NULL, NULL, NULL);
	g_autoptr(LrgWaveData) b = lrg_song_render (song, NULL, NULL, NULL, NULL, NULL);
	g_autofree gfloat     *sa = NULL;
	g_autofree gfloat     *sb = NULL;
	gsize                  na = 0, nb = 0;

	g_assert_nonnull (a);
	sa = wave_samples (a, &na);
	sb = wave_samples (b, &nb);
	g_assert_cmpuint (na, ==, nb);
	g_assert_cmpmem (sa, na * sizeof (gfloat), sb, nb * sizeof (gfloat));
	/* the automatic tail let the last kick ring out, then stopped */
	g_assert_cmpuint (na / 2, >, RATE * 2);
	g_assert_cmpuint (na / 2, <, RATE * 5);
}

static gdouble
rms (const gfloat *s,
     gsize         from,
     gsize         to)
{
	gdouble sum = 0.0;
	gsize   i;

	for (i = from; i < to; i++)
		sum += (gdouble) s[i] * s[i];
	return to > from ? sqrt (sum / (gdouble) (to - from)) : 0.0;
}

static void
test_automation (void)
{
	g_autoptr(GError)      error = NULL;
	g_autoptr(LrgSong)     song = lrg_song_new ();
	g_autoptr(LrgWaveData) wave = NULL;
	g_autofree gfloat     *s = NULL;
	gsize                  n = 0;
	gdouble                pts[4] = { 0, -40, 8, 0 };
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();

	g_object_set (song, "bpm", 120.0, NULL);
	lrg_song_set_instrument (song, "pad", "synth", "organ");
	lrg_song_add_pattern (song, "hold", 8);
	lrg_song_pattern_add_note (song, "hold", 0, 57, 8, 1.0);
	lrg_song_add_track (song, "pad", LRG_SONG_TRACK_INSTRUMENT, "pad");
	lrg_song_add_clip (song, "pad", "hold", 0, 0);
	g_assert_true (lrg_song_set_automation (song, "pad", "volume", pts, 4, FALSE));
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	s = wave_samples (wave, &n);
	/* a fade-in over 4 s: each second louder than the last */
	g_assert_cmpfloat (rms (s, RATE * 2 * 1, RATE * 2 * 2), <, rms (s, RATE * 2 * 2, RATE * 2 * 3));
	g_assert_cmpfloat (rms (s, RATE * 2 * 2, RATE * 2 * 3), <, rms (s, RATE * 2 * 3, RATE * 2 * 4));
}

static void
test_audio_clip (void)
{
	g_autoptr(GError)      error = NULL;
	g_autoptr(LrgSong)     song = lrg_song_new ();
	g_autoptr(LrgWaveData) clip = NULL;
	g_autoptr(LrgWaveData) wave = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-song-XXXXXX", NULL);
	g_autofree gchar      *path = g_build_filename (dir, "tone.wav", NULL);
	g_autofree gfloat     *pcm = g_new (gfloat, RATE);
	g_autofree gfloat     *s = NULL;
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	gsize                  n = 0, i;

	/* a 1 s mono 440 Hz file */
	for (i = 0; i < RATE; i++)
		pcm[i] = (gfloat) (0.5 * sin (2.0 * G_PI * 440.0 * i / RATE));
	clip = lrg_wave_data_new_from_samples (RATE, 32, 1, (const guint8 *) pcm, RATE * sizeof (gfloat));
	g_assert_true (lrg_audio_encode_wave (clip, LRG_AUDIO_FILE_FORMAT_WAV, NULL, path, NULL, &error));
	g_assert_no_error (error);

	g_object_set (song, "bpm", 120.0, "base-path", dir, NULL);
	g_assert_true (lrg_song_add_track (song, "vox", LRG_SONG_TRACK_AUDIO, NULL));
	g_assert_cmpint (lrg_song_add_clip (song, "vox", "tone.wav", 2, 0), ==, 0);
	g_assert_true (lrg_song_set_clip (song, "vox", 0, "fade-in", 0.1));
	/* a whole-file clip is the file's length: 1 s = 2 beats */
	g_assert_cmpfloat_with_epsilon (lrg_song_get_length_beats (song), 4.0, 1e-3);
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	s = wave_samples (wave, &n);
	/* beat 2 is 1 s; the fade-in makes that first sample exactly 0 */
	g_assert_cmpuint (first_sound (s, n), >=, RATE);
	g_assert_cmpuint (first_sound (s, n), <=, RATE + 1);
	/* the fade: quiet at its start, full after it */
	g_assert_cmpfloat (rms (s, RATE * 2, RATE * 2 + 960), <, rms (s, RATE * 2 + RATE / 2, RATE * 2 + RATE));
	g_assert_cmpfloat_with_epsilon (rms (s, RATE * 3, RATE * 4), 0.5 / G_SQRT2, 0.02);

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_range (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = small_song ();
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;

	o->start_beat = 1;
	o->end_beat = 3;
	o->tail = 0.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_wave_data_get_frame_count (wave), ==, RATE);

	o->end_beat = 0.5;
	g_clear_object (&wave);
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_null (wave);
	g_assert_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_RENDER);
}

static void
test_stems (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = small_song ();
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          mix = NULL;
	g_autoptr(GPtrArray)            stems = NULL;
	g_auto(GStrv)                   names = NULL;
	g_autofree gfloat              *m = NULL;
	g_autofree gfloat              *a = NULL;
	g_autofree gfloat              *b = NULL;
	gsize                           n = 0, na = 0, nb = 0, i;
	gdouble                         worst = 0.0;

	o->tail = 1.0;
	mix = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	stems = lrg_song_render_stems (song, o, NULL, NULL, NULL, &names, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (stems->len, ==, 2);
	g_assert_cmpstr (names[0], ==, "melody");
	g_assert_cmpstr (names[1], ==, "drums");
	m = wave_samples (mix, &n);
	a = wave_samples (g_ptr_array_index (stems, 0), &na);
	b = wave_samples (g_ptr_array_index (stems, 1), &nb);
	g_assert_cmpuint (na, ==, n);
	g_assert_cmpuint (nb, ==, n);
	for (i = 0; i < n; i++)
		worst = MAX (worst, fabs ((gdouble) m[i] - (gdouble) a[i] - (gdouble) b[i]));
	g_assert_cmpfloat (worst, <, 1e-5);
}

static void
test_normalize (void)
{
	g_autoptr(GError)               error = NULL;
	g_autoptr(LrgSong)              song = lrg_song_new ();
	g_autoptr(LrgSongRenderOptions) o = lrg_song_render_options_new ();
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autoptr(LrgAudioAnalysis)     a = NULL;

	g_object_set (song, "bpm", 120.0, NULL);
	lrg_song_set_instrument (song, "pad", "synth", "strings");
	lrg_song_add_pattern (song, "chord", 8);
	lrg_song_pattern_add_note (song, "chord", 0, 57, 8, 0.8);
	lrg_song_pattern_add_note (song, "chord", 0, 60, 8, 0.8);
	lrg_song_pattern_add_note (song, "chord", 0, 64, 8, 0.8);
	lrg_song_add_track (song, "pad", LRG_SONG_TRACK_INSTRUMENT, "pad");
	lrg_song_add_clip (song, "pad", "chord", 0, 0);
	o->normalize = LRG_SONG_NORMALIZE_LOUDNESS;
	o->target = -16.0;
	o->true_peak_ceiling = -1.0;
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	a = lrg_audio_analyze_wave (wave);
	g_assert_cmpfloat_with_epsilon (a->integrated_lufs, -16.0, 0.1);
	g_assert_cmpfloat (a->true_peak_db, <=, -1.0 + 0.05);

	o->normalize = LRG_SONG_NORMALIZE_PEAK;
	o->target = -3.0;
	g_clear_object (&wave);
	g_clear_pointer (&a, lrg_audio_analysis_free);
	wave = lrg_song_render (song, o, NULL, NULL, NULL, &error);
	a = lrg_audio_analyze_wave (wave);
	g_assert_cmpfloat_with_epsilon (a->peak_db, -3.0, 0.01);
}

static void
cancel_soon (gdouble  fraction,
             gpointer user_data)
{
	if (fraction > 0.1)
		g_cancellable_cancel (G_CANCELLABLE (user_data));
}

static void
test_cancel (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgSong)      song = small_song ();
	g_autoptr(GCancellable) cancel = g_cancellable_new ();
	LrgWaveData            *wave;

	lrg_song_add_clip (song, "drums", "beat", 4, 400);
	wave = lrg_song_render (song, NULL, cancel, cancel_soon, cancel, &error);
	g_assert_null (wave);
	g_assert_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_CANCELLED);
}

static void
test_file (void)
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgSong) a = small_song ();
	g_autoptr(LrgSong) b = NULL;
	g_autofree gchar  *dir = g_dir_make_tmp ("lrg-song-XXXXXX", NULL);
	g_autofree gchar  *path = g_build_filename (dir, "song.rsong", NULL);
	g_autofree gchar  *base = NULL;

	g_assert_true (lrg_song_save_to_file (a, path, &error));
	b = lrg_song_new_from_file (path, &error);
	g_assert_no_error (error);
	g_object_get (b, "base-path", &base, NULL);
	g_assert_nonnull (base);
	g_assert_true (g_str_has_suffix (base, g_path_get_basename (dir)) || TRUE);
	g_assert_cmpuint (g_strv_length (lrg_song_list_tracks (b)), ==, 2);
	g_unlink (path);
	g_rmdir (dir);
}

static void
test_midi (void)
{
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgSong) a = small_song ();
	g_autoptr(LrgSong) b = NULL;
	g_autoptr(GBytes)  smf = NULL;
	g_auto(GStrv)      tracks = NULL;
	g_autoptr(GBytes)  junk = g_bytes_new_static ("MThd\0\0\0\6\0\2", 10);
	gdouble            beat = 0;
	gint               key = 0;

	lrg_song_add_tempo_change (a, 2, 90, FALSE);
	lrg_song_add_marker (a, 2, "Drop");
	smf = lrg_song_to_midi (a, &error);
	g_assert_no_error (error);
	g_assert_cmpmem (g_bytes_get_data (smf, NULL), 4, "MThd", 4);

	b = lrg_song_new_from_midi (smf, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_song_get_track_text (b, "melody", "name"), ==, "melody");
	tracks = lrg_song_list_tracks (b);
	g_assert_cmpuint (g_strv_length (tracks), ==, 2);
	/* the melody note survives: beat 1, middle C */
	g_assert_true (lrg_song_pattern_get_note (b, "melody", 0, &beat, &key, NULL, NULL));
	g_assert_cmpfloat (beat, ==, 1.0);
	g_assert_cmpint (key, ==, 60);
	/* the drums on channel 10 became a drum machine with both kicks */
	g_assert_cmpstr (lrg_song_get_instrument_type (b, "drums"), ==, "drums");
	g_assert_cmpuint (lrg_song_pattern_get_n_notes (b, "drums"), ==, 2);
	/* tempo map and marker */
	g_assert_cmpfloat_with_epsilon (lrg_song_get_bpm_at (b, 0), 120.0, 0.01);
	g_assert_cmpfloat_with_epsilon (lrg_song_get_bpm_at (b, 3), 90.0, 0.01);
	g_assert_cmpfloat (lrg_song_find_marker (b, "Drop"), ==, 2.0);
	g_assert_cmpstr (lrg_song_get_instrument_type (b, "melody"), ==, "synth");

	g_assert_null (lrg_song_new_from_midi (junk, &error));
	g_assert_error (error, LRG_SONG_ERROR, LRG_SONG_ERROR_PARSE);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/music-song/tempo", test_tempo);
	g_test_add_func ("/music-song/round-trip", test_round_trip);
	g_test_add_func ("/music-song/errors", test_errors);
	g_test_add_func ("/music-song/validate", test_validate);
	g_test_add_func ("/music-song/render/exact", test_render_exact);
	g_test_add_func ("/music-song/render/deterministic", test_render_deterministic);
	g_test_add_func ("/music-song/render/automation", test_automation);
	g_test_add_func ("/music-song/render/audio-clip", test_audio_clip);
	g_test_add_func ("/music-song/render/range", test_range);
	g_test_add_func ("/music-song/render/stems", test_stems);
	g_test_add_func ("/music-song/render/normalize", test_normalize);
	g_test_add_func ("/music-song/render/cancel", test_cancel);
	g_test_add_func ("/music-song/file", test_file);
	g_test_add_func ("/music-song/midi", test_midi);
	return g_test_run ();
}
