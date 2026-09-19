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
G_END_DECLS
