/* test-world-ui.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for world-space UI (LrgWorldWidget, LrgSpeechBubble,
 * LrgNameplate, LrgEmote) and lrg_camera3d_project_point().  All
 * headless: text is measured with the 0.6 x size estimate, which the
 * expected wraps below are computed from.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>
#include <raylib.h>

/* ==========================================================================
 * Projection
 * ========================================================================== */

static void
test_project_perspective (void)
{
	g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();
	GrlVector3 p;
	GrlVector2 s;
	gfloat depth = 0.0f;

	lrg_camera3d_set_position_xyz (camera, 0.0f, 0.0f, 10.0f);
	lrg_camera3d_set_target_xyz (camera, 0.0f, 0.0f, 0.0f);
	lrg_camera3d_set_up_xyz (camera, 0.0f, 1.0f, 0.0f);
	lrg_camera3d_set_fovy (camera, 45.0f);

	p.x = 0.0f; p.y = 0.0f; p.z = 0.0f;
	g_assert_true (lrg_camera3d_project_point (camera, &p, 800, 600, &s, &depth));
	g_assert_cmpfloat_with_epsilon (s.x, 400.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (s.y, 300.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (depth, 10.0f, 1e-4f);

	/* up in the world is up on screen (smaller y) */
	p.y = 1.0f;
	g_assert_true (lrg_camera3d_project_point (camera, &p, 800, 600, &s, NULL));
	g_assert_cmpfloat (s.y, <, 300.0f);

	/* behind the camera */
	p.x = 0.0f; p.y = 0.0f; p.z = 20.0f;
	g_assert_false (lrg_camera3d_project_point (camera, &p, 800, 600, &s, &depth));
	g_assert_cmpfloat (depth, <, 0.0f);
}

/* raylib's own GetWorldToScreenEx() is pure math: use it as the reference */
static void
test_project_matches_raylib (void)
{
	g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();
	Camera3D rl;
	guint i;
	static const gfloat points[][3] = {
		{ 0.0f, 0.0f, 0.0f }, { 1.5f, 2.0f, -3.0f }, { -4.0f, 0.5f, 2.0f },
		{ 3.0f, -1.0f, 1.0f }, { 0.25f, 6.0f, -8.0f }
	};

	lrg_camera3d_set_position_xyz (camera, 6.0f, 8.0f, 12.0f);
	lrg_camera3d_set_target_xyz (camera, 0.0f, 1.0f, 0.0f);
	lrg_camera3d_set_up_xyz (camera, 0.0f, 1.0f, 0.0f);
	lrg_camera3d_set_fovy (camera, 50.0f);

	rl.position = (Vector3) { 6.0f, 8.0f, 12.0f };
	rl.target = (Vector3) { 0.0f, 1.0f, 0.0f };
	rl.up = (Vector3) { 0.0f, 1.0f, 0.0f };
	rl.fovy = 50.0f;
	rl.projection = CAMERA_PERSPECTIVE;

	for (i = 0; i < G_N_ELEMENTS (points); i++)
	{
		GrlVector3 p = { points[i][0], points[i][1], points[i][2] };
		Vector3 rp = { points[i][0], points[i][1], points[i][2] };
		Vector2 ref = GetWorldToScreenEx (rp, rl, 1280, 720);
		GrlVector2 s;

		g_assert_true (lrg_camera3d_project_point (camera, &p, 1280, 720, &s, NULL));
		g_assert_cmpfloat_with_epsilon (s.x, ref.x, 0.05f);
		g_assert_cmpfloat_with_epsilon (s.y, ref.y, 0.05f);
	}

	/* orthographic: fovy is the view height */
	lrg_camera3d_set_projection (camera, LRG_PROJECTION_ORTHOGRAPHIC);
	lrg_camera3d_set_position_xyz (camera, 0.0f, 0.0f, 10.0f);
	lrg_camera3d_set_target_xyz (camera, 0.0f, 0.0f, 0.0f);
	lrg_camera3d_set_fovy (camera, 10.0f);
	{
		GrlVector3 p = { 5.0f, 5.0f, 0.0f };
		GrlVector2 s;

		g_assert_true (lrg_camera3d_project_point (camera, &p, 800, 600, &s, NULL));
		g_assert_cmpfloat_with_epsilon (s.x, 700.0f, 0.05f);
		g_assert_cmpfloat_with_epsilon (s.y, 0.0f, 0.05f);
	}
}

/* ==========================================================================
 * Speech bubble
 * ========================================================================== */

static void
test_bubble_wrap (void)
{
	g_autoptr(LrgSpeechBubble) bubble = NULL;
	gfloat w;
	gfloat h;

	/* 16 px text measures 9.6 px a character headless; 220 px fits 22 */
	bubble = lrg_speech_bubble_new ("The quick brown fox jumps over the lazy dog");
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 3);
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 0), ==, "The quick brown fox");
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 1), ==, "jumps over the lazy");
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 2), ==, "dog");
	g_assert_null (lrg_speech_bubble_get_line (bubble, 3));

	lrg_world_widget_measure (LRG_WORLD_WIDGET (bubble), &w, &h);
	g_assert_cmpfloat_with_epsilon (w, 19 * 9.6f + 16.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (h, 3 * 20.0f + 16.0f + 10.0f, 1e-3f);

	/* a word wider than a line is broken between characters */
	lrg_speech_bubble_set_text (bubble, "Supercalifragilisticexpialidocious!!");
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 2);
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 0), ==, "Supercalifragilisticex");
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 1), ==, "pialidocious!!");

	/* paragraphs, including an empty one */
	lrg_speech_bubble_set_text (bubble, "one\n\ntwo");
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 3);
	g_assert_cmpstr (lrg_speech_bubble_get_line (bubble, 1), ==, "");

	/* a narrower bubble, and scale rewraps */
	lrg_speech_bubble_set_text (bubble, "aaaa bbbb cccc");
	lrg_speech_bubble_set_max_width (bubble, 50.0f);
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 3);
	lrg_world_widget_set_scale (LRG_WORLD_WIDGET (bubble), 2.0f);
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 3);
	lrg_speech_bubble_set_max_width (bubble, 100.0f);
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 2);

	lrg_speech_bubble_set_text (bubble, NULL);
	g_assert_cmpstr (lrg_speech_bubble_get_text (bubble), ==, "");
	g_assert_cmpuint (lrg_speech_bubble_get_n_lines (bubble), ==, 0);
}

static void
on_hidden (LrgWorldWidget *widget,
           gpointer        user_data)
{
	(*(guint *) user_data)++;
}

static void
test_bubble_fade_and_hold (void)
{
	g_autoptr(LrgSpeechBubble) bubble = lrg_speech_bubble_new ("hi");
	LrgWorldWidget *w = LRG_WORLD_WIDGET (bubble);
	guint hidden = 0;

	g_signal_connect (w, "hidden", G_CALLBACK (on_hidden), &hidden);
	g_assert_cmpfloat (lrg_world_widget_get_alpha (w), ==, 1.0f);
	lrg_world_widget_hide (w, 0.0f);
	g_assert_cmpfloat (lrg_world_widget_get_alpha (w), ==, 0.0f);
	g_assert_cmpuint (hidden, ==, 1);
	g_assert_false (lrg_world_widget_is_shown (w));

	lrg_speech_bubble_say (bubble, "hello there", 1.0f);
	g_assert_true (lrg_world_widget_is_shown (w));
	lrg_world_widget_update (w, 0.075f);
	g_assert_cmpfloat_with_epsilon (lrg_world_widget_get_alpha (w), 0.5f, 1e-4f);
	lrg_world_widget_update (w, 0.1f);
	g_assert_cmpfloat (lrg_world_widget_get_alpha (w), ==, 1.0f);

	/* held for a second, then fades out over 0.4 s */
	lrg_world_widget_update (w, 0.5f);
	g_assert_true (lrg_world_widget_is_shown (w));
	lrg_world_widget_update (w, 0.6f);
	g_assert_false (lrg_world_widget_is_shown (w));
	g_assert_cmpfloat (lrg_world_widget_get_alpha (w), ==, 0.0f);
	g_assert_cmpuint (hidden, ==, 2);

	/* hiding something already hidden emits nothing */
	lrg_world_widget_hide (w, 0.2f);
	g_assert_cmpuint (hidden, ==, 2);
}

static void
test_box_and_hit (void)
{
	g_autoptr(LrgSpeechBubble) bubble = lrg_speech_bubble_new ("hey");
	LrgWorldWidget *w = LRG_WORLD_WIDGET (bubble);
	GrlRectangle box;
	gfloat bw;
	gfloat bh;

	lrg_world_widget_measure (w, &bw, &bh);
	lrg_world_widget_get_box_at (w, 100.0f, 200.0f, &box);
	g_assert_cmpfloat_with_epsilon (box.x, 100.0f - bw * 0.5f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (box.y, 200.0f - 8.0f - bh, 1e-4f);

	g_assert_false (lrg_world_widget_get_last_box (w, &box));
	lrg_world_widget_set_offset (w, 10.0f, 0.0f);
	lrg_world_widget_draw_at (w, 100.0f, 200.0f);   /* records the box headless */
	g_assert_true (lrg_world_widget_get_last_box (w, &box));
	g_assert_cmpfloat_with_epsilon (box.x, 110.0f - bw * 0.5f, 1e-4f);
	g_assert_true (lrg_world_widget_contains_point (w, 110.0f, 195.0f));
	g_assert_false (lrg_world_widget_contains_point (w, 110.0f, 205.0f));

	lrg_world_widget_hide (w, 0.0f);
	g_assert_false (lrg_world_widget_contains_point (w, 110.0f, 195.0f));
}

static void
test_draw_with_cameras (void)
{
	g_autoptr(LrgEmote) emote = lrg_emote_new_glyph ("!");
	g_autoptr(LrgCamera2D) cam2d = lrg_camera2d_new ();
	g_autoptr(LrgCamera3D) cam3d = lrg_camera3d_new ();
	LrgWorldWidget *w = LRG_WORLD_WIDGET (emote);
	GrlVector2 screen;
	GrlRectangle box;
	gfloat x;
	gfloat y;
	gfloat z;

	lrg_world_widget_set_anchor (w, 10.0f, 5.0f, -2.0f);
	lrg_world_widget_get_anchor (w, &x, &y, &z);
	g_assert_cmpfloat (z, ==, -2.0f);

	lrg_camera2d_set_offset_xy (cam2d, 400.0f, 300.0f);
	lrg_camera2d_set_target_xy (cam2d, 0.0f, 0.0f);
	lrg_camera2d_set_zoom (cam2d, 2.0f);
	lrg_world_widget_project_2d (w, cam2d, &screen);
	g_assert_cmpfloat_with_epsilon (screen.x, 420.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (screen.y, 310.0f, 1e-3f);
	lrg_world_widget_draw_2d (w, cam2d);
	g_assert_true (lrg_world_widget_get_last_box (w, &box));
	g_assert_cmpfloat_with_epsilon (box.x + box.width * 0.5f, 420.0f, 1e-3f);

	/* a 3D anchor behind the camera draws nothing */
	lrg_camera3d_set_position_xyz (cam3d, 0.0f, 0.0f, -10.0f);
	lrg_camera3d_set_target_xyz (cam3d, 0.0f, 0.0f, -20.0f);
	g_assert_false (lrg_world_widget_project_3d (w, cam3d, 640, 480, &screen));
	lrg_world_widget_draw_3d (w, cam3d, 640, 480);
	g_assert_false (lrg_world_widget_get_last_box (w, &box));

	lrg_camera3d_set_target_xyz (cam3d, 10.0f, 5.0f, -2.0f);
	lrg_world_widget_draw_3d (w, cam3d, 640, 480);
	g_assert_true (lrg_world_widget_get_last_box (w, &box));
	g_assert_cmpfloat_with_epsilon (box.x + box.width * 0.5f, 320.0f, 1e-2f);
}

/* ==========================================================================
 * Nameplate
 * ========================================================================== */

static void
test_nameplate (void)
{
	g_autoptr(LrgNameplate) plate = lrg_nameplate_new ("Kudu the Great Orchestrator");
	LrgWorldWidget *w = LRG_WORLD_WIDGET (plate);
	GrlColor busy = { 220, 60, 60, 255 };
	gfloat short_w;
	gfloat full_w;
	gfloat badge_w;

	g_assert_cmpstr (lrg_nameplate_get_display_text (plate), ==, "Kudu the Gr...");
	lrg_world_widget_measure (w, &short_w, NULL);
	g_assert_cmpfloat_with_epsilon (short_w, 14 * 7.2f + 12.0f, 1e-3f);

	lrg_nameplate_set_hovered (plate, TRUE);
	g_assert_true (lrg_nameplate_get_hovered (plate));
	g_assert_cmpstr (lrg_nameplate_get_display_text (plate), ==, "Kudu the Great Orchestrator");
	lrg_world_widget_measure (w, &full_w, NULL);
	g_assert_cmpfloat (full_w, >, short_w);
	lrg_nameplate_set_hovered (plate, FALSE);

	/* hover from the pointer, against the box last drawn */
	g_assert_false (lrg_nameplate_update_hover (plate, 100.0f, 95.0f));
	lrg_world_widget_draw_at (w, 100.0f, 100.0f);
	g_assert_true (lrg_nameplate_update_hover (plate, 100.0f, 90.0f));
	g_assert_cmpstr (lrg_nameplate_get_display_text (plate), ==, "Kudu the Great Orchestrator");
	/* still hovered after the box grows: it is tested at its short width */
	lrg_world_widget_draw_at (w, 100.0f, 100.0f);
	g_assert_true (lrg_nameplate_update_hover (plate, 100.0f, 90.0f));
	g_assert_false (lrg_nameplate_update_hover (plate, 100.0f + full_w * 0.5f - 1.0f, 90.0f));
	g_assert_false (lrg_nameplate_update_hover (plate, 300.0f, 90.0f));

	/* the status dot takes room */
	lrg_nameplate_set_badge_color (plate, &busy);
	lrg_world_widget_measure (w, &badge_w, NULL);
	g_assert_cmpfloat_with_epsilon (badge_w - short_w, 12.0f * 0.8f, 1e-3f);
	lrg_nameplate_set_badge_color (plate, NULL);

	lrg_nameplate_set_max_chars (plate, 0);
	g_assert_cmpstr (lrg_nameplate_get_display_text (plate), ==, "Kudu the Great Orchestrator");
	lrg_nameplate_set_text (plate, "Ox");
	lrg_nameplate_set_max_chars (plate, 4);
	g_assert_cmpstr (lrg_nameplate_get_display_text (plate), ==, "Ox");
	g_assert_cmpstr (lrg_nameplate_get_text (plate), ==, "Ox");
	lrg_nameplate_set_style (plate, NULL, NULL, 20.0f);
	lrg_world_widget_measure (w, &short_w, NULL);
	g_assert_cmpfloat_with_epsilon (short_w, 2 * 12.0f + 12.0f, 1e-3f);
}

/* ==========================================================================
 * Emote
 * ========================================================================== */

static void
test_emote (void)
{
	g_autoptr(LrgEmote) emote = lrg_emote_new_glyph ("?");
	LrgWorldWidget *w = LRG_WORLD_WIDGET (emote);
	gfloat max_scale = 0.0f;
	gfloat size;
	guint i;

	g_assert_cmpstr (lrg_emote_get_glyph (emote), ==, "?");
	g_assert_cmpfloat (lrg_emote_get_pop_scale (emote), ==, 1.0f);

	lrg_emote_pop (emote, 2.0f);
	g_assert_cmpfloat_with_epsilon (lrg_emote_get_pop_scale (emote), 0.5f, 1e-5f);
	lrg_world_widget_measure (w, &size, NULL);
	g_assert_cmpfloat_with_epsilon (size, 12.0f, 1e-4f);

	/* ease-out-back: overshoots, then settles at 1 */
	for (i = 0; i < 20; i++)
	{
		lrg_world_widget_update (w, 0.01f);
		max_scale = MAX (max_scale, lrg_emote_get_pop_scale (emote));
	}
	g_assert_cmpfloat (max_scale, >, 1.0f);
	g_assert_cmpfloat (lrg_emote_get_pop_scale (emote), ==, 1.0f);

	/* bob: a quarter period of 1.5 Hz is the top of the bob */
	lrg_emote_pop (emote, 0.0f);
	lrg_world_widget_update (w, 1.0f / 6.0f);
	g_assert_cmpfloat_with_epsilon (lrg_emote_get_bob_offset (emote), 3.0f, 1e-3f);
	lrg_emote_set_bob (emote, 0.0f, 1.0f);
	g_assert_cmpfloat (lrg_emote_get_bob_offset (emote), ==, 0.0f);
	lrg_world_widget_update (w, 0.1f);    /* finish the pop */

	lrg_emote_set_size (emote, 40.0f);
	lrg_world_widget_measure (w, &size, NULL);
	g_assert_cmpfloat_with_epsilon (size, 40.0f, 1e-4f);
	lrg_emote_set_glyph (emote, "zZ");
	g_assert_cmpstr (lrg_emote_get_glyph (emote), ==, "zZ");

	/* drawing headless is harmless */
	lrg_world_widget_draw_at (w, 10.0f, 10.0f);
	g_assert_true (lrg_world_widget_contains_point (w, 10.0f, 0.0f));
	g_assert_cmpfloat_with_epsilon (lrg_world_widget_measure_text ("abc", 10.0f), 18.0f, 1e-4f);
	g_assert_cmpfloat (lrg_world_widget_measure_text (NULL, 10.0f), ==, 0.0f);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/world-ui/project/perspective", test_project_perspective);
	g_test_add_func ("/world-ui/project/matches-raylib", test_project_matches_raylib);
	g_test_add_func ("/world-ui/bubble/wrap", test_bubble_wrap);
	g_test_add_func ("/world-ui/bubble/fade-and-hold", test_bubble_fade_and_hold);
	g_test_add_func ("/world-ui/widget/box-and-hit", test_box_and_hit);
	g_test_add_func ("/world-ui/widget/cameras", test_draw_with_cameras);
	g_test_add_func ("/world-ui/nameplate", test_nameplate);
	g_test_add_func ("/world-ui/emote", test_emote);

	return g_test_run ();
}
