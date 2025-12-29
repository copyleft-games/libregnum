/* lrg-market.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-market.h"
#include "../lrg-log.h"

#include <math.h>

/* Market entry for a resource */
typedef struct
{
    LrgResource *resource;
    gdouble      base_price;
    gdouble      current_price;
    gdouble      min_price;
    gdouble      max_price;
    gdouble      supply;
    gdouble      demand;
} MarketEntry;

static void
market_entry_free (gpointer data)
{
    MarketEntry *entry = (MarketEntry *)data;
    g_clear_object (&entry->resource);
    g_free (entry);
}

struct _LrgMarket
{
    GObject      parent_instance;

    GHashTable  *entries;         /* gchar* (id) -> MarketEntry* */
    gdouble      volatility;
    gdouble      buy_markup;
    gdouble      sell_markdown;
    gdouble      supply_decay;    /* How fast supply/demand decay */
    gdouble      price_speed;     /* How fast prices change */
};

G_DEFINE_TYPE (LrgMarket, lrg_market, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VOLATILITY,
    PROP_BUY_MARKUP,
    PROP_SELL_MARKDOWN,
    N_PROPS
};

enum
{
    SIGNAL_PRICE_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_market_finalize (GObject *object)
{
    LrgMarket *self = LRG_MARKET (object);

    g_clear_pointer (&self->entries, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_market_parent_class)->finalize (object);
}

static void
lrg_market_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
    LrgMarket *self = LRG_MARKET (object);

    switch (prop_id)
    {
    case PROP_VOLATILITY:
        g_value_set_double (value, self->volatility);
        break;
    case PROP_BUY_MARKUP:
        g_value_set_double (value, self->buy_markup);
        break;
    case PROP_SELL_MARKDOWN:
        g_value_set_double (value, self->sell_markdown);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_market_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    LrgMarket *self = LRG_MARKET (object);

    switch (prop_id)
    {
    case PROP_VOLATILITY:
        lrg_market_set_volatility (self, g_value_get_double (value));
        break;
    case PROP_BUY_MARKUP:
        lrg_market_set_buy_markup (self, g_value_get_double (value));
        break;
    case PROP_SELL_MARKDOWN:
        lrg_market_set_sell_markdown (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_market_class_init (LrgMarketClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_market_finalize;
    object_class->get_property = lrg_market_get_property;
    object_class->set_property = lrg_market_set_property;

    /**
     * LrgMarket:volatility:
     *
     * How much prices fluctuate (0.0 = stable, 1.0 = volatile).
     */
    properties[PROP_VOLATILITY] =
        g_param_spec_double ("volatility",
                             "Volatility",
                             "Price volatility",
                             0.0, 1.0, 0.1,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMarket:buy-markup:
     *
     * Markup when buying (1.0 = no markup, 1.1 = 10% markup).
     */
    properties[PROP_BUY_MARKUP] =
        g_param_spec_double ("buy-markup",
                             "Buy Markup",
                             "Price markup when buying",
                             0.0, 10.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMarket:sell-markdown:
     *
     * Markdown when selling (1.0 = no markdown, 0.9 = 10% markdown).
     */
    properties[PROP_SELL_MARKDOWN] =
        g_param_spec_double ("sell-markdown",
                             "Sell Markdown",
                             "Price markdown when selling",
                             0.0, 1.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgMarket::price-changed:
     * @self: the market
     * @resource: the #LrgResource whose price changed
     * @old_price: the previous price
     * @new_price: the new price
     *
     * Emitted when a resource's price changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PRICE_CHANGED] =
        g_signal_new ("price-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 3,
                      LRG_TYPE_RESOURCE,
                      G_TYPE_DOUBLE,
                      G_TYPE_DOUBLE);
}

static void
lrg_market_init (LrgMarket *self)
{
    self->entries = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            market_entry_free);
    self->volatility = 0.1;
    self->buy_markup = 1.0;
    self->sell_markdown = 1.0;
    self->supply_decay = 0.1;     /* 10% decay per second */
    self->price_speed = 0.05;     /* 5% price change per second */
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgMarket *
lrg_market_new (void)
{
    return g_object_new (LRG_TYPE_MARKET, NULL);
}

/* ==========================================================================
 * Resource Registration
 * ========================================================================== */

void
lrg_market_register_resource (LrgMarket   *self,
                              LrgResource *resource,
                              gdouble      base_price,
                              gdouble      min_price,
                              gdouble      max_price)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (base_price >= 0.0);
    g_return_if_fail (min_price <= base_price && base_price <= max_price);

    entry = g_new0 (MarketEntry, 1);
    entry->resource = g_object_ref (resource);
    entry->base_price = base_price;
    entry->current_price = base_price;
    entry->min_price = min_price;
    entry->max_price = max_price;
    entry->supply = 0.0;
    entry->demand = 0.0;

    id = lrg_resource_get_id (resource);
    g_hash_table_insert (self->entries, g_strdup (id), entry);
}

gboolean
lrg_market_unregister_resource (LrgMarket   *self,
                                LrgResource *resource)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    id = lrg_resource_get_id (resource);
    return g_hash_table_remove (self->entries, id);
}

gboolean
lrg_market_is_registered (LrgMarket   *self,
                          LrgResource *resource)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    id = lrg_resource_get_id (resource);
    return g_hash_table_contains (self->entries, id);
}

GList *
lrg_market_get_resources (LrgMarket *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_MARKET (self), NULL);

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        MarketEntry *entry = (MarketEntry *)value;
        result = g_list_prepend (result, entry->resource);
    }

    return result;
}

/* ==========================================================================
 * Pricing
 * ========================================================================== */

gdouble
lrg_market_get_price (LrgMarket   *self,
                      LrgResource *resource)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
        return entry->current_price;

    return 0.0;
}

gdouble
lrg_market_get_base_price (LrgMarket   *self,
                           LrgResource *resource)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
        return entry->base_price;

    return 0.0;
}

void
lrg_market_set_base_price (LrgMarket   *self,
                           LrgResource *resource,
                           gdouble      base_price)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (base_price >= 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
    {
        entry->base_price = CLAMP (base_price, entry->min_price, entry->max_price);
    }
}

gdouble
lrg_market_get_buy_price (LrgMarket   *self,
                          LrgResource *resource)
{
    gdouble base_price;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    base_price = lrg_market_get_price (self, resource);
    return base_price * self->buy_markup;
}

gdouble
lrg_market_get_sell_price (LrgMarket   *self,
                           LrgResource *resource)
{
    gdouble base_price;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    base_price = lrg_market_get_price (self, resource);
    return base_price * self->sell_markdown;
}

/* ==========================================================================
 * Supply/Demand
 * ========================================================================== */

void
lrg_market_add_supply (LrgMarket   *self,
                       LrgResource *resource,
                       gdouble      amount)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (amount >= 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
    {
        entry->supply += amount;
    }
}

void
lrg_market_add_demand (LrgMarket   *self,
                       LrgResource *resource,
                       gdouble      amount)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (amount >= 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
    {
        entry->demand += amount;
    }
}

gdouble
lrg_market_get_supply (LrgMarket   *self,
                       LrgResource *resource)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
        return entry->supply;

    return 0.0;
}

gdouble
lrg_market_get_demand (LrgMarket   *self,
                       LrgResource *resource)
{
    MarketEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->entries, id);

    if (entry != NULL)
        return entry->demand;

    return 0.0;
}

/* ==========================================================================
 * Transactions
 * ========================================================================== */

gboolean
lrg_market_buy (LrgMarket       *self,
                LrgResource     *resource,
                gdouble          amount,
                LrgResource     *currency,
                LrgResourcePool *buyer_pool)
{
    gdouble price;
    gdouble total_cost;

    g_return_val_if_fail (LRG_IS_MARKET (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);
    g_return_val_if_fail (amount > 0.0, FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (currency), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (buyer_pool), FALSE);

    price = lrg_market_get_buy_price (self, resource);
    total_cost = price * amount;

    /* Check if buyer has enough currency */
    if (!lrg_resource_pool_has (buyer_pool, currency, total_cost))
        return FALSE;

    /* Perform transaction */
    lrg_resource_pool_remove (buyer_pool, currency, total_cost);
    lrg_resource_pool_add (buyer_pool, resource, amount);

    /* Record demand */
    lrg_market_add_demand (self, resource, amount);

    return TRUE;
}

gboolean
lrg_market_sell (LrgMarket       *self,
                 LrgResource     *resource,
                 gdouble          amount,
                 LrgResource     *currency,
                 LrgResourcePool *seller_pool)
{
    gdouble price;
    gdouble total_value;

    g_return_val_if_fail (LRG_IS_MARKET (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);
    g_return_val_if_fail (amount > 0.0, FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (currency), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (seller_pool), FALSE);

    /* Check if seller has the resource */
    if (!lrg_resource_pool_has (seller_pool, resource, amount))
        return FALSE;

    price = lrg_market_get_sell_price (self, resource);
    total_value = price * amount;

    /* Perform transaction */
    lrg_resource_pool_remove (seller_pool, resource, amount);
    lrg_resource_pool_add (seller_pool, currency, total_value);

    /* Record supply */
    lrg_market_add_supply (self, resource, amount);

    return TRUE;
}

/* ==========================================================================
 * Market Properties
 * ========================================================================== */

gdouble
lrg_market_get_volatility (LrgMarket *self)
{
    g_return_val_if_fail (LRG_IS_MARKET (self), 0.0);
    return self->volatility;
}

void
lrg_market_set_volatility (LrgMarket *self,
                           gdouble    volatility)
{
    g_return_if_fail (LRG_IS_MARKET (self));

    volatility = CLAMP (volatility, 0.0, 1.0);

    if (self->volatility != volatility)
    {
        self->volatility = volatility;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VOLATILITY]);
    }
}

gdouble
lrg_market_get_buy_markup (LrgMarket *self)
{
    g_return_val_if_fail (LRG_IS_MARKET (self), 1.0);
    return self->buy_markup;
}

void
lrg_market_set_buy_markup (LrgMarket *self,
                           gdouble    markup)
{
    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (markup >= 0.0);

    if (self->buy_markup != markup)
    {
        self->buy_markup = markup;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUY_MARKUP]);
    }
}

gdouble
lrg_market_get_sell_markdown (LrgMarket *self)
{
    g_return_val_if_fail (LRG_IS_MARKET (self), 1.0);
    return self->sell_markdown;
}

void
lrg_market_set_sell_markdown (LrgMarket *self,
                              gdouble    markdown)
{
    g_return_if_fail (LRG_IS_MARKET (self));

    markdown = CLAMP (markdown, 0.0, 1.0);

    if (self->sell_markdown != markdown)
    {
        self->sell_markdown = markdown;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELL_MARKDOWN]);
    }
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

void
lrg_market_update (LrgMarket *self,
                   gdouble    delta)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_MARKET (self));
    g_return_if_fail (delta >= 0.0);

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        MarketEntry *entry = (MarketEntry *)value;
        gdouble old_price;
        gdouble target_price;
        gdouble supply_demand_ratio;
        gdouble random_factor;
        gdouble price_change;

        old_price = entry->current_price;

        /*
         * Calculate target price based on supply/demand.
         * More supply = lower price, more demand = higher price.
         */
        if (entry->supply + entry->demand > 0.0001)
        {
            supply_demand_ratio = entry->demand / (entry->supply + entry->demand);
            /* Map 0.5 (balanced) to base price, 0 to min, 1 to max */
            target_price = entry->min_price +
                           (supply_demand_ratio * (entry->max_price - entry->min_price));
        }
        else
        {
            /* No activity, drift toward base price */
            target_price = entry->base_price;
        }

        /* Add random volatility */
        random_factor = (g_random_double () - 0.5) * 2.0 * self->volatility;
        target_price *= (1.0 + random_factor * 0.1);

        /* Clamp to valid range */
        target_price = CLAMP (target_price, entry->min_price, entry->max_price);

        /* Move toward target price gradually */
        price_change = (target_price - entry->current_price) * self->price_speed * delta;
        entry->current_price += price_change;
        entry->current_price = CLAMP (entry->current_price,
                                       entry->min_price,
                                       entry->max_price);

        /* Decay supply and demand */
        entry->supply *= (1.0 - self->supply_decay * delta);
        entry->demand *= (1.0 - self->supply_decay * delta);

        /* Emit price changed signal if significant change */
        if (fabs (entry->current_price - old_price) > 0.001)
        {
            g_signal_emit (self, signals[SIGNAL_PRICE_CHANGED], 0,
                           entry->resource, old_price, entry->current_price);
        }
    }
}

void
lrg_market_reset_prices (LrgMarket *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_MARKET (self));

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        MarketEntry *entry = (MarketEntry *)value;
        gdouble old_price = entry->current_price;

        entry->current_price = entry->base_price;

        if (entry->current_price != old_price)
        {
            g_signal_emit (self, signals[SIGNAL_PRICE_CHANGED], 0,
                           entry->resource, old_price, entry->current_price);
        }
    }
}

void
lrg_market_clear_supply_demand (LrgMarket *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_MARKET (self));

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        MarketEntry *entry = (MarketEntry *)value;
        entry->supply = 0.0;
        entry->demand = 0.0;
    }
}
