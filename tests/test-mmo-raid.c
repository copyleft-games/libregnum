/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <math.h>

static void
assert_error (GError **error,
              gint     code)
{
    g_assert_error (*error, G_IO_ERROR, code);
    g_clear_error (error);
}

/* a lock lasts to the next reset boundary and not a second longer */
static void
test_lockout_period (void)
{
    g_autoptr(LrgMmoLockout) book = lrg_mmo_lockout_new (NULL, LRG_RESET_PERIOD_WEEKLY);
    LrgResetSchedule *schedule = lrg_mmo_lockout_get_schedule (book);
    gint64 start = lrg_reset_schedule_get_period_start (schedule, LRG_RESET_PERIOD_WEEKLY, 2900);
    gint64 next = start + 7 * 86400;
    gint64 mid = start + 3 * 86400 + 777;

    g_assert_cmpint (lrg_mmo_lockout_get_period (book), ==, LRG_RESET_PERIOD_WEEKLY);
    g_assert_false (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", mid));
    g_assert_cmpint (lrg_mmo_lockout_get_reset (book, "alice", "boss:1", mid), ==, 0);
    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "boss:1", mid));
    /* the second lock in the same week is refused, and changes nothing */
    g_assert_false (lrg_mmo_lockout_lock (book, "alice", "boss:1", mid + 5));
    g_assert_cmpuint (lrg_mmo_lockout_get_count (book), ==, 1);
    g_assert_true (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", mid));
    g_assert_true (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", next - 1));
    g_assert_cmpint (lrg_mmo_lockout_get_reset (book, "alice", "boss:1", mid), ==, next);
    /* the reset instant opens the next week */
    g_assert_false (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", next));
    /* others and other keys are not locked */
    g_assert_false (lrg_mmo_lockout_is_locked (book, "bob", "boss:1", mid));
    g_assert_false (lrg_mmo_lockout_is_locked (book, "alice", "boss:2", mid));
    /* a new week locks again, replacing the old lock */
    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "boss:1", next + 10));
    g_assert_cmpuint (lrg_mmo_lockout_get_count (book), ==, 1);
    g_assert_true (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", next + 20));
    g_assert_false (lrg_mmo_lockout_is_locked (book, "alice", "boss:1", mid));
}

static void
test_lockout_list_unlock_purge (void)
{
    g_autoptr(LrgMmoLockout) book = lrg_mmo_lockout_new (NULL, LRG_RESET_PERIOD_DAILY);
    g_autoptr(GVariant) list = NULL;
    const gchar *key;
    gint64 reset;
    gint64 now = 5 * 86400 + 100;

    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "zeta", now));
    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "alpha", now));
    g_assert_true (lrg_mmo_lockout_lock (book, "bob", "alpha", now));
    list = lrg_mmo_lockout_list (book, "alice", now);
    g_assert_cmpuint (g_variant_n_children (list), ==, 2);
    g_variant_get_child (list, 0, "(&sx)", &key, &reset);
    g_assert_cmpstr (key, ==, "alpha");
    g_assert_cmpint (reset, ==, lrg_mmo_lockout_get_reset (book, "alice", "alpha", now));
    g_variant_get_child (list, 1, "(&sx)", &key, &reset);
    g_assert_cmpstr (key, ==, "zeta");
    g_clear_pointer (&list, g_variant_unref);
    /* a day later the list is empty and the locks are dead weight */
    list = lrg_mmo_lockout_list (book, "alice", now + 86400);
    g_assert_cmpuint (g_variant_n_children (list), ==, 0);
    g_assert_cmpuint (lrg_mmo_lockout_get_count (book), ==, 3);
    g_assert_cmpuint (lrg_mmo_lockout_purge (book, now + 86400), ==, 3);
    g_assert_cmpuint (lrg_mmo_lockout_get_count (book), ==, 0);
    /* unlock lifts a live lock, once */
    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "alpha", now));
    g_assert_true (lrg_mmo_lockout_unlock (book, "alice", "alpha"));
    g_assert_false (lrg_mmo_lockout_unlock (book, "alice", "alpha"));
    g_assert_false (lrg_mmo_lockout_is_locked (book, "alice", "alpha", now));
    /* invalid names are never locked */
    g_assert_false (lrg_mmo_lockout_lock (book, "", "alpha", now));
    g_assert_false (lrg_mmo_lockout_lock (book, "alice", NULL, now));
    g_assert_false (lrg_mmo_lockout_lock (book, "ali\x1f" "ce", "alpha", now));
    g_assert_cmpuint (lrg_mmo_lockout_get_count (book), ==, 0);
}

/* the book survives a round trip byte for byte; bad state changes nothing */
static void
test_lockout_persist (void)
{
    g_autoptr(LrgMmoLockout) book = lrg_mmo_lockout_new (NULL, LRG_RESET_PERIOD_WEEKLY);
    g_autoptr(LrgMmoLockout) other = lrg_mmo_lockout_new (NULL, LRG_RESET_PERIOD_WEEKLY);
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GVariant) again = NULL;
    g_autoptr(GVariant) bad = NULL;
    g_autoptr(GError) error = NULL;
    gint64 now = 40 * 86400;

    g_assert_true (lrg_mmo_lockout_lock (book, "alice", "boss:1", now));
    g_assert_true (lrg_mmo_lockout_lock (book, "bob", "gate:7", now));
    state = lrg_mmo_lockout_to_variant (book);
    g_assert_true (g_variant_is_of_type (state, G_VARIANT_TYPE (LRG_MMO_LOCKOUT_VARIANT_TYPE)));
    g_assert_true (lrg_mmo_lockout_restore (other, state, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_lockout_is_locked (other, "alice", "boss:1", now));
    g_assert_true (lrg_mmo_lockout_is_locked (other, "bob", "gate:7", now));
    again = lrg_mmo_lockout_to_variant (other);
    g_assert_true (g_variant_equal (state, again));

    /* wrong type, empty name, duplicates: refused, the book stays */
    bad = g_variant_ref_sink (g_variant_new ("a(ss)", NULL));
    g_assert_false (lrg_mmo_lockout_restore (other, bad, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_pointer (&bad, g_variant_unref);
    bad = g_variant_ref_sink (g_variant_new_parsed ("[('x', 'k', int64 3), ('', 'k', int64 3)]"));
    g_assert_false (lrg_mmo_lockout_restore (other, bad, &error));
    assert_error (&error, G_IO_ERROR_INVALID_DATA);
    g_clear_pointer (&bad, g_variant_unref);
    bad = g_variant_ref_sink (g_variant_new_parsed ("[('x', 'k', int64 3), ('x', 'k', int64 4)]"));
    g_assert_false (lrg_mmo_lockout_restore (other, bad, &error));
    assert_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_cmpuint (lrg_mmo_lockout_get_count (other), ==, 2);
    g_assert_true (lrg_mmo_lockout_is_locked (other, "alice", "boss:1", now));
    /* NULL restores an empty book */
    g_assert_true (lrg_mmo_lockout_restore (other, NULL, &error));
    g_assert_cmpuint (lrg_mmo_lockout_get_count (other), ==, 0);
}

static void
test_tag_board (void)
{
    g_autoptr(LrgMmoTagBoard) board = lrg_mmo_tag_board_new ();
    g_autoptr(GVariant) list = NULL;
    g_auto(GStrv) eligible = NULL;
    const gchar *account;
    gdouble damage;
    gdouble presence;

    g_assert_null (lrg_mmo_tag_board_get_first (board, 9));
    g_assert_false (lrg_mmo_tag_board_is_tagged (board, 9, "alice"));
    g_assert_true (lrg_mmo_tag_board_tag (board, 9, "alice", 60.0));
    g_assert_false (lrg_mmo_tag_board_tag (board, 9, "alice", 40.0));
    g_assert_true (lrg_mmo_tag_board_tag (board, 9, "bob", 5.0));
    /* standing near tags without a blow */
    g_assert_true (lrg_mmo_tag_board_add_presence (board, 9, "carol", 20.0));
    g_assert_true (lrg_mmo_tag_board_add_presence (board, 9, "dave", 2.0));
    g_assert_false (lrg_mmo_tag_board_add_presence (board, 9, "carol", 1.0));
    g_assert_cmpstr (lrg_mmo_tag_board_get_first (board, 9), ==, "alice");
    g_assert_cmpfloat (lrg_mmo_tag_board_get_amount (board, 9, "alice"), ==, 100.0);
    g_assert_cmpfloat (lrg_mmo_tag_board_get_presence (board, 9, "carol"), ==, 21.0);
    g_assert_cmpfloat (lrg_mmo_tag_board_get_total (board, 9), ==, 105.0);
    g_assert_cmpuint (lrg_mmo_tag_board_get_count (board, 9), ==, 4);
    g_assert_true (lrg_mmo_tag_board_is_tagged (board, 9, "dave"));
    /* targets are independent */
    g_assert_cmpuint (lrg_mmo_tag_board_get_count (board, 10), ==, 0);
    g_assert_true (lrg_mmo_tag_board_tag (board, 10, "bob", 1.0));
    g_assert_cmpstr (lrg_mmo_tag_board_get_first (board, 10), ==, "bob");
    g_assert_cmpuint (lrg_mmo_tag_board_get_target_count (board), ==, 2);

    list = lrg_mmo_tag_board_list (board, 9);
    g_variant_get_child (list, 0, "(&sdd)", &account, &damage, &presence);
    g_assert_cmpstr (account, ==, "alice");
    g_assert_cmpfloat (damage, ==, 100.0);

    /* 10 % of the damage or 15 seconds of presence: alice (95 %), bob
     * (4.8 % of 105 fails), carol qualifies by presence, dave by neither */
    eligible = lrg_mmo_tag_board_get_eligible (board, 9, 0.10, 15.0);
    g_assert_cmpuint (g_strv_length (eligible), ==, 2);
    g_assert_cmpstr (eligible[0], ==, "alice");
    g_assert_cmpstr (eligible[1], ==, "carol");
    g_strfreev (eligible);
    eligible = lrg_mmo_tag_board_get_eligible (board, 9, 0.0, 1000.0);
    /* a zero share still needs some damage: bob and alice only */
    g_assert_cmpuint (g_strv_length (eligible), ==, 2);
    g_assert_cmpstr (eligible[1], ==, "bob");

    /* refusals */
    g_assert_false (lrg_mmo_tag_board_tag (board, 9, "", 1.0));
    g_assert_false (lrg_mmo_tag_board_tag (board, 9, "erin", -1.0));
    g_assert_false (lrg_mmo_tag_board_tag (board, 9, "erin", NAN));
    g_assert_false (lrg_mmo_tag_board_add_presence (board, 9, "erin", 0.0));
    g_assert_cmpuint (lrg_mmo_tag_board_get_count (board, 9), ==, 4);

    g_assert_true (lrg_mmo_tag_board_clear (board, 9));
    g_assert_false (lrg_mmo_tag_board_clear (board, 9));
    g_assert_cmpuint (lrg_mmo_tag_board_get_count (board, 9), ==, 0);
    lrg_mmo_tag_board_clear_all (board);
    g_assert_cmpuint (lrg_mmo_tag_board_get_target_count (board), ==, 0);
}

/* a target holds at most LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS accounts */
static void
test_tag_board_cap (void)
{
    g_autoptr(LrgMmoTagBoard) board = lrg_mmo_tag_board_new ();
    guint i;

    for (i = 0; i < LRG_MMO_TAG_BOARD_MAX_CONTRIBUTORS; i++)
    {
        g_autofree gchar *name = g_strdup_printf ("p%u", i);

        g_assert_true (lrg_mmo_tag_board_tag (board, 1, name, 1.0));
    }
    g_assert_false (lrg_mmo_tag_board_tag (board, 1, "one-too-many", 1.0));
    g_assert_false (lrg_mmo_tag_board_is_tagged (board, 1, "one-too-many"));
    /* an account already in still contributes */
    g_assert_false (lrg_mmo_tag_board_tag (board, 1, "p0", 5.0));
    g_assert_cmpfloat (lrg_mmo_tag_board_get_amount (board, 1, "p0"), ==, 6.0);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/lockout/period", test_lockout_period);
    g_test_add_func ("/mmo/lockout/list-unlock-purge", test_lockout_list_unlock_purge);
    g_test_add_func ("/mmo/lockout/persist", test_lockout_persist);
    g_test_add_func ("/mmo/tag-board/basic", test_tag_board);
    g_test_add_func ("/mmo/tag-board/cap", test_tag_board_cap);
    return g_test_run ();
}
