/* lrg-achievement-progress.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementProgress - Boxed type for achievement progress tracking.
 *
 * Tracks current progress toward an achievement goal.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACHIEVEMENT_PROGRESS (lrg_achievement_progress_get_type ())

/**
 * LrgAchievementProgress:
 *
 * Tracks progress toward an achievement goal.
 *
 * This is a boxed type that stores the current progress value
 * and the target value required for unlock.
 *
 * Since: 1.0
 */
typedef struct _LrgAchievementProgress LrgAchievementProgress;

LRG_AVAILABLE_IN_ALL
GType lrg_achievement_progress_get_type (void) G_GNUC_CONST;

/**
 * lrg_achievement_progress_new:
 * @current: current progress value
 * @target: target value for completion
 *
 * Creates a new achievement progress instance.
 *
 * Returns: (transfer full): a new #LrgAchievementProgress
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementProgress *
lrg_achievement_progress_new (gint64 current,
                              gint64 target);

/**
 * lrg_achievement_progress_copy:
 * @self: an #LrgAchievementProgress
 *
 * Creates a copy of the progress.
 *
 * Returns: (transfer full): a new #LrgAchievementProgress
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementProgress *
lrg_achievement_progress_copy (const LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_free:
 * @self: an #LrgAchievementProgress
 *
 * Frees the progress instance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_progress_free (LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_get_current:
 * @self: an #LrgAchievementProgress
 *
 * Gets the current progress value.
 *
 * Returns: the current progress
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_achievement_progress_get_current (const LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_set_current:
 * @self: an #LrgAchievementProgress
 * @current: the new current value
 *
 * Sets the current progress value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_progress_set_current (LrgAchievementProgress *self,
                                      gint64                  current);

/**
 * lrg_achievement_progress_get_target:
 * @self: an #LrgAchievementProgress
 *
 * Gets the target progress value.
 *
 * Returns: the target value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_achievement_progress_get_target (const LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_set_target:
 * @self: an #LrgAchievementProgress
 * @target: the new target value
 *
 * Sets the target progress value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_progress_set_target (LrgAchievementProgress *self,
                                     gint64                  target);

/**
 * lrg_achievement_progress_is_complete:
 * @self: an #LrgAchievementProgress
 *
 * Checks if progress has reached the target.
 *
 * Returns: %TRUE if current >= target
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_progress_is_complete (const LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_get_percentage:
 * @self: an #LrgAchievementProgress
 *
 * Gets the progress as a percentage (0.0 to 1.0).
 *
 * Returns: progress percentage, clamped to 0.0-1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_progress_get_percentage (const LrgAchievementProgress *self);

/**
 * lrg_achievement_progress_increment:
 * @self: an #LrgAchievementProgress
 * @amount: amount to add
 *
 * Increments the current progress value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_progress_increment (LrgAchievementProgress *self,
                                    gint64                  amount);

/**
 * lrg_achievement_progress_reset:
 * @self: an #LrgAchievementProgress
 *
 * Resets current progress to 0.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_progress_reset (LrgAchievementProgress *self);

G_END_DECLS
