/* lrg-producer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProducer - Component that produces resources over time.
 *
 * A producer component uses a recipe to produce resources at a
 * configurable rate. It can be attached to game objects like buildings.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../ecs/lrg-component.h"
#include "lrg-production-recipe.h"
#include "lrg-resource-pool.h"

G_BEGIN_DECLS

#define LRG_TYPE_PRODUCER (lrg_producer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgProducer, lrg_producer, LRG, PRODUCER, LrgComponent)

/**
 * LrgProducerClass:
 * @parent_class: the parent class
 * @on_production_started: called when production starts
 * @on_production_complete: called when production completes
 * @can_produce: check if production can start
 *
 * The class structure for #LrgProducer.
 *
 * Since: 1.0
 */
struct _LrgProducerClass
{
    LrgComponentClass parent_class;

    /**
     * LrgProducerClass::on_production_started:
     * @self: the producer
     *
     * Called when a production cycle starts.
     */
    void     (* on_production_started)  (LrgProducer *self);

    /**
     * LrgProducerClass::on_production_complete:
     * @self: the producer
     *
     * Called when a production cycle completes.
     * The recipe has already been produced when this is called.
     */
    void     (* on_production_complete) (LrgProducer *self);

    /**
     * LrgProducerClass::can_produce:
     * @self: the producer
     *
     * Checks if production can start. Default checks recipe
     * requirements and if not already producing.
     *
     * Returns: %TRUE if production can start
     */
    gboolean (* can_produce)            (LrgProducer *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_producer_new:
 *
 * Creates a new producer component.
 *
 * Returns: (transfer full): A new #LrgProducer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgProducer *
lrg_producer_new (void);

/**
 * lrg_producer_new_with_recipe:
 * @recipe: the production recipe
 * @pool: the resource pool to produce into
 *
 * Creates a new producer component with a recipe and pool.
 *
 * Returns: (transfer full): A new #LrgProducer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgProducer *
lrg_producer_new_with_recipe (LrgProductionRecipe *recipe,
                              LrgResourcePool     *pool);

/* Properties */

/**
 * lrg_producer_get_recipe:
 * @self: an #LrgProducer
 *
 * Gets the production recipe.
 *
 * Returns: (transfer none) (nullable): the recipe
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgProductionRecipe *
lrg_producer_get_recipe (LrgProducer *self);

/**
 * lrg_producer_set_recipe:
 * @self: an #LrgProducer
 * @recipe: (nullable): the production recipe
 *
 * Sets the production recipe.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_set_recipe (LrgProducer         *self,
                         LrgProductionRecipe *recipe);

/**
 * lrg_producer_get_resource_pool:
 * @self: an #LrgProducer
 *
 * Gets the resource pool where outputs are stored.
 *
 * Returns: (transfer none) (nullable): the resource pool
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResourcePool *
lrg_producer_get_resource_pool (LrgProducer *self);

/**
 * lrg_producer_set_resource_pool:
 * @self: an #LrgProducer
 * @pool: (nullable): the resource pool
 *
 * Sets the resource pool where outputs are stored.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_set_resource_pool (LrgProducer     *self,
                                LrgResourcePool *pool);

/**
 * lrg_producer_get_input_pool:
 * @self: an #LrgProducer
 *
 * Gets the resource pool where inputs are consumed from.
 * If NULL, uses the same pool as output.
 *
 * Returns: (transfer none) (nullable): the input pool
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResourcePool *
lrg_producer_get_input_pool (LrgProducer *self);

/**
 * lrg_producer_set_input_pool:
 * @self: an #LrgProducer
 * @pool: (nullable): the input pool (NULL to use output pool)
 *
 * Sets the resource pool where inputs are consumed from.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_set_input_pool (LrgProducer     *self,
                             LrgResourcePool *pool);

/**
 * lrg_producer_get_rate_multiplier:
 * @self: an #LrgProducer
 *
 * Gets the production rate multiplier.
 *
 * Returns: the rate multiplier (1.0 = normal speed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_producer_get_rate_multiplier (LrgProducer *self);

/**
 * lrg_producer_set_rate_multiplier:
 * @self: an #LrgProducer
 * @multiplier: the rate multiplier (1.0 = normal, 2.0 = 2x speed)
 *
 * Sets the production rate multiplier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_set_rate_multiplier (LrgProducer *self,
                                  gdouble      multiplier);

/**
 * lrg_producer_get_auto_restart:
 * @self: an #LrgProducer
 *
 * Gets whether production automatically restarts after completing.
 *
 * Returns: %TRUE if auto-restart is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_producer_get_auto_restart (LrgProducer *self);

/**
 * lrg_producer_set_auto_restart:
 * @self: an #LrgProducer
 * @auto_restart: whether to auto-restart
 *
 * Sets whether production automatically restarts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_set_auto_restart (LrgProducer *self,
                               gboolean     auto_restart);

/* State */

/**
 * lrg_producer_get_is_producing:
 * @self: an #LrgProducer
 *
 * Gets whether production is currently in progress.
 *
 * Returns: %TRUE if producing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_producer_get_is_producing (LrgProducer *self);

/**
 * lrg_producer_get_progress:
 * @self: an #LrgProducer
 *
 * Gets the current production progress.
 *
 * Returns: progress from 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_producer_get_progress (LrgProducer *self);

/**
 * lrg_producer_get_elapsed_time:
 * @self: an #LrgProducer
 *
 * Gets the elapsed production time in seconds.
 *
 * Returns: elapsed time
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_producer_get_elapsed_time (LrgProducer *self);

/**
 * lrg_producer_get_remaining_time:
 * @self: an #LrgProducer
 *
 * Gets the remaining production time in seconds.
 *
 * Returns: remaining time
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_producer_get_remaining_time (LrgProducer *self);

/* Control */

/**
 * lrg_producer_start:
 * @self: an #LrgProducer
 *
 * Starts production if possible.
 *
 * Returns: %TRUE if production was started
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_producer_start (LrgProducer *self);

/**
 * lrg_producer_stop:
 * @self: an #LrgProducer
 *
 * Stops production in progress. Does not refund consumed inputs.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_stop (LrgProducer *self);

/**
 * lrg_producer_cancel:
 * @self: an #LrgProducer
 *
 * Cancels production and refunds consumed inputs.
 *
 * Returns: %TRUE if production was cancelled and inputs refunded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_producer_cancel (LrgProducer *self);

/**
 * lrg_producer_complete_immediately:
 * @self: an #LrgProducer
 *
 * Completes the current production immediately.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_producer_complete_immediately (LrgProducer *self);

/**
 * lrg_producer_can_produce:
 * @self: an #LrgProducer
 *
 * Checks if production can start (has recipe, pool, inputs).
 *
 * Returns: %TRUE if production can start
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_producer_can_produce (LrgProducer *self);

G_END_DECLS
