/* test-skeleton2d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for 2D skeletal animation: LrgSkeleton2D (hierarchy, world
 * transforms, skins, slots, draw order), LrgSkinnedSprite (CPU skinning,
 * automatic weights), LrgSkeleton2DClip (sampling, curves, slot swaps,
 * draw order, events), the .rskel2d format (fixture, byte-stable round
 * trip, strict errors) and LrgSkeleton2DComponent (crossfades, events,
 * drawing a bending arm and a walking figure under a GPU).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
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
		{ "bones: []\n", LRG_SKELETON2D_ERROR_VERSION, "<data>: /: missing \"rskel2d: 1\"" },
		{ "rskel2d: 2\n", LRG_SKELETON2D_ERROR_VERSION, "/rskel2d: unsupported version 2" },
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
	g_test_add_func ("/skeleton2d/format/fixture", test_format_fixture);
	g_test_add_func ("/skeleton2d/format/round-trip", test_format_round_trip);
	g_test_add_func ("/skeleton2d/format/errors", test_format_errors);
	g_test_add_func ("/skeleton2d/perf", test_perf);
	g_test_add_func ("/skeleton2d/gpu/world-arm", test_gpu_world_arm);
	g_test_add_func ("/skeleton2d/gpu/figure", test_gpu_figure);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
