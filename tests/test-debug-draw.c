/* test-debug-draw.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgDebugDraw: recording every primitive, tessellation
 * counts, durations ticked by delta time, categories, the depth-tested
 * and always-on-top lists, headless no-ops, and (with a display) pixel
 * checks that lines and boxes reach the target and honour the depth
 * test.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define EPS (1e-4)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

/* init_graphics_context: a hidden window when a display exists. */
static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-debug-draw-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static GrlVector3
v3 (gfloat x,
    gfloat y,
    gfloat z)
{
	GrlVector3 v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

static void
identity (gfloat *m)
{
	memset (m, 0, 16 * sizeof (gfloat));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* ==========================================================================
 * Recording
 * ========================================================================== */

/* Every primitive is recorded with its shape, control points, colour
 * and the pen's state. */
static void
test_record_each (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (1, 2, 3);
	GrlVector3 b = v3 (4, 5, 6);
	GrlVector3 pts[3];
	GrlVector2 p2a = { 1, 2 };
	GrlVector2 p2b = { 3, 4 };
	GrlColor   red = { 255, 0, 0, 200 };
	GrlColor   out;
	GrlVector3 got;
	gfloat     m[16];
	gfloat     vp[16];
	guint      i;
	static const LrgDebugShape expected[] = {
		LRG_DEBUG_SHAPE_LINE, LRG_DEBUG_SHAPE_POLYLINE, LRG_DEBUG_SHAPE_ARROW,
		LRG_DEBUG_SHAPE_BOX, LRG_DEBUG_SHAPE_ORIENTED_BOX, LRG_DEBUG_SHAPE_SPHERE,
		LRG_DEBUG_SHAPE_CAPSULE, LRG_DEBUG_SHAPE_CYLINDER, LRG_DEBUG_SHAPE_CONE,
		LRG_DEBUG_SHAPE_FRUSTUM, LRG_DEBUG_SHAPE_GRID, LRG_DEBUG_SHAPE_AXES,
		LRG_DEBUG_SHAPE_CIRCLE, LRG_DEBUG_SHAPE_POINT, LRG_DEBUG_SHAPE_TEXT,
		LRG_DEBUG_SHAPE_LINE_2D, LRG_DEBUG_SHAPE_RECT_2D, LRG_DEBUG_SHAPE_CIRCLE_2D,
		LRG_DEBUG_SHAPE_TEXT_2D
	};

	pts[0] = v3 (0, 0, 0);
	pts[1] = v3 (1, 0, 0);
	pts[2] = v3 (1, 1, 0);
	identity (m);
	m[12] = 7.0f;
	lrg_shadow_perspective (60.0f, 1.0f, 1.0f, 10.0f, vp);

	lrg_debug_draw_line (dd, &a, &b, &red, 0);
	lrg_debug_draw_polyline (dd, pts, 3, TRUE, NULL, 0);
	lrg_debug_draw_arrow (dd, &a, &b, 0.5f, NULL, 0);
	lrg_debug_draw_box (dd, &a, &b, NULL, 0);
	lrg_debug_draw_oriented_box (dd, m, &b, NULL, 0);
	lrg_debug_draw_sphere (dd, &a, 2.0f, NULL, 0);
	lrg_debug_draw_capsule (dd, &a, &b, 0.5f, NULL, 0);
	lrg_debug_draw_cylinder (dd, &a, &b, 0.5f, NULL, 0);
	lrg_debug_draw_cone (dd, &a, &b, 0.5f, NULL, 0);
	lrg_debug_draw_frustum (dd, vp, NULL, 0);
	lrg_debug_draw_grid (dd, &a, 1.0f, 4, NULL, 0);
	lrg_debug_draw_axes (dd, m, 1.0f, 0);
	lrg_debug_draw_circle (dd, &a, &b, 1.0f, NULL, 0);
	lrg_debug_draw_point (dd, &a, 0.2f, NULL, 0);
	lrg_debug_draw_text (dd, &a, "hello", NULL, 0);
	lrg_debug_draw_line_2d (dd, &p2a, &p2b, NULL, 0);
	lrg_debug_draw_rect_2d (dd, 10, 20, 30, 40, NULL, 0);
	lrg_debug_draw_circle_2d (dd, &p2a, 5.0f, NULL, 0);
	lrg_debug_draw_text_2d (dd, &p2a, "hud", NULL, 0);

	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, G_N_ELEMENTS (expected));
	for (i = 0; i < G_N_ELEMENTS (expected); i++)
	{
		g_assert_cmpint (lrg_debug_draw_get_shape (dd, i), ==, expected[i]);
		g_assert_cmpuint (lrg_debug_draw_get_primitive_category (dd, i), ==,
		                  LRG_DEBUG_CATEGORY_GENERAL);
		g_assert_true (lrg_debug_draw_get_primitive_depth_test (dd, i));
		g_assert_false (lrg_debug_draw_get_primitive_solid (dd, i));
		g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_thickness (dd, i), 1.0, EPS);
	}

	/* colour, points, text */
	g_assert_true (lrg_debug_draw_get_primitive_color (dd, 0, &out));
	g_assert_cmpuint (out.r, ==, 255);
	g_assert_cmpuint (out.g, ==, 0);
	g_assert_cmpuint (out.a, ==, 200);
	g_assert_true (lrg_debug_draw_get_primitive_color (dd, 1, &out));
	g_assert_cmpuint (out.g, ==, 255);   /* NULL: white */
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 0, 1, &got));
	g_assert_cmpfloat_with_epsilon (got.x, 4.0, EPS);
	g_assert_cmpfloat_with_epsilon (got.z, 6.0, EPS);
	g_assert_false (lrg_debug_draw_get_primitive_point (dd, 0, 2, &got));
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 1, 2, &got));
	g_assert_cmpfloat_with_epsilon (got.y, 1.0, EPS);
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 4, 0, &got));
	g_assert_cmpfloat_with_epsilon (got.x, 7.0, EPS);
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 16, 1, &got));
	g_assert_cmpfloat_with_epsilon (got.x, 30.0, EPS);
	g_assert_cmpfloat_with_epsilon (got.y, 40.0, EPS);
	g_assert_cmpstr (lrg_debug_draw_get_primitive_text (dd, 14), ==, "hello");
	g_assert_cmpstr (lrg_debug_draw_get_primitive_text (dd, 18), ==, "hud");
	g_assert_null (lrg_debug_draw_get_primitive_text (dd, 0));
	g_assert_false (lrg_debug_draw_get_primitive_color (dd, 99, &out));

	/* frustum anchor: the near plane's centre, 1 unit in front of an
	 * identity view (looking down -Z) */
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 9, 0, &got));
	g_assert_cmpfloat_with_epsilon (got.z, -1.0, 1e-3);
	g_assert_cmpfloat_with_epsilon (got.x, 0.0, 1e-3);

	/* a too-short polyline and a singular frustum are ignored */
	memset (vp, 0, sizeof vp);
	lrg_debug_draw_polyline (dd, pts, 1, FALSE, NULL, 0);
	lrg_debug_draw_frustum (dd, vp, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, G_N_ELEMENTS (expected));
}

/* The CPU tessellation of each shape has the expected size. */
static void
test_tessellation (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (0, 0, 0);
	GrlVector3 b = v3 (0, 2, 0);
	GrlVector3 up = v3 (0, 1, 0);
	GrlVector3 pts[4];
	gfloat     m[16];
	gfloat     vp[16];
	guint      seg = lrg_debug_draw_get_segments (dd);

	g_assert_cmpuint (seg, ==, 16);
	identity (m);
	lrg_shadow_ortho (-1, 1, -1, 1, 0.1f, 10, vp);
	pts[0] = v3 (0, 0, 0);
	pts[1] = v3 (1, 0, 0);
	pts[2] = v3 (1, 1, 0);
	pts[3] = v3 (0, 1, 0);

#define CHECK_LINES(expr, n) \
	G_STMT_START { \
		lrg_debug_draw_clear (dd); \
		expr; \
		g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, (n)); \
	} G_STMT_END

	CHECK_LINES (lrg_debug_draw_line (dd, &a, &b, NULL, 0), 1);
	CHECK_LINES (lrg_debug_draw_polyline (dd, pts, 4, FALSE, NULL, 0), 3);
	CHECK_LINES (lrg_debug_draw_polyline (dd, pts, 4, TRUE, NULL, 0), 4);
	CHECK_LINES (lrg_debug_draw_arrow (dd, &a, &b, 0, NULL, 0), 5);
	CHECK_LINES (lrg_debug_draw_box (dd, &a, &b, NULL, 0), 12);
	CHECK_LINES (lrg_debug_draw_oriented_box (dd, m, &b, NULL, 0), 12);
	CHECK_LINES (lrg_debug_draw_sphere (dd, &a, 1, NULL, 0), 3 * seg);
	/* 4 side lines, 2 equators, 4 half circles of seg / 2 */
	CHECK_LINES (lrg_debug_draw_capsule (dd, &a, &b, 1, NULL, 0), 4 + 2 * seg + 4 * (seg / 2));
	CHECK_LINES (lrg_debug_draw_cylinder (dd, &a, &b, 1, NULL, 0), 4 + 2 * seg);
	CHECK_LINES (lrg_debug_draw_cone (dd, &b, &a, 1, NULL, 0), seg + 4);
	CHECK_LINES (lrg_debug_draw_frustum (dd, vp, NULL, 0), 12);
	CHECK_LINES (lrg_debug_draw_grid (dd, &a, 1, 4, NULL, 0), 10);
	CHECK_LINES (lrg_debug_draw_axes (dd, m, 1, 0), 3);
	CHECK_LINES (lrg_debug_draw_circle (dd, &a, &up, 1, NULL, 0), seg);
	CHECK_LINES (lrg_debug_draw_point (dd, &a, 1, NULL, 0), 3);
	CHECK_LINES (lrg_debug_draw_text (dd, &a, "x", NULL, 0), 0);
	g_assert_cmpuint (lrg_debug_draw_get_label_count (dd), ==, 1);
	/* 2D shapes never reach the 3D lists */
	CHECK_LINES (lrg_debug_draw_rect_2d (dd, 0, 0, 1, 1, NULL, 0), 0);
#undef CHECK_LINES

	/* solid shapes become triangles */
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_set_solid (dd, TRUE);
	lrg_debug_draw_box (dd, &a, &b, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==, 12);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 0);
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_circle (dd, &a, &up, 1, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==, seg);
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_sphere (dd, &a, 1, NULL, 0);
	/* two domes of seg / 4 rings: the top ring of each is a fan */
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==,
	                  2 * (seg * (2 * (seg / 4) - 1)));
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_cone (dd, &b, &a, 1, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==, 2 * seg);
	/* lines stay lines when solid */
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 1);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==, 0);

	/* the pen's segment count */
	lrg_debug_draw_clear (dd);
	lrg_debug_draw_set_solid (dd, FALSE);
	lrg_debug_draw_set_segments (dd, 8);
	lrg_debug_draw_circle (dd, &a, &up, 1, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 8);
	lrg_debug_draw_set_segments (dd, 1);
	g_assert_cmpuint (lrg_debug_draw_get_segments (dd), ==, 4);
}

/* One-frame primitives live until end_frame; timed ones until ticks
 * consumed their duration. */
static void
test_durations (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (0, 0, 0);
	GrlVector3 b = v3 (1, 0, 0);

	lrg_debug_draw_line (dd, &a, &b, NULL, 0);        /* one frame */
	lrg_debug_draw_line (dd, &a, &b, NULL, 1.0f);     /* one second */
	lrg_debug_draw_line (dd, &a, &b, NULL, 0.25f);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 3);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_remaining (dd, 0), 0.0, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_remaining (dd, 1), 1.0, EPS);

	/* ticks do not remove one-frame primitives */
	lrg_debug_draw_tick (dd, 0.5f);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 2);
	g_assert_cmpint (lrg_debug_draw_get_shape (dd, 0), ==, LRG_DEBUG_SHAPE_LINE);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_remaining (dd, 0), 0.0, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_remaining (dd, 1), 0.5, EPS);

	/* end of frame drops the one-frame line, keeps the timed one */
	lrg_debug_draw_end_frame (dd);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_remaining (dd, 0), 0.5, EPS);
	lrg_debug_draw_tick (dd, -3.0f);   /* negative: no time */
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	lrg_debug_draw_tick (dd, 0.5f);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 0);
}

/* Categories filter at record time and hide timed primitives. */
static void
test_categories (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (0, 0, 0);
	GrlVector3 b = v3 (1, 0, 0);
	guint32    mask;

	g_assert_cmpstr (lrg_debug_category_get_name (LRG_DEBUG_CATEGORY_PHYSICS), ==, "physics");
	g_assert_cmpstr (lrg_debug_category_get_name (LRG_DEBUG_CATEGORY_USER), ==, "user");
	g_assert_null (lrg_debug_category_get_name (20));
	g_assert_null (lrg_debug_category_get_name (99));
	g_assert_cmpint (lrg_debug_category_from_name ("navigation"), ==, LRG_DEBUG_CATEGORY_NAVIGATION);
	g_assert_cmpint (lrg_debug_category_from_name ("nope"), ==, -1);
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_DEBUG_CATEGORY));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_DEBUG_SHAPE));

	/* a timed physics line, then physics off */
	lrg_debug_draw_set_category (dd, LRG_DEBUG_CATEGORY_PHYSICS);
	lrg_debug_draw_line (dd, &a, &b, NULL, 5.0f);
	g_assert_cmpuint (lrg_debug_draw_get_primitive_category (dd, 0), ==, LRG_DEBUG_CATEGORY_PHYSICS);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 1);
	lrg_debug_draw_set_category_enabled (dd, LRG_DEBUG_CATEGORY_PHYSICS, FALSE);
	g_assert_false (lrg_debug_draw_is_category_enabled (dd, LRG_DEBUG_CATEGORY_PHYSICS));
	g_assert_true (lrg_debug_draw_is_category_enabled (dd, LRG_DEBUG_CATEGORY_NAVIGATION));
	mask = lrg_debug_draw_get_category_mask (dd);
	g_assert_cmpuint (mask & (1u << LRG_DEBUG_CATEGORY_PHYSICS), ==, 0);

	/* recorded but hidden; new physics lines dropped */
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 0);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);

	/* other categories still record */
	lrg_debug_draw_set_category (dd, LRG_DEBUG_CATEGORY_USER + 3);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 2);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 1);

	/* back on: the timed line shows again */
	g_object_set (dd, "category-mask", (guint)LRG_DEBUG_CATEGORY_MASK_ALL, NULL);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 2);

	/* clear one category */
	lrg_debug_draw_clear_category (dd, LRG_DEBUG_CATEGORY_PHYSICS);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	g_assert_cmpuint (lrg_debug_draw_get_primitive_category (dd, 0), ==, LRG_DEBUG_CATEGORY_USER + 3);

	/* disabled: nothing records, everything cleared */
	lrg_debug_draw_set_enabled (dd, FALSE);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 0);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 0);
	g_assert_false (lrg_debug_draw_is_category_enabled (dd, LRG_DEBUG_CATEGORY_GENERAL));
	lrg_debug_draw_set_enabled (dd, TRUE);
	g_assert_true (lrg_debug_draw_get_enabled (dd));
}

/* The pen's depth test picks the list; push/pop restores the pen. */
static void
test_depth_lists (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (0, 0, 0);
	GrlVector3 b = v3 (1, 1, 1);

	lrg_debug_draw_box (dd, &a, &b, NULL, 0);
	lrg_debug_draw_push (dd);
	lrg_debug_draw_set_depth_test (dd, FALSE);
	lrg_debug_draw_set_thickness (dd, 3.0f);
	lrg_debug_draw_set_category (dd, LRG_DEBUG_CATEGORY_LIGHTS);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	lrg_debug_draw_set_solid (dd, TRUE);
	lrg_debug_draw_box (dd, &a, &b, NULL, 0);
	lrg_debug_draw_pop (dd);

	g_assert_true (lrg_debug_draw_get_depth_test (dd));
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_thickness (dd), 1.0, EPS);
	g_assert_cmpuint (lrg_debug_draw_get_category (dd), ==, LRG_DEBUG_CATEGORY_GENERAL);
	g_assert_false (lrg_debug_draw_get_solid (dd));

	g_assert_false (lrg_debug_draw_get_primitive_depth_test (dd, 1));
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_primitive_thickness (dd, 1), 3.0, EPS);
	g_assert_true (lrg_debug_draw_get_primitive_solid (dd, 2));
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 12);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, FALSE), ==, 1);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, FALSE), ==, 12);
	g_assert_cmpuint (lrg_debug_draw_get_triangle_count (dd, TRUE), ==, 0);

	/* thickness clamps */
	lrg_debug_draw_set_thickness (dd, 1000.0f);
	g_assert_cmpfloat_with_epsilon (lrg_debug_draw_get_thickness (dd), 64.0, EPS);
	lrg_debug_draw_set_text_size (dd, 1);
	g_assert_cmpuint (lrg_debug_draw_get_text_size (dd), ==, 4);

	/* unbalanced pop warns and keeps the pen */
	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*pop without a matching push*");
	lrg_debug_draw_pop (dd);
	g_test_assert_expected_messages ();
}

/* A camera's frustum: the near plane's centre lies near metres in
 * front of the eye. */
static void
test_camera_frustum (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	g_autoptr(LrgCamera3D)  camera = lrg_camera3d_new ();
	GrlVector3 got;

	lrg_camera3d_set_position_xyz (camera, 0, 0, 10);
	lrg_camera3d_set_target_xyz (camera, 0, 0, 0);
	lrg_debug_draw_camera_frustum (dd, camera, 1.5f, 2.0f, 50.0f, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	g_assert_cmpint (lrg_debug_draw_get_shape (dd, 0), ==, LRG_DEBUG_SHAPE_FRUSTUM);
	g_assert_true (lrg_debug_draw_get_primitive_point (dd, 0, 0, &got));
	g_assert_cmpfloat_with_epsilon (got.x, 0.0, 1e-3);
	g_assert_cmpfloat_with_epsilon (got.y, 0.0, 1e-3);
	g_assert_cmpfloat_with_epsilon (got.z, 8.0, 1e-2);
	g_assert_cmpuint (lrg_debug_draw_get_line_count (dd, TRUE), ==, 12);

	/* orthographic, with defaults */
	lrg_camera3d_set_projection (camera, LRG_PROJECTION_ORTHOGRAPHIC);
	lrg_debug_draw_camera_frustum (dd, camera, 0, 0, 0, NULL, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 2);
}

/* Without a context drawing does nothing, but flush still ends the
 * frame; the default instance is a singleton. */
static void
test_headless (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (0, 0, 0);
	GrlVector3 b = v3 (1, 0, 0);
	gfloat     m[16];

	if (graphics_available)
	{
		g_test_skip ("A graphics context exists; covered by /debug-draw/gl/*");
		return;
	}
	identity (m);
	lrg_debug_draw_line (dd, &a, &b, NULL, 0);
	lrg_debug_draw_line (dd, &a, &b, NULL, 2.0f);
	lrg_debug_draw_draw_3d (dd);
	lrg_debug_draw_draw_labels (dd);
	lrg_debug_draw_draw_2d (dd);
	g_assert_cmpuint (lrg_debug_draw_get_draw_calls (dd), ==, 0);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 2);
	lrg_debug_draw_flush (dd, NULL);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);
	lrg_debug_draw_flush_matrices (dd, m, m);
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 1);

	g_assert_true (lrg_debug_draw_get_default () == lrg_debug_draw_get_default ());
	g_assert_true (LRG_IS_DEBUG_DRAW (lrg_debug_draw_get_default ()));
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define TARGET (64)

/* render: @dd drawn into a fresh black TARGET x TARGET texture from
 * (0, 0, 5) looking at the origin; returns the pixels. */
static Image
render (LrgDebugDraw *dd)
{
	RenderTexture2D target = LoadRenderTexture (TARGET, TARGET);
	gfloat eye[3] = { 0, 0, 5 };
	gfloat at[3] = { 0, 0, 0 };
	gfloat up[3] = { 0, 1, 0 };
	gfloat view[16];
	gfloat proj[16];
	Image  image;

	lrg_shadow_look_at (eye, at, up, view);
	lrg_shadow_perspective (60.0f, 1.0f, 0.1f, 100.0f, proj);
	BeginTextureMode (target);
	ClearBackground (BLACK);
	lrg_debug_draw_flush_matrices (dd, view, proj);
	EndTextureMode ();
	image = LoadImageFromTexture (target.texture);
	UnloadRenderTexture (target);
	return image;
}

/* count_matching: pixels within 8 of @c on every channel. */
static guint
count_matching (Image image,
                Color c)
{
	guint n = 0;
	gint  x;
	gint  y;

	for (y = 0; y < image.height; y++)
		for (x = 0; x < image.width; x++)
		{
			Color p = GetImageColor (image, x, y);

			if (ABS ((gint)p.r - c.r) <= 8 && ABS ((gint)p.g - c.g) <= 8
			    && ABS ((gint)p.b - c.b) <= 8)
				n++;
		}
	return n;
}

/* A thin and a thick horizontal line both reach the target, and a 3D
 * label is drawn over them. */
static void
test_gl_line (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 a = v3 (-2, 0.3f, 0);
	GrlVector3 b = v3 (2, 0.3f, 0);
	GrlVector3 c = v3 (-2, -0.6f, 0);
	GrlVector3 d = v3 (2, -0.6f, 0);
	GrlColor   red = { 255, 0, 0, 255 };
	GrlColor   blue = { 0, 0, 255, 255 };
	GrlColor   yellow = { 255, 255, 0, 255 };
	GrlVector3 label = v3 (0, 1.5f, 0);
	Image      image;
	guint      thin;
	guint      thick;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	lrg_debug_draw_line (dd, &a, &b, &red, 0);
	lrg_debug_draw_set_thickness (dd, 6.0f);
	lrg_debug_draw_line (dd, &c, &d, &blue, 0);
	lrg_debug_draw_text (dd, &label, "Ab", &yellow, 0);
	image = render (dd);
	g_assert_cmpuint (count_matching (image, (Color){ 255, 255, 0, 255 }), >, 4);
	thin = count_matching (image, (Color){ 255, 0, 0, 255 });
	thick = count_matching (image, (Color){ 0, 0, 255, 255 });
	if (g_getenv ("LRG_DEBUG_DRAW_SCREENSHOT") != NULL)
		g_assert_true (ExportImage (image, g_getenv ("LRG_DEBUG_DRAW_SCREENSHOT")));
	UnloadImage (image);

	/* a 1-pixel line spans the width; the 6-pixel one about six times it */
	g_assert_cmpuint (thin, >=, TARGET / 2);
	g_assert_cmpuint (thick, >=, thin * 4);
	g_assert_cmpuint (lrg_debug_draw_get_draw_calls (dd), ==, 2);
	/* flushed: the one-frame lines are gone */
	g_assert_cmpuint (lrg_debug_draw_get_count (dd), ==, 0);
}

/* A solid box covers the centre; a depth-tested line behind it is hidden
 * while an always-on-top line is not. */
static void
test_gl_box_depth (void)
{
	g_autoptr(LrgDebugDraw) dd = lrg_debug_draw_new ();
	GrlVector3 lo = v3 (-1, -1, -1);
	GrlVector3 hi = v3 (1, 1, 1);
	GrlVector3 a = v3 (-0.5f, 0, -3);
	GrlVector3 b = v3 (0.5f, 0, -3);
	GrlVector3 c = v3 (-0.5f, 0.5f, -3);
	GrlVector3 d = v3 (0.5f, 0.5f, -3);
	GrlColor   green = { 0, 255, 0, 255 };
	GrlColor   red = { 255, 0, 0, 255 };
	GrlColor   blue = { 0, 0, 255, 255 };
	Image      image;
	Color      centre;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}
	/* wire box alone: its edges show, its middle stays black */
	lrg_debug_draw_box (dd, &lo, &hi, &green, 0);
	image = render (dd);
	centre = GetImageColor (image, TARGET / 2, TARGET / 2);
	g_assert_cmpuint (centre.g, <, 32);
	g_assert_cmpuint (count_matching (image, (Color){ 0, 255, 0, 255 }), >=, 40);
	UnloadImage (image);

	/* solid box with lines behind it */
	lrg_debug_draw_set_solid (dd, TRUE);
	lrg_debug_draw_box (dd, &lo, &hi, &green, 0);
	lrg_debug_draw_set_thickness (dd, 4.0f);
	lrg_debug_draw_line (dd, &a, &b, &red, 0);
	lrg_debug_draw_set_depth_test (dd, FALSE);
	lrg_debug_draw_line (dd, &c, &d, &blue, 0);
	image = render (dd);
	centre = GetImageColor (image, TARGET / 2, TARGET / 2);
	g_assert_cmpuint (centre.g, >, 200);
	g_assert_cmpuint (centre.r, <, 32);
	g_assert_cmpuint (count_matching (image, (Color){ 255, 0, 0, 255 }), ==, 0);
	g_assert_cmpuint (count_matching (image, (Color){ 0, 0, 255, 255 }), >, 8);
	UnloadImage (image);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	graphics_available = init_graphics_context ();

	g_test_add_func ("/debug-draw/record-each", test_record_each);
	g_test_add_func ("/debug-draw/tessellation", test_tessellation);
	g_test_add_func ("/debug-draw/durations", test_durations);
	g_test_add_func ("/debug-draw/categories", test_categories);
	g_test_add_func ("/debug-draw/depth-lists", test_depth_lists);
	g_test_add_func ("/debug-draw/camera-frustum", test_camera_frustum);
	g_test_add_func ("/debug-draw/headless", test_headless);
	g_test_add_func ("/debug-draw/gl/line", test_gl_line);
	g_test_add_func ("/debug-draw/gl/box-depth", test_gl_box_depth);

	return g_test_run ();
}
