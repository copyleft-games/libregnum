/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <math.h>

static void
simulation (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoSimulation) sim = lrg_mmo_simulation_new (8);
    g_autoptr(LrgMmoSimulation) restored = lrg_mmo_simulation_new (8);
    g_autoptr(GVariant) snapshot = NULL;
    g_autoptr(GVariant) state = NULL;
    gdouble x, y, z;
    guint health;
    guint64 sequence;
    g_assert_true (lrg_mmo_simulation_spawn (sim, 1, "alice", "world", 0, 0, 0, .1, 100, 1, 20, 40, 10, 3, &error));
    g_assert_true (lrg_mmo_simulation_spawn (sim, 2, "bob", "world", 4, 0, 0, .1, 100, 2, 20, 40, 10, 3, &error));
    g_assert_true (lrg_mmo_simulation_advance (sim, 3, &error));
    g_assert_false (lrg_mmo_simulation_move (sim, "bob", 1, 1, 1, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_simulation_move (sim, "alice", 1, 1, 4, 0, 0, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_simulation_move (sim, "alice", 1, 1, 1, 0, 0, &error));
    g_assert_false (lrg_mmo_simulation_move (sim, "alice", 1, 2, 2, 0, 0, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_simulation_attack (sim, "alice", 1, 2, 2, &error));
    g_assert_false (lrg_mmo_simulation_attack (sim, "alice", 1, 3, 2, &error));
    g_clear_error (&error);
    snapshot = lrg_mmo_simulation_snapshot (sim);
    g_assert_true (lrg_mmo_simulation_restore (restored, snapshot, &error));
    g_assert_true (lrg_mmo_simulation_advance (restored, 1, &error));
    g_assert_false (lrg_mmo_simulation_attack (restored, "alice", 1, 3, 2, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_simulation_add_obstacle (sim, "world", 2, -1, -1, 3, 1, 1, &error));
    g_assert_true (lrg_mmo_simulation_advance (sim, 20, &error));
    g_assert_false (lrg_mmo_simulation_move (sim, "alice", 1, 3, 4, 0, 0, &error));
    g_clear_error (&error);
    g_assert_false (lrg_mmo_simulation_attack (sim, "alice", 1, 3, 2, &error));
    g_clear_error (&error);
    g_assert_false (lrg_mmo_simulation_move (sim, "alice", 1, 3, NAN, 0, 0, &error));
    g_clear_error (&error);
    state = lrg_mmo_simulation_lookup (sim, 2);
    g_variant_get (state, "(dddut)", &x, &y, &z, &health, &sequence);
    g_assert_cmpuint (health, ==, 60);
    g_assert_no_error (error);
}

static void
postgres (void)
{
    const gchar *connection = g_getenv ("LRG_TEST_POSTGRES");
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) a = NULL, b = NULL;
    g_autoptr(LrgMmoMarket) market_a = NULL, market_b = NULL;
    g_autoptr(LrgMmoShardDirectory) directory_a = NULL, directory_b = NULL;
    g_autoptr(GVariant) changes = NULL, altered = NULL, lookup = NULL, audit = NULL;
    g_autoptr(GBytes) state = g_bytes_new_static ("handoff", 7);
    g_autofree gchar *id = g_uuid_string_random ();
    GVariantBuilder builder;
    guint64 fence, next;
    gboolean duplicate;
    if (connection == NULL)
    {
        g_test_skip ("Set LRG_TEST_POSTGRES to an isolated test database");
        return;
    }
    a = lrg_mmo_store_new_postgres (connection, &error);
    g_assert_no_error (error);
    g_assert_nonnull (a);
    b = lrg_mmo_store_new_postgres (connection, &error);
    g_assert_no_error (error);
    market_a = lrg_mmo_market_new (a);
    market_b = lrg_mmo_market_new (b);
    g_assert_true (lrg_mmo_market_grant (market_a, id, "coins", 42, id, &error));
    g_assert_true (lrg_mmo_market_grant (market_b, id, "coins", 42, id, &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market_b, id, "coins", &error), ==, 42);
    directory_a = lrg_mmo_shard_directory_new (a, &error);
    directory_b = lrg_mmo_shard_directory_new (b, &error);
    fence = lrg_mmo_shard_directory_acquire (directory_a, id, "worker-a", "tls://a", 60, &error);
    g_assert_cmpuint (fence, >, 0);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&builder, "(st@ay)", id, (guint64) 0,
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, "state", 5, 1));
    changes = g_variant_ref_sink (g_variant_builder_end (&builder));
    next = lrg_mmo_shard_directory_handoff (directory_a, id, "worker-a", fence, "worker-b", "tls://b", state, 60, &error);
    g_assert_cmpuint (next, >, fence);
    g_assert_false (lrg_mmo_store_commit_fenced (a, id, "worker-a", fence, changes, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_store_commit_fenced (b, id, "worker-b", next, changes, &error));
    g_assert_false (lrg_mmo_store_commit (a, changes, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error (&error);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&builder, "(st@ay)", id, (guint64) 1,
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, "", 0, 1));
    altered = g_variant_ref_sink (g_variant_builder_end (&builder));
    g_assert_true (lrg_mmo_store_commit_once (a, id, altered, &duplicate, &error));
    g_assert_false (duplicate);
    g_assert_true (lrg_mmo_store_commit_once (b, id, altered, &duplicate, &error));
    g_assert_true (duplicate);
    lookup = lrg_mmo_shard_directory_lookup (directory_b, id, &error);
    g_assert_nonnull (lookup);
    audit = lrg_mmo_store_read_audit (b, 0, 100, &error);
    g_assert_nonnull (audit);
    g_assert_true (lrg_mmo_shard_directory_release (directory_b, id, "worker-b", next, &error));
    g_assert_no_error (error);
}

static void
social_economy (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoSocial) social = lrg_mmo_social_new (store);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(GVariant) messages = NULL;
    g_autoptr(GBytes) metadata = g_bytes_new_static ("rare", 4);
    g_assert_true (lrg_mmo_social_friend (social, "alice", "bob", 0, &error));
    g_assert_false (lrg_mmo_social_friend (social, "alice", "bob", 1, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_social_friend (social, "bob", "alice", 1, &error));
    g_assert_true (lrg_mmo_social_are_friends (social, "alice", "bob", &error));
    g_assert_true (lrg_mmo_social_block (social, "bob", "alice", TRUE, &error));
    g_assert_false (lrg_mmo_social_are_friends (social, "alice", "bob", &error));
    g_assert_true (lrg_mmo_social_create_guild (social, "alice", "guild", &error));
    g_assert_true (lrg_mmo_social_invite (social, "alice", "guild", "bob", &error));
    g_assert_true (lrg_mmo_social_join (social, "bob", "guild", &error));
    g_assert_true (lrg_mmo_social_channel (social, "alice", "guild", "welcome", "chat1", FALSE, &error));
    g_assert_false (lrg_mmo_social_channel (social, "bob", "guild", "chat1", "redact1", TRUE, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_social_channel (social, "alice", "guild", "chat1", "redact1", TRUE, &error));
    messages = lrg_mmo_social_read_channel (social, "bob", "guild", &error);
    g_assert_cmpuint (g_variant_n_children (messages), ==, 0);
    g_assert_null (lrg_mmo_social_read_channel (social, "eve", "guild", &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_market_grant (market, "alice", "ore", 10, "ore1", &error));
    g_assert_true (lrg_mmo_market_grant (market, "bob", "coins", 100, "funds1", &error));
    g_assert_true (lrg_mmo_market_grant (market, "eve", "coins", 100, "funds2", &error));
    g_assert_true (lrg_mmo_market_offer (market, "alice", "bob", "trade", "ore", 2, "coins", 5, 60, &error));
    g_assert_false (lrg_mmo_market_accept_offer (market, "eve", "trade", "bad", FALSE, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_market_accept_offer (market, "bob", "trade", "accept1", FALSE, &error));
    g_assert_true (lrg_mmo_market_accept_offer (market, "bob", "trade", "accept1", FALSE, &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "bob", "ore", &error), ==, 2);
    g_assert_true (lrg_mmo_market_auction (market, "alice", "auction", "ore", 3, 10, 1, &error));
    g_assert_true (lrg_mmo_market_bid (market, "bob", "auction", 10, "bid1", &error));
    g_assert_true (lrg_mmo_market_bid (market, "eve", "auction", 20, "bid2", &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "bob", "coins", &error), ==, 95);
    g_assert_false (lrg_mmo_market_settle (market, "auction", &error));
    g_clear_error (&error);
    g_usleep (1100000);
    g_assert_true (lrg_mmo_market_settle (market, "auction", &error));
    g_assert_true (lrg_mmo_market_settle (market, "auction", &error));
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "coins", &error), ==, 25);
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "eve", "ore", &error), ==, 3);
    g_assert_true (lrg_mmo_market_mint_item (market, "unique1", "alice", "sword", metadata, &error));
    g_assert_false (lrg_mmo_market_mint_item (market, "unique1", "bob", "sword", metadata, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_market_transfer_item (market, "alice", "bob", "unique1", "item1", &error));
    g_assert_false (lrg_mmo_market_transfer_item (market, "alice", "eve", "unique1", "item2", &error));
    g_clear_error (&error);
    g_assert_no_error (error);
}

static void
fair_pages (void)
{
    g_autoptr(LrgMmoReplicator) server = lrg_mmo_replicator_new (10, 4, 4);
    g_autoptr(LrgMmoReplica) client = lrg_mmo_replica_new (4);
    g_autoptr(GBytes) state = g_bytes_new_static ("x", 1);
    g_autoptr(GVariant) found = NULL;
    guint i;
    gboolean more;
    lrg_mmo_replica_reset (client, "world");
    g_assert_true (lrg_mmo_replicator_upsert (server, 2, "zone", 0, 0, 0, state, NULL));
    for (i = 0; i < 3; i++)
    {
        g_autoptr(GVariant) page = NULL;
        g_assert_true (lrg_mmo_replicator_upsert (server, 1, "zone", 0, 0, 0, state, NULL));
        page = lrg_mmo_replicator_build_page (server, 1, "zone", 0, 0, 0, 10, 128, &more, NULL);
        g_assert_nonnull (page);
        g_assert_true (lrg_mmo_replica_apply (client, "world", page, NULL));
        g_assert_true (lrg_mmo_replicator_acknowledge (server, 1, lrg_mmo_replica_get_sequence (client), NULL));
    }
    found = lrg_mmo_replica_lookup (client, 2);
    g_assert_nonnull (found);
}

/* Pages of one round where all @n_npcs entities (ids 1..n) and the avatar
 * (id 100) changed; returns whether the avatar made the page and marks the
 * NPCs that did in @seen. */
static gboolean
crowded_round (LrgMmoReplicator *server, guint n_npcs, gboolean *seen)
{
    g_autoptr(GBytes) state = g_bytes_new_static ("x", 1);
    g_autoptr(GVariant) page = NULL;
    g_autoptr(GVariant) updates = NULL;
    gboolean more, avatar = FALSE;
    guint64 sequence, previous = 0;
    guint i;

    for (i = 1; i <= n_npcs; i++)
        g_assert_true (lrg_mmo_replicator_upsert (server, i, "zone", 0, 0, 0, state, NULL));
    g_assert_true (lrg_mmo_replicator_upsert (server, 100, "zone", 0, 0, 0, state, NULL));
    page = lrg_mmo_replicator_build_page (server, 7, "zone", 0, 0, 0, 10, 256, &more, NULL);
    g_assert_nonnull (page);
    g_assert_true (more);
    g_variant_get_child (page, 0, "t", &sequence);
    updates = g_variant_get_child_value (page, 1);
    for (i = 0; i < g_variant_n_children (updates); i++)
    {
        guint64 id;
        g_variant_get_child (updates, i, "(ttddd@ay)", &id, NULL, NULL, NULL, NULL, NULL);
        /* updates stay in ascending ID order within a page */
        g_assert_cmpuint (id, >, previous);
        previous = id;
        if (id == 100)
            avatar = TRUE;
        else
            seen[id] = TRUE;
    }
    g_assert_true (lrg_mmo_replicator_acknowledge (server, 7, sequence, NULL));
    return avatar;
}

/*
 * In a crowd where every entity changes every round and a page holds three,
 * the focus entity (the viewer's avatar) is on every page, and the
 * round-robin still reaches every other entity.  Without a focus the avatar
 * waits its turn and misses most pages.
 */
static void
focus_pages (void)
{
    g_autoptr(LrgMmoReplicator) focused = lrg_mmo_replicator_new (32, 4, 4);
    g_autoptr(LrgMmoReplicator) plain = lrg_mmo_replicator_new (32, 4, 4);
    gboolean seen[21] = { FALSE };
    gboolean ignored[21] = { FALSE };
    guint round, avatar_focused = 0, avatar_plain = 0, i;

    lrg_mmo_replicator_set_focus (focused, 7, 100);
    for (round = 0; round < 20; round++)
    {
        if (crowded_round (focused, 20, seen))
            avatar_focused++;
        if (crowded_round (plain, 20, ignored))
            avatar_plain++;
    }
    g_assert_cmpuint (avatar_focused, ==, 20);
    g_assert_cmpuint (avatar_plain, <, 10);
    for (i = 1; i <= 20; i++)
        g_assert_true (seen[i]);

    /* Clearing the focus (or forgetting the viewer) restores the plain rotation */
    lrg_mmo_replicator_set_focus (focused, 7, 0);
    avatar_focused = 0;
    for (round = 0; round < 20; round++)
        if (crowded_round (focused, 20, seen))
            avatar_focused++;
    g_assert_cmpuint (avatar_focused, <, 10);
}

static void
second_factor (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoAuth) auth = lrg_mmo_auth_new (store);
    g_autoptr(GBytes) secret = g_bytes_new_static ("12345678901234567890", 20);
    g_autofree gchar *token = NULL;
    g_assert_true (lrg_mmo_auth_register (auth, "alice", "test-long-password", &error));
    /* RFC 6238 SHA1 vectors, reduced to six digits. */
    g_assert_true (lrg_mmo_auth_set_totp (auth, "alice", secret, 287082, 59, &error));
    g_assert_null (lrg_mmo_auth_login (auth, "alice", "test-long-password", 1111111109, &error));
    g_clear_error (&error);
    g_assert_null (lrg_mmo_auth_login_totp (auth, "alice", "test-long-password", 287082, 59, &error));
    g_clear_error (&error);
    token = lrg_mmo_auth_login_totp (auth, "alice", "test-long-password", 81804, 1111111109, &error);
    g_assert_nonnull (token);
    g_assert_null (lrg_mmo_auth_login_totp (auth, "alice", "test-long-password", 81804, 1111111109, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_auth_set_totp (auth, "alice", NULL, 0, 1111111109, &error));
    g_assert_null (lrg_mmo_auth_verify (auth, token, 1111111110, &error));
    g_clear_error (&error);
    g_clear_pointer (&token, g_free);
    token = lrg_mmo_auth_login (auth, "alice", "test-long-password", 1111111110, &error);
    g_assert_nonnull (token);
    g_assert_no_error (error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/completion/simulation", simulation);
    g_test_add_func ("/mmo/completion/postgres", postgres);
    g_test_add_func ("/mmo/completion/social-economy", social_economy);
    g_test_add_func ("/mmo/completion/fair-pages", fair_pages);
    g_test_add_func ("/mmo/completion/focus-pages", focus_pages);
    g_test_add_func ("/mmo/completion/second-factor", second_factor);
    return g_test_run ();
}
