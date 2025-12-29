/* lrg-idle-calculator.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIdleCalculator - Offline progress simulation for idle games.
 *
 * Tracks generators and simulates time passage to calculate
 * accumulated resources during offline periods.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-number.h"

G_BEGIN_DECLS

#define LRG_TYPE_IDLE_CALCULATOR (lrg_idle_calculator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIdleCalculator, lrg_idle_calculator, LRG, IDLE_CALCULATOR, GObject)

/**
 * LrgIdleGenerator:
 * @id: Unique identifier
 * @base_rate: Base production rate per second
 * @count: Number of generators owned
 * @multiplier: Additional multiplier
 * @enabled: Whether generator is currently producing
 *
 * A boxed type representing a generator that produces resources over time.
 */
typedef struct _LrgIdleGenerator LrgIdleGenerator;

struct _LrgIdleGenerator
{
    gchar        *id;
    LrgBigNumber *base_rate;
    gint64        count;
    gdouble       multiplier;
    gboolean      enabled;
};

#define LRG_TYPE_IDLE_GENERATOR (lrg_idle_generator_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_idle_generator_get_type (void) G_GNUC_CONST;

/* Generator construction */

/**
 * lrg_idle_generator_new:
 * @id: Unique identifier for this generator
 * @base_rate: Base production rate per second
 *
 * Creates a new idle generator.
 *
 * Returns: (transfer full): A new #LrgIdleGenerator
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleGenerator *
lrg_idle_generator_new (const gchar        *id,
                        const LrgBigNumber *base_rate);

/**
 * lrg_idle_generator_new_simple:
 * @id: Unique identifier
 * @base_rate: Base rate as double
 *
 * Creates a new generator with simple numeric rate.
 *
 * Returns: (transfer full): A new #LrgIdleGenerator
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleGenerator *
lrg_idle_generator_new_simple (const gchar *id,
                               gdouble      base_rate);

/**
 * lrg_idle_generator_copy:
 * @self: an #LrgIdleGenerator
 *
 * Creates a deep copy of a generator.
 *
 * Returns: (transfer full): A copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleGenerator *
lrg_idle_generator_copy (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_free:
 * @self: an #LrgIdleGenerator
 *
 * Frees a generator.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_generator_free (LrgIdleGenerator *self);

/* Generator accessors */

/**
 * lrg_idle_generator_get_id:
 * @self: an #LrgIdleGenerator
 *
 * Gets the generator ID.
 *
 * Returns: (transfer none): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_idle_generator_get_id (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_get_base_rate:
 * @self: an #LrgIdleGenerator
 *
 * Gets the base production rate per second.
 *
 * Returns: (transfer none): The base rate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_idle_generator_get_base_rate (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_get_count:
 * @self: an #LrgIdleGenerator
 *
 * Gets how many of this generator are owned.
 *
 * Returns: The count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_idle_generator_get_count (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_set_count:
 * @self: an #LrgIdleGenerator
 * @count: New count
 *
 * Sets how many of this generator are owned.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_generator_set_count (LrgIdleGenerator *self,
                              gint64            count);

/**
 * lrg_idle_generator_get_multiplier:
 * @self: an #LrgIdleGenerator
 *
 * Gets the multiplier applied to this generator.
 *
 * Returns: The multiplier
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_generator_get_multiplier (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_set_multiplier:
 * @self: an #LrgIdleGenerator
 * @multiplier: New multiplier
 *
 * Sets the multiplier for this generator.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_generator_set_multiplier (LrgIdleGenerator *self,
                                   gdouble           multiplier);

/**
 * lrg_idle_generator_is_enabled:
 * @self: an #LrgIdleGenerator
 *
 * Checks if the generator is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_idle_generator_is_enabled (const LrgIdleGenerator *self);

/**
 * lrg_idle_generator_set_enabled:
 * @self: an #LrgIdleGenerator
 * @enabled: Whether to enable
 *
 * Enables or disables the generator.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_generator_set_enabled (LrgIdleGenerator *self,
                                gboolean          enabled);

/**
 * lrg_idle_generator_get_effective_rate:
 * @self: an #LrgIdleGenerator
 *
 * Gets the effective rate (base_rate * count * multiplier).
 *
 * Returns: (transfer full): The effective rate per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_generator_get_effective_rate (const LrgIdleGenerator *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgIdleGenerator, lrg_idle_generator_free)

/* Calculator construction */

/**
 * lrg_idle_calculator_new:
 *
 * Creates a new idle calculator.
 *
 * Returns: (transfer full): A new #LrgIdleCalculator
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleCalculator *
lrg_idle_calculator_new (void);

/* Generator management */

/**
 * lrg_idle_calculator_add_generator:
 * @self: an #LrgIdleCalculator
 * @generator: (transfer none): Generator to add
 *
 * Adds a generator to the calculator. The calculator takes a copy.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_calculator_add_generator (LrgIdleCalculator      *self,
                                   const LrgIdleGenerator *generator);

/**
 * lrg_idle_calculator_remove_generator:
 * @self: an #LrgIdleCalculator
 * @id: Generator ID to remove
 *
 * Removes a generator by ID.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_idle_calculator_remove_generator (LrgIdleCalculator *self,
                                      const gchar       *id);

/**
 * lrg_idle_calculator_get_generator:
 * @self: an #LrgIdleCalculator
 * @id: Generator ID to find
 *
 * Gets a generator by ID.
 *
 * Returns: (transfer none) (nullable): The generator, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleGenerator *
lrg_idle_calculator_get_generator (LrgIdleCalculator *self,
                                   const gchar       *id);

/**
 * lrg_idle_calculator_get_generators:
 * @self: an #LrgIdleCalculator
 *
 * Gets all generators.
 *
 * Returns: (transfer none) (element-type LrgIdleGenerator): Array of generators
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_idle_calculator_get_generators (LrgIdleCalculator *self);

/* Global multiplier */

/**
 * lrg_idle_calculator_get_global_multiplier:
 * @self: an #LrgIdleCalculator
 *
 * Gets the global multiplier applied to all production.
 *
 * Returns: The global multiplier
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_calculator_get_global_multiplier (LrgIdleCalculator *self);

/**
 * lrg_idle_calculator_set_global_multiplier:
 * @self: an #LrgIdleCalculator
 * @multiplier: New global multiplier
 *
 * Sets the global multiplier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_calculator_set_global_multiplier (LrgIdleCalculator *self,
                                           gdouble            multiplier);

/* Rate calculation */

/**
 * lrg_idle_calculator_get_total_rate:
 * @self: an #LrgIdleCalculator
 *
 * Gets the total production rate per second from all generators.
 *
 * Returns: (transfer full): Total rate per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_calculator_get_total_rate (LrgIdleCalculator *self);

/* Simulation */

/**
 * lrg_idle_calculator_simulate:
 * @self: an #LrgIdleCalculator
 * @seconds: Number of seconds to simulate
 *
 * Simulates time passage and returns accumulated production.
 *
 * Returns: (transfer full): Total production over the time period
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_calculator_simulate (LrgIdleCalculator *self,
                              gdouble            seconds);

/**
 * lrg_idle_calculator_simulate_offline:
 * @self: an #LrgIdleCalculator
 * @last_active_time: Unix timestamp of last activity
 * @efficiency: Offline efficiency (0.0 to 1.0)
 * @max_hours: Maximum hours to calculate (0 = unlimited)
 *
 * Calculates production during an offline period.
 *
 * Returns: (transfer full): Total offline production
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_calculator_simulate_offline (LrgIdleCalculator *self,
                                      gint64             last_active_time,
                                      gdouble            efficiency,
                                      gdouble            max_hours);

/* Snapshot for save/load */

/**
 * lrg_idle_calculator_take_snapshot:
 * @self: an #LrgIdleCalculator
 *
 * Records the current time for later offline calculation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_calculator_take_snapshot (LrgIdleCalculator *self);

/**
 * lrg_idle_calculator_get_snapshot_time:
 * @self: an #LrgIdleCalculator
 *
 * Gets the last snapshot timestamp.
 *
 * Returns: Unix timestamp of last snapshot
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_idle_calculator_get_snapshot_time (LrgIdleCalculator *self);

/**
 * lrg_idle_calculator_set_snapshot_time:
 * @self: an #LrgIdleCalculator
 * @time: Unix timestamp
 *
 * Sets the snapshot time (for save/load).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_calculator_set_snapshot_time (LrgIdleCalculator *self,
                                       gint64             time);

G_END_DECLS
