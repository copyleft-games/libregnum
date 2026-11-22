/* test-beam.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgBeam's geometry, headless: straight and arced curves in
 * both bends, length, the dash pattern and its flow, progress, and
 * moving ends.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>

static void
assert_dash (LrgBeam *beam,
             guint    index,
             gfloat   from,
             gfloat   to)
{
	gfloat a;
	gfloat b;

	g_assert_true (lrg_beam_get_dash (beam, index, &a, &b));
	g_assert_cmpfloat_with_epsilon (a, from, 1e-3f);
	g_assert_cmpfloat_with_epsilon (b, to, 1e-3f);
}

static void
test_straight (void)
{
	g_autoptr(LrgBeam) beam = lrg_beam_new ();
	gfloat x;
	gfloat y;
	gfloat z;

	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_beam_get_length (beam), 100.0f, 1e-3f);
	lrg_beam_sample (beam, 0.5f, &x, &y, &z);
	g_assert_cmpfloat_with_epsilon (x, 50.0f, 1e-4f);
	g_assert_cmpfloat (y, ==, 0.0f);
	lrg_beam_sample (beam, 2.0f, &x, NULL, NULL);    /* clamped */
	g_assert_cmpfloat (x, ==, 100.0f);

	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 1);
	assert_dash (beam, 0, 0.0f, 100.0f);
	g_assert_false (lrg_beam_get_dash (beam, 1, NULL, NULL));

	/* ends that move */
	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 30.0f, 40.0f, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_beam_get_length (beam), 50.0f, 1e-3f);
}

static void
test_arcs (void)
{
	g_autoptr(LrgBeam) beam = lrg_beam_new ();
	gfloat x;
	gfloat y;
	gfloat z;

	/* 2D: a beam going right bulges up (towards -y) */
	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
	lrg_beam_set_arc (beam, 20.0f, LRG_BEAM_BEND_PERPENDICULAR);
	lrg_beam_sample (beam, 0.5f, &x, &y, &z);
	g_assert_cmpfloat_with_epsilon (x, 50.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (y, -20.0f, 1e-4f);
	g_assert_cmpfloat (lrg_beam_get_length (beam), >, 100.0f);
	g_assert_cmpfloat (lrg_beam_get_length (beam), <, 115.0f);
	lrg_beam_set_arc (beam, -20.0f, LRG_BEAM_BEND_PERPENDICULAR);
	lrg_beam_sample (beam, 0.5f, NULL, &y, NULL);
	g_assert_cmpfloat_with_epsilon (y, 20.0f, 1e-4f);

	/* 3D: bulges up the axis, +y by default */
	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
	lrg_beam_set_arc (beam, 3.0f, LRG_BEAM_BEND_AXIS);
	lrg_beam_sample (beam, 0.5f, &x, &y, &z);
	g_assert_cmpfloat_with_epsilon (x, 5.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (y, 3.0f, 1e-4f);
	lrg_beam_set_arc_axis (beam, 0.0f, 0.0f, 2.0f);
	lrg_beam_sample (beam, 0.5f, NULL, &y, &z);
	g_assert_cmpfloat_with_epsilon (y, 0.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (z, 3.0f, 1e-4f);
	lrg_beam_set_arc_axis (beam, 0.0f, 0.0f, 0.0f);   /* ignored */
	lrg_beam_sample (beam, 0.5f, NULL, NULL, &z);
	g_assert_cmpfloat_with_epsilon (z, 3.0f, 1e-4f);

	/* the polyline length approaches the curve with more segments */
	lrg_beam_set_segments (beam, 1);
	g_assert_cmpfloat_with_epsilon (lrg_beam_get_length (beam), 10.0f, 1e-4f);
	lrg_beam_set_segments (beam, 64);
	g_assert_cmpfloat (lrg_beam_get_length (beam), >, 11.0f);
}

static void
test_dashes_flow (void)
{
	g_autoptr(LrgBeam) beam = lrg_beam_new ();

	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
	lrg_beam_set_dash (beam, 10.0f, 10.0f, 5.0f);
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 5);
	assert_dash (beam, 0, 0.0f, 10.0f);
	assert_dash (beam, 4, 80.0f, 90.0f);

	lrg_beam_update (beam, 1.0f);       /* moved 5 towards the end */
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 5);
	assert_dash (beam, 0, 5.0f, 15.0f);

	lrg_beam_update (beam, 2.0f);       /* 15: a dash enters at the start */
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 6);
	assert_dash (beam, 0, 0.0f, 5.0f);
	assert_dash (beam, 1, 15.0f, 25.0f);
	assert_dash (beam, 5, 95.0f, 100.0f);

	/* the pattern repeats every 20: a full period changes nothing */
	lrg_beam_update (beam, 4.0f);
	assert_dash (beam, 0, 0.0f, 5.0f);

	/* flowing backwards */
	lrg_beam_set_dash (beam, 10.0f, 10.0f, -5.0f);
	lrg_beam_update (beam, 1.0f);
	assert_dash (beam, 0, 0.0f, 5.0f);
	assert_dash (beam, 1, 15.0f, 25.0f);

	/* a solid beam does not flow */
	lrg_beam_set_dash (beam, 10.0f, 0.0f, 50.0f);
	lrg_beam_update (beam, 1.0f);
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 1);
}

static void
test_progress (void)
{
	g_autoptr(LrgBeam) beam = lrg_beam_new ();
	GrlColor red = { 255, 0, 0, 200 };

	lrg_beam_set_endpoints (beam, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f);
	lrg_beam_set_progress (beam, 0.5f);
	g_assert_cmpfloat (lrg_beam_get_progress (beam), ==, 0.5f);
	assert_dash (beam, 0, 0.0f, 50.0f);

	lrg_beam_set_dash (beam, 10.0f, 10.0f, 0.0f);
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 3);
	assert_dash (beam, 2, 40.0f, 50.0f);

	lrg_beam_set_progress (beam, -1.0f);
	g_assert_cmpfloat (lrg_beam_get_progress (beam), ==, 0.0f);
	g_assert_cmpuint (lrg_beam_get_n_dashes (beam), ==, 0);

	/* drawing without a window does nothing, harmlessly */
	lrg_beam_set_progress (beam, 1.0f);
	lrg_beam_set_color (beam, &red);
	lrg_beam_set_width (beam, 0.0f);
	lrg_beam_draw_2d (beam);
	lrg_beam_draw_3d (beam);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/beam/straight", test_straight);
	g_test_add_func ("/beam/arcs", test_arcs);
	g_test_add_func ("/beam/dashes-flow", test_dashes_flow);
	g_test_add_func ("/beam/progress", test_progress);

	return g_test_run ();
}
