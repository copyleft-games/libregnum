/* test-frame-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgFrameLayout: anchored, relative and explicit boxes,
 * the safe area, the overlap / frame / safe-area checks, the problem
 * list and the debug overlay.  Everything is headless.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Asserts a box's rectangle (to a thousandth of a pixel). */
static void
assert_box (LrgFrameLayout *layout,
            const gchar    *name,
            gfloat          x,
            gfloat          y,
            gfloat          w,
            gfloat          h)
{
	GrlRectangle r;

	g_assert_true (lrg_frame_layout_get_box (layout, name, &r));
	g_assert_cmpfloat_with_epsilon (r.x, x, 1e-3);
	g_assert_cmpfloat_with_epsilon (r.y, y, 1e-3);
	g_assert_cmpfloat_with_epsilon (r.width, w, 1e-3);
	g_assert_cmpfloat_with_epsilon (r.height, h, 1e-3);
}

static void
set_rect (LrgFrameLayout *layout,
          const gchar    *name,
          gfloat          x,
          gfloat          y,
          gfloat          w,
          gfloat          h)
{
	GrlRectangle r;

	r.x = x;
	r.y = y;
	r.width = w;
	r.height = h;
	lrg_frame_layout_set_box_rect (layout, name, &r);
}

/* The number of problems, freeing the list. */
static guint
n_problems (LrgFrameLayout *layout)
{
	g_autoptr(GPtrArray) problems = lrg_frame_layout_get_problems (layout);

	return problems->len;
}

/* Asserts that validate fails with @code and returns the message. */
static gchar *
expect_error (LrgFrameLayout *layout,
              gint            code)
{
	g_autoptr(GError) error = NULL;

	g_assert_false (lrg_frame_layout_validate (layout, &error));
	g_assert_error (error, LRG_FRAME_LAYOUT_ERROR, code);
	return g_strdup (error->message);
}

/* Asserts a pixel's colour (exact for opaque colours). */
static void
assert_pixel (GrlImage *image,
              gint      x,
              gint      y,
              guint8    r,
              guint8    g,
              guint8    b)
{
	g_autoptr(GrlColor) c = grl_image_get_pixel (image, x, y);

	if (c->r != r || c->g != g || c->b != b)
		g_error ("pixel (%d, %d) is %u,%u,%u, expected %u,%u,%u",
		         x, y, c->r, c->g, c->b, r, g, b);
}

/* ==========================================================================
 * Construction and safe area
 * ========================================================================== */

static void
test_new (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (960.0f, 540.0f);
	g_autoptr(LrgFrameLayout) tiny = lrg_frame_layout_new (0.0f, -5.0f);
	g_autoptr(GPtrArray) names = NULL;
	GrlRectangle safe;

	g_assert_true (LRG_IS_FRAME_LAYOUT (layout));
	g_assert_cmpfloat (lrg_frame_layout_get_width (layout), ==, 960.0f);
	g_assert_cmpfloat (lrg_frame_layout_get_height (layout), ==, 540.0f);

	/* the safe area starts as the whole frame */
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.x, ==, 0.0f);
	g_assert_cmpfloat (safe.y, ==, 0.0f);
	g_assert_cmpfloat (safe.width, ==, 960.0f);
	g_assert_cmpfloat (safe.height, ==, 540.0f);

	/* sizes below 1 become 1 */
	g_assert_cmpfloat (lrg_frame_layout_get_width (tiny), ==, 1.0f);
	g_assert_cmpfloat (lrg_frame_layout_get_height (tiny), ==, 1.0f);

	/* an empty layout is clean */
	names = lrg_frame_layout_get_names (layout);
	g_assert_cmpuint (names->len, ==, 0);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
	g_assert_cmpuint (n_problems (layout), ==, 0);
}

static void
test_safe_area (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	GrlRectangle safe;

	/* --- margins --- */
	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 20.0f, 30.0f, 40.0f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.x, ==, 10.0f);
	g_assert_cmpfloat (safe.y, ==, 20.0f);
	g_assert_cmpfloat (safe.width, ==, 160.0f);
	g_assert_cmpfloat (safe.height, ==, 40.0f);

	/* --- negative and non-finite margins count as 0 --- */
	lrg_frame_layout_set_safe_area_margins (layout, -5.0f, NAN, INFINITY, 0.0f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.x, ==, 0.0f);
	g_assert_cmpfloat (safe.y, ==, 0.0f);
	g_assert_cmpfloat (safe.width, ==, 200.0f);
	g_assert_cmpfloat (safe.height, ==, 100.0f);

	/* --- margins larger than the frame meet in proportion --- */
	lrg_frame_layout_set_safe_area_margins (layout, 150.0f, 0.0f, 150.0f, 300.0f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.x, ==, 100.0f);
	g_assert_cmpfloat (safe.width, ==, 0.0f);
	g_assert_cmpfloat (safe.y, ==, 0.0f);
	g_assert_cmpfloat (safe.height, ==, 0.0f);

	/* --- percent: of the width left/right, of the height top/bottom --- */
	lrg_frame_layout_set_safe_area_percent (layout, 0.1f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat_with_epsilon (safe.x, 20.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (safe.y, 10.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (safe.width, 160.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (safe.height, 80.0f, 1e-4);

	/* --- percent is clamped into 0 .. 0.5 --- */
	lrg_frame_layout_set_safe_area_percent (layout, 0.7f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.x, ==, 100.0f);
	g_assert_cmpfloat (safe.y, ==, 50.0f);
	g_assert_cmpfloat (safe.width, ==, 0.0f);
	g_assert_cmpfloat (safe.height, ==, 0.0f);

	lrg_frame_layout_set_safe_area_percent (layout, -1.0f);
	lrg_frame_layout_get_safe_area (layout, &safe);
	g_assert_cmpfloat (safe.width, ==, 200.0f);
	g_assert_cmpfloat (safe.height, ==, 100.0f);
}

/* ==========================================================================
 * Anchored boxes
 * ========================================================================== */

/*
 * Frame 200 x 100, margins 10 / 20 / 30 / 40: the safe area is
 * (10, 20) 160 x 40.  A 20 x 10 box on each anchor, hand computed.
 */
static const struct
{
	LrgHudAnchor anchor;
	gfloat       x;
	gfloat       y;
} anchor_cases[] = {
	{ LRG_HUD_ANCHOR_TOP_LEFT, 10.0f, 20.0f },
	{ LRG_HUD_ANCHOR_TOP, 80.0f, 20.0f },
	{ LRG_HUD_ANCHOR_TOP_RIGHT, 150.0f, 20.0f },
	{ LRG_HUD_ANCHOR_LEFT, 10.0f, 35.0f },
	{ LRG_HUD_ANCHOR_CENTER, 80.0f, 35.0f },
	{ LRG_HUD_ANCHOR_RIGHT, 150.0f, 35.0f },
	{ LRG_HUD_ANCHOR_BOTTOM_LEFT, 10.0f, 50.0f },
	{ LRG_HUD_ANCHOR_BOTTOM, 80.0f, 50.0f },
	{ LRG_HUD_ANCHOR_BOTTOM_RIGHT, 150.0f, 50.0f }
};

static void
test_anchor_margins (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	guint i;

	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 20.0f, 30.0f, 40.0f);
	for (i = 0; i < G_N_ELEMENTS (anchor_cases); i++)
	{
		lrg_frame_layout_add_box (layout, "box", anchor_cases[i].anchor, 0.0f, 0.0f, 20.0f, 10.0f);
		assert_box (layout, "box", anchor_cases[i].x, anchor_cases[i].y, 20.0f, 10.0f);

		/* offsets move right and down whatever the anchor */
		lrg_frame_layout_add_box (layout, "box", anchor_cases[i].anchor, 3.0f, -4.0f, 20.0f, 10.0f);
		assert_box (layout, "box", anchor_cases[i].x + 3.0f, anchor_cases[i].y - 4.0f, 20.0f, 10.0f);
	}
}

/* Frame 200 x 100 at 10 %: safe area (20, 10) 160 x 80. */
static void
test_anchor_percent (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	static const gfloat expected[9][2] = {
		{ 20.0f, 10.0f }, { 90.0f, 10.0f }, { 160.0f, 10.0f },
		{ 20.0f, 45.0f }, { 90.0f, 45.0f }, { 160.0f, 45.0f },
		{ 20.0f, 80.0f }, { 90.0f, 80.0f }, { 160.0f, 80.0f }
	};
	guint i;

	lrg_frame_layout_set_safe_area_percent (layout, 0.1f);
	for (i = 0; i < 9; i++)
	{
		lrg_frame_layout_add_box (layout, "box", (LrgHudAnchor)i, 0.0f, 0.0f, 20.0f, 10.0f);
		assert_box (layout, "box", expected[i][0], expected[i][1], 20.0f, 10.0f);
		g_assert_true (lrg_frame_layout_validate (layout, NULL));
	}
}

/* Anchored boxes follow a safe area changed after they were added. */
static void
test_anchor_follows_safe_area (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);

	lrg_frame_layout_add_box (layout, "badge", LRG_HUD_ANCHOR_BOTTOM_RIGHT, 0.0f, 0.0f, 20.0f, 10.0f);
	assert_box (layout, "badge", 180.0f, 90.0f, 20.0f, 10.0f);
	lrg_frame_layout_set_safe_area_margins (layout, 0.0f, 0.0f, 15.0f, 5.0f);
	assert_box (layout, "badge", 165.0f, 85.0f, 20.0f, 10.0f);
}

/* ==========================================================================
 * Relative boxes
 * ========================================================================== */

/*
 * The target is (100, 100) 40 x 20; a 10 x 6 box with a gap of 5 on each
 * side and alignment, hand computed.
 */
static const struct
{
	LrgFrameSide  side;
	LrgFrameAlign align;
	gfloat        x;
	gfloat        y;
} relative_cases[] = {
	{ LRG_FRAME_SIDE_ABOVE, LRG_FRAME_ALIGN_START, 100.0f, 89.0f },
	{ LRG_FRAME_SIDE_ABOVE, LRG_FRAME_ALIGN_CENTER, 115.0f, 89.0f },
	{ LRG_FRAME_SIDE_ABOVE, LRG_FRAME_ALIGN_END, 130.0f, 89.0f },
	{ LRG_FRAME_SIDE_BELOW, LRG_FRAME_ALIGN_START, 100.0f, 125.0f },
	{ LRG_FRAME_SIDE_BELOW, LRG_FRAME_ALIGN_CENTER, 115.0f, 125.0f },
	{ LRG_FRAME_SIDE_BELOW, LRG_FRAME_ALIGN_END, 130.0f, 125.0f },
	{ LRG_FRAME_SIDE_LEFT, LRG_FRAME_ALIGN_START, 85.0f, 100.0f },
	{ LRG_FRAME_SIDE_LEFT, LRG_FRAME_ALIGN_CENTER, 85.0f, 107.0f },
	{ LRG_FRAME_SIDE_LEFT, LRG_FRAME_ALIGN_END, 85.0f, 114.0f },
	{ LRG_FRAME_SIDE_RIGHT, LRG_FRAME_ALIGN_START, 145.0f, 100.0f },
	{ LRG_FRAME_SIDE_RIGHT, LRG_FRAME_ALIGN_CENTER, 145.0f, 107.0f },
	{ LRG_FRAME_SIDE_RIGHT, LRG_FRAME_ALIGN_END, 145.0f, 114.0f }
};

static void
test_relative (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	guint i;

	set_rect (layout, "hero", 100.0f, 100.0f, 40.0f, 20.0f);
	for (i = 0; i < G_N_ELEMENTS (relative_cases); i++)
	{
		lrg_frame_layout_add_box_relative (layout, "bubble", "hero", relative_cases[i].side, 5.0f,
		                                   10.0f, 6.0f, relative_cases[i].align);
		assert_box (layout, "bubble", relative_cases[i].x, relative_cases[i].y, 10.0f, 6.0f);
		/* a gap keeps them apart: clean */
		g_assert_true (lrg_frame_layout_validate (layout, NULL));
	}
}

/* A relative box follows its target, through a chain. */
static void
test_relative_follows (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);

	lrg_frame_layout_add_box (layout, "hero", LRG_HUD_ANCHOR_TOP_LEFT, 10.0f, 10.0f, 50.0f, 50.0f);
	lrg_frame_layout_add_box_relative (layout, "bubble", "hero", LRG_FRAME_SIDE_RIGHT, 4.0f,
	                                   30.0f, 20.0f, LRG_FRAME_ALIGN_START);
	lrg_frame_layout_add_box_relative (layout, "caption", "bubble", LRG_FRAME_SIDE_BELOW, 2.0f,
	                                   30.0f, 8.0f, LRG_FRAME_ALIGN_CENTER);
	assert_box (layout, "bubble", 64.0f, 10.0f, 30.0f, 20.0f);
	assert_box (layout, "caption", 64.0f, 32.0f, 30.0f, 8.0f);

	/* move the hero: both follow */
	lrg_frame_layout_add_box (layout, "hero", LRG_HUD_ANCHOR_TOP_LEFT, 100.0f, 50.0f, 50.0f, 50.0f);
	assert_box (layout, "bubble", 154.0f, 50.0f, 30.0f, 20.0f);
	assert_box (layout, "caption", 154.0f, 72.0f, 30.0f, 8.0f);

	/* a box added before its target is placed once the target exists */
	lrg_frame_layout_add_box_relative (layout, "early", "late", LRG_FRAME_SIDE_ABOVE, 0.0f,
	                                   10.0f, 10.0f, LRG_FRAME_ALIGN_START);
	g_assert_false (lrg_frame_layout_get_box (layout, "early", NULL));
	set_rect (layout, "late", 200.0f, 200.0f, 10.0f, 10.0f);
	assert_box (layout, "early", 200.0f, 190.0f, 10.0f, 10.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

/* A negative gap overlaps the boxes on purpose. */
static void
test_relative_negative_gap (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autofree gchar *message = NULL;

	set_rect (layout, "speaker", 50.0f, 100.0f, 60.0f, 100.0f);
	lrg_frame_layout_add_box_relative (layout, "bubble", "speaker", LRG_FRAME_SIDE_RIGHT, -10.0f,
	                                   80.0f, 40.0f, LRG_FRAME_ALIGN_START);
	assert_box (layout, "bubble", 100.0f, 100.0f, 80.0f, 40.0f);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OVERLAP);
	g_assert_nonnull (strstr (message, "share (x=100, y=100, w=10, h=40)"));

	lrg_frame_layout_allow_overlap (layout, "speaker", "bubble");
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

/* ==========================================================================
 * Unknown relative targets
 * ========================================================================== */

static void
test_unknown_box (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autofree gchar *message = NULL;
	GrlRectangle r;

	lrg_frame_layout_add_box_relative (layout, "bubble", "ghost", LRG_FRAME_SIDE_RIGHT, 4.0f,
	                                   30.0f, 20.0f, LRG_FRAME_ALIGN_START);
	r.x = 99.0f;
	g_assert_false (lrg_frame_layout_get_box (layout, "bubble", &r));
	g_assert_cmpfloat (r.x, ==, 0.0f);
	g_assert_cmpfloat (r.width, ==, 0.0f);
	g_assert_false (lrg_frame_layout_get_box (layout, "nobody", &r));
	g_assert_false (lrg_frame_layout_get_box (layout, NULL, &r));

	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX);
	g_assert_cmpstr (message, ==,
	                 "unknown box: \"bubble\" is placed relative to \"ghost\", which does not exist");

	/* an unresolved box takes no part in the other checks */
	set_rect (layout, "wall", 0.0f, 0.0f, 300.0f, 300.0f);
	g_assert_cmpuint (n_problems (layout), ==, 1);
}

/* A chain whose root is missing, and placements that loop. */
static void
test_unknown_chain_and_loop (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autoptr(GPtrArray) problems = NULL;
	g_autoptr(GPtrArray) loop = NULL;

	lrg_frame_layout_add_box_relative (layout, "b", "a", LRG_FRAME_SIDE_BELOW, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);
	lrg_frame_layout_add_box_relative (layout, "c", "b", LRG_FRAME_SIDE_BELOW, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);
	problems = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (problems->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (problems, 0), ==,
	                 "unknown box: \"b\" is placed relative to \"a\", which does not exist");
	g_assert_cmpstr (g_ptr_array_index (problems, 1), ==,
	                 "unknown box: \"c\" is placed relative to \"b\", which has no rectangle itself");

	/* close the loop: a beside c */
	lrg_frame_layout_add_box_relative (layout, "a", "c", LRG_FRAME_SIDE_BELOW, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);
	loop = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (loop->len, ==, 3);
	/* exactly one box names the loop; the others have no target rectangle */
	g_assert_cmpuint ((strstr (g_ptr_array_index (loop, 0), "loop back") != NULL) +
	                  (strstr (g_ptr_array_index (loop, 1), "loop back") != NULL) +
	                  (strstr (g_ptr_array_index (loop, 2), "loop back") != NULL), ==, 1);
	g_assert_false (lrg_frame_layout_get_box (layout, "a", NULL));
	g_assert_false (lrg_frame_layout_get_box (layout, "b", NULL));
	g_assert_false (lrg_frame_layout_get_box (layout, "c", NULL));

	/* breaking the loop places all three */
	set_rect (layout, "a", 0.0f, 0.0f, 10.0f, 10.0f);
	assert_box (layout, "b", 0.0f, 10.0f, 10.0f, 10.0f);
	assert_box (layout, "c", 0.0f, 20.0f, 10.0f, 10.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

static void
test_self_reference (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autofree gchar *message = NULL;

	lrg_frame_layout_add_box_relative (layout, "me", "me", LRG_FRAME_SIDE_LEFT, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);
	g_assert_false (lrg_frame_layout_get_box (layout, "me", NULL));
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX);
	g_assert_nonnull (strstr (message, "loop"));
}

/* ==========================================================================
 * Overlaps
 * ========================================================================== */

static void
test_overlap (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autofree gchar *message = NULL;

	set_rect (layout, "title", 10.0f, 10.0f, 100.0f, 40.0f);
	set_rect (layout, "bubble", 90.0f, 30.0f, 60.0f, 60.0f);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OVERLAP);
	g_assert_cmpstr (message, ==,
	                 "overlap: \"title\" (x=10, y=10, w=100, h=40) and \"bubble\" "
	                 "(x=90, y=30, w=60, h=60) share (x=90, y=30, w=20, h=20)");
}

/* allow_overlap(a, b) and allow_overlap(b, a) are the same permission. */
static void
test_allow_overlap_order (void)
{
	guint order;

	for (order = 0; order < 2; order++)
	{
		g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);

		set_rect (layout, "speaker", 0.0f, 0.0f, 50.0f, 50.0f);
		set_rect (layout, "bubble", 40.0f, 40.0f, 50.0f, 50.0f);
		set_rect (layout, "logo", 80.0f, 80.0f, 50.0f, 50.0f);
		g_assert_cmpuint (n_problems (layout), ==, 2);

		if (order == 0)
			lrg_frame_layout_allow_overlap (layout, "speaker", "bubble");
		else
			lrg_frame_layout_allow_overlap (layout, "bubble", "speaker");
		/* bubble/logo is still reported */
		g_assert_cmpuint (n_problems (layout), ==, 1);
		g_free (expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OVERLAP));

		/* allowing a pair twice, or a box with itself, changes nothing */
		lrg_frame_layout_allow_overlap (layout, "speaker", "bubble");
		lrg_frame_layout_allow_overlap (layout, "logo", "logo");
		g_assert_cmpuint (n_problems (layout), ==, 1);

		lrg_frame_layout_allow_overlap (layout, "logo", "bubble");
		g_assert_true (lrg_frame_layout_validate (layout, NULL));
	}
}

/* A permission given before the boxes exist, and kept across replace. */
static void
test_allow_overlap_persists (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);

	lrg_frame_layout_allow_overlap (layout, "a", "b");
	set_rect (layout, "a", 0.0f, 0.0f, 50.0f, 50.0f);
	set_rect (layout, "b", 10.0f, 10.0f, 50.0f, 50.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	g_assert_true (lrg_frame_layout_remove_box (layout, "b"));
	lrg_frame_layout_add_box (layout, "b", LRG_HUD_ANCHOR_TOP_LEFT, 5.0f, 5.0f, 20.0f, 20.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

/* Boxes that share an edge or a corner do not overlap. */
static void
test_touching_is_not_overlap (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);

	set_rect (layout, "a", 10.0f, 10.0f, 50.0f, 50.0f);
	set_rect (layout, "right", 60.0f, 10.0f, 50.0f, 50.0f);	/* shares a's right edge */
	set_rect (layout, "below", 10.0f, 60.0f, 50.0f, 50.0f);	/* shares a's bottom edge */
	set_rect (layout, "corner", 60.0f, 60.0f, 50.0f, 50.0f);	/* touches three corners */
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* relative placement with gap 0 touches too */
	lrg_frame_layout_add_box_relative (layout, "above", "a", LRG_FRAME_SIDE_ABOVE, 0.0f,
	                                   50.0f, 10.0f, LRG_FRAME_ALIGN_START);
	assert_box (layout, "above", 10.0f, 0.0f, 50.0f, 10.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* one pixel more and it overlaps */
	set_rect (layout, "right", 59.0f, 10.0f, 50.0f, 50.0f);
	g_free (expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OVERLAP));
}

/* ==========================================================================
 * Frame and safe area
 * ========================================================================== */

static void
test_out_of_frame (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autofree gchar *message = NULL;

	/* exactly filling the frame is fine */
	set_rect (layout, "full", 0.0f, 0.0f, 200.0f, 100.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
	g_assert_true (lrg_frame_layout_remove_box (layout, "full"));

	lrg_frame_layout_add_box (layout, "footer", LRG_HUD_ANCHOR_BOTTOM, 0.0f, 12.0f, 100.0f, 20.0f);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME);
	g_assert_cmpstr (message, ==,
	                 "out of frame: \"footer\" (x=50, y=92, w=100, h=20) leaves the 200x100 frame by bottom 12");

	/* every edge it leaves is named */
	set_rect (layout, "footer", -5.0f, -6.0f, 220.0f, 120.0f);
	g_clear_pointer (&message, g_free);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME);
	g_assert_nonnull (strstr (message, "by left 5, top 6, right 15, bottom 14"));
}

static void
test_out_of_safe_area (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autofree gchar *message = NULL;
	g_autoptr(GPtrArray) problems = NULL;

	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 10.0f, 10.0f, 10.0f);

	/* inside the frame, but past the safe area's left edge */
	set_rect (layout, "logo", 4.0f, 20.0f, 30.0f, 30.0f);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA);
	g_assert_cmpstr (message, ==,
	                 "out of safe area: \"logo\" (x=4, y=20, w=30, h=30) leaves the safe area "
	                 "(x=10, y=10, w=180, h=80) by left 6");

	/* outside the frame: only the frame problem is reported */
	set_rect (layout, "logo", -4.0f, 20.0f, 30.0f, 30.0f);
	problems = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (problems->len, ==, 1);
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 0), "out of frame:"));

	/* on the safe area's edge is inside */
	set_rect (layout, "logo", 10.0f, 10.0f, 180.0f, 80.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* anchored boxes sit inside the safe area by construction */
	lrg_frame_layout_set_safe_area_percent (layout, 0.05f);
	lrg_frame_layout_remove_box (layout, "logo");
	lrg_frame_layout_add_box (layout, "a", LRG_HUD_ANCHOR_TOP_LEFT, 0.0f, 0.0f, 30.0f, 30.0f);
	lrg_frame_layout_add_box (layout, "b", LRG_HUD_ANCHOR_BOTTOM_RIGHT, 0.0f, 0.0f, 30.0f, 30.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

/* ==========================================================================
 * The problem list
 * ========================================================================== */

/* Every problem is listed, boxes first, the code is the first one's. */
static void
test_multiple_problems (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) problems = NULL;
	g_auto(GStrv) lines = NULL;
	guint i;

	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 10.0f, 10.0f, 10.0f);
	set_rect (layout, "title", 20.0f, 20.0f, 100.0f, 20.0f);
	set_rect (layout, "badge", 110.0f, 15.0f, 30.0f, 30.0f);	/* overlaps title */
	set_rect (layout, "edge", 185.0f, 50.0f, 10.0f, 10.0f);	/* out of safe area */
	set_rect (layout, "gone", 190.0f, 95.0f, 20.0f, 20.0f);	/* out of frame */
	lrg_frame_layout_add_box_relative (layout, "lost", "nobody", LRG_FRAME_SIDE_LEFT, 0.0f, 5.0f, 5.0f,
	                                   LRG_FRAME_ALIGN_CENTER);

	problems = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (problems->len, ==, 4);
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 0), "out of safe area: \"edge\""));
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 1), "out of frame: \"gone\""));
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 2), "unknown box: \"lost\""));
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 3), "overlap: \"title\""));

	/* the error message is the same lines, the code the first one's */
	g_assert_false (lrg_frame_layout_validate (layout, &error));
	g_assert_error (error, LRG_FRAME_LAYOUT_ERROR, LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA);
	lines = g_strsplit (error->message, "\n", -1);
	g_assert_cmpuint (g_strv_length (lines), ==, problems->len);
	for (i = 0; i < problems->len; i++)
		g_assert_cmpstr (lines[i], ==, g_ptr_array_index (problems, i));

	/* validate with no error location still reports failure */
	g_assert_false (lrg_frame_layout_validate (layout, NULL));
}

/* ==========================================================================
 * Replace, remove, names, sizes
 * ========================================================================== */

static void
test_replace_and_remove (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (300.0f, 300.0f);
	g_autoptr(GPtrArray) names = NULL;
	g_autoptr(GPtrArray) after = NULL;
	g_autofree gchar *message = NULL;

	set_rect (layout, "a", 0.0f, 0.0f, 10.0f, 10.0f);
	lrg_frame_layout_add_box (layout, "b", LRG_HUD_ANCHOR_CENTER, 0.0f, 0.0f, 10.0f, 10.0f);
	lrg_frame_layout_add_box_relative (layout, "c", "a", LRG_FRAME_SIDE_RIGHT, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);

	/* replacing keeps the order and switches the placement kind */
	lrg_frame_layout_add_box (layout, "a", LRG_HUD_ANCHOR_BOTTOM_RIGHT, 0.0f, 0.0f, 20.0f, 20.0f);
	assert_box (layout, "a", 280.0f, 280.0f, 20.0f, 20.0f);
	assert_box (layout, "c", 300.0f, 280.0f, 10.0f, 10.0f);	/* follows a, now off-frame */
	set_rect (layout, "c", 50.0f, 50.0f, 5.0f, 5.0f);	/* no longer relative */
	set_rect (layout, "a", 0.0f, 0.0f, 10.0f, 10.0f);
	assert_box (layout, "c", 50.0f, 50.0f, 5.0f, 5.0f);

	names = lrg_frame_layout_get_names (layout);
	g_assert_cmpuint (names->len, ==, 3);
	g_assert_cmpstr (g_ptr_array_index (names, 0), ==, "a");
	g_assert_cmpstr (g_ptr_array_index (names, 1), ==, "b");
	g_assert_cmpstr (g_ptr_array_index (names, 2), ==, "c");

	/* removing a target leaves its followers unplaced */
	lrg_frame_layout_add_box_relative (layout, "c", "a", LRG_FRAME_SIDE_BELOW, 0.0f, 10.0f, 10.0f,
	                                   LRG_FRAME_ALIGN_START);
	g_assert_true (lrg_frame_layout_remove_box (layout, "a"));
	g_assert_false (lrg_frame_layout_remove_box (layout, "a"));
	g_assert_false (lrg_frame_layout_get_box (layout, "a", NULL));
	g_assert_false (lrg_frame_layout_get_box (layout, "c", NULL));
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_UNKNOWN_BOX);

	after = lrg_frame_layout_get_names (layout);
	g_assert_cmpuint (after->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (after, 0), ==, "b");
	g_assert_cmpstr (g_ptr_array_index (after, 1), ==, "c");
}

static void
test_sizes (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (100.0f, 100.0f);
	GrlRectangle r;

	/* negative and non-finite sizes and offsets count as 0 */
	lrg_frame_layout_add_box (layout, "neg", LRG_HUD_ANCHOR_CENTER, NAN, 0.0f, -10.0f, INFINITY);
	assert_box (layout, "neg", 50.0f, 50.0f, 0.0f, 0.0f);
	r.x = 10.0f;
	r.y = INFINITY;
	r.width = -4.0f;
	r.height = 5.0f;
	lrg_frame_layout_set_box_rect (layout, "rect", &r);
	assert_box (layout, "rect", 10.0f, 0.0f, 0.0f, 5.0f);

	/* zero-size boxes overlap nothing, even inside another box */
	set_rect (layout, "panel", 0.0f, 0.0f, 100.0f, 100.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* but a zero-size box outside the frame is still out of it */
	set_rect (layout, "point", 120.0f, 50.0f, 0.0f, 0.0f);
	g_free (expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME));
}

static void
test_enum_types (void)
{
	GEnumClass *klass;

	klass = g_type_class_ref (LRG_TYPE_FRAME_SIDE);
	g_assert_cmpstr (g_enum_get_value (klass, LRG_FRAME_SIDE_ABOVE)->value_nick, ==, "above");
	g_assert_cmpstr (g_enum_get_value (klass, LRG_FRAME_SIDE_RIGHT)->value_nick, ==, "right");
	g_assert_cmpuint (klass->n_values, ==, 4);
	g_type_class_unref (klass);

	klass = g_type_class_ref (LRG_TYPE_FRAME_ALIGN);
	g_assert_cmpstr (g_enum_get_value (klass, LRG_FRAME_ALIGN_CENTER)->value_nick, ==, "center");
	g_assert_cmpuint (klass->n_values, ==, 3);
	g_type_class_unref (klass);

	klass = g_type_class_ref (LRG_TYPE_FRAME_LAYOUT_ERROR);
	g_assert_cmpstr (g_enum_get_value (klass, LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA)->value_nick, ==,
	                 "out-of-safe-area");
	g_assert_cmpuint (klass->n_values, ==, 4);
	g_type_class_unref (klass);
}

/* ==========================================================================
 * Debug overlay
 * ========================================================================== */

/*
 * Frame 200 x 100, margins 10: "a" (40, 30) 50 x 40 is clean, "b"
 * (120, 30) 40 x 40 and "c" (140, 50) 40 x 30 overlap in (140, 50) 20 x 20.
 */
static void
test_draw_debug (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlColor) shade = NULL;
	g_autoptr(GrlColor) label = NULL;
	GrlColor black = { 0, 0, 0, 255 };
	GrlImage *image;

	canvas = lrg_image_canvas_new (200, 100, &black);
	g_assert_nonnull (canvas);
	image = lrg_image_canvas_get_image (canvas);

	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 10.0f, 10.0f, 10.0f);
	set_rect (layout, "a", 40.0f, 30.0f, 50.0f, 40.0f);
	set_rect (layout, "b", 120.0f, 30.0f, 40.0f, 40.0f);
	set_rect (layout, "c", 140.0f, 50.0f, 40.0f, 30.0f);
	lrg_frame_layout_add_box_relative (layout, "lost", "nobody", LRG_FRAME_SIDE_LEFT, 0.0f, 5.0f, 5.0f,
	                                   LRG_FRAME_ALIGN_CENTER);
	lrg_frame_layout_draw_debug (layout, canvas);

	/* --- the safe area: yellow 1-pixel outline --- */
	assert_pixel (image, 10, 20, 255, 210, 0);
	assert_pixel (image, 100, 10, 255, 210, 0);
	assert_pixel (image, 189, 20, 255, 210, 0);
	assert_pixel (image, 100, 89, 255, 210, 0);
	assert_pixel (image, 11, 20, 0, 0, 0);

	/* --- "a": green outline, interior untouched --- */
	assert_pixel (image, 40, 60, 60, 220, 120);
	assert_pixel (image, 89, 60, 60, 220, 120);
	assert_pixel (image, 65, 69, 60, 220, 120);
	assert_pixel (image, 65, 60, 0, 0, 0);
	/* its label is filled green at the top-left corner */
	assert_pixel (image, 41, 31, 60, 220, 120);
	assert_pixel (image, 41, 41, 60, 220, 120);

	/* --- "b" and "c": red outlines, 3 pixels thick --- */
	assert_pixel (image, 120, 60, 255, 40, 40);
	assert_pixel (image, 122, 60, 255, 40, 40);
	assert_pixel (image, 123, 60, 0, 0, 0);
	assert_pixel (image, 179, 75, 255, 40, 40);
	/* a red label for a problem box */
	label = grl_image_get_pixel (image, 121, 31);
	g_assert_cmpuint (label->r, ==, 255);

	/* --- the shared area is shaded translucent red over black --- */
	shade = grl_image_get_pixel (image, 150, 60);
	g_assert_cmpuint (shade->r, >, 60);
	g_assert_cmpuint (shade->r, <, 160);
	g_assert_cmpuint (shade->g, <, 40);
	g_assert_cmpuint (shade->b, <, 40);
	/* outside it, inside "b", nothing is drawn */
	assert_pixel (image, 130, 60, 0, 0, 0);

	/* --- the image's blend mode is restored --- */
	g_assert_cmpint (grl_image_get_blend_mode (image), ==, GRL_IMAGE_BLEND_REPLACE);
}

/* An empty layout draws only the safe area. */
static void
test_draw_debug_empty (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (50.0f, 50.0f);
	g_autoptr(LrgImageCanvas) canvas = NULL;
	GrlColor black = { 0, 0, 0, 255 };
	GrlImage *image;

	canvas = lrg_image_canvas_new (50, 50, &black);
	image = lrg_image_canvas_get_image (canvas);
	lrg_frame_layout_draw_debug (layout, canvas);
	assert_pixel (image, 0, 25, 255, 210, 0);
	assert_pixel (image, 25, 25, 0, 0, 0);
}

/* ==========================================================================
 * Backdrops
 * ========================================================================== */

/* A backdrop overlaps nothing and may leave the safe area, not the frame. */
static void
test_backdrop_checks (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autoptr(GPtrArray) problems = NULL;
	g_autoptr(GPtrArray) out = NULL;
	g_autofree gchar *message = NULL;

	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 10.0f, 10.0f, 10.0f);
	set_rect (layout, "bg", 0.0f, 0.0f, 200.0f, 100.0f);
	set_rect (layout, "title", 20.0f, 20.0f, 50.0f, 20.0f);
	set_rect (layout, "hero", 100.0f, 30.0f, 40.0f, 40.0f);

	/* --- not a backdrop yet: out of safe area plus two overlaps --- */
	g_assert_false (lrg_frame_layout_get_backdrop (layout, "bg"));
	problems = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (problems->len, ==, 3);
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 0), "out of safe area: \"bg\""));
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 1), "overlap: \"bg\""));
	g_assert_true (g_str_has_prefix (g_ptr_array_index (problems, 2), "overlap: \"bg\""));

	/* --- as a backdrop the layout is clean --- */
	lrg_frame_layout_set_backdrop (layout, "bg", TRUE);
	g_assert_true (lrg_frame_layout_get_backdrop (layout, "bg"));
	g_assert_false (lrg_frame_layout_get_backdrop (layout, "title"));
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* other boxes still overlap each other */
	set_rect (layout, "hero", 60.0f, 30.0f, 40.0f, 40.0f);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OVERLAP);
	g_assert_true (g_str_has_prefix (message, "overlap: \"title\""));
	g_assert_null (strstr (message, "\"bg\""));
	set_rect (layout, "hero", 100.0f, 30.0f, 40.0f, 40.0f);

	/* two backdrops do not overlap each other either */
	set_rect (layout, "photo", 0.0f, 50.0f, 200.0f, 50.0f);
	lrg_frame_layout_set_backdrop (layout, "photo", TRUE);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* --- leaving the frame is still reported --- */
	set_rect (layout, "bg", -5.0f, 0.0f, 210.0f, 100.0f);
	out = lrg_frame_layout_get_problems (layout);
	g_assert_cmpuint (out->len, ==, 1);
	g_clear_pointer (&message, g_free);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_FRAME);
	g_assert_cmpstr (message, ==,
	                 "out of frame: \"bg\" (x=-5, y=0, w=210, h=100) leaves the 200x100 frame by left 5, right 5");

	/* --- unmarking restores the checks --- */
	set_rect (layout, "bg", 0.0f, 0.0f, 200.0f, 100.0f);
	lrg_frame_layout_set_backdrop (layout, "bg", FALSE);
	g_assert_false (lrg_frame_layout_get_backdrop (layout, "bg"));
	g_clear_pointer (&message, g_free);
	message = expect_error (layout, LRG_FRAME_LAYOUT_ERROR_OUT_OF_SAFE_AREA);
	/* unmarking twice is harmless */
	lrg_frame_layout_set_backdrop (layout, "bg", FALSE);
}

/* The mark is kept by name: before the box exists, across remove and replace. */
static void
test_backdrop_persists (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);

	lrg_frame_layout_set_safe_area_percent (layout, 0.05f);
	lrg_frame_layout_set_backdrop (layout, "sky", TRUE);
	g_assert_true (lrg_frame_layout_get_backdrop (layout, "sky"));
	set_rect (layout, "sky", 0.0f, 0.0f, 200.0f, 100.0f);
	lrg_frame_layout_add_box (layout, "title", LRG_HUD_ANCHOR_TOP, 0.0f, 0.0f, 100.0f, 20.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* replace with another placement kind */
	lrg_frame_layout_add_box (layout, "sky", LRG_HUD_ANCHOR_CENTER, 0.0f, 0.0f, 200.0f, 100.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));

	/* remove and add again */
	g_assert_true (lrg_frame_layout_remove_box (layout, "sky"));
	g_assert_true (lrg_frame_layout_get_backdrop (layout, "sky"));
	set_rect (layout, "sky", 0.0f, 0.0f, 200.0f, 100.0f);
	g_assert_true (lrg_frame_layout_validate (layout, NULL));
}

/* A backdrop is a grey dashed 1-pixel outline under the other boxes. */
static void
test_backdrop_draw (void)
{
	g_autoptr(LrgFrameLayout) layout = lrg_frame_layout_new (200.0f, 100.0f);
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(LrgImageCanvas) again = NULL;
	GrlColor black = { 0, 0, 0, 255 };
	GrlImage *image;

	canvas = lrg_image_canvas_new (200, 100, &black);
	image = lrg_image_canvas_get_image (canvas);
	lrg_frame_layout_set_safe_area_margins (layout, 10.0f, 10.0f, 10.0f, 10.0f);
	set_rect (layout, "bg", 0.0f, 0.0f, 200.0f, 100.0f);
	lrg_frame_layout_set_backdrop (layout, "bg", TRUE);
	set_rect (layout, "a", 40.0f, 30.0f, 50.0f, 40.0f);
	lrg_frame_layout_draw_debug (layout, canvas);

	/* --- dashes: 6 on, 4 off, from each edge's start --- */
	assert_pixel (image, 23, 99, 160, 160, 160);	/* bottom, dash 20..25 */
	assert_pixel (image, 27, 99, 0, 0, 0);		/* bottom, gap 26..29 */
	assert_pixel (image, 199, 52, 160, 160, 160);	/* right, dash 50..55 */
	assert_pixel (image, 199, 57, 0, 0, 0);		/* right, gap */
	assert_pixel (image, 0, 41, 160, 160, 160);	/* left, dash 40..45 */
	assert_pixel (image, 1, 41, 0, 0, 0);		/* one pixel thin */
	/* its label is grey */
	assert_pixel (image, 1, 12, 160, 160, 160);

	/* --- the safe area and the boxes are drawn over it --- */
	assert_pixel (image, 10, 50, 255, 210, 0);
	assert_pixel (image, 40, 60, 60, 220, 120);

	/* --- a backdrop that leaves the frame is drawn red --- */
	again = lrg_image_canvas_new (200, 100, &black);
	set_rect (layout, "bg", -5.0f, 0.0f, 210.0f, 100.0f);
	lrg_frame_layout_draw_debug (layout, again);
	assert_pixel (lrg_image_canvas_get_image (again), 100, 1, 255, 40, 40);
}

gint
main (gint   argc,
      gchar *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/frame-layout/new", test_new);
	g_test_add_func ("/frame-layout/safe-area", test_safe_area);
	g_test_add_func ("/frame-layout/anchor/margins", test_anchor_margins);
	g_test_add_func ("/frame-layout/anchor/percent", test_anchor_percent);
	g_test_add_func ("/frame-layout/anchor/follows-safe-area", test_anchor_follows_safe_area);
	g_test_add_func ("/frame-layout/relative/sides", test_relative);
	g_test_add_func ("/frame-layout/relative/follows", test_relative_follows);
	g_test_add_func ("/frame-layout/relative/negative-gap", test_relative_negative_gap);
	g_test_add_func ("/frame-layout/unknown/missing", test_unknown_box);
	g_test_add_func ("/frame-layout/unknown/chain-and-loop", test_unknown_chain_and_loop);
	g_test_add_func ("/frame-layout/unknown/self", test_self_reference);
	g_test_add_func ("/frame-layout/overlap/detected", test_overlap);
	g_test_add_func ("/frame-layout/overlap/allow-order", test_allow_overlap_order);
	g_test_add_func ("/frame-layout/overlap/allow-persists", test_allow_overlap_persists);
	g_test_add_func ("/frame-layout/overlap/touching", test_touching_is_not_overlap);
	g_test_add_func ("/frame-layout/frame/out-of-frame", test_out_of_frame);
	g_test_add_func ("/frame-layout/frame/out-of-safe-area", test_out_of_safe_area);
	g_test_add_func ("/frame-layout/problems/multiple", test_multiple_problems);
	g_test_add_func ("/frame-layout/boxes/replace-remove", test_replace_and_remove);
	g_test_add_func ("/frame-layout/boxes/sizes", test_sizes);
	g_test_add_func ("/frame-layout/enums", test_enum_types);
	g_test_add_func ("/frame-layout/backdrop/checks", test_backdrop_checks);
	g_test_add_func ("/frame-layout/backdrop/persists", test_backdrop_persists);
	g_test_add_func ("/frame-layout/backdrop/draw", test_backdrop_draw);
	g_test_add_func ("/frame-layout/draw-debug/boxes", test_draw_debug);
	g_test_add_func ("/frame-layout/draw-debug/empty", test_draw_debug_empty);

	return g_test_run ();
}
