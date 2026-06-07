/* test-keyframe-curve.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgKeyframeCurve.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* Epsilon for floating-point comparisons */
#define EPSILON 0.0001f

/* ==========================================================================
 * Lifecycle tests
 * ========================================================================== */

static void
test_keyframe_curve_new (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;

    curve = lrg_keyframe_curve_new ();

    g_assert_nonnull (curve);
    g_assert_true (LRG_IS_KEYFRAME_CURVE (curve));
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 0);
}

/* ==========================================================================
 * Key count tests
 * ========================================================================== */

static void
test_keyframe_curve_key_count (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;

    curve = lrg_keyframe_curve_new ();
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 0);

    lrg_keyframe_curve_add_key (curve, 0.0f, 10.0f, LRG_EASING_LINEAR);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 1);

    lrg_keyframe_curve_add_key (curve, 1.0f, 20.0f, LRG_EASING_LINEAR);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 2);

    lrg_keyframe_curve_add_key (curve, 0.5f, 15.0f, LRG_EASING_LINEAR);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 3);
}

/* ==========================================================================
 * Single-key: constant value for all t
 * ========================================================================== */

static void
test_keyframe_curve_single_key (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.5f, 42.0f, LRG_EASING_LINEAR);

    /* Regardless of t, the single value is returned */
    result = lrg_keyframe_curve_sample (curve, 0.0f);
    g_assert_cmpfloat_with_epsilon (result, 42.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 42.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 1.0f);
    g_assert_cmpfloat_with_epsilon (result, 42.0f, EPSILON);
}

/* ==========================================================================
 * Two-key linear interpolation
 * ========================================================================== */

static void
test_keyframe_curve_two_key_linear (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,   0.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 1.0f, 100.0f, LRG_EASING_LINEAR);

    /* Boundary values */
    result = lrg_keyframe_curve_sample (curve, 0.0f);
    g_assert_cmpfloat_with_epsilon (result, 0.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 1.0f);
    g_assert_cmpfloat_with_epsilon (result, 100.0f, EPSILON);

    /* Midpoint */
    result = lrg_keyframe_curve_sample (curve, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 50.0f, EPSILON);

    /* Quarter */
    result = lrg_keyframe_curve_sample (curve, 0.25f);
    g_assert_cmpfloat_with_epsilon (result, 25.0f, EPSILON);
}

/* ==========================================================================
 * Two-key eased interpolation
 * ========================================================================== */

static void
test_keyframe_curve_two_key_eased (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;
    gfloat expected;

    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,   0.0f, LRG_EASING_EASE_IN_OUT_CUBIC);
    lrg_keyframe_curve_add_key (curve, 1.0f, 100.0f, LRG_EASING_LINEAR);

    /* Exact endpoints must be returned unchanged */
    result = lrg_keyframe_curve_sample (curve, 0.0f);
    g_assert_cmpfloat_with_epsilon (result, 0.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 1.0f);
    g_assert_cmpfloat_with_epsilon (result, 100.0f, EPSILON);

    /* t=0.5: ease-in-out-cubic at 0.5 => 4*0.5^3 = 0.5 => value 50 */
    expected = lrg_easing_interpolate (LRG_EASING_EASE_IN_OUT_CUBIC, 0.0f, 100.0f, 0.5f);
    result = lrg_keyframe_curve_sample (curve, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, expected, EPSILON);
}

/* ==========================================================================
 * Three+ keys with different per-segment easing
 * ========================================================================== */

static void
test_keyframe_curve_multi_key (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;
    gfloat expected;

    /*
     * Three keys: 0.0 -> 0.5 -> 1.0
     * Segment 1 [0, 0.5]: ease-in-cubic, from 0 to 100
     * Segment 2 [0.5, 1]: ease-out-cubic, from 100 to 50
     */
    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,   0.0f, LRG_EASING_EASE_IN_CUBIC);
    lrg_keyframe_curve_add_key (curve, 0.5f, 100.0f, LRG_EASING_EASE_OUT_CUBIC);
    lrg_keyframe_curve_add_key (curve, 1.0f,  50.0f, LRG_EASING_LINEAR);

    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 3);

    /* Exact segment boundary */
    result = lrg_keyframe_curve_sample (curve, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 100.0f, EPSILON);

    /* Midpoint of segment 1: local t'=0.5, ease-in-cubic(0.5)=0.125 */
    expected = lrg_easing_interpolate (LRG_EASING_EASE_IN_CUBIC, 0.0f, 100.0f, 0.5f);
    result = lrg_keyframe_curve_sample (curve, 0.25f);
    g_assert_cmpfloat_with_epsilon (result, expected, EPSILON);

    /* Midpoint of segment 2: local t'=0.5, ease-out-cubic */
    expected = lrg_easing_interpolate (LRG_EASING_EASE_OUT_CUBIC, 100.0f, 50.0f, 0.5f);
    result = lrg_keyframe_curve_sample (curve, 0.75f);
    g_assert_cmpfloat_with_epsilon (result, expected, EPSILON);

    /* Endpoints */
    result = lrg_keyframe_curve_sample (curve, 0.0f);
    g_assert_cmpfloat_with_epsilon (result, 0.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 1.0f);
    g_assert_cmpfloat_with_epsilon (result, 50.0f, EPSILON);
}

/* ==========================================================================
 * Four keys — verify more complex segment routing
 * ========================================================================== */

static void
test_keyframe_curve_four_keys (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    /*
     * Keys at t=0, 0.25, 0.75, 1.0 with linear easing so we can compute
     * expected values analytically.
     */
    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.00f,   0.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 0.25f,  40.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 0.75f,  80.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 1.00f, 100.0f, LRG_EASING_LINEAR);

    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 4);

    /* Within segment [0, 0.25] at t=0.125 => local t'=0.5 => value=20 */
    result = lrg_keyframe_curve_sample (curve, 0.125f);
    g_assert_cmpfloat_with_epsilon (result, 20.0f, EPSILON);

    /* Within segment [0.25, 0.75] at t=0.5 => local t'=0.5 => value=60 */
    result = lrg_keyframe_curve_sample (curve, 0.50f);
    g_assert_cmpfloat_with_epsilon (result, 60.0f, EPSILON);

    /* Within segment [0.75, 1.0] at t=0.875 => local t'=0.5 => value=90 */
    result = lrg_keyframe_curve_sample (curve, 0.875f);
    g_assert_cmpfloat_with_epsilon (result, 90.0f, EPSILON);
}

/* ==========================================================================
 * Clamp before first key
 * ========================================================================== */

static void
test_keyframe_curve_clamp_before_first (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    /* Keys starting at t=0.2 */
    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.2f, 33.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 1.0f, 99.0f, LRG_EASING_LINEAR);

    /* Any t before 0.2 should return first key's value */
    result = lrg_keyframe_curve_sample (curve, 0.0f);
    g_assert_cmpfloat_with_epsilon (result, 33.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 0.1f);
    g_assert_cmpfloat_with_epsilon (result, 33.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 0.19f);
    g_assert_cmpfloat_with_epsilon (result, 33.0f, EPSILON);
}

/* ==========================================================================
 * Clamp after last key
 * ========================================================================== */

static void
test_keyframe_curve_clamp_after_last (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    /* Keys ending at t=0.8 */
    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,  10.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 0.8f, 200.0f, LRG_EASING_LINEAR);

    /* Any t after 0.8 should return last key's value */
    result = lrg_keyframe_curve_sample (curve, 0.9f);
    g_assert_cmpfloat_with_epsilon (result, 200.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 1.0f);
    g_assert_cmpfloat_with_epsilon (result, 200.0f, EPSILON);
}

/* ==========================================================================
 * t < 0 and t > 1 clamp to end keys
 * ========================================================================== */

static void
test_keyframe_curve_clamp_out_of_range (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,  5.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 1.0f, 95.0f, LRG_EASING_LINEAR);

    /* Negative t clamps to first key */
    result = lrg_keyframe_curve_sample (curve, -0.5f);
    g_assert_cmpfloat_with_epsilon (result, 5.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, -100.0f);
    g_assert_cmpfloat_with_epsilon (result, 5.0f, EPSILON);

    /* t > 1 clamps to last key */
    result = lrg_keyframe_curve_sample (curve, 1.5f);
    g_assert_cmpfloat_with_epsilon (result, 95.0f, EPSILON);

    result = lrg_keyframe_curve_sample (curve, 100.0f);
    g_assert_cmpfloat_with_epsilon (result, 95.0f, EPSILON);
}

/* ==========================================================================
 * Duplicate-t key overwrites (last-write-wins)
 * ========================================================================== */

static void
test_keyframe_curve_duplicate_t (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gfloat result;

    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f, 10.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 0.5f, 50.0f, LRG_EASING_LINEAR);
    /* Overwrite the 0.5 key */
    lrg_keyframe_curve_add_key (curve, 0.5f, 77.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, 1.0f, 90.0f, LRG_EASING_LINEAR);

    /* Count must remain 3, not 4 */
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 3);

    /* The overwritten value must be used */
    result = lrg_keyframe_curve_sample (curve, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 77.0f, EPSILON);
}

/* ==========================================================================
 * Headless rendering loop pattern
 * ========================================================================== */

static void
test_keyframe_curve_headless_loop (void)
{
    g_autoptr (LrgKeyframeCurve) curve = NULL;
    gint n_frames;
    gint frame;
    gfloat prev_value;

    /*
     * Simulate a 30-frame bake.  With ease-in-out the values should be
     * monotonically increasing for a 0->100 curve.  This also confirms the
     * pattern compiles cleanly and produces reasonable output.
     */
    curve = lrg_keyframe_curve_new ();
    lrg_keyframe_curve_add_key (curve, 0.0f,   0.0f, LRG_EASING_EASE_IN_OUT_CUBIC);
    lrg_keyframe_curve_add_key (curve, 1.0f, 100.0f, LRG_EASING_LINEAR);

    n_frames = 30;
    prev_value = -1.0f;

    for (frame = 0; frame < n_frames; frame++)
    {
        gfloat t;
        gfloat x;

        t = (gfloat) frame / (gfloat)(n_frames - 1);
        x = lrg_keyframe_curve_sample (curve, t);

        /* Values must be monotonically non-decreasing for ease-in-out */
        g_assert_cmpfloat (x, >=, prev_value - EPSILON);
        prev_value = x;
    }

    /* First frame must be 0, last frame must be 100 */
    g_assert_cmpfloat_with_epsilon (
        lrg_keyframe_curve_sample (curve, 0.0f), 0.0f, EPSILON);
    g_assert_cmpfloat_with_epsilon (
        lrg_keyframe_curve_sample (curve, 1.0f), 100.0f, EPSILON);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/keyframe-curve/new",             test_keyframe_curve_new);
    g_test_add_func ("/keyframe-curve/key-count",       test_keyframe_curve_key_count);
    g_test_add_func ("/keyframe-curve/single-key",      test_keyframe_curve_single_key);
    g_test_add_func ("/keyframe-curve/two-key-linear",  test_keyframe_curve_two_key_linear);
    g_test_add_func ("/keyframe-curve/two-key-eased",   test_keyframe_curve_two_key_eased);
    g_test_add_func ("/keyframe-curve/multi-key",       test_keyframe_curve_multi_key);
    g_test_add_func ("/keyframe-curve/four-keys",       test_keyframe_curve_four_keys);
    g_test_add_func ("/keyframe-curve/clamp-before",    test_keyframe_curve_clamp_before_first);
    g_test_add_func ("/keyframe-curve/clamp-after",     test_keyframe_curve_clamp_after_last);
    g_test_add_func ("/keyframe-curve/clamp-out-range", test_keyframe_curve_clamp_out_of_range);
    g_test_add_func ("/keyframe-curve/duplicate-t",     test_keyframe_curve_duplicate_t);
    g_test_add_func ("/keyframe-curve/headless-loop",   test_keyframe_curve_headless_loop);

    return g_test_run ();
}
