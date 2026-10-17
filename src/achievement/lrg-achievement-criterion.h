/* lrg-achievement-criterion.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementCriterion - one step of an achievement with its own
 * progress ("win 10 fights", "visit 25 different towns").
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACHIEVEMENT_CRITERION (lrg_achievement_criterion_get_type ())

/**
 * LrgAchievementCriterion:
 *
 * One criterion of an #LrgAchievement: an id unique within its
 * achievement, a description, a target and the progress towards it.
 * The progress is a count (lrg_achievement_criterion_increment()), a
 * value (lrg_achievement_criterion_set_current(), clamped to 0 ..
 * target) or a number of distinct keys
 * (lrg_achievement_criterion_add_key(): the same town twice counts
 * once).  An achievement with criteria unlocks when every criterion is
 * complete.
 *
 * Reference counted; the achievement holds a reference to each of its
 * criteria.
 *
 * Since: 1.0
 */
typedef struct _LrgAchievementCriterion LrgAchievementCriterion;

LRG_AVAILABLE_IN_ALL
GType lrg_achievement_criterion_get_type (void) G_GNUC_CONST;

/**
 * lrg_achievement_criterion_new:
 * @id: the criterion's id within its achievement
 * @description: (nullable): what the player does
 * @target: the progress that completes it (at least 1)
 *
 * Returns: (transfer full): a new criterion at 0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementCriterion *
lrg_achievement_criterion_new (const gchar *id,
                               const gchar *description,
                               gint64       target);

/**
 * lrg_achievement_criterion_ref:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: (transfer full): @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementCriterion *
lrg_achievement_criterion_ref (LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_unref:
 * @self: (transfer full): an #LrgAchievementCriterion
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_criterion_unref (LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_get_id:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: (transfer none): the id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_criterion_get_id (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_get_description:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_criterion_get_description (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_get_target:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: the target
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_achievement_criterion_get_target (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_get_current:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: the progress, 0 .. the target
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_achievement_criterion_get_current (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_set_current:
 * @self: an #LrgAchievementCriterion
 * @value: the progress (clamped to 0 .. the target)
 *
 * Returns: %TRUE when the progress changed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_criterion_set_current (LrgAchievementCriterion *self,
                                       gint64                   value);

/**
 * lrg_achievement_criterion_increment:
 * @self: an #LrgAchievementCriterion
 * @amount: added to the progress (may be negative; the result is
 *   clamped)
 *
 * Returns: %TRUE when the progress changed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_criterion_increment (LrgAchievementCriterion *self,
                                     gint64                   amount);

/**
 * lrg_achievement_criterion_add_key:
 * @self: an #LrgAchievementCriterion
 * @key: a distinct thing done ("Daggerfall", "S0000016")
 *
 * Counts @key once: the first time it is seen the progress rises by
 * one (up to the target; keys past the target are still remembered).
 *
 * Returns: %TRUE when @key was new
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_criterion_add_key (LrgAchievementCriterion *self,
                                   const gchar             *key);

/**
 * lrg_achievement_criterion_has_key:
 * @self: an #LrgAchievementCriterion
 * @key: a key
 *
 * Returns: %TRUE when @key was counted
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_criterion_has_key (const LrgAchievementCriterion *self,
                                   const gchar                   *key);

/**
 * lrg_achievement_criterion_get_keys:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: (transfer full) (array zero-terminated=1): the keys counted,
 *   in the order they were first seen
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_achievement_criterion_get_keys (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_is_complete:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: %TRUE when the progress reached the target
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_criterion_is_complete (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_get_fraction:
 * @self: an #LrgAchievementCriterion
 *
 * Returns: the progress as 0.0 .. 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_criterion_get_fraction (const LrgAchievementCriterion *self);

/**
 * lrg_achievement_criterion_reset:
 * @self: an #LrgAchievementCriterion
 *
 * Back to 0, the keys forgotten.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_criterion_reset (LrgAchievementCriterion *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAchievementCriterion, lrg_achievement_criterion_unref)

G_END_DECLS
