/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * lrg-mmo-holdings.h - deeds, leases and stakes between accounts
 *
 * Server-authoritative ownership of unique assets (deeds: houses, shops,
 * plots, ships), sales of a deed for coins with the deed held in escrow,
 * leases that charge rent per period from the tenant to the owner even
 * while both are offline, and ventures in which accounts buy units of an
 * owner's enterprise and receive dividends.  Every mutation is one
 * LrgMmoStore transaction over the deed, the venture, the coin balances
 * and the indexes it touches, so a crash, a disconnect or two concurrent
 * offers can never duplicate or lose coins or a deed: a conflicting
 * revision rolls the whole batch back.  Coins are the "coins" balances of
 * LrgMmoMarket on the same store, so the market and the holdings share
 * one purse per account.  See docs/modules/mmo/holdings.org.
 */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-mmo-store.h"
G_BEGIN_DECLS

/**
 * LRG_MMO_HOLDINGS_MAX_INDEX:
 *
 * The most entries one index (an account's deeds, the open sales, the
 * active leases, ...) holds.  Operations that would grow an index past
 * it fail with %G_IO_ERROR_NO_SPACE.
 */
#define LRG_MMO_HOLDINGS_MAX_INDEX (4096)

/**
 * LRG_MMO_HOLDINGS_MAX_HOLDERS:
 *
 * The most distinct accounts holding units of one venture (a dividend
 * pays them all in one transaction).
 */
#define LRG_MMO_HOLDINGS_MAX_HOLDERS (64)

/**
 * LRG_MMO_HOLDINGS_MAX_PERIODS:
 *
 * The most rent periods one lrg_mmo_holdings_collect() settles; a host
 * that was down for longer calls it again.
 */
#define LRG_MMO_HOLDINGS_MAX_PERIODS (64)

#define LRG_TYPE_MMO_HOLDINGS (lrg_mmo_holdings_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoHoldings, lrg_mmo_holdings, LRG, MMO_HOLDINGS, GObject)

/**
 * lrg_mmo_holdings_new:
 * @store: transactional store, retained by the service
 *
 * Creates the holdings service.  Like every MMO service it is confined to
 * the thread that uses @store.  Arguments named @actor are trusted,
 * authenticated account IDs (1 to 64 ASCII letters, digits, '_' or '-');
 * never copy them from a request payload.
 *
 * Returns: (transfer full): the service
 */
LRG_AVAILABLE_IN_ALL
LrgMmoHoldings *
lrg_mmo_holdings_new (LrgMmoStore *store);

/**
 * lrg_mmo_holdings_set_clock:
 * @self: the service
 * @now: Unix seconds every following call takes as "now", or 0 for the
 *   real time
 *
 * Fixes the service clock (tests, replays, simulated time).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_holdings_set_clock (LrgMmoHoldings *self,
                            gint64          now);

/**
 * lrg_mmo_holdings_get_time:
 * @self: the service
 *
 * Returns: the service's "now" in Unix seconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_holdings_get_time (LrgMmoHoldings *self);

/**
 * lrg_mmo_holdings_get_coins:
 * @self: the service
 * @account: account ID
 * @error: (nullable): return location for an error
 *
 * Reads the account's "coins" balance (the LrgMmoMarket purse).
 *
 * Returns: nonnegative coins, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_holdings_get_coins (LrgMmoHoldings  *self,
                            const gchar     *account,
                            GError         **error);

/**
 * lrg_mmo_holdings_issue:
 * @self: the service
 * @deed: unique deed ID
 * @owner: the account that will own it
 * @definition: catalog definition ID (what kind of asset it is)
 * @metadata: immutable per-deed metadata, at most 64 KiB
 * @price: coins @owner pays for it (0: granted)
 * @treasury: (nullable): the account credited with @price (a sink such as
 *   "realm-treasury"); required when @price is positive
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Trusted host operation (the realm selling an unclaimed asset): creates
 * the deed for @owner and, when @price is positive, debits @owner and
 * credits @treasury in the same transaction.  An existing deed fails with
 * %G_IO_ERROR_EXISTS; insufficient coins with %G_IO_ERROR_NO_SPACE.  A
 * retry with the same @operation and intent succeeds without charging
 * again.
 *
 * Returns: whether issued (or an identical retry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_issue (LrgMmoHoldings  *self,
                        const gchar     *deed,
                        const gchar     *owner,
                        const gchar     *definition,
                        GBytes          *metadata,
                        gint64           price,
                        const gchar     *treasury,
                        const gchar     *operation,
                        GError         **error);

/**
 * lrg_mmo_holdings_get_deed:
 * @self: the service
 * @deed: deed ID
 * @error: (nullable): return location for an error
 *
 * A trusted view of a deed as a dictionary: "owner" s, "definition" s,
 * "metadata" ay, "sale-price" x (0: not for sale), "sale-buyer" s (""
 * anyone), "sale-expiry" x, "lease" y (0 none, 1 offered, 2 active),
 * "tenant" s (the tenant, or the account the offer is addressed to, ""
 * anyone), "rent" x, "period" x (seconds), "periods" u (the term),
 * "settled" u (periods charged so far, paid or not), "starts" x,
 * "paid-until" x, "deposit" x, "arrears" u (unpaid periods in a row),
 * "owed" x (unpaid rent), "earned" x (rent the owner received).  A missing
 * deed fails with %G_IO_ERROR_NOT_FOUND.
 *
 * Returns: (transfer full) (nullable): an a{sv} dictionary
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_holdings_get_deed (LrgMmoHoldings  *self,
                           const gchar     *deed,
                           GError         **error);

/**
 * lrg_mmo_holdings_list:
 * @self: the service
 * @index: "owner/ACCOUNT" (deeds it owns), "tenant/ACCOUNT" (deeds it
 *   leases), "sales" (deeds offered for sale), "leases" (deeds with an
 *   offered or active lease), "ventures" (open ventures),
 *   "manager/ACCOUNT" (ventures it opened) or "investor/ACCOUNT"
 *   (ventures it holds units of)
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the sorted IDs, "as" (empty when
 *   nothing was ever indexed there)
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_holdings_list (LrgMmoHoldings  *self,
                       const gchar     *index,
                       GError         **error);

/**
 * lrg_mmo_holdings_offer_sale:
 * @self: the service
 * @actor: the authenticated owner
 * @deed: deed ID
 * @price: positive asking price in coins
 * @buyer: (nullable): the only account that may buy, %NULL or "" for
 *   anyone
 * @duration: seconds the offer stands, 1 to 2592000 (30 days)
 * @error: (nullable): return location for an error
 *
 * Puts the deed in escrow for sale: until it is bought, cancelled or the
 * offer expires, its owner cannot offer it again at another price.  The
 * deed's lease (if any) carries over to the buyer.
 *
 * Returns: whether offered
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_offer_sale (LrgMmoHoldings  *self,
                             const gchar     *actor,
                             const gchar     *deed,
                             gint64           price,
                             const gchar     *buyer,
                             guint            duration,
                             GError         **error);

/**
 * lrg_mmo_holdings_cancel_sale:
 * @self: the service
 * @actor: the authenticated owner
 * @deed: deed ID
 * @error: (nullable): return location for an error
 *
 * Withdraws a sale offer (an expired one too).
 *
 * Returns: whether withdrawn
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_cancel_sale (LrgMmoHoldings  *self,
                              const gchar     *actor,
                              const gchar     *deed,
                              GError         **error);

/**
 * lrg_mmo_holdings_buy:
 * @self: the service
 * @actor: the authenticated buyer
 * @deed: deed ID
 * @price: the price the buyer agreed to; it must equal the asking price,
 *   so a seller changing the offer meanwhile never surprises the buyer
 * @cut_permille: the host's fee in per mille of @price (0 .. 1000)
 * @fee_account: (nullable): the account the fee goes to; required with a
 *   positive cut
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Settles an unexpired sale atomically: the buyer pays @price, the seller
 * receives it less the cut, the fee account the cut, and the deed changes
 * owner (and indexes) in the same transaction.  A second buyer of the
 * same deed fails (%G_IO_ERROR_PERMISSION_DENIED once settled, or a
 * revision conflict while racing) and keeps its coins.
 *
 * Returns: whether bought (or an identical retry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_buy (LrgMmoHoldings  *self,
                      const gchar     *actor,
                      const gchar     *deed,
                      gint64           price,
                      guint            cut_permille,
                      const gchar     *fee_account,
                      const gchar     *operation,
                      GError         **error);

/**
 * lrg_mmo_holdings_offer_lease:
 * @self: the service
 * @actor: the authenticated owner
 * @deed: deed ID without an active lease
 * @tenant: (nullable): the only account that may take it, %NULL or "" for
 *   anyone
 * @rent: positive coins per period
 * @period: seconds per period, 1 to 31536000
 * @periods: the term in periods, 1 to 520
 * @deposit: coins the tenant leaves in escrow while leasing (0 or more)
 * @error: (nullable): return location for an error
 *
 * Offers the deed for lease (an earlier offer is replaced).
 *
 * Returns: whether offered
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_offer_lease (LrgMmoHoldings  *self,
                              const gchar     *actor,
                              const gchar     *deed,
                              const gchar     *tenant,
                              gint64           rent,
                              gint64           period,
                              guint            periods,
                              gint64           deposit,
                              GError         **error);

/**
 * lrg_mmo_holdings_take_lease:
 * @self: the service
 * @actor: the authenticated tenant (not the owner)
 * @deed: deed ID with an offered lease
 * @rent: the rent the tenant agreed to (must equal the offer's)
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Starts the lease: the tenant pays the first period's rent to the owner
 * and the deposit into escrow, in one transaction.
 *
 * Returns: whether leased (or an identical retry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_take_lease (LrgMmoHoldings  *self,
                             const gchar     *actor,
                             const gchar     *deed,
                             gint64           rent,
                             const gchar     *operation,
                             GError         **error);

/**
 * lrg_mmo_holdings_collect:
 * @self: the service
 * @deed: deed ID
 * @max_arrears: unpaid periods in a row that end the lease (1 or more)
 * @error: (nullable): return location for an error
 *
 * Host-scheduled settlement of a lease, whether the owner and tenant are
 * online or not: each due period (up to %LRG_MMO_HOLDINGS_MAX_PERIODS)
 * moves the rent (and earlier unpaid rent first) from the tenant to the
 * owner; a period the tenant cannot pay adds to the arrears.  Reaching
 * @max_arrears ends the lease with the deposit paying the owner what is
 * owed (the rest, if any, back to the tenant); the end of the term ends
 * it with the deposit returned less anything owed.  A deed without an
 * active lease or with nothing due settles nothing.
 *
 * Returns: the periods settled (0 or more), or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_mmo_holdings_collect (LrgMmoHoldings  *self,
                          const gchar     *deed,
                          guint            max_arrears,
                          GError         **error);

/**
 * lrg_mmo_holdings_end_lease:
 * @self: the service
 * @actor: the authenticated tenant or owner
 * @deed: deed ID
 * @error: (nullable): return location for an error
 *
 * The tenant leaves (the current period is not refunded; the deposit
 * comes back less anything owed), the owner withdraws an offered lease, or
 * the owner evicts a tenant in arrears (the deposit pays what is owed,
 * the rest returns to the tenant).  An owner cannot end a lease that is
 * paid up.
 *
 * Returns: whether ended
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_end_lease (LrgMmoHoldings  *self,
                            const gchar     *actor,
                            const gchar     *deed,
                            GError         **error);

/**
 * lrg_mmo_holdings_open_venture:
 * @self: the service
 * @actor: the authenticated manager
 * @venture: unique venture ID
 * @deed: (nullable): the deed the venture runs on; the manager must own
 *   it (%NULL or "" for none)
 * @unit_price: positive coins per unit
 * @units: units on offer, 1 to 1000000
 * @error: (nullable): return location for an error
 *
 * Opens a venture other accounts can buy units of.  Coins paid for units
 * go to the manager at once (it is capital); dividends come later from
 * lrg_mmo_holdings_pay_dividend().
 *
 * Returns: whether opened
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_open_venture (LrgMmoHoldings  *self,
                               const gchar     *actor,
                               const gchar     *venture,
                               const gchar     *deed,
                               gint64           unit_price,
                               guint            units,
                               GError         **error);

/**
 * lrg_mmo_holdings_get_venture:
 * @self: the service
 * @venture: venture ID
 * @error: (nullable): return location for an error
 *
 * A view of a venture: "manager" s, "deed" s, "unit-price" x, "units" u,
 * "sold" u, "paid" x (dividends paid so far), "open" b, "holders" a(su).
 *
 * Returns: (transfer full) (nullable): an a{sv} dictionary
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_holdings_get_venture (LrgMmoHoldings  *self,
                              const gchar     *venture,
                              GError         **error);

/**
 * lrg_mmo_holdings_invest:
 * @self: the service
 * @actor: the authenticated investor (not the manager)
 * @venture: an open venture
 * @units: units to buy (1 or more, at most those left)
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Buys units: the investor's coins go to the manager and the units to the
 * investor in one transaction.
 *
 * Returns: whether bought (or an identical retry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_invest (LrgMmoHoldings  *self,
                         const gchar     *actor,
                         const gchar     *venture,
                         guint            units,
                         const gchar     *operation,
                         GError         **error);

/**
 * lrg_mmo_holdings_pay_dividend:
 * @self: the service
 * @actor: the authenticated manager (or the host on its behalf)
 * @venture: an open venture with sold units
 * @amount: coins to share among the holders
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Pays every holder its share of @amount (rounded down per holder) from
 * the manager's coins in one transaction; what rounding leaves stays with
 * the manager.
 *
 * Returns: the coins actually paid out (0 when nothing was sold, when
 *   rounding left nothing, or for an identical retry of an earlier
 *   payment, which pays nothing again), or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_holdings_pay_dividend (LrgMmoHoldings  *self,
                               const gchar     *actor,
                               const gchar     *venture,
                               gint64           amount,
                               const gchar     *operation,
                               GError         **error);

/**
 * lrg_mmo_holdings_close_venture:
 * @self: the service
 * @actor: the authenticated manager
 * @venture: an open venture
 * @buyback: coins per unit the manager pays every holder back (0: none)
 * @operation: unique durable operation ID
 * @error: (nullable): return location for an error
 *
 * Closes the venture, buying the units back at @buyback from the
 * manager's coins in the same transaction.
 *
 * Returns: whether closed (or an identical retry)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_holdings_close_venture (LrgMmoHoldings  *self,
                                const gchar     *actor,
                                const gchar     *venture,
                                gint64           buyback,
                                const gchar     *operation,
                                GError         **error);

G_END_DECLS
