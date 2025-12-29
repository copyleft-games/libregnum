/* lrg-market.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMarket - Supply/demand price simulation.
 *
 * A market tracks resource prices that fluctuate based on supply
 * and demand. Useful for tycoon-style economy simulations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-resource.h"
#include "lrg-resource-pool.h"

G_BEGIN_DECLS

#define LRG_TYPE_MARKET (lrg_market_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMarket, lrg_market, LRG, MARKET, GObject)

/* Construction */

/**
 * lrg_market_new:
 *
 * Creates a new market.
 *
 * Returns: (transfer full): A new #LrgMarket
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMarket *
lrg_market_new (void);

/* Resource Registration */

/**
 * lrg_market_register_resource:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to register
 * @base_price: the base price for this resource
 * @min_price: minimum price (floor)
 * @max_price: maximum price (ceiling)
 *
 * Registers a resource with the market.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_register_resource (LrgMarket   *self,
                              LrgResource *resource,
                              gdouble      base_price,
                              gdouble      min_price,
                              gdouble      max_price);

/**
 * lrg_market_unregister_resource:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to unregister
 *
 * Removes a resource from the market.
 *
 * Returns: %TRUE if the resource was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_market_unregister_resource (LrgMarket   *self,
                                LrgResource *resource);

/**
 * lrg_market_is_registered:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to check
 *
 * Checks if a resource is registered with the market.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_market_is_registered (LrgMarket   *self,
                          LrgResource *resource);

/**
 * lrg_market_get_resources:
 * @self: an #LrgMarket
 *
 * Gets the list of registered resources.
 *
 * Returns: (transfer container) (element-type LrgResource): list of resources
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_market_get_resources (LrgMarket *self);

/* Pricing */

/**
 * lrg_market_get_price:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to query
 *
 * Gets the current market price for a resource.
 *
 * Returns: the current price, or 0 if not registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_price (LrgMarket   *self,
                      LrgResource *resource);

/**
 * lrg_market_get_base_price:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to query
 *
 * Gets the base price for a resource.
 *
 * Returns: the base price, or 0 if not registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_base_price (LrgMarket   *self,
                           LrgResource *resource);

/**
 * lrg_market_set_base_price:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 * @base_price: the new base price
 *
 * Sets the base price for a resource.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_set_base_price (LrgMarket   *self,
                           LrgResource *resource,
                           gdouble      base_price);

/**
 * lrg_market_get_buy_price:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 *
 * Gets the price to buy a resource (may include markup).
 *
 * Returns: the buy price
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_buy_price (LrgMarket   *self,
                          LrgResource *resource);

/**
 * lrg_market_get_sell_price:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 *
 * Gets the price when selling a resource (may include markdown).
 *
 * Returns: the sell price
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_sell_price (LrgMarket   *self,
                           LrgResource *resource);

/* Supply/Demand */

/**
 * lrg_market_add_supply:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 * @amount: the supply amount
 *
 * Adds to the supply of a resource (tends to lower price).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_add_supply (LrgMarket   *self,
                       LrgResource *resource,
                       gdouble      amount);

/**
 * lrg_market_add_demand:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 * @amount: the demand amount
 *
 * Adds to the demand of a resource (tends to raise price).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_add_demand (LrgMarket   *self,
                       LrgResource *resource,
                       gdouble      amount);

/**
 * lrg_market_get_supply:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 *
 * Gets the current supply level.
 *
 * Returns: the supply level
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_supply (LrgMarket   *self,
                       LrgResource *resource);

/**
 * lrg_market_get_demand:
 * @self: an #LrgMarket
 * @resource: the #LrgResource
 *
 * Gets the current demand level.
 *
 * Returns: the demand level
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_demand (LrgMarket   *self,
                       LrgResource *resource);

/* Transactions */

/**
 * lrg_market_buy:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to buy
 * @amount: the amount to buy
 * @currency: the currency #LrgResource to pay with
 * @buyer_pool: the buyer's #LrgResourcePool
 *
 * Buys a resource from the market. Currency is deducted and
 * resource is added to the buyer's pool. Adds to demand.
 *
 * Returns: %TRUE if the purchase succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_market_buy (LrgMarket       *self,
                LrgResource     *resource,
                gdouble          amount,
                LrgResource     *currency,
                LrgResourcePool *buyer_pool);

/**
 * lrg_market_sell:
 * @self: an #LrgMarket
 * @resource: the #LrgResource to sell
 * @amount: the amount to sell
 * @currency: the currency #LrgResource to receive
 * @seller_pool: the seller's #LrgResourcePool
 *
 * Sells a resource to the market. Resource is deducted and
 * currency is added to the seller's pool. Adds to supply.
 *
 * Returns: %TRUE if the sale succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_market_sell (LrgMarket       *self,
                 LrgResource     *resource,
                 gdouble          amount,
                 LrgResource     *currency,
                 LrgResourcePool *seller_pool);

/* Market Properties */

/**
 * lrg_market_get_volatility:
 * @self: an #LrgMarket
 *
 * Gets the market volatility (how much prices fluctuate).
 *
 * Returns: volatility from 0.0 (stable) to 1.0 (volatile)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_volatility (LrgMarket *self);

/**
 * lrg_market_set_volatility:
 * @self: an #LrgMarket
 * @volatility: volatility from 0.0 (stable) to 1.0 (volatile)
 *
 * Sets the market volatility.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_set_volatility (LrgMarket *self,
                           gdouble    volatility);

/**
 * lrg_market_get_buy_markup:
 * @self: an #LrgMarket
 *
 * Gets the buy markup percentage.
 *
 * Returns: markup as a multiplier (1.0 = no markup, 1.1 = 10% markup)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_buy_markup (LrgMarket *self);

/**
 * lrg_market_set_buy_markup:
 * @self: an #LrgMarket
 * @markup: markup as a multiplier (1.0 = no markup)
 *
 * Sets the buy markup percentage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_set_buy_markup (LrgMarket *self,
                           gdouble    markup);

/**
 * lrg_market_get_sell_markdown:
 * @self: an #LrgMarket
 *
 * Gets the sell markdown percentage.
 *
 * Returns: markdown as a multiplier (1.0 = no markdown, 0.9 = 10% markdown)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_market_get_sell_markdown (LrgMarket *self);

/**
 * lrg_market_set_sell_markdown:
 * @self: an #LrgMarket
 * @markdown: markdown as a multiplier (1.0 = no markdown)
 *
 * Sets the sell markdown percentage.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_set_sell_markdown (LrgMarket *self,
                              gdouble    markdown);

/* Simulation */

/**
 * lrg_market_update:
 * @self: an #LrgMarket
 * @delta: time elapsed in seconds
 *
 * Updates market prices based on supply/demand and volatility.
 * Should be called each frame or at regular intervals.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_update (LrgMarket *self,
                   gdouble    delta);

/**
 * lrg_market_reset_prices:
 * @self: an #LrgMarket
 *
 * Resets all prices to their base values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_reset_prices (LrgMarket *self);

/**
 * lrg_market_clear_supply_demand:
 * @self: an #LrgMarket
 *
 * Clears accumulated supply and demand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_market_clear_supply_demand (LrgMarket *self);

G_END_DECLS
