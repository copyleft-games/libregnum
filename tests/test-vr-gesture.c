/* test-vr-gesture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gestures drawn in the air: every built-in shape is recognised from a
 * hand-drawn (noisy, offset, scaled) copy of itself; direction and
 * orientation count; closed shapes match from any start; pushes and
 * pulls are told by depth in the head's view whichever way it faces;
 * small strokes are nothing.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* a small deterministic wobble, -1 .. 1 */
static gfloat
wobble (guint i)
{
    return sinf ((gfloat)i * 2.3f) * 0.6f + cosf ((gfloat)i * 5.1f) * 0.4f;
}

/* draws @n x, y pairs as a hand would, @size metres across, the plane
   facing -Z, with @noise metres of wobble, and names it */
static LrgVRGesture *
draw (LrgVRGestureRecognizer *gestures,
      const gfloat           *flat,
      guint                   n,
      gfloat                  size,
      gfloat                  noise)
{
    guint i;

    lrg_vr_gesture_recognizer_begin (gestures, NULL);
    for (i = 0; i < n; i++)
    {
        gfloat p[3];

        p[0] = 0.2f + flat[i * 2] * size + wobble (i) * noise;
        p[1] = 1.3f + flat[i * 2 + 1] * size + wobble (i + 17) * noise;
        p[2] = -0.4f + wobble (i + 31) * noise;
        lrg_vr_gesture_recognizer_add_point (gestures, p, (gdouble)i / 90.0);
    }
    return lrg_vr_gesture_recognizer_end (gestures);
}

static LrgVRGestureRecognizer *
builtin (void)
{
    LrgVRGestureRecognizer *gestures = lrg_vr_gesture_recognizer_new ();

    lrg_vr_gesture_recognizer_add_builtin_templates (gestures);
    return gestures;
}

/* --------------------------------------------------------- the shapes - */

static void
test_builtin_names (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    g_auto(GStrv) names = lrg_vr_gesture_recognizer_dup_names (gestures);

    /* each name once, in order */
    g_assert_cmpuint (g_strv_length (names), ==, 10);
    g_assert_cmpstr (names[0], ==, "line-up");
    g_assert_true (g_strv_contains ((const gchar * const *)names, "circle"));
    g_assert_true (g_strv_contains ((const gchar * const *)names, "zigzag"));
    g_assert_true (g_strv_contains ((const gchar * const *)names, "square"));
}

static void
test_builtin_recognised (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    g_auto(GStrv) names = lrg_vr_gesture_recognizer_dup_names (gestures);
    guint i;

    /* every shape, drawn 30 cm across with a centimetre of wobble, is
       itself and nothing else */
    for (i = 0; names[i] != NULL; i++)
    {
        g_autoptr(LrgVRGesture) drawn = NULL;
        const gfloat *shape;
        guint n;

        shape = lrg_vr_gesture_recognizer_get_template (gestures, names[i], &n);
        g_assert_nonnull (shape);
        g_assert_cmpuint (n, >, 2);
        drawn = draw (gestures, shape, n, 0.3f, 0.01f);
        g_test_message ("%s: %s %.3f", names[i], drawn->name, (gdouble)drawn->score);
        g_assert_cmpstr (drawn->name, ==, names[i]);
        g_assert_cmpfloat (drawn->score, >, 0.8f);
        g_assert_cmpfloat (drawn->length, >, 0.25f);
        g_assert_cmpfloat (drawn->duration, >, 0.0f);
    }
}

static void
test_direction_counts (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    static const gfloat up[] = { 0.0f, 0.0f, 0.02f, 0.5f, -0.01f, 1.0f };
    static const gfloat down[] = { 0.0f, 1.0f, 0.02f, 0.5f, -0.01f, 0.0f };
    static const gfloat diagonal[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    g_autoptr(LrgVRGesture) a = NULL;
    g_autoptr(LrgVRGesture) b = NULL;
    g_autoptr(LrgVRGesture) c = NULL;

    a = draw (gestures, up, 3, 0.4f, 0.0f);
    b = draw (gestures, down, 3, 0.4f, 0.0f);
    c = draw (gestures, diagonal, 2, 0.4f, 0.0f);
    g_assert_cmpstr (a->name, ==, "line-up");
    g_assert_cmpstr (b->name, ==, "line-down");
    /* a diagonal is no line of the four */
    g_assert_null (c->name);
}

static void
test_circle_any_start (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    gfloat ring[41 * 2];
    guint start;

    /* an ellipse a little wider than tall, begun at the bottom, the
       left and the right, both ways round */
    for (start = 0; start < 4; start++)
    {
        guint way;

        for (way = 0; way < 2; way++)
        {
            g_autoptr(LrgVRGesture) drawn = NULL;
            guint i;

            for (i = 0; i <= 40; i++)
            {
                gfloat a = (gfloat)start * (gfloat)(G_PI / 2.0) +
                           (way == 0 ? 1.0f : -1.0f) * (gfloat)i * (gfloat)(2.0 * G_PI / 40.0);

                ring[i * 2] = 1.2f * sinf (a);
                ring[i * 2 + 1] = cosf (a);
            }
            drawn = draw (gestures, ring, 41, 0.15f, 0.008f);
            g_assert_cmpstr (drawn->name, ==, "circle");
        }
    }
}

static void
test_square_not_circle (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    /* a square begun at its bottom-right corner, anticlockwise */
    static const gfloat square[] = { 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    static const gfloat triangle[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.9f, 0.0f, 0.0f };
    g_autoptr(LrgVRGesture) a = NULL;
    g_autoptr(LrgVRGesture) b = NULL;

    a = draw (gestures, square, 5, 0.3f, 0.005f);
    b = draw (gestures, triangle, 4, 0.3f, 0.005f);
    g_assert_cmpstr (a->name, ==, "square");
    g_assert_cmpstr (b->name, ==, "triangle");
}

static void
test_scale_and_place (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    static const gfloat z[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    g_autoptr(LrgVRGesture) small = NULL;
    g_autoptr(LrgVRGesture) big = NULL;

    small = draw (gestures, z, 4, 0.15f, 0.0f);
    big = draw (gestures, z, 4, 0.9f, 0.0f);
    g_assert_cmpstr (small->name, ==, "zigzag");
    g_assert_cmpstr (big->name, ==, "zigzag");
    g_assert_cmpfloat_with_epsilon (small->width, 0.15f, 1e-4f);
    g_assert_cmpfloat_with_epsilon (big->height, 0.9f, 1e-4f);
}

static void
test_too_small (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    static const gfloat z[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    g_autoptr(LrgVRGesture) tap = NULL;
    g_autoptr(LrgVRGesture) nothing = NULL;

    /* a 4 cm Z is a hand that wobbled, not a gesture */
    tap = draw (gestures, z, 4, 0.04f, 0.0f);
    g_assert_null (tap->name);
    /* an end without a begin is nothing either */
    nothing = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_null (nothing->name);
    g_assert_false (lrg_vr_gesture_recognizer_is_drawing (gestures));
}

/* ---------------------------------------------------- push and pull - */

static void
thrust (LrgVRGestureRecognizer *gestures,
        const gfloat           *head,
        const gfloat           *along,
        gfloat                  metres)
{
    guint i;

    lrg_vr_gesture_recognizer_begin (gestures, head);
    for (i = 0; i <= 10; i++)
    {
        gfloat f = (gfloat)i / 10.0f * metres;
        gfloat p[3];

        /* a little sideways arc, as an arm makes */
        p[0] = along[0] * f + 0.02f * sinf ((gfloat)i * 0.3f);
        p[1] = 1.2f + along[1] * f;
        p[2] = along[2] * f;
        lrg_vr_gesture_recognizer_add_point (gestures, p, (gdouble)i / 90.0);
    }
}

static void
test_push_pull (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = lrg_vr_gesture_recognizer_new ();
    /* a head turned a quarter left (looking along -X): its right is -Z */
    static const gfloat turned[16] = {
        0.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.6f, 0.0f, 1.0f
    };
    static const gfloat minus_z[3] = { 0.0f, 0.0f, -1.0f };
    static const gfloat minus_x[3] = { -1.0f, 0.0f, 0.0f };
    static const gfloat plus_x[3] = { 1.0f, 0.0f, 0.0f };
    g_autoptr(LrgVRGesture) a = NULL;
    g_autoptr(LrgVRGesture) b = NULL;
    g_autoptr(LrgVRGesture) c = NULL;
    g_autoptr(LrgVRGesture) d = NULL;

    /* no templates: pushes and pulls still are */
    thrust (gestures, NULL, minus_z, 0.35f);
    a = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpstr (a->name, ==, LRG_VR_GESTURE_PUSH);
    g_assert_cmpfloat (a->depth, >, 0.3f);

    thrust (gestures, turned, minus_x, 0.35f);
    b = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpstr (b->name, ==, LRG_VR_GESTURE_PUSH);

    thrust (gestures, turned, plus_x, 0.35f);
    c = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpstr (c->name, ==, LRG_VR_GESTURE_PULL);
    g_assert_cmpfloat (c->depth, <, -0.3f);

    /* the head facing -X, a move along -Z is sideways: a line, not a push */
    lrg_vr_gesture_recognizer_add_builtin_templates (gestures);
    thrust (gestures, turned, minus_z, 0.35f);
    d = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpstr (d->name, ==, "line-right");
}

/* --------------------------------------------------------- templates - */

static void
test_templates (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = lrg_vr_gesture_recognizer_new ();
    static const gfloat point[] = { 0.5f, 0.5f, 0.5f, 0.5f };
    static const gfloat hook[] = { 0.0f, 1.0f, 0.0f, 0.0f, 0.6f, 0.0f };
    g_autoptr(LrgVRGesture) drawn = NULL;
    g_autoptr(LrgVRGesture) copy = NULL;
    g_auto(GStrv) names = NULL;
    guint n;

    g_test_expect_message ("Libregnum-VR", G_LOG_LEVEL_WARNING, "*\"dot\" spans no distance*");
    g_assert_false (lrg_vr_gesture_recognizer_add_template (gestures, "dot", point, 2, FALSE));
    g_test_assert_expected_messages ();
    g_assert_true (lrg_vr_gesture_recognizer_add_template (gestures, "hook", hook, 3, FALSE));
    g_assert_null (lrg_vr_gesture_recognizer_get_template (gestures, "dot", &n));
    g_assert_cmpuint (n, ==, 0);

    drawn = lrg_vr_gesture_recognizer_recognize (gestures, hook, 3);
    g_assert_cmpstr (drawn->name, ==, "hook");
    g_assert_cmpfloat_with_epsilon (drawn->score, 1.0f, 1e-3f);
    copy = lrg_vr_gesture_copy (drawn);
    g_assert_cmpstr (copy->name, ==, "hook");
    g_assert_true (copy->name != drawn->name);

    /* a stricter threshold than any drawing can meet */
    lrg_vr_gesture_recognizer_set_threshold (gestures, 2.0f);
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_get_threshold (gestures), ==, 1.0f);
    lrg_vr_gesture_recognizer_set_min_size (gestures, -1.0f);
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_get_min_size (gestures), ==, 0.0f);

    lrg_vr_gesture_recognizer_add_builtin_templates (gestures);
    g_assert_cmpuint (lrg_vr_gesture_recognizer_remove_template (gestures, "circle"), ==, 2);
    lrg_vr_gesture_recognizer_clear_templates (gestures);
    names = lrg_vr_gesture_recognizer_dup_names (gestures);
    g_assert_cmpuint (g_strv_length (names), ==, 0);
}

static void
test_ambiguous (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = lrg_vr_gesture_recognizer_new ();
    static const gfloat hook[] = { 0.0f, 1.0f, 0.0f, 0.0f, 0.6f, 0.0f };
    static const gfloat near[] = { 0.0f, 1.0f, 0.02f, 0.0f, 0.6f, 0.02f };
    g_autoptr(LrgVRGesture) one = NULL;
    g_autoptr(LrgVRGesture) two = NULL;

    /* alone, a hook is a hook; beside a shape all but the same, it is
       neither: a wrong spell is worse than none */
    g_assert_true (lrg_vr_gesture_recognizer_add_template (gestures, "hook", hook, 3, FALSE));
    one = lrg_vr_gesture_recognizer_recognize (gestures, hook, 3);
    g_assert_cmpstr (one->name, ==, "hook");
    g_assert_true (lrg_vr_gesture_recognizer_add_template (gestures, "almost", near, 3, FALSE));
    two = lrg_vr_gesture_recognizer_recognize (gestures, hook, 3);
    g_assert_null (two->name);
    g_assert_cmpfloat (two->score, >, 0.9f);
}

static void
test_peek_and_fit (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    g_autoptr(LrgVRGesture) half = NULL;
    g_autoptr(LrgVRGesture) whole = NULL;
    g_autoptr(LrgVRGesture) ended = NULL;
    gfloat fitted[LRG_VR_GESTURE_SAMPLES * 3];
    gfloat centre[3];
    gfloat right[3];
    gfloat up[3];
    gfloat size;
    gfloat worst;
    guint i;

    /* nothing drawn: nothing to peek at, frame or fit */
    g_assert_false (lrg_vr_gesture_recognizer_get_frame (gestures, centre, right, up, &size));
    g_assert_false (lrg_vr_gesture_recognizer_fit_template (gestures, "circle", fitted));

    /* a circle 30 cm across round (0.2, 1.3, -0.4), begun at the top */
    lrg_vr_gesture_recognizer_begin (gestures, NULL);
    for (i = 0; i <= 40; i++)
    {
        gfloat a = (gfloat)i * (gfloat)(2.0 * G_PI / 40.0);
        gfloat p[3];

        p[0] = 0.2f + 0.15f * sinf (a);
        p[1] = 1.3f + 0.15f * cosf (a);
        p[2] = -0.4f;
        lrg_vr_gesture_recognizer_add_point (gestures, p, (gdouble)i / 90.0);
        if (i == 10)
        {
            /* a quarter in: an arc, most like something, not yet a circle */
            half = lrg_vr_gesture_recognizer_peek (gestures);
            g_assert_nonnull (half->nearest);
            g_assert_cmpstr (half->name, !=, "circle");
        }
    }
    whole = lrg_vr_gesture_recognizer_peek (gestures);
    g_assert_cmpstr (whole->name, ==, "circle");
    g_assert_cmpstr (whole->nearest, ==, "circle");
    g_assert_true (lrg_vr_gesture_recognizer_is_drawing (gestures));

    /* its box: centred on the circle, 30 cm, in the -Z facing plane */
    g_assert_true (lrg_vr_gesture_recognizer_get_frame (gestures, centre, right, up, &size));
    g_assert_cmpfloat_with_epsilon (centre[0], 0.2f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (centre[1], 1.3f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (centre[2], -0.4f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (size, 0.3f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (right[0], 1.0f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (up[1], 1.0f, 1e-5f);

    /* the circle template laid over it: every point on the drawn ring */
    g_assert_true (lrg_vr_gesture_recognizer_fit_template (gestures, "circle", fitted));
    g_assert_false (lrg_vr_gesture_recognizer_fit_template (gestures, "hook", fitted));
    worst = 0.0f;
    for (i = 0; i < LRG_VR_GESTURE_SAMPLES; i++)
    {
        gfloat r = hypotf (fitted[i * 3] - 0.2f, fitted[i * 3 + 1] - 1.3f);

        worst = MAX (worst, fabsf (r - 0.15f));
        g_assert_cmpfloat_with_epsilon (fitted[i * 3 + 2], -0.4f, 1e-4f);
    }
    g_assert_cmpfloat (worst, <, 0.02f);

    /* ending names it the same */
    ended = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpstr (ended->name, ==, "circle");
}

static void
test_nearest_below_threshold (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    static const gfloat diagonal[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    g_autoptr(LrgVRGesture) drawn = NULL;
    g_autoptr(LrgVRGesture) copy = NULL;

    drawn = draw (gestures, diagonal, 2, 0.4f, 0.0f);
    g_assert_null (drawn->name);
    g_assert_nonnull (drawn->nearest);
    copy = lrg_vr_gesture_copy (drawn);
    g_assert_cmpstr (copy->nearest, ==, drawn->nearest);
}

static void
test_points_kept (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    static const gfloat a[3] = { 0.0f, 1.0f, 0.0f };
    static const gfloat b[3] = { 0.1f, 1.0f, 0.0f };
    const gfloat *points;
    guint n;

    /* samples before a begin are dropped */
    lrg_vr_gesture_recognizer_add_point (gestures, a, 0.0);
    g_assert_null (lrg_vr_gesture_recognizer_get_points (gestures, &n));
    lrg_vr_gesture_recognizer_begin (gestures, NULL);
    lrg_vr_gesture_recognizer_add_point (gestures, a, 0.0);
    lrg_vr_gesture_recognizer_add_point (gestures, b, 0.1);
    points = lrg_vr_gesture_recognizer_get_points (gestures, &n);
    g_assert_cmpuint (n, ==, 2);
    g_assert_cmpfloat (points[3], ==, 0.1f);
    lrg_vr_gesture_recognizer_cancel (gestures);
    g_assert_null (lrg_vr_gesture_recognizer_get_points (gestures, &n));
}

static void
test_score_template (void)
{
    g_autoptr(LrgVRGestureRecognizer) gestures = builtin ();
    g_autoptr(LrgVRGesture) ended = NULL;
    gfloat circle;
    gfloat square;
    guint i;

    /* nothing drawn: no score */
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, "circle"), ==, -1.0f);

    /* a circle 30 cm across: its own score is end()'s, the others lower */
    lrg_vr_gesture_recognizer_begin (gestures, NULL);
    for (i = 0; i <= 40; i++)
    {
        gfloat a = (gfloat)i * (gfloat)(2.0 * G_PI / 40.0);
        gfloat p[3];

        p[0] = 0.15f * sinf (a);
        p[1] = 1.3f + 0.15f * cosf (a);
        p[2] = -0.4f;
        lrg_vr_gesture_recognizer_add_point (gestures, p, (gdouble)i / 90.0);
    }
    circle = lrg_vr_gesture_recognizer_score_template (gestures, "circle");
    square = lrg_vr_gesture_recognizer_score_template (gestures, "square");
    g_assert_cmpfloat (circle, >, 0.9f);
    g_assert_cmpfloat (square, >=, 0.0f);
    g_assert_cmpfloat (square, <, circle);
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, "zigzag"), <, circle);
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, "hook"), ==, -1.0f);
    /* flat in the plane: not a thrust either way */
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, LRG_VR_GESTURE_PUSH), ==, 0.0f);
    ended = lrg_vr_gesture_recognizer_end (gestures);
    g_assert_cmpfloat_with_epsilon (ended->score, circle, 1e-5f);

    /* a straight push away from the head: a push, never a pull */
    lrg_vr_gesture_recognizer_begin (gestures, NULL);
    for (i = 0; i <= 20; i++)
    {
        gfloat p[3] = { 0.0f, 1.3f, -0.3f - 0.02f * (gfloat)i };

        lrg_vr_gesture_recognizer_add_point (gestures, p, (gdouble)i / 90.0);
    }
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, LRG_VR_GESTURE_PUSH), >, 0.9f);
    g_assert_cmpfloat (lrg_vr_gesture_recognizer_score_template (gestures, LRG_VR_GESTURE_PULL), ==, 0.0f);
    lrg_vr_gesture_recognizer_cancel (gestures);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/vr/gesture/builtin-names", test_builtin_names);
    g_test_add_func ("/vr/gesture/builtin-recognised", test_builtin_recognised);
    g_test_add_func ("/vr/gesture/direction-counts", test_direction_counts);
    g_test_add_func ("/vr/gesture/circle-any-start", test_circle_any_start);
    g_test_add_func ("/vr/gesture/square-not-circle", test_square_not_circle);
    g_test_add_func ("/vr/gesture/scale-and-place", test_scale_and_place);
    g_test_add_func ("/vr/gesture/too-small", test_too_small);
    g_test_add_func ("/vr/gesture/push-pull", test_push_pull);
    g_test_add_func ("/vr/gesture/templates", test_templates);
    g_test_add_func ("/vr/gesture/ambiguous", test_ambiguous);
    g_test_add_func ("/vr/gesture/peek-and-fit", test_peek_and_fit);
    g_test_add_func ("/vr/gesture/nearest-below-threshold", test_nearest_below_threshold);
    g_test_add_func ("/vr/gesture/points-kept", test_points_kept);
    g_test_add_func ("/vr/gesture/score-template", test_score_template);

    return g_test_run ();
}
