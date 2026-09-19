/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <math.h>

static LrgKeyframeCurve *
make_curve (void)
{
    LrgKeyframeCurve *curve = lrg_keyframe_curve_new ();

    lrg_keyframe_curve_add_key (curve, 2.0f, 40.0f, LRG_EASING_LINEAR);
    lrg_keyframe_curve_add_key (curve, -2.0f, 0.0f, LRG_EASING_EASE_IN_QUAD);
    lrg_keyframe_curve_add_key (curve, 0.0f, 20.0f, LRG_EASING_LINEAR);
    return curve;
}

static LrgPath *
make_path (void)
{
    LrgPath *path = lrg_path_new ();

    lrg_path_append (path, 0, 0);
    lrg_path_append (path, 3, 4);
    lrg_path_append (path, 6, 8);
    lrg_path_set_total_cost (path, 42.0f);
    return path;
}

static void
assert_point (LrgPath *path, guint index, gint x, gint y)
{
    gint actual_x;
    gint actual_y;

    g_assert_true (lrg_path_get_point (path, index, &actual_x, &actual_y));
    g_assert_cmpint (actual_x, ==, x);
    g_assert_cmpint (actual_y, ==, y);
}

static void
test_curve_get (void)
{
    g_autoptr(LrgKeyframeCurve) curve = make_curve ();
    gfloat t;
    gfloat value;
    LrgEasingType easing;

    g_assert_true (lrg_keyframe_curve_get_key (curve, 0, &t, &value, &easing));
    g_assert_cmpfloat (t, ==, -2.0f);
    g_assert_cmpfloat (value, ==, 0.0f);
    g_assert_cmpint (easing, ==, LRG_EASING_EASE_IN_QUAD);
    g_assert_true (lrg_keyframe_curve_get_key (curve, 2, &t, &value, NULL));
    g_assert_cmpfloat (t, ==, 2.0f);
    g_assert_cmpfloat (value, ==, 40.0f);
    g_assert_true (lrg_keyframe_curve_get_key (curve, 1, NULL, NULL, NULL));
    g_assert_false (lrg_keyframe_curve_get_key (curve, G_MAXUINT, &t, &value, &easing));
    g_assert_cmpfloat (t, ==, 0.0f);
    g_assert_cmpfloat (value, ==, 0.0f);
    g_assert_cmpint (easing, ==, LRG_EASING_LINEAR);
}

static void
test_curve_remove (void)
{
    g_autoptr(LrgKeyframeCurve) curve = make_curve ();

    g_assert_false (lrg_keyframe_curve_remove_key (curve, 0.01f));
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 3);
    g_assert_true (lrg_keyframe_curve_remove_key (curve, 0.0f));
    g_assert_cmpfloat (lrg_keyframe_curve_sample (curve, 0.0f), ==, 10.0f);
    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*isfinite (t)*");
    g_assert_false (lrg_keyframe_curve_remove_key (curve, NAN));
    g_test_assert_expected_messages ();
    g_assert_true (lrg_keyframe_curve_remove_key (curve, -2.0f));
    g_assert_cmpfloat (lrg_keyframe_curve_sample (curve, -2.0f), ==, 40.0f);
    g_assert_true (lrg_keyframe_curve_remove_key (curve, 2.0f));
    g_assert_false (lrg_keyframe_curve_remove_key (curve, 2.0f));
}

static void
test_curve_clear (void)
{
    g_autoptr(LrgKeyframeCurve) curve = make_curve ();

    lrg_keyframe_curve_clear (curve);
    lrg_keyframe_curve_clear (curve);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (curve), ==, 0);
    lrg_keyframe_curve_add_key (curve, 1.0f, 7.0f, LRG_EASING_LINEAR);
    g_assert_cmpfloat (lrg_keyframe_curve_sample (curve, 0.0f), ==, 7.0f);
}

static void
test_curve_copy (void)
{
    g_autoptr(LrgKeyframeCurve) curve = make_curve ();
    g_autoptr(LrgKeyframeCurve) copy = lrg_keyframe_curve_copy (curve);
    g_autoptr(LrgKeyframeCurve) empty = NULL;
    guint i;

    for (i = 0; i <= 32; i++)
    {
        gfloat t = -2.0f + i / 8.0f;

        g_assert_cmpfloat (lrg_keyframe_curve_sample (copy, t), ==,
                           lrg_keyframe_curve_sample (curve, t));
    }
    lrg_keyframe_curve_clear (curve);
    empty = lrg_keyframe_curve_copy (curve);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (empty), ==, 0);
    g_clear_object (&curve);
    g_assert_cmpuint (lrg_keyframe_curve_get_key_count (copy), ==, 3);
    g_assert_cmpfloat (lrg_keyframe_curve_sample (copy, -1.0f), ==, 5.0f);
}

static void
test_curve_range (void)
{
    g_autoptr(LrgKeyframeCurve) curve = make_curve ();
    gfloat start;
    gfloat end;

    g_assert_true (lrg_keyframe_curve_get_time_range (curve, &start, &end));
    g_assert_cmpfloat (start, ==, -2.0f);
    g_assert_cmpfloat (end, ==, 2.0f);
    lrg_keyframe_curve_clear (curve);
    g_assert_false (lrg_keyframe_curve_get_time_range (curve, &start, &end));
    g_assert_cmpfloat (start, ==, 0.0f);
    g_assert_cmpfloat (end, ==, 0.0f);
    lrg_keyframe_curve_add_key (curve, 9.0f, 1.0f, LRG_EASING_LINEAR);
    g_assert_true (lrg_keyframe_curve_get_time_range (curve, &start, &end));
    g_assert_cmpfloat (start, ==, 9.0f);
    g_assert_cmpfloat (end, ==, 9.0f);
    g_assert_true (lrg_keyframe_curve_get_time_range (curve, NULL, NULL));
}

static void
test_path_set (void)
{
    g_autoptr(LrgPath) path = make_path ();

    g_assert_false (lrg_path_set_point (path, G_MAXUINT, 5, 6));
    g_assert_true (lrg_path_set_point (path, 1, 3, 4));
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 42.0f);
    g_assert_true (lrg_path_set_point (path, 1, G_MININT, G_MAXINT));
    assert_point (path, 1, G_MININT, G_MAXINT);
    g_assert_cmpuint (lrg_path_get_length (path), ==, 3);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 0.0f);
}

static void
test_path_remove (void)
{
    g_autoptr(LrgPath) path = make_path ();

    g_assert_false (lrg_path_remove_point (path, 3));
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 42.0f);
    g_assert_true (lrg_path_remove_point (path, 1));
    assert_point (path, 1, 6, 8);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 0.0f);
    g_assert_true (lrg_path_remove_point (path, 0));
    assert_point (path, 0, 6, 8);
    g_assert_true (lrg_path_remove_point (path, 0));
    g_assert_true (lrg_path_is_empty (path));
    g_assert_false (lrg_path_remove_point (path, 0));
}

static void
test_path_truncate (void)
{
    g_autoptr(LrgPath) path = make_path ();

    lrg_path_truncate (path, G_MAXUINT);
    lrg_path_truncate (path, 3);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 42.0f);
    lrg_path_truncate (path, 2);
    g_assert_cmpuint (lrg_path_get_length (path), ==, 2);
    assert_point (path, 1, 3, 4);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 0.0f);
    lrg_path_truncate (path, 0);
    g_assert_true (lrg_path_is_empty (path));
}

static void
test_path_append_path (void)
{
    g_autoptr(LrgPath) path = make_path ();
    g_autoptr(LrgPath) other = lrg_path_new ();
    guint i;

    lrg_path_append_path (path, other);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 42.0f);
    lrg_path_append_path (path, path);
    g_assert_cmpuint (lrg_path_get_length (path), ==, 6);
    for (i = 0; i < 6; i++)
        assert_point (path, i, (i % 3) * 3, (i % 3) * 4);
    g_assert_cmpfloat (lrg_path_get_total_cost (path), ==, 0.0f);
    lrg_path_append_path (other, path);
    lrg_path_clear (path);
    g_assert_cmpuint (lrg_path_get_length (other), ==, 6);
    assert_point (other, 5, 6, 8);
}

static void
test_path_distance (void)
{
    g_autoptr(LrgPath) path = make_path ();

    g_assert_cmpfloat (lrg_path_get_distance (path), ==, 10.0);
    lrg_path_reverse (path);
    g_assert_cmpfloat (lrg_path_get_distance (path), ==, 10.0);
    lrg_path_clear (path);
    g_assert_cmpfloat (lrg_path_get_distance (path), ==, 0.0);
    lrg_path_append (path, G_MININT, 0);
    g_assert_cmpfloat (lrg_path_get_distance (path), ==, 0.0);
    lrg_path_append (path, G_MAXINT, 0);
    g_assert_cmpfloat (lrg_path_get_distance (path), ==, (gdouble) G_MAXINT - G_MININT);
    lrg_path_append (path, G_MAXINT, 0);
    g_assert_cmpfloat (lrg_path_get_distance (path), ==, 4294967295.0);
}

static void
test_blackboard_size (void)
{
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();

    g_assert_cmpuint (lrg_blackboard_get_size (board), ==, 0);
    lrg_blackboard_set_string (board, "empty", NULL);
    lrg_blackboard_set_int (board, "count", 1);
    lrg_blackboard_set_int (board, "count", 2);
    g_assert_cmpuint (lrg_blackboard_get_size (board), ==, 2);
    lrg_blackboard_remove (board, "empty");
    g_assert_cmpuint (lrg_blackboard_get_size (board), ==, 1);
    lrg_blackboard_clear (board);
    g_assert_cmpuint (lrg_blackboard_get_size (board), ==, 0);
}

static void
test_blackboard_keys (void)
{
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();
    g_auto(GStrv) keys = lrg_blackboard_dup_keys (board);

    g_assert_nonnull (keys);
    g_assert_null (keys[0]);
    g_clear_pointer (&keys, g_strfreev);
    lrg_blackboard_set_int (board, "z", 1);
    lrg_blackboard_set_int (board, "a", 2);
    lrg_blackboard_set_int (board, "", 3);
    keys = lrg_blackboard_dup_keys (board);
    lrg_blackboard_clear (board);
    g_clear_object (&board);
    g_assert_cmpstr (keys[0], ==, "");
    g_assert_cmpstr (keys[1], ==, "a");
    g_assert_cmpstr (keys[2], ==, "z");
    g_assert_null (keys[3]);
}

static void
test_blackboard_string (void)
{
    g_autoptr(LrgBlackboard) board = lrg_blackboard_new ();
    g_autofree gchar *value = NULL;

    g_assert_null (lrg_blackboard_dup_string (board, "missing"));
    lrg_blackboard_set_int (board, "key", 1);
    g_assert_null (lrg_blackboard_dup_string (board, "key"));
    lrg_blackboard_set_string (board, "key", NULL);
    g_assert_null (lrg_blackboard_dup_string (board, "key"));
    lrg_blackboard_set_string (board, "key", "original");
    value = lrg_blackboard_dup_string (board, "key");
    lrg_blackboard_set_string (board, "key", "replacement");
    g_clear_object (&board);
    g_assert_cmpstr (value, ==, "original");
}

static void
test_random_copy (void)
{
    g_autoptr(LrgRandomStream) stream = lrg_random_stream_new (1234, 5678);
    g_autoptr(LrgRandomStream) copy = NULL;
    g_autofree gchar *before = NULL;
    g_autofree gchar *after = NULL;
    guint i;

    lrg_random_stream_advance (stream, G_MAXUINT64 - 100);
    before = lrg_random_stream_snapshot (stream);
    copy = lrg_random_stream_copy (stream);
    for (i = 0; i < 100; i++)
        lrg_random_stream_next_uint (copy);
    after = lrg_random_stream_snapshot (stream);
    g_assert_cmpstr (before, ==, after);
    lrg_random_stream_advance (stream, 100);
    for (i = 0; i < 128; i++)
        g_assert_cmpuint (lrg_random_stream_next_uint (copy), ==,
                          lrg_random_stream_next_uint (stream));
    g_clear_object (&stream);
    g_assert_cmpfloat (lrg_random_stream_next_double (copy), >=, 0.0);
}

static void
test_easing_large_span (void)
{
    gfloat result;

    result = lrg_easing_interpolate (LRG_EASING_LINEAR, -G_MAXFLOAT, G_MAXFLOAT, 0.5f);
    g_assert_cmpfloat (result, ==, 0.0f);
    result = lrg_easing_interpolate (LRG_EASING_LINEAR, G_MAXFLOAT, -G_MAXFLOAT, 0.75f);
    g_assert_cmpfloat (result, ==, -G_MAXFLOAT / 2.0f);
    g_assert_cmpfloat (lrg_easing_interpolate (LRG_EASING_LINEAR, -G_MAXFLOAT,
                                            G_MAXFLOAT, 0.0f), ==, -G_MAXFLOAT);
    g_assert_cmpfloat (lrg_easing_interpolate (LRG_EASING_LINEAR, -G_MAXFLOAT,
                                            G_MAXFLOAT, 1.0f), ==, G_MAXFLOAT);
    g_assert_cmpfloat (lrg_easing_interpolate (LRG_EASING_LINEAR, 1, 3, 2), ==, 5);
    g_assert_cmpfloat (lrg_easing_interpolate (LRG_EASING_EASE_OUT_BACK, 0, 1, 0.7f), >, 1);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/utilities/curve/get", test_curve_get);
    g_test_add_func ("/utilities/curve/remove", test_curve_remove);
    g_test_add_func ("/utilities/curve/clear", test_curve_clear);
    g_test_add_func ("/utilities/curve/copy", test_curve_copy);
    g_test_add_func ("/utilities/curve/range", test_curve_range);
    g_test_add_func ("/utilities/path/set", test_path_set);
    g_test_add_func ("/utilities/path/remove", test_path_remove);
    g_test_add_func ("/utilities/path/truncate", test_path_truncate);
    g_test_add_func ("/utilities/path/append", test_path_append_path);
    g_test_add_func ("/utilities/path/distance", test_path_distance);
    g_test_add_func ("/utilities/blackboard/size", test_blackboard_size);
    g_test_add_func ("/utilities/blackboard/keys", test_blackboard_keys);
    g_test_add_func ("/utilities/blackboard/string", test_blackboard_string);
    g_test_add_func ("/utilities/random/copy", test_random_copy);
    g_test_add_func ("/utilities/easing/large-span", test_easing_large_span);
    return g_test_run ();
}
