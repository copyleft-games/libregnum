/* test-spring.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgSpring and the global motion settings.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* run_until_settled: steps at 60 Hz, returns seconds taken (or -1) */
static gfloat
run_until_settled (LrgSpring *spring,
                   gfloat     limit)
{
	gfloat t = 0.0f;

	while (t < limit)
	{
		if (!lrg_spring_step (spring, DT))
			return t;
		t += DT;
	}
	return -1.0f;
}

/* Reset global state around every test */
static void
reset_motion (void)
{
	lrg_motion_set_speed (1.0f);
	lrg_motion_set_reduced (FALSE);
}

static void
test_spring_init (void)
{
	LrgSpring s;
	gfloat    r;
	gfloat    d;

	reset_motion ();
	lrg_spring_init (&s, 3.0f, LRG_MOTION_BOUNCY);
	lrg_motion_get_params (LRG_MOTION_BOUNCY, &r, &d);

	g_assert_cmpfloat (s.value, ==, 3.0f);
	g_assert_cmpfloat (s.target, ==, 3.0f);
	g_assert_cmpfloat (s.velocity, ==, 0.0f);
	g_assert_cmpfloat (s.response, ==, r);
	g_assert_cmpfloat (s.damping, ==, d);
	g_assert_true (lrg_spring_is_settled (&s));
	g_assert_false (lrg_spring_step (&s, DT));
}

static void
test_spring_presets (void)
{
	gfloat r;
	gfloat d;

	lrg_motion_get_params (LRG_MOTION_SNAPPY, &r, &d);
	g_assert_cmpfloat (r, ==, 0.16f);
	g_assert_cmpfloat (d, ==, 0.90f);
	lrg_motion_get_params (LRG_MOTION_SMOOTH, &r, &d);
	g_assert_cmpfloat (d, ==, 1.0f);
	lrg_motion_get_params (LRG_MOTION_WOBBLY, NULL, &d);
	g_assert_cmpfloat (d, ==, 0.32f);
	lrg_motion_get_params (LRG_MOTION_NONE, &r, NULL);
	g_assert_cmpfloat (r, ==, 0.0f);
}

static void
test_spring_critical_no_overshoot (void)
{
	LrgSpring s;
	gfloat    peak = 0.0f;
	gfloat    t;

	reset_motion ();
	lrg_spring_init (&s, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_set_target (&s, 1.0f);

	for (t = 0.0f; t < 2.0f; t += DT)
	{
		lrg_spring_step (&s, DT);
		peak = MAX (peak, s.value);
	}

	g_assert_cmpfloat (peak, <=, 1.0f + 1e-4f);
	g_assert_true (lrg_spring_is_settled (&s));
	g_assert_cmpfloat (s.value, ==, 1.0f);
}

static void
test_spring_underdamped_overshoots (void)
{
	LrgSpring s;
	gfloat    peak = 0.0f;
	gfloat    t;

	reset_motion ();
	lrg_spring_init (&s, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_set_target (&s, 1.0f);

	for (t = 0.0f; t < 3.0f; t += DT)
	{
		lrg_spring_step (&s, DT);
		peak = MAX (peak, s.value);
	}

	/* z = 0.52: the textbook overshoot exp(-pi z / sqrt(1 - z^2)) ~ 15% */
	g_assert_cmpfloat (peak, >, 1.10f);
	g_assert_cmpfloat (peak, <, 1.20f);
	g_assert_cmpfloat (s.value, ==, 1.0f);
}

static void
test_spring_overdamped_converges (void)
{
	LrgSpring s;
	gfloat    took;

	reset_motion ();
	lrg_spring_init_full (&s, 10.0f, 0.3f, 2.5f);
	lrg_spring_set_target (&s, 0.0f);
	took = run_until_settled (&s, 10.0f);

	g_assert_cmpfloat (took, >, 0.0f);
	g_assert_cmpfloat (s.value, ==, 0.0f);
}

static void
test_spring_frame_rate_independent (void)
{
	LrgSpring coarse;
	LrgSpring fine;
	guint     i;

	reset_motion ();

	/* Closed-form steps: one 1/30 s step lands where two 1/60 s do,
	 * in every damping regime */
	for (i = 0; i < 3; i++)
	{
		gfloat damping = i == 0 ? 0.4f : (i == 1 ? 1.0f : 1.8f);
		guint  k;

		lrg_spring_init_full (&coarse, 0.0f, 0.4f, damping);
		lrg_spring_init_full (&fine, 0.0f, 0.4f, damping);
		lrg_spring_set_target (&coarse, 100.0f);
		lrg_spring_set_target (&fine, 100.0f);

		for (k = 0; k < 10; k++)
		{
			lrg_spring_step (&coarse, 2.0f * DT);
			lrg_spring_step (&fine, DT);
			lrg_spring_step (&fine, DT);
			g_assert_cmpfloat_with_epsilon (coarse.value, fine.value, 1e-3);
			g_assert_cmpfloat_with_epsilon (coarse.velocity, fine.velocity, 1e-2);
		}
	}
}

static void
test_spring_retarget_keeps_velocity (void)
{
	LrgSpring s;
	gfloat    v;

	reset_motion ();
	lrg_spring_init (&s, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_set_target (&s, 100.0f);
	lrg_spring_step (&s, DT);
	lrg_spring_step (&s, DT);
	v = s.velocity;
	g_assert_cmpfloat (v, >, 0.0f);

	lrg_spring_set_target (&s, -100.0f);
	g_assert_cmpfloat (s.velocity, ==, v);

	/* It keeps moving forward briefly before turning around */
	lrg_spring_step (&s, 0.001f);
	g_assert_cmpfloat (s.velocity, >, 0.0f);
}

static void
test_spring_presets_settle_in_time (void)
{
	LrgMotion motions[] = { LRG_MOTION_SNAPPY, LRG_MOTION_SMOOTH,
	                        LRG_MOTION_BOUNCY, LRG_MOTION_WOBBLY };
	gfloat    limits[] = { 0.6f, 1.0f, 2.0f, 4.0f };
	guint     i;

	reset_motion ();
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
	{
		LrgSpring s;
		gfloat    took;

		lrg_spring_init (&s, 0.0f, motions[i]);
		lrg_spring_set_target (&s, 1.0f);
		took = run_until_settled (&s, 10.0f);
		g_assert_cmpfloat (took, >, 0.0f);
		g_assert_cmpfloat (took, <, limits[i]);
	}
}

static void
test_spring_none_and_reduced_snap (void)
{
	LrgSpring s;

	reset_motion ();
	lrg_spring_init (&s, 0.0f, LRG_MOTION_NONE);
	lrg_spring_set_target (&s, 5.0f);
	g_assert_false (lrg_spring_step (&s, DT));
	g_assert_cmpfloat (s.value, ==, 5.0f);

	lrg_motion_set_reduced (TRUE);
	g_assert_true (lrg_motion_get_reduced ());
	lrg_spring_init (&s, 0.0f, LRG_MOTION_WOBBLY);
	lrg_spring_set_target (&s, 7.0f);
	g_assert_false (lrg_spring_step (&s, DT));
	g_assert_cmpfloat (s.value, ==, 7.0f);
	reset_motion ();
}

static void
test_spring_speed_scaling (void)
{
	LrgSpring normal;
	LrgSpring slow;

	reset_motion ();
	lrg_spring_init (&normal, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&slow, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_set_target (&normal, 1.0f);
	lrg_spring_set_target (&slow, 1.0f);

	lrg_spring_step (&normal, DT);
	lrg_motion_set_speed (0.5f);
	g_assert_cmpfloat (lrg_motion_get_speed (), ==, 0.5f);
	lrg_spring_step (&slow, 2.0f * DT);
	g_assert_cmpfloat_with_epsilon (normal.value, slow.value, 1e-5);

	lrg_motion_set_speed (1000.0f);
	g_assert_cmpfloat (lrg_motion_get_speed (), ==, 10.0f);
	reset_motion ();
}

static void
test_spring_hitch_is_capped (void)
{
	LrgSpring s;

	reset_motion ();
	lrg_spring_init (&s, 0.0f, LRG_MOTION_WOBBLY);
	lrg_spring_set_target (&s, 1.0f);

	/* A 5 s frame hitch advances at most MAX_STEP, not the whole
	 * animation, so the spring is still in motion afterwards */
	g_assert_true (lrg_spring_step (&s, 5.0f));
	g_assert_false (lrg_spring_is_settled (&s));
}

static void
test_spring_boxed (void)
{
	g_autoptr(LrgSpring) a = lrg_spring_new (2.0f, LRG_MOTION_SMOOTH);
	g_autoptr(LrgSpring) b = NULL;

	lrg_spring_set_target (a, 9.0f);
	b = lrg_spring_copy (a);
	g_assert_cmpfloat (b->target, ==, 9.0f);
	lrg_spring_snap (b, 4.0f);
	g_assert_cmpfloat (b->value, ==, 4.0f);
	g_assert_true (lrg_spring_is_settled (b));
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_SPRING));
}

static void
test_motion_follows_accessibility (void)
{
	g_autoptr(LrgAccessibilitySettings) settings = lrg_accessibility_settings_new ();

	reset_motion ();
	lrg_accessibility_settings_set_reduce_motion (settings, TRUE);
	lrg_motion_follow_accessibility (settings);
	g_assert_true (lrg_motion_get_reduced ());

	lrg_accessibility_settings_set_reduce_motion (settings, FALSE);
	g_assert_false (lrg_motion_get_reduced ());

	/* Stop following: further changes are ignored */
	lrg_motion_follow_accessibility (NULL);
	lrg_accessibility_settings_set_reduce_motion (settings, TRUE);
	g_assert_false (lrg_motion_get_reduced ());

	/* Following a settings object that dies is safe */
	{
		LrgAccessibilitySettings *temp = lrg_accessibility_settings_new ();

		lrg_motion_follow_accessibility (temp);
		g_object_unref (temp);
		lrg_motion_follow_accessibility (NULL);
	}
	reset_motion ();
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/spring/init", test_spring_init);
	g_test_add_func ("/spring/presets", test_spring_presets);
	g_test_add_func ("/spring/critical-no-overshoot", test_spring_critical_no_overshoot);
	g_test_add_func ("/spring/underdamped-overshoots", test_spring_underdamped_overshoots);
	g_test_add_func ("/spring/overdamped-converges", test_spring_overdamped_converges);
	g_test_add_func ("/spring/frame-rate-independent", test_spring_frame_rate_independent);
	g_test_add_func ("/spring/retarget-keeps-velocity", test_spring_retarget_keeps_velocity);
	g_test_add_func ("/spring/presets-settle", test_spring_presets_settle_in_time);
	g_test_add_func ("/spring/none-and-reduced", test_spring_none_and_reduced_snap);
	g_test_add_func ("/spring/speed-scaling", test_spring_speed_scaling);
	g_test_add_func ("/spring/hitch-capped", test_spring_hitch_is_capped);
	g_test_add_func ("/spring/boxed", test_spring_boxed);
	g_test_add_func ("/spring/follow-accessibility", test_motion_follows_accessibility);

	return g_test_run ();
}
