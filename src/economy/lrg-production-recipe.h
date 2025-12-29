/* lrg-production-recipe.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProductionRecipe - Defines input/output transformations for production.
 *
 * A recipe specifies what resources are consumed and what resources are
 * produced. Can also include production time and output chances.
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

#define LRG_TYPE_PRODUCTION_RECIPE (lrg_production_recipe_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProductionRecipe, lrg_production_recipe, LRG, PRODUCTION_RECIPE, GObject)

/* Construction */

/**
 * lrg_production_recipe_new:
 * @id: unique identifier for this recipe
 *
 * Creates a new production recipe.
 *
 * Returns: (transfer full): A new #LrgProductionRecipe
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgProductionRecipe *
lrg_production_recipe_new (const gchar *id);

/* Properties */

/**
 * lrg_production_recipe_get_id:
 * @self: an #LrgProductionRecipe
 *
 * Gets the unique identifier for this recipe.
 *
 * Returns: (transfer none): the recipe ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_production_recipe_get_id (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_get_name:
 * @self: an #LrgProductionRecipe
 *
 * Gets the display name for this recipe.
 *
 * Returns: (transfer none) (nullable): the display name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_production_recipe_get_name (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_set_name:
 * @self: an #LrgProductionRecipe
 * @name: (nullable): the display name
 *
 * Sets the display name for this recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_set_name (LrgProductionRecipe *self,
                                const gchar         *name);

/**
 * lrg_production_recipe_get_description:
 * @self: an #LrgProductionRecipe
 *
 * Gets the description for this recipe.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_production_recipe_get_description (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_set_description:
 * @self: an #LrgProductionRecipe
 * @description: (nullable): the description
 *
 * Sets the description for this recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_set_description (LrgProductionRecipe *self,
                                       const gchar         *description);

/**
 * lrg_production_recipe_get_production_time:
 * @self: an #LrgProductionRecipe
 *
 * Gets the time required to complete this recipe, in seconds.
 *
 * Returns: production time in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_production_recipe_get_production_time (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_set_production_time:
 * @self: an #LrgProductionRecipe
 * @time: production time in seconds (must be >= 0)
 *
 * Sets the time required to complete this recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_set_production_time (LrgProductionRecipe *self,
                                           gdouble              time);

/**
 * lrg_production_recipe_get_enabled:
 * @self: an #LrgProductionRecipe
 *
 * Gets whether this recipe is enabled/available.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_get_enabled (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_set_enabled:
 * @self: an #LrgProductionRecipe
 * @enabled: whether the recipe is enabled
 *
 * Sets whether this recipe is enabled/available.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_set_enabled (LrgProductionRecipe *self,
                                   gboolean             enabled);

/* Inputs */

/**
 * lrg_production_recipe_add_input:
 * @self: an #LrgProductionRecipe
 * @resource: the required #LrgResource
 * @amount: the amount required (must be > 0)
 *
 * Adds an input requirement to the recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_add_input (LrgProductionRecipe *self,
                                 LrgResource         *resource,
                                 gdouble              amount);

/**
 * lrg_production_recipe_remove_input:
 * @self: an #LrgProductionRecipe
 * @resource: the #LrgResource to remove
 *
 * Removes an input requirement from the recipe.
 *
 * Returns: %TRUE if the input was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_remove_input (LrgProductionRecipe *self,
                                    LrgResource         *resource);

/**
 * lrg_production_recipe_get_input_amount:
 * @self: an #LrgProductionRecipe
 * @resource: the #LrgResource to query
 *
 * Gets the required amount of an input resource.
 *
 * Returns: the required amount, or 0 if not an input
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_production_recipe_get_input_amount (LrgProductionRecipe *self,
                                        LrgResource         *resource);

/**
 * lrg_production_recipe_get_inputs:
 * @self: an #LrgProductionRecipe
 *
 * Gets the list of input resources.
 *
 * Returns: (transfer container) (element-type LrgResource): list of inputs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_production_recipe_get_inputs (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_get_input_count:
 * @self: an #LrgProductionRecipe
 *
 * Gets the number of input resource types.
 *
 * Returns: number of inputs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_production_recipe_get_input_count (LrgProductionRecipe *self);

/* Outputs */

/**
 * lrg_production_recipe_add_output:
 * @self: an #LrgProductionRecipe
 * @resource: the produced #LrgResource
 * @amount: the amount produced (must be > 0)
 * @chance: probability of producing this output (0.0 to 1.0, 1.0 = always)
 *
 * Adds an output to the recipe with an optional chance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_production_recipe_add_output (LrgProductionRecipe *self,
                                  LrgResource         *resource,
                                  gdouble              amount,
                                  gdouble              chance);

/**
 * lrg_production_recipe_remove_output:
 * @self: an #LrgProductionRecipe
 * @resource: the #LrgResource to remove
 *
 * Removes an output from the recipe.
 *
 * Returns: %TRUE if the output was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_remove_output (LrgProductionRecipe *self,
                                     LrgResource         *resource);

/**
 * lrg_production_recipe_get_output_amount:
 * @self: an #LrgProductionRecipe
 * @resource: the #LrgResource to query
 *
 * Gets the output amount for a resource.
 *
 * Returns: the output amount, or 0 if not an output
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_production_recipe_get_output_amount (LrgProductionRecipe *self,
                                         LrgResource         *resource);

/**
 * lrg_production_recipe_get_output_chance:
 * @self: an #LrgProductionRecipe
 * @resource: the #LrgResource to query
 *
 * Gets the output chance for a resource.
 *
 * Returns: the chance (0.0 to 1.0), or 0 if not an output
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_production_recipe_get_output_chance (LrgProductionRecipe *self,
                                         LrgResource         *resource);

/**
 * lrg_production_recipe_get_outputs:
 * @self: an #LrgProductionRecipe
 *
 * Gets the list of output resources.
 *
 * Returns: (transfer container) (element-type LrgResource): list of outputs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_production_recipe_get_outputs (LrgProductionRecipe *self);

/**
 * lrg_production_recipe_get_output_count:
 * @self: an #LrgProductionRecipe
 *
 * Gets the number of output resource types.
 *
 * Returns: number of outputs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_production_recipe_get_output_count (LrgProductionRecipe *self);

/* Production */

/**
 * lrg_production_recipe_can_produce:
 * @self: an #LrgProductionRecipe
 * @pool: the #LrgResourcePool to check against
 *
 * Checks if the recipe can be produced with the resources in the pool.
 *
 * Returns: %TRUE if all inputs are available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_can_produce (LrgProductionRecipe *self,
                                   LrgResourcePool     *pool);

/**
 * lrg_production_recipe_can_produce_count:
 * @self: an #LrgProductionRecipe
 * @pool: the #LrgResourcePool to check against
 *
 * Gets how many times this recipe can be produced with available resources.
 *
 * Returns: number of times recipe can be produced
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_production_recipe_can_produce_count (LrgProductionRecipe *self,
                                         LrgResourcePool     *pool);

/**
 * lrg_production_recipe_produce:
 * @self: an #LrgProductionRecipe
 * @pool: the #LrgResourcePool to modify
 *
 * Produces the recipe: consumes inputs and adds outputs to the pool.
 * Output chances are rolled for each output.
 *
 * Returns: %TRUE if production succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_produce (LrgProductionRecipe *self,
                               LrgResourcePool     *pool);

/**
 * lrg_production_recipe_produce_guaranteed:
 * @self: an #LrgProductionRecipe
 * @pool: the #LrgResourcePool to modify
 *
 * Produces the recipe with all outputs guaranteed (ignores chances).
 *
 * Returns: %TRUE if production succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_produce_guaranteed (LrgProductionRecipe *self,
                                          LrgResourcePool     *pool);

/**
 * lrg_production_recipe_produce_to_pool:
 * @self: an #LrgProductionRecipe
 * @source: the source #LrgResourcePool (inputs consumed from here)
 * @destination: the destination #LrgResourcePool (outputs added here)
 *
 * Produces the recipe with separate source and destination pools.
 *
 * Returns: %TRUE if production succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_production_recipe_produce_to_pool (LrgProductionRecipe *self,
                                       LrgResourcePool     *source,
                                       LrgResourcePool     *destination);

G_END_DECLS
