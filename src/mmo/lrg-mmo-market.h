/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-mmo-store.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_MARKET (lrg_mmo_market_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoMarket, lrg_mmo_market, LRG, MMO_MARKET, GObject)

/**
 * lrg_mmo_market_new:
 * @store: transactional store, retained by the service
 *
 * Creates a persistent fungible-resource market. All mutations are transactional.
 * Caller supplies authenticated actor IDs; never trust IDs copied from request payloads.
 *
 * Returns: (transfer full): service
 */
LRG_AVAILABLE_IN_ALL
LrgMmoMarket *
lrg_mmo_market_new (LrgMmoStore *store);

/**
 * lrg_mmo_market_get_balance:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @resource: resource ID; coins is the auction currency
 * @error: (nullable): return location for error
 *
 * Reads the quantity owned by an account; absent balances are zero.
 *
 * Returns: nonnegative quantity, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_market_get_balance (LrgMmoMarket *self,
                            const gchar *account,
                            const gchar *resource,
                            GError **error);

/**
 * lrg_mmo_market_grant:
 * @self: the service; confine to its owning thread
 * @account: ASCII account ID, 1 to 64 letters, digits, underscores or hyphens
 * @resource: resource ID
 * @quantity: positive quantity
 * @operation: unique durable operation ID
 * @error: (nullable): return location for error
 *
 * Trusted administration/game reward operation. Credits resources once for this exact intent.
 *
 * Returns: whether committed or already applied
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_grant (LrgMmoMarket *self,
                      const gchar *account,
                      const gchar *resource,
                      gint64 quantity,
                      const gchar *operation,
                      GError **error);

/**
 * lrg_mmo_market_transfer:
 * @self: the service; confine to its owning thread
 * @actor: authenticated sender
 * @recipient: destination account
 * @resource: resource ID
 * @quantity: positive quantity
 * @operation: unique durable operation ID
 * @error: (nullable): return location for error
 *
 * Atomically transfers resources; insufficient funds and overflow leave both accounts unchanged.
 *
 * Returns: whether committed or already applied
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_transfer (LrgMmoMarket *self,
                         const gchar *actor,
                         const gchar *recipient,
                         const gchar *resource,
                         gint64 quantity,
                         const gchar *operation,
                         GError **error);

/**
 * lrg_mmo_market_list:
 * @self: the service; confine to its owning thread
 * @actor: authenticated seller
 * @listing: unique listing ID
 * @resource: item resource ID, excluding coins
 * @quantity: positive quantity
 * @price: positive buyout price in coins
 * @duration: listing seconds, 1 to 604800
 * @error: (nullable): return location for error
 *
 * Escrows items and creates a persistent buyout listing atomically.
 *
 * Returns: whether listed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_list (LrgMmoMarket *self,
                     const gchar *actor,
                     const gchar *listing,
                     const gchar *resource,
                     gint64 quantity,
                     gint64 price,
                     guint duration,
                     GError **error);

/**
 * lrg_mmo_market_buy:
 * @self: the service; confine to its owning thread
 * @actor: authenticated buyer
 * @listing: listing ID
 * @operation: unique durable operation ID
 * @error: (nullable): return location for error
 *
 * Atomically settles an unexpired listing: buyer payment, seller credit, item delivery and closure.
 *
 * Returns: whether bought or already applied
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_buy (LrgMmoMarket *self,
                    const gchar *actor,
                    const gchar *listing,
                    const gchar *operation,
                    GError **error);

/**
 * lrg_mmo_market_cancel:
 * @self: the service; confine to its owning thread
 * @actor: authenticated seller
 * @listing: listing ID
 * @error: (nullable): return location for error
 *
 * Closes an unsold listing and returns escrow to its seller, including after expiry.
 *
 * Returns: whether cancelled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_cancel (LrgMmoMarket *self,
                       const gchar *actor,
                       const gchar *listing,
                       GError **error);

/**
 * lrg_mmo_market_inspect:
 * @self: the service; confine to its owning thread
 * @listing: listing ID
 * @error: (nullable): return location for error
 *
 * Reads listing details: seller, resource, quantity, price, expiry Unix seconds, closed.
 *
 * Returns: (transfer full) (nullable): (ssxxxb) listing
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_market_inspect (LrgMmoMarket *self,
                        const gchar *listing,
                        GError **error);

/**
 * lrg_mmo_market_offer:
 * @self: market service
 * @actor: authenticated sender
 * @recipient: intended recipient
 * @offer: unique offer ID
 * @give_resource: resource reserved from sender
 * @give_quantity: positive reserved quantity
 * @take_resource: resource requested in exchange, different from give_resource
 * @take_quantity: requested quantity; zero creates a claimable gift/mail attachment
 * @duration: expiry in seconds, 1 to 604800
 * @error: (nullable): return location for error
 *
 * Creates an immutable addressed trade with sender escrow. Repeated IDs reject.
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_offer (LrgMmoMarket *self,
    const gchar *actor,
    const gchar *recipient,
    const gchar *offer,
    const gchar *give_resource,
    gint64 give_quantity,
    const gchar *take_resource,
    gint64 take_quantity,
    guint duration,
    GError **error);

/**
 * lrg_mmo_market_accept_offer:
 * @self: market service
 * @actor: authenticated recipient, or sender for cancellation
 * @offer: offer ID
 * @operation: durable retry ID
 * @cancel: return escrow to sender instead of accepting
 * @error: (nullable): return location for error
 *
 * Atomically exchanges both sides, or returns escrow on cancellation. Expired offers
 * can only be cancelled; a zero-price attachment is claimed once.
 * Returns: whether settled or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_accept_offer (LrgMmoMarket *self,
    const gchar *actor,
    const gchar *offer,
    const gchar *operation,
    gboolean cancel,
    GError **error);

/**
 * lrg_mmo_market_auction:
 * @self: market service
 * @actor: authenticated seller
 * @auction: unique auction ID
 * @resource: non-coins resource
 * @quantity: escrow quantity
 * @minimum: minimum bid in coins
 * @duration: duration seconds, 1 to 604800
 * @error: (nullable): return location for error
 *
 * Creates an auction with item escrow and a fixed server-clock deadline.
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_auction (LrgMmoMarket *self,
    const gchar *actor,
    const gchar *auction,
    const gchar *resource,
    gint64 quantity,
    gint64 minimum,
    guint duration,
    GError **error);

/**
 * lrg_mmo_market_bid:
 * @self: market service
 * @actor: authenticated bidder
 * @auction: auction ID
 * @amount: total bid, higher than the current bid
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * Reserves bid funds and refunds the previous bidder in one transaction.
 * Returns: whether accepted or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_bid (LrgMmoMarket *self,
    const gchar *actor,
    const gchar *auction,
    gint64 amount,
    const gchar *operation,
    GError **error);

/**
 * lrg_mmo_market_settle:
 * @self: market service
 * @auction: auction ID
 * @error: (nullable): return location for error
 *
 * Settles an expired auction atomically. Unsold escrow returns to its seller.
 * Repeated settlement is harmless. Hosts schedule this for expired auction IDs.
 * Returns: whether settled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_settle (LrgMmoMarket *self,
    const gchar *auction,
    GError **error);

/**
 * lrg_mmo_market_mint_item:
 * @self: market service
 * @item: globally unique item ID
 * @owner: owner account
 * @definition: catalog definition ID
 * @metadata: immutable per-instance metadata, at most 64 KiB
 * @error: (nullable): return location for error
 *
 * Trusted administration: creates exactly one unique item. Existing IDs reject.
 * Returns: whether minted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_market_mint_item (LrgMmoMarket *self, const gchar *item, const gchar *owner,
                                   const gchar *definition, GBytes *metadata, GError **error);
/**
 * lrg_mmo_market_get_item:
 * @self: market service
 * @item: unique item ID
 * @error: (nullable): return location for error
 *
 * Trusted query for owner, definition and metadata. Hosts filter private metadata.
 * Returns: (transfer full) (nullable): (ssay) record
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_market_get_item (LrgMmoMarket *self, const gchar *item, GError **error);
/**
 * lrg_mmo_market_transfer_item:
 * @self: market service
 * @actor: authenticated owner
 * @recipient: new owner
 * @item: unique item ID
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * Atomically transfers ownership, preserving immutable metadata.
 * Returns: whether transferred or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_market_transfer_item (LrgMmoMarket *self, const gchar *actor, const gchar *recipient,
                                       const gchar *item, const gchar *operation, GError **error);
/**
 * LRG_MMO_MARKET_OWNER_LOT:
 *
 * The owner a unique item has while a lot holds it in escrow.  It is not
 * a valid account ID, so no account can move or burn the item meanwhile.
 */
#define LRG_MMO_MARKET_OWNER_LOT "@lot"

/**
 * LRG_MMO_MARKET_OWNER_BURNED:
 *
 * The owner of a unique item that left the market
 * (lrg_mmo_market_burn_item()); the record stays as a tombstone so the
 * ID is never minted twice.
 */
#define LRG_MMO_MARKET_OWNER_BURNED "@burned"

/**
 * LRG_MMO_MARKET_MAX_EXCHANGE_ITEMS:
 *
 * The most unique items one side of lrg_mmo_market_exchange() may give.
 */
#define LRG_MMO_MARKET_MAX_EXCHANGE_ITEMS (64)

/**
 * LRG_MMO_MARKET_CUT_SCALE:
 *
 * A lot's cut is given in parts per this scale (per mille): 50 is 5 %.
 */
#define LRG_MMO_MARKET_CUT_SCALE (1000)

/**
 * LRG_MMO_MARKET_LOT_OPEN:
 *
 * Lot state: taking bids.
 */
#define LRG_MMO_MARKET_LOT_OPEN (0)

/**
 * LRG_MMO_MARKET_LOT_SOLD:
 *
 * Lot state: sold, by buyout or to the highest bidder at expiry.
 */
#define LRG_MMO_MARKET_LOT_SOLD (1)

/**
 * LRG_MMO_MARKET_LOT_UNSOLD:
 *
 * Lot state: expired without a bid; the item went back to its seller.
 */
#define LRG_MMO_MARKET_LOT_UNSOLD (2)

/**
 * LRG_MMO_MARKET_LOT_CANCELLED:
 *
 * Lot state: withdrawn by its seller before any bid.
 */
#define LRG_MMO_MARKET_LOT_CANCELLED (3)

/**
 * LrgMmoMarketClock:
 * @user_data: the data given to lrg_mmo_market_set_clock()
 *
 * The market's clock: the current time in Unix seconds.
 *
 * Returns: the time
 */
typedef gint64 (*LrgMmoMarketClock) (gpointer user_data);

/**
 * lrg_mmo_market_set_clock:
 * @self: the service
 * @clock: (nullable) (scope notified) (closure user_data) (destroy notify): the
 *   clock, %NULL for the real time
 * @user_data: data for @clock
 * @notify: (nullable): frees @user_data
 *
 * Replaces the clock every expiry and deadline is measured with (a host's
 * server clock, or a test's).  Deadlines already stored keep their value.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_market_set_clock (LrgMmoMarket *self,
                          LrgMmoMarketClock clock,
                          gpointer user_data,
                          GDestroyNotify notify);

/**
 * lrg_mmo_market_now:
 * @self: the service
 *
 * Returns: the market clock's current Unix time in seconds
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_market_now (LrgMmoMarket *self);

/**
 * lrg_mmo_market_revoke:
 * @self: the service; confine to its owning thread
 * @account: account to debit
 * @resource: resource ID
 * @quantity: positive quantity
 * @operation: unique durable operation ID
 * @error: (nullable): return location for error
 *
 * Trusted withdrawal, the counterpart of lrg_mmo_market_grant(): debits
 * resources leaving the market (into a game character) once for this
 * exact intent.  Insufficient balances change nothing.
 *
 * Returns: whether committed or already applied
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_revoke (LrgMmoMarket *self,
                       const gchar *account,
                       const gchar *resource,
                       gint64 quantity,
                       const gchar *operation,
                       GError **error);

/**
 * lrg_mmo_market_has_operation:
 * @self: the service
 * @operation: a durable operation ID
 * @applied: (out): whether a committed operation holds this ID
 * @error: (nullable): return location for error
 *
 * Asks whether an operation with this ID was committed, whatever its
 * intent.  A host that hands goods to a game character outside the
 * market (a client that lost its connection before the answer arrived)
 * uses it to learn whether a deposit, withdrawal or burn it derived from
 * the request happened, without repeating it.
 *
 * Returns: %TRUE when the answer is in @applied, %FALSE with @error for
 *   an invalid ID or a store failure
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_has_operation (LrgMmoMarket *self,
                              const gchar *operation,
                              gboolean *applied,
                              GError **error);

/**
 * lrg_mmo_market_burn_item:
 * @self: market service
 * @actor: authenticated owner
 * @item: unique item ID
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * Takes a unique item out of the market (into a game character): its
 * owner becomes %LRG_MMO_MARKET_OWNER_BURNED and the metadata stays, so
 * the host can still deliver it and the ID is never reused.
 *
 * Returns: whether burned or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_burn_item (LrgMmoMarket *self,
                          const gchar *actor,
                          const gchar *item,
                          const gchar *operation,
                          GError **error);

/**
 * lrg_mmo_market_exchange:
 * @self: market service
 * @actor_a: one side, authenticated by the host
 * @items_a: (array zero-terminated=1) (nullable): unique items @actor_a gives
 * @coins_a: coins @actor_a gives, zero or more
 * @actor_b: the other side
 * @items_b: (array zero-terminated=1) (nullable): unique items @actor_b gives
 * @coins_b: coins @actor_b gives
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * A direct trade: every item and both coin amounts change hands in one
 * transaction, or nothing does (an item not owned by its side, short
 * coins, an overflow, a repeated item or a conflicting revision).  The
 * intent names both sides in full.
 *
 * Returns: whether exchanged or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_exchange (LrgMmoMarket *self,
                         const gchar *actor_a,
                         const gchar *const *items_a,
                         gint64 coins_a,
                         const gchar *actor_b,
                         const gchar *const *items_b,
                         gint64 coins_b,
                         const gchar *operation,
                         GError **error);

/**
 * lrg_mmo_market_lot_cut:
 * @price: a sale price in coins
 * @cut: the cut in parts per %LRG_MMO_MARKET_CUT_SCALE
 *
 * The house's share of a sale, rounded down, without overflow.
 *
 * Returns: the cut in coins
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_market_lot_cut (gint64 price,
                        guint cut);

/**
 * lrg_mmo_market_lot_create:
 * @self: market service
 * @actor: authenticated seller, owner of @item
 * @lot: unique lot ID
 * @item: the unique item sold
 * @house: the account that receives cuts and forfeited deposits (an
 *   auction house), not the seller
 * @minimum: the lowest first bid in coins, positive
 * @buyout: the price that buys at once, 0 for none, else at least @minimum
 * @deposit: coins taken from the seller now, returned on a sale and
 *   forfeited to @house when the lot expires unsold or is cancelled
 * @cut: the house's share of a sale per %LRG_MMO_MARKET_CUT_SCALE
 * @duration: seconds until expiry, 1 to 604800
 * @error: (nullable): return location for error
 *
 * Escrows the item (its owner becomes %LRG_MMO_MARKET_OWNER_LOT) and the
 * deposit and opens the lot, atomically.
 *
 * Returns: whether created
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_lot_create (LrgMmoMarket *self,
                           const gchar *actor,
                           const gchar *lot,
                           const gchar *item,
                           const gchar *house,
                           gint64 minimum,
                           gint64 buyout,
                           gint64 deposit,
                           guint cut,
                           guint duration,
                           GError **error);

/**
 * lrg_mmo_market_lot_bid:
 * @self: market service
 * @actor: authenticated bidder, neither the seller nor the house
 * @lot: lot ID
 * @amount: total bid: at least the minimum and above the current bid
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * Reserves the bid and refunds the outbid bidder in one transaction (a
 * bidder raising their own bid pays the difference).  A bid at or over
 * the buyout is the buyout: the lot is sold at the buyout price at once.
 *
 * Returns: whether accepted or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_lot_bid (LrgMmoMarket *self,
                        const gchar *actor,
                        const gchar *lot,
                        gint64 amount,
                        const gchar *operation,
                        GError **error);

/**
 * lrg_mmo_market_lot_buyout:
 * @self: market service
 * @actor: authenticated buyer
 * @lot: lot ID with a buyout price
 * @operation: durable retry ID
 * @error: (nullable): return location for error
 *
 * Buys the lot at its buyout price: the buyer pays, the outbid bidder is
 * refunded, the item goes to the buyer, the seller receives the price
 * less the cut plus the deposit and the house the cut, atomically.
 *
 * Returns: whether bought or identical retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_lot_buyout (LrgMmoMarket *self,
                           const gchar *actor,
                           const gchar *lot,
                           const gchar *operation,
                           GError **error);

/**
 * lrg_mmo_market_lot_settle:
 * @self: market service
 * @lot: lot ID
 * @error: (nullable): return location for error
 *
 * Settles an expired lot: sold to the highest bidder (as a buyout), or
 * unsold (the item back to the seller, the deposit to the house).
 * Settling a closed lot is harmless; an open unexpired one is refused.
 *
 * Returns: whether settled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_lot_settle (LrgMmoMarket *self,
                           const gchar *lot,
                           GError **error);

/**
 * lrg_mmo_market_lot_cancel:
 * @self: market service
 * @actor: authenticated seller
 * @lot: an open lot without bids
 * @error: (nullable): return location for error
 *
 * Withdraws a lot: the item returns to the seller, the deposit is
 * forfeited to the house.
 *
 * Returns: whether cancelled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_market_lot_cancel (LrgMmoMarket *self,
                           const gchar *actor,
                           const gchar *lot,
                           GError **error);

/**
 * lrg_mmo_market_lot_inspect:
 * @self: market service
 * @lot: lot ID
 * @error: (nullable): return location for error
 *
 * Reads a lot: seller, item, house, bidder ("" for none), minimum, bid,
 * buyout, deposit, expiry (Unix seconds), cut, state
 * (%LRG_MMO_MARKET_LOT_OPEN ..).
 *
 * Returns: (transfer full) (nullable): "(ssssxxxxxuy)" record
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_market_lot_inspect (LrgMmoMarket *self,
                            const gchar *lot,
                            GError **error);
G_END_DECLS
