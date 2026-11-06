/* test-paint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the SDF paint list and renderer.
 *
 * The first half records lists headless and checks the instance
 * stream (packing, clamping, batching, clip and opacity stacks, group
 * transforms). The second half opens a hidden window when a display is
 * available, renders into a texture and reads pixels back to check
 * that the shader really produces the shapes.
 */

#include <glib.h>
#include <math.h>
#include <raylib.h>
#include <rlgl.h>
#include <libregnum.h>

#define TARGET_W (128)
#define TARGET_H (128)

static const GrlColor C_RED = { 255, 0, 0, 255 };
static const GrlColor C_BLUE = { 0, 0, 255, 255 };
static const GrlColor C_WHITE = { 255, 255, 255, 255 };
static const GrlColor C_BLACK = { 0, 0, 0, 255 };

/* ==========================================================================
 * Value types
 * ========================================================================== */

static void
test_instance_layout (void)
{
	/* The GPU layout is part of the shader contract: six vec4s, three
	 * RGBA8 colours and a texture id */
	g_assert_cmpuint (sizeof (LrgPaintInstance), ==, 112);
}

static void
test_corner_radii (void)
{
	g_autoptr(LrgCornerRadii) r = lrg_corner_radii_new (1.0f, 2.0f, 3.0f, 4.0f);
	g_autoptr(LrgCornerRadii) c = NULL;

	lrg_corner_radii_scale (r, 2.0f);
	c = lrg_corner_radii_copy (r);
	g_assert_cmpfloat (c->top_left, ==, 2.0f);
	g_assert_cmpfloat (c->bottom_left, ==, 8.0f);

	lrg_corner_radii_init_uniform (c, 5.0f);
	g_assert_cmpfloat (c->top_right, ==, 5.0f);
	g_assert_cmpfloat (c->bottom_right, ==, 5.0f);
}

static void
test_color_helpers (void)
{
	GrlColor out;

	lrg_paint_color_mix (&C_BLACK, &C_WHITE, 0.5f, &out);
	g_assert_cmpuint (out.r, ==, 128);
	g_assert_cmpuint (out.a, ==, 255);

	lrg_paint_color_shade (&C_RED, 0.5f, &out);
	g_assert_cmpuint (out.r, ==, 255);
	g_assert_cmpuint (out.g, ==, 128);

	lrg_paint_color_shade (&C_RED, -0.5f, &out);
	g_assert_cmpuint (out.r, ==, 128);
	g_assert_cmpuint (out.g, ==, 0);

	lrg_paint_color_with_alpha (&C_RED, 0.25f, &out);
	g_assert_cmpuint (out.a, ==, 64);

	/* WCAG reference values */
	g_assert_cmpfloat_with_epsilon (lrg_paint_color_contrast (&C_BLACK, &C_WHITE), 21.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (lrg_paint_color_contrast (&C_RED, &C_RED), 1.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (lrg_paint_color_luminance (&C_WHITE), 1.0f, 0.001f);
}

/* ==========================================================================
 * Recording
 * ========================================================================== */

static void
test_rect_packing (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;

	lrg_paint_list_rect (list, 10.0f, 20.0f, 30.0f, 40.0f, 5.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat (inst->rect[0], ==, 10.0f);
	g_assert_cmpfloat (inst->rect[1], ==, 20.0f);
	g_assert_cmpfloat (inst->rect[2], ==, 40.0f);
	g_assert_cmpfloat (inst->rect[3], ==, 60.0f);
	g_assert_cmpfloat (inst->radii[0], ==, 5.0f);
	g_assert_cmpuint (inst->color[0], ==, 255);
	g_assert_cmpuint (inst->color[3], ==, 255);
	g_assert_cmpint (lrg_paint_instance_get_prim (inst), ==, LRG_PAINT_PRIM_RECT);
	g_assert_cmpuint (inst->texture, ==, 0);
}

static void
test_radius_clamp (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgCornerRadii          radii = { 100.0f, -3.0f, 4.0f, 7.0f };
	const LrgPaintInstance *inst;

	lrg_paint_list_rect_ex (list, 0.0f, 0.0f, 20.0f, 10.0f, &radii, &C_RED, NULL, 0.0f, NULL);
	inst = lrg_paint_list_get_instance (list, 0);

	/* Half the shorter side (10 / 2) caps every radius; negatives clamp to 0 */
	g_assert_cmpfloat (inst->radii[0], ==, 5.0f);
	g_assert_cmpfloat (inst->radii[1], ==, 0.0f);
	g_assert_cmpfloat (inst->radii[2], ==, 4.0f);
	g_assert_cmpfloat (inst->radii[3], ==, 5.0f);
}

static void
test_invisible_shapes_skipped (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	GrlColor clear = { 255, 0, 0, 0 };

	lrg_paint_list_rect (list, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, &C_RED);   /* zero width */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, &clear); /* alpha 0 */
	lrg_paint_list_rect_ex (list, 0.0f, 0.0f, 10.0f, 10.0f, NULL, NULL, NULL, 0.0f, NULL);
	lrg_paint_list_line (list, 0.0f, 0.0f, 5.0f, 5.0f, 0.0f, &C_RED);    /* zero thickness */

	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 0);
}

static void
test_border_and_gradient (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgPaintGradient        grad;
	const LrgPaintInstance *inst;

	lrg_paint_gradient_init_linear (&grad, &C_RED, &C_BLUE, 90.0f);
	lrg_paint_list_rect_ex (list, 0.0f, 0.0f, 40.0f, 20.0f, NULL, NULL, &grad, 3.0f, &C_WHITE);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat (inst->params[0], ==, 3.0f);
	g_assert_cmpint ((gint) inst->params[3], ==, LRG_PAINT_GRADIENT_LINEAR);
	g_assert_cmpfloat_with_epsilon (inst->extra[0], 0.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (inst->extra[1], 1.0f, 1e-5);
	g_assert_cmpuint (inst->color[0], ==, 255);
	g_assert_cmpuint (inst->color2[2], ==, 255);
	g_assert_cmpuint (inst->border[1], ==, 255);

	/* Radial with radius 0 gets half the diagonal */
	lrg_paint_gradient_init_radial (&grad, &C_RED, &C_BLUE, 0.5f, 0.5f, 0.0f);
	lrg_paint_list_rect_ex (list, 0.0f, 0.0f, 30.0f, 40.0f, NULL, NULL, &grad, 0.0f, NULL);
	inst = lrg_paint_list_get_instance (list, 1);
	g_assert_cmpint ((gint) inst->params[3], ==, LRG_PAINT_GRADIENT_RADIAL);
	g_assert_cmpfloat_with_epsilon (inst->extra[2], 25.0f, 1e-4);

	/* A border can't be thicker than half the shape */
	lrg_paint_list_border (list, 0.0f, 0.0f, 10.0f, 6.0f, 2.0f, 50.0f, &C_WHITE);
	inst = lrg_paint_list_get_instance (list, 2);
	g_assert_cmpfloat (inst->params[0], ==, 3.0f);
}

static void
test_shadow_geometry (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	GrlColor                shade = { 0, 0, 0, 100 };
	const LrgPaintInstance *inst;

	lrg_paint_list_shadow (list, 10.0f, 10.0f, 50.0f, 30.0f, 6.0f,
	                       12.0f, 2.0f, 0.0f, 4.0f, &shade);
	inst = lrg_paint_list_get_instance (list, 0);

	g_assert_cmpint (lrg_paint_instance_get_prim (inst), ==, LRG_PAINT_PRIM_SHADOW);
	g_assert_cmpfloat (inst->rect[0], ==, 8.0f);   /* x - spread */
	g_assert_cmpfloat (inst->rect[1], ==, 12.0f);  /* y + offset - spread */
	g_assert_cmpfloat (inst->rect[2], ==, 62.0f);
	g_assert_cmpfloat (inst->rect[3], ==, 46.0f);
	g_assert_cmpfloat (inst->radii[0], ==, 8.0f);  /* radius + spread */
	g_assert_cmpfloat (inst->params[1], ==, 12.0f);
}

static void
test_line_and_circle (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;

	lrg_paint_list_line (list, 1.0f, 2.0f, 30.0f, 40.0f, 3.0f, &C_RED);
	lrg_paint_list_circle (list, 50.0f, 50.0f, 10.0f, &C_BLUE);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (lrg_paint_instance_get_prim (inst), ==, LRG_PAINT_PRIM_LINE);
	g_assert_cmpfloat (inst->rect[2], ==, 30.0f);
	g_assert_cmpfloat (inst->params[1], ==, 3.0f);

	inst = lrg_paint_list_get_instance (list, 1);
	g_assert_cmpfloat (inst->rect[0], ==, 40.0f);
	g_assert_cmpfloat (inst->radii[2], ==, 10.0f);
}

static void
test_triangle (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;

	lrg_paint_list_triangle (list, 10.0f, 10.0f, 30.0f, 10.0f, 20.0f, 30.0f, &C_RED);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpint (lrg_paint_instance_get_prim (inst), ==, LRG_PAINT_PRIM_TRIANGLE);
	g_assert_cmpfloat (inst->rect[2], ==, 30.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 20.0f);
	g_assert_cmpfloat (inst->extra[1], ==, 30.0f);

	/* The third vertex moves with group transforms too */
	lrg_paint_list_transform_group (list, 0, 1.0f, 0.0f, 0.0f, 5.0f, 7.0f, 1.0f);
	g_assert_cmpfloat (inst->extra[0], ==, 25.0f);
	g_assert_cmpfloat (inst->extra[1], ==, 37.0f);
}

static void
test_batching_merges_untextured (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	guint                   i;
	guint                   first;
	guint                   count;

	for (i = 0; i < 50; i++)
		lrg_paint_list_rect (list, (gfloat) i, 0.0f, 4.0f, 4.0f, 1.0f, &C_RED);

	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);
	g_assert_cmpint (lrg_paint_list_get_command (list, 0, &first, &count, NULL), ==,
	                 LRG_PAINT_COMMAND_INSTANCES);
	g_assert_cmpuint (first, ==, 0);
	g_assert_cmpuint (count, ==, 50);
}

static gint callback_runs = 0;

static void
count_callback (gpointer user_data)
{
	callback_runs++;
}

static void
test_callback_splits_batches (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	guint                   first;
	guint                   count;
	gfloat                  x0;
	gfloat                  y1;

	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	lrg_paint_list_push_clip (list, 1.0f, 2.0f, 3.0f, 4.0f);
	lrg_paint_list_callback (list, count_callback, NULL, NULL);
	lrg_paint_list_pop_clip (list);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	lrg_paint_list_callback (list, count_callback, NULL, NULL);

	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 4);
	g_assert_cmpint (lrg_paint_list_get_command (list, 1, NULL, NULL, NULL), ==,
	                 LRG_PAINT_COMMAND_CALLBACK);
	g_assert_cmpint (lrg_paint_list_get_command (list, 2, &first, &count, NULL), ==,
	                 LRG_PAINT_COMMAND_INSTANCES);
	g_assert_cmpuint (first, ==, 1);
	g_assert_cmpuint (count, ==, 1);

	g_assert_true (lrg_paint_list_get_command_clip (list, 1, &x0, NULL, NULL, &y1));
	g_assert_cmpfloat (x0, ==, 1.0f);
	g_assert_cmpfloat (y1, ==, 6.0f);
	g_assert_false (lrg_paint_list_get_command_clip (list, 3, NULL, NULL, NULL, NULL));

	callback_runs = 0;
	lrg_paint_list_run_callback (list, 1);
	lrg_paint_list_run_callback (list, 3);
	g_assert_cmpint (callback_runs, ==, 2);
}

static void
on_destroy_count (gpointer data)
{
	(*(gint *) data)++;
}

static void
test_callback_destroy_on_clear (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	gint                    destroyed = 0;

	lrg_paint_list_callback (list, count_callback, &destroyed, on_destroy_count);
	lrg_paint_list_callback (list, count_callback, &destroyed, on_destroy_count);
	lrg_paint_list_clear (list);
	g_assert_cmpint (destroyed, ==, 2);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 0);
}

static void
test_clip_stack (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	gfloat                  x;
	gfloat                  y;
	gfloat                  w;
	gfloat                  h;
	const LrgPaintInstance *inst;

	lrg_paint_list_push_clip (list, 10.0f, 10.0f, 100.0f, 100.0f);
	lrg_paint_list_push_clip (list, 50.0f, 0.0f, 100.0f, 30.0f);
	lrg_paint_list_get_clip (list, &x, &y, &w, &h);

	/* Intersection of both */
	g_assert_cmpfloat (x, ==, 50.0f);
	g_assert_cmpfloat (y, ==, 10.0f);
	g_assert_cmpfloat (w, ==, 60.0f);
	g_assert_cmpfloat (h, ==, 20.0f);

	lrg_paint_list_rect (list, 60.0f, 15.0f, 5.0f, 5.0f, 0.0f, &C_RED);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat (inst->clip[0], ==, 50.0f);
	g_assert_cmpfloat (inst->clip[3], ==, 30.0f);

	/* Fully outside the clip: culled at record time */
	lrg_paint_list_rect (list, 0.0f, 80.0f, 5.0f, 5.0f, 0.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);

	lrg_paint_list_pop_clip (list);
	lrg_paint_list_get_clip (list, &x, NULL, &w, NULL);
	g_assert_cmpfloat (x, ==, 10.0f);
	g_assert_cmpfloat (w, ==, 100.0f);

	/* Disjoint push collapses to an empty clip and culls everything */
	lrg_paint_list_push_clip (list, 500.0f, 500.0f, 10.0f, 10.0f);
	lrg_paint_list_get_clip (list, NULL, NULL, &w, &h);
	g_assert_cmpfloat (w, ==, 0.0f);
	g_assert_cmpfloat (h, ==, 0.0f);
	lrg_paint_list_pop_clip (list);
	lrg_paint_list_pop_clip (list);
}

static void
test_unbalanced_pops_warn (void)
{
	if (g_test_subprocess ())
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		lrg_paint_list_pop_clip (list);
		lrg_paint_list_pop_opacity (list);
		return;
	}

	g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
	g_test_trap_assert_stderr ("*pop_clip without a matching push_clip*");
}

static void
test_opacity_stack (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;

	lrg_paint_list_push_opacity (list, 0.5f);
	lrg_paint_list_push_opacity (list, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_paint_list_get_opacity (list), 0.25f, 1e-6);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	lrg_paint_list_pop_opacity (list);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	lrg_paint_list_pop_opacity (list);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);

	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->color[3], ==, 64);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 1)->color[3], ==, 128);
	inst = lrg_paint_list_get_instance (list, 2);
	g_assert_cmpuint (inst->color[3], ==, 255);
}

static void
test_group_transform (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	const LrgPaintInstance *inst;
	guint                   mark;

	lrg_paint_list_rect (list, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, &C_BLUE);  /* outside group */
	mark = lrg_paint_list_begin_group (list);
	g_assert_cmpuint (mark, ==, 1);
	lrg_paint_list_rect_ex (list, 100.0f, 100.0f, 20.0f, 20.0f, NULL, &C_RED, NULL, 2.0f, &C_WHITE);
	lrg_paint_list_line (list, 100.0f, 100.0f, 120.0f, 100.0f, 4.0f, &C_RED);

	/* Scale 0.5 about (110,110), shift +5,+0, half opacity */
	lrg_paint_list_transform_group (list, mark, 0.5f, 110.0f, 110.0f, 5.0f, 0.0f, 0.5f);

	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpfloat (inst->rect[0], ==, 0.0f);
	g_assert_cmpuint (inst->color[3], ==, 255);

	inst = lrg_paint_list_get_instance (list, 1);
	g_assert_cmpfloat (inst->rect[0], ==, 110.0f);
	g_assert_cmpfloat (inst->rect[1], ==, 105.0f);
	g_assert_cmpfloat (inst->rect[2], ==, 120.0f);
	g_assert_cmpfloat (inst->rect[3], ==, 115.0f);
	g_assert_cmpfloat (inst->params[0], ==, 1.0f);
	g_assert_cmpuint (inst->color[3], ==, 128);
	g_assert_cmpuint (inst->border[3], ==, 128);

	inst = lrg_paint_list_get_instance (list, 2);
	g_assert_cmpfloat (inst->params[1], ==, 2.0f);
	g_assert_cmpfloat (inst->rect[0], ==, 110.0f);
	g_assert_cmpfloat (inst->rect[2], ==, 120.0f);
}

static void
test_clear_resets (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	gfloat                  w;

	lrg_paint_list_push_clip (list, 0.0f, 0.0f, 10.0f, 10.0f);
	lrg_paint_list_push_opacity (list, 0.1f);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);
	lrg_paint_list_clear (list);

	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);
	g_assert_cmpfloat (lrg_paint_list_get_opacity (list), ==, 1.0f);
	lrg_paint_list_get_clip (list, NULL, NULL, &w, NULL);
	g_assert_cmpfloat (w, >, 1.0e6f);
}

static void
test_stress (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	guint                   i;

	for (i = 0; i < 20000; i++)
		lrg_paint_list_rect (list, (gfloat) (i % 640), (gfloat) (i / 640), 3.0f, 3.0f, 1.0f, &C_RED);

	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 20000);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);
}

static void
test_measure_without_window (void)
{
	gfloat w;
	gfloat h;

	/* No glyph data without a window: an estimate that still grows
	 * with the text and counts lines. Runs in a subprocess, which never
	 * opens the hidden test window. */
	if (!g_test_subprocess ())
	{
		g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
		g_test_trap_assert_passed ();
		return;
	}
	lrg_paint_measure_text (NULL, "abcd", 20.0f, 0.0f, &w, &h);
	g_assert_cmpfloat (w, >, 0.0f);
	g_assert_cmpfloat (h, ==, 20.0f);

	lrg_paint_measure_text (NULL, "ab\ncd", 20.0f, 0.0f, NULL, &h);
	g_assert_cmpfloat (h, ==, 44.0f);
}

static void
test_renderer_headless (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgPaintRenderer       *renderer;

	if (!g_test_subprocess ())
	{
		g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
		g_test_trap_assert_passed ();
		return;
	}

	renderer = lrg_paint_renderer_get_default ();
	g_assert_true (renderer == lrg_paint_renderer_get_default ());
	lrg_paint_list_rect (list, 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, &C_RED);

	g_assert_false (lrg_paint_renderer_is_available ());
	g_assert_false (lrg_paint_renderer_render (renderer, list));
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 0);
}

/* ==========================================================================
 * GPU rendering (hidden window)
 * ========================================================================== */

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

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
	test_window = grl_window_new (8, 8, "lrg-paint-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

#define SKIP_WITHOUT_GL() \
	do { \
		if (!graphics_available) \
		{ \
			g_test_skip ("Graphics context not available"); \
			return; \
		} \
	} while (0)

/* render_list: draw @list over black into a texture, return pixels */
static Image
render_list (LrgPaintList *list)
{
	RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (), list));
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static Color
pixel (Image *image,
       gint   x,
       gint   y)
{
	return GetImageColor (*image, x, y);
}

static void
test_gpu_rect_fill_and_corners (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect (list, 16.0f, 16.0f, 96.0f, 96.0f, 24.0f, &C_RED);
	img = render_list (list);

	/* Solid in the middle */
	c = pixel (&img, 64, 64);
	g_assert_cmpuint (c.r, >=, 250);
	g_assert_cmpuint (c.g, <=, 5);

	/* Rounded away at the very corner, solid along the straight edge */
	c = pixel (&img, 17, 17);
	g_assert_cmpuint (c.r, <=, 10);
	c = pixel (&img, 64, 17);
	g_assert_cmpuint (c.r, >=, 250);

	/* Outside */
	c = pixel (&img, 5, 64);
	g_assert_cmpuint (c.r, ==, 0);

	UnloadImage (img);
}

static void
test_gpu_antialiased_edge (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();

	/* Edge at x = 40.5 cuts pixel 40 in half */
	lrg_paint_list_rect (list, 40.5f, 10.0f, 40.0f, 40.0f, 0.0f, &C_WHITE);
	img = render_list (list);

	c = pixel (&img, 40, 30);
	g_assert_cmpuint (c.r, >, 90);
	g_assert_cmpuint (c.r, <, 165);
	c = pixel (&img, 41, 30);
	g_assert_cmpuint (c.r, >=, 250);

	UnloadImage (img);
}

static void
test_gpu_border (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect_ex (list, 20.0f, 20.0f, 80.0f, 80.0f, NULL, &C_RED, NULL, 6.0f, &C_BLUE);
	img = render_list (list);

	c = pixel (&img, 22, 60);   /* inside the border band */
	g_assert_cmpuint (c.b, >=, 250);
	g_assert_cmpuint (c.r, <=, 5);
	c = pixel (&img, 60, 60);   /* fill */
	g_assert_cmpuint (c.r, >=, 250);
	g_assert_cmpuint (c.b, <=, 5);

	UnloadImage (img);
}

static void
test_gpu_linear_gradient (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	LrgPaintGradient        grad;
	Image                   img;
	Color                   left;
	Color                   mid;
	Color                   right;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_gradient_init_linear (&grad, &C_BLACK, &C_WHITE, 0.0f);
	lrg_paint_list_rect_ex (list, 0.0f, 0.0f, 128.0f, 128.0f, NULL, NULL, &grad, 0.0f, NULL);
	img = render_list (list);

	left = pixel (&img, 1, 64);
	mid = pixel (&img, 64, 64);
	right = pixel (&img, 126, 64);
	g_assert_cmpuint (left.r, <, 10);
	g_assert_cmpuint (mid.r, >, 115);
	g_assert_cmpuint (mid.r, <, 140);
	g_assert_cmpuint (right.r, >, 245);

	UnloadImage (img);
}

static void
test_gpu_shadow_falloff (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	Color                   inside;
	Color                   edge;
	Color                   far;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_shadow (list, 32.0f, 32.0f, 64.0f, 64.0f, 8.0f,
	                       16.0f, 0.0f, 0.0f, 0.0f, &C_WHITE);
	img = render_list (list);

	inside = pixel (&img, 64, 64);
	edge = pixel (&img, 32, 64);
	far = pixel (&img, 10, 64);

	/* Bright inside, half at the edge, fading to nothing */
	g_assert_cmpuint (inside.r, >, 240);
	g_assert_cmpuint (edge.r, >, 90);
	g_assert_cmpuint (edge.r, <, 165);
	g_assert_cmpuint (far.r, <, 20);
	g_assert_cmpuint (inside.r, >, edge.r);
	g_assert_cmpuint (edge.r, >, far.r);

	UnloadImage (img);
}

static void
test_gpu_line (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_line (list, 10.0f, 64.0f, 118.0f, 64.0f, 6.0f, &C_WHITE);
	img = render_list (list);

	g_assert_cmpuint (pixel (&img, 64, 64).r, >=, 250);
	g_assert_cmpuint (pixel (&img, 64, 74).r, ==, 0);
	/* Round cap reaches a little past the end point */
	g_assert_cmpuint (pixel (&img, 119, 64).r, >, 100);

	UnloadImage (img);
}

static void
test_gpu_clip (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_push_clip (list, 0.0f, 0.0f, 64.0f, 128.0f);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &C_WHITE);
	lrg_paint_list_pop_clip (list);
	img = render_list (list);

	g_assert_cmpuint (pixel (&img, 32, 64).r, >=, 250);
	g_assert_cmpuint (pixel (&img, 96, 64).r, ==, 0);

	UnloadImage (img);
}

static void
test_gpu_order_and_alpha (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	GrlColor                half_blue = { 0, 0, 255, 128 };
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &C_RED);
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &half_blue);
	img = render_list (list);

	/* Later shapes composite over earlier ones */
	c = pixel (&img, 64, 64);
	g_assert_cmpuint (c.r, >, 115);
	g_assert_cmpuint (c.r, <, 140);
	g_assert_cmpuint (c.b, >, 115);
	g_assert_cmpuint (c.b, <, 140);

	UnloadImage (img);
}

static void
test_gpu_triangle (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();

	/* Clockwise on screen; winding must not matter */
	lrg_paint_list_triangle (list, 20.0f, 20.0f, 108.0f, 20.0f, 64.0f, 108.0f, &C_WHITE);
	img = render_list (list);

	g_assert_cmpuint (pixel (&img, 64, 40).r, >=, 250);   /* inside */
	g_assert_cmpuint (pixel (&img, 25, 100).r, ==, 0);    /* outside, lower left */
	g_assert_cmpuint (pixel (&img, 64, 10).r, ==, 0);     /* above the top edge */
	UnloadImage (img);

	lrg_paint_list_clear (list);
	lrg_paint_list_triangle (list, 20.0f, 20.0f, 64.0f, 108.0f, 108.0f, 20.0f, &C_WHITE);
	img = render_list (list);
	g_assert_cmpuint (pixel (&img, 64, 40).r, >=, 250);
	UnloadImage (img);
}

static void
test_gpu_image_alpha_not_brightened (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	g_autoptr(GrlTexture)   tex = NULL;
	g_autoptr(GrlImage)     src = NULL;
	guint8                  pixels[4 * 4 * 4];
	guint                   i;
	Image                   img;
	Color                   c;

	SKIP_WITHOUT_GL ();

	/* A half-transparent white texture over black must give ~50% grey,
	 * not white: texels are straight alpha and must be premultiplied
	 * (this is what anti-aliased glyph edges rely on) */
	for (i = 0; i < sizeof (pixels); i += 4)
	{
		pixels[i] = pixels[i + 1] = pixels[i + 2] = 255;
		pixels[i + 3] = 128;
	}
	src = grl_image_new_from_pixels (4, 4, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels);
	tex = grl_texture_new_from_image (src);

	list = lrg_paint_list_new ();
	lrg_paint_list_image (list, tex, NULL, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, NULL);
	img = render_list (list);
	c = pixel (&img, 64, 64);
	g_assert_cmpuint (c.r, >, 110);
	g_assert_cmpuint (c.r, <, 145);
	UnloadImage (img);
}

static void
draw_green_square (gpointer user_data)
{
	DrawRectangle (0, 0, 128, 128, (Color){ 0, 255, 0, 255 });
}

static void
test_gpu_callback_clipped (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, &C_RED);
	lrg_paint_list_push_clip (list, 0.0f, 0.0f, 64.0f, 128.0f);
	lrg_paint_list_callback (list, draw_green_square, NULL, NULL);
	lrg_paint_list_pop_clip (list);
	img = render_list (list);

	g_assert_cmpuint (pixel (&img, 32, 64).g, >=, 250);
	g_assert_cmpuint (pixel (&img, 96, 64).r, >=, 250);
	g_assert_cmpuint (pixel (&img, 96, 64).g, ==, 0);

	UnloadImage (img);
}

static void
test_gpu_text_batches_with_shapes (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	Image                   img;
	gint                    x;
	gint                    y;
	gboolean                ink = FALSE;
	LrgPaintRenderer       *renderer;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	renderer = lrg_paint_renderer_get_default ();

	/* Default font: glyphs sample its atlas; untextured shapes on
	 * either side merge into the same batch */
	lrg_paint_list_rect (list, 0.0f, 0.0f, 128.0f, 30.0f, 0.0f, &C_BLUE);
	lrg_paint_list_text (list, NULL, "Hi!", 4.0f, 40.0f, 20.0f, 2.0f, &C_WHITE);
	lrg_paint_list_rect (list, 0.0f, 100.0f, 128.0f, 28.0f, 0.0f, &C_RED);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >=, 5);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 1);

	img = render_list (list);
	g_assert_cmpuint (lrg_paint_renderer_get_draw_calls (renderer), ==, 1);

	for (y = 40; y < 62 && !ink; y++)
		for (x = 4; x < 60 && !ink; x++)
			if (pixel (&img, x, y).r > 200)
				ink = TRUE;
	g_assert_true (ink);

	UnloadImage (img);
}

static void
test_gpu_measure_matches_layout (void)
{
	g_autoptr(LrgPaintList) list = NULL;
	gfloat                  w;
	gfloat                  h;
	guint                   n;
	const LrgPaintInstance *inst;

	SKIP_WITHOUT_GL ();
	list = lrg_paint_list_new ();
	lrg_paint_measure_text (NULL, "Wide text", 20.0f, 1.0f, &w, &h);
	lrg_paint_list_text (list, NULL, "Wide text", 0.0f, 0.0f, 20.0f, 1.0f, &C_WHITE);

	/* The last glyph ends near the measured width */
	inst = lrg_paint_list_get_instances (list, &n);
	g_assert_cmpuint (n, ==, 8);   /* the space is not drawn */
	g_assert_cmpfloat (fabsf (inst[n - 1].rect[2] - w), <, 6.0f);
	g_assert_cmpfloat (h, ==, 20.0f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/paint/types/instance-layout", test_instance_layout);
	g_test_add_func ("/paint/types/corner-radii", test_corner_radii);
	g_test_add_func ("/paint/types/colors", test_color_helpers);

	g_test_add_func ("/paint/list/rect-packing", test_rect_packing);
	g_test_add_func ("/paint/list/radius-clamp", test_radius_clamp);
	g_test_add_func ("/paint/list/invisible-skipped", test_invisible_shapes_skipped);
	g_test_add_func ("/paint/list/border-and-gradient", test_border_and_gradient);
	g_test_add_func ("/paint/list/shadow-geometry", test_shadow_geometry);
	g_test_add_func ("/paint/list/line-and-circle", test_line_and_circle);
	g_test_add_func ("/paint/list/triangle", test_triangle);
	g_test_add_func ("/paint/list/batching", test_batching_merges_untextured);
	g_test_add_func ("/paint/list/callback-splits", test_callback_splits_batches);
	g_test_add_func ("/paint/list/callback-destroy", test_callback_destroy_on_clear);
	g_test_add_func ("/paint/list/clip-stack", test_clip_stack);
	g_test_add_func ("/paint/list/unbalanced-pops", test_unbalanced_pops_warn);
	g_test_add_func ("/paint/list/opacity-stack", test_opacity_stack);
	g_test_add_func ("/paint/list/group-transform", test_group_transform);
	g_test_add_func ("/paint/list/clear", test_clear_resets);
	g_test_add_func ("/paint/list/stress", test_stress);
	g_test_add_func ("/paint/list/measure-headless", test_measure_without_window);
	g_test_add_func ("/paint/renderer/headless", test_renderer_headless);

	if (!g_test_subprocess ())
		graphics_available = init_graphics_context ();

	g_test_add_func ("/paint/gpu/rect-fill-and-corners", test_gpu_rect_fill_and_corners);
	g_test_add_func ("/paint/gpu/antialiased-edge", test_gpu_antialiased_edge);
	g_test_add_func ("/paint/gpu/border", test_gpu_border);
	g_test_add_func ("/paint/gpu/linear-gradient", test_gpu_linear_gradient);
	g_test_add_func ("/paint/gpu/shadow-falloff", test_gpu_shadow_falloff);
	g_test_add_func ("/paint/gpu/line", test_gpu_line);
	g_test_add_func ("/paint/gpu/clip", test_gpu_clip);
	g_test_add_func ("/paint/gpu/order-and-alpha", test_gpu_order_and_alpha);
	g_test_add_func ("/paint/gpu/triangle", test_gpu_triangle);
	g_test_add_func ("/paint/gpu/image-alpha", test_gpu_image_alpha_not_brightened);
	g_test_add_func ("/paint/gpu/callback-clipped", test_gpu_callback_clipped);
	g_test_add_func ("/paint/gpu/text-batches", test_gpu_text_batches_with_shapes);
	g_test_add_func ("/paint/gpu/measure-matches-layout", test_gpu_measure_matches_layout);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
