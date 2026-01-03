/* lrg-template-statistics.h - Game statistics tracking system
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_STATISTICS_H
#define LRG_TEMPLATE_STATISTICS_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_STATISTICS (lrg_template_statistics_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTemplateStatistics, lrg_template_statistics,
                      LRG, TEMPLATE_STATISTICS, GObject)

/**
 * SECTION:lrg-template-statistics
 * @title: LrgTemplateStatistics
 * @short_description: Game statistics tracking system
 *
 * #LrgTemplateStatistics provides a flexible system for tracking game
 * statistics like kill counts, high scores, play time, and achievements
 * progress. It supports different stat types:
 *
 * - **Counters**: Incrementable values (enemies defeated, items collected)
 * - **Maximums**: Tracks highest value (high score, longest combo)
 * - **Minimums**: Tracks lowest value (fastest time, fewest deaths)
 * - **Timers**: Accumulated time tracking (total play time, time in level)
 *
 * The statistics system implements #LrgSaveable for persistence.
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * LrgTemplateStatistics *stats = lrg_template_statistics_new ("player-stats");
 *
 * // Track a counter
 * lrg_template_statistics_track_counter (stats, "enemies_defeated", 1);
 * lrg_template_statistics_track_counter (stats, "enemies_defeated", 1);
 *
 * // Track a maximum (high score)
 * lrg_template_statistics_track_maximum (stats, "high_score", 15000.0);
 *
 * // Track a minimum (fastest time)
 * lrg_template_statistics_track_minimum (stats, "fastest_level_1", 45.7);
 *
 * // Track time
 * lrg_template_statistics_timer_start (stats, "session_time");
 * // ... gameplay ...
 * lrg_template_statistics_timer_stop (stats, "session_time");
 *
 * // Query stats
 * gint64 kills = lrg_template_statistics_get_counter (stats, "enemies_defeated");
 * gdouble high = lrg_template_statistics_get_maximum (stats, "high_score");
 * ]|
 */

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_statistics_new:
 * @save_id: unique identifier for save/load operations
 *
 * Creates a new statistics tracker.
 *
 * The @save_id should be unique among all saveable objects and stable
 * across application runs.
 *
 * Returns: (transfer full): A new #LrgTemplateStatistics
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateStatistics *
lrg_template_statistics_new (const gchar *save_id);

/* ==========================================================================
 * Counter Statistics
 * ========================================================================== */

/**
 * lrg_template_statistics_track_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @increment: value to add (can be negative)
 *
 * Increments a counter statistic by the given amount.
 *
 * If the statistic doesn't exist, it is created with an initial value
 * of @increment.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_track_counter (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gint64                 increment);

/**
 * lrg_template_statistics_get_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets the current value of a counter statistic.
 *
 * Returns: The counter value, or 0 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_template_statistics_get_counter (LrgTemplateStatistics *self,
                                      const gchar           *name);

/**
 * lrg_template_statistics_set_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the new value
 *
 * Sets a counter statistic to an absolute value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_set_counter (LrgTemplateStatistics *self,
                                      const gchar           *name,
                                      gint64                 value);

/* ==========================================================================
 * Maximum Statistics
 * ========================================================================== */

/**
 * lrg_template_statistics_track_maximum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the value to compare
 *
 * Updates a maximum statistic if @value exceeds the current value.
 *
 * Use this for high scores, longest combos, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_track_maximum (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gdouble                value);

/**
 * lrg_template_statistics_get_maximum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets the maximum recorded value for a statistic.
 *
 * Returns: The maximum value, or -G_MAXDOUBLE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_statistics_get_maximum (LrgTemplateStatistics *self,
                                      const gchar           *name);

/* ==========================================================================
 * Minimum Statistics
 * ========================================================================== */

/**
 * lrg_template_statistics_track_minimum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the value to compare
 *
 * Updates a minimum statistic if @value is lower than the current value.
 *
 * Use this for fastest times, fewest deaths, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_track_minimum (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gdouble                value);

/**
 * lrg_template_statistics_get_minimum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets the minimum recorded value for a statistic.
 *
 * Returns: The minimum value, or G_MAXDOUBLE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_statistics_get_minimum (LrgTemplateStatistics *self,
                                      const gchar           *name);

/* ==========================================================================
 * Timer Statistics
 * ========================================================================== */

/**
 * lrg_template_statistics_timer_start:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Starts or resumes a timer statistic.
 *
 * If the timer is already running, this has no effect.
 * Use for tracking cumulative time like total play time.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_timer_start (LrgTemplateStatistics *self,
                                      const gchar           *name);

/**
 * lrg_template_statistics_timer_stop:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Stops a timer and accumulates the elapsed time.
 *
 * If the timer is not running, this has no effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_timer_stop (LrgTemplateStatistics *self,
                                     const gchar           *name);

/**
 * lrg_template_statistics_timer_reset:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Resets a timer to zero and stops it if running.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_timer_reset (LrgTemplateStatistics *self,
                                      const gchar           *name);

/**
 * lrg_template_statistics_get_timer:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Gets the total accumulated time for a timer in seconds.
 *
 * If the timer is currently running, includes elapsed time since start.
 *
 * Returns: The accumulated time in seconds, or 0.0 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_statistics_get_timer (LrgTemplateStatistics *self,
                                    const gchar           *name);

/**
 * lrg_template_statistics_is_timer_running:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Checks if a timer is currently running.
 *
 * Returns: %TRUE if the timer is running
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_statistics_is_timer_running (LrgTemplateStatistics *self,
                                           const gchar           *name);

/* ==========================================================================
 * Utility Methods
 * ========================================================================== */

/**
 * lrg_template_statistics_has_stat:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Checks if a statistic exists.
 *
 * Returns: %TRUE if the statistic exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_statistics_has_stat (LrgTemplateStatistics *self,
                                   const gchar           *name);

/**
 * lrg_template_statistics_remove_stat:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Removes a statistic of any type.
 *
 * Returns: %TRUE if the statistic was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_statistics_remove_stat (LrgTemplateStatistics *self,
                                      const gchar           *name);

/**
 * lrg_template_statistics_clear_all:
 * @self: an #LrgTemplateStatistics
 *
 * Removes all statistics.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_statistics_clear_all (LrgTemplateStatistics *self);

/**
 * lrg_template_statistics_get_all_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the names of all tracked statistics.
 *
 * Returns: (transfer full) (element-type utf8): A list of stat names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_template_statistics_get_all_names (LrgTemplateStatistics *self);

/**
 * lrg_template_statistics_get_counter_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the names of all counter statistics.
 *
 * Returns: (transfer full) (element-type utf8): A list of counter names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_template_statistics_get_counter_names (LrgTemplateStatistics *self);

/**
 * lrg_template_statistics_get_maximum_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the names of all maximum statistics.
 *
 * Returns: (transfer full) (element-type utf8): A list of maximum stat names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_template_statistics_get_maximum_names (LrgTemplateStatistics *self);

/**
 * lrg_template_statistics_get_minimum_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the names of all minimum statistics.
 *
 * Returns: (transfer full) (element-type utf8): A list of minimum stat names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_template_statistics_get_minimum_names (LrgTemplateStatistics *self);

/**
 * lrg_template_statistics_get_timer_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the names of all timer statistics.
 *
 * Returns: (transfer full) (element-type utf8): A list of timer names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_template_statistics_get_timer_names (LrgTemplateStatistics *self);

/* ==========================================================================
 * Save ID
 * ========================================================================== */

/**
 * lrg_template_statistics_get_save_id:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the save identifier for this statistics tracker.
 *
 * Returns: (transfer none): The save ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_statistics_get_id (LrgTemplateStatistics *self);

G_END_DECLS

#endif /* LRG_TEMPLATE_STATISTICS_H */
