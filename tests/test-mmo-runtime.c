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

/* team matches: whole parties, balanced solos, the lifetime and the waits */
static void
teams (void)
{
    g_autoptr(LrgMmoMatchmaker) queue = lrg_mmo_matchmaker_new (32);
    g_autoptr(GVariant) party = g_variant_ref_sink (g_variant_new_parsed ("[('alice', uint32 1500), ('bob', uint32 1500)]"));
    g_autoptr(GVariant) trio = g_variant_ref_sink (g_variant_new_parsed ("[('p1', uint32 1500), ('p2', uint32 1500), ('p3', uint32 1500)]"));
    g_autoptr(GPtrArray) match = NULL;
    g_autoptr(GError) error = NULL;
    GPtrArray *first, *second;
    guint rating = 0, size = 0;
    guint i;

    /* bad requests */
    g_assert_null (lrg_mmo_matchmaker_take_teams (queue, "arena", 0, 2, 10, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_null (lrg_mmo_matchmaker_take_teams (queue, "arena", 2, 1, 10, 0, &error));
    g_clear_error (&error);

    /* two solos of each rating: 2v2 teams pair a high with a low */
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "a", "2v2", 1000, 0, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "b", "2v2", 1000, 0, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "c", "2v2", 1200, 0, &error));
    match = lrg_mmo_matchmaker_take_teams (queue, "2v2", 2, 2, 500, 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (match->len, ==, 0);
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, "2v2"), ==, 3);
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "d", "2v2", 1200, 0, &error));
    match = lrg_mmo_matchmaker_take_teams (queue, "2v2", 2, 2, 500, 0, &error);
    g_assert_cmpuint (match->len, ==, 2);
    for (i = 0; i < 2; i++)
    {
        GPtrArray *team = g_ptr_array_index (match, i);
        g_assert_cmpuint (team->len, ==, 2);
        /* one 1000 and one 1200 on each side */
        g_assert_true ((g_str_equal (g_ptr_array_index (team, 0), "c") || g_str_equal (g_ptr_array_index (team, 0), "d")) !=
                       (g_str_equal (g_ptr_array_index (team, 1), "c") || g_str_equal (g_ptr_array_index (team, 1), "d")));
    }
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, NULL), ==, 0);

    /* a party is never split: 3v3 of a trio, a pair and a solo */
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, party, "3v3", 1, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "solo", "3v3", 1500, 1, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, trio, "3v3", 1, &error));
    match = lrg_mmo_matchmaker_take_teams (queue, "3v3", 3, 2, 10, 2, &error);
    g_assert_cmpuint (match->len, ==, 2);
    first = g_ptr_array_index (match, 0);
    second = g_ptr_array_index (match, 1);
    g_assert_cmpuint (first->len, ==, 3);
    g_assert_cmpuint (second->len, ==, 3);
    for (i = 0; i < 2; i++)
    {
        GPtrArray *team = g_ptr_array_index (match, i);
        gboolean has_alice = FALSE, has_bob = FALSE, has_p1 = FALSE, has_p3 = FALSE;
        guint j;
        for (j = 0; j < team->len; j++)
        {
            has_alice |= g_str_equal (g_ptr_array_index (team, j), "alice");
            has_bob |= g_str_equal (g_ptr_array_index (team, j), "bob");
            has_p1 |= g_str_equal (g_ptr_array_index (team, j), "p1");
            has_p3 |= g_str_equal (g_ptr_array_index (team, j), "p3");
        }
        g_assert_true (has_alice == has_bob);
        g_assert_true (has_p1 == has_p3);
    }
    g_clear_pointer (&match, g_ptr_array_unref);

    /* a party larger than a team never matches; another mode is not taken */
    g_assert_true (lrg_mmo_matchmaker_enqueue_party (queue, trio, "2v2", 3, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "x", "other", 1500, 3, &error));
    match = lrg_mmo_matchmaker_take_teams (queue, "2v2", 2, 2, 10, 3, &error);
    g_assert_cmpuint (match->len, ==, 0);
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_true (lrg_mmo_matchmaker_is_queued (queue, "p2"));
    g_assert_false (lrg_mmo_matchmaker_is_queued (queue, "nobody"));

    /* waits and the oldest ticket's rating */
    g_assert_cmpint (lrg_mmo_matchmaker_get_wait (queue, "p2", 5 * G_TIME_SPAN_SECOND + 3), ==, 5 * G_TIME_SPAN_SECOND);
    g_assert_cmpint (lrg_mmo_matchmaker_get_wait (queue, "nobody", 10), ==, -1);
    g_assert_cmpint (lrg_mmo_matchmaker_get_oldest_wait (queue, "2v2", 13), ==, 10);
    g_assert_cmpint (lrg_mmo_matchmaker_get_oldest_wait (queue, "none", 13), ==, -1);
    g_assert_true (lrg_mmo_matchmaker_get_oldest_rating (queue, "2v2", &rating, &size));
    g_assert_cmpuint (rating, ==, 1500);
    g_assert_cmpuint (size, ==, 3);
    g_assert_false (lrg_mmo_matchmaker_get_oldest_rating (queue, "none", &rating, NULL));

    /* a longer lifetime keeps tickets past sixty seconds; then they go */
    lrg_mmo_matchmaker_set_ticket_lifetime (queue, 600 * G_TIME_SPAN_SECOND);
    g_assert_cmpint (lrg_mmo_matchmaker_get_ticket_lifetime (queue), ==, 600 * G_TIME_SPAN_SECOND);
    match = lrg_mmo_matchmaker_take_teams (queue, "2v2", 2, 2, 10, 120 * G_TIME_SPAN_SECOND, &error);
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_true (lrg_mmo_matchmaker_is_queued (queue, "p2"));
    match = lrg_mmo_matchmaker_take_teams (queue, "2v2", 2, 2, 10, 601 * G_TIME_SPAN_SECOND, &error);
    g_clear_pointer (&match, g_ptr_array_unref);
    g_assert_false (lrg_mmo_matchmaker_is_queued (queue, "p2"));
    g_assert_cmpuint (lrg_mmo_matchmaker_get_queued (queue, NULL), ==, 0);
    lrg_mmo_matchmaker_set_ticket_lifetime (queue, 5);
    g_assert_cmpint (lrg_mmo_matchmaker_get_ticket_lifetime (queue), ==, G_TIME_SPAN_SECOND);

    /* a rating spread too wide leaves the queue alone */
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "lo", "1v1", 100, 601 * G_TIME_SPAN_SECOND, &error));
    g_assert_true (lrg_mmo_matchmaker_enqueue (queue, "hi", "1v1", 900, 601 * G_TIME_SPAN_SECOND, &error));
    match = lrg_mmo_matchmaker_take_teams (queue, "1v1", 1, 2, 100, 601 * G_TIME_SPAN_SECOND, &error);
    g_assert_cmpuint (match->len, ==, 0);
    g_clear_pointer (&match, g_ptr_array_unref);
    match = lrg_mmo_matchmaker_take_teams (queue, "1v1", 1, 2, 800, 601 * G_TIME_SPAN_SECOND, &error);
    g_assert_cmpuint (match->len, ==, 2);
}

/* rated seasons: Elo by team means, retries, closure and a new season */
static void
rated (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoSeason) service = lrg_mmo_season_new (store);
    g_autoptr(GVariant) ratings = NULL;
    g_autoptr(GVariant) standings = NULL;
    const gchar *red[] = { "alice", "bob", NULL };
    const gchar *blue[] = { "carol", "dave", NULL };
    const gchar *twice[] = { "alice", "alice", NULL };
    const gchar *none[] = { NULL };
    const gchar *account;
    guint rating = 0, games = 0, capacity = 0;
    gint64 start = 0, end = 0;
    gboolean is_rated = TRUE;

    g_assert_false (lrg_mmo_season_create_rated (service, "s1", 100, 200, 10, 0, 32, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_season_create_rated (service, "s1", 100, 200, 10, 1500, 32, &error));
    g_assert_false (lrg_mmo_season_create_rated (service, "s1", 100, 200, 10, 1500, 32, &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_season_get_window (service, "s1", &start, &end, &capacity, &is_rated, &error));
    g_assert_cmpint (start, ==, 100);
    g_assert_cmpint (end, ==, 200);
    g_assert_cmpuint (capacity, ==, 10);
    g_assert_true (is_rated);
    g_assert_false (lrg_mmo_season_get_window (service, "nope", NULL, NULL, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* unknown entrants start at the base */
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "alice", &rating, &games, &error));
    g_assert_cmpuint (rating, ==, 1500);
    g_assert_cmpuint (games, ==, 0);

    /* bad teams */
    g_assert_false (lrg_mmo_season_record_teams (service, "s1", twice, blue, 1, "m0", 150, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_season_record_teams (service, "s1", none, blue, 1, "m0", 150, &error));
    g_clear_error (&error);
    g_assert_false (lrg_mmo_season_record_teams (service, "s1", red, red, 1, "m0", 150, &error));
    g_clear_error (&error);

    /* even teams: the winners gain K/2, the losers lose it */
    g_assert_true (lrg_mmo_season_record_teams (service, "s1", red, blue, 1, "m1", 150, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "bob", &rating, &games, &error));
    g_assert_cmpuint (rating, ==, 1516);
    g_assert_cmpuint (games, ==, 1);
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "dave", &rating, NULL, &error));
    g_assert_cmpuint (rating, ==, 1484);
    /* an exact retry changes nothing; a changed one is refused */
    g_assert_true (lrg_mmo_season_record_teams (service, "s1", red, blue, 1, "m1", 150, &error));
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "bob", &rating, &games, &error));
    g_assert_cmpuint (rating, ==, 1516);
    g_assert_cmpuint (games, ==, 1);
    g_assert_false (lrg_mmo_season_record_teams (service, "s1", red, blue, 2, "m1", 151, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* a draw between unequal teams moves the favourite down */
    g_assert_true (lrg_mmo_season_record_teams (service, "s1", red, blue, 0, "m2", 160, &error));
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "alice", &rating, &games, &error));
    g_assert_cmpuint (rating, <, 1516);
    g_assert_cmpuint (games, ==, 2);
    standings = lrg_mmo_season_standings (service, "s1", &error);
    g_assert_cmpuint (g_variant_n_children (standings), ==, 4);
    ratings = lrg_mmo_season_ratings (service, "s1", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_variant_n_children (ratings), ==, 4);
    g_variant_get_child (ratings, 0, "(&suu)", &account, &rating, &games);
    g_assert_true (g_str_equal (account, "alice") || g_str_equal (account, "bob"));

    /* one against one moves ratings too */
    g_assert_true (lrg_mmo_season_record (service, "s1", "carol", "erin", 1, "m3", 170, &error));
    g_assert_true (lrg_mmo_season_get_rating (service, "s1", "erin", &rating, NULL, &error));
    g_assert_cmpuint (rating, <, 1500);

    /* closed: no new result; the next season starts from the base */
    g_assert_false (lrg_mmo_season_record_teams (service, "s1", red, blue, 1, "late", 200, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_season_create_rated (service, "s2", 200, 300, 10, 1500, 32, &error));
    g_assert_true (lrg_mmo_season_get_rating (service, "s2", "alice", &rating, NULL, &error));
    g_assert_cmpuint (rating, ==, 1500);

    /* an unrated season has no ratings */
    g_assert_true (lrg_mmo_season_create (service, "plain", 0, 10, 4, &error));
    g_assert_false (lrg_mmo_season_get_rating (service, "plain", "alice", &rating, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_null (lrg_mmo_season_ratings (service, "plain", &error));
    g_clear_error (&error);
    g_assert_true (lrg_mmo_season_get_window (service, "plain", NULL, NULL, NULL, &is_rated, &error));
    g_assert_false (is_rated);
    g_assert_true (lrg_mmo_season_record_teams (service, "plain", red, blue, 2, "p1", 5, &error));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/runtime/recovery-moderation", recovery);
    g_test_add_func ("/mmo/runtime/seasons", seasons);
    g_test_add_func ("/mmo/runtime/whole-party-placement", parties);
    g_test_add_func ("/mmo/runtime/team-matches", teams);
    g_test_add_func ("/mmo/runtime/rated-seasons", rated);
    return g_test_run ();
}
