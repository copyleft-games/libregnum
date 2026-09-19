/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "mmo/lrg-mmo-auth.h"
#include "mmo/lrg-mmo-season.h"
#include "mmo/lrg-mmo-matchmaker.h"

static void
recovery (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoAuth) auth = lrg_mmo_auth_new (store);
    g_autofree gchar *proof = NULL;
    g_autofree gchar *stale = NULL;
    g_autofree gchar *session = NULL;
    g_autofree gchar *account = NULL;
    g_autoptr(GVariant) delivery = NULL;
    g_autoptr(GVariant) denied = NULL;
    const gchar *address, *token;
    g_assert_true (lrg_mmo_auth_register (auth, "alice", "original-password", &error));
    denied = lrg_mmo_auth_prepare_recovery (auth, "alice", 100, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
    proof = lrg_mmo_auth_begin_address (auth, "alice", "alice@example.test\r\nBcc:evil", 100, &error);
    g_assert_null (proof);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    stale = lrg_mmo_auth_begin_recovery (auth, "alice", 100, &error);
    session = lrg_mmo_auth_login (auth, "alice", "original-password", 100, &error);
    proof = lrg_mmo_auth_begin_address (auth, "alice", "alice@example.test", 100, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_auth_confirm_address (auth, proof, 101, &error));
    g_assert_false (lrg_mmo_auth_confirm_address (auth, proof, 102, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    account = lrg_mmo_auth_verify (auth, session, 102, &error);
    g_assert_null (account);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_auth_recover (auth, stale, "stolen-password", 102, &error));
    g_clear_error (&error);
    delivery = lrg_mmo_auth_prepare_recovery (auth, "alice", 102, &error);
    g_assert_no_error (error);
    g_variant_get (delivery, "(&s&s)", &address, &token);
    g_assert_cmpstr (address, ==, "alice@example.test");
    denied = lrg_mmo_auth_prepare_recovery (auth, "alice", 103, &error);
    g_assert_null (denied);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_auth_recover (auth, token, "replacement-password", 104, &error));
    g_assert_false (lrg_mmo_auth_recover (auth, token, "another-password", 105, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_auth_moderate (auth, "moderator", "alice", TRUE, "abusive chat", "ban-1", &error));
    g_assert_true (lrg_mmo_auth_moderate (auth, "moderator", "alice", TRUE, "abusive chat", "ban-1", &error));
    denied = lrg_mmo_auth_prepare_recovery (auth, "alice", 200, &error);
    g_assert_null (denied);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_auth_moderate (auth, "moderator", "alice", FALSE, "appeal", "ban-1", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_auth_moderate (auth, "moderator", "alice", FALSE, "appeal", "appeal-1", &error));
    g_assert_no_error (error);
}

static void
seasons (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoSeason) service = lrg_mmo_season_new (store);
    g_autoptr(GVariant) standings = NULL;
    g_autoptr(GVariant) unchanged = NULL;
    const gchar *account;
    guint wins, losses, draws;
    g_assert_true (lrg_mmo_season_create (service, "summer", 100, 200, 2, &error));
    g_assert_false (lrg_mmo_season_record (service, "summer", "alice", "bob", 1, "early", 99, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_season_record (service, "summer", "alice", "bob", 1, "match-1", 100, &error));
    g_assert_true (lrg_mmo_season_record (service, "summer", "alice", "bob", 1, "match-1", 200, &error));
    g_assert_true (lrg_mmo_season_record (service, "summer", "alice", "bob", 0, "match-2", 101, &error));
    standings = lrg_mmo_season_standings (service, "summer", &error);
    g_assert_no_error (error);
    g_variant_get_child (standings, 0, "(&suuu)", &account, &wins, &losses, &draws);
    g_assert_cmpstr (account, ==, "alice");
    g_assert_cmpuint (wins, ==, 1);
    g_assert_cmpuint (losses, ==, 0);
    g_assert_cmpuint (draws, ==, 1);
    g_assert_false (lrg_mmo_season_record (service, "summer", "alice", "eve", 1, "full", 102, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_season_record (service, "summer", "alice", "bob", 2, "match-1", 102, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_season_record (service, "summer", "alice", "bob", 2, "late", 200, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    unchanged = lrg_mmo_season_standings (service, "summer", &error);
    g_assert_true (g_variant_equal (standings, unchanged));
}
static void
parties (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (4);
    g_autoptr(GVariant) party = g_variant_ref_sink (g_variant_new_parsed ("[('alice', uint32 100), ('bob', uint32 120)]"));
    g_autoptr(GPtrArray) match = NULL;
    g_autoptr(GError) error = NULL;
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, party, "arena", 1, &error));
    g_assert_false (lrg_mmo_matchmaker_enqueue (queue, "bob", "arena", 120, 1, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "carol", "arena", 110, 1, &error));
    match = lrg_mmo_matchmaker_take (queue, "arena", 2, 10, 2, &error);
    g_assert_cmpuint (match->len, ==, 0);
    g_clear_pointer (&match, g_ptr_array_unref);
    match = lrg_mmo_matchmaker_take (queue, "arena", 3, 20, 2, &error);
    g_assert_cmpuint (match->len, ==, 3);
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, party, "arena", 3, &error));
    lrg_mmo_matchmaker_cancel (queue, "alice");
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "bob", "arena", 120, 3, &error));
    lrg_mmo_matchmaker_cancel (queue, "bob");
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, party, "arena", 4, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "carol", "arena", 110, 4, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "dave", "arena", 110, 4, &error));
    g_assert_false (lrg_mmo_matchmaker_enqueue (queue, "eve", "arena", 110, 4, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error (&error);
    match = lrg_mmo_matchmaker_take (queue, "arena", 4, 20, 60000004, &error);
    g_assert_cmpuint (match->len, ==, 0);
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, party, "arena", 60000004, &error));
    g_assert_no_error (error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/runtime/recovery-moderation", recovery);
    g_test_add_func ("/mmo/runtime/seasons", seasons);
    g_test_add_func ("/mmo/runtime/whole-party-placement", parties);
    return g_test_run ();
}
