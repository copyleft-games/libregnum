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

static void
test_advance_reference (void)
{
    const guint counts[] = { 0, 1, 2, 3, 31, 32, 63, 64, 255, 1024, 65537 };
    const guint64 seeds[] = { 0, 42, G_MAXUINT64 };
    guint i, j, k;

    for (i = 0; i < G_N_ELEMENTS (seeds); i++)
    {
        for (j = 0; j < G_N_ELEMENTS (counts); j++)
        {
            g_autoptr(LrgRandomStream) fast = lrg_random_stream_new (seeds[i], seeds[i]);
            g_autoptr(LrgRandomStream) slow = lrg_random_stream_new (seeds[i], seeds[i]);
            g_autofree gchar *fast_state = NULL;
            g_autofree gchar *slow_state = NULL;

            lrg_random_stream_advance (fast, counts[j]);
            for (k = 0; k < counts[j]; k++)
                lrg_random_stream_next_uint (slow);
            fast_state = lrg_random_stream_snapshot (fast);
            slow_state = lrg_random_stream_snapshot (slow);
            g_assert_cmpstr (fast_state, ==, slow_state);
            for (k = 0; k < 10; k++)
                g_assert_cmpuint (lrg_random_stream_next_uint (fast), ==,
                                  lrg_random_stream_next_uint (slow));
        }
    }
}

static void
test_advance_large (void)
{
    g_autoptr(LrgRandomStream) a = lrg_random_stream_new (42, 54);
    g_autoptr(LrgRandomStream) b = lrg_random_stream_new (42, 54);
    g_autofree gchar *before = lrg_random_stream_snapshot (a);
    g_autofree gchar *after = NULL;
    g_autofree gchar *half_state = g_strdup (before);
    const guint64 half = G_GUINT64_CONSTANT (1) << 63;
    guint i;

    /* The odd-increment LCG has period 2^64. */
    lrg_random_stream_advance (a, G_MAXUINT64);
    lrg_random_stream_advance (a, 1);
    after = lrg_random_stream_snapshot (a);
    g_assert_cmpstr (before, ==, after);

    /* Half a period changes only the high state bit for this full-period LCG. */
    half_state[9] = "0123456789abcdef"[g_ascii_xdigit_value (half_state[9]) ^ 8];
    lrg_random_stream_advance (a, half);
    g_clear_pointer (&after, g_free);
    after = lrg_random_stream_snapshot (a);
    g_assert_cmpstr (half_state, ==, after);
    lrg_random_stream_advance (a, half);

    lrg_random_stream_advance (a, half + 12345);
    lrg_random_stream_advance (b, half);
    lrg_random_stream_advance (b, 12345);
    for (i = 0; i < 100; i++)
        g_assert_cmpuint (lrg_random_stream_next_uint (a), ==,
                          lrg_random_stream_next_uint (b));
}

static void
test_advance_restore (void)
{
    g_autoptr(LrgRandomStream) a = lrg_random_stream_new (123, 456);
    g_autoptr(LrgRandomStream) b = lrg_random_stream_new (0, 0);
    g_autofree gchar *snapshot = NULL;
    guint i;

    lrg_random_stream_advance (a, G_MAXUINT64 - 500);
    snapshot = lrg_random_stream_snapshot (a);
    g_assert_true (lrg_random_stream_restore (b, snapshot, NULL));
    /* Double draws consume exactly two raw outputs. */
    for (i = 0; i < 20; i++)
        lrg_random_stream_next_double (a);
    lrg_random_stream_advance (b, 40);
    g_assert_cmpuint (lrg_random_stream_next_uint (a), ==,
                      lrg_random_stream_next_uint (b));
}

static void
test_restore_existing_error (void)
{
    g_autoptr(LrgRandomStream) stream = lrg_random_stream_new (1, 2);
    g_autofree gchar *before = lrg_random_stream_snapshot (stream);
    g_autofree gchar *after = NULL;
    g_autoptr(GError) error = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED, "existing");
    GError *original = error;

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*error == NULL || *error == NULL*");
    g_assert_false (lrg_random_stream_restore (stream,
                    "pcg32-v1:0000000000000000:0000000000000001", &error));
    g_test_assert_expected_messages ();
    g_assert_true (error == original);
    after = lrg_random_stream_snapshot (stream);
    g_assert_cmpstr (before, ==, after);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/random/vector", test_vector);
    g_test_add_func ("/random/advance-reference", test_advance_reference);
    g_test_add_func ("/random/advance-large", test_advance_large);
    g_test_add_func ("/random/advance-restore", test_advance_restore);
    g_test_add_func ("/random/restore-existing-error", test_restore_existing_error);
    g_test_add_func ("/random/resume", test_resume);
    g_test_add_func ("/random/invalid", test_invalid);
    g_test_add_func ("/random/independence-bounds", test_independence_and_bounds);
    g_test_add_func ("/random/save-context", test_save_context);
    return g_test_run ();
}
