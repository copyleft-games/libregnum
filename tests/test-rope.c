/* test-rope.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgRope: the ends stay pinned, a slack rope sags below
 * the line between its anchors and settles, a rope whose anchors are a rope's
 * length apart is straight, segment lengths stay near the rest length, a
 * floor holds slack up, moving an anchor pulls the rope along, tension and
 * the argument limits.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

static gfloat
dist (const gfloat *a,
      const gfloat *b)
{
    return sqrtf ((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                  (a[2] - b[2]) * (a[2] - b[2]));
}

static void
settle (LrgRope *rope,
        guint    steps)
{
    guint i;

    for (i = 0; i < steps; i++)
        lrg_rope_step (rope, 1.0f / 60.0f);
}

static void
test_pinned_ends_and_sag (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (16, 6.0f);
    const gfloat a[3] = { 0.0f, 2.0f, 0.0f };
    const gfloat b[3] = { 4.0f, 2.0f, 0.0f };
    const gfloat *p;
    guint n = 0;
    gfloat lowest = 100.0f;
    guint i;

    g_assert_cmpuint (lrg_rope_get_point_count (rope), ==, 17);
    g_assert_cmpfloat_with_epsilon (lrg_rope_get_length (rope), 6.0f, 1e-6f);
    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 240);
    p = lrg_rope_get_points (rope, &n);
    g_assert_cmpuint (n, ==, 17);

    /* the ends are exactly the anchors */
    g_assert_cmpfloat_with_epsilon (dist (p, a), 0.0f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (dist (p + 16 * 3, b), 0.0f, 1e-5f);

    /* the middle hangs below the anchors' line: a 6 m rope over 4 m sags about 1.4 m */
    for (i = 0; i < n; i++)
        lowest = MIN (lowest, p[i * 3 + 1]);
    g_assert_cmpfloat (lowest, <, 2.0f - 0.9f);
    g_assert_cmpfloat (lowest, >, 2.0f - 2.2f);

    /* it stays in the plane of the anchors and between them in X */
    for (i = 0; i < n; i++)
    {
        g_assert_cmpfloat (fabsf (p[i * 3 + 2]), <, 0.05f);
        g_assert_cmpfloat (p[i * 3 + 0], >, -0.3f);
        g_assert_cmpfloat (p[i * 3 + 0], <, 4.3f);
    }
}

static void
test_taut_is_straight (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (6, 5.0f);
    const gfloat a[3] = { 0.0f, 1.0f, 0.0f };
    const gfloat b[3] = { 5.0f, 1.0f, 0.0f };
    const gfloat *p;
    guint n = 0;
    guint i;

    lrg_rope_set_iterations (rope, 64);
    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 120);
    p = lrg_rope_get_points (rope, &n);
    for (i = 0; i < n; i++)
        g_assert_cmpfloat (fabsf (p[i * 3 + 1] - 1.0f), <, 0.15f);   /* 0.3 percent stretch is a 0.1 m sag over 5 m */
    g_assert_cmpfloat_with_epsilon (lrg_rope_get_tension (rope), 1.0f, 1e-5f);

    /* anchors further apart than the rope: stretched straight, ends held */
    {
        const gfloat far[3] = { 8.0f, 1.0f, 0.0f };

        lrg_rope_set_anchors (rope, a, far);
        settle (rope, 60);
        p = lrg_rope_get_points (rope, &n);
        g_assert_cmpfloat_with_epsilon (dist (p + 6 * 3, far), 0.0f, 1e-4f);
        g_assert_cmpfloat_with_epsilon (lrg_rope_get_tension (rope), 1.0f, 1e-5f);
    }
}

static void
test_segment_lengths (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (20, 8.0f);
    const gfloat a[3] = { 0.0f, 3.0f, 0.0f };
    const gfloat b[3] = { 5.0f, 3.0f, 2.0f };
    const gfloat *p;
    guint n = 0;
    guint i;

    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 240);
    p = lrg_rope_get_points (rope, &n);
    for (i = 0; i + 1 < n; i++)
    {
        gfloat d = dist (p + i * 3, p + (i + 1) * 3);

        g_assert_cmpfloat (d, >, 0.4f * 0.9f);
        g_assert_cmpfloat (d, <, 0.4f * 1.1f);
    }
}

static void
test_floor (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (16, 8.0f);
    const gfloat a[3] = { 0.0f, 0.5f, 0.0f };
    const gfloat b[3] = { 3.0f, 0.5f, 0.0f };
    const gfloat *p;
    guint n = 0;
    guint i;

    lrg_rope_set_floor (rope, TRUE, 0.0f);
    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 240);
    p = lrg_rope_get_points (rope, &n);
    for (i = 0; i < n; i++)
        g_assert_cmpfloat (p[i * 3 + 1], >=, -1e-4f);
    /* the slack lies on the floor somewhere between the ends */
    {
        gfloat lowest = 100.0f;

        for (i = 1; i + 1 < n; i++)
            lowest = MIN (lowest, p[i * 3 + 1]);
        g_assert_cmpfloat (lowest, <, 0.05f);
    }
}

static void
test_follows_the_anchor (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (12, 6.0f);
    gfloat a[3] = { 0.0f, 1.5f, 0.0f };
    gfloat b[3] = { 3.0f, 1.5f, 0.0f };
    const gfloat *p;
    guint n = 0;
    guint i;

    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 120);
    /* the far end moves 10 m along X over two seconds: the rope is dragged and ends at it */
    for (i = 0; i < 120; i++)
    {
        b[0] = 3.0f + 10.0f * (gfloat)(i + 1) / 120.0f;
        lrg_rope_set_anchors (rope, a, b);
        lrg_rope_step (rope, 1.0f / 60.0f);
    }
    p = lrg_rope_get_points (rope, &n);
    g_assert_cmpfloat_with_epsilon (p[(n - 1) * 3], 13.0f, 1e-4f);
    /* the first anchor held it: the rope is stretched toward its length */
    g_assert_cmpfloat_with_epsilon (lrg_rope_get_tension (rope), 1.0f, 1e-5f);
}

static void
test_reset_and_limits (void)
{
    g_autoptr(LrgRope) rope = lrg_rope_new (8, 4.0f);
    const gfloat a[3] = { 0.0f, 3.0f, 0.0f };
    const gfloat b[3] = { 2.0f, 3.0f, 0.0f };
    const gfloat *p;
    guint n = 0;

    /* a step before anchors does nothing; reset hangs the rope again */
    lrg_rope_step (rope, 0.1f);
    lrg_rope_set_anchors (rope, a, b);
    settle (rope, 10);
    lrg_rope_reset (rope);
    p = lrg_rope_get_points (rope, &n);
    g_assert_cmpfloat_with_epsilon (p[0], 0.0f, 1e-6f);
    g_assert_cmpfloat_with_epsilon (p[(n - 1) * 3 + 1], -4.0f, 1e-5f);

    /* non-finite and negative steps are ignored; absurd steps are taken in pieces */
    lrg_rope_set_anchors (rope, a, b);
    lrg_rope_step (rope, NAN);
    lrg_rope_step (rope, -1.0f);
    lrg_rope_step (rope, 5.0f);
    p = lrg_rope_get_points (rope, &n);
    g_assert_true (isfinite (p[3 * 4 + 1]));

    lrg_rope_set_iterations (rope, 0);
    lrg_rope_set_iterations (rope, 1000);
    lrg_rope_set_damping (rope, 7.0f);
    lrg_rope_set_gravity (rope, NAN);
    lrg_rope_step (rope, 0.1f);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/rope/pinned-and-sag", test_pinned_ends_and_sag);
    g_test_add_func ("/rope/taut", test_taut_is_straight);
    g_test_add_func ("/rope/segment-lengths", test_segment_lengths);
    g_test_add_func ("/rope/floor", test_floor);
    g_test_add_func ("/rope/follows-the-anchor", test_follows_the_anchor);
    g_test_add_func ("/rope/reset-and-limits", test_reset_and_limits);
    return g_test_run ();
}
