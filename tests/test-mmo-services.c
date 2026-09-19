/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include "mmo/lrg-mmo-protocol.h"
#include "mmo/lrg-mmo-prediction.h"
#include "mmo/lrg-mmo-gate.h"
#include "mmo/lrg-mmo-matchmaker.h"
#include "mmo/lrg-mmo-auth.h"
#include "mmo/lrg-mmo-shard-directory.h"
#include "mmo/lrg-mmo-market.h"
#include "mmo/lrg-mmo-social.h"
#include "mmo/lrg-mmo-replica.h"
#include "mmo/lrg-mmo-replicator.h"

static void
clear_error (GError **error, gint code)
{
    g_assert_error (*error, G_IO_ERROR, code);
    g_clear_error (error);
}

static GVariant *
record_change (const gchar *key, guint64 revision, const gchar *text)
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&builder, "(st@ay)", key, revision,
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, text, strlen (text), 1));
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

static void
test_auth (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoAuth) auth = lrg_mmo_auth_new (store);
    g_autofree gchar *token = NULL;
    g_autofree gchar *rotated = NULL;
    g_autofree gchar *identity = NULL;
    g_autofree gchar *recovery = NULL;
    g_assert_no_error (error);
    g_assert_false (lrg_mmo_auth_register (auth, "alice", "short", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_auth_register (auth, "alice", "a long password for testing", &error));
    g_assert_null (lrg_mmo_auth_login (auth, "alice", "the wrong password", 1000, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    token = lrg_mmo_auth_login (auth, "alice", "a long password for testing", 1000, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (strlen (token), ==, 64);
    identity = lrg_mmo_auth_verify (auth, token, 1001, &error);
    g_assert_cmpstr (identity, ==, "alice");
    rotated = lrg_mmo_auth_rotate (auth, token, 1002, &error);
    g_assert_nonnull (rotated);
    g_assert_cmpstr (rotated, !=, token);
    g_assert_null (lrg_mmo_auth_verify (auth, token, 1003, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_null (lrg_mmo_auth_verify (auth, rotated, 4602, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    recovery = lrg_mmo_auth_begin_recovery (auth, "alice", 1003, &error);
    g_assert_nonnull (recovery);
    g_assert_null (lrg_mmo_auth_verify (auth, recovery, 1004, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_auth_recover (auth, recovery, "my replacement password", 1004, &error));
    g_assert_false (lrg_mmo_auth_recover (auth, recovery, "another replacement password", 1005, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_null (lrg_mmo_auth_verify (auth, rotated, 1005, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_pointer (&token, g_free);
    token = lrg_mmo_auth_login (auth, "alice", "my replacement password", 1006, &error);
    g_assert_nonnull (token);
    g_assert_true (lrg_mmo_auth_set_banned (auth, "alice", TRUE, &error));
    g_assert_null (lrg_mmo_auth_verify (auth, token, 1007, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_auth_set_banned (auth, "alice", FALSE, &error));
    g_assert_null (lrg_mmo_auth_verify (auth, token, 1008, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_auth_revoke (auth, token, &error));
    g_assert_true (lrg_mmo_auth_revoke (auth, token, &error));
    g_assert_no_error (error);
}

static void
test_receipts_backup (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = g_dir_make_tmp ("lrg-backup-XXXXXX", &error);
    g_autofree gchar *path = g_build_filename (dir, "backup.db", NULL);
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoStore) restored = NULL;
    g_autoptr(GVariant) batch = record_change ("character", 0, "state");
    g_autoptr(GVariant) other = record_change ("character", 1, "other");
    g_autoptr(GVariant) audit = NULL;
    g_autoptr(GBytes) bytes = NULL;
    guint64 revision;
    gboolean duplicate;
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_store_commit_once (store, "save-1", batch, &duplicate, &error));
    g_assert_false (duplicate);
    g_assert_true (lrg_mmo_store_commit_once (store, "save-1", batch, &duplicate, &error));
    g_assert_true (duplicate);
    g_assert_false (lrg_mmo_store_commit_once (store, "save-1", other, &duplicate, &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    audit = lrg_mmo_store_read_audit (store, 0, 10, &error);
    g_assert_cmpuint (g_variant_n_children (audit), ==, 1);
    g_assert_true (lrg_mmo_store_backup (store, path, &error));
    g_assert_false (lrg_mmo_store_backup (store, path, &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    restored = lrg_mmo_store_new (path, &error);
    bytes = lrg_mmo_store_read (restored, "character", &revision, &error);
    g_assert_cmpuint (revision, ==, 1);
    g_assert_cmpmem (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), "state", 5);
    g_assert_true (lrg_mmo_store_commit_once (restored, "save-1", batch, &duplicate, &error));
    g_assert_true (duplicate);
    g_assert_no_error (error);
    g_clear_object (&restored);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

static void
test_fencing (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = g_dir_make_tmp ("lrg-lease-XXXXXX", &error);
    g_autofree gchar *path = g_build_filename (dir, "leases.db", NULL);
    g_autoptr(LrgMmoStore) store_a = lrg_mmo_store_new (path, &error);
    g_autoptr(LrgMmoStore) store_b = lrg_mmo_store_new (path, &error);
    g_autoptr(LrgMmoShardDirectory) a = lrg_mmo_shard_directory_new (store_a, &error);
    g_autoptr(LrgMmoShardDirectory) b = lrg_mmo_shard_directory_new (store_b, &error);
    g_autoptr(GVariant) first = record_change ("world", 0, "first");
    g_autoptr(GVariant) second = record_change ("world", 1, "second");
    g_autoptr(GVariant) lease = NULL;
    g_autoptr(GBytes) state = g_bytes_new_static ("handoff", 7);
    guint64 fence, next;
    g_assert_no_error (error);
    fence = lrg_mmo_shard_directory_acquire (a, "town", "worker-a", "tls://a", 60, &error);
    g_assert_cmpuint (fence, ==, 1);
    g_assert_cmpuint (lrg_mmo_shard_directory_acquire (b, "town", "worker-b", "tls://b", 60, &error), ==, 0);
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_store_commit_fenced (store_a, "town", "worker-a", fence, first, &error));
    next = lrg_mmo_shard_directory_handoff (a, "town", "worker-a", fence, "worker-b", "tls://b", state, 60, &error);
    g_assert_cmpuint (next, >, fence);
    g_assert_false (lrg_mmo_store_commit_fenced (store_a, "town", "worker-a", fence, second, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_shard_directory_renew (a, "town", "worker-a", fence, 60, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_store_commit_fenced (store_b, "town", "worker-b", next, second, &error));
    lease = lrg_mmo_shard_directory_lookup (b, "town", &error);
    g_assert_nonnull (lease);
    g_assert_true (lrg_mmo_shard_directory_release (b, "town", "worker-b", next, &error));
    g_assert_null (lrg_mmo_shard_directory_lookup (a, "town", &error));
    clear_error (&error, G_IO_ERROR_NOT_FOUND);
    fence = lrg_mmo_shard_directory_acquire (a, "town", "worker-c", "tls://c", 60, &error);
    g_assert_cmpuint (fence, >, next);
    g_assert_no_error (error);
    g_clear_object (&a);
    g_clear_object (&b);
    g_clear_object (&store_a);
    g_clear_object (&store_b);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

static void
test_market (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_assert_true (lrg_mmo_market_grant (market, "alice", "sword", 10, "grant-alice", &error));
    g_assert_true (lrg_mmo_market_grant (market, "alice", "sword", 10, "grant-alice", &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "sword", &error), ==, 10);
    g_assert_false (lrg_mmo_market_grant (market, "alice", "sword", 20, "grant-alice", &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_true (lrg_mmo_market_list (market, "alice", "auction", "sword", 3, 50, 60, &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "sword", &error), ==, 7);
    g_assert_false (lrg_mmo_market_buy (market, "bob", "auction", "buy-1", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_true (lrg_mmo_market_grant (market, "bob", "coins", 100, "fund-bob", &error));
    g_assert_true (lrg_mmo_market_buy (market, "bob", "auction", "buy-1", &error));
    g_assert_true (lrg_mmo_market_buy (market, "bob", "auction", "buy-1", &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "bob", "coins", &error), ==, 50);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "coins", &error), ==, 50);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "bob", "sword", &error), ==, 3);
    g_assert_false (lrg_mmo_market_cancel (market, "alice", "auction", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_market_list (market, "alice", "auction2", "sword", 2, 50, 60, &error));
    g_assert_false (lrg_mmo_market_cancel (market, "bob", "auction2", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_market_cancel (market, "alice", "auction2", &error));
    g_assert_true (lrg_mmo_market_transfer (market, "bob", "alice", "sword", 1, "trade-1", &error));
    g_assert_true (lrg_mmo_market_transfer (market, "bob", "alice", "sword", 1, "trade-1", &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "sword", &error), ==, 8);
    g_assert_true (lrg_mmo_market_grant (market, "rich", "coins", G_MAXINT64, "rich-grant", &error));
    g_assert_false (lrg_mmo_market_transfer (market, "bob", "rich", "coins", 1, "overflow", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "bob", "coins", &error), ==, 50);
    g_assert_no_error (error);
}

static void
test_social (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoSocial) social = lrg_mmo_social_new (store);
    g_autoptr(GVariant) messages = NULL;
    g_autoptr(GVariant) guild = NULL;
    const gchar *leader;
    g_assert_true (lrg_mmo_social_create_guild (social, "alice", "heroes", &error));
    g_assert_false (lrg_mmo_social_join (social, "bob", "heroes", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_social_invite (social, "alice", "heroes", "bob", &error));
    g_assert_true (lrg_mmo_social_join (social, "bob", "heroes", &error));
    g_assert_false (lrg_mmo_social_invite (social, "bob", "heroes", "carol", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_social_leave (social, "alice", "heroes", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_social_set_role (social, "alice", "heroes", "bob", 3, &error));
    guild = lrg_mmo_social_get_guild (social, "heroes", &error);
    g_variant_get_child (guild, 0, "&s", &leader);
    g_assert_cmpstr (leader, ==, "bob");
    g_assert_true (lrg_mmo_social_leave (social, "alice", "heroes", &error));
    g_assert_true (lrg_mmo_social_leave (social, "bob", "heroes", &error));
    g_assert_true (lrg_mmo_social_send_message (social, "alice", "bob", "hello", "message-1", &error));
    g_assert_true (lrg_mmo_social_send_message (social, "alice", "bob", "hello", "message-1", &error));
    messages = lrg_mmo_social_read_inbox (social, "bob", &error);
    g_assert_cmpuint (g_variant_n_children (messages), ==, 1);
    g_assert_true (lrg_mmo_social_block (social, "bob", "alice", TRUE, &error));
    g_assert_false (lrg_mmo_social_send_message (social, "alice", "bob", "blocked", "message-2", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_social_block (social, "bob", "alice", FALSE, &error));
    g_assert_true (lrg_mmo_social_send_message (social, "alice", "bob", "unblocked", "message-2", &error));
    g_assert_no_error (error);
}

static void
test_replica (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoReplicator) source = lrg_mmo_replicator_new (10, 1, 10);
    g_autoptr(LrgMmoReplica) replica = lrg_mmo_replica_new (10);
    g_autoptr(GBytes) state = g_bytes_new_static ("x", 1);
    g_autoptr(GVariant) delta = NULL;
    g_autoptr(GVariant) position = NULL;
    gdouble x, y, z;
    lrg_mmo_replica_reset (replica, "stream-1");
    g_assert_true (lrg_mmo_replicator_upsert (source, 1, "town", 0, 0, 0, state, &error));
    delta = lrg_mmo_replicator_build (source, 1, "town", 0, 0, 0, 20, &error);
    g_assert_false (lrg_mmo_replica_apply (replica, "stream-2", delta, &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_true (lrg_mmo_replica_apply (replica, "stream-1", delta, &error));
    g_assert_true (lrg_mmo_replica_apply (replica, "stream-1", delta, &error));
    g_assert_true (lrg_mmo_replicator_acknowledge (source, 1, lrg_mmo_replica_get_sequence (replica), &error));
    g_clear_pointer (&delta, g_variant_unref);
    g_assert_true (lrg_mmo_replicator_upsert (source, 1, "town", 10, 0, 0, state, &error));
    delta = lrg_mmo_replicator_build (source, 1, "town", 0, 0, 0, 20, &error);
    g_assert_true (lrg_mmo_replica_apply (replica, "stream-1", delta, &error));
    position = lrg_mmo_replica_interpolate (replica, 1, 0.5);
    g_variant_get (position, "(ddd)", &x, &y, &z);
    g_assert_cmpfloat (x, ==, 5);
    g_assert_cmpfloat (y, ==, 0);
    g_assert_cmpfloat (z, ==, 0);
    g_assert_true (lrg_mmo_replicator_acknowledge (source, 1, lrg_mmo_replica_get_sequence (replica), &error));
    g_clear_pointer (&delta, g_variant_unref);
    lrg_mmo_replicator_remove (source, 1);
    delta = lrg_mmo_replicator_build (source, 1, "town", 0, 0, 0, 20, &error);
    g_assert_true (lrg_mmo_replica_apply (replica, "stream-1", delta, &error));
    g_assert_null (lrg_mmo_replica_lookup (replica, 1));
    g_assert_no_error (error);
}

static void
test_pages (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoReplicator) rep = lrg_mmo_replicator_new (100, 1, 10);
    g_autoptr(LrgMmoReplica) client = lrg_mmo_replica_new (100);
    g_autoptr(GBytes) state = g_bytes_new_static ("x", 1);
    gboolean more = TRUE;
    guint pages = 0, i;
    lrg_mmo_replica_reset (client, "epoch");
    for (i = 1; i <= 25; i++)
        g_assert_true (lrg_mmo_replicator_upsert (rep, i, "town", 0, 0, 0, state, &error));
    while (more && pages < 100)
    {
        g_autoptr(GVariant) page = lrg_mmo_replicator_build_page (rep, 1, "town", 0, 0, 0, 20, 256, &more, &error);
        g_autoptr(GVariant) retry = NULL;
        gboolean retry_more;
        g_assert_no_error (error);
        g_assert_cmpuint (g_variant_get_size (page), <=, 256);
        retry = lrg_mmo_replicator_build_page (rep, 1, "town", 0, 0, 0, 20, 256, &retry_more, &error);
        g_assert_true (g_variant_equal (page, retry));
        g_assert_cmpint (more, ==, retry_more);
        g_assert_true (lrg_mmo_replica_apply (client, "epoch", page, &error));
        g_assert_true (lrg_mmo_replicator_acknowledge (rep, 1, lrg_mmo_replica_get_sequence (client), &error));
        pages++;
    }
    g_assert_false (more);
    g_assert_cmpuint (pages, >, 1);
    for (i = 1; i <= 25; i++)
    {
        g_autoptr(GVariant) entity = lrg_mmo_replica_lookup (client, i);
        g_assert_nonnull (entity);
    }
    g_assert_no_error (error);
}

static void
test_protocol (void)
{
    g_autofree guint8 *data = g_malloc0 (10000);
    g_autoptr(GBytes) payload = g_bytes_new (data, 10000);
    g_autoptr(GBytes) frame = NULL;
    g_autoptr(GBytes) decoded = NULL;
    g_autoptr(GBytes) malformed = NULL;
    g_autofree guint8 *copy = NULL;
    g_autoptr(GError) error = NULL;
    gsize length;
    guint opcode;
    guint64 sequence;
    frame = lrg_mmo_protocol_encode (42, G_MAXUINT64, payload, TRUE, &error);
    g_assert_cmpuint (g_bytes_get_size (frame), <, 10000);
    decoded = lrg_mmo_protocol_decode (frame, &opcode, &sequence, &error);
    g_assert_cmpuint (opcode, ==, 42);
    g_assert_cmpuint (sequence, ==, G_MAXUINT64);
    g_assert_true (g_bytes_equal (payload, decoded));
    length = g_bytes_get_size (frame);
    copy = g_memdup2 (g_bytes_get_data (frame, NULL), length);
    copy[4] = 2;
    malformed = g_bytes_new (copy, length);
    g_assert_null (lrg_mmo_protocol_decode (malformed, &opcode, &sequence, &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    g_clear_pointer (&malformed, g_bytes_unref);
    copy[4] = 1;
    memset (copy + 16, 255, 4);
    malformed = g_bytes_new (copy, length);
    g_assert_null (lrg_mmo_protocol_decode (malformed, &opcode, &sequence, &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
}

static void
test_prediction (void)
{
    g_autoptr(LrgMmoPrediction) prediction = lrg_mmo_prediction_new (2);
    g_autoptr(GVariant) position = NULL;
    g_autoptr(GError) error = NULL;
    gdouble x, y, z;
    g_assert_true (lrg_mmo_prediction_push (prediction, 1, 2, 0, 0, &error));
    g_assert_true (lrg_mmo_prediction_push (prediction, 2, 3, 0, 0, &error));
    g_assert_false (lrg_mmo_prediction_push (prediction, 3, 4, 0, 0, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_prediction_reconcile (prediction, 1, 1, 0, 0, &error));
    position = lrg_mmo_prediction_get_position (prediction);
    g_variant_get (position, "(ddd)", &x, &y, &z);
    g_assert_cmpfloat (x, ==, 4);
    g_assert_false (lrg_mmo_prediction_reconcile (prediction, 3, 10, 0, 0, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (lrg_mmo_prediction_push (prediction, 3, NAN, 0, 0, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_prediction_push (prediction, 3, 4, 0, 0, &error));
    g_assert_no_error (error);
}

static void
test_gate (void)
{
    g_autoptr(LrgMmoGate) gate = lrg_mmo_gate_new (1, 2, 1);
    g_autoptr(GError) error = NULL;
    g_assert_true (lrg_mmo_gate_admit (gate, "ip-a", 0, &error));
    g_assert_true (lrg_mmo_gate_admit (gate, "ip-a", 0, &error));
    g_assert_false (lrg_mmo_gate_admit (gate, "ip-a", 0, &error));
    clear_error (&error, G_IO_ERROR_WOULD_BLOCK);
    g_assert_false (lrg_mmo_gate_admit (gate, "ip-b", 0, &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_true (lrg_mmo_gate_admit (gate, "ip-a", G_TIME_SPAN_SECOND, &error));
    g_assert_false (lrg_mmo_gate_admit (gate, "ip-a", 0, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_gate_admit (gate, "ip-b", 61 * G_TIME_SPAN_SECOND, &error));
    g_assert_no_error (error);
}

static void
test_matchmaker (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (3);
    g_autoptr(GPtrArray) group = NULL;
    g_autoptr(GError) error = NULL;
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "a", "arena", 100, 0, &error));
    g_assert_false (lrg_mmo_matchmaker_enqueue (queue, "a", "arena", 100, 0, &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "b", "arena", 110, 0, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "c", "arena", 500, 0, &error));
    group = lrg_mmo_matchmaker_take (queue, "arena", 3, 20, 0, &error);
    g_assert_cmpuint (group->len, ==, 0);
    g_clear_pointer (&group, g_ptr_array_unref);
    group = lrg_mmo_matchmaker_take (queue, "arena", 2, 20, 0, &error);
    g_assert_cmpuint (group->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (group, 0), ==, "a");
    g_clear_pointer (&group, g_ptr_array_unref);
    group = lrg_mmo_matchmaker_take (queue, "arena", 2, G_MAXUINT, 60 * G_TIME_SPAN_SECOND, &error);
    g_assert_cmpuint (group->len, ==, 0);
    g_assert_no_error (error);
}

typedef struct
{
    const gchar *path;
    const gchar *buyer;
    GMutex *mutex;
    GCond *condition;
    guint *ready;
    gboolean success;
    GError *error;
} Purchase;

static gpointer
purchase_thread (gpointer data)
{
    Purchase *purchase = data;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (purchase->path, &purchase->error);
    g_autoptr(LrgMmoMarket) market = store != NULL ? lrg_mmo_market_new (store) : NULL;
    g_mutex_lock (purchase->mutex);
    (*purchase->ready)++;
    g_cond_broadcast (purchase->condition);
    while (*purchase->ready < 2)
        g_cond_wait (purchase->condition, purchase->mutex);
    g_mutex_unlock (purchase->mutex);
    if (market != NULL)
        purchase->success = lrg_mmo_market_buy (market, purchase->buyer, "only-one",
                                                purchase->buyer, &purchase->error);
    return NULL;
}

static void
test_concurrent_purchase (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = g_dir_make_tmp ("lrg-race-XXXXXX", &error);
    g_autofree gchar *path = g_build_filename (dir, "market.db", NULL);
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (path, &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    GMutex mutex;
    GCond condition;
    guint ready = 0;
    Purchase a = { path, "alice", &mutex, &condition, &ready, FALSE, NULL };
    Purchase b = { path, "bob", &mutex, &condition, &ready, FALSE, NULL };
    GThread *first, *second;
    g_mutex_init (&mutex);
    g_cond_init (&condition);
    g_assert_true (lrg_mmo_market_grant (market, "seller", "sword", 1, "seed-item", &error));
    g_assert_true (lrg_mmo_market_grant (market, "alice", "coins", 10, "seed-a", &error));
    g_assert_true (lrg_mmo_market_grant (market, "bob", "coins", 10, "seed-b", &error));
    g_assert_true (lrg_mmo_market_list (market, "seller", "only-one", "sword", 1, 10, 60, &error));
    g_assert_no_error (error);
    first = g_thread_new ("buyer-a", purchase_thread, &a);
    second = g_thread_new ("buyer-b", purchase_thread, &b);
    g_thread_join (first);
    g_thread_join (second);
    g_assert_cmpuint (a.success + b.success, ==, 1);
    g_assert_nonnull (a.success ? b.error : a.error);
    g_clear_error (&a.error);
    g_clear_error (&b.error);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "seller", "coins", &error), ==, 10);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "coins", &error) +
                     lrg_mmo_market_get_balance (market, "bob", "coins", &error), ==, 10);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "sword", &error) +
                     lrg_mmo_market_get_balance (market, "bob", "sword", &error), ==, 1);
    g_assert_no_error (error);
    g_clear_object (&market);
    g_clear_object (&store);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
    g_cond_clear (&condition);
    g_mutex_clear (&mutex);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/services/auth", test_auth);
    g_test_add_func ("/mmo/services/receipts-backup", test_receipts_backup);
    g_test_add_func ("/mmo/services/fencing", test_fencing);
    g_test_add_func ("/mmo/services/market", test_market);
    g_test_add_func ("/mmo/services/social", test_social);
    g_test_add_func ("/mmo/services/replica", test_replica);
    g_test_add_func ("/mmo/services/pages", test_pages);
    g_test_add_func ("/mmo/services/protocol", test_protocol);
    g_test_add_func ("/mmo/services/prediction", test_prediction);
    g_test_add_func ("/mmo/services/gate", test_gate);
    g_test_add_func ("/mmo/services/matchmaker", test_matchmaker);
    g_test_add_func ("/mmo/services/concurrent-purchase", test_concurrent_purchase);
    return g_test_run ();
}
