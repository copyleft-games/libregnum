/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include "mmo/lrg-mmo-realm.h"
#include "mmo/lrg-mmo-replicator.h"
#include "mmo/lrg-mmo-group.h"
#include "mmo/lrg-mmo-store.h"

static void
assert_error (GError **error,
              gint     code)
{
    g_assert_error (*error, G_IO_ERROR, code);
    g_clear_error (error);
}

static void
on_ended (LrgMmoRealm *realm,
          guint32      peer,
          guint       *count)
{
    g_assert_null (lrg_mmo_realm_get_account (realm, peer));
    (*count)++;
}

static void
test_realm_admission (void)
{
    g_autoptr(LrgMmoRealm) realm = lrg_mmo_realm_new (2, 1000000);
    g_autoptr(GError) error = NULL;
    guint ended = 0;
    g_signal_connect (realm, "session-ended", G_CALLBACK (on_ended), &ended);
    g_assert_false (lrg_mmo_realm_login (realm, 0, "alice", 0, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_realm_login (realm, 1, "alice", 0, &error));
    g_assert_false (lrg_mmo_realm_login (realm, 2, "alice", 0, &error));
    assert_error (&error, G_IO_ERROR_EXISTS);
    g_assert_true (lrg_mmo_realm_login (realm, 2, "bob", 0, &error));
    g_assert_false (lrg_mmo_realm_login (realm, 3, "carol", 0, &error));
    assert_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpuint (lrg_mmo_realm_expire (realm, 999999), ==, 0);
    g_assert_cmpuint (lrg_mmo_realm_expire (realm, 1000000), ==, 2);
    g_assert_cmpuint (ended, ==, 2);
    g_assert_cmpuint (lrg_mmo_realm_get_session_count (realm), ==, 0);
    g_assert_true (lrg_mmo_realm_login (realm, 3, "alice", 1000000, &error));
    g_assert_no_error (error);
}

static void
test_realm_zones (void)
{
    g_autoptr(LrgMmoRealm) realm = lrg_mmo_realm_new (2, 1000000);
    g_autoptr(GError) error = NULL;
    g_assert_true (lrg_mmo_realm_add_zone (realm, "a", 1, &error));
    g_assert_true (lrg_mmo_realm_add_zone (realm, "b", 1, &error));
    g_assert_false (lrg_mmo_realm_add_zone (realm, "a", 10, &error));
    assert_error (&error, G_IO_ERROR_EXISTS);
    g_assert_true (lrg_mmo_realm_login (realm, 1, "alice", 0, &error));
    g_assert_true (lrg_mmo_realm_login (realm, 2, "bob", 0, &error));
    g_assert_true (lrg_mmo_realm_enter_zone (realm, 1, "a", &error));
    g_assert_true (lrg_mmo_realm_enter_zone (realm, 2, "b", &error));
    g_assert_false (lrg_mmo_realm_enter_zone (realm, 1, "b", &error));
    assert_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpstr (lrg_mmo_realm_get_zone (realm, 1), ==, "a");
    g_assert_true (lrg_mmo_realm_enter_zone (realm, 1, "a", &error));
    lrg_mmo_realm_logout (realm, 2);
    g_assert_true (lrg_mmo_realm_enter_zone (realm, 1, "b", &error));
    g_assert_true (lrg_mmo_realm_login (realm, 2, "bob", 0, &error));
    g_assert_true (lrg_mmo_realm_enter_zone (realm, 2, "a", &error));
    g_assert_no_error (error);
}

static void
test_realm_commands (void)
{
    g_autoptr(LrgMmoRealm) realm = lrg_mmo_realm_new (1, 1000000);
    g_autoptr(GError) error = NULL;
    guint i;
    g_assert_false (lrg_mmo_realm_accept_command (realm, 1, 1, 0, &error));
    assert_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_realm_login (realm, 1, "alice", 0, &error));
    for (i = 1; i <= 100; i++)
        g_assert_true (lrg_mmo_realm_accept_command (realm, 1, i, 0, &error));
    g_assert_false (lrg_mmo_realm_accept_command (realm, 1, 101, 0, &error));
    assert_error (&error, G_IO_ERROR_WOULD_BLOCK);
    g_assert_true (lrg_mmo_realm_accept_command (realm, 1, 101, 20000, &error));
    g_assert_false (lrg_mmo_realm_accept_command (realm, 1, 101, 40000, &error));
    assert_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_false (lrg_mmo_realm_accept_command (realm, 1, 102, 10000, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (lrg_mmo_realm_accept_command (realm, 1, 102, 1020000, &error));
    assert_error (&error, G_IO_ERROR_TIMED_OUT);
    g_assert_cmpuint (lrg_mmo_realm_expire (realm, 1020000), ==, 1);
}

static void
on_tick (LrgMmoRealm *realm,
         guint64      number,
         gdouble      step,
         guint       *count)
{
    (*count)++;
    g_assert_cmpuint (number, ==, *count);
    g_assert_cmpfloat (step, ==, 0.05);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, 1), ==, 0);
}

static void
test_realm_ticks (void)
{
    g_autoptr(LrgMmoRealm) realm = lrg_mmo_realm_new (1, 1);
    guint count = 0;
    g_signal_connect (realm, "tick", G_CALLBACK (on_tick), &count);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, 0.02), ==, 0);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, 0.03), ==, 1);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, 1e300), ==, 8);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, NAN), ==, 0);
    g_assert_cmpuint (lrg_mmo_realm_advance (realm, -1), ==, 0);
    g_assert_cmpuint (count, ==, 9);
}

static guint64
delta_sequence (GVariant *delta)
{
    guint64 sequence;
    g_variant_get_child (delta, 0, "t", &sequence);
    return sequence;
}

static void
assert_delta (GVariant *delta,
              guint     updates,
              guint     removals)
{
    g_autoptr(GVariant) u = NULL;
    g_autoptr(GVariant) r = NULL;
    g_assert_nonnull (delta);
    g_assert_true (g_variant_is_of_type (delta, G_VARIANT_TYPE ("(ta(ttddday)at)")));
    u = g_variant_get_child_value (delta, 1);
    r = g_variant_get_child_value (delta, 2);
    g_assert_cmpuint (g_variant_n_children (u), ==, updates);
    g_assert_cmpuint (g_variant_n_children (r), ==, removals);
}

static void
test_replication_ack (void)
{
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (10, 2, 10);
    g_autoptr(GBytes) state = g_bytes_new_static ("hello", 5);
    g_autoptr(GVariant) first = NULL;
    g_autoptr(GVariant) retry = NULL;
    g_autoptr(GVariant) next = NULL;
    g_autoptr(GError) error = NULL;
    g_assert_true (lrg_mmo_replicator_upsert (rep, 1, "a", -1, 0, 0, state, &error));
    g_assert_true (lrg_mmo_replicator_upsert (rep, 2, "b", -1, 0, 0, state, &error));
    g_assert_true (lrg_mmo_replicator_upsert (rep, 3, "a", 50, 0, 0, state, &error));
    first = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (first, 1, 0);
    lrg_mmo_replicator_remove (rep, 1);
    retry = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    g_assert_true (g_variant_equal (first, retry));
    g_assert_false (lrg_mmo_replicator_acknowledge (rep, 1, 999, &error));
    assert_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, delta_sequence (first), &error));
    next = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (next, 0, 1);
    g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, delta_sequence (next), &error));
    g_clear_pointer (&next, g_variant_unref);
    g_assert_true (lrg_mmo_replicator_upsert (rep, 1, "a", 0, 0, 0, state, &error));
    next = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (next, 1, 0);
    g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, delta_sequence (next), &error));
    g_clear_pointer (&next, g_variant_unref);
    next = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (next, 0, 0);
    g_assert_no_error (error);
}

static void
test_replication_motion (void)
{
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (2, 1, 10);
    g_autoptr(GBytes) state = g_bytes_new_static ("x", 1);
    g_autoptr(GVariant) delta = NULL;
    g_autoptr(GError) error = NULL;
    guint64 previous;
    g_assert_true (lrg_mmo_replicator_upsert (rep, G_MAXUINT64, "a", -10, 0, 0, state, &error));
    delta = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (delta, 1, 0);
    g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, delta_sequence (delta), &error));
    g_clear_pointer (&delta, g_variant_unref);
    g_assert_true (lrg_mmo_replicator_upsert (rep, G_MAXUINT64, "a", 11, 0, 0, state, &error));
    delta = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (delta, 0, 1);
    previous = delta_sequence (delta);
    lrg_mmo_replicator_forget (rep, 1);
    g_clear_pointer (&delta, g_variant_unref);
    delta = lrg_mmo_replicator_build (rep, 1, "a", 11, 0, 0, 0, &error);
    assert_delta (delta, 1, 0);
    g_assert_cmpuint (delta_sequence (delta), >, previous);
    g_assert_null (lrg_mmo_replicator_build (rep, 2, "a", 0, 0, 0, 10, &error));
    assert_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_false (lrg_mmo_replicator_upsert (rep, 2, "a", NAN, 0, 0, state, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null (lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 41, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_replication_budget (void)
{
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (20, 1, 10);
    g_autofree gchar *data = g_malloc0 (65536);
    g_autoptr(GBytes) state = g_bytes_new (data, 65536);
    g_autoptr(GVariant) delta = NULL;
    g_autoptr(GError) error = NULL;
    guint i;
    for (i = 1; i <= 16; i++)
        g_assert_true (lrg_mmo_replicator_upsert (rep, i, "a", 0, 0, 0, state, &error));
    g_assert_null (lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error));
    assert_error (&error, G_IO_ERROR_MESSAGE_TOO_LARGE);
    lrg_mmo_replicator_remove (rep, 16);
    delta = lrg_mmo_replicator_build (rep, 1, "a", 0, 0, 0, 10, &error);
    assert_delta (delta, 15, 0);
    g_assert_no_error (error);
}

static void
test_group (void)
{
    g_autoptr(LrgMmoGroup) group = lrg_mmo_group_new (100, 3);
    g_autoptr(GError) error = NULL;
    g_autoptr(GArray) members = NULL;
    g_assert_false (lrg_mmo_group_add (group, 2, 3, &error));
    assert_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_group_add (group, 100, 2, &error));
    g_assert_true (lrg_mmo_group_add (group, 100, 3, &error));
    g_assert_false (lrg_mmo_group_add (group, 100, 4, &error));
    assert_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_false (lrg_mmo_group_remove (group, 3, 2, &error));
    assert_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    members = lrg_mmo_group_get_members (group);
    g_assert_cmpuint (g_array_index (members, guint64, 0), ==, 2);
    g_assert_true (lrg_mmo_group_remove (group, 100, 100, &error));
    g_assert_cmpuint (lrg_mmo_group_get_leader (group), ==, 2);
    g_assert_true (lrg_mmo_group_remove (group, 2, 3, &error));
    g_assert_true (lrg_mmo_group_remove (group, 2, 2, &error));
    g_assert_cmpuint (lrg_mmo_group_get_leader (group), ==, 0);
    g_assert_cmpuint (members->len, ==, 3);
    g_assert_no_error (error);
}

static GVariant *
changes (const gchar *key,
         guint64      revision,
         const gchar *data,
         gboolean     second)
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&builder, "(st@ay)", key, revision,
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, data, strlen (data), 1));
    if (second)
        g_variant_builder_add (&builder, "(st@ay)", "second", (guint64) 0,
                               g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, "", 0, 1));
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static void
test_store_transaction (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(GVariant) batch = changes ("first", 0, "one", TRUE);
    g_autoptr(GBytes) bytes = NULL;
    guint64 revision;
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_store_commit (store, batch, &error));
    g_clear_pointer (&batch, g_variant_unref);
    batch = changes ("first", 1, "two", TRUE);
    g_assert_false (lrg_mmo_store_commit (store, batch, &error));
    assert_error (&error, G_IO_ERROR_WRONG_ETAG);
    bytes = lrg_mmo_store_read (store, "first", &revision, &error);
    g_assert_cmpuint (revision, ==, 1);
    g_assert_cmpmem (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), "one", 3);
    g_clear_pointer (&bytes, g_bytes_unref);
    bytes = lrg_mmo_store_read (store, "second", &revision, &error);
    g_assert_cmpuint (g_bytes_get_size (bytes), ==, 0);
    g_clear_pointer (&batch, g_variant_unref);
    batch = changes ("first", 1, "two", FALSE);
    g_assert_true (lrg_mmo_store_commit (store, batch, &error));
    g_assert_false (lrg_mmo_store_commit (store, batch, &error));
    assert_error (&error, G_IO_ERROR_WRONG_ETAG);
    g_assert_null (lrg_mmo_store_read (store, "missing", &revision, &error));
    assert_error (&error, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (revision, ==, 0);
}

static void
test_store_persistence (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = g_dir_make_tmp ("lrg-mmo-XXXXXX", &error);
    g_autofree gchar *path = g_build_filename (dir, "world.db", NULL);
    g_autoptr(LrgMmoStore) a = NULL;
    g_autoptr(LrgMmoStore) b = NULL;
    g_autoptr(GVariant) batch = changes ("character/alice", 0, "state", FALSE);
    g_autoptr(GBytes) bytes = NULL;
    guint64 revision;
    g_assert_no_error (error);
    a = lrg_mmo_store_new (path, &error);
    b = lrg_mmo_store_new (path, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_store_commit (a, batch, &error));
    g_assert_false (lrg_mmo_store_commit (b, batch, &error));
    assert_error (&error, G_IO_ERROR_WRONG_ETAG);
    g_clear_object (&a);
    g_clear_object (&b);
    a = lrg_mmo_store_new (path, &error);
    bytes = lrg_mmo_store_read (a, "character/alice", &revision, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (revision, ==, 1);
    g_assert_cmpuint (g_bytes_get_size (bytes), ==, 5);
    g_clear_object (&a);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

static void
test_store_validation (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(GVariant) batch = changes ("second", 0, "x", TRUE);
    g_assert_false (lrg_mmo_store_commit (store, batch, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_pointer (&batch, g_variant_unref);
    batch = changes ("key", G_MAXUINT64, "x", FALSE);
    g_assert_false (lrg_mmo_store_commit (store, batch, &error));
    assert_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/realm/admission", test_realm_admission);
    g_test_add_func ("/mmo/realm/zones", test_realm_zones);
    g_test_add_func ("/mmo/realm/commands", test_realm_commands);
    g_test_add_func ("/mmo/realm/ticks", test_realm_ticks);
    g_test_add_func ("/mmo/replication/ack", test_replication_ack);
    g_test_add_func ("/mmo/replication/motion", test_replication_motion);
    g_test_add_func ("/mmo/replication/budget", test_replication_budget);
    g_test_add_func ("/mmo/group/membership", test_group);
    g_test_add_func ("/mmo/store/transaction", test_store_transaction);
    g_test_add_func ("/mmo/store/persistence", test_store_persistence);
    g_test_add_func ("/mmo/store/validation", test_store_validation);
    return g_test_run ();
}
