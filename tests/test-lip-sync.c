/* test-lip-sync.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the 2D face tools: the standard shape sheet, LrgLipSync
 * (ARPAbet -> viseme map, YAML and CSV loaders with their errors, viseme
 * keys with cross-fades, the audio loudness fallback).  Everything runs
 * headless.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-3f)

/* ==========================================================================
 * Shape sheet
 * ========================================================================== */

static void
test_shape_sheet (void)
{
	const gchar * const *all = lrg_shape_sheet_list_all ();
	const gchar * const *mouth = lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_MOUTH);
	LrgShapeCategory     cat;
	LrgViseme            v;
	guint                total = 0;
	guint                c;
	guint                i;

	/* the sheet of the issue: 11 visemes, 8 eye, 4 brow, 5 mood, 3 body */
	g_assert_cmpuint (g_strv_length ((gchar **)all), ==, 31);
	g_assert_cmpuint (g_strv_length ((gchar **)mouth), ==, 11);
	g_assert_cmpuint (g_strv_length ((gchar **)lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_EYES)), ==, 8);
	g_assert_cmpuint (g_strv_length ((gchar **)lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_BROWS)), ==, 4);
	g_assert_cmpuint (g_strv_length ((gchar **)lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_MOOD)), ==, 5);
	g_assert_cmpuint (g_strv_length ((gchar **)lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_BODY)), ==, 3);
	g_assert_null (lrg_shape_sheet_list ((LrgShapeCategory)99)[0]);
	for (c = LRG_SHAPE_CATEGORY_MOUTH; c <= LRG_SHAPE_CATEGORY_BODY; c++)
		total += g_strv_length ((gchar **)lrg_shape_sheet_list ((LrgShapeCategory)c));
	g_assert_cmpuint (total, ==, 31);

	/* names are unique and look themselves up in the right category */
	for (i = 0; all[i] != NULL; i++)
	{
		guint k;

		for (k = i + 1; all[k] != NULL; k++)
			g_assert_cmpstr (all[i], !=, all[k]);
		g_assert_true (lrg_shape_sheet_lookup (all[i], &cat));
		g_assert_true (g_strv_contains (lrg_shape_sheet_list (cat), all[i]));
	}
	g_assert_true (lrg_shape_sheet_lookup (LRG_SHAPE_BROW_WORRIED, &cat));
	g_assert_cmpint (cat, ==, LRG_SHAPE_CATEGORY_BROWS);
	g_assert_false (lrg_shape_sheet_lookup ("blink", NULL));
	g_assert_false (lrg_shape_sheet_lookup (NULL, NULL));

	/* visemes <-> names */
	g_assert_cmpstr (lrg_viseme_get_shape_name (LRG_VISEME_REST), ==, "rest");
	g_assert_cmpstr (lrg_viseme_get_shape_name (LRG_VISEME_MBP), ==, LRG_SHAPE_MBP);
	g_assert_cmpstr (lrg_viseme_get_shape_name (LRG_VISEME_CDGKNRSTYZ), ==, "CDGKNRSTYZ");
	g_assert_null (lrg_viseme_get_shape_name ((LrgViseme)42));
	for (i = 0; mouth[i] != NULL; i++)
	{
		g_assert_true (lrg_viseme_from_shape_name (mouth[i], &v));
		g_assert_cmpint (v, ==, (gint)i);
	}
	g_assert_false (lrg_viseme_from_shape_name ("a", NULL));
	g_assert_false (lrg_viseme_from_shape_name (LRG_SHAPE_BLINK_L, NULL));
}

/* ==========================================================================
 * Phonemes
 * ========================================================================== */

static void
test_arpabet (void)
{
	static const struct
	{
		const gchar *phone;
		LrgViseme    viseme;
	} cases[] = {
		{ "AA", LRG_VISEME_A }, { "AH0", LRG_VISEME_A }, { "ae1", LRG_VISEME_A }, { "AY2", LRG_VISEME_A },
		{ "EH", LRG_VISEME_E }, { "ER1", LRG_VISEME_E }, { "EY0", LRG_VISEME_E },
		{ "IY1", LRG_VISEME_I }, { "ih", LRG_VISEME_I },
		{ "OW1", LRG_VISEME_O }, { "AO", LRG_VISEME_O }, { "OY", LRG_VISEME_O },
		{ "UW", LRG_VISEME_U }, { "UH1", LRG_VISEME_U },
		{ "M", LRG_VISEME_MBP }, { "b", LRG_VISEME_MBP }, { "P", LRG_VISEME_MBP },
		{ "F", LRG_VISEME_FV }, { "V", LRG_VISEME_FV },
		{ "L", LRG_VISEME_L }, { "TH", LRG_VISEME_L }, { "DH", LRG_VISEME_L },
		{ "W", LRG_VISEME_WQ },
		{ "S", LRG_VISEME_CDGKNRSTYZ }, { "NG", LRG_VISEME_CDGKNRSTYZ }, { "SH", LRG_VISEME_CDGKNRSTYZ },
		{ "HH", LRG_VISEME_CDGKNRSTYZ }, { "R", LRG_VISEME_CDGKNRSTYZ }, { "Y", LRG_VISEME_CDGKNRSTYZ },
		/* Gentle's position tags, padding */
		{ "hh_B", LRG_VISEME_CDGKNRSTYZ }, { "ah_I", LRG_VISEME_A }, { " ow_E ", LRG_VISEME_O },
		/* silence, noise, unknown */
		{ "sil", LRG_VISEME_REST }, { "SP", LRG_VISEME_REST }, { "spn", LRG_VISEME_REST },
		{ "", LRG_VISEME_REST }, { NULL, LRG_VISEME_REST }, { "XX", LRG_VISEME_REST }, { "1", LRG_VISEME_REST },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		if (g_test_verbose ())
			g_test_message ("%s", cases[i].phone != NULL ? cases[i].phone : "(null)");
		g_assert_cmpint (lrg_lip_sync_phoneme_to_viseme (cases[i].phone), ==, cases[i].viseme);
	}
}

static void
test_phoneme_list (void)
{
	g_autoptr(LrgLipSync) ls = lrg_lip_sync_new ();
	gdouble               start;
	gdouble               end;
	const gchar          *ph;
	LrgViseme             v;

	g_assert_true (lrg_lip_sync_add_phoneme (ls, 0.5, 0.6, "M"));
	g_assert_true (lrg_lip_sync_add_phoneme (ls, 0.1, 0.2, "AA1"));
	g_assert_true (lrg_lip_sync_add_phoneme (ls, 0.5, 0.55, "F"));   /* equal start: after M */
	g_assert_false (lrg_lip_sync_add_phoneme (ls, -0.1, 0.2, "AA"));
	g_assert_false (lrg_lip_sync_add_phoneme (ls, 0.3, 0.2, "AA"));
	g_assert_false (lrg_lip_sync_add_phoneme (ls, NAN, 0.2, "AA"));
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 3);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 0, &start, &end, &ph, &v));
	g_assert_cmpfloat (start, ==, 0.1);
	g_assert_cmpstr (ph, ==, "AA1");
	g_assert_cmpint (v, ==, LRG_VISEME_A);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 2, NULL, NULL, &ph, &v));
	g_assert_cmpstr (ph, ==, "F");
	g_assert_cmpint (v, ==, LRG_VISEME_FV);
	g_assert_false (lrg_lip_sync_get_phoneme (ls, 3, NULL, NULL, NULL, NULL));
	g_assert_cmpfloat (lrg_lip_sync_get_duration (ls), ==, 0.6);
	lrg_lip_sync_clear (ls);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 0);
	g_assert_cmpfloat (lrg_lip_sync_get_duration (ls), ==, 0.0);
}

/* ==========================================================================
 * Loaders
 * ========================================================================== */

static void
test_load_yaml (void)
{
	g_autoptr(LrgLipSync) ls = lrg_lip_sync_new ();
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *path = g_test_build_filename (G_TEST_DIST, "data", "lipsync-hello.yaml", NULL);
	g_autofree gchar     *missing = g_build_filename (g_get_tmp_dir (), "lrg-no-such-dir", "x.yaml", NULL);
	gdouble               start;
	gdouble               end;
	const gchar          *ph;
	LrgViseme             v;

	/* the "hello world" alignment */
	g_assert_true (lrg_lip_sync_load_file (ls, path, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 11);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 4, &start, &end, &ph, &v));
	g_assert_cmpfloat_with_epsilon (start, 0.30, 1e-9);
	g_assert_cmpfloat_with_epsilon (end, 0.45, 1e-9);
	g_assert_cmpstr (ph, ==, "OW1");
	g_assert_cmpint (v, ==, LRG_VISEME_O);
	g_assert_cmpfloat_with_epsilon (lrg_lip_sync_get_duration (ls), 1.0, 1e-9);

	/* text: replaces, a null phoneme is silence, empty is empty */
	g_assert_true (lrg_lip_sync_load_yaml (ls, "- {start: 0, end: 1, phoneme: M}\n"
	                                       "- {start: 1, end: 2, phoneme: null}\n", -1, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 2);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 1, NULL, NULL, &ph, &v));
	g_assert_cmpstr (ph, ==, "sil");
	g_assert_cmpint (v, ==, LRG_VISEME_REST);
	g_assert_true (lrg_lip_sync_load_yaml (ls, "", -1, &error));
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 0);

	/* the file must exist */
	g_assert_false (lrg_lip_sync_load_file (ls, missing, &error));
	g_assert_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_IO);
}

typedef struct
{
	const gchar *doc;
	gint         code;
	const gchar *message;
} BadDoc;

static void
test_load_yaml_errors (void)
{
	static const BadDoc cases[] = {
		{ "- {start: 0, end: [1\n", LRG_LIP_SYNC_ERROR_PARSE, "<data>: " },
		{ "start: 0\n", LRG_LIP_SYNC_ERROR_PARSE, "<data>: /: expected a list of {start, end, phoneme}" },
		{ "- 3\n", LRG_LIP_SYNC_ERROR_PARSE, "<data>: /0: expected a mapping" },
		{ "- {start: 0, end: 1, phone: AA}\n", LRG_LIP_SYNC_ERROR_INVALID, "<data>: /0: unknown key \"phone\"" },
		{ "- {start: 0, end: 1}\n", LRG_LIP_SYNC_ERROR_INVALID, "<data>: /0: missing key \"phoneme\"" },
		{ "- {start: 0, end: 1, phoneme: AA}\n- {start: x, end: 1, phoneme: AA}\n", LRG_LIP_SYNC_ERROR_PARSE,
		  "<data>: /1/start: expected a number, got \"x\"" },
		{ "- {start: -1, end: 1, phoneme: AA}\n", LRG_LIP_SYNC_ERROR_INVALID,
		  "<data>: /0/start: a time must be 0 or more" },
		{ "- {start: 2, end: 1, phoneme: AA}\n", LRG_LIP_SYNC_ERROR_INVALID,
		  "<data>: /0/end: the end comes before the start" },
		{ "- {start: 0, end: 1, phoneme: [AA]}\n", LRG_LIP_SYNC_ERROR_PARSE, "<data>: /0/phoneme: expected a string" },
	};
	g_autoptr(LrgLipSync) ls = lrg_lip_sync_new ();
	guint                 i;

	lrg_lip_sync_add_phoneme (ls, 0.0, 1.0, "AA");
	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError) error = NULL;

		g_assert_false (lrg_lip_sync_load_yaml (ls, cases[i].doc, -1, &error));
		if (g_test_verbose ())
			g_test_message ("case %u: %s", i, error->message);
		g_assert_error (error, LRG_LIP_SYNC_ERROR, cases[i].code);
		g_assert_nonnull (strstr (error->message, cases[i].message));
		/* a failed load leaves the track alone */
		g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 1);
	}
}

static void
test_load_csv (void)
{
	static const BadDoc cases[] = {
		{ "0,1\n", LRG_LIP_SYNC_ERROR_PARSE, "<data>: line 1: expected 3 fields (start,end,phoneme), got 2" },
		{ "start,end,phoneme\n0,1,AA\n0.5,x,M\n", LRG_LIP_SYNC_ERROR_PARSE,
		  "<data>: line 3: expected numbers for start and end" },
		{ "0,1,AA\nstart,end,phoneme\n", LRG_LIP_SYNC_ERROR_PARSE,
		  "<data>: line 2: expected numbers for start and end" },
		{ "1,0.5,AA\n", LRG_LIP_SYNC_ERROR_INVALID, "<data>: line 1: expected 0 <= start <= end" },
		{ "0,1,AA,extra\n", LRG_LIP_SYNC_ERROR_PARSE, "got 4" },
	};
	g_autoptr(LrgLipSync) ls = lrg_lip_sync_new ();
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *dir = NULL;
	g_autofree gchar     *path = NULL;
	const gchar          *ph;
	gdouble               start;
	guint                 i;

	/* a header, CRLF line ends, comments, blank lines, padding, an empty
	 * label */
	g_assert_true (lrg_lip_sync_load_csv (ls, "start,end,phoneme\r\n# hello\r\n\r\n 0.10 , 0.16 , HH \r\n"
	                                      "0.16,0.23,AH0\r\n0.23,0.3,\r\n", -1, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 3);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 0, &start, NULL, &ph, NULL));
	g_assert_cmpfloat_with_epsilon (start, 0.10, 1e-9);
	g_assert_cmpstr (ph, ==, "HH");
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 2, NULL, NULL, &ph, NULL));
	g_assert_cmpstr (ph, ==, "sil");

	/* no header; a length cuts the text */
	g_assert_true (lrg_lip_sync_load_csv (ls, "0,1,M\n1,2,AA\n", 6, &error));
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 1);

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError) err = NULL;

		g_assert_false (lrg_lip_sync_load_csv (ls, cases[i].doc, -1, &err));
		if (g_test_verbose ())
			g_test_message ("case %u: %s", i, err->message);
		g_assert_error (err, LRG_LIP_SYNC_ERROR, cases[i].code);
		g_assert_nonnull (strstr (err->message, cases[i].message));
		g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 1);
	}

	/* a .csv file is read as CSV, and messages name it */
	dir = g_dir_make_tmp ("lrg-lipsync-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "line.csv", NULL);
	g_assert_true (g_file_set_contents (path, "0,0.5,OW1\n0.5,1,bad,row\n", -1, &error));
	g_assert_false (lrg_lip_sync_load_file (ls, path, &error));
	g_assert_error (error, LRG_LIP_SYNC_ERROR, LRG_LIP_SYNC_ERROR_PARSE);
	g_assert_true (g_str_has_prefix (error->message, path));
	g_assert_nonnull (strstr (error->message, ": line 2: "));
	g_clear_error (&error);
	g_assert_true (g_file_set_contents (path, "0,0.5,OW1\n", -1, &error));
	g_assert_true (lrg_lip_sync_load_file (ls, path, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 0, NULL, NULL, &ph, NULL));
	g_assert_cmpstr (ph, ==, "OW1");
	g_unlink (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Writing keys
 * ========================================================================== */

/* the sum of every viseme weight of the "mouth" slot at @t */
static gfloat
viseme_sum (LrgSkeleton2DClip *clip,
            gfloat             t)
{
	const gchar * const *mouth = lrg_shape_sheet_list (LRG_SHAPE_CATEGORY_MOUTH);
	gfloat               sum = 0.0f;
	guint                i;

	for (i = 0; mouth[i] != NULL; i++)
	{
		gfloat w;

		lrg_skeleton2d_clip_sample_shape (clip, "mouth", mouth[i], t, &w);
		sum += w;
	}
	return sum;
}

static void
test_write_clip (void)
{
	g_autoptr(LrgLipSync)        ls = lrg_lip_sync_new ();
	g_autoptr(LrgSkeleton2DClip) clip = lrg_skeleton2d_clip_new ("hello");
	g_autoptr(LrgSkeleton2DClip) cut = lrg_skeleton2d_clip_new ("cut");
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *path = g_test_build_filename (G_TEST_DIST, "data", "lipsync-hello.yaml", NULL);
	g_auto(GStrv)                shapes = NULL;
	gfloat                       t;
	gfloat                       w;
	guint                        keys;
	LrgSkeleton2DCurve           curve;

	g_assert_true (lrg_lip_sync_load_file (ls, path, &error));
	g_assert_no_error (error);

	/* runs: rest, CDG, A, L, O, rest, WQ, E, L, CDG, rest (11 runs, 10
	 * boundaries): 3 keys for the first run (on, hold, off), 4 per
	 * middle run, 2 for the last (no fade-out) */
	keys = lrg_lip_sync_write_clip (ls, clip, "mouth", 0.06f);
	g_assert_cmpuint (keys, ==, 3 + 9 * 4 + 2);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_clip_get_duration (clip), 1.0f, EPS);
	shapes = lrg_skeleton2d_clip_get_shapes (clip, "mouth");
	g_assert_cmpuint (g_strv_length (shapes), ==, 7);   /* rest CDG A L O WQ E */
	g_assert_false (g_strv_contains ((const gchar * const *)shapes, "MBP"));

	/* "AH0" 0.16 .. 0.23: in across 0.13 .. 0.19, out across 0.20 .. 0.26 */
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (clip, "mouth", "A"), ==, 4);
	g_assert_true (lrg_skeleton2d_clip_get_shape_key (clip, "mouth", "A", 0, &t, &w, &curve));
	g_assert_cmpfloat_with_epsilon (t, 0.13f, EPS);
	g_assert_cmpfloat (w, ==, 0.0f);
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_get_shape_key (clip, "mouth", "A", 1, &t, &w, NULL);
	g_assert_cmpfloat_with_epsilon (t, 0.19f, EPS);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_get_shape_key (clip, "mouth", "A", 2, &t, &w, NULL);
	g_assert_cmpfloat_with_epsilon (t, 0.20f, EPS);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_get_shape_key (clip, "mouth", "A", 3, &t, &w, NULL);
	g_assert_cmpfloat_with_epsilon (t, 0.26f, EPS);
	g_assert_cmpfloat (w, ==, 0.0f);

	/* half way through the boundary at 0.16: A and HH's shape share */
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "A", 0.16f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.5f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "CDGKNRSTYZ", 0.16f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.5f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "O", 0.40f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);

	/* the cross-fades are complementary: one full mouth at every time */
	for (t = 0.0f; t <= 1.0f; t += 0.005f)
		g_assert_cmpfloat_with_epsilon (viseme_sum (clip, t), 1.0f, 1e-3f);

	/* rest holds at the start and the end */
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "rest", 0.0f, &w);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "rest", 1.0f, &w);
	g_assert_cmpfloat (w, ==, 1.0f);

	/* no cross-fade: stepped switches exactly at the boundaries */
	keys = lrg_lip_sync_write_clip (ls, cut, "mouth", 0.0f);
	/* on and off per run (the last only on), plus a 0 at time 0 for the
	 * six shapes first seen later */
	g_assert_cmpuint (keys, ==, 2 + 9 * 2 + 1 + 6);
	lrg_skeleton2d_clip_sample_shape (cut, "mouth", "A", 0.159f, &w);
	g_assert_cmpfloat (w, ==, 0.0f);
	lrg_skeleton2d_clip_sample_shape (cut, "mouth", "A", 0.161f, &w);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_sample_shape (cut, "mouth", "A", 0.229f, &w);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_sample_shape (cut, "mouth", "A", 0.231f, &w);
	g_assert_cmpfloat (w, ==, 0.0f);
	for (t = 0.0025f; t <= 1.0f; t += 0.005f)
		g_assert_cmpfloat_with_epsilon (viseme_sum (cut, t), 1.0f, 1e-3f);
}

/* Short phonemes shorten the blend; gaps become rest; overlaps cut the
 * earlier phoneme short; a clip already longer keeps its duration. */
static void
test_write_clip_edges (void)
{
	g_autoptr(LrgLipSync)        ls = lrg_lip_sync_new ();
	g_autoptr(LrgSkeleton2DClip) clip = lrg_skeleton2d_clip_new ("edges");
	g_autoptr(LrgSkeleton2DClip) empty = lrg_skeleton2d_clip_new ("empty");
	g_autoptr(LrgLipSync)        none = lrg_lip_sync_new ();
	gfloat                       t;
	gfloat                       w;

	lrg_skeleton2d_clip_set_duration (clip, 5.0f);
	lrg_lip_sync_add_phoneme (ls, 0.5, 0.52, "M");     /* 20 ms: blends of 10 ms */
	lrg_lip_sync_add_phoneme (ls, 0.52, 1.0, "AA");
	lrg_lip_sync_add_phoneme (ls, 0.8, 1.2, "IY");     /* cuts AA at 0.8 */
	g_assert_cmpuint (lrg_lip_sync_write_clip (ls, clip, "mouth", 0.2f), >, 0);
	g_assert_cmpfloat (lrg_skeleton2d_clip_get_duration (clip), ==, 5.0f);

	/* the leading gap is rest; MBP reaches 1 only at its middle */
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "rest", 0.2f, &w);
	g_assert_cmpfloat (w, ==, 1.0f);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "MBP", 0.51f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "MBP", 0.53f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.0f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "A", 0.8f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.5f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", "I", 1.1f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);
	for (t = 0.0f; t <= 1.2f; t += 0.0025f)
		g_assert_cmpfloat_with_epsilon (viseme_sum (clip, t), 1.0f, 1e-3f);

	/* nothing to write */
	g_assert_cmpuint (lrg_lip_sync_write_clip (none, empty, "mouth", 0.06f), ==, 0);
	g_assert_cmpfloat (lrg_skeleton2d_clip_get_duration (empty), ==, 0.0f);
}

/* ==========================================================================
 * Audio fallback
 * ========================================================================== */

/* 0.5 s of a 440 Hz tone then 0.5 s of silence, stereo 32-bit float */
static LrgWaveData *
make_tone_then_silence (gboolean cancel)
{
	const guint        rate = 16000;
	const guint        frames = rate;
	g_autofree gfloat *pcm = g_new0 (gfloat, frames * 2);
	guint              f;

	for (f = 0; f < frames / 2; f++)
	{
		gfloat s = 0.5f * sinf (2.0f * (gfloat)G_PI * 440.0f * (gfloat)f / (gfloat)rate);

		pcm[f * 2] = s;
		/* @cancel: the right channel is the inverse, so the downmix is
		 * silent */
		pcm[f * 2 + 1] = cancel ? -s : s;
	}
	return lrg_wave_data_new_from_samples (rate, 32, 2, (const guint8 *)pcm, (gsize)frames * 2 * sizeof (gfloat));
}

static void
test_wave_fallback (void)
{
	g_autoptr(LrgWaveData)       wave = make_tone_then_silence (FALSE);
	g_autoptr(LrgWaveData)       flat = make_tone_then_silence (TRUE);
	g_autoptr(LrgLipSync)        ls = NULL;
	g_autoptr(LrgLipSync)        quiet = NULL;
	g_autoptr(LrgSkeleton2DClip) clip = lrg_skeleton2d_clip_new ("speech");
	gdouble                      start;
	gdouble                      end;
	const gchar                 *ph;
	LrgViseme                    v;
	gfloat                       w;

	g_assert_nonnull (wave);
	g_assert_true (lrg_wave_data_is_valid (wave));
	g_assert_null (lrg_lip_sync_new_from_wave (wave, 0.0, 0.05f));

	/* 50 windows of 20 ms: 25 loud, 25 quiet */
	ls = lrg_lip_sync_new_from_wave (wave, 50.0, 0.05f);
	g_assert_nonnull (ls);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (ls), ==, 2);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 0, &start, &end, &ph, &v));
	g_assert_cmpfloat_with_epsilon (start, 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (end, 0.5, 1e-9);
	g_assert_cmpint (v, ==, LRG_VISEME_A);
	g_assert_true (lrg_lip_sync_get_phoneme (ls, 1, &start, &end, &ph, &v));
	g_assert_cmpfloat_with_epsilon (start, 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (end, 1.0, 1e-9);
	g_assert_cmpint (v, ==, LRG_VISEME_REST);

	/* into a clip: mouth open while the tone plays */
	lrg_lip_sync_write_clip (ls, clip, "mouth", LRG_LIP_SYNC_DEFAULT_CROSSFADE);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", LRG_SHAPE_A, 0.25f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "mouth", LRG_SHAPE_REST, 0.75f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);

	/* the channels are averaged: a cancelling stereo pair is silence */
	quiet = lrg_lip_sync_new_from_wave (flat, 50.0, 0.05f);
	g_assert_cmpuint (lrg_lip_sync_get_phoneme_count (quiet), ==, 1);
	g_assert_true (lrg_lip_sync_get_phoneme (quiet, 0, NULL, &end, NULL, &v));
	g_assert_cmpint (v, ==, LRG_VISEME_REST);
	g_assert_cmpfloat_with_epsilon (end, 1.0, 1e-9);
}

/* ==========================================================================
 * Lip sync on a character
 * ========================================================================== */

/* A one-slot "face": a quad with an A shape that drops the bottom edge
 * and a rest shape that does nothing, plus blink and breathe shapes. */
static LrgSkeleton2D *
make_face (LrgSkinnedSprite **out_quad)
{
	LrgSkeleton2D               *skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite)  quad = lrg_skinned_sprite_new_quad (0, 0, 10, 10, 0);
	gfloat                       open[8] = { 0, 0, 0, 0, 0, 4, 0, 4 };
	gfloat                       blink[8] = { 0, 3, 0, 3, 0, 0, 0, 0 };
	gint                         bone;
	gint                         slot;

	bone = lrg_skeleton2d_add_bone (skel, "head", -1, 0, 0, 0, 1, 1, 10);
	slot = lrg_skeleton2d_add_slot (skel, "mouth", bone, "face");
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_A, open, 8));
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_REST, NULL, 0));
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_BLINK_L, blink, 8));
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_BREATHE, open, 8));
	g_assert_true (lrg_skeleton2d_set_attachment (skel, NULL, slot, "face", quad));
	if (out_quad != NULL)
		*out_quad = quad;
	return skel;
}

static void
test_lip_sync_character (void)
{
	LrgSkinnedSprite             *quad = NULL;
	g_autoptr(LrgSkeleton2D)      skel = make_face (&quad);
	g_autoptr(LrgLipSync)         ls = lrg_lip_sync_new ();
	g_autoptr(LrgSkeleton2DClip)  clip = lrg_skeleton2d_clip_new ("say");
	g_autofree gchar             *text = NULL;
	g_autoptr(LrgSkeleton2D)      back = NULL;
	g_autoptr(GError)             error = NULL;
	const gfloat                 *pos;

	lrg_lip_sync_add_phoneme (ls, 0.0, 0.2, "sil");
	lrg_lip_sync_add_phoneme (ls, 0.2, 0.6, "AA1");
	lrg_lip_sync_write_clip (ls, clip, "mouth", 0.1f);
	lrg_skeleton2d_add_clip (skel, clip);

	/* mid-vowel the bottom edge is down 4 */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (clip, skel, 0.4f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (quad, NULL);
	g_assert_cmpfloat_with_epsilon (pos[5], 14.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[1], 0.0f, EPS);

	/* across the blend at 0.2 it is half open */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (clip, skel, 0.2f, 1.0f);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (quad, NULL);
	g_assert_cmpfloat_with_epsilon (pos[5], 12.0f, EPS);

	/* the keyed character survives a version 2 save and load */
	text = lrg_skeleton2d_save_to_data (skel, NULL);
	g_assert_true (g_str_has_prefix (text, "rskel2d: 2\n"));
	back = lrg_skeleton2d_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (lrg_skeleton2d_get_clip (back, "say"), "mouth", "A"),
	                  ==, lrg_skeleton2d_clip_get_shape_key_count (clip, "mouth", "A"));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/lip-sync/shape-sheet", test_shape_sheet);
	g_test_add_func ("/lip-sync/arpabet", test_arpabet);
	g_test_add_func ("/lip-sync/phonemes", test_phoneme_list);
	g_test_add_func ("/lip-sync/load/yaml", test_load_yaml);
	g_test_add_func ("/lip-sync/load/yaml-errors", test_load_yaml_errors);
	g_test_add_func ("/lip-sync/load/csv", test_load_csv);
	g_test_add_func ("/lip-sync/write-clip", test_write_clip);
	g_test_add_func ("/lip-sync/write-clip-edges", test_write_clip_edges);
	g_test_add_func ("/lip-sync/wave-fallback", test_wave_fallback);
	g_test_add_func ("/lip-sync/character", test_lip_sync_character);

	return g_test_run ();
}
