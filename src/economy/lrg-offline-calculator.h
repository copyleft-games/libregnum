/* lrg-offline-calculator.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgOfflineCalculator - Calculates offline progress for idle games.
 *
 * The offline calculator simulates time passing while the game is not
 * running. It tracks producers and calculates what resources they would
 * have generated during the offline period.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-resource-pool.h"
#include "lrg-producer.h"

G_BEGIN_DECLS

#define LRG_TYPE_OFFLINE_CALCULATOR (lrg_offline_calculator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgOfflineCalculator, lrg_offline_calculator, LRG, OFFLINE_CALCULATOR, GObject)

/* Construction */

/**
 * lrg_offline_calculator_new:
 *
 * Creates a new offline calculator.
 *
 * Returns: (transfer full): A new #LrgOfflineCalculator
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgOfflineCalculator *
lrg_offline_calculator_new (void);

/* Producer Registration */

/**
 * lrg_offline_calculator_add_producer:
 * @self: an #LrgOfflineCalculator
 * @producer: the #LrgProducer to track
 *
 * Adds a producer to be tracked for offline calculation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_add_producer (LrgOfflineCalculator *self,
                                     LrgProducer          *producer);

/**
 * lrg_offline_calculator_remove_producer:
 * @self: an #LrgOfflineCalculator
 * @producer: the #LrgProducer to remove
 *
 * Removes a producer from tracking.
 *
 * Returns: %TRUE if the producer was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_offline_calculator_remove_producer (LrgOfflineCalculator *self,
                                        LrgProducer          *producer);

/**
 * lrg_offline_calculator_clear_producers:
 * @self: an #LrgOfflineCalculator
 *
 * Removes all tracked producers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_clear_producers (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_get_producer_count:
 * @self: an #LrgOfflineCalculator
 *
 * Gets the number of tracked producers.
 *
 * Returns: number of producers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_offline_calculator_get_producer_count (LrgOfflineCalculator *self);

/* Snapshot */

/**
 * lrg_offline_calculator_take_snapshot:
 * @self: an #LrgOfflineCalculator
 *
 * Takes a snapshot of the current time for later offline calculation.
 * Call this when the game is about to close.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_take_snapshot (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_get_snapshot_time:
 * @self: an #LrgOfflineCalculator
 *
 * Gets the Unix timestamp of the last snapshot.
 *
 * Returns: Unix timestamp, or 0 if no snapshot taken
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_offline_calculator_get_snapshot_time (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_set_snapshot_time:
 * @self: an #LrgOfflineCalculator
 * @timestamp: Unix timestamp
 *
 * Sets the snapshot time manually (for loading from save).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_set_snapshot_time (LrgOfflineCalculator *self,
                                          gint64                timestamp);

/* Calculation */

/**
 * lrg_offline_calculator_calculate:
 * @self: an #LrgOfflineCalculator
 * @result_pool: (out): #LrgResourcePool to store calculated results
 *
 * Calculates resources gained during offline time.
 * Uses current time minus snapshot time as the offline duration.
 *
 * Returns: offline duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_offline_calculator_calculate (LrgOfflineCalculator *self,
                                  LrgResourcePool      *result_pool);

/**
 * lrg_offline_calculator_calculate_duration:
 * @self: an #LrgOfflineCalculator
 * @duration: offline duration in seconds
 * @result_pool: (out): #LrgResourcePool to store calculated results
 *
 * Calculates resources for a specific duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_calculate_duration (LrgOfflineCalculator *self,
                                           gdouble               duration,
                                           LrgResourcePool      *result_pool);

/**
 * lrg_offline_calculator_apply:
 * @self: an #LrgOfflineCalculator
 * @pool: the #LrgResourcePool to add results to
 *
 * Calculates and applies offline progress to a pool.
 * Combines calculate() and adding to pool in one call.
 *
 * Returns: offline duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_offline_calculator_apply (LrgOfflineCalculator *self,
                              LrgResourcePool      *pool);

/* Settings */

/**
 * lrg_offline_calculator_get_efficiency:
 * @self: an #LrgOfflineCalculator
 *
 * Gets the offline efficiency multiplier.
 * A value of 1.0 means 100% of normal production.
 *
 * Returns: efficiency multiplier (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_offline_calculator_get_efficiency (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_set_efficiency:
 * @self: an #LrgOfflineCalculator
 * @efficiency: efficiency multiplier (0.0 to 1.0)
 *
 * Sets the offline efficiency multiplier.
 * Games often give reduced resources during offline time.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_set_efficiency (LrgOfflineCalculator *self,
                                       gdouble               efficiency);

/**
 * lrg_offline_calculator_get_max_hours:
 * @self: an #LrgOfflineCalculator
 *
 * Gets the maximum offline hours to calculate.
 *
 * Returns: maximum hours, or G_MAXDOUBLE for unlimited
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_offline_calculator_get_max_hours (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_set_max_hours:
 * @self: an #LrgOfflineCalculator
 * @max_hours: maximum hours to calculate (use G_MAXDOUBLE for unlimited)
 *
 * Sets the maximum offline hours to calculate.
 * Prevents excessive gains from very long offline periods.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_set_max_hours (LrgOfflineCalculator *self,
                                      gdouble               max_hours);

/**
 * lrg_offline_calculator_get_min_seconds:
 * @self: an #LrgOfflineCalculator
 *
 * Gets the minimum offline seconds before calculation.
 *
 * Returns: minimum seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_offline_calculator_get_min_seconds (LrgOfflineCalculator *self);

/**
 * lrg_offline_calculator_set_min_seconds:
 * @self: an #LrgOfflineCalculator
 * @min_seconds: minimum seconds before offline progress kicks in
 *
 * Sets the minimum offline time before calculating progress.
 * Prevents tiny gains from brief app switches.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_offline_calculator_set_min_seconds (LrgOfflineCalculator *self,
                                        gdouble               min_seconds);

G_END_DECLS
