/* test-skeleton2d-character.c - issue #8 end to end: a talking character
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The demo character of the skeleton2d examples
 * (examples/skeleton2d/lrg-demo-character.c, compiled into this test):
 * an LrgSkeleton2D built in code with generated textures and the full
 * standard shape sheet.  Everything here runs on the CPU, with no window
 * and no files but tests/data/lipsync-hello.yaml:
 *
 * - every shape of the sheet changes the rendered character;
 * - the character lip-syncs "hello world" through LrgLipSync, blinks
 *   through LrgSkeleton2DProcedural and changes mood halfway, rendered
 *   by LrgReelSkeleton2DClip and LrgReelRenderer into a PNG sequence:
 *   the mouth differs between viseme frames, the eye is shut on the
 *   blink frame, and a parallel render on 8 threads is byte-identical
 *   to the sequential one;
 * - the character and its clip survive a .rskel2d round trip (byte-
 *   identical re-save, same shapes and deformation);
 * - a component crossfade between two mood clips mixes shape weights
 *   linearly, down to the deformed vertices.
 *
 * LRG_TEST_SHOTS=DIR saves a few renders for a visual check.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <math.h>

#include <libregnum.h>

#include "lrg-demo-character.h"

#define EPS          (1e-3f)

/* the reel: 1 s at 30 fps, 256 px square, the character at half size
 * standing on the bottom edge */
#define REEL_SIZE    (256)
#define REEL_FPS     (30.0)
#define REEL_FRAMES  (30)
#define ACTOR_X      (128.0)
#define ACTOR_Y      (256.0)
#define ACTOR_SCALE  (0.5)

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Saves @img when LRG_TEST_SHOTS names a directory. */
static void
maybe_save (GrlImage    *img,
            const gchar *name)
{
	const gchar      *dir = g_getenv ("LRG_TEST_SHOTS");
	g_autofree gchar *path = NULL;

	if (dir == NULL || *dir == '\0')
		return;
	path = g_strdup_printf ("%s/%s.png", dir, name);
	grl_image_export (img, path);
}

/* The canvas box of a feature, for a character drawn with its root at
 * (ACTOR_X, ACTOR_Y) and scale ACTOR_SCALE. */
static void
region_box (LrgDemoRegion  region,
            gint          *x0,
            gint          *y0,
            gint          *x1,
            gint          *y1)
{
	gfloat x;
	gfloat y;
	gfloat w;
	gfloat h;

	lrg_demo_character_get_region (region, &x, &y, &w, &h);
	*x0 = (gint)floor (ACTOR_X + ACTOR_SCALE * x);
	*y0 = (gint)floor (ACTOR_Y + ACTOR_SCALE * y);
	*x1 = (gint)ceil (ACTOR_X + ACTOR_SCALE * (x + w));
	*y1 = (gint)ceil (ACTOR_Y + ACTOR_SCALE * (y + h));
}

/* Pixels in [x0, x1) x [y0, y1) whose RGB differ by more than 8 between
 * two images of the same size. */
static guint
count_diff (GrlImage *a,
            GrlImage *b,
            gint      x0,
            gint      y0,
            gint      x1,
            gint      y1)
{
	const guint8 *pa = grl_image_get_pixels (a, NULL);
	const guint8 *pb = grl_image_get_pixels (b, NULL);
	gint          w = grl_image_get_width (a);
	guint         n = 0;
	gint          x;
	gint          y;

	g_assert_cmpint (w, ==, grl_image_get_width (b));
	g_assert_cmpint (grl_image_get_height (a), ==, grl_image_get_height (b));
	x0 = MAX (x0, 0);
	y0 = MAX (y0, 0);
	x1 = MIN (x1, w);
	y1 = MIN (y1, grl_image_get_height (a));
	for (y = y0; y < y1; y++)
	{
		for (x = x0; x < x1; x++)
		{
			const guint8 *p = pa + ((gsize)y * (gsize)w + (gsize)x) * 4;
			const guint8 *q = pb + ((gsize)y * (gsize)w + (gsize)x) * 4;

			if (ABS ((gint)p[0] - q[0]) > 8 || ABS ((gint)p[1] - q[1]) > 8 || ABS ((gint)p[2] - q[2]) > 8)
				n++;
		}
	}
	return n;
}

/* Draws @skel in its current shape weights at the reel's placement into
 * a new image. */
static GrlImage *
render_pose (LrgSkeleton2D *skel)
{
	GrlColor                  paper = { 250, 244, 232, 255 };
	g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new (REEL_SIZE, REEL_SIZE, &paper);

	lrg_skeleton2d_set_root_transform (skel, (gfloat)ACTOR_X, (gfloat)ACTOR_Y, 0.0f,
	                                   (gfloat)ACTOR_SCALE, (gfloat)ACTOR_SCALE);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, canvas, NULL);
	return grl_image_copy (lrg_image_canvas_get_image (canvas));
}

/* The attachment slot @slot of @skel shows. */
static LrgSkinnedSprite *
slot_sprite (LrgSkeleton2D *skel,
             const gchar   *slot)
{
	gint si = lrg_skeleton2d_find_slot (skel, slot);

	g_assert_cmpint (si, >=, 0);
	return lrg_skeleton2d_get_attachment (skel, si, lrg_skeleton2d_get_slot_attachment (skel, si));
}

/* ------------------------------------------------------------------ */
/* The full shape sheet                                                */
/* ------------------------------------------------------------------ */

/*
 * Every name of the standard sheet is implemented by some slot, and at
 * weight 1 changes the rendered character, in the right place: mouth
 * shapes in the mouth box, blinks in their own eye only, everything
 * else somewhere.  "rest" is the neutral mouth by definition and must
 * not change anything.
 */
static void
test_full_sheet (void)
{
	g_autoptr(LrgSkeleton2D) skel = lrg_demo_character_new ();
	g_autoptr(GrlImage)      rest = NULL;
	const gchar * const     *all = lrg_shape_sheet_list_all ();
	gint                     mx0, my0, mx1, my1;
	gint                     lx0, ly0, lx1, ly1;
	gint                     rx0, ry0, rx1, ry1;
	guint                    i;

	region_box (LRG_DEMO_REGION_MOUTH, &mx0, &my0, &mx1, &my1);
	region_box (LRG_DEMO_REGION_EYE_LEFT, &lx0, &ly0, &lx1, &ly1);
	region_box (LRG_DEMO_REGION_EYE_RIGHT, &rx0, &ry0, &rx1, &ry1);

	lrg_skeleton2d_reset_pose (skel);
	rest = render_pose (skel);
	maybe_save (rest, "s2d-character-rest");

	for (i = 0; all[i] != NULL; i++)
	{
		g_autoptr(GrlImage) img = NULL;
		LrgShapeCategory    cat;
		guint               slots;
		guint               total;

		lrg_skeleton2d_reset_pose (skel);
		slots = lrg_demo_character_set_shape (skel, all[i], 1.0f);
		g_assert_cmpuint (slots, >=, 1);
		img = render_pose (skel);
		total = count_diff (rest, img, 0, 0, REEL_SIZE, REEL_SIZE);
		g_assert_true (lrg_shape_sheet_lookup (all[i], &cat));

		if (g_strcmp0 (all[i], LRG_SHAPE_REST) == 0)
		{
			g_assert_cmpuint (total, ==, 0);
			continue;
		}
		if (total < 20)
			g_error ("shape %s changes only %u pixels", all[i], total);

		/* visemes move the mouth and nothing else */
		if (cat == LRG_SHAPE_CATEGORY_MOUTH)
		{
			g_assert_cmpuint (count_diff (rest, img, mx0, my0, mx1, my1), ==, total);
			g_assert_cmpuint (slots, ==, 1);
		}

		/* a blink shuts one eye: the character's left is screen right */
		if (g_strcmp0 (all[i], LRG_SHAPE_BLINK_L) == 0)
		{
			g_assert_cmpuint (count_diff (rest, img, lx0, ly0, lx1, ly1), ==, total);
			g_assert_cmpuint (count_diff (rest, img, rx0, ry0, rx1, ry1), ==, 0);
		}
		if (g_strcmp0 (all[i], LRG_SHAPE_BLINK_R) == 0)
		{
			g_assert_cmpuint (count_diff (rest, img, rx0, ry0, rx1, ry1), ==, total);
			g_assert_cmpuint (count_diff (rest, img, lx0, ly0, lx1, ly1), ==, 0);
		}

		/* moods span the face: eyes, brows and mouth */
		if (cat == LRG_SHAPE_CATEGORY_MOOD)
		{
			g_assert_cmpuint (slots, ==, 3);
			g_assert_cmpuint (count_diff (rest, img, mx0, my0, mx1, my1), >, 0);
		}

		/* squash and stretch move every slot together */
		if (g_strcmp0 (all[i], LRG_SHAPE_SQUASH) == 0 || g_strcmp0 (all[i], LRG_SHAPE_STRETCH) == 0)
			g_assert_cmpuint (slots, ==, 5);
		if (g_str_has_prefix (all[i], "blink") || g_strcmp0 (all[i], LRG_SHAPE_A) == 0 ||
		    g_strcmp0 (all[i], LRG_SHAPE_HAPPY) == 0 || g_strcmp0 (all[i], LRG_SHAPE_SQUASH) == 0)
		{
			g_autofree gchar *name = g_strdup_printf ("s2d-character-%s", all[i]);

			maybe_save (img, name);
		}
	}
}

/* ------------------------------------------------------------------ */
/* A talking, blinking character rendered headless                      */
/* ------------------------------------------------------------------ */

/* the pose callback: the procedural blink on top of the clip */
static void
apply_procedural (LrgReelSkeleton2DClip *clip,
                  LrgSkeleton2D         *skel,
                  gdouble                seconds,
                  gpointer               user_data)
{
	lrg_skeleton2d_procedural_apply (LRG_SKELETON2D_PROCEDURAL (user_data), skel, seconds);
}

/* the frame index whose time maximises @shape's weight in @clip */
static gint
peak_frame (LrgSkeleton2DClip *clip,
            const gchar       *shape)
{
	gint   best = -1;
	gfloat best_w = 0.0f;
	gint   f;

	for (f = 0; f < REEL_FRAMES; f++)
	{
		gfloat w = 0.0f;

		if (lrg_skeleton2d_clip_sample_shape (clip, LRG_DEMO_SLOT_MOUTH, shape, (gfloat)(f / REEL_FPS), &w) && w > best_w)
		{
			best_w = w;
			best = f;
		}
	}
	g_assert_cmpint (best, >=, 0);
	g_assert_cmpfloat (best_w, >, 0.99f);
	return best;
}

/* frame @f of the sequence in @dir */
static GrlImage *
load_frame (const gchar *dir,
            gint         f)
{
	g_autofree gchar *name = g_strdup_printf ("frame_%05d.png", f);
	g_autofree gchar *path = g_build_filename (dir, name, NULL);
	GrlImage         *img = grl_image_new_from_file (path);

	g_assert_nonnull (img);
	g_assert_cmpint (grl_image_get_width (img), ==, REEL_SIZE);
	return img;
}

/* RGB of pixel (x, y) */
static void
pixel_at (GrlImage *img,
          gint      x,
          gint      y,
          guint8    rgb[3])
{
	const guint8 *p = grl_image_get_pixels (img, NULL);

	p += ((gsize)y * (gsize)grl_image_get_width (img) + (gsize)x) * 4;
	rgb[0] = p[0];
	rgb[1] = p[1];
	rgb[2] = p[2];
}

/* Removes the PNG files in @dir and @dir itself. */
static void
remove_frames (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	if (d == NULL)
		return;
	while ((name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *path = g_build_filename (dir, name, NULL);

		g_unlink (path);
	}
	g_rmdir (dir);
}

static void
test_talking_head (void)
{
	g_autoptr(GError)                  error = NULL;
	g_autofree gchar                  *line = g_test_build_filename (G_TEST_DIST, "data", "lipsync-hello.yaml", NULL);
	g_autofree gchar                  *tmp = g_dir_make_tmp ("lrg-s2d-talk-XXXXXX", &error);
	g_autofree gchar                  *seq_dir = NULL;
	g_autofree gchar                  *par_dir = NULL;
	g_autoptr(LrgLipSync)              lip = lrg_lip_sync_new ();
	g_autoptr(LrgSkeleton2D)           skel = lrg_demo_character_new ();
	g_autoptr(LrgSkeleton2DClip)       talk = lrg_skeleton2d_clip_new ("talk");
	g_autoptr(LrgSkeleton2DProcedural) proc = lrg_skeleton2d_procedural_new ();
	g_autoptr(LrgReel)                 reel = NULL;
	g_autoptr(LrgReelSolidClip)        bg = NULL;
	g_autoptr(LrgReelSkeleton2DClip)   actor = NULL;
	g_autoptr(LrgReelRenderer)         renderer = NULL;
	g_autoptr(LrgReelSeqExporter)      seq = NULL;
	g_autoptr(LrgReelSeqExporter)      par = NULL;
	GrlColor                           paper = { 250, 244, 232, 255 };
	gint                               f_rest;
	gint                               f_o;
	gint                               f_l;
	gint                               f_blink = -1;
	gint                               f_open = -1;
	gfloat                             blink_w = 0.0f;
	gint                               mx0, my0, mx1, my1;
	gint                               ex;
	gint                               ey;
	gint                               f;

	g_assert_no_error (error);
	seq_dir = g_build_filename (tmp, "seq", NULL);
	par_dir = g_build_filename (tmp, "par", NULL);

	/* the line: "hello world", 1 s; visemes keyed on the mouth with
	 * 40 ms cross-fades */
	g_assert_true (lrg_lip_sync_load_file (lip, line, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_lip_sync_write_clip (lip, talk, LRG_DEMO_SLOT_MOUTH, 0.04f), >, 0);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_clip_get_duration (talk), 1.0f, EPS);

	/* the mood: happy until 0.7 s, sad from 0.8 s (keys on eyes, brows
	 * and mouth) */
	g_assert_cmpuint (lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, 0.0f, 1.0f), ==, 3);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, 0.7f, 1.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, 0.8f, 0.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SAD, 0.7f, 0.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SAD, 0.8f, 1.0f);
	lrg_skeleton2d_add_clip (skel, talk);

	/* the blink: every 0.3 .. 0.45 s, 0.15 s long, seeded */
	g_assert_true (lrg_skeleton2d_procedural_add_blink (proc, LRG_DEMO_SLOT_EYES, 0.3f, 0.45f, 0.15f, 3));

	/* frames to look at: the peaks of rest, O ("hello") and L ("world"),
	 * a fully shut and a fully open eye before the mood change */
	f_rest = peak_frame (talk, LRG_SHAPE_REST);
	f_o = peak_frame (talk, LRG_SHAPE_O);
	f_l = peak_frame (talk, LRG_SHAPE_L);
	for (f = 0; f < 21; f++)
	{
		gfloat w = lrg_skeleton2d_procedural_get_blink_weight (proc, 0, f / REEL_FPS);

		if (w > blink_w)
		{
			blink_w = w;
			f_blink = f;
		}
		if (f_open < 0 && w == 0.0f && f > 0)
			f_open = f;
	}
	g_assert_cmpfloat (blink_w, >, 0.75f);   /* the lid then covers the pupil */
	g_assert_cmpint (f_open, >=, 0);

	/* the reel: backdrop and the character, played once */
	reel = lrg_reel_new ("talk", REEL_SIZE, REEL_SIZE, REEL_FPS, REEL_FRAMES);
	bg = lrg_reel_solid_clip_new (&paper);
	lrg_reel_add_clip (reel, LRG_REEL_CLIP (bg));
	actor = lrg_reel_skeleton2d_clip_new (skel, talk);
	lrg_reel_skeleton2d_clip_set_loop (actor, FALSE);
	lrg_reel_skeleton2d_clip_set_placement (actor, ACTOR_X, ACTOR_Y, ACTOR_SCALE);
	lrg_reel_skeleton2d_clip_set_pose_func (actor, apply_procedural, g_object_ref (proc), g_object_unref);
	lrg_reel_add_clip (reel, LRG_REEL_CLIP (actor));
	renderer = lrg_reel_renderer_new (reel);

	/* sequential and 8-thread renders of the same reel */
	seq = lrg_reel_seq_exporter_new (seq_dir, "frame_%05d", LRG_REEL_IMAGE_FORMAT_PNG);
	g_assert_true (lrg_reel_renderer_render_to_exporter (renderer, LRG_REEL_EXPORTER (seq), &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_reel_seq_exporter_get_frame_count (seq), ==, REEL_FRAMES);
	par = lrg_reel_seq_exporter_new (par_dir, "frame_%05d", LRG_REEL_IMAGE_FORMAT_PNG);
	g_assert_true (lrg_reel_renderer_render_parallel (renderer, 8, LRG_REEL_EXPORTER (par), &error));
	g_assert_no_error (error);
	for (f = 0; f < REEL_FRAMES; f++)
	{
		g_autofree gchar *name = g_strdup_printf ("frame_%05d.png", f);
		g_autofree gchar *a = g_build_filename (seq_dir, name, NULL);
		g_autofree gchar *b = g_build_filename (par_dir, name, NULL);
		g_autofree gchar *da = NULL;
		g_autofree gchar *db = NULL;
		gsize             la = 0;
		gsize             lb = 0;

		g_assert_true (g_file_get_contents (a, &da, &la, NULL));
		g_assert_true (g_file_get_contents (b, &db, &lb, NULL));
		g_assert_cmpmem (da, la, db, lb);
	}

	/* the mouth differs between visemes, and the rest of the face does
	 * not (no blink or mood change between these frames) */
	region_box (LRG_DEMO_REGION_MOUTH, &mx0, &my0, &mx1, &my1);
	{
		g_autoptr(GrlImage) rest = load_frame (seq_dir, f_rest);
		g_autoptr(GrlImage) o = load_frame (seq_dir, f_o);
		g_autoptr(GrlImage) l = load_frame (seq_dir, f_l);

		maybe_save (o, "s2d-talk-O");
		maybe_save (l, "s2d-talk-L");
		g_assert_cmpuint (count_diff (rest, o, mx0, my0, mx1, my1), >, 50);
		g_assert_cmpuint (count_diff (o, l, mx0, my0, mx1, my1), >, 50);
		g_assert_cmpuint (count_diff (rest, l, mx0, my0, mx1, my1), >, 50);
	}

	/* the blink frame shows skin where the open eye shows its pupil
	 * (the character's left eye, centred on the iris) */
	ex = (gint)(ACTOR_X + ACTOR_SCALE * 46.0);
	ey = (gint)(ACTOR_Y + ACTOR_SCALE * (-312.0 + 3.0));
	{
		g_autoptr(GrlImage) shut = load_frame (seq_dir, f_blink);
		g_autoptr(GrlImage) open = load_frame (seq_dir, f_open);
		gint                lx0, ly0, lx1, ly1;
		guint8              c[3];

		maybe_save (shut, "s2d-talk-blink");
		region_box (LRG_DEMO_REGION_EYE_LEFT, &lx0, &ly0, &lx1, &ly1);
		g_assert_cmpuint (count_diff (shut, open, lx0, ly0, lx1, ly1), >, 50);
		pixel_at (open, ex, ey, c);
		g_assert_cmpuint (c[0], <, 100);   /* pupil */
		pixel_at (shut, ex, ey, c);
		g_assert_cmpuint (c[0], >, 200);   /* skin */
		g_assert_cmpuint (c[1], >, 160);
	}

	/* the mood change shows: the last frame (sad) and an early one
	 * (happy) differ around the eyes even with the mouth at rest */
	{
		g_autoptr(GrlImage) happy = load_frame (seq_dir, f_rest);
		g_autoptr(GrlImage) sad = load_frame (seq_dir, REEL_FRAMES - 1);
		gint                fx0, fy0, fx1, fy1;

		maybe_save (sad, "s2d-talk-sad");
		region_box (LRG_DEMO_REGION_FACE, &fx0, &fy0, &fx1, &fy1);
		g_assert_cmpuint (count_diff (happy, sad, fx0, fy0, fx1, my0), >, 50);
	}

	remove_frames (seq_dir);
	remove_frames (par_dir);
	g_rmdir (tmp);
}

/* ------------------------------------------------------------------ */
/* .rskel2d round trip                                                 */
/* ------------------------------------------------------------------ */

static void
test_round_trip (void)
{
	g_autoptr(GError)            error = NULL;
	g_autoptr(LrgSkeleton2D)     skel = lrg_demo_character_new ();
	g_autoptr(LrgSkeleton2D)     back = NULL;
	g_autoptr(LrgSkeleton2DClip) talk = lrg_skeleton2d_clip_new ("talk");
	g_autoptr(LrgLipSync)        lip = lrg_lip_sync_new ();
	g_autofree gchar            *line = g_test_build_filename (G_TEST_DIST, "data", "lipsync-hello.yaml", NULL);
	g_autofree gchar            *first = NULL;
	g_autofree gchar            *second = NULL;
	gsize                        n1 = 0;
	gsize                        n2 = 0;
	guint                        i;

	/* the character with a lip-sync and mood clip */
	g_assert_true (lrg_lip_sync_load_file (lip, line, &error));
	lrg_lip_sync_write_clip (lip, talk, LRG_DEMO_SLOT_MOUTH, 0.04f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SURPRISED, 0.0f, 0.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SURPRISED, 0.5f, 1.0f);
	lrg_skeleton2d_add_clip (skel, talk);

	/* save, load, save again: the same bytes, a version 2 document */
	first = lrg_skeleton2d_save_to_data (skel, &n1);
	g_assert_nonnull (first);
	g_assert_nonnull (strstr (first, "rskel2d: 2"));
	back = lrg_skeleton2d_new_from_data (first, (gssize)n1, &error);
	g_assert_no_error (error);
	g_assert_nonnull (back);
	second = lrg_skeleton2d_save_to_data (back, &n2);
	g_assert_cmpmem (first, n1, second, n2);

	/* every attachment keeps every shape with the same offsets */
	for (i = 0; i < lrg_skeleton2d_get_slot_count (skel); i++)
	{
		const gchar      *slot = lrg_skeleton2d_get_slot_name (skel, (gint)i);
		LrgSkinnedSprite *a = slot_sprite (skel, slot);
		LrgSkinnedSprite *b = slot_sprite (back, slot);
		guint             k;

		g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (a), ==, lrg_skinned_sprite_get_shape_count (b));
		for (k = 0; k < lrg_skinned_sprite_get_shape_count (a); k++)
		{
			const gchar  *name = lrg_skinned_sprite_get_shape_name (a, k);
			const gfloat *oa;
			const gfloat *ob;
			guint         na = 0;
			guint         nb = 0;
			guint         j;

			g_assert_cmpstr (name, ==, lrg_skinned_sprite_get_shape_name (b, k));
			oa = lrg_skinned_sprite_get_shape_offsets (a, name, &na);
			ob = lrg_skinned_sprite_get_shape_offsets (b, name, &nb);
			g_assert_cmpuint (na, ==, nb);
			for (j = 0; j < na; j++)
				g_assert_cmpfloat_with_epsilon (oa[j], ob[j], 1e-3f);
		}
	}

	/* the loaded clip poses the loaded character as the original does */
	{
		LrgSkeleton2DClip *talk2 = lrg_skeleton2d_get_clip (back, "talk");
		const gfloat      *pa;
		const gfloat      *pb;
		guint             na = 0;
		guint             nb = 0;
		guint             j;

		g_assert_nonnull (talk2);
		lrg_skeleton2d_reset_pose (skel);
		lrg_skeleton2d_reset_pose (back);
		lrg_skeleton2d_clip_apply (talk, skel, 0.4f, 1.0f);
		lrg_skeleton2d_clip_apply (talk2, back, 0.4f, 1.0f);
		lrg_skeleton2d_update_world (skel);
		lrg_skeleton2d_update_world (back);
		lrg_skeleton2d_deform (skel);
		lrg_skeleton2d_deform (back);
		pa = lrg_skinned_sprite_get_deformed (slot_sprite (skel, LRG_DEMO_SLOT_MOUTH), &na);
		pb = lrg_skinned_sprite_get_deformed (slot_sprite (back, LRG_DEMO_SLOT_MOUTH), &nb);
		g_assert_cmpuint (na, ==, nb);
		for (j = 0; j < na; j++)
			g_assert_cmpfloat_with_epsilon (pa[j], pb[j], 1e-2f);
		g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (back, lrg_skeleton2d_find_slot (back, LRG_DEMO_SLOT_EYES),
		                                                    LRG_SHAPE_SURPRISED), >, 0.7f);
	}
}

/* ------------------------------------------------------------------ */
/* Crossfading two mood clips                                          */
/* ------------------------------------------------------------------ */

/*
 * "calm" holds happy at 1; "upset" ramps sad from 0 to 1 over 1 s.  A
 * one-second component crossfade from calm to upset, half-way, mixes sad
 * at 0.5 (the fade) of 0.5 (upset's own key) = 0.25 over calm's 0, and
 * leaves happy at calm's 1 (upset does not key it).  The mouth's
 * deformed vertices are then exactly rest + happy + 0.25 sad offsets.
 */
static void
test_crossfade (void)
{
	g_autoptr(LrgSkeleton2D)          skel = lrg_demo_character_new ();
	g_autoptr(LrgSkeleton2DClip)      calm = lrg_skeleton2d_clip_new ("calm");
	g_autoptr(LrgSkeleton2DClip)      upset = lrg_skeleton2d_clip_new ("upset");
	g_autoptr(LrgSkeleton2DComponent) comp = NULL;
	LrgSkinnedSprite                 *mouth = slot_sprite (skel, LRG_DEMO_SLOT_MOUTH);
	gint                              ms = lrg_skeleton2d_find_slot (skel, LRG_DEMO_SLOT_MOUTH);
	gint                              es = lrg_skeleton2d_find_slot (skel, LRG_DEMO_SLOT_EYES);
	const gfloat                     *happy;
	const gfloat                     *sad;
	const gfloat                     *pos;
	guint                             n = 0;
	guint                             v;

	lrg_demo_character_key_shape (skel, calm, LRG_SHAPE_HAPPY, 0.0f, 1.0f);
	lrg_demo_character_key_shape (skel, calm, LRG_SHAPE_SAD, 0.0f, 0.0f);
	lrg_skeleton2d_clip_set_duration (calm, 1.0f);
	lrg_demo_character_key_shape (skel, upset, LRG_SHAPE_SAD, 0.0f, 0.0f);
	lrg_demo_character_key_shape (skel, upset, LRG_SHAPE_SAD, 1.0f, 1.0f);
	lrg_skeleton2d_add_clip (skel, calm);
	lrg_skeleton2d_add_clip (skel, upset);

	comp = lrg_skeleton2d_component_new_with_skeleton (skel);
	g_assert_true (lrg_skeleton2d_component_play (comp, "calm", TRUE));
	lrg_component_update (LRG_COMPONENT (comp), 0.1f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_HAPPY), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_SAD), 0.0f, EPS);

	g_assert_true (lrg_skeleton2d_component_crossfade (comp, "upset", 1.0f, FALSE));
	lrg_component_update (LRG_COMPONENT (comp), 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_SAD), 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, es, LRG_SHAPE_SAD), 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_HAPPY), 1.0f, EPS);

	/* down to the vertices (the root is unplaced and the head unturned,
	 * so deformed positions are setup positions plus offsets) */
	lrg_skeleton2d_set_root_transform (skel, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	happy = lrg_skinned_sprite_get_shape_offsets (mouth, LRG_SHAPE_HAPPY, &n);
	sad = lrg_skinned_sprite_get_shape_offsets (mouth, LRG_SHAPE_SAD, NULL);
	pos = lrg_skinned_sprite_get_deformed (mouth, NULL);
	g_assert_cmpuint (n, ==, lrg_skinned_sprite_get_vertex_count (mouth) * 2);
	for (v = 0; v < lrg_skinned_sprite_get_vertex_count (mouth); v++)
	{
		gfloat x;
		gfloat y;

		lrg_skinned_sprite_get_vertex (mouth, v, &x, &y, NULL, NULL);
		g_assert_cmpfloat_with_epsilon (pos[v * 2], x + happy[v * 2] + 0.25f * sad[v * 2], 1e-2f);
		g_assert_cmpfloat_with_epsilon (pos[v * 2 + 1], y + happy[v * 2 + 1] + 0.25f * sad[v * 2 + 1], 1e-2f);
	}

	/* after the fade: upset's own weights, happy no longer keyed */
	lrg_component_update (LRG_COMPONENT (comp), 0.6f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_SAD), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, ms, LRG_SHAPE_HAPPY), 0.0f, EPS);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/skeleton2d-character/full-sheet", test_full_sheet);
	g_test_add_func ("/skeleton2d-character/talking-head", test_talking_head);
	g_test_add_func ("/skeleton2d-character/round-trip", test_round_trip);
	g_test_add_func ("/skeleton2d-character/crossfade", test_crossfade);

	return g_test_run ();
}
