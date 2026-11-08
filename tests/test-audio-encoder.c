/* test-audio-encoder.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAudioAnalysis (levels, BS.1770 loudness, true peak)
 * and the audio encoder/decoder: format names, native WAV/AIFF round
 * trips, and every FFmpeg format that has a backend here (skipped
 * otherwise): encode a stereo tone, decode it, compare length and level.
 */

#include <math.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define RATE (48000)

/* @seconds of a stereo sine at @hz and @db dBFS peak */
static gfloat *
tone (gdouble hz,
      gdouble db,
      gdouble seconds,
      gsize  *n)
{
	gsize   frames = (gsize) (seconds * RATE);
	gfloat *s = g_new (gfloat, frames * 2);
	gdouble a = pow (10.0, db / 20.0);
	gsize   i;

	for (i = 0; i < frames; i++)
		s[i * 2] = s[i * 2 + 1] = (gfloat) (a * sin (2.0 * G_PI * hz * i / RATE));
	*n = frames * 2;
	return s;
}

/* ==========================================================================
 * Analysis
 * ========================================================================== */

static void
test_analysis (void)
{
	g_autofree gfloat           *s = NULL;
	g_autoptr(LrgAudioAnalysis)  a = NULL;
	g_autofree gfloat           *quiet = g_new0 (gfloat, RATE * 2);
	gsize                        n = 0;

	/* EBU: a stereo 1 kHz sine at -20 dBFS reads -20.0 LUFS */
	s = tone (1000.0, -20.0, 10.0, &n);
	a = lrg_audio_analyze (s, n, 2, RATE);
	g_assert_nonnull (a);
	g_assert_cmpfloat_with_epsilon (a->integrated_lufs, -20.0, 0.1);
	g_assert_cmpfloat_with_epsilon (a->short_term_max_lufs, -20.0, 0.1);
	g_assert_cmpfloat_with_epsilon (a->peak_db, -20.0, 0.01);
	g_assert_cmpfloat_with_epsilon (a->rms_db, -23.01, 0.05);
	g_assert_cmpfloat (a->true_peak_db, >=, a->peak_db);
	g_assert_cmpfloat (a->loudness_range, <, 0.5);
	g_assert_cmpfloat_with_epsilon (a->duration, 10.0, 1e-9);
	g_assert_cmpuint (a->clipped_samples, ==, 0);

	/* the gain to reach -14 LUFS is +6 dB */
	g_assert_cmpfloat_with_epsilon (20.0 * log10 (lrg_audio_analysis_normalize_gain (a, TRUE, -14.0, 0.0)), 6.0, 0.15);
	/* a ceiling of -18 dBTP limits it */
	g_assert_cmpfloat (20.0 * log10 (lrg_audio_analysis_normalize_gain (a, TRUE, -14.0, -18.0)), <, 2.1);

	/* silence */
	g_clear_pointer (&a, lrg_audio_analysis_free);
	a = lrg_audio_analyze (quiet, RATE * 2, 2, RATE);
	g_assert_cmpfloat (a->integrated_lufs, ==, LRG_AUDIO_LOUDNESS_SILENCE);
	g_assert_cmpfloat (a->peak_db, ==, LRG_AUDIO_LOUDNESS_SILENCE);
	g_assert_cmpfloat (lrg_audio_analysis_normalize_gain (a, TRUE, -14.0, -1.0), ==, 1.0);

	/* bad layouts */
	g_assert_null (lrg_audio_analyze (quiet, 3, 2, RATE));
	g_assert_null (lrg_audio_analyze (quiet, 2, 0, RATE));
}

static void
test_true_peak (void)
{
	gfloat                      s[RATE];
	g_autoptr(LrgAudioAnalysis) a = NULL;
	guint                       i;

	/* a sine at fs/4 sampled 45 degrees off its peaks: every sample is
	 * 0.707 of the amplitude, the true peak is the amplitude (+3 dB) */
	for (i = 0; i < RATE; i++)
		s[i] = (gfloat) (0.5 * sin (2.0 * G_PI * 0.25 * i + G_PI / 4.0));
	a = lrg_audio_analyze (s, RATE, 1, RATE);
	g_assert_cmpfloat_with_epsilon (a->peak_db, 20.0 * log10 (0.5 * G_SQRT2 / 2.0), 0.01);
	g_assert_cmpfloat_with_epsilon (a->true_peak_db, 20.0 * log10 (0.5), 0.5);
}

/* ==========================================================================
 * Formats
 * ========================================================================== */

static void
test_names (void)
{
	LrgAudioFileFormat f;

	g_assert_true (lrg_audio_file_format_from_path ("x/song.FLAC", &f));
	g_assert_cmpint (f, ==, LRG_AUDIO_FILE_FORMAT_FLAC);
	g_assert_true (lrg_audio_file_format_from_path ("a.alac.m4a", &f));
	g_assert_cmpint (f, ==, LRG_AUDIO_FILE_FORMAT_ALAC);
	g_assert_true (lrg_audio_file_format_from_path ("a.m4a", &f));
	g_assert_cmpint (f, ==, LRG_AUDIO_FILE_FORMAT_AAC);
	g_assert_true (lrg_audio_file_format_from_path ("a.aif", &f));
	g_assert_cmpint (f, ==, LRG_AUDIO_FILE_FORMAT_AIFF);
	g_assert_false (lrg_audio_file_format_from_path ("a.docx", &f));
	g_assert_false (lrg_audio_file_format_from_path ("noext", &f));
	g_assert_true (lrg_audio_file_format_from_name ("wv", &f));
	g_assert_cmpint (f, ==, LRG_AUDIO_FILE_FORMAT_WAVPACK);
	g_assert_cmpstr (lrg_audio_file_format_get_extension (LRG_AUDIO_FILE_FORMAT_OPUS), ==, ".opus");
	g_assert_true (lrg_audio_file_format_is_lossless (LRG_AUDIO_FILE_FORMAT_FLAC));
	g_assert_false (lrg_audio_file_format_is_lossless (LRG_AUDIO_FILE_FORMAT_MP3));
	g_assert_cmpint (lrg_audio_encoder_get_backend (LRG_AUDIO_FILE_FORMAT_WAV), ==, LRG_AUDIO_ENCODER_BACKEND_NATIVE);
}

/* encodes a 1 s tone as @format, decodes it and compares */
static void
round_trip (LrgAudioFileFormat format,
            guint              bits,
            gdouble            tolerance)
{
	g_autoptr(GError)                error = NULL;
	g_autoptr(LrgAudioEncodeOptions) o = lrg_audio_encode_options_new ();
	g_autofree gchar                *dir = g_dir_make_tmp ("lrg-enc-XXXXXX", NULL);
	g_autofree gchar                *name = g_strconcat ("tone", lrg_audio_file_format_get_extension (format), NULL);
	g_autofree gchar                *path = g_build_filename (dir, name, NULL);
	g_autofree gfloat               *s = NULL;
	g_autoptr(LrgWaveData)           back = NULL;
	g_autoptr(LrgAudioAnalysis)      in = NULL;
	g_autoptr(LrgAudioAnalysis)      out = NULL;
	gsize                            n = 0;
	gdouble                          seconds;

	if (lrg_audio_encoder_get_backend (format) == LRG_AUDIO_ENCODER_BACKEND_NONE)
	{
		g_test_message ("no backend for %s", name);
		g_rmdir (dir);
		return;
	}
	s = tone (440.0, -6.0, 1.0, &n);
	o->bit_depth = bits;
	o->title = g_strdup ("Tone");
	o->artist = g_strdup ("libregnum");
	g_assert_true (lrg_audio_encode_samples (s, n, 2, RATE, format, o, path, NULL, &error));
	g_assert_no_error (error);
	back = lrg_audio_decode_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);
	seconds = (gdouble) lrg_wave_data_get_frame_count (back) / lrg_wave_data_get_sample_rate (back);
	g_test_message ("%s: %u Hz, %u ch, %.4f s", name, lrg_wave_data_get_sample_rate (back),
	                lrg_wave_data_get_channels (back), seconds);
	/* lossy codecs add up to a frame or two of priming */
	g_assert_cmpfloat_with_epsilon (seconds, 1.0, lrg_audio_file_format_is_lossless (format) ? 1e-6 : 0.12);
	in = lrg_audio_analyze (s, n, 2, RATE);
	out = lrg_audio_analyze_wave (back);
	g_assert_cmpfloat_with_epsilon (out->rms_db, in->rms_db, tolerance);
	g_unlink (path);
	g_rmdir (dir);
}

static void
test_native (void)
{
	round_trip (LRG_AUDIO_FILE_FORMAT_WAV, 16, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_WAV, 24, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_WAV, 32, 0.001);
}

static void
test_aiff (void)
{
	if (lrg_audio_encoder_get_backend (LRG_AUDIO_FILE_FORMAT_FLAC) == LRG_AUDIO_ENCODER_BACKEND_NONE)
	{
		g_test_skip ("reading AIFF needs FFmpeg");
		return;
	}
	round_trip (LRG_AUDIO_FILE_FORMAT_AIFF, 16, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_AIFF, 24, 0.01);
}

static void
test_ffmpeg_formats (void)
{
	if (lrg_audio_encoder_get_backend (LRG_AUDIO_FILE_FORMAT_FLAC) == LRG_AUDIO_ENCODER_BACKEND_NONE)
	{
		g_test_skip ("no FFmpeg here");
		return;
	}
	round_trip (LRG_AUDIO_FILE_FORMAT_FLAC, 16, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_FLAC, 24, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_MP3, 0, 1.0);
	round_trip (LRG_AUDIO_FILE_FORMAT_OGG, 0, 1.0);
	round_trip (LRG_AUDIO_FILE_FORMAT_OPUS, 0, 1.0);
	round_trip (LRG_AUDIO_FILE_FORMAT_AAC, 0, 1.0);
	round_trip (LRG_AUDIO_FILE_FORMAT_ALAC, 16, 0.01);
	round_trip (LRG_AUDIO_FILE_FORMAT_WAVPACK, 16, 0.01);
}

static void
test_errors (void)
{
	g_autoptr(GError) error = NULL;
	gfloat            s[4] = { 0 };

	g_assert_false (lrg_audio_encode_samples (s, 3, 2, RATE, LRG_AUDIO_FILE_FORMAT_WAV, NULL, "/tmp/x.wav", NULL,
	                                          &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_null (lrg_audio_decode_file ("/nonexistent/file.flac", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/audio-encoder/analysis", test_analysis);
	g_test_add_func ("/audio-encoder/true-peak", test_true_peak);
	g_test_add_func ("/audio-encoder/names", test_names);
	g_test_add_func ("/audio-encoder/native", test_native);
	g_test_add_func ("/audio-encoder/aiff", test_aiff);
	g_test_add_func ("/audio-encoder/ffmpeg-formats", test_ffmpeg_formats);
	g_test_add_func ("/audio-encoder/errors", test_errors);
	return g_test_run ();
}
