/* lrg-achievement-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementManager - Singleton manager for achievements.
 *
 * Central manager for achievement registration, tracking, and persistence.
 * Implements LrgSaveable for save/load integration.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-achievement.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACHIEVEMENT_MANAGER (lrg_achievement_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAchievementManager, lrg_achievement_manager, LRG, ACHIEVEMENT_MANAGER, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_achievement_manager_get_default:
 *
 * Gets the default achievement manager instance.
 *
 * Returns: (transfer none): the default #LrgAchievementManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementManager *
lrg_achievement_manager_get_default (void);

/* ==========================================================================
 * Achievement Registration
 * ========================================================================== */

/**
 * lrg_achievement_manager_register:
 * @self: an #LrgAchievementManager
 * @achievement: (transfer full): the #LrgAchievement to register
 *
 * Registers an achievement with the manager.
 *
 * The manager takes ownership of the achievement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_register (LrgAchievementManager *self,
                                  LrgAchievement        *achievement);

/**
 * lrg_achievement_manager_unregister:
 * @self: an #LrgAchievementManager
 * @id: achievement ID to unregister
 *
 * Unregisters an achievement.
 *
 * Returns: %TRUE if found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_manager_unregister (LrgAchievementManager *self,
                                    const gchar           *id);

/**
 * lrg_achievement_manager_get:
 * @self: an #LrgAchievementManager
 * @id: achievement ID
 *
 * Gets an achievement by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgAchievement, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievement *
lrg_achievement_manager_get (LrgAchievementManager *self,
                             const gchar           *id);

/**
 * lrg_achievement_manager_get_all:
 * @self: an #LrgAchievementManager
 *
 * Gets all registered achievements.
 *
 * Returns: (transfer container) (element-type LrgAchievement): list of achievements
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_achievement_manager_get_all (LrgAchievementManager *self);

/**
 * lrg_achievement_manager_get_count:
 * @self: an #LrgAchievementManager
 *
 * Gets the number of registered achievements.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_manager_get_count (LrgAchievementManager *self);

/* ==========================================================================
 * Achievement State
 * ========================================================================== */

/**
 * lrg_achievement_manager_unlock:
 * @self: an #LrgAchievementManager
 * @id: achievement ID
 *
 * Unlocks an achievement by ID.
 *
 * Returns: %TRUE if newly unlocked, %FALSE if already unlocked or not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_manager_unlock (LrgAchievementManager *self,
                                const gchar           *id);

/**
 * lrg_achievement_manager_increment_progress:
 * @self: an #LrgAchievementManager
 * @id: achievement ID
 * @amount: amount to add
 *
 * Increments progress for an achievement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_increment_progress (LrgAchievementManager *self,
                                            const gchar           *id,
                                            gint64                 amount);

/**
 * lrg_achievement_manager_set_progress:
 * @self: an #LrgAchievementManager
 * @id: achievement ID
 * @value: the new progress value
 *
 * Sets progress for an achievement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_set_progress (LrgAchievementManager *self,
                                      const gchar           *id,
                                      gint64                 value);

/**
 * lrg_achievement_manager_is_unlocked:
 * @self: an #LrgAchievementManager
 * @id: achievement ID
 *
 * Checks if an achievement is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_manager_is_unlocked (LrgAchievementManager *self,
                                     const gchar           *id);

/* ==========================================================================
 * Statistics
 * ========================================================================== */

/**
 * lrg_achievement_manager_get_unlocked_count:
 * @self: an #LrgAchievementManager
 *
 * Gets the number of unlocked achievements.
 *
 * Returns: the unlocked count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_manager_get_unlocked_count (LrgAchievementManager *self);

/**
 * lrg_achievement_manager_get_total_points:
 * @self: an #LrgAchievementManager
 *
 * Gets total points from all achievements.
 *
 * Returns: total possible points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_manager_get_total_points (LrgAchievementManager *self);

/**
 * lrg_achievement_manager_get_earned_points:
 * @self: an #LrgAchievementManager
 *
 * Gets points earned from unlocked achievements.
 *
 * Returns: earned points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_manager_get_earned_points (LrgAchievementManager *self);

/**
 * lrg_achievement_manager_get_completion_percentage:
 * @self: an #LrgAchievementManager
 *
 * Gets completion percentage (0.0 to 1.0).
 *
 * Returns: completion percentage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_manager_get_completion_percentage (LrgAchievementManager *self);

/* ==========================================================================
 * Local Statistics
 * ========================================================================== */

/**
 * lrg_achievement_manager_set_stat_int:
 * @self: an #LrgAchievementManager
 * @name: statistic name
 * @value: the value
 *
 * Sets an integer statistic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_set_stat_int (LrgAchievementManager *self,
                                      const gchar           *name,
                                      gint64                 value);

/**
 * lrg_achievement_manager_get_stat_int:
 * @self: an #LrgAchievementManager
 * @name: statistic name
 *
 * Gets an integer statistic.
 *
 * Returns: the value, or 0 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_achievement_manager_get_stat_int (LrgAchievementManager *self,
                                      const gchar           *name);

/**
 * lrg_achievement_manager_increment_stat:
 * @self: an #LrgAchievementManager
 * @name: statistic name
 * @amount: amount to add
 *
 * Increments an integer statistic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_increment_stat (LrgAchievementManager *self,
                                        const gchar           *name,
                                        gint64                 amount);

/**
 * lrg_achievement_manager_set_stat_float:
 * @self: an #LrgAchievementManager
 * @name: statistic name
 * @value: the value
 *
 * Sets a float statistic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_set_stat_float (LrgAchievementManager *self,
                                        const gchar           *name,
                                        gdouble                value);

/**
 * lrg_achievement_manager_get_stat_float:
 * @self: an #LrgAchievementManager
 * @name: statistic name
 *
 * Gets a float statistic.
 *
 * Returns: the value, or 0.0 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_manager_get_stat_float (LrgAchievementManager *self,
                                        const gchar           *name);

/* ==========================================================================
 * Reset
 * ========================================================================== */

/**
 * lrg_achievement_manager_reset_all:
 * @self: an #LrgAchievementManager
 *
 * Resets all achievement progress and unlocks.
 *
 * Use for development/testing only.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_reset_all (LrgAchievementManager *self);

/**
 * lrg_achievement_manager_reset_stats:
 * @self: an #LrgAchievementManager
 *
 * Resets all statistics.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_manager_reset_stats (LrgAchievementManager *self);

G_END_DECLS
