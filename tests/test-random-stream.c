/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>

static void
test_vector (void)
{
    g_autoptr(LrgRandomStream) stream = lrg_random_stream_new (42, 54);
    const guint32 expected[] = { 0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                0x83d2f293, 0xbfa4784b, 0xcbed606e };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (expected); i++)
        g_assert_cmpuint (lrg_random_stream_next_uint (stream), ==, expected[i]);
}

static void
test_resume (void)
{
    g_autoptr(LrgRandomStream) a = lrg_random_stream_new (G_MAXUINT64, 17);
    g_autoptr(LrgRandomStream) b = lrg_random_stream_new (0, 0);
    g_autofree gchar *snapshot = NULL;
    guint i;

    for (i = 0; i < 57; i++)
        lrg_random_stream_next_uint (a);
    snapshot = lrg_random_stream_snapshot (a);
    g_assert_true (lrg_random_stream_restore (b, snapshot, NULL));
    for (i = 0; i < 1000; i++)
    {
        g_assert_cmpuint (lrg_random_stream_bounded (a, 2147483649u), ==,
                          lrg_random_stream_bounded (b, 2147483649u));
        g_assert_cmpfloat (lrg_random_stream_next_double (a), ==,
                           lrg_random_stream_next_double (b));
    }
}

static void
test_invalid (void)
{
    g_autoptr(LrgRandomStream) stream = lrg_random_stream_new (1, 2);
    g_autofree gchar *before = lrg_random_stream_snapshot (stream);
    const gchar *bad[] = { NULL, "", "pcg32-v2:0000000000000000:0000000000000001",
                          "pcg32-v1:0000000000000000:0000000000000002",
                          "pcg32-v1:000000000000000z:0000000000000001",
                          "pcg32-v1:0000000000000000:0000000000000001x" };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (bad); i++)
    {
        g_autoptr(GError) error = NULL;
        g_autofree gchar *after = NULL;

        g_assert_false (lrg_random_stream_restore (stream, bad[i], &error));
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        after = lrg_random_stream_snapshot (stream);
        g_assert_cmpstr (before, ==, after);
    }
}

static void
test_independence_and_bounds (void)
{
    g_autoptr(LrgRandomStream) a = lrg_random_stream_new (12, 4);
    g_autoptr(LrgRandomStream) b = lrg_random_stream_new (12, 4);
    g_autoptr(LrgRandomStream) c = lrg_random_stream_new (12, 5);
    guint i;
    gboolean different = FALSE;
    gboolean seen[7] = { FALSE };

    for (i = 0; i < 10000; i++)
    {
        guint32 value = lrg_random_stream_next_uint (a);
        guint32 bounded;
        gdouble unit;

        g_random_int ();
        g_assert_cmpuint (value, ==, lrg_random_stream_next_uint (b));
        different |= value != lrg_random_stream_next_uint (c);
        bounded = lrg_random_stream_bounded (c, 7);
        g_assert_cmpuint (bounded, <, 7);
        seen[bounded] = TRUE;
        g_assert_cmpuint (lrg_random_stream_bounded (c, 1), ==, 0);
        g_assert_cmpuint (lrg_random_stream_bounded (c, G_MAXUINT32), <, G_MAXUINT32);
        unit = lrg_random_stream_next_double (c);
        g_assert_cmpfloat (unit, >=, 0);
        g_assert_cmpfloat (unit, <, 1);
    }
    g_assert_true (different);
    for (i = 0; i < 7; i++)
        g_assert_true (seen[i]);
}

static void
test_save_context (void)
{
    g_autoptr(LrgRandomStream) stream = lrg_random_stream_new (42, 54);
    g_autoptr(LrgSaveContext) save = lrg_save_context_new_for_save ();
    g_autoptr(LrgSaveContext) load = NULL;
    g_autofree gchar *snapshot = NULL;
    g_autofree gchar *yaml = NULL;
    g_autoptr(GError) error = NULL;
    guint32 expected;

    lrg_random_stream_next_double (stream);
    snapshot = lrg_random_stream_snapshot (stream);
    lrg_save_context_write_string (save, "loot-rng", snapshot);
    yaml = lrg_save_context_to_string (save, &error);
    g_assert_no_error (error);
    load = lrg_save_context_new_for_load (yaml, &error);
    g_assert_no_error (error);
    expected = lrg_random_stream_next_uint (stream);
    g_assert_true (lrg_random_stream_restore (stream,
        lrg_save_context_read_string (load, "loot-rng", NULL), &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_random_stream_next_uint (stream), ==, expected);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/random/vector", test_vector);
    g_test_add_func ("/random/resume", test_resume);
    g_test_add_func ("/random/invalid", test_invalid);
    g_test_add_func ("/random/independence-bounds", test_independence_and_bounds);
    g_test_add_func ("/random/save-context", test_save_context);
    return g_test_run ();
}
