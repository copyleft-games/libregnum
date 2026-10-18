/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * test-mmo-holdings.c - deeds, sales, leases and ventures on LrgMmoHoldings
 *
 * Every case checks the coins of everyone involved after both the
 * successful and the refused operations: a refusal must leave every
 * balance and record as it was.
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "mmo/lrg-mmo-store.h"
#include "mmo/lrg-mmo-market.h"
#include "mmo/lrg-mmo-holdings.h"

#define T0 (1000000)

static void
clear_error (GError **error, gint code)
{
    g_assert_error (*error, G_IO_ERROR, code);
    g_clear_error (error);
}

static gint64
coins (LrgMmoHoldings *holdings, const gchar *account)
{
    g_autoptr(GError) error = NULL;
    gint64 value = lrg_mmo_holdings_get_coins (holdings, account, &error);
    g_assert_no_error (error);
    return value;
}

static void
fund (LrgMmoMarket *market, const gchar *account, gint64 amount)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *operation = g_strdup_printf ("fund-%s-%" G_GINT64_FORMAT, account, amount);
    g_assert_true (lrg_mmo_market_grant (market, account, "coins", amount, operation, &error));
    g_assert_no_error (error);
}

static gint64
deed_int (LrgMmoHoldings *holdings, const gchar *deed, const gchar *field)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) view = lrg_mmo_holdings_get_deed (holdings, deed, &error);
    g_autoptr(GVariant) value = NULL;
    g_assert_no_error (error);
    value = g_variant_lookup_value (view, field, NULL);
    g_assert_nonnull (value);
    if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
        return g_variant_get_int64 (value);
    if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
        return g_variant_get_uint32 (value);
    return g_variant_get_byte (value);
}

static gchar *
deed_str (LrgMmoHoldings *holdings, const gchar *deed, const gchar *field)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) view = lrg_mmo_holdings_get_deed (holdings, deed, &error);
    gchar *text = NULL;
    g_assert_no_error (error);
    g_assert_true (g_variant_lookup (view, field, "s", &text));
    return text;
}

/* the index as one comma-separated string */
static gchar *
listed (LrgMmoHoldings *holdings, const gchar *index)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) ids = lrg_mmo_holdings_list (holdings, index, &error);
    g_autofree const gchar **strv = NULL;
    g_assert_no_error (error);
    strv = g_variant_get_strv (ids, NULL);
    return g_strjoinv (",", (gchar **) strv);
}

static void
assert_listed (LrgMmoHoldings *holdings, const gchar *index, const gchar *expected)
{
    g_autofree gchar *text = listed (holdings, index);
    g_assert_cmpstr (text, ==, expected);
}

static void
issue (LrgMmoHoldings *holdings, const gchar *deed, const gchar *owner)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) meta = g_bytes_new_static ("house", 5);
    g_autofree gchar *operation = g_strconcat ("issue-", deed, NULL);
    g_assert_true (lrg_mmo_holdings_issue (holdings, deed, owner, "house", meta, 0, NULL,
                                           operation, &error));
    g_assert_no_error (error);
}

static void
test_issue (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);
    g_autoptr(GBytes) meta = g_bytes_new_static ("{type: 2}", 9);
    g_autoptr(GBytes) big = NULL;
    g_autofree gchar *owner = NULL;
    gchar *blob;

    fund (market, "alice", 500);
    /* the realm sells a deed: the price moves to the treasury, once */
    g_assert_true (lrg_mmo_holdings_issue (holdings, "house-1", "alice", "cottage", meta, 300,
                                           "treasury", "claim-1", &error));
    g_assert_true (lrg_mmo_holdings_issue (holdings, "house-1", "alice", "cottage", meta, 300,
                                           "treasury", "claim-1", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "alice"), ==, 200);
    g_assert_cmpint (coins (holdings, "treasury"), ==, 300);
    /* the market sees the same purse */
    g_assert_cmpint (lrg_mmo_market_get_balance (market, "alice", "coins", &error), ==, 200);
    owner = deed_str (holdings, "house-1", "owner");
    g_assert_cmpstr (owner, ==, "alice");
    assert_listed (holdings, "owner/alice", "house-1");

    /* refusals change nothing */
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house-1", "bob", "cottage", meta, 0, NULL,
                                            "claim-2", &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house-2", "alice", "cottage", meta, 300,
                                            "treasury", "claim-3", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (coins (holdings, "alice"), ==, 200);
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house-2", "alice", "cottage", meta, 10,
                                            "treasury", "claim-1", &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house 2", "alice", "cottage", meta, 0,
                                            NULL, "claim-4", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house-2", "alice", "cottage", meta, 10,
                                            NULL, "claim-5", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    blob = g_malloc0 (65537);
    big = g_bytes_new_take (blob, 65537);
    g_assert_false (lrg_mmo_holdings_issue (holdings, "house-2", "alice", "cottage", big, 0,
                                            NULL, "claim-6", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null (lrg_mmo_holdings_get_deed (holdings, "house-2", &error));
    clear_error (&error, G_IO_ERROR_NOT_FOUND);
    g_assert_null (lrg_mmo_holdings_list (holdings, "owners/alice", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null (lrg_mmo_holdings_list (holdings, "owner/al ice", &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    assert_listed (holdings, "owner/nobody", "");
}

static void
test_sale (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);
    g_autofree gchar *owner = NULL;

    lrg_mmo_holdings_set_clock (holdings, T0);
    g_assert_cmpint (lrg_mmo_holdings_get_time (holdings), ==, T0);
    issue (holdings, "shop-1", "alice");
    fund (market, "bob", 1000);
    fund (market, "carol", 1000);

    /* only the owner offers; the deed sits in escrow until settled */
    g_assert_false (lrg_mmo_holdings_offer_sale (holdings, "bob", "shop-1", 500, NULL, 60, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_holdings_offer_sale (holdings, "alice", "shop-1", 0, NULL, 60, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "alice", "shop-1", 500, "carol", 60, &error));
    g_assert_no_error (error);
    assert_listed (holdings, "sales", "shop-1");
    g_assert_false (lrg_mmo_holdings_offer_sale (holdings, "alice", "shop-1", 10, NULL, 60, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);

    /* addressed to carol: bob cannot buy, nor at a changed price */
    g_assert_false (lrg_mmo_holdings_buy (holdings, "bob", "shop-1", 500, 0, NULL, "buy-b1", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_holdings_buy (holdings, "carol", "shop-1", 400, 0, NULL, "buy-c0", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_holdings_buy (holdings, "alice", "shop-1", 500, 0, NULL, "buy-a", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_cmpint (coins (holdings, "bob"), ==, 1000);
    g_assert_cmpint (coins (holdings, "carol"), ==, 1000);

    /* carol buys with a 5 % cut to the realm; a retry changes nothing */
    g_assert_true (lrg_mmo_holdings_buy (holdings, "carol", "shop-1", 500, 50, "realm", "buy-c1", &error));
    g_assert_true (lrg_mmo_holdings_buy (holdings, "carol", "shop-1", 500, 50, "realm", "buy-c1", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "carol"), ==, 500);
    g_assert_cmpint (coins (holdings, "alice"), ==, 475);
    g_assert_cmpint (coins (holdings, "realm"), ==, 25);
    owner = deed_str (holdings, "shop-1", "owner");
    g_assert_cmpstr (owner, ==, "carol");
    assert_listed (holdings, "sales", "");
    assert_listed (holdings, "owner/alice", "");
    assert_listed (holdings, "owner/carol", "shop-1");
    g_assert_cmpint (deed_int (holdings, "shop-1", "sale-price"), ==, 0);
    /* settled: nobody buys it again */
    g_assert_false (lrg_mmo_holdings_buy (holdings, "bob", "shop-1", 500, 0, NULL, "buy-b2", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);

    /* an offer expires; the owner may withdraw it or offer again */
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "carol", "shop-1", 900, NULL, 60, &error));
    lrg_mmo_holdings_set_clock (holdings, T0 + 61);
    g_assert_false (lrg_mmo_holdings_buy (holdings, "bob", "shop-1", 900, 0, NULL, "buy-b3", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "carol", "shop-1", 800, NULL, 60, &error));
    g_assert_false (lrg_mmo_holdings_cancel_sale (holdings, "bob", "shop-1", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_holdings_cancel_sale (holdings, "carol", "shop-1", &error));
    g_assert_false (lrg_mmo_holdings_cancel_sale (holdings, "carol", "shop-1", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    assert_listed (holdings, "sales", "");

    /* a buyer short of coins keeps them, and the deed stays */
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "carol", "shop-1", 5000, NULL, 60, &error));
    g_assert_false (lrg_mmo_holdings_buy (holdings, "bob", "shop-1", 5000, 0, NULL, "buy-b4", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (coins (holdings, "bob"), ==, 1000);
    assert_listed (holdings, "owner/carol", "shop-1");
}

static void
test_lease (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);

    lrg_mmo_holdings_set_clock (holdings, T0);
    issue (holdings, "flat-1", "olga");
    fund (market, "tom", 100);

    g_assert_false (lrg_mmo_holdings_offer_lease (holdings, "tom", "flat-1", NULL, 10, 100, 4, 20, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-1", NULL, 10, 0, 4, 20, &error));
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-1", NULL, 10, 100, 4, 20, &error));
    g_assert_no_error (error);
    assert_listed (holdings, "leases", "flat-1");

    /* the owner cannot lease its own deed; a changed rent is refused */
    g_assert_false (lrg_mmo_holdings_take_lease (holdings, "olga", "flat-1", 10, "lease-o", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false (lrg_mmo_holdings_take_lease (holdings, "tom", "flat-1", 9, "lease-t0", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    /* first period and deposit at once */
    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "tom", "flat-1", 10, "lease-t1", &error));
    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "tom", "flat-1", 10, "lease-t1", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "tom"), ==, 70);
    g_assert_cmpint (coins (holdings, "olga"), ==, 10);
    g_assert_cmpint (deed_int (holdings, "flat-1", "deposit"), ==, 20);
    g_assert_cmpint (deed_int (holdings, "flat-1", "lease"), ==, 2);
    assert_listed (holdings, "tenant/tom", "flat-1");
    /* the owner cannot end a lease that is paid up; nothing is due yet */
    g_assert_false (lrg_mmo_holdings_end_lease (holdings, "olga", "flat-1", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 0);
    g_assert_false (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-1", NULL, 5, 100, 4, 0, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);

    /* two periods later, while both are offline: two rents collected */
    lrg_mmo_holdings_set_clock (holdings, T0 + 250);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 2);
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "tom"), ==, 50);
    g_assert_cmpint (coins (holdings, "olga"), ==, 30);
    g_assert_cmpint (deed_int (holdings, "flat-1", "earned"), ==, 30);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 0);

    /* the term ends after the fourth period: the deposit comes back */
    lrg_mmo_holdings_set_clock (holdings, T0 + 400);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 1);
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "tom"), ==, 60);
    g_assert_cmpint (coins (holdings, "olga"), ==, 40);
    g_assert_cmpint (deed_int (holdings, "flat-1", "lease"), ==, 0);
    assert_listed (holdings, "tenant/tom", "");
    assert_listed (holdings, "leases", "");
    /* every coin is accounted for: 100 + 0 in, 60 + 40 out */
    g_assert_cmpint (coins (holdings, "tom") + coins (holdings, "olga"), ==, 100);
}

static void
test_arrears (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);

    lrg_mmo_holdings_set_clock (holdings, T0);
    issue (holdings, "flat-1", "olga");
    issue (holdings, "flat-2", "olga");
    fund (market, "tom", 25);

    /* an addressed lease: only tom may take it */
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-1", "tom", 10, 100, 10, 12, &error));
    g_assert_false (lrg_mmo_holdings_take_lease (holdings, "eve", "flat-1", 10, "lease-e", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    /* short of the deposit: nothing moves */
    fund (market, "ann", 15);
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-2", NULL, 10, 100, 10, 12, &error));
    g_assert_false (lrg_mmo_holdings_take_lease (holdings, "ann", "flat-2", 10, "lease-a", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (coins (holdings, "ann"), ==, 15);
    g_assert_cmpint (coins (holdings, "olga"), ==, 0);

    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "tom", "flat-1", 10, "lease-t", &error));
    g_assert_cmpint (coins (holdings, "tom"), ==, 3);
    /* one period: tom pays 3 of 10 and owes 7 */
    lrg_mmo_holdings_set_clock (holdings, T0 + 100);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 1);
    g_assert_cmpint (coins (holdings, "tom"), ==, 0);
    g_assert_cmpint (coins (holdings, "olga"), ==, 13);
    g_assert_cmpint (deed_int (holdings, "flat-1", "owed"), ==, 7);
    g_assert_cmpint (deed_int (holdings, "flat-1", "arrears"), ==, 1);
    /* the next unpaid period ends it: the deposit (12) covers 12 of the
     * 17 owed, the rest is written off */
    lrg_mmo_holdings_set_clock (holdings, T0 + 200);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 1);
    g_assert_no_error (error);
    g_assert_cmpint (deed_int (holdings, "flat-1", "lease"), ==, 0);
    g_assert_cmpint (coins (holdings, "olga"), ==, 25);
    g_assert_cmpint (coins (holdings, "tom"), ==, 0);
    assert_listed (holdings, "tenant/tom", "");
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 0);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 0, &error), ==, -1);
    clear_error (&error, G_IO_ERROR_INVALID_ARGUMENT);

    /* the owner evicts a tenant in arrears; the deposit settles it */
    fund (market, "ann", 7);
    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "ann", "flat-2", 10, "lease-a2", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "ann"), ==, 0);
    lrg_mmo_holdings_set_clock (holdings, T0 + 300);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-2", 5, &error), ==, 1);
    g_assert_cmpint (deed_int (holdings, "flat-2", "owed"), ==, 10);
    g_assert_false (lrg_mmo_holdings_end_lease (holdings, "eve", "flat-2", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_holdings_end_lease (holdings, "olga", "flat-2", &error));
    g_assert_no_error (error);
    /* olga: 25 + 10 first rent + 10 of the deposit; ann gets 2 back */
    g_assert_cmpint (coins (holdings, "olga"), ==, 45);
    g_assert_cmpint (coins (holdings, "ann"), ==, 2);
    g_assert_false (lrg_mmo_holdings_end_lease (holdings, "olga", "flat-2", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);

    /* the owner withdraws an offer; a tenant leaves early */
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-2", NULL, 1, 100, 3, 1, &error));
    g_assert_true (lrg_mmo_holdings_end_lease (holdings, "olga", "flat-2", &error));
    assert_listed (holdings, "leases", "");
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-2", NULL, 1, 100, 3, 1, &error));
    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "ann", "flat-2", 1, "lease-a3", &error));
    g_assert_cmpint (coins (holdings, "ann"), ==, 0);
    g_assert_true (lrg_mmo_holdings_end_lease (holdings, "ann", "flat-2", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "ann"), ==, 1);
}

static void
test_lease_sale (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);

    lrg_mmo_holdings_set_clock (holdings, T0);
    issue (holdings, "flat-1", "olga");
    fund (market, "tom", 200);
    fund (market, "ivan", 500);
    g_assert_true (lrg_mmo_holdings_offer_lease (holdings, "olga", "flat-1", NULL, 10, 100, 5, 20, &error));
    g_assert_true (lrg_mmo_holdings_take_lease (holdings, "tom", "flat-1", 10, "lease-t", &error));
    /* sold with its tenant: the rent now goes to the new owner */
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "olga", "flat-1", 300, NULL, 600, &error));
    g_assert_true (lrg_mmo_holdings_buy (holdings, "ivan", "flat-1", 300, 0, NULL, "buy-i", &error));
    g_assert_no_error (error);
    lrg_mmo_holdings_set_clock (holdings, T0 + 100);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "flat-1", 2, &error), ==, 1);
    g_assert_cmpint (coins (holdings, "ivan"), ==, 210);
    g_assert_cmpint (coins (holdings, "olga"), ==, 310);
    /* the tenant buys its home: the lease ends, its deposit returns */
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "ivan", "flat-1", 100, "tom", 600, &error));
    g_assert_true (lrg_mmo_holdings_buy (holdings, "tom", "flat-1", 100, 0, NULL, "buy-t", &error));
    g_assert_no_error (error);
    g_assert_cmpint (deed_int (holdings, "flat-1", "lease"), ==, 0);
    g_assert_cmpint (coins (holdings, "tom"), ==, 200 - 10 - 20 - 10 - 100 + 20);
    g_assert_cmpint (coins (holdings, "ivan"), ==, 310);
    assert_listed (holdings, "tenant/tom", "");
    assert_listed (holdings, "owner/tom", "flat-1");
    assert_listed (holdings, "owner/ivan", "");
}

static void
test_venture (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);
    g_autoptr(GVariant) view = NULL;
    gboolean open = TRUE;

    issue (holdings, "inn-1", "mia");
    fund (market, "ann", 1000);
    fund (market, "bob", 1000);
    fund (market, "mia", 100);

    g_assert_false (lrg_mmo_holdings_open_venture (holdings, "bob", "inn-shares", "inn-1", 10, 100, &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_holdings_open_venture (holdings, "mia", "inn-shares", "inn-1", 10, 100, &error));
    g_assert_false (lrg_mmo_holdings_open_venture (holdings, "mia", "inn-shares", NULL, 10, 100, &error));
    clear_error (&error, G_IO_ERROR_EXISTS);
    assert_listed (holdings, "ventures", "inn-shares");
    assert_listed (holdings, "manager/mia", "inn-shares");

    /* capital goes to the manager at once */
    g_assert_false (lrg_mmo_holdings_invest (holdings, "mia", "inn-shares", 1, "inv-m", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_true (lrg_mmo_holdings_invest (holdings, "ann", "inn-shares", 30, "inv-a", &error));
    g_assert_true (lrg_mmo_holdings_invest (holdings, "ann", "inn-shares", 30, "inv-a", &error));
    g_assert_true (lrg_mmo_holdings_invest (holdings, "bob", "inn-shares", 10, "inv-b", &error));
    g_assert_no_error (error);
    g_assert_false (lrg_mmo_holdings_invest (holdings, "bob", "inn-shares", 61, "inv-b2", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (coins (holdings, "ann"), ==, 700);
    g_assert_cmpint (coins (holdings, "bob"), ==, 900);
    g_assert_cmpint (coins (holdings, "mia"), ==, 500);
    assert_listed (holdings, "investor/ann", "inn-shares");

    /* 100 among 40 units: ann 75, bob 25; 101: rounding keeps 1 */
    g_assert_cmpint (lrg_mmo_holdings_pay_dividend (holdings, "mia", "inn-shares", 101, "div-1", &error), ==, 100);
    g_assert_cmpint (lrg_mmo_holdings_pay_dividend (holdings, "mia", "inn-shares", 101, "div-1", &error), ==, 0);
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "ann"), ==, 775);
    g_assert_cmpint (coins (holdings, "bob"), ==, 925);
    g_assert_cmpint (coins (holdings, "mia"), ==, 400);
    g_assert_cmpint (lrg_mmo_holdings_pay_dividend (holdings, "ann", "inn-shares", 10, "div-2", &error), ==, -1);
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_cmpint (lrg_mmo_holdings_pay_dividend (holdings, "mia", "inn-shares", 10000, "div-3", &error), ==, -1);
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_cmpint (coins (holdings, "mia"), ==, 400);

    /* closing buys the units back */
    g_assert_false (lrg_mmo_holdings_close_venture (holdings, "mia", "inn-shares", 20, "close-1", &error));
    clear_error (&error, G_IO_ERROR_NO_SPACE);
    g_assert_true (lrg_mmo_holdings_close_venture (holdings, "mia", "inn-shares", 5, "close-2", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "ann"), ==, 925);
    g_assert_cmpint (coins (holdings, "bob"), ==, 975);
    g_assert_cmpint (coins (holdings, "mia"), ==, 200);
    view = lrg_mmo_holdings_get_venture (holdings, "inn-shares", &error);
    g_assert_true (g_variant_lookup (view, "open", "b", &open));
    g_assert_false (open);
    assert_listed (holdings, "ventures", "");
    g_assert_false (lrg_mmo_holdings_invest (holdings, "bob", "inn-shares", 1, "inv-b3", &error));
    clear_error (&error, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_null (lrg_mmo_holdings_get_venture (holdings, "nothing", &error));
    clear_error (&error, G_IO_ERROR_NOT_FOUND);
}

/* a stored deed record that does not hold together is refused */
static void
test_damaged (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (":memory:", &error);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);
    g_autoptr(GVariant) record = NULL;
    g_autoptr(GBytes) bytes = NULL;
    GVariantBuilder batch;

    /* an active lease without a tenant */
    record = g_variant_ref_sink (g_variant_new ("(ss@ay(xsx)(ysxxuuxxxuxx))", "olga", "house",
        g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, "", 0, 1),
        (gint64) 0, "", (gint64) 0, (guint8) 2, "", (gint64) 1, (gint64) 1, 1u, 1u,
        (gint64) 0, (gint64) 0, (gint64) 0, 0u, (gint64) 0, (gint64) 0));
    bytes = g_variant_get_data_as_bytes (record);
    g_variant_builder_init (&batch, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&batch, "(st@ay)", "holdings/deed/bad", (guint64) 0,
                           g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE));
    g_assert_true (lrg_mmo_store_commit (store, g_variant_builder_end (&batch), &error));
    g_assert_null (lrg_mmo_holdings_get_deed (holdings, "bad", &error));
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
    g_assert_cmpint (lrg_mmo_holdings_collect (holdings, "bad", 2, &error), ==, -1);
    clear_error (&error, G_IO_ERROR_INVALID_DATA);
}

typedef struct
{
    const gchar *path;
    const gchar *buyer;
    GMutex      *mutex;
    GCond       *condition;
    guint       *ready;
    gboolean     success;
    GError      *error;
} Purchase;

static gpointer
purchase_thread (gpointer data)
{
    Purchase *purchase = data;
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (purchase->path, &purchase->error);
    g_autoptr(LrgMmoHoldings) holdings = store != NULL ? lrg_mmo_holdings_new (store) : NULL;
    g_autofree gchar *operation = g_strconcat ("race-", purchase->buyer, NULL);
    g_mutex_lock (purchase->mutex);
    (*purchase->ready)++;
    g_cond_broadcast (purchase->condition);
    while (*purchase->ready < 2)
        g_cond_wait (purchase->condition, purchase->mutex);
    g_mutex_unlock (purchase->mutex);
    if (holdings != NULL)
        purchase->success = lrg_mmo_holdings_buy (holdings, purchase->buyer, "manor", 100, 0, NULL,
                                                  operation, &purchase->error);
    return NULL;
}

/* two buyers race for one deed on two connections: one wins, the other
 * keeps its coins; after a restart the state is the same */
static void
test_race_restart (void)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = g_dir_make_tmp ("lrg-holdings-XXXXXX", &error);
    g_autofree gchar *path = g_build_filename (dir, "holdings.db", NULL);
    g_autofree gchar *wal = g_strconcat (path, "-wal", NULL);
    g_autofree gchar *shm = g_strconcat (path, "-shm", NULL);
    g_autoptr(LrgMmoStore) store = lrg_mmo_store_new (path, &error);
    g_autoptr(LrgMmoMarket) market = lrg_mmo_market_new (store);
    g_autoptr(LrgMmoHoldings) holdings = lrg_mmo_holdings_new (store);
    g_autofree gchar *owner = NULL;
    GMutex mutex;
    GCond condition;
    guint ready = 0;
    Purchase a = { NULL, "alice", NULL, NULL, NULL, FALSE, NULL };
    Purchase b = { NULL, "bob", NULL, NULL, NULL, FALSE, NULL };
    GThread *first, *second;

    g_mutex_init (&mutex);
    g_cond_init (&condition);
    a.path = b.path = path;
    a.mutex = b.mutex = &mutex;
    a.condition = b.condition = &condition;
    a.ready = b.ready = &ready;
    issue (holdings, "manor", "seller");
    fund (market, "alice", 100);
    fund (market, "bob", 100);
    g_assert_true (lrg_mmo_holdings_offer_sale (holdings, "seller", "manor", 100, NULL, 600, &error));
    g_assert_no_error (error);
    first = g_thread_new ("buyer-a", purchase_thread, &a);
    second = g_thread_new ("buyer-b", purchase_thread, &b);
    g_thread_join (first);
    g_thread_join (second);
    g_assert_cmpuint (a.success + b.success, ==, 1);
    g_assert_nonnull (a.success ? b.error : a.error);
    g_clear_error (&a.error);
    g_clear_error (&b.error);

    /* reopen: one owner, one payment, the loser's coins intact */
    g_clear_object (&holdings);
    g_clear_object (&market);
    g_clear_object (&store);
    store = lrg_mmo_store_new (path, &error);
    g_assert_no_error (error);
    holdings = lrg_mmo_holdings_new (store);
    owner = deed_str (holdings, "manor", "owner");
    g_assert_cmpstr (owner, ==, a.success ? "alice" : "bob");
    g_assert_cmpint (coins (holdings, "seller"), ==, 100);
    g_assert_cmpint (coins (holdings, "alice") + coins (holdings, "bob"), ==, 100);
    /* the winner's retry after the restart is recognised, not repeated */
    g_assert_true (lrg_mmo_holdings_buy (holdings, owner, "manor", 100, 0, NULL,
                                         a.success ? "race-alice" : "race-bob", &error));
    g_assert_no_error (error);
    g_assert_cmpint (coins (holdings, "seller"), ==, 100);

    g_clear_object (&holdings);
    g_clear_object (&store);
    g_remove (wal);
    g_remove (shm);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
    g_cond_clear (&condition);
    g_mutex_clear (&mutex);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mmo/holdings/issue", test_issue);
    g_test_add_func ("/mmo/holdings/sale", test_sale);
    g_test_add_func ("/mmo/holdings/lease", test_lease);
    g_test_add_func ("/mmo/holdings/arrears", test_arrears);
    g_test_add_func ("/mmo/holdings/lease-sale", test_lease_sale);
    g_test_add_func ("/mmo/holdings/venture", test_venture);
    g_test_add_func ("/mmo/holdings/damaged", test_damaged);
    g_test_add_func ("/mmo/holdings/race-restart", test_race_restart);
    return g_test_run ();
}
