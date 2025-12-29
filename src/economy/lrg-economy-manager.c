/* lrg-economy-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-economy-manager.h"
#include "../lrg-log.h"

/* Default singleton instance */
static LrgEconomyManager *default_manager = NULL;

struct _LrgEconomyManager
{
    GObject      parent_instance;

    GHashTable  *resources;   /* gchar* (id) -> LrgResource* */
    GHashTable  *recipes;     /* gchar* (id) -> LrgProductionRecipe* */
    LrgMarket   *market;
};

G_DEFINE_TYPE (LrgEconomyManager, lrg_economy_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MARKET,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_economy_manager_finalize (GObject *object)
{
    LrgEconomyManager *self = LRG_ECONOMY_MANAGER (object);

    g_clear_pointer (&self->resources, g_hash_table_unref);
    g_clear_pointer (&self->recipes, g_hash_table_unref);
    g_clear_object (&self->market);

    G_OBJECT_CLASS (lrg_economy_manager_parent_class)->finalize (object);
}

static void
lrg_economy_manager_dispose (GObject *object)
{
    LrgEconomyManager *self = LRG_ECONOMY_MANAGER (object);

    /* Clear singleton reference if this is the default instance */
    if (default_manager == self)
        default_manager = NULL;

    G_OBJECT_CLASS (lrg_economy_manager_parent_class)->dispose (object);
}

static void
lrg_economy_manager_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgEconomyManager *self = LRG_ECONOMY_MANAGER (object);

    switch (prop_id)
    {
    case PROP_MARKET:
        g_value_set_object (value, self->market);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_economy_manager_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgEconomyManager *self = LRG_ECONOMY_MANAGER (object);

    switch (prop_id)
    {
    case PROP_MARKET:
        lrg_economy_manager_set_market (self, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_economy_manager_class_init (LrgEconomyManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_economy_manager_finalize;
    object_class->dispose = lrg_economy_manager_dispose;
    object_class->get_property = lrg_economy_manager_get_property;
    object_class->set_property = lrg_economy_manager_set_property;

    /**
     * LrgEconomyManager:market:
     *
     * The global market for price simulation.
     */
    properties[PROP_MARKET] =
        g_param_spec_object ("market",
                             "Market",
                             "Global market",
                             LRG_TYPE_MARKET,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_economy_manager_init (LrgEconomyManager *self)
{
    self->resources = g_hash_table_new_full (g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              g_object_unref);

    self->recipes = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            g_object_unref);

    /* Create default market */
    self->market = lrg_market_new ();
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

LrgEconomyManager *
lrg_economy_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = g_object_new (LRG_TYPE_ECONOMY_MANAGER, NULL);
    }

    return default_manager;
}

/* ==========================================================================
 * Resource Registration
 * ========================================================================== */

void
lrg_economy_manager_register_resource (LrgEconomyManager *self,
                                       LrgResource       *resource)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_ECONOMY_MANAGER (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));

    id = lrg_resource_get_id (resource);
    g_hash_table_insert (self->resources,
                         g_strdup (id),
                         g_object_ref (resource));

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY, "Registered resource: %s", id);
}

gboolean
lrg_economy_manager_unregister_resource (LrgEconomyManager *self,
                                         const gchar       *resource_id)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), FALSE);
    g_return_val_if_fail (resource_id != NULL, FALSE);

    return g_hash_table_remove (self->resources, resource_id);
}

LrgResource *
lrg_economy_manager_get_resource (LrgEconomyManager *self,
                                  const gchar       *resource_id)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);
    g_return_val_if_fail (resource_id != NULL, NULL);

    return g_hash_table_lookup (self->resources, resource_id);
}

GList *
lrg_economy_manager_get_resources (LrgEconomyManager *self)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);

    return g_hash_table_get_values (self->resources);
}

GList *
lrg_economy_manager_get_resources_by_category (LrgEconomyManager   *self,
                                               LrgResourceCategory  category)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);

    g_hash_table_iter_init (&iter, self->resources);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgResource *resource = LRG_RESOURCE (value);
        if (lrg_resource_get_category (resource) == category)
        {
            result = g_list_prepend (result, resource);
        }
    }

    return result;
}

/* ==========================================================================
 * Recipe Registration
 * ========================================================================== */

void
lrg_economy_manager_register_recipe (LrgEconomyManager   *self,
                                     LrgProductionRecipe *recipe)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_ECONOMY_MANAGER (self));
    g_return_if_fail (LRG_IS_PRODUCTION_RECIPE (recipe));

    id = lrg_production_recipe_get_id (recipe);
    g_hash_table_insert (self->recipes,
                         g_strdup (id),
                         g_object_ref (recipe));

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY, "Registered recipe: %s", id);
}

gboolean
lrg_economy_manager_unregister_recipe (LrgEconomyManager *self,
                                       const gchar       *recipe_id)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), FALSE);
    g_return_val_if_fail (recipe_id != NULL, FALSE);

    return g_hash_table_remove (self->recipes, recipe_id);
}

LrgProductionRecipe *
lrg_economy_manager_get_recipe (LrgEconomyManager *self,
                                const gchar       *recipe_id)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);
    g_return_val_if_fail (recipe_id != NULL, NULL);

    return g_hash_table_lookup (self->recipes, recipe_id);
}

GList *
lrg_economy_manager_get_recipes (LrgEconomyManager *self)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);

    return g_hash_table_get_values (self->recipes);
}

GList *
lrg_economy_manager_get_recipes_for_output (LrgEconomyManager *self,
                                            LrgResource       *resource)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;
    const gchar *resource_id;

    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), NULL);

    resource_id = lrg_resource_get_id (resource);

    g_hash_table_iter_init (&iter, self->recipes);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgProductionRecipe *recipe = LRG_PRODUCTION_RECIPE (value);
        g_autoptr(GList) outputs = lrg_production_recipe_get_outputs (recipe);
        GList *l;

        for (l = outputs; l != NULL; l = l->next)
        {
            LrgResource *output = LRG_RESOURCE (l->data);
            if (g_strcmp0 (lrg_resource_get_id (output), resource_id) == 0)
            {
                result = g_list_prepend (result, recipe);
                break;
            }
        }
    }

    return result;
}

/* ==========================================================================
 * Market Management
 * ========================================================================== */

LrgMarket *
lrg_economy_manager_get_market (LrgEconomyManager *self)
{
    g_return_val_if_fail (LRG_IS_ECONOMY_MANAGER (self), NULL);
    return self->market;
}

void
lrg_economy_manager_set_market (LrgEconomyManager *self,
                                LrgMarket         *market)
{
    g_return_if_fail (LRG_IS_ECONOMY_MANAGER (self));
    g_return_if_fail (market == NULL || LRG_IS_MARKET (market));

    if (g_set_object (&self->market, market))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKET]);
    }
}

/* ==========================================================================
 * Update
 * ========================================================================== */

void
lrg_economy_manager_update (LrgEconomyManager *self,
                            gdouble            delta)
{
    g_return_if_fail (LRG_IS_ECONOMY_MANAGER (self));
    g_return_if_fail (delta >= 0.0);

    /* Update market prices */
    if (self->market != NULL)
    {
        lrg_market_update (self->market, delta);
    }
}

/* ==========================================================================
 * Utility
 * ========================================================================== */

void
lrg_economy_manager_clear (LrgEconomyManager *self)
{
    g_return_if_fail (LRG_IS_ECONOMY_MANAGER (self));

    g_hash_table_remove_all (self->resources);
    g_hash_table_remove_all (self->recipes);

    if (self->market != NULL)
    {
        lrg_market_reset_prices (self->market);
        lrg_market_clear_supply_demand (self->market);
    }

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY, "Cleared economy manager");
}
