/* test-skeleton2d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for 2D skeletal animation: LrgSkeleton2D (hierarchy, world
 * transforms, skins, slots, draw order), LrgSkinnedSprite (CPU skinning,
 * automatic weights), LrgSkeleton2DClip (sampling, curves, slot swaps,
 * draw order, events), blend shapes (sprite API, morph before skinning,
 * slot weights, weight tracks with curves, crossfading shapes), the
 * .rskel2d format (v1 and v2 fixtures, byte-stable round trip, strict
 * errors) and LrgSkeleton2DComponent (crossfades, events, drawing a
 * bending arm and a walking figure under a GPU).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define EPS (1e-3f)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

/* Opens a hidden 8x8 window when a display exists. */
static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-skeleton2d-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

/* A two-bone arm along +X: "upper" 0..50, "fore" 50..100, and a 10-cell
 * strip mesh over it (y -5 .. 5) weighted by hand: x < 50 on upper,
 * x == 50 half and half, x > 50 on fore. */
static LrgSkeleton2D *
make_arm (LrgSkinnedSprite **out_strip)
{
	LrgSkeleton2D               *skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite)  strip = lrg_skinned_sprite_new_grid (0, -5, 100, 10, 10, 1);
	gint                         upper;
	gint                         fore;
	gint                         slot;
	guint                        i;

	upper = lrg_skeleton2d_add_bone (skel, "upper", -1, 0, 0, 0, 1, 1, 50);
	fore = lrg_skeleton2d_add_bone (skel, "fore", upper, 50, 0, 0, 1, 1, 50);
	slot = lrg_skeleton2d_add_slot (skel, "arm", upper, "arm");

	for (i = 0; i < lrg_skinned_sprite_get_vertex_count (strip); i++)
	{
		gfloat x;
		gint   bones[2];
		gfloat weights[2];

		lrg_skinned_sprite_get_vertex (strip, i, &x, NULL, NULL, NULL);
		bones[0] = upper;
		bones[1] = fore;
		weights[0] = x < 50.0f ? 1.0f : x > 50.0f ? 0.0f : 0.5f;
		weights[1] = 1.0f - weights[0];
		g_assert_true (lrg_skinned_sprite_set_weights (strip, i, 2, bones, weights));
	}
	g_assert_true (lrg_skeleton2d_set_attachment (skel, NULL, slot, "arm", strip));
	if (out_strip != NULL)
		*out_strip = strip;
	return skel;
}

/* A clip "bend" turning the forearm 90 degrees over one second. */
static LrgSkeleton2DClip *
make_bend_clip (void)
{
	LrgSkeleton2DClip *clip = lrg_skeleton2d_clip_new ("bend");

	lrg_skeleton2d_clip_add_rotate_key (clip, "fore", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (clip, "fore", 1.0f, 90.0f, LRG_SKELETON2D_CURVE_LINEAR);
	return clip;
}

/* A limb strip from (x0, y0) to (x0 + w, y0 + h) with @rows cells,
 * auto-weighted to @bones. */
static LrgSkinnedSprite *
limb (LrgSkeleton2D *skel,
      gfloat         x,
      gfloat         y,
      gfloat         w,
      gfloat         h,
      guint          rows,
      const gint    *bones,
      guint          n_bones)
{
	LrgSkinnedSprite *s = lrg_skinned_sprite_new_grid (x, y, w, h, 1, rows);

	g_assert_true (lrg_skeleton2d_compute_auto_weights (skel, s, bones, n_bones, 4.0f, 2, 1));
	return s;
}

/* A stick figure standing at the origin, head up (y down):
 * root > hip > torso (up) > head, torso > arms (upper > lower),
 * hip > legs (thigh > shin); one slot per part, two skins and two clips
 * ("walk" with every key kind, "wave"). */
static LrgSkeleton2D *
make_figure (void)
{
	LrgSkeleton2D               *skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkeleton2DClip) walk = lrg_skeleton2d_clip_new ("walk");
	g_autoptr(LrgSkeleton2DClip) wave = lrg_skeleton2d_clip_new ("wave");
	gint                         b[16];
	gint                         s[8];
	gint                         pair[2];
	const gchar                 *flipped[] = { "arm_l", "leg_l", "torso", "head", "leg_r", "arm_r", NULL };

	lrg_skeleton2d_set_name (skel, "figure");
	b[0] = lrg_skeleton2d_add_bone (skel, "root", -1, 0, 0, 0, 1, 1, 0);
	b[1] = lrg_skeleton2d_add_bone (skel, "hip", b[0], 0, 0, 0, 1, 1, 0);
	b[2] = lrg_skeleton2d_add_bone (skel, "torso", b[1], 0, 0, -90, 1, 1, 60);
	b[3] = lrg_skeleton2d_add_bone (skel, "head", b[2], 60, 0, 0, 1, 1, 22);
	b[4] = lrg_skeleton2d_add_bone (skel, "upper_l", b[2], 50, -12, 180, 1, 1, 30);
	b[5] = lrg_skeleton2d_add_bone (skel, "lower_l", b[4], 30, 0, 0, 1, 1, 30);
	b[6] = lrg_skeleton2d_add_bone (skel, "upper_r", b[2], 50, 12, 180, 1, 1, 30);
	b[7] = lrg_skeleton2d_add_bone (skel, "lower_r", b[6], 30, 0, 0, 1, 1, 30);
	b[8] = lrg_skeleton2d_add_bone (skel, "thigh_l", b[1], -8, 0, 90, 1, 1, 35);
	b[9] = lrg_skeleton2d_add_bone (skel, "shin_l", b[8], 35, 0, 0, 1, 1, 35);
	b[10] = lrg_skeleton2d_add_bone (skel, "thigh_r", b[1], 8, 0, 90, 1, 1, 35);
	b[11] = lrg_skeleton2d_add_bone (skel, "shin_r", b[10], 35, 0, 0, 1, 1, 35);

	/* slots, back to front */
	s[0] = lrg_skeleton2d_add_slot (skel, "arm_r", b[6], "arm");
	s[1] = lrg_skeleton2d_add_slot (skel, "leg_r", b[10], "leg");
	s[2] = lrg_skeleton2d_add_slot (skel, "torso", b[2], "torso");
	s[3] = lrg_skeleton2d_add_slot (skel, "head", b[3], "head");
	s[4] = lrg_skeleton2d_add_slot (skel, "leg_l", b[8], "leg");
	s[5] = lrg_skeleton2d_add_slot (skel, "arm_l", b[4], "arm");
	lrg_skeleton2d_set_slot_setup_color (skel, s[0], 0.25f, 0.45f, 0.8f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, s[1], 0.3f, 0.3f, 0.45f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, s[2], 0.85f, 0.4f, 0.2f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, s[3], 0.95f, 0.8f, 0.65f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, s[4], 0.4f, 0.4f, 0.6f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, s[5], 0.35f, 0.6f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);

	/* attachments (meshes in setup space) */
	{
		g_autoptr(LrgSkinnedSprite) arm_r = NULL;
		g_autoptr(LrgSkinnedSprite) arm_l = NULL;
		g_autoptr(LrgSkinnedSprite) leg_r = NULL;
		g_autoptr(LrgSkinnedSprite) leg_l = NULL;
		g_autoptr(LrgSkinnedSprite) torso = lrg_skinned_sprite_new_quad (-14, -62, 28, 64, b[2]);
		g_autoptr(LrgSkinnedSprite) head = lrg_skinned_sprite_new_quad (-12, -86, 24, 24, -1);
		g_autoptr(LrgSkinnedSprite) blink = lrg_skinned_sprite_new_quad (-12, -86, 24, 24, -1);
		g_autoptr(LrgSkinnedSprite) big_head = lrg_skinned_sprite_new_quad (-16, -92, 32, 32, -1);

		pair[0] = b[6];
		pair[1] = b[7];
		arm_r = limb (skel, 7, -52, 10, 62, 12, pair, 2);
		pair[0] = b[4];
		pair[1] = b[5];
		arm_l = limb (skel, -17, -52, 10, 62, 12, pair, 2);
		pair[0] = b[10];
		pair[1] = b[11];
		leg_r = limb (skel, 3, -2, 11, 72, 12, pair, 2);
		pair[0] = b[8];
		pair[1] = b[9];
		leg_l = limb (skel, -14, -2, 11, 72, 12, pair, 2);

		lrg_skinned_sprite_set_image (head, "head.png");
		lrg_skinned_sprite_set_region (head, 0, 0, 32, 32);
		lrg_skinned_sprite_set_image (blink, "head.png");
		lrg_skinned_sprite_set_region (blink, 32, 0, 32, 32);

		lrg_skeleton2d_set_attachment (skel, NULL, s[0], "arm", arm_r);
		lrg_skeleton2d_set_attachment (skel, NULL, s[1], "leg", leg_r);
		lrg_skeleton2d_set_attachment (skel, NULL, s[2], "torso", torso);
		lrg_skeleton2d_set_attachment (skel, NULL, s[3], "head", head);
		lrg_skeleton2d_set_attachment (skel, NULL, s[3], "blink", blink);
		lrg_skeleton2d_set_attachment (skel, NULL, s[4], "leg", leg_l);
		lrg_skeleton2d_set_attachment (skel, NULL, s[5], "arm", arm_l);
		lrg_skeleton2d_set_attachment (skel, "big-head", s[3], "head", big_head);
	}

	/* walk: legs and arms swing in opposition, the torso bobs, the head
	 * blinks and tints, the arms swap depth halfway, footstep events */
	lrg_skeleton2d_clip_set_loop (walk, TRUE);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_l", 0.0f, -25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_l", 0.5f, 25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_l", 1.0f, -25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_l", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_l", 0.25f, -40.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_l", 0.5f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_r", 0.0f, 25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_r", 0.5f, -25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "thigh_r", 1.0f, 25.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_r", 0.5f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_r", 0.75f, -40.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "shin_r", 1.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_l", 0.0f, 30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_l", 0.5f, -30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_l", 1.0f, 30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "lower_l", 0.0f, -10.0f, LRG_SKELETON2D_CURVE_BEZIER);
	lrg_skeleton2d_clip_set_key_bezier (walk, "lower_l", LRG_SKELETON2D_CHANNEL_ROTATE, 0, 0.42f, 0.0f, 0.58f, 1.0f);
	lrg_skeleton2d_clip_add_rotate_key (walk, "lower_l", 0.5f, -70.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "lower_l", 1.0f, -10.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_r", 0.0f, -30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_r", 0.5f, 30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (walk, "upper_r", 1.0f, -30.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_translate_key (walk, "hip", 0.0f, 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_translate_key (walk, "hip", 0.25f, 0.0f, -4.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_translate_key (walk, "hip", 0.5f, 0.0f, 0.0f, LRG_SKELETON2D_CURVE_STEPPED);
	lrg_skeleton2d_clip_add_scale_key (walk, "head", 0.0f, 1.0f, 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_scale_key (walk, "head", 0.5f, 1.1f, 0.95f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_attachment_key (walk, "head", 0.0f, "head");
	lrg_skeleton2d_clip_add_attachment_key (walk, "head", 0.4f, "blink");
	lrg_skeleton2d_clip_add_attachment_key (walk, "head", 0.5f, "head");
	lrg_skeleton2d_clip_add_color_key (walk, "torso", 0.0f, 0.85f, 0.4f, 0.2f, 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_color_key (walk, "torso", 0.5f, 1.0f, 0.6f, 0.3f, 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_draw_order_key (walk, 0.5f, flipped);
	lrg_skeleton2d_clip_add_draw_order_key (walk, 1.0f, NULL);
	lrg_skeleton2d_clip_add_event_key (walk, 0.0f, "step");
	lrg_skeleton2d_clip_add_event_key (walk, 0.5f, "step");
	lrg_skeleton2d_add_clip (skel, walk);

	lrg_skeleton2d_clip_set_duration (wave, 2.0f);
	lrg_skeleton2d_clip_add_rotate_key (wave, "upper_r", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (wave, "upper_r", 0.5f, 150.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_add_clip (skel, wave);
	return skel;
}

/* ==========================================================================
 * Hierarchy and world transforms
 * ========================================================================== */

static void
test_hierarchy (void)
{
	g_autoptr(LrgSkeleton2D) skel = lrg_skeleton2d_new ();
	gint                     root;
	gint                     upper;
	gint                     fore;
	gfloat                   x;
	gfloat                   y;
	gfloat                   m[6];

	root = lrg_skeleton2d_add_bone (skel, "root", -1, 100, 50, 0, 1, 1, 0);
	upper = lrg_skeleton2d_add_bone (skel, "upper", root, 0, 0, 90, 1, 1, 40);
	fore = lrg_skeleton2d_add_bone (skel, "fore", upper, 40, 0, 0, 1, 1, 30);
	g_assert_cmpint (root, ==, 0);
	g_assert_cmpint (fore, ==, 2);

	/* invalid additions */
	g_assert_cmpint (lrg_skeleton2d_add_bone (skel, "root", -1, 0, 0, 0, 1, 1, 0), ==, -1);
	g_assert_cmpint (lrg_skeleton2d_add_bone (skel, "x", 7, 0, 0, 0, 1, 1, 0), ==, -1);
	g_assert_cmpint (lrg_skeleton2d_add_bone (skel, "", -1, 0, 0, 0, 1, 1, 0), ==, -1);
	g_assert_cmpint (lrg_skeleton2d_find_bone (skel, "fore"), ==, fore);
	g_assert_cmpint (lrg_skeleton2d_get_bone_parent (skel, fore), ==, upper);
	g_assert_cmpstr (lrg_skeleton2d_get_bone_name (skel, upper), ==, "upper");

	/* rotation 90 turns +X to +Y (clockwise on a y-down screen) */
	lrg_skeleton2d_update_world (skel);
	g_assert_true (lrg_skeleton2d_get_bone_world_position (skel, fore, &x, &y));
	g_assert_cmpfloat_with_epsilon (x, 100.0f, EPS);
	g_assert_cmpfloat_with_epsilon (y, 90.0f, EPS);
	g_assert_true (lrg_skeleton2d_get_bone_world_tip (skel, fore, &x, &y));
	g_assert_cmpfloat_with_epsilon (x, 100.0f, EPS);
	g_assert_cmpfloat_with_epsilon (y, 120.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_bone_world_rotation (skel, fore), 90.0f, EPS);

	/* local pose + root transform (translate, rotate -90, scale 2) */
	lrg_skeleton2d_set_bone_local (skel, fore, 40, 0, 90, 1, 1);
	lrg_skeleton2d_set_root_transform (skel, 10, 20, -90, 2, 2);
	lrg_skeleton2d_update_world (skel);
	/* root (100,50) -> rot -90 -> (50,-100) -> *2 -> (100,-200) + (10,20) */
	g_assert_true (lrg_skeleton2d_get_bone_world_position (skel, root, &x, &y));
	g_assert_cmpfloat_with_epsilon (x, 110.0f, EPS);
	g_assert_cmpfloat_with_epsilon (y, -180.0f, EPS);
	/* upper points +X in world (90 - 90); fore turns another 90 -> +Y */
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_bone_world_rotation (skel, upper), 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_bone_world_rotation (skel, fore), 90.0f, EPS);
	g_assert_true (lrg_skeleton2d_get_bone_world_position (skel, fore, &x, &y));
	g_assert_cmpfloat_with_epsilon (x, 110.0f + 80.0f, EPS);
	g_assert_cmpfloat_with_epsilon (y, -180.0f, EPS);
	g_assert_true (lrg_skeleton2d_get_bone_world_matrix (skel, fore, m));
	g_assert_cmpfloat_with_epsilon (m[1], 2.0f, EPS);  /* +X axis -> +Y, scale 2 */

	/* reset returns to setup; picking finds the child over its parent */
	lrg_skeleton2d_set_root_transform (skel, 0, 0, 0, 1, 1);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_update_world (skel);
	g_assert_cmpint (lrg_skeleton2d_pick_bone (skel, 101, 100, 3), ==, fore);
	g_assert_cmpint (lrg_skeleton2d_pick_bone (skel, 101, 70, 3), ==, upper);
	g_assert_cmpint (lrg_skeleton2d_pick_bone (skel, 300, 300, 3), ==, -1);

	/* the setup pose skins to the identity, also under rotated and
	 * scaled parents */
	{
		g_autoptr(LrgSkeleton2D) arm = lrg_skeleton2d_new ();
		const gfloat            *pal;
		guint                    n;
		guint                    i;
		gint                     t;
		gint                     u;

		t = lrg_skeleton2d_add_bone (arm, "torso", -1, 3, 4, -90, 1, 2, 60);
		u = lrg_skeleton2d_add_bone (arm, "up", t, 50, -12, 180, 1.5f, 1, 30);
		lrg_skeleton2d_add_bone (arm, "lo", u, 30, 0, 20, 1, 1, 30);
		lrg_skeleton2d_update_world (arm);
		pal = lrg_skeleton2d_get_palette (arm, &n);
		g_assert_cmpuint (n, ==, 18);
		for (i = 0; i < n; i++)
			g_assert_cmpfloat_with_epsilon (pal[i], (i % 6 == 0 || i % 6 == 3) ? 1.0f : 0.0f, EPS);
	}
}

/* ==========================================================================
 * Skinning
 * ========================================================================== */

static void
test_skinning (void)
{
	LrgSkinnedSprite         *strip = NULL;
	g_autoptr(LrgSkeleton2D)  skel = make_arm (&strip);
	const gfloat             *pos;
	guint                     n;
	guint                     i;
	gfloat                    minx;
	gfloat                    maxy;

	g_assert_cmpuint (lrg_skinned_sprite_get_vertex_count (strip), ==, 22);
	g_assert_cmpuint (lrg_skinned_sprite_get_triangle_count (strip), ==, 20);

	/* setup pose: the palette is the identity, nothing moves */
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, &n);
	g_assert_cmpuint (n, ==, 44);
	for (i = 0; i < 22; i++)
	{
		gfloat x;
		gfloat y;

		lrg_skinned_sprite_get_vertex (strip, i, &x, &y, NULL, NULL);
		g_assert_cmpfloat_with_epsilon (pos[i * 2], x, EPS);
		g_assert_cmpfloat_with_epsilon (pos[i * 2 + 1], y, EPS);
	}

	/* bend the forearm 90 degrees: fore vertices (x, y) go to
	 * (50 - y, x - 50); the elbow row blends half and half */
	lrg_skeleton2d_set_bone_local (skel, 1, 50, 0, 90, 1, 1);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	for (i = 0; i < 22; i++)
	{
		gfloat x;
		gfloat y;
		gfloat ex;
		gfloat ey;

		lrg_skinned_sprite_get_vertex (strip, i, &x, &y, NULL, NULL);
		if (x < 50.0f)
		{
			ex = x;
			ey = y;
		}
		else if (x > 50.0f)
		{
			ex = 50.0f - y;
			ey = x - 50.0f;
		}
		else
		{
			ex = 0.5f * x + 0.5f * (50.0f - y);
			ey = 0.5f * y + 0.5f * (x - 50.0f);
		}
		g_assert_cmpfloat_with_epsilon (pos[i * 2], ex, EPS);
		g_assert_cmpfloat_with_epsilon (pos[i * 2 + 1], ey, EPS);
	}
	g_assert_true (lrg_skinned_sprite_get_deformed_bounds (strip, &minx, NULL, NULL, &maxy));
	g_assert_cmpfloat_with_epsilon (minx, 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (maxy, 50.0f, EPS);

	/* weights: normalised, heaviest first, bad input refused */
	{
		gint   b[4];
		gfloat w[4];
		gint   in_b[3] = { 1, 0, 1 };
		gfloat in_w[3] = { 1.0f, 3.0f, 0.0f };

		g_assert_true (lrg_skinned_sprite_set_weights (strip, 0, 3, in_b, in_w));
		g_assert_cmpuint (lrg_skinned_sprite_get_weights (strip, 0, b, w), ==, 2);
		g_assert_cmpint (b[0], ==, 0);
		g_assert_cmpfloat_with_epsilon (w[0], 0.75f, EPS);
		g_assert_cmpfloat_with_epsilon (w[1], 0.25f, EPS);
		g_assert_cmpint (b[2], ==, -1);
		in_b[0] = -1;
		g_assert_false (lrg_skinned_sprite_set_weights (strip, 0, 1, in_b, in_w));
		g_assert_false (lrg_skinned_sprite_set_weights (strip, 999, 0, NULL, NULL));
		g_assert_false (lrg_skinned_sprite_add_triangle (strip, 0, 1, 999));
	}

	/* an unweighted quad follows the slot's bone (default bone) */
	{
		g_autoptr(LrgSkinnedSprite) quad = lrg_skinned_sprite_new_quad (60, -5, 10, 10, -1);
		const gfloat               *p;
		guint                       np;

		lrg_skinned_sprite_deform (quad, lrg_skeleton2d_get_palette (skel, &np), np, 1);
		p = lrg_skinned_sprite_get_deformed (quad, NULL);
		/* (60, -5) on fore -> (55, 10) */
		g_assert_cmpfloat_with_epsilon (p[0], 55.0f, EPS);
		g_assert_cmpfloat_with_epsilon (p[1], 10.0f, EPS);
	}
}

/* ==========================================================================
 * Clips
 * ========================================================================== */

static void
test_clip_sampling (void)
{
	g_autoptr(LrgSkeleton2D)     skel = make_figure ();
	g_autoptr(LrgSkeleton2DClip) clip = lrg_skeleton2d_clip_new ("curves");
	LrgSkeleton2DClip           *walk;
	gfloat                       v[4];
	gfloat                       rot;
	gfloat                       y;
	gfloat                       c[4];
	const gint                  *order;
	guint                        n;
	gint                         head;
	g_auto(GStrv)                targets = NULL;

	/* linear, stepped and Bézier spans */
	lrg_skeleton2d_clip_add_rotate_key (clip, "a", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_rotate_key (clip, "a", 1.0f, 90.0f, LRG_SKELETON2D_CURVE_STEPPED);
	lrg_skeleton2d_clip_add_rotate_key (clip, "a", 2.0f, 0.0f, LRG_SKELETON2D_CURVE_BEZIER);
	lrg_skeleton2d_clip_add_rotate_key (clip, "a", 3.0f, 100.0f, LRG_SKELETON2D_CURVE_LINEAR);
	g_assert_true (lrg_skeleton2d_clip_set_key_bezier (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 2,
	                                                   0.42f, 0.0f, 0.58f, 1.0f));
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_clip_get_duration (clip), 3.0f, EPS);
	g_assert_true (lrg_skeleton2d_clip_sample (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 0.5f, v));
	g_assert_cmpfloat_with_epsilon (v[0], 45.0f, EPS);
	g_assert_true (lrg_skeleton2d_clip_sample (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 1.5f, v));
	g_assert_cmpfloat_with_epsilon (v[0], 90.0f, EPS);
	g_assert_true (lrg_skeleton2d_clip_sample (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 2.5f, v));
	g_assert_cmpfloat_with_epsilon (v[0], 50.0f, 0.01f);  /* symmetric ease: midpoint */
	g_assert_true (lrg_skeleton2d_clip_sample (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 2.2f, v));
	g_assert_cmpfloat (v[0], <, 15.0f);                    /* ease-in: slower than linear (20) */
	g_assert_cmpfloat (v[0], >, 0.0f);
	g_assert_true (lrg_skeleton2d_clip_sample (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 9.0f, v));
	g_assert_cmpfloat_with_epsilon (v[0], 100.0f, EPS);
	g_assert_false (lrg_skeleton2d_clip_sample (clip, "nope", LRG_SKELETON2D_CHANNEL_ROTATE, 0.0f, v));
	/* a key at an existing time replaces it; remove_key */
	lrg_skeleton2d_clip_add_rotate_key (clip, "a", 1.0f, 80.0f, LRG_SKELETON2D_CURVE_STEPPED);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE), ==, 4);
	g_assert_true (lrg_skeleton2d_clip_remove_key (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE, 3));
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "a", LRG_SKELETON2D_CHANNEL_ROTATE), ==, 3);

	/* the walk clip on the figure, mid-stride at 0.45 s */
	walk = lrg_skeleton2d_get_clip (skel, "walk");
	g_assert_nonnull (walk);
	targets = lrg_skeleton2d_clip_get_targets (walk, FALSE);
	g_assert_cmpstr (targets[0], ==, "thigh_l");
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (walk, skel, 0.45f, 1.0f);
	lrg_skeleton2d_get_bone_local (skel, lrg_skeleton2d_find_bone (skel, "thigh_l"), NULL, NULL, &rot, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rot, 90.0f + 20.0f, EPS);  /* setup 90 + offset 20 */
	lrg_skeleton2d_get_bone_local (skel, lrg_skeleton2d_find_bone (skel, "hip"), NULL, &y, NULL, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (y, -0.8f, EPS);
	head = lrg_skeleton2d_find_slot (skel, "head");
	g_assert_cmpstr (lrg_skeleton2d_get_slot_attachment (skel, head), ==, "blink");      /* swap */
	g_assert_true (lrg_skinned_sprite_get_image (lrg_skeleton2d_get_attachment (skel, head, NULL)) != NULL);
	order = lrg_skeleton2d_get_draw_order (skel, &n);
	g_assert_cmpuint (n, ==, 6);
	g_assert_cmpint (order[0], ==, 0);                                             /* setup order */
	lrg_skeleton2d_get_slot_color (skel, lrg_skeleton2d_find_slot (skel, "torso"), c);
	g_assert_cmpfloat_with_epsilon (c[0], 0.85f + 0.15f * 0.9f, EPS);

	/* at 0.6 s: head back, draw order flipped (arm_l at the back) */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (walk, skel, 0.6f, 1.0f);
	g_assert_cmpstr (lrg_skeleton2d_get_slot_attachment (skel, head), ==, "head");
	order = lrg_skeleton2d_get_draw_order (skel, NULL);
	g_assert_cmpint (order[0], ==, lrg_skeleton2d_find_slot (skel, "arm_l"));
	g_assert_cmpint (order[5], ==, lrg_skeleton2d_find_slot (skel, "arm_r"));
	/* at the end: the null key restores the setup order */
	lrg_skeleton2d_clip_apply (walk, skel, 1.0f, 1.0f);
	order = lrg_skeleton2d_get_draw_order (skel, NULL);
	g_assert_cmpint (order[0], ==, 0);

	/* half weight mixes bones, switches attachments and order */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (walk, skel, 0.45f, 0.25f);
	lrg_skeleton2d_get_bone_local (skel, lrg_skeleton2d_find_bone (skel, "thigh_l"), NULL, NULL, &rot, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rot, 90.0f + 5.0f, EPS);
	g_assert_cmpstr (lrg_skeleton2d_get_slot_attachment (skel, head), ==, "head");  /* below 0.5 */

	/* skins: the active skin first, the default skin as a fallback */
	g_assert_cmpuint (lrg_skeleton2d_get_skin_count (skel), ==, 2);
	g_assert_true (lrg_skeleton2d_set_skin (skel, "big-head"));
	g_assert_true (lrg_skeleton2d_get_attachment (skel, head, "head") ==
	               lrg_skeleton2d_get_skin_attachment (skel, "big-head", head, "head"));
	g_assert_true (lrg_skeleton2d_get_attachment (skel, head, "blink") ==
	               lrg_skeleton2d_get_skin_attachment (skel, NULL, head, "blink"));
	g_assert_false (lrg_skeleton2d_set_skin (skel, "missing"));
	g_assert_cmpstr (lrg_skeleton2d_get_skin (skel), ==, "big-head");
	g_assert_true (lrg_skeleton2d_set_skin (skel, NULL));
	{
		g_auto(GStrv) names = lrg_skeleton2d_list_attachments (skel, NULL, head);

		g_assert_cmpuint (g_strv_length (names), ==, 2);
		g_assert_cmpstr (names[1], ==, "blink");
	}
}

/* ==========================================================================
 * Component: crossfade, events, finished
 * ========================================================================== */

typedef struct
{
	guint  events;
	guint  finished;
	gchar *last_event;
} Counter;

static void
on_event (LrgSkeleton2DComponent *comp,
          const gchar            *clip,
          const gchar            *event,
          Counter                *c)
{
	c->events++;
	g_free (c->last_event);
	c->last_event = g_strdup_printf ("%s/%s", clip, event);
}

static void
on_finished (LrgSkeleton2DComponent *comp,
             const gchar            *clip,
             Counter                *c)
{
	c->finished++;
}

static void
test_crossfade (void)
{
	g_autoptr(LrgSkeleton2D)          skel = make_arm (NULL);
	g_autoptr(LrgSkeleton2DClip)      hold = lrg_skeleton2d_clip_new ("hold");
	g_autoptr(LrgSkeleton2DClip)      bend = make_bend_clip ();
	g_autoptr(LrgSkeleton2DComponent) comp = NULL;
	Counter                           c = { 0, 0, NULL };
	gfloat                            rot;

	lrg_skeleton2d_clip_add_rotate_key (hold, "fore", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_set_duration (hold, 1.0f);
	lrg_skeleton2d_clip_add_event_key (bend, 0.0f, "start");
	lrg_skeleton2d_clip_add_event_key (bend, 0.75f, "snap");
	lrg_skeleton2d_add_clip (skel, hold);
	lrg_skeleton2d_add_clip (skel, bend);

	comp = lrg_skeleton2d_component_new_with_skeleton (skel);
	g_signal_connect (comp, "event", G_CALLBACK (on_event), &c);
	g_signal_connect (comp, "finished", G_CALLBACK (on_finished), &c);

	g_assert_false (lrg_skeleton2d_component_play (comp, "missing", TRUE));
	g_assert_true (lrg_skeleton2d_component_play (comp, "hold", TRUE));
	lrg_component_update (LRG_COMPONENT (comp), 0.25f);

	/* fade from hold (0 degrees) into bend (0 -> 90 over 1 s) over 1 s */
	g_assert_true (lrg_skeleton2d_component_crossfade (comp, "bend", 1.0f, FALSE));
	g_assert_cmpuint (c.events, ==, 1);                       /* "start" at time 0 */
	g_assert_cmpstr (c.last_event, ==, "bend/start");
	lrg_component_update (LRG_COMPONENT (comp), 0.5f);
	/* bend at 0.5 s is 45 degrees, mixed at weight 0.5 over hold's 0 */
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 0.5f, EPS);
	lrg_skeleton2d_get_bone_local (skel, 1, NULL, NULL, &rot, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rot, 22.5f, EPS);
	lrg_component_update (LRG_COMPONENT (comp), 0.3f);       /* 0.8 s: "snap" fired */
	g_assert_cmpuint (c.events, ==, 2);
	g_assert_cmpstr (c.last_event, ==, "bend/snap");
	lrg_component_update (LRG_COMPONENT (comp), 0.3f);       /* 1.1 s: fade done, clip done */
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 1.0f, EPS);
	lrg_skeleton2d_get_bone_local (skel, 1, NULL, NULL, &rot, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rot, 90.0f, EPS);
	g_assert_cmpuint (c.finished, ==, 1);
	g_assert_false (lrg_skeleton2d_component_is_playing (comp));
	g_assert_cmpstr (lrg_skeleton2d_component_get_clip (comp), ==, "bend");

	/* looping fires events once per loop; scrubbing fires none */
	g_assert_true (lrg_skeleton2d_component_play (comp, "bend", TRUE));
	c.events = 0;
	lrg_component_update (LRG_COMPONENT (comp), 2.5f);       /* snap, start+snap, start */
	g_assert_cmpuint (c.events, ==, 4);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_time (comp), 0.5f, EPS);
	lrg_skeleton2d_component_set_time (comp, 0.9f);
	g_assert_cmpuint (c.events, ==, 4);
	lrg_skeleton2d_get_bone_local (skel, 1, NULL, NULL, &rot, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (rot, 81.0f, EPS);

	/* the clip property plays; stop returns to setup */
	g_object_set (comp, "clip", "hold", NULL);
	g_assert_cmpstr (lrg_skeleton2d_component_get_clip (comp), ==, "hold");
	lrg_skeleton2d_component_stop (comp);
	g_assert_null (lrg_skeleton2d_component_get_clip (comp));
	g_free (c.last_event);
}

/* ==========================================================================
 * Blend shapes
 * ========================================================================== */

/* The sprite's shape API at its boundaries: sizes, replacement, missing
 * names, vertices added later, copy and clear. */
static void
test_shape_api (void)
{
	g_autoptr(LrgSkinnedSprite) quad = lrg_skinned_sprite_new_quad (0, 0, 10, 10, -1);
	g_autoptr(LrgSkinnedSprite) copy = NULL;
	gfloat                      off[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	gfloat                      bad[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	const gfloat               *got;
	guint                       n;

	/* wrong length, empty name and non-finite offsets are refused */
	g_assert_false (lrg_skinned_sprite_add_shape (quad, "A", off, 6));
	g_assert_false (lrg_skinned_sprite_add_shape (quad, "", off, 8));
	bad[3] = INFINITY;
	g_assert_false (lrg_skinned_sprite_add_shape (quad, "A", bad, 8));
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (quad), ==, 0);

	/* add, read back, weights start at 0 */
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_A, off, 8));
	g_assert_true (lrg_skinned_sprite_add_shape (quad, LRG_SHAPE_BLINK_L, NULL, 0));
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (quad), ==, 2);
	g_assert_cmpstr (lrg_skinned_sprite_get_shape_name (quad, 1), ==, "blink_l");
	g_assert_null (lrg_skinned_sprite_get_shape_name (quad, 2));
	g_assert_cmpint (lrg_skinned_sprite_find_shape (quad, "A"), ==, 0);
	g_assert_cmpint (lrg_skinned_sprite_find_shape (quad, "nope"), ==, -1);
	got = lrg_skinned_sprite_get_shape_offsets (quad, "A", &n);
	g_assert_cmpuint (n, ==, 8);
	g_assert_cmpfloat (got[5], ==, 6.0f);
	got = lrg_skinned_sprite_get_shape_offsets (quad, "blink_l", &n);
	g_assert_cmpuint (n, ==, 8);
	g_assert_cmpfloat (got[7], ==, 0.0f);
	g_assert_null (lrg_skinned_sprite_get_shape_offsets (quad, "nope", &n));
	g_assert_cmpuint (n, ==, 0);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (quad, "A"), ==, 0.0f);

	/* weights: unknown names are a no-op, non-finite values ignored, the
	 * sprite itself does not clamp */
	lrg_skinned_sprite_set_shape_weight (quad, "nope", 1.0f);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (quad, "nope"), ==, 0.0f);
	lrg_skinned_sprite_set_shape_weight (quad, "A", 0.5f);
	lrg_skinned_sprite_set_shape_weight (quad, "A", NAN);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (quad, "A"), ==, 0.5f);
	lrg_skinned_sprite_set_shape_weight (quad, "blink_l", 2.0f);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (quad, "blink_l"), ==, 2.0f);

	/* replacing keeps the weight and the index */
	off[0] = 9.0f;
	g_assert_true (lrg_skinned_sprite_add_shape (quad, "A", off, 8));
	g_assert_cmpint (lrg_skinned_sprite_find_shape (quad, "A"), ==, 0);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (quad, "A"), ==, 0.5f);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_offsets (quad, "A", NULL)[0], ==, 9.0f);

	/* a vertex added later gets a zero offset in every shape */
	g_assert_cmpint (lrg_skinned_sprite_add_vertex (quad, 20, 20, 1, 1), ==, 4);
	got = lrg_skinned_sprite_get_shape_offsets (quad, "A", &n);
	g_assert_cmpuint (n, ==, 10);
	g_assert_cmpfloat (got[8], ==, 0.0f);
	g_assert_cmpfloat (got[9], ==, 0.0f);

	/* copies are deep and keep weights */
	copy = lrg_skinned_sprite_copy (quad);
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (copy), ==, 2);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (copy, "A"), ==, 0.5f);
	g_assert_true (lrg_skinned_sprite_get_shape_offsets (copy, "A", NULL) !=
	               lrg_skinned_sprite_get_shape_offsets (quad, "A", NULL));
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_offsets (copy, "A", NULL)[0], ==, 9.0f);

	/* remove; clear drops the rest */
	g_assert_true (lrg_skinned_sprite_remove_shape (quad, "A"));
	g_assert_false (lrg_skinned_sprite_remove_shape (quad, "A"));
	g_assert_cmpint (lrg_skinned_sprite_find_shape (quad, "blink_l"), ==, 0);
	lrg_skinned_sprite_clear (quad);
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (quad), ==, 0);
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (copy), ==, 2);
}

/* Shapes morph the bind positions before skinning (so the offset turns
 * with the bone), mix additively, and the skeleton hands each slot's
 * weights to the attachment it shows. */
static void
test_shape_deform (void)
{
	LrgSkinnedSprite         *strip = NULL;
	g_autoptr(LrgSkeleton2D)  skel = make_arm (&strip);
	g_autofree gfloat        *up = NULL;
	g_autofree gfloat        *wide = NULL;
	const gfloat             *pos;
	guint                     nv;
	gfloat                    x;
	gfloat                    y;

	/* vertex 10 is the top-right corner (100, -5), fully on "fore" */
	nv = lrg_skinned_sprite_get_vertex_count (strip);
	lrg_skinned_sprite_get_vertex (strip, 10, &x, &y, NULL, NULL);
	g_assert_cmpfloat (x, ==, 100.0f);
	g_assert_cmpfloat (y, ==, -5.0f);
	up = g_new0 (gfloat, nv * 2);
	wide = g_new0 (gfloat, nv * 2);
	up[10 * 2 + 1] = 4.0f;
	wide[10 * 2] = 2.0f;
	g_assert_true (lrg_skinned_sprite_add_shape (strip, "up", up, nv * 2));
	g_assert_true (lrg_skinned_sprite_add_shape (strip, "wide", wide, nv * 2));

	/* bend the forearm 90 degrees: (x, y) on fore -> (50 - y, x - 50) */
	lrg_skeleton2d_set_bone_local (skel, 1, 50, 0, 90, 1, 1);
	lrg_skeleton2d_update_world (skel);

	/* weight 0: plain skinning */
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[20], 55.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[21], 50.0f, EPS);

	/* "up" at 1 through the slot: (100, -1) skinned -> (51, 50); applied
	 * after skinning it would have been (55, 54) */
	lrg_skeleton2d_set_shape_weight (skel, 0, "up", 1.0f);
	lrg_skeleton2d_deform (skel);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (strip, "up"), ==, 1.0f);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[20], 51.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[21], 50.0f, EPS);

	/* shapes add: up 0.5 + wide 1 -> (102, -3) -> (53, 52) */
	lrg_skeleton2d_set_shape_weight (skel, 0, "up", 0.5f);
	lrg_skeleton2d_set_shape_weight (skel, 0, "wide", 1.0f);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[20], 53.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[21], 52.0f, EPS);
	/* other vertices did not move off their skinned place */
	g_assert_cmpfloat_with_epsilon (pos[0], 0.0f, EPS);
	g_assert_cmpfloat_with_epsilon (pos[1], -5.0f, EPS);

	/* slot weights clamp to 0 .. 1; shapes the attachment lacks and bad
	 * slots are a no-op; non-finite weights are ignored */
	lrg_skeleton2d_set_shape_weight (skel, 0, "wide", 7.0f);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 0, "wide"), ==, 1.0f);
	lrg_skeleton2d_set_shape_weight (skel, 0, "wide", -1.0f);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 0, "wide"), ==, 0.0f);
	lrg_skeleton2d_set_shape_weight (skel, 0, "wide", NAN);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 0, "wide"), ==, 0.0f);
	lrg_skeleton2d_set_shape_weight (skel, 0, LRG_SHAPE_SHOUT, 1.0f);
	lrg_skeleton2d_set_shape_weight (skel, 7, "up", 1.0f);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 7, "up"), ==, 0.0f);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 0, "never"), ==, 0.0f);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[20], 53.0f, EPS);    /* up 0.5 -> (100, -3) */
	g_assert_cmpfloat_with_epsilon (pos[21], 50.0f, EPS);

	/* reset_pose zeroes the weights, and the zero reaches the sprite */
	lrg_skeleton2d_reset_pose (skel);
	g_assert_cmpfloat (lrg_skeleton2d_get_shape_weight (skel, 0, "up"), ==, 0.0f);
	lrg_skeleton2d_set_bone_local (skel, 1, 50, 0, 90, 1, 1);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (strip, "up"), ==, 0.0f);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[20], 55.0f, EPS);

	/* an unweighted vertex (default bone branch) and no palette at all
	 * (bind branch) morph too */
	{
		g_autoptr(LrgSkinnedSprite) quad = lrg_skinned_sprite_new_quad (60, -5, 10, 10, -1);
		gfloat                      d[8] = { 0, 2, 0, 0, 0, 0, 0, 0 };
		const gfloat               *p;
		guint                       np = 0;

		const gfloat               *pal = lrg_skeleton2d_get_palette (skel, &np);

		g_assert_true (lrg_skinned_sprite_add_shape (quad, "s", d, 8));
		lrg_skinned_sprite_set_shape_weight (quad, "s", 1.0f);
		lrg_skinned_sprite_deform (quad, pal, np, 1);
		p = lrg_skinned_sprite_get_deformed (quad, NULL);
		/* (60, -3) on fore -> (53, 10) */
		g_assert_cmpfloat_with_epsilon (p[0], 53.0f, EPS);
		g_assert_cmpfloat_with_epsilon (p[1], 10.0f, EPS);
		lrg_skinned_sprite_deform (quad, NULL, 0, -1);
		p = lrg_skinned_sprite_get_deformed (quad, NULL);
		g_assert_cmpfloat_with_epsilon (p[0], 60.0f, EPS);
		g_assert_cmpfloat_with_epsilon (p[1], -3.0f, EPS);
	}
}

/* Weight timelines: keys, curves, sampling, listing, applying with a mix
 * weight. */
static void
test_shape_clip (void)
{
	g_autoptr(LrgSkeleton2D)     skel = make_arm (NULL);
	g_autoptr(LrgSkeleton2DClip) clip = lrg_skeleton2d_clip_new ("talk");
	g_auto(GStrv)                shapes = NULL;
	g_auto(GStrv)                slots = NULL;
	gfloat                       w;
	gfloat                       t;
	LrgSkeleton2DCurve           curve;

	lrg_skeleton2d_clip_add_shape_key (clip, "arm", "A", 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_shape_key (clip, "arm", "A", 1.0f, 1.0f, LRG_SKELETON2D_CURVE_STEPPED);
	lrg_skeleton2d_clip_add_shape_key (clip, "arm", "A", 2.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_shape_key (clip, "arm", "O", 0.5f, 3.0f, LRG_SKELETON2D_CURVE_LINEAR);   /* clamped */
	g_assert_cmpfloat (lrg_skeleton2d_clip_get_duration (clip), ==, 2.0f);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (clip, "arm", "A"), ==, 3);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (clip, "arm", "nope"), ==, 0);
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (clip, "hand", "A"), ==, 0);
	g_assert_true (lrg_skeleton2d_clip_get_shape_key (clip, "arm", "O", 0, &t, &w, &curve));
	g_assert_cmpfloat (w, ==, 1.0f);
	g_assert_false (lrg_skeleton2d_clip_get_shape_key (clip, "arm", "O", 1, NULL, NULL, NULL));

	/* the generic (target, channel) functions see no shape keys */
	g_assert_cmpuint (lrg_skeleton2d_clip_get_key_count (clip, "arm", LRG_SKELETON2D_CHANNEL_SHAPE), ==, 0);
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_SKELETON2D_CHANNEL);

		g_assert_cmpstr (g_enum_get_value (klass, LRG_SKELETON2D_CHANNEL_SHAPE)->value_nick, ==, "shape");
	}

	/* listing */
	shapes = lrg_skeleton2d_clip_get_shapes (clip, "arm");
	g_assert_cmpuint (g_strv_length (shapes), ==, 2);
	g_assert_cmpstr (shapes[0], ==, "A");
	g_assert_cmpstr (shapes[1], ==, "O");
	slots = lrg_skeleton2d_clip_get_targets (clip, TRUE);
	g_assert_cmpstr (slots[0], ==, "arm");

	/* sampling: linear, stepped hold, clamped ends */
	g_assert_true (lrg_skeleton2d_clip_sample_shape (clip, "arm", "A", 0.25f, &w));
	g_assert_cmpfloat_with_epsilon (w, 0.25f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "arm", "A", 1.75f, &w);
	g_assert_cmpfloat_with_epsilon (w, 1.0f, EPS);
	lrg_skeleton2d_clip_sample_shape (clip, "arm", "A", 9.0f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.0f, EPS);
	g_assert_false (lrg_skeleton2d_clip_sample_shape (clip, "arm", "nope", 0.5f, &w));
	g_assert_cmpfloat (w, ==, 0.0f);

	/* a Bézier ease-in-out is half way at the middle, slower at 1/4 */
	g_assert_true (lrg_skeleton2d_clip_set_shape_key_bezier (clip, "arm", "A", 0, 0.42f, 0.0f, 0.58f, 1.0f));
	g_assert_false (lrg_skeleton2d_clip_set_shape_key_bezier (clip, "arm", "A", 9, 0, 0, 1, 1));
	g_assert_true (lrg_skeleton2d_clip_get_shape_key (clip, "arm", "A", 0, NULL, NULL, &curve));
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_BEZIER);
	lrg_skeleton2d_clip_sample_shape (clip, "arm", "A", 0.5f, &w);
	g_assert_cmpfloat_with_epsilon (w, 0.5f, 1e-3f);
	lrg_skeleton2d_clip_sample_shape (clip, "arm", "A", 0.25f, &w);
	g_assert_cmpfloat (w, <, 0.2f);

	/* applying: alpha mixes from the current weight */
	lrg_skeleton2d_add_clip (skel, clip);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (clip, skel, 1.5f, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, "A"), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, "O"), 1.0f, EPS);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_set_shape_weight (skel, 0, "A", 0.5f);
	lrg_skeleton2d_clip_apply (clip, skel, 1.5f, 0.25f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, "A"), 0.625f, EPS);

	/* removing keys */
	g_assert_true (lrg_skeleton2d_clip_remove_shape_key (clip, "arm", "O", 0));
	g_assert_false (lrg_skeleton2d_clip_remove_shape_key (clip, "arm", "O", 0));
	g_assert_cmpuint (lrg_skeleton2d_clip_get_shape_key_count (clip, "arm", "O"), ==, 0);
	g_assert_false (lrg_skeleton2d_clip_sample_shape (clip, "arm", "O", 0.5f, &w));
}

/* A component crossfade mixes shape weights like every other channel. */
static void
test_shape_crossfade (void)
{
	LrgSkinnedSprite                 *strip = NULL;
	g_autoptr(LrgSkeleton2D)          skel = make_arm (&strip);
	g_autoptr(LrgSkeleton2DClip)      calm = lrg_skeleton2d_clip_new ("calm");
	g_autoptr(LrgSkeleton2DClip)      shout = lrg_skeleton2d_clip_new ("shout");
	g_autoptr(LrgSkeleton2DComponent) comp = NULL;
	g_autofree gfloat                *off = NULL;
	guint                             nv = lrg_skinned_sprite_get_vertex_count (strip);
	const gfloat                     *pos;

	/* vertex 0 (0, -5) moves up 10 with the shout shape */
	off = g_new0 (gfloat, nv * 2);
	off[1] = -10.0f;
	g_assert_true (lrg_skinned_sprite_add_shape (strip, LRG_SHAPE_SHOUT, off, nv * 2));

	lrg_skeleton2d_clip_add_shape_key (calm, "arm", LRG_SHAPE_SHOUT, 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_shape_key (calm, "arm", LRG_SHAPE_HAPPY, 0.0f, 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_set_duration (calm, 1.0f);
	lrg_skeleton2d_clip_add_shape_key (shout, "arm", LRG_SHAPE_SHOUT, 0.0f, 0.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_clip_add_shape_key (shout, "arm", LRG_SHAPE_SHOUT, 1.0f, 1.0f, LRG_SKELETON2D_CURVE_LINEAR);
	lrg_skeleton2d_add_clip (skel, calm);
	lrg_skeleton2d_add_clip (skel, shout);

	comp = lrg_skeleton2d_component_new_with_skeleton (skel);
	g_assert_true (lrg_skeleton2d_component_play (comp, "calm", TRUE));
	lrg_component_update (LRG_COMPONENT (comp), 0.25f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, LRG_SHAPE_HAPPY), 1.0f, EPS);

	/* fade calm (shout 0, happy 1) into shout (0 -> 1 over 1 s) over 1 s:
	 * at 0.5 s shout samples 0.5 and mixes at 0.5 over 0 -> 0.25; happy
	 * is only keyed by calm, so it stays at calm's 1 */
	g_assert_true (lrg_skeleton2d_component_crossfade (comp, "shout", 1.0f, FALSE));
	lrg_component_update (LRG_COMPONENT (comp), 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, LRG_SHAPE_SHOUT), 0.25f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, LRG_SHAPE_HAPPY), 1.0f, EPS);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[1], -7.5f, EPS);

	/* fade done: shout's own weight */
	lrg_component_update (LRG_COMPONENT (comp), 0.6f);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_component_get_mix_weight (comp), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, LRG_SHAPE_SHOUT), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_skeleton2d_get_shape_weight (skel, 0, LRG_SHAPE_HAPPY), 0.0f, EPS);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (strip, NULL);
	g_assert_cmpfloat_with_epsilon (pos[1], -15.0f, EPS);
}

/* ==========================================================================
 * Automatic weights
 * ========================================================================== */

static void
test_auto_weights (void)
{
	g_autoptr(LrgSkeleton2D)    skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite) strip = lrg_skinned_sprite_new_grid (0, -5, 100, 10, 20, 2);
	guint                       pass;

	lrg_skeleton2d_add_bone (skel, "a", -1, 0, 0, 0, 1, 1, 50);
	lrg_skeleton2d_add_bone (skel, "b", 0, 50, 0, 0, 1, 1, 50);

	for (pass = 0; pass < 2; pass++)
	{
		guint i;

		/* without and with smoothing */
		g_assert_true (lrg_skeleton2d_compute_auto_weights (skel, strip, NULL, 0, 3.0f, 4, pass * 3));
		for (i = 0; i < lrg_skinned_sprite_get_vertex_count (strip); i++)
		{
			gint   b[4];
			gfloat w[4];
			gfloat x;
			gfloat sum = 0.0f;
			guint  n = lrg_skinned_sprite_get_weights (strip, i, b, w);
			guint  k;

			lrg_skinned_sprite_get_vertex (strip, i, &x, NULL, NULL, NULL);
			g_assert_cmpuint (n, >=, 1);
			g_assert_cmpuint (n, <=, 2);
			for (k = 0; k < n; k++)
				sum += w[k];
			g_assert_cmpfloat_with_epsilon (sum, 1.0f, 1e-4f);
			/* the nearest bone dominates away from the joint */
			if (x < 40.0f)
				g_assert_cmpint (b[0], ==, 0);
			if (x > 60.0f)
				g_assert_cmpint (b[0], ==, 1);
			if (x <= 10.0f || x >= 90.0f)
				g_assert_cmpfloat (w[0], >, 0.85f);
		}
	}

	/* max_influences 1 gives rigid weights; bad input is refused */
	g_assert_true (lrg_skeleton2d_compute_auto_weights (skel, strip, NULL, 0, 2.0f, 1, 0));
	g_assert_cmpuint (lrg_skinned_sprite_get_weights (strip, 0, NULL, NULL), ==, 1);
	{
		gint bad[1] = { 7 };

		g_assert_false (lrg_skeleton2d_compute_auto_weights (skel, strip, bad, 1, 2.0f, 1, 0));
	}
	g_assert_false (lrg_skinned_sprite_compute_auto_weights (strip, NULL, 0, NULL, 0, 2.0f, 1, 0));
}

/* ==========================================================================
 * Format
 * ========================================================================== */

/* The writer's output for a small skeleton: pins the format byte for
 * byte (every section and key kind appears). */
static const gchar *fixture =
	"rskel2d: 1\n"
	"name: arm\n"
	"bones:\n"
	"  - name: upper\n"
	"    length: 50\n"
	"  - name: fore\n"
	"    parent: upper\n"
	"    x: 50\n"
	"    rotation: -12.5\n"
	"    scale: [1.5, 1]\n"
	"    length: 50\n"
	"slots:\n"
	"  - name: arm\n"
	"    bone: upper\n"
	"    attachment: arm\n"
	"    color: [1, 0.5, 0.25, 1]\n"
	"  - name: hand\n"
	"    bone: fore\n"
	"skins:\n"
	"  - name: default\n"
	"    attachments:\n"
	"      - slot: arm\n"
	"        name: arm\n"
	"        image: limbs/arm.png\n"
	"        region: [0, 16, 64, 16]\n"
	"        vertices:\n"
	"          - [0, -5, 0, 0]\n"
	"          - [100, -5, 1, 0]\n"
	"          - [0, 5, 0, 1]\n"
	"          - [100, 5, 1, 1]\n"
	"        weights:\n"
	"          - [upper, 1]\n"
	"          - [fore, 0.7, upper, 0.3]\n"
	"          - []\n"
	"          - [fore, 1]\n"
	"        triangles:\n"
	"          - [0, 1, 2]\n"
	"          - [1, 3, 2]\n"
	"  - name: \"gold skin\"\n"
	"    attachments:\n"
	"      - slot: hand\n"
	"        name: fist\n"
	"        vertices:\n"
	"          - [95, -4, 0, 0]\n"
	"          - [105, -4, 1, 0]\n"
	"          - [100, 4, 0.5, 1]\n"
	"        triangles:\n"
	"          - [0, 1, 2]\n"
	"animations:\n"
	"  - name: wave\n"
	"    duration: 1.5\n"
	"    loop: true\n"
	"    bones:\n"
	"      - bone: fore\n"
	"        translate:\n"
	"          - {time: 0, x: 0, y: 0}\n"
	"          - {time: 1, x: 2, y: -1.25, curve: stepped}\n"
	"        rotate:\n"
	"          - {time: 0, angle: 0, curve: [0.42, 0, 0.58, 1]}\n"
	"          - {time: 0.5, angle: 45}\n"
	"        scale:\n"
	"          - {time: 0.25, x: 1, y: 1.1}\n"
	"    slots:\n"
	"      - slot: hand\n"
	"        attachment:\n"
	"          - {time: 0, name: fist}\n"
	"          - {time: 1, name: null}\n"
	"        color:\n"
	"          - {time: 0.5, color: [1, 1, 1, 0.5]}\n"
	"    draw_order:\n"
	"      - {time: 0.5, order: [hand, arm]}\n"
	"      - {time: 1.5, order: null}\n"
	"    events:\n"
	"      - {time: 0.5, name: \"wave hello\"}\n"
	"  - name: idle\n";

static void
test_format_fixture (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgSkeleton2D) skel = lrg_skeleton2d_new_from_data (fixture, -1, &error);
	g_autofree gchar        *text = NULL;
	LrgSkeleton2DClip       *wave;
	LrgSkinnedSprite        *arm;
	gint                     b[4];
	gfloat                   w[4];
	gfloat                   r[4];
	gfloat                   t;
	gfloat                   v[4];
	LrgSkeleton2DCurve       curve;
	const gchar             *att;
	const gchar * const     *order;

	g_assert_no_error (error);
	g_assert_nonnull (skel);
	text = lrg_skeleton2d_save_to_data (skel, NULL);
	g_assert_cmpstr (text, ==, fixture);

	/* field by field */
	g_assert_cmpstr (lrg_skeleton2d_get_name (skel), ==, "arm");
	g_assert_cmpuint (lrg_skeleton2d_get_bone_count (skel), ==, 2);
	g_assert_cmpuint (lrg_skeleton2d_get_skin_count (skel), ==, 2);
	g_assert_cmpstr (lrg_skeleton2d_get_skin_name (skel, 1), ==, "gold skin");
	g_assert_null (lrg_skeleton2d_get_slot_setup_attachment (skel, 1));
	arm = lrg_skeleton2d_get_skin_attachment (skel, NULL, 0, "arm");
	g_assert_nonnull (arm);
	g_assert_cmpstr (lrg_skinned_sprite_get_image (arm), ==, "limbs/arm.png");
	g_assert_true (lrg_skinned_sprite_get_region (arm, &r[0], &r[1], &r[2], &r[3]));
	g_assert_cmpfloat (r[1], ==, 16.0f);
	g_assert_cmpuint (lrg_skinned_sprite_get_weights (arm, 1, b, w), ==, 2);
	g_assert_cmpint (b[0], ==, 1);
	g_assert_cmpfloat (w[0], ==, 0.7f);
	g_assert_cmpuint (lrg_skinned_sprite_get_weights (arm, 2, b, w), ==, 0);
	wave = lrg_skeleton2d_get_clip (skel, "wave");
	g_assert_true (lrg_skeleton2d_clip_get_loop (wave));
	g_assert_cmpfloat (lrg_skeleton2d_clip_get_duration (wave), ==, 1.5f);
	g_assert_true (lrg_skeleton2d_clip_get_key (wave, "fore", LRG_SKELETON2D_CHANNEL_TRANSLATE, 1, &t, v, &curve));
	g_assert_cmpfloat (v[1], ==, -1.25f);
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_STEPPED);
	g_assert_true (lrg_skeleton2d_clip_get_attachment_key (wave, "hand", 1, &t, &att));
	g_assert_null (att);
	order = lrg_skeleton2d_clip_get_draw_order_key (wave, 0, &t);
	g_assert_cmpstr (order[0], ==, "hand");
	g_assert_null (lrg_skeleton2d_clip_get_draw_order_key (wave, 1, NULL));
	g_assert_cmpstr (lrg_skeleton2d_clip_get_event (wave, 0, &t), ==, "wave hello");
	g_assert_cmpuint (lrg_skeleton2d_clip_get_event_count (lrg_skeleton2d_get_clip (skel, "idle")), ==, 0);
}

/* A version 2 document: blend shapes on an attachment (sparse offsets, an
 * empty shape) and shape weight keys with curves.  The writer emits it
 * byte for byte. */
static const gchar *fixture_v2 =
	"rskel2d: 2\n"
	"name: face\n"
	"bones:\n"
	"  - name: head\n"
	"    length: 40\n"
	"slots:\n"
	"  - name: face\n"
	"    bone: head\n"
	"    attachment: face\n"
	"skins:\n"
	"  - name: default\n"
	"    attachments:\n"
	"      - slot: face\n"
	"        name: face\n"
	"        vertices:\n"
	"          - [0, 0, 0, 0]\n"
	"          - [10, 0, 1, 0]\n"
	"          - [0, 10, 0, 1]\n"
	"        weights:\n"
	"          - [head, 1]\n"
	"          - [head, 1]\n"
	"          - [head, 1]\n"
	"        triangles:\n"
	"          - [0, 1, 2]\n"
	"        shapes:\n"
	"          - name: blink_l\n"
	"            offsets:\n"
	"              - [0, 0, 2.5]\n"
	"              - [2, -1, 0.5]\n"
	"          - name: A\n"
	"            offsets: []\n"
	"animations:\n"
	"  - name: talk\n"
	"    duration: 1\n"
	"    slots:\n"
	"      - slot: face\n"
	"        shapes:\n"
	"          - shape: A\n"
	"            keys:\n"
	"              - {time: 0, w: 0}\n"
	"              - {time: 0.25, w: 1, curve: [0.42, 0, 0.58, 1]}\n"
	"              - {time: 1, w: 0, curve: stepped}\n"
	"          - shape: blink_l\n"
	"            keys:\n"
	"              - {time: 0.5, w: 0.75}\n";

static void
test_format_v2 (void)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(LrgSkeleton2D) skel = lrg_skeleton2d_new_from_data (fixture_v2, -1, &error);
	g_autoptr(LrgSkeleton2D) back = NULL;
	g_autofree gchar        *text = NULL;
	g_autofree gchar        *text2 = NULL;
	g_autofree gchar        *v1 = NULL;
	LrgSkeleton2DClip       *talk;
	LrgSkinnedSprite        *face;
	const gfloat            *off;
	guint                    n;
	gfloat                   w;
	LrgSkeleton2DCurve       curve;

	g_assert_no_error (error);
	g_assert_nonnull (skel);
	text = lrg_skeleton2d_save_to_data (skel, NULL);
	g_assert_cmpstr (text, ==, fixture_v2);

	/* field by field */
	face = lrg_skeleton2d_get_skin_attachment (skel, NULL, 0, "face");
	g_assert_cmpuint (lrg_skinned_sprite_get_shape_count (face), ==, 2);
	off = lrg_skinned_sprite_get_shape_offsets (face, "blink_l", &n);
	g_assert_cmpuint (n, ==, 6);
	g_assert_cmpfloat (off[0], ==, 0.0f);
	g_assert_cmpfloat (off[1], ==, 2.5f);
	g_assert_cmpfloat (off[2], ==, 0.0f);
	g_assert_cmpfloat (off[3], ==, 0.0f);
	g_assert_cmpfloat (off[4], ==, -1.0f);
	g_assert_cmpfloat (off[5], ==, 0.5f);
	talk = lrg_skeleton2d_get_clip (skel, "talk");
	g_assert_true (lrg_skeleton2d_clip_get_shape_key (talk, "face", "A", 1, NULL, &w, &curve));
	g_assert_cmpfloat (w, ==, 1.0f);
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_BEZIER);
	g_assert_true (lrg_skeleton2d_clip_get_shape_key (talk, "face", "A", 2, NULL, NULL, &curve));
	g_assert_cmpint (curve, ==, LRG_SKELETON2D_CURVE_STEPPED);

	/* load / save again: same bytes */
	back = lrg_skeleton2d_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	text2 = lrg_skeleton2d_save_to_data (back, NULL);
	g_assert_cmpstr (text2, ==, fixture_v2);

	/* without the version 2 features the writer falls back to version 1 */
	g_assert_true (lrg_skinned_sprite_remove_shape (face, "blink_l"));
	g_assert_true (lrg_skinned_sprite_remove_shape (face, "A"));
	lrg_skeleton2d_remove_clip (skel, "talk");
	v1 = lrg_skeleton2d_save_to_data (skel, NULL);
	g_assert_true (g_str_has_prefix (v1, "rskel2d: 1\n"));
	g_assert_null (strstr (v1, "shapes"));

	/* the shapes of a loaded character drive its mesh through a clip */
	lrg_skeleton2d_reset_pose (back);
	lrg_skeleton2d_clip_apply (lrg_skeleton2d_get_clip (back, "talk"), back, 0.5f, 1.0f);
	lrg_skeleton2d_update_world (back);
	lrg_skeleton2d_deform (back);
	off = lrg_skinned_sprite_get_deformed (lrg_skeleton2d_get_skin_attachment (back, NULL, 0, "face"), NULL);
	g_assert_cmpfloat_with_epsilon (off[1], 0.75f * 2.5f, EPS);
	g_assert_cmpfloat_with_epsilon (off[4], -0.75f, EPS);
}

static void
test_format_round_trip (void)
{
	g_autoptr(LrgSkeleton2D) skel = make_figure ();
	g_autoptr(LrgSkeleton2D) back = NULL;
	g_autoptr(LrgSkeleton2D) from_file = NULL;
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *text1 = NULL;
	g_autofree gchar        *text2 = NULL;
	g_autofree gchar        *text3 = NULL;
	g_autofree gchar        *dir = NULL;
	g_autofree gchar        *path = NULL;
	g_autofree gchar        *base = NULL;
	gsize                    len;
	gfloat                   rot_a;
	gfloat                   rot_b;

	/* save, load, save: same bytes */
	text1 = lrg_skeleton2d_save_to_data (skel, &len);
	g_assert_cmpuint (len, ==, strlen (text1));
	back = lrg_skeleton2d_new_from_data (text1, (gssize)len, &error);
	g_assert_no_error (error);
	text2 = lrg_skeleton2d_save_to_data (back, NULL);
	g_assert_cmpstr (text1, ==, text2);

	/* the reloaded skeleton poses like the original */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (lrg_skeleton2d_get_clip (skel, "walk"), skel, 0.3f, 1.0f);
	lrg_skeleton2d_clip_apply (lrg_skeleton2d_get_clip (back, "walk"), back, 0.3f, 1.0f);
	lrg_skeleton2d_get_bone_local (skel, 9, NULL, NULL, &rot_a, NULL, NULL);
	lrg_skeleton2d_get_bone_local (back, 9, NULL, NULL, &rot_b, NULL, NULL);
	g_assert_cmpfloat (rot_a, ==, rot_b);

	/* through a file: base-dir is the file's directory */
	dir = g_dir_make_tmp ("lrg-skel2d-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "figure.rskel2d", NULL);
	g_assert_true (lrg_skeleton2d_save_to_file (back, path, &error));
	g_assert_no_error (error);
	from_file = lrg_skeleton2d_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_skeleton2d_get_base_dir (from_file), ==, dir);
	text3 = lrg_skeleton2d_save_to_data (from_file, NULL);
	g_assert_cmpstr (text1, ==, text3);
	g_unlink (path);
	g_rmdir (dir);

	base = g_build_filename (g_get_tmp_dir (), "lrg-no-such-dir", "x.rskel2d", NULL);
	g_assert_null (lrg_skeleton2d_new_from_file (base, &error));
	g_assert_error (error, LRG_SKELETON2D_ERROR, LRG_SKELETON2D_ERROR_IO);
}

typedef struct
{
	const gchar *doc;
	gint         code;
	const gchar *message;
} BadDoc;

static void
test_format_errors (void)
{
	static const BadDoc cases[] = {
		{ "bones: []\n", LRG_SKELETON2D_ERROR_VERSION, "<data>: /: missing \"rskel2d: 2\"" },
		{ "rskel2d: 3\n", LRG_SKELETON2D_ERROR_VERSION, "/rskel2d: unsupported version 3 (this library reads 1 to 2)" },
		{ "rskel2d: 1.5\n", LRG_SKELETON2D_ERROR_VERSION, "/rskel2d: unsupported version 1.5" },
		{ "rskel2d: 0\n", LRG_SKELETON2D_ERROR_VERSION, "/rskel2d: unsupported version 0" },
		/* version 2 features in a version 1 document */
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        shapes: []\n", LRG_SKELETON2D_ERROR_VERSION,
		  "<data>: /skins/0/attachments/0/shapes: a blend shape needs \"rskel2d: 2\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes: []\n", LRG_SKELETON2D_ERROR_VERSION,
		  "/animations/0/slots/0/shapes: a shape weight timeline needs \"rskel2d: 2\"" },
		/* version 2 shapes */
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        shapes:\n          - name: x\n            offsets:\n              - [1, 0, 0]\n",
		  LRG_SKELETON2D_ERROR_INVALID, "/skins/0/attachments/0/shapes/0/offsets/0/0: expected a vertex index from 0 to 0" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        shapes:\n          - name: x\n            offsets:\n              - [0, 1]\n",
		  LRG_SKELETON2D_ERROR_PARSE, "/skins/0/attachments/0/shapes/0/offsets/0: expected [vertex, dx, dy]" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        shapes:\n          - name: x\n            offsets:\n              - [0, 1, 1]\n"
		  "              - [0, 2, 2]\n",
		  LRG_SKELETON2D_ERROR_INVALID, "/skins/0/attachments/0/shapes/0/offsets/1/0: vertex 0 listed twice" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        shapes:\n          - name: x\n"
		  "          - name: x\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/skins/0/attachments/0/shapes/1/name: duplicate shape \"x\"" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        shapes:\n          - name: x\n"
		  "            weight: 1\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/skins/0/attachments/0/shapes/0: unknown key \"weight\"" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes:\n          - keys: []\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/slots/0/shapes/0: missing key \"shape\"" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes:\n          - shape: A\n            keys:\n"
		  "              - {time: 0, w: 1.5}\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/slots/0/shapes/0/keys/0/w: a shape weight must be from 0 to 1" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes:\n          - shape: A\n            keys:\n"
		  "              - {time: 0}\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/slots/0/shapes/0/keys/0: missing key \"w\"" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes:\n          - shape: A\n            keys:\n"
		  "              - {time: 0, weight: 1}\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/slots/0/shapes/0/keys/0: unknown key \"weight\"" },
		{ "rskel2d: 2\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nanimations:\n  - name: w\n"
		  "    slots:\n      - slot: s\n        shapes:\n          - shape: A\n          - shape: A\n",
		  LRG_SKELETON2D_ERROR_INVALID, "/animations/0/slots/0/shapes/1/shape: duplicate shape \"A\"" },
		{ "rskel2d: 1\nbone: []\n", LRG_SKELETON2D_ERROR_INVALID, "/: unknown key \"bone\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\n    parent: b\n  - name: b\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/bones/0/parent: unknown bone \"b\" (a parent must come before its children)" },
		{ "rskel2d: 1\nbones:\n  - name: a\n  - name: a\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/bones/1/name: duplicate bone \"a\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\n    x: left\n", LRG_SKELETON2D_ERROR_PARSE,
		  "/bones/0/x: expected a number, got \"left\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\n    scale: [1]\n", LRG_SKELETON2D_ERROR_PARSE,
		  "/bones/0/scale: expected a list of 2 numbers" },
		{ "rskel2d: 1\nbones:\n  - x: 1\n", LRG_SKELETON2D_ERROR_INVALID, "/bones/0: missing key \"name\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: z\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/slots/0/bone: unknown bone \"z\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        triangles:\n          - [0, 0, 3]\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/skins/0/attachments/0/triangles/0/2: expected a vertex index from 0 to 0" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        weights:\n          - [q, 1]\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/skins/0/attachments/0/weights/0/0: unknown bone \"q\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\nskins:\n  - name: default\n"
		  "    attachments:\n      - slot: s\n        name: m\n        vertices:\n          - [0, 0, 0, 0]\n"
		  "        weights: []\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/skins/0/attachments/0/weights: expected one row per vertex (1), got 0" },
		{ "rskel2d: 1\nanimations:\n  - name: w\n    bones:\n      - bone: nope\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/bones/0/bone: unknown bone \"nope\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nanimations:\n  - name: w\n    bones:\n      - bone: a\n"
		  "        rotate:\n          - {time: -1, angle: 3}\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/bones/0/rotate/0/time: a time must be 0 or more" },
		{ "rskel2d: 1\nbones:\n  - name: a\nanimations:\n  - name: w\n    bones:\n      - bone: a\n"
		  "        rotate:\n          - {time: 0, angle: 3, curve: wobbly}\n", LRG_SKELETON2D_ERROR_PARSE,
		  "/animations/0/bones/0/rotate/0/curve: expected linear, stepped or [x1, y1, x2, y2]" },
		{ "rskel2d: 1\nbones:\n  - name: a\nanimations:\n  - name: w\n    bones:\n      - bone: a\n"
		  "        rotate:\n          - {time: 0, angel: 3}\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/0/bones/0/rotate/0: unknown key \"angel\"" },
		{ "rskel2d: 1\nbones:\n  - name: a\nslots:\n  - name: s\n    bone: a\n  - name: t\n    bone: a\n"
		  "animations:\n  - name: w\n    draw_order:\n      - {time: 0, order: [s, s]}\n",
		  LRG_SKELETON2D_ERROR_INVALID, "/animations/0/draw_order/0/order/1: slot \"s\" named twice" },
		{ "rskel2d: 1\nanimations:\n  - name: w\n  - name: w\n", LRG_SKELETON2D_ERROR_INVALID,
		  "/animations/1/name: duplicate animation \"w\"" },
		{ "rskel2d: 1\nanimations:\n  - name: w\n    loop: maybe\n", LRG_SKELETON2D_ERROR_PARSE,
		  "/animations/0/loop: expected true or false" },
		{ "rskel2d: [1\n", LRG_SKELETON2D_ERROR_PARSE, "<data>: " },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)        error = NULL;
		g_autoptr(LrgSkeleton2D) skel = lrg_skeleton2d_new_from_data (cases[i].doc, -1, &error);

		if (g_test_verbose ())
			g_test_message ("case %u: %s", i, error != NULL ? error->message : "(no error)");
		g_assert_null (skel);
		g_assert_error (error, LRG_SKELETON2D_ERROR, cases[i].code);
		g_assert_nonnull (strstr (error->message, cases[i].message));
	}
}

/* ==========================================================================
 * Performance (reported, not asserted)
 * ========================================================================== */

static void
test_perf (void)
{
	g_autoptr(LrgSkeleton2D) skel = make_figure ();
	LrgSkeleton2DClip       *walk = lrg_skeleton2d_get_clip (skel, "walk");
	g_autoptr(LrgSkinnedSprite) big = lrg_skinned_sprite_new_grid (-50, -50, 100, 100, 31, 31);
	g_autoptr(GTimer)        timer = g_timer_new ();
	const guint              iters = 2000;
	guint                    i;
	guint                    n_pal;
	const gfloat            *pal;
	gdouble                  pose_us;
	gdouble                  skin_us;
	gdouble                  figure_us;

	/* 1024-vertex grid with four influences per vertex */
	g_assert_true (lrg_skeleton2d_compute_auto_weights (skel, big, NULL, 0, 3.0f, 4, 0));

	g_timer_start (timer);
	for (i = 0; i < iters; i++)
	{
		lrg_skeleton2d_reset_pose (skel);
		lrg_skeleton2d_clip_apply (walk, skel, (gfloat)(i % 100) / 100.0f, 1.0f);
		lrg_skeleton2d_update_world (skel);
	}
	pose_us = g_timer_elapsed (timer, NULL) * 1e6 / iters;

	pal = lrg_skeleton2d_get_palette (skel, &n_pal);
	g_timer_start (timer);
	for (i = 0; i < iters; i++)
		lrg_skinned_sprite_deform (big, pal, n_pal, 0);
	skin_us = g_timer_elapsed (timer, NULL) * 1e6 / iters;

	g_timer_start (timer);
	for (i = 0; i < iters; i++)
		lrg_skeleton2d_deform (skel);
	figure_us = g_timer_elapsed (timer, NULL) * 1e6 / iters;

	g_test_message ("pose (12 bones, walk clip: reset + apply + update_world): %.2f us", pose_us);
	g_test_message ("skin 1024 vertices x 4 influences: %.2f us (%.1f ns/vertex)", skin_us, skin_us * 1000.0 / 1024.0);
	g_test_message ("deform the figure (6 slots, %u vertices): %.2f us", 4u * 26u + 8u, figure_us);
	if (g_getenv ("LRG_SKEL2D_PERF") != NULL)
		g_printerr ("perf: pose %.2f us, skin1024x4 %.2f us, figure %.2f us\n", pose_us, skin_us, figure_us);
}

/* ==========================================================================
 * CPU: lrg_skeleton2d_draw_to_canvas() draws without a window
 * ========================================================================== */

/* Saves @img when LRG_TEST_SHOTS names a directory (visual checks). */
static void
maybe_save_cpu (GrlImage    *img,
                const gchar *name)
{
	const gchar      *dir = g_getenv ("LRG_TEST_SHOTS");
	g_autofree gchar *path = NULL;

	if (dir == NULL || *dir == '\0')
		return;
	path = g_strdup_printf ("%s/%s.png", dir, name);
	grl_image_export (img, path);
}

/* The RGB of pixel (x, y) of @img is within 6 of (r, g, b). */
static gboolean
cpu_pixel_is (GrlImage *img,
              gint      x,
              gint      y,
              guint8    r,
              guint8    g,
              guint8    b)
{
	const guint8 *p = grl_image_get_pixels (img, NULL);

	p += ((gsize)y * (gsize)grl_image_get_width (img) + (gsize)x) * 4;
	return ABS ((gint)p[0] - r) <= 6 && ABS ((gint)p[1] - g) <= 6 && ABS ((gint)p[2] - b) <= 6;
}

/* A black canvas */
static LrgImageCanvas *
black_canvas (gint w,
              gint h)
{
	GrlColor black = { 0, 0, 0, 255 };

	return lrg_image_canvas_new (w, h, &black);
}

/* The arm of the GPU test drawn on the CPU: straight, then bent by the
 * "bend" clip; the forearm pixels move with the bone. */
static void
test_cpu_arm (void)
{
	g_autoptr(LrgSkeleton2D)     skel = make_arm (NULL);
	g_autoptr(LrgSkeleton2DClip) bend = make_bend_clip ();
	g_autoptr(LrgImageCanvas)    straight = black_canvas (240, 200);
	g_autoptr(LrgImageCanvas)    bent = black_canvas (240, 200);
	GrlImage                    *img;

	lrg_skeleton2d_set_slot_setup_color (skel, 0, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_set_root_transform (skel, 100.0f, 60.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, straight, NULL);
	img = lrg_image_canvas_get_image (straight);
	maybe_save_cpu (img, "s2d-cpu-arm-straight");
	/* the strip covers x 100 .. 200, y 55 .. 65 */
	g_assert_true (cpu_pixel_is (img, 125, 60, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 185, 60, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 100, 55, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 199, 64, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 99, 60, 0, 0, 0));
	g_assert_true (cpu_pixel_is (img, 200, 60, 0, 0, 0));
	g_assert_true (cpu_pixel_is (img, 150, 54, 0, 0, 0));
	g_assert_true (cpu_pixel_is (img, 150, 65, 0, 0, 0));

	/* bend the forearm 90 degrees: it now hangs down from (150, 60) */
	lrg_skeleton2d_clip_apply (bend, skel, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, bent, NULL);
	img = lrg_image_canvas_get_image (bent);
	maybe_save_cpu (img, "s2d-cpu-arm-bent");
	g_assert_true (cpu_pixel_is (img, 125, 60, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 150, 95, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 185, 60, 0, 0, 0));   /* the straight forearm is gone */
}

/*
 * Slot colours tint, the draw tint multiplies them, draw order decides
 * which of two overlapping slots is on top, a transparent slot is
 * skipped, and the canvas transform places the skeleton like a camera.
 */
static void
test_cpu_tint_and_order (void)
{
	g_autoptr(LrgSkeleton2D)    skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite) back = lrg_skinned_sprite_new_quad (0, 0, 20, 20, -1);
	g_autoptr(LrgSkinnedSprite) front = lrg_skinned_sprite_new_quad (10, 10, 20, 20, -1);
	g_autoptr(LrgImageCanvas)   c1 = black_canvas (64, 64);
	g_autoptr(LrgImageCanvas)   c2 = black_canvas (64, 64);
	g_autoptr(LrgImageCanvas)   c3 = black_canvas (64, 64);
	g_autoptr(LrgImageCanvas)   c4 = black_canvas (64, 64);
	GrlColor                    grey = { 128, 128, 128, 255 };
	gint                        bone;
	gint                        sb;
	gint                        sf;
	gint                        reversed[2];

	bone = lrg_skeleton2d_add_bone (skel, "root", -1, 0, 0, 0, 1, 1, 0);
	sb = lrg_skeleton2d_add_slot (skel, "back", bone, "q");
	sf = lrg_skeleton2d_add_slot (skel, "front", bone, "q");
	g_assert_true (lrg_skeleton2d_set_attachment (skel, NULL, sb, "q", back));
	g_assert_true (lrg_skeleton2d_set_attachment (skel, NULL, sf, "q", front));
	lrg_skeleton2d_set_slot_setup_color (skel, sb, 1.0f, 0.0f, 0.0f, 1.0f);
	lrg_skeleton2d_set_slot_setup_color (skel, sf, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_set_root_transform (skel, 10.0f, 10.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);

	/* setup order: back (red) under front (blue) */
	lrg_skeleton2d_draw_to_canvas (skel, c1, NULL);
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c1), 15, 15, 255, 0, 0));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c1), 25, 25, 0, 0, 255));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c1), 35, 35, 0, 0, 255));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c1), 45, 45, 0, 0, 0));

	/* reversed order puts red on top in the overlap; the grey tint halves
	 * every slot colour */
	reversed[0] = sf;
	reversed[1] = sb;
	g_assert_true (lrg_skeleton2d_set_draw_order (skel, reversed, 2));
	lrg_skeleton2d_draw_to_canvas (skel, c2, &grey);
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c2), 25, 25, 128, 0, 0));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c2), 35, 35, 0, 0, 128));

	/* a slot at alpha 0 is not drawn; at alpha 0.5 it blends */
	lrg_skeleton2d_set_slot_color (skel, sb, 1.0f, 0.0f, 0.0f, 0.0f);
	lrg_skeleton2d_set_slot_color (skel, sf, 0.0f, 0.0f, 1.0f, 0.5f);
	lrg_skeleton2d_draw_to_canvas (skel, c3, NULL);
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c3), 15, 15, 0, 0, 0));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c3), 25, 25, 0, 0, 128));

	/* the canvas transform moves the whole skeleton like a 2D camera */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_update_world (skel);
	lrg_image_canvas_translate (c4, 20.0f, 0.0f);
	lrg_skeleton2d_draw_to_canvas (skel, c4, NULL);
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c4), 15, 15, 0, 0, 0));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c4), 35, 15, 255, 0, 0));
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (c4), 55, 35, 0, 0, 255));
}

/* The walking figure with a generated checker atlas on the head: both
 * checker colours show inside the head box, the body is drawn, and the
 * region picks the atlas cell (the blink cell is another region). */
static void
test_cpu_figure (void)
{
	g_autoptr(LrgSkeleton2D)    skel = make_figure ();
	g_autoptr(GrlImage)         faces = NULL;
	g_autoptr(LrgImageCanvas)   canvas = NULL;
	g_autoptr(LrgSkinnedSprite) copy = NULL;
	GrlColor                    c1 = { 250, 225, 190, 255 };
	GrlColor                    c2 = { 60, 40, 30, 255 };
	GrlColor                    bg = { 24, 26, 32, 255 };
	GrlImage                   *img;
	LrgSkeleton2DClip          *walk;
	LrgSkinnedSprite           *head_sprite;
	gint                        head;
	guint                       lit = 0;
	guint                       dark = 0;
	gint                        x;
	gint                        y;

	faces = grl_image_new_checked (64, 32, 8, 4, &c1, &c2);
	head = lrg_skeleton2d_find_slot (skel, "head");
	head_sprite = lrg_skeleton2d_get_skin_attachment (skel, NULL, head, "head");
	lrg_skinned_sprite_set_source_image (head_sprite, faces);
	lrg_skinned_sprite_set_source_image (lrg_skeleton2d_get_skin_attachment (skel, NULL, head, "blink"), faces);
	g_assert_true (lrg_skinned_sprite_get_source_image (head_sprite) == faces);

	/* copies share the CPU source */
	copy = lrg_skinned_sprite_copy (head_sprite);
	g_assert_true (lrg_skinned_sprite_get_source_image (copy) == faces);

	walk = lrg_skeleton2d_get_clip (skel, "walk");
	g_assert_nonnull (walk);
	canvas = lrg_image_canvas_new (440, 280, &bg);
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_clip_apply (walk, skel, 0.3f, 1.0f);
	lrg_skeleton2d_set_root_transform (skel, 90.0f, 170.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, canvas, NULL);
	img = lrg_image_canvas_get_image (canvas);
	maybe_save_cpu (img, "s2d-cpu-figure");

	/* the torso is around the hip; the head samples the checker,
	 * tinted by the slot colour */
	g_assert_false (cpu_pixel_is (img, 90, 140, 24, 26, 32));
	for (y = 80; y < 110; y++)
	{
		for (x = 80; x < 100; x++)
		{
			const guint8 *p = grl_image_get_pixels (img, NULL) + ((gsize)y * 440 + (gsize)x) * 4;

			if (p[0] > 180 && p[1] > 140)
				lit++;
			else if (p[0] > 35 && p[0] < 90 && p[2] < 60)
				dark++;
		}
	}
	g_assert_cmpuint (lit, >, 30);
	g_assert_cmpuint (dark, >, 30);
}

/* An atlas region: the sprite samples only its cell, and bilinear
 * filtering clamps at the cell border instead of bleeding the
 * neighbouring cell in. */
static void
test_cpu_region (void)
{
	g_autoptr(LrgSkinnedSprite) quad = lrg_skinned_sprite_new_quad (0, 0, 16, 16, -1);
	g_autoptr(GrlImage)         atlas = NULL;
	g_autoptr(LrgImageCanvas)   canvas = black_canvas (16, 16);
	GrlColor                    red = { 255, 0, 0, 255 };
	GrlColor                    green = { 0, 255, 0, 255 };
	GrlImage                   *img;
	guint8                     *p;
	gint                        x;
	gint                        y;

	/* a 4x2 atlas: red cell on the left, green cell on the right */
	atlas = grl_image_new_color (4, 2, &red);
	p = grl_image_get_pixels (atlas, NULL);
	for (y = 0; y < 2; y++)
		for (x = 2; x < 4; x++)
			memcpy (p + (y * 4 + x) * 4, &green, 4);

	lrg_skinned_sprite_set_source_image (quad, atlas);
	lrg_skinned_sprite_set_region (quad, 2, 0, 2, 2);
	lrg_skinned_sprite_draw_to_canvas (quad, canvas, NULL);
	img = lrg_image_canvas_get_image (canvas);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			g_assert_true (cpu_pixel_is (img, x, y, 0, 255, 0));

	/* no region: the whole atlas, red on the left half */
	lrg_skinned_sprite_set_region (quad, 0, 0, 0, 0);
	lrg_skinned_sprite_draw_to_canvas (quad, canvas, NULL);
	g_assert_true (cpu_pixel_is (img, 1, 8, 255, 0, 0));
	g_assert_true (cpu_pixel_is (img, 14, 8, 0, 255, 0));
}

/*
 * Blend shapes reach the CPU path exactly as the GPU path: the weights
 * live on the slot and only get to the sprite through the skeleton's
 * per-slot deform, so lrg_skeleton2d_draw_to_canvas() must go through it
 * too.  A 2x2 grid mesh on a rotated bone has a shape that pushes its
 * right column 16 units outwards; at slot weight 1 the CPU render
 * covers pixels the weight 0 render leaves black, and the positions it
 * drew are the ones lrg_skeleton2d_deform() computes.
 */
static void
test_cpu_shapes (void)
{
	g_autoptr(LrgSkeleton2D)    skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite) grid = lrg_skinned_sprite_new_grid (0, 0, 20, 20, 2, 2);
	g_autoptr(LrgImageCanvas)   rest = black_canvas (64, 64);
	g_autoptr(LrgImageCanvas)   open = black_canvas (64, 64);
	g_autofree gfloat          *push = NULL;
	g_autofree gfloat          *drawn = NULL;
	GrlColor                    black = { 0, 0, 0, 255 };
	const gfloat               *pos;
	GrlImage                   *a;
	GrlImage                   *b;
	guint                       nv;
	guint                       i;
	gint                        bone;
	gint                        slot;

	/* the mesh sits on one bone that the pose turns 90 degrees, placed
	 * at (40, 10): setup (x, y) lands on canvas (40 - y, 10 + x) */
	bone = lrg_skeleton2d_add_bone (skel, "head", -1, 0, 0, 0, 1, 1, 0);
	slot = lrg_skeleton2d_add_slot (skel, "face", bone, "mesh");
	g_assert_true (lrg_skeleton2d_set_attachment (skel, NULL, slot, "mesh", grid));
	lrg_skeleton2d_set_slot_setup_color (skel, slot, 1.0f, 0.0f, 0.0f, 1.0f);

	/* the shape moves every vertex of the right column (x = 20) by +16 x */
	nv = lrg_skinned_sprite_get_vertex_count (grid);
	push = g_new0 (gfloat, nv * 2);
	for (i = 0; i < nv; i++)
	{
		gfloat x;
		gfloat y;

		lrg_skinned_sprite_get_vertex (grid, i, &x, &y, NULL, NULL);
		if (x == 20.0f)
			push[i * 2] = 16.0f;
	}
	g_assert_true (lrg_skinned_sprite_add_shape (grid, LRG_SHAPE_STRETCH, push, nv * 2));

	/* weight 0 */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_set_bone_local (skel, bone, 0, 0, 90, 1, 1);
	lrg_skeleton2d_set_root_transform (skel, 40.0f, 10.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, rest, NULL);
	a = lrg_image_canvas_get_image (rest);
	maybe_save_cpu (a, "s2d-cpu-shape-rest");

	/* weight 1, set on the slot only (never on the sprite directly) */
	lrg_skeleton2d_set_shape_weight (skel, slot, LRG_SHAPE_STRETCH, 1.0f);
	lrg_skeleton2d_draw_to_canvas (skel, open, NULL);
	b = lrg_image_canvas_get_image (open);
	maybe_save_cpu (b, "s2d-cpu-shape-stretch");
	g_assert_cmpfloat (lrg_skinned_sprite_get_shape_weight (grid, LRG_SHAPE_STRETCH), ==, 1.0f);

	/* rest covers canvas y 10 .. 30; the stretch reaches y 46 */
	g_assert_true (cpu_pixel_is (a, 30, 20, 255, 0, 0));
	g_assert_true (cpu_pixel_is (a, 30, 38, 0, 0, 0));
	g_assert_true (cpu_pixel_is (b, 30, 20, 255, 0, 0));
	g_assert_true (cpu_pixel_is (b, 30, 38, 255, 0, 0));
	g_assert_true (cpu_pixel_is (b, 30, 44, 255, 0, 0));
	g_assert_true (cpu_pixel_is (b, 30, 48, 0, 0, 0));
	g_assert_true (memcmp (grl_image_get_pixels (a, NULL), grl_image_get_pixels (b, NULL), 64 * 64 * 4) != 0);

	/* the CPU draw left exactly the deform's positions in the sprite */
	pos = lrg_skinned_sprite_get_deformed (grid, NULL);
	drawn = g_memdup2 (pos, nv * 2 * sizeof (gfloat));
	lrg_skeleton2d_deform (skel);
	pos = lrg_skinned_sprite_get_deformed (grid, NULL);
	for (i = 0; i < nv * 2; i++)
		g_assert_cmpfloat (drawn[i], ==, pos[i]);
	for (i = 0; i < nv; i++)
	{
		gfloat x;
		gfloat y;

		lrg_skinned_sprite_get_vertex (grid, i, &x, &y, NULL, NULL);
		if (x == 20.0f)
			g_assert_cmpfloat_with_epsilon (pos[i * 2 + 1], 10.0f + 36.0f, EPS);
	}

	/* back to weight 0 through a pose reset: the render matches rest */
	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_set_bone_local (skel, bone, 0, 0, 90, 1, 1);
	lrg_skeleton2d_update_world (skel);
	lrg_image_canvas_clear (open, &black);
	lrg_skeleton2d_draw_to_canvas (skel, open, NULL);
	g_assert_true (memcmp (grl_image_get_pixels (a, NULL), grl_image_get_pixels (lrg_image_canvas_get_image (open), NULL), 64 * 64 * 4) == 0);
}

/* Image paths load on the CPU, relative to the skeleton's base
 * directory: lazily on the first draw, or up front with errors. */
static void
test_cpu_source_files (void)
{
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *dir = g_dir_make_tmp ("lrg-s2d-cpu-XXXXXX", &error);
	g_autofree gchar           *png = NULL;
	g_autoptr(GrlImage)         blue = NULL;
	g_autoptr(LrgSkeleton2D)    skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkeleton2D)    skel2 = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite) quad = lrg_skinned_sprite_new_quad (0, 0, 8, 8, -1);
	g_autoptr(LrgSkinnedSprite) quad2 = lrg_skinned_sprite_new_quad (0, 0, 8, 8, -1);
	g_autoptr(LrgSkinnedSprite) missing = lrg_skinned_sprite_new_quad (0, 0, 8, 8, -1);
	g_autoptr(LrgImageCanvas)   canvas = black_canvas (8, 8);
	GrlColor                    c = { 20, 40, 220, 255 };
	gint                        bone;
	gint                        slot;

	g_assert_no_error (error);
	png = g_build_filename (dir, "blue.png", NULL);
	blue = grl_image_new_color (4, 4, &c);
	g_assert_true (grl_image_export (blue, png));

	/* lazy: the first draw loads blue.png from the base directory */
	bone = lrg_skeleton2d_add_bone (skel, "root", -1, 0, 0, 0, 1, 1, 0);
	slot = lrg_skeleton2d_add_slot (skel, "s", bone, "q");
	lrg_skinned_sprite_set_image (quad, "blue.png");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "q", quad);
	lrg_skeleton2d_set_base_dir (skel, dir);
	lrg_skeleton2d_update_world (skel);
	lrg_skeleton2d_draw_to_canvas (skel, canvas, NULL);
	g_assert_true (cpu_pixel_is (lrg_image_canvas_get_image (canvas), 4, 4, 20, 40, 220));
	g_assert_nonnull (lrg_skinned_sprite_get_source_image (quad));

	/* up front: both sprites naming blue.png share one decoded image */
	bone = lrg_skeleton2d_add_bone (skel2, "root", -1, 0, 0, 0, 1, 1, 0);
	slot = lrg_skeleton2d_add_slot (skel2, "s", bone, "q");
	lrg_skinned_sprite_set_image (quad2, "blue.png");
	lrg_skinned_sprite_set_image (missing, "blue.png");
	lrg_skeleton2d_set_attachment (skel2, NULL, slot, "q", quad2);
	lrg_skeleton2d_set_attachment (skel2, NULL, slot, "r", missing);
	lrg_skeleton2d_set_base_dir (skel2, dir);
	g_assert_true (lrg_skeleton2d_load_source_images (skel2, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_skinned_sprite_get_source_image (quad2) == lrg_skinned_sprite_get_source_image (missing));

	/* a missing file is an error from the explicit loader */
	lrg_skinned_sprite_set_source_image (missing, NULL);
	lrg_skinned_sprite_set_image (missing, "nope.png");
	g_assert_false (lrg_skeleton2d_load_source_images (skel2, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

	g_unlink (png);
	g_rmdir (dir);
}

/* ==========================================================================
 * GPU: a component in an LrgWorld draws a bending arm; a walking figure
 * ========================================================================== */

/* Saves @img when LRG_TEST_SHOTS names a directory (visual checks). */
static void
maybe_save (Image        img,
            const gchar *name)
{
	const gchar      *dir = g_getenv ("LRG_TEST_SHOTS");
	g_autofree gchar *path = NULL;

	if (dir == NULL || *dir == '\0')
		return;
	path = g_strdup_printf ("%s/%s.png", dir, name);
	ExportImage (img, path);
}

static gboolean
pixel_is (Image  img,
          gint   x,
          gint   y,
          guint8 r,
          guint8 g,
          guint8 b)
{
	const guint8 *p = (const guint8 *)img.data + ((gsize)y * (gsize)img.width + (gsize)x) * 4;

	return ABS ((gint)p[0] - r) <= 6 && ABS ((gint)p[1] - g) <= 6 && ABS ((gint)p[2] - b) <= 6;
}

static void
test_gpu_world_arm (void)
{
	g_autoptr(LrgWorld)               world = NULL;
	g_autoptr(LrgSkeleton2D)          skel = NULL;
	g_autoptr(LrgSkeleton2DClip)      bend = NULL;
	g_autoptr(LrgSkeleton2DComponent) comp = NULL;
	LrgGameObject                    *obj;
	RenderTexture2D                   rt;
	Image                             img;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	skel = make_arm (NULL);
	bend = make_bend_clip ();
	lrg_skeleton2d_add_clip (skel, bend);
	lrg_skeleton2d_set_slot_setup_color (skel, 0, 1.0f, 0.0f, 0.0f, 1.0f);

	world = lrg_world_new ();
	obj = lrg_game_object_new_at (100.0f, 60.0f);
	comp = lrg_skeleton2d_component_new_with_skeleton (skel);
	lrg_game_object_add_component (obj, LRG_COMPONENT (comp));
	lrg_world_add_object (world, obj);
	g_assert_true (lrg_skeleton2d_component_play (comp, "bend", FALSE));
	lrg_world_update (world, 1.0f);   /* the clip ends: forearm at 90 degrees */

	rt = LoadRenderTexture (240, 200);
	BeginTextureMode (rt);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_world_draw (world);   /* the game object draws its skeleton component */
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	ImageFlipVertical (&img);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	maybe_save (img, "m34-skel2d-arm");

	/* upper arm along +X from (100, 60); forearm turned down from
	 * (150, 60) to (150, 110) */
	g_assert_true (pixel_is (img, 125, 60, 255, 0, 0));
	g_assert_true (pixel_is (img, 150, 95, 255, 0, 0));
	g_assert_true (pixel_is (img, 185, 60, 0, 0, 0));   /* the straight forearm is gone */
	UnloadImage (img);
	UnloadRenderTexture (rt);
}

static void
test_gpu_figure (void)
{
	g_autoptr(LrgWorld)               world = NULL;
	g_autoptr(LrgSkeleton2D)          skel = make_figure ();
	g_autoptr(LrgSkeleton2DComponent) comp = NULL;
	g_autoptr(GrlImage)               faces = NULL;
	g_autoptr(GrlTexture)             tex = NULL;
	GrlColor                          c1 = { 250, 225, 190, 255 };
	GrlColor                          c2 = { 60, 40, 30, 255 };
	LrgGameObject                    *objs[3];
	RenderTexture2D                   rt;
	Image                             img;
	gint                              head;
	guint                             i;
	guint                             lit = 0;
	guint                             dark = 0;
	gint                              x;
	gint                              y;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	/* the head attachments sample a generated 64x32 checker atlas */
	faces = grl_image_new_checked (64, 32, 8, 4, &c1, &c2);
	tex = grl_texture_new_from_image (faces);
	head = lrg_skeleton2d_find_slot (skel, "head");
	lrg_skinned_sprite_set_texture (lrg_skeleton2d_get_skin_attachment (skel, NULL, head, "head"), tex);
	lrg_skinned_sprite_set_texture (lrg_skeleton2d_get_skin_attachment (skel, NULL, head, "blink"), tex);

	/* three figures: walking (mid-stride), walking mirrored and bigger,
	 * crossfading from walk to wave */
	world = lrg_world_new ();
	for (i = 0; i < 3; i++)
	{
		g_autoptr(LrgSkeleton2D) copy = NULL;
		g_autofree gchar        *text = lrg_skeleton2d_save_to_data (skel, NULL);
		LrgSkeleton2DComponent  *c;

		copy = i == 0 ? g_object_ref (skel) : lrg_skeleton2d_new_from_data (text, -1, NULL);
		lrg_skinned_sprite_set_texture (lrg_skeleton2d_get_skin_attachment (copy, NULL, head, "head"), tex);
		lrg_skinned_sprite_set_texture (lrg_skeleton2d_get_skin_attachment (copy, NULL, head, "blink"), tex);
		objs[i] = lrg_game_object_new_at (90.0f + 150.0f * (gfloat)i, 170.0f);
		c = lrg_skeleton2d_component_new_with_skeleton (copy);
		g_object_set (c, "show-bones", i == 0, "flip-x", i == 1, "scale", i == 1 ? 1.2f : 1.0f, NULL);
		lrg_game_object_add_component (objs[i], LRG_COMPONENT (c));
		lrg_world_add_object (world, objs[i]);
		lrg_skeleton2d_component_play (c, "walk", TRUE);
		if (i == 2)
		{
			lrg_world_update (world, 0.2f);
			lrg_skeleton2d_component_crossfade (c, "wave", 0.6f, FALSE);
		}
		if (i == 0)
			comp = g_object_ref (c);
		g_object_unref (c);
	}
	lrg_world_update (world, 0.3f);

	rt = LoadRenderTexture (440, 280);
	BeginTextureMode (rt);
	ClearBackground ((Color){ 24, 26, 32, 255 });
	DrawRectangle (0, 250, 440, 30, (Color){ 60, 70, 60, 255 });
	lrg_world_draw (world);
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	ImageFlipVertical (&img);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	maybe_save (img, "m34-skel2d-figure");

	/* the first figure's torso (orange) is around its hip, the head
	 * checker shows both checker colours */
	g_assert_false (pixel_is (img, 90, 140, 24, 26, 32));
	/* the head quad samples the checker (tinted by the slot colour):
	 * both bright and dark checks show inside its box */
	for (y = 80; y < 110; y++)
	{
		for (x = 80; x < 100; x++)
		{
			const guint8 *p = (const guint8 *)img.data + ((gsize)y * (gsize)img.width + (gsize)x) * 4;

			if (p[0] > 180 && p[1] > 140)
				lit++;
			else if (p[0] > 35 && p[0] < 90 && p[2] < 60)
				dark++;
		}
	}
	g_assert_cmpuint (lit, >, 30);
	g_assert_cmpuint (dark, >, 30);
	g_assert_cmpfloat (lrg_skeleton2d_component_get_time (comp), >, 0.29f);
	UnloadImage (img);
	UnloadRenderTexture (rt);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/skeleton2d/hierarchy", test_hierarchy);
	g_test_add_func ("/skeleton2d/skinning", test_skinning);
	g_test_add_func ("/skeleton2d/clip-sampling", test_clip_sampling);
	g_test_add_func ("/skeleton2d/crossfade", test_crossfade);
	g_test_add_func ("/skeleton2d/auto-weights", test_auto_weights);
	g_test_add_func ("/skeleton2d/shapes/api", test_shape_api);
	g_test_add_func ("/skeleton2d/shapes/deform", test_shape_deform);
	g_test_add_func ("/skeleton2d/shapes/clip", test_shape_clip);
	g_test_add_func ("/skeleton2d/shapes/crossfade", test_shape_crossfade);
	g_test_add_func ("/skeleton2d/format/fixture", test_format_fixture);
	g_test_add_func ("/skeleton2d/format/v2", test_format_v2);
	g_test_add_func ("/skeleton2d/format/round-trip", test_format_round_trip);
	g_test_add_func ("/skeleton2d/format/errors", test_format_errors);
	g_test_add_func ("/skeleton2d/perf", test_perf);
	g_test_add_func ("/skeleton2d/cpu/arm", test_cpu_arm);
	g_test_add_func ("/skeleton2d/cpu/tint-and-order", test_cpu_tint_and_order);
	g_test_add_func ("/skeleton2d/cpu/figure", test_cpu_figure);
	g_test_add_func ("/skeleton2d/cpu/region", test_cpu_region);
	g_test_add_func ("/skeleton2d/cpu/source-files", test_cpu_source_files);
	g_test_add_func ("/skeleton2d/cpu/shapes", test_cpu_shapes);
	g_test_add_func ("/skeleton2d/gpu/world-arm", test_gpu_world_arm);
	g_test_add_func ("/skeleton2d/gpu/figure", test_gpu_figure);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
