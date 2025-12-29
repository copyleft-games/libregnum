/* lrg-economy-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEconomyManager - Singleton managing global economy state.
 *
 * The economy manager provides central registration and lookup of
 * resources, recipes, and markets. It also handles economy updates.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-resource.h"
#include "lrg-production-recipe.h"
#include "lrg-market.h"

G_BEGIN_DECLS

#define LRG_TYPE_ECONOMY_MANAGER (lrg_economy_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEconomyManager, lrg_economy_manager, LRG, ECONOMY_MANAGER, GObject)

/* Singleton Access */

/**
 * lrg_economy_manager_get_default:
 *
 * Gets the default economy manager instance.
 * Creates it if it doesn't exist.
 *
 * Returns: (transfer none): The default #LrgEconomyManager instance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEconomyManager *
lrg_economy_manager_get_default (void);

/* Resource Registration */

/**
 * lrg_economy_manager_register_resource:
 * @self: an #LrgEconomyManager
 * @resource: the #LrgResource to register
 *
 * Registers a resource with the economy manager.
 * Resources must be registered before they can be looked up by ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_economy_manager_register_resource (LrgEconomyManager *self,
                                       LrgResource       *resource);

/**
 * lrg_economy_manager_unregister_resource:
 * @self: an #LrgEconomyManager
 * @resource_id: the resource ID to unregister
 *
 * Unregisters a resource.
 *
 * Returns: %TRUE if the resource was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_economy_manager_unregister_resource (LrgEconomyManager *self,
                                         const gchar       *resource_id);

/**
 * lrg_economy_manager_get_resource:
 * @self: an #LrgEconomyManager
 * @resource_id: the resource ID to look up
 *
 * Gets a registered resource by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgResource, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResource *
lrg_economy_manager_get_resource (LrgEconomyManager *self,
                                  const gchar       *resource_id);

/**
 * lrg_economy_manager_get_resources:
 * @self: an #LrgEconomyManager
 *
 * Gets all registered resources.
 *
 * Returns: (transfer container) (element-type LrgResource): list of resources
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_economy_manager_get_resources (LrgEconomyManager *self);

/**
 * lrg_economy_manager_get_resources_by_category:
 * @self: an #LrgEconomyManager
 * @category: the #LrgResourceCategory to filter by
 *
 * Gets resources matching a category.
 *
 * Returns: (transfer container) (element-type LrgResource): list of resources
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_economy_manager_get_resources_by_category (LrgEconomyManager   *self,
                                               LrgResourceCategory  category);

/* Recipe Registration */

/**
 * lrg_economy_manager_register_recipe:
 * @self: an #LrgEconomyManager
 * @recipe: the #LrgProductionRecipe to register
 *
 * Registers a production recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_economy_manager_register_recipe (LrgEconomyManager   *self,
                                     LrgProductionRecipe *recipe);

/**
 * lrg_economy_manager_unregister_recipe:
 * @self: an #LrgEconomyManager
 * @recipe_id: the recipe ID to unregister
 *
 * Unregisters a recipe.
 *
 * Returns: %TRUE if the recipe was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_economy_manager_unregister_recipe (LrgEconomyManager *self,
                                       const gchar       *recipe_id);

/**
 * lrg_economy_manager_get_recipe:
 * @self: an #LrgEconomyManager
 * @recipe_id: the recipe ID to look up
 *
 * Gets a registered recipe by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgProductionRecipe, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgProductionRecipe *
lrg_economy_manager_get_recipe (LrgEconomyManager *self,
                                const gchar       *recipe_id);

/**
 * lrg_economy_manager_get_recipes:
 * @self: an #LrgEconomyManager
 *
 * Gets all registered recipes.
 *
 * Returns: (transfer container) (element-type LrgProductionRecipe): list of recipes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_economy_manager_get_recipes (LrgEconomyManager *self);

/**
 * lrg_economy_manager_get_recipes_for_output:
 * @self: an #LrgEconomyManager
 * @resource: the output #LrgResource to filter by
 *
 * Gets recipes that produce a specific resource.
 *
 * Returns: (transfer container) (element-type LrgProductionRecipe): list of recipes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_economy_manager_get_recipes_for_output (LrgEconomyManager *self,
                                            LrgResource       *resource);

/* Market Management */

/**
 * lrg_economy_manager_get_market:
 * @self: an #LrgEconomyManager
 *
 * Gets the global market.
 *
 * Returns: (transfer none): the #LrgMarket
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgMarket *
lrg_economy_manager_get_market (LrgEconomyManager *self);

/**
 * lrg_economy_manager_set_market:
 * @self: an #LrgEconomyManager
 * @market: (nullable): the #LrgMarket to use
 *
 * Sets the global market.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_economy_manager_set_market (LrgEconomyManager *self,
                                LrgMarket         *market);

/* Update */

/**
 * lrg_economy_manager_update:
 * @self: an #LrgEconomyManager
 * @delta: time elapsed in seconds
 *
 * Updates the economy (market prices, etc.).
 * Should be called each frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_economy_manager_update (LrgEconomyManager *self,
                            gdouble            delta);

/* Utility */

/**
 * lrg_economy_manager_clear:
 * @self: an #LrgEconomyManager
 *
 * Clears all registered resources and recipes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_economy_manager_clear (LrgEconomyManager *self);

G_END_DECLS
