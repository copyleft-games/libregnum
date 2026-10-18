/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * test-mmo-finder.c - role queues (LrgMmoMatchmaker) and instances
 * (LrgMmoInstances), the pieces of a group finder.
 */
#include "mmo/lrg-mmo-matchmaker.h"
#include "mmo/lrg-mmo-instances.h"

#define SECOND G_TIME_SPAN_SECOND

enum { TANK, HEALER, DAMAGE };
#define ROLE(r) (1u << (r))

static const guint dungeon[] = { 1, 1, 3 };

static void
clear_error (GError **error, gint code)
{
    g_assert_error (*error, G_IO_ERROR, code);
    g_clear_error (error);
}

static gboolean
solo (LrgMmoMatchmaker *queue, const gchar *account, guint rating, guint roles,
      gint64 since, gint64 now, GError **error)
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suu)"));
    g_variant_builder_add (&builder, "(suu)", account, rating, roles);
    return lrg_mmo_matchmaker_enqueue_roles (queue, g_variant_builder_end (&builder), "dungeon", since, now, error);
}

/* the role index @account got in @match, -1 when absent */
static gint
role_of (GVariant *match, const gchar *account)
{
    GVariantIter iter;
    const gchar *name;
    guint role;
    g_variant_iter_init (&iter, match);
    while (g_variant_iter_next (&iter, "(&su)", &name, &role))
        if (g_str_equal (name, account))
            return (gint) role;
    return -1;
}

static void
test_roles_fill (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (32);
    g_autoptr(GVariant) match = NULL;
    g_autoptr(GError) error = NULL;

    /* five damage dealers and nobody to tank or heal: no group */
    g_assert_true (solo (queue, "d1", 10, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "d2", 10, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "d3", 10, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "d4", 10, ROLE (DAMAGE), 0, 0, &error));
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 100, 1, G_MAXINT64, SECOND, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 0);
    g_clear_pointer (&match, g_variant_unref);

    /* a flexible player (tank or damage) and a healer: the flexible one tanks, d4 waits */
    g_assert_true (solo (queue, "flex", 12, ROLE (TANK) | ROLE (DAMAGE), SECOND, SECOND, &error));
    g_assert_true (solo (queue, "heal", 11, ROLE (HEALER), SECOND, SECOND, &error));
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 100, 1, G_MAXINT64, 2 * SECOND, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 5);
    g_assert_cmpint (role_of (match, "flex"), ==, TANK);
    g_assert_cmpint (role_of (match, "heal"), ==, HEALER);
    g_assert_cmpint (role_of (match, "d1"), ==, DAMAGE);
    g_assert_cmpint (role_of (match, "d3"), ==, DAMAGE);
    g_assert_cmpint (role_of (match, "d4"), ==, -1);
    g_assert_true (lrg_mmo_matchmaker_is_queued (queue, "d4"));
    g_assert_false (lrg_mmo_matchmaker_is_queued (queue, "flex"));
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, "dungeon"), ==, 1);
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, NULL), ==, 1);
}

static void
test_roles_party_and_spread (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (32);
    g_autoptr(GVariant) match = NULL;
    g_autoptr(GPtrArray) ticket = NULL;
    g_autoptr(GError) error = NULL;
    GVariantBuilder builder;
    const gchar *mode = NULL;
    gint64 since = -1;
    guint roles = 99;

    /* a party of two tanks cannot share one tank seat: never matched together */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suu)"));
    g_variant_builder_add (&builder, "(suu)", "t1", 10, ROLE (TANK));
    g_variant_builder_add (&builder, "(suu)", "t2", 10, ROLE (TANK));
    g_assert_true (lrg_mmo_matchmaker_enqueue_roles (queue, g_variant_builder_end (&builder), "dungeon", 0, 0, &error));
    /* a party with any role (mask 0) and a healer */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(suu)"));
    g_variant_builder_add (&builder, "(suu)", "any", 10, 0u);
    g_variant_builder_add (&builder, "(suu)", "h1", 10, ROLE (HEALER));
    g_assert_true (lrg_mmo_matchmaker_enqueue_roles (queue, g_variant_builder_end (&builder), "dungeon", 0, 0, &error));
    ticket = lrg_mmo_matchmaker_get_ticket (queue, "h1", &mode, &since, &roles);
    g_assert_cmpuint (ticket->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (ticket, 0), ==, "any");
    g_assert_cmpstr (mode, ==, "dungeon");
    g_assert_cmpint (since, ==, 0);
    g_assert_cmpuint (roles, ==, ROLE (HEALER));
    g_assert_null (lrg_mmo_matchmaker_get_ticket (queue, "nobody", NULL, NULL, NULL));
    /* a far rating stays out of the group */
    g_assert_true (solo (queue, "far", 900, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "d1", 12, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "d2", 14, ROLE (DAMAGE), 0, 0, &error));
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 20, 1, G_MAXINT64, 0, &error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 0);
    g_clear_pointer (&match, g_variant_unref);
    g_assert_true (solo (queue, "d3", 15, ROLE (DAMAGE), 0, 0, &error));
    /* the tank party is skipped (it does not fit), "any" tanks */
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 20, 1, G_MAXINT64, 0, &error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 5);
    g_assert_cmpint (role_of (match, "any"), ==, TANK);
    g_assert_cmpint (role_of (match, "far"), ==, -1);
    g_assert_cmpint (role_of (match, "t1"), ==, -1);
    g_assert_true (lrg_mmo_matchmaker_is_queued (queue, "t2"));
    {
        const guint two_tanks[] = { ROLE (TANK), ROLE (TANK) };
        const guint tank_any[] = { ROLE (TANK), 0 };
        g_assert_false (lrg_mmo_matchmaker_roles_fit (two_tanks, 2, dungeon, 3));
        g_assert_true (lrg_mmo_matchmaker_roles_fit (tank_any, 2, dungeon, 3));
        g_assert_false (lrg_mmo_matchmaker_roles_fit (tank_any, 2, dungeon, 0));
    }
}

static void
test_roles_partial_swap_expiry (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (8);
    g_autoptr(GVariant) match = NULL;
    g_autoptr(GPtrArray) expired = NULL;
    g_autoptr(GPtrArray) modes = NULL;
    g_autoptr(GError) error = NULL;

    lrg_mmo_matchmaker_set_timeout (queue, 600 * SECOND);
    g_assert_cmpint (lrg_mmo_matchmaker_get_timeout (queue), ==, 600 * SECOND);
    g_assert_true (solo (queue, "a", 5, ROLE (DAMAGE), 0, 0, &error));
    g_assert_true (solo (queue, "b", 5, ROLE (DAMAGE), 0, 0, &error));
    /* a swap keeps the place in the queue */
    g_assert_true (lrg_mmo_matchmaker_set_roles (queue, "b", ROLE (TANK), &error));
    g_assert_false (lrg_mmo_matchmaker_set_roles (queue, "nobody", ROLE (TANK), &error));
    clear_error (&error, G_IO_ERROR_NOT_FOUND);
    g_assert_false (lrg_mmo_matchmaker_set_roles (queue, "a", 1u << 12, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    /* not long enough for a short group */
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 100, 2, 30 * SECOND, 10 * SECOND, &error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 0);
    g_clear_pointer (&match, g_variant_unref);
    /* a minimum above the group: still nothing */
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 100, 3, 30 * SECOND, 31 * SECOND, &error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 0);
    g_clear_pointer (&match, g_variant_unref);
    /* thirty seconds: the two start short, b tanks */
    match = lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 3, 100, 2, 30 * SECOND, 31 * SECOND, &error);
    g_assert_cmpuint (g_variant_n_children (match), ==, 2);
    g_assert_cmpint (role_of (match, "b"), ==, TANK);
    g_assert_cmpint (role_of (match, "a"), ==, DAMAGE);

    /* put back after a decline: an old since keeps the front place */
    g_assert_true (solo (queue, "c", 5, ROLE (HEALER), 40 * SECOND, 40 * SECOND, &error));
    g_assert_true (solo (queue, "a", 5, ROLE (DAMAGE), 0, 41 * SECOND, &error));
    modes = lrg_mmo_matchmaker_list_modes (queue);
    g_assert_cmpuint (modes->len, ==, 1);
    {
        g_autoptr(GVariant) saved = lrg_mmo_matchmaker_save (queue, 41 * SECOND);
        const gchar *first = NULL;
        gint64 age = 0;
        g_autoptr(GVariant) members = NULL;
        g_variant_get_child (saved, 0, "(&sx@a(suu))", &first, &age, &members);
        g_assert_cmpint (age, ==, 41 * SECOND);
    }
    /* expiry tells who; a ticket older than the timeout is refused */
    expired = lrg_mmo_matchmaker_expire (queue, 700 * SECOND);
    g_assert_cmpuint (expired->len, ==, 2);
    g_assert_false (solo (queue, "z", 5, ROLE (DAMAGE), 0, 700 * SECOND, &error));
    clear_error (&error, G_IO_ERROR_TIMED_OUT);
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, NULL), ==, 0);
    /* bad requests */
    g_assert_null (lrg_mmo_matchmaker_take_roles (queue, "dungeon", dungeon, 0, 100, 2, 0, 700 * SECOND, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null (lrg_mmo_matchmaker_take_roles (queue, "bad mode", dungeon, 3, 100, 2, 0, 700 * SECOND, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (solo (queue, "future", 5, 0, 800 * SECOND, 700 * SECOND, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_roles_save_load (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (8);
    g_autoptr(LrgMmoMatchmaker) restored = lrg_mmo_matchmaker_new (8);
    g_autoptr(LrgMmoMatchmaker) small = lrg_mmo_matchmaker_new (1);
    g_autoptr(GVariant) saved = NULL;
    g_autoptr(GVariant) broken = NULL;
    g_autoptr(GPtrArray) ticket = NULL;
    g_autoptr(GError) error = NULL;
    gint64 since = 0;
    guint roles = 0;

    lrg_mmo_matchmaker_set_timeout (queue, 100 * SECOND);
    lrg_mmo_matchmaker_set_timeout (restored, 100 * SECOND);
    g_assert_true (solo (queue, "old", 5, ROLE (TANK), 0, 0, &error));
    g_assert_true (solo (queue, "new", 7, ROLE (HEALER) | ROLE (DAMAGE), 50 * SECOND, 50 * SECOND, &error));
    saved = lrg_mmo_matchmaker_save (queue, 60 * SECOND);
    /* a new process: its clock starts again */
    g_assert_true (lrg_mmo_matchmaker_load (restored, saved, 1000, &error));
    g_assert_no_error (error);
    ticket = lrg_mmo_matchmaker_get_ticket (restored, "new", NULL, &since, &roles);
    g_assert_nonnull (ticket);
    g_assert_cmpuint (roles, ==, ROLE (HEALER) | ROLE (DAMAGE));
    g_assert_cmpint (since, ==, 1000 - 10 * SECOND);
    /* "old" waited 60 s: the ages survived, so it expires 40 s later */
    g_clear_pointer (&ticket, g_ptr_array_unref);
    ticket = lrg_mmo_matchmaker_expire (restored, 1000 + 40 * SECOND);
    g_assert_cmpuint (ticket->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (ticket, 0), ==, "old");
    /* loading twice, into a small queue, or garbage: refused, nothing changed */
    g_assert_false (lrg_mmo_matchmaker_load (restored, saved, 2000, &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    g_assert_false (lrg_mmo_matchmaker_load (small, saved, 0, &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (small, NULL), ==, 0);
    broken = g_variant_ref_sink (g_variant_new_string ("nonsense"));
    g_assert_false (lrg_mmo_matchmaker_load (small, broken, 0, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_instances (void)
{
    g_autoptr(LrgMmoInstances) table = lrg_mmo_instances_new (2);
    g_autoptr(GPtrArray) members = NULL;
    g_autoptr(GArray) closed = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *party[] = { "alice", "bob", NULL };
    const gchar *raid[] = { "carol", NULL };
    const gchar *again[] = { "alice", NULL };
    guint64 a, b;

    a = lrg_mmo_instances_create (table, "dungeon-1", 3, party, 0, &error);
    g_assert_cmpuint (a, !=, 0);
    g_assert_cmpstr (lrg_mmo_instances_get_content (table, a), ==, "dungeon-1");
    g_assert_cmpuint (lrg_mmo_instances_get_capacity (table, a), ==, 3);
    g_assert_cmpuint (lrg_mmo_instances_find (table, "bob"), ==, a);
    /* one instance per account */
    g_assert_cmpuint (lrg_mmo_instances_create (table, "raid", 10, again, 0, &error), ==, 0);
    clear_error (&error, G_IO_ERROR_EXISTS);
    b = lrg_mmo_instances_create (table, "raid", 10, raid, 0, &error);
    g_assert_cmpuint (b, ==, a + 1);
    /* a full table */
    g_assert_cmpuint (lrg_mmo_instances_create (table, "x", 1, NULL, 0, &error), ==, 0);
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    /* members only; a full instance takes nobody else */
    g_assert_true (lrg_mmo_instances_enter (table, a, "alice", &error));
    g_assert_false (lrg_mmo_instances_enter (table, a, "carol", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_instances_enter (table, 999, "alice", &error));
    clear_error (&error, G_IO_ERROR_NOT_FOUND);
    g_assert_true (lrg_mmo_instances_add_member (table, a, "dave", &error));
    g_assert_false (lrg_mmo_instances_add_member (table, a, "erin", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_false (lrg_mmo_instances_add_member (table, a, "carol", &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    g_assert_cmpuint (lrg_mmo_instances_get_inside (table, a), ==, 1);
    g_assert_true (lrg_mmo_instances_is_inside (table, "alice"));
    /* a seat comes free when a member goes */
    g_assert_cmpuint (lrg_mmo_instances_remove_member (table, "bob", 0), ==, a);
    g_assert_true (lrg_mmo_instances_add_member (table, a, "erin", &error));
    members = lrg_mmo_instances_get_members (table, a);
    g_assert_cmpuint (members->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (members, 2), ==, "erin");
    /* expiry: the raid nobody entered closes after the empty time, the dungeon is in use */
    closed = lrg_mmo_instances_expire (table, 60 * SECOND, G_MAXINT64, 61 * SECOND);
    g_assert_cmpuint (closed->len, ==, 1);
    g_assert_cmpuint (g_array_index (closed, guint64, 0), ==, b);
    g_assert_cmpuint (lrg_mmo_instances_find (table, "carol"), ==, 0);
    g_clear_pointer (&closed, g_array_unref);
    lrg_mmo_instances_leave (table, "alice", 70 * SECOND);
    closed = lrg_mmo_instances_expire (table, 60 * SECOND, G_MAXINT64, 100 * SECOND);
    g_assert_cmpuint (closed->len, ==, 0);
    g_clear_pointer (&closed, g_array_unref);
    closed = lrg_mmo_instances_expire (table, 60 * SECOND, G_MAXINT64, 130 * SECOND);
    g_assert_cmpuint (closed->len, ==, 1);
    g_assert_cmpuint (lrg_mmo_instances_get_count (table), ==, 0);
}

static void
test_instances_save_load (void)
{
    g_autoptr(LrgMmoInstances) table = lrg_mmo_instances_new (4);
    g_autoptr(LrgMmoInstances) restored = lrg_mmo_instances_new (4);
    g_autoptr(LrgMmoInstances) tiny = lrg_mmo_instances_new (1);
    g_autoptr(GVariant) saved = NULL;
    g_autoptr(GVariant) twice = NULL;
    g_autoptr(GArray) list = NULL;
    g_autoptr(GError) error = NULL;
    const gchar *one[] = { "alice", NULL };
    const gchar *two[] = { "bob", "carol", NULL };
    GVariantBuilder builder;
    guint64 a, b;

    a = lrg_mmo_instances_create (table, "delve-3", 5, one, 0, &error);
    b = lrg_mmo_instances_create (table, "raid", 10, two, 10 * SECOND, &error);
    g_assert_true (lrg_mmo_instances_enter (table, b, "bob", &error));
    saved = lrg_mmo_instances_save (table, 20 * SECOND);
    g_assert_true (lrg_mmo_instances_load (restored, saved, 5, &error));
    g_assert_no_error (error);
    list = lrg_mmo_instances_list (restored);
    g_assert_cmpuint (list->len, ==, 2);
    g_assert_cmpuint (lrg_mmo_instances_find (restored, "carol"), ==, b);
    g_assert_false (lrg_mmo_instances_is_inside (restored, "bob"));
    g_assert_cmpstr (lrg_mmo_instances_get_content (restored, a), ==, "delve-3");
    /* the IDs go on where they stopped */
    g_assert_cmpuint (lrg_mmo_instances_create (restored, "x", 1, NULL, 10, &error), ==, b + 1);
    /* refusals change nothing */
    g_assert_false (lrg_mmo_instances_load (restored, saved, 5, &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    g_assert_false (lrg_mmo_instances_load (tiny, saved, 5, &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tsuxas)"));
    g_variant_builder_add (&builder, "(tsux^as)", (guint64) 1, "a", 2u, (gint64) 0, one);
    g_variant_builder_add (&builder, "(tsux^as)", (guint64) 2, "b", 2u, (gint64) 0, one);
    twice = g_variant_ref_sink (g_variant_new ("(ta(tsuxas))", (guint64) 3, &builder));
    g_assert_false (lrg_mmo_instances_load (tiny, twice, 5, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_mmo_instances_get_count (tiny), ==, 0);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/finder/roles-fill", test_roles_fill);
    g_test_add_func ("/mmo/finder/roles-party-spread", test_roles_party_and_spread);
    g_test_add_func ("/mmo/finder/roles-partial-swap-expiry", test_roles_partial_swap_expiry);
    g_test_add_func ("/mmo/finder/roles-save-load", test_roles_save_load);
    g_test_add_func ("/mmo/finder/instances", test_instances);
    g_test_add_func ("/mmo/finder/instances-save-load", test_instances_save_load);
    return g_test_run ();
}
