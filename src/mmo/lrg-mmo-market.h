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

G_END_DECLS
