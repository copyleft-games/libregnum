/* lrg-consumer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgConsumer - Component that consumes resources over time.
 *
 * A consumer component continuously consumes resources at a configurable
 * rate. When resources are depleted, it enters a "starved" state and
 * can emit signals for gameplay effects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../ecs/lrg-component.h"
#include "lrg-resource.h"
#include "lrg-resource-pool.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONSUMER (lrg_consumer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgConsumer, lrg_consumer, LRG, CONSUMER, LrgComponent)

/**
 * LrgConsumerClass:
 * @parent_class: the parent class
 * @on_starved: called when a requirement cannot be met
 * @on_satisfied: called when requirements are met again
 *
 * The class structure for #LrgConsumer.
 *
 * Since: 1.0
 */
struct _LrgConsumerClass
{
    LrgComponentClass parent_class;

    /**
     * LrgConsumerClass::on_starved:
     * @self: the consumer
     * @resource: the resource that ran out
     *
     * Called when a required resource is depleted.
     */
    void (* on_starved)   (LrgConsumer *self,
                           LrgResource *resource);

    /**
     * LrgConsumerClass::on_satisfied:
     * @self: the consumer
     *
     * Called when all requirements are being met again.
     */
    void (* on_satisfied) (LrgConsumer *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_consumer_new:
 *
 * Creates a new consumer component.
 *
 * Returns: (transfer full): A new #LrgConsumer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgConsumer *
lrg_consumer_new (void);

/* Properties */

/**
 * lrg_consumer_get_resource_pool:
 * @self: an #LrgConsumer
 *
 * Gets the resource pool to consume from.
 *
 * Returns: (transfer none) (nullable): the resource pool
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResourcePool *
lrg_consumer_get_resource_pool (LrgConsumer *self);

/**
 * lrg_consumer_set_resource_pool:
 * @self: an #LrgConsumer
 * @pool: (nullable): the resource pool
 *
 * Sets the resource pool to consume from.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consumer_set_resource_pool (LrgConsumer     *self,
                                LrgResourcePool *pool);

/**
 * lrg_consumer_get_rate_multiplier:
 * @self: an #LrgConsumer
 *
 * Gets the consumption rate multiplier.
 *
 * Returns: the rate multiplier (1.0 = normal)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_consumer_get_rate_multiplier (LrgConsumer *self);

/**
 * lrg_consumer_set_rate_multiplier:
 * @self: an #LrgConsumer
 * @multiplier: the rate multiplier (1.0 = normal, 2.0 = 2x consumption)
 *
 * Sets the consumption rate multiplier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consumer_set_rate_multiplier (LrgConsumer *self,
                                  gdouble      multiplier);

/**
 * lrg_consumer_get_active:
 * @self: an #LrgConsumer
 *
 * Gets whether the consumer is actively consuming.
 *
 * Returns: %TRUE if active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consumer_get_active (LrgConsumer *self);

/**
 * lrg_consumer_set_active:
 * @self: an #LrgConsumer
 * @active: whether to consume resources
 *
 * Sets whether the consumer is actively consuming.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consumer_set_active (LrgConsumer *self,
                         gboolean     active);

/* Requirements */

/**
 * lrg_consumer_add_requirement:
 * @self: an #LrgConsumer
 * @resource: the #LrgResource to consume
 * @rate: consumption rate per second (must be > 0)
 *
 * Adds a resource consumption requirement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consumer_add_requirement (LrgConsumer *self,
                              LrgResource *resource,
                              gdouble      rate);

/**
 * lrg_consumer_remove_requirement:
 * @self: an #LrgConsumer
 * @resource: the #LrgResource to stop consuming
 *
 * Removes a resource consumption requirement.
 *
 * Returns: %TRUE if the requirement was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consumer_remove_requirement (LrgConsumer *self,
                                 LrgResource *resource);

/**
 * lrg_consumer_get_requirement_rate:
 * @self: an #LrgConsumer
 * @resource: the #LrgResource to query
 *
 * Gets the consumption rate for a resource.
 *
 * Returns: consumption rate per second, or 0 if not a requirement
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_consumer_get_requirement_rate (LrgConsumer *self,
                                   LrgResource *resource);

/**
 * lrg_consumer_get_requirements:
 * @self: an #LrgConsumer
 *
 * Gets the list of required resources.
 *
 * Returns: (transfer container) (element-type LrgResource): list of requirements
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_consumer_get_requirements (LrgConsumer *self);

/**
 * lrg_consumer_get_requirement_count:
 * @self: an #LrgConsumer
 *
 * Gets the number of resource requirements.
 *
 * Returns: number of requirements
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_consumer_get_requirement_count (LrgConsumer *self);

/**
 * lrg_consumer_clear_requirements:
 * @self: an #LrgConsumer
 *
 * Removes all resource requirements.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consumer_clear_requirements (LrgConsumer *self);

/* State */

/**
 * lrg_consumer_is_starved:
 * @self: an #LrgConsumer
 *
 * Checks if any requirement is not being met.
 *
 * Returns: %TRUE if starved
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consumer_is_starved (LrgConsumer *self);

/**
 * lrg_consumer_is_resource_starved:
 * @self: an #LrgConsumer
 * @resource: the #LrgResource to check
 *
 * Checks if a specific resource requirement is not being met.
 *
 * Returns: %TRUE if starved for this resource
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consumer_is_resource_starved (LrgConsumer *self,
                                  LrgResource *resource);

/**
 * lrg_consumer_get_satisfaction:
 * @self: an #LrgConsumer
 *
 * Gets the overall satisfaction level (0.0 = all starved, 1.0 = all met).
 *
 * Returns: satisfaction level from 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_consumer_get_satisfaction (LrgConsumer *self);

/**
 * lrg_consumer_get_time_until_starved:
 * @self: an #LrgConsumer
 * @resource: the #LrgResource to check
 *
 * Calculates how long until a resource runs out at current consumption rate.
 *
 * Returns: time in seconds, or G_MAXDOUBLE if not consuming this resource
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_consumer_get_time_until_starved (LrgConsumer *self,
                                     LrgResource *resource);

G_END_DECLS
