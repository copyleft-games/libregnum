/* lrg-achievement.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievement - Base class for achievement definitions.
 *
 * Derivable type that defines an achievement with optional
 * custom unlock logic via the check_unlock virtual method.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-achievement-progress.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACHIEVEMENT (lrg_achievement_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAchievement, lrg_achievement, LRG, ACHIEVEMENT, GObject)

/**
 * LrgAchievementClass:
 * @parent_class: parent class
 * @check_unlock: virtual method to check if achievement should unlock
 * @on_unlocked: virtual method called when achievement unlocks
 *
 * Class structure for #LrgAchievement.
 *
 * Subclasses can override check_unlock to implement custom unlock logic.
 *
 * Since: 1.0
 */
struct _LrgAchievementClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgAchievementClass::check_unlock:
     * @self: the achievement
     *
     * Checks if the achievement should be unlocked.
     *
     * The default implementation returns %TRUE if progress is complete.
     * Subclasses can override for custom conditions.
     *
     * Returns: %TRUE if should unlock
     */
    gboolean (*check_unlock) (LrgAchievement *self);

    /**
     * LrgAchievementClass::on_unlocked:
     * @self: the achievement
     *
     * Called when the achievement is unlocked.
     *
     * Subclasses can override to perform custom actions.
     */
    void (*on_unlocked) (LrgAchievement *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_achievement_new:
 * @id: unique achievement identifier
 * @name: display name
 * @description: achievement description
 *
 * Creates a new achievement definition.
 *
 * Returns: (transfer full): a new #LrgAchievement
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievement *
lrg_achievement_new (const gchar *id,
                     const gchar *name,
                     const gchar *description);

/**
 * lrg_achievement_new_with_progress:
 * @id: unique achievement identifier
 * @name: display name
 * @description: achievement description
 * @target: target progress value
 *
 * Creates a new achievement with progress tracking.
 *
 * Returns: (transfer full): a new #LrgAchievement
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievement *
lrg_achievement_new_with_progress (const gchar *id,
                                   const gchar *name,
                                   const gchar *description,
                                   gint64       target);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_achievement_get_id:
 * @self: an #LrgAchievement
 *
 * Gets the unique achievement ID.
 *
 * Returns: (transfer none): the achievement ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_get_id (LrgAchievement *self);

/**
 * lrg_achievement_get_name:
 * @self: an #LrgAchievement
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the display name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_get_name (LrgAchievement *self);

/**
 * lrg_achievement_get_description:
 * @self: an #LrgAchievement
 *
 * Gets the description.
 *
 * Returns: (transfer none): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_get_description (LrgAchievement *self);

/**
 * lrg_achievement_get_icon:
 * @self: an #LrgAchievement
 *
 * Gets the icon path.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_get_icon (LrgAchievement *self);

/**
 * lrg_achievement_set_icon:
 * @self: an #LrgAchievement
 * @icon: (nullable): path to icon
 *
 * Sets the icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_set_icon (LrgAchievement *self,
                          const gchar    *icon);

/**
 * lrg_achievement_get_locked_icon:
 * @self: an #LrgAchievement
 *
 * Gets the locked icon path.
 *
 * Returns: (transfer none) (nullable): the locked icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_get_locked_icon (LrgAchievement *self);

/**
 * lrg_achievement_set_locked_icon:
 * @self: an #LrgAchievement
 * @icon: (nullable): path to locked icon
 *
 * Sets the locked icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_set_locked_icon (LrgAchievement *self,
                                 const gchar    *icon);

/**
 * lrg_achievement_is_hidden:
 * @self: an #LrgAchievement
 *
 * Checks if this is a hidden achievement.
 *
 * Returns: %TRUE if hidden until unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_is_hidden (LrgAchievement *self);

/**
 * lrg_achievement_set_hidden:
 * @self: an #LrgAchievement
 * @hidden: whether to hide
 *
 * Sets whether this achievement is hidden.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_set_hidden (LrgAchievement *self,
                            gboolean        hidden);

/**
 * lrg_achievement_get_points:
 * @self: an #LrgAchievement
 *
 * Gets the point value for this achievement.
 *
 * Returns: the point value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_get_points (LrgAchievement *self);

/**
 * lrg_achievement_set_points:
 * @self: an #LrgAchievement
 * @points: point value
 *
 * Sets the point value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_set_points (LrgAchievement *self,
                            guint           points);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_achievement_is_unlocked:
 * @self: an #LrgAchievement
 *
 * Checks if the achievement is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_is_unlocked (LrgAchievement *self);

/**
 * lrg_achievement_get_unlock_time:
 * @self: an #LrgAchievement
 *
 * Gets the time when this achievement was unlocked.
 *
 * Returns: (transfer none) (nullable): the unlock time, or %NULL if not unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GDateTime *
lrg_achievement_get_unlock_time (LrgAchievement *self);

/**
 * lrg_achievement_unlock:
 * @self: an #LrgAchievement
 *
 * Unlocks the achievement.
 *
 * Emits the "unlocked" signal if not already unlocked.
 *
 * Returns: %TRUE if newly unlocked, %FALSE if already unlocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_unlock (LrgAchievement *self);

/**
 * lrg_achievement_lock:
 * @self: an #LrgAchievement
 *
 * Locks the achievement (for testing/development).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_lock (LrgAchievement *self);

/* ==========================================================================
 * Progress
 * ========================================================================== */

/**
 * lrg_achievement_get_progress:
 * @self: an #LrgAchievement
 *
 * Gets the progress tracking data.
 *
 * Returns: (transfer none) (nullable): the progress, or %NULL if no progress tracking
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementProgress *
lrg_achievement_get_progress (LrgAchievement *self);

/**
 * lrg_achievement_has_progress:
 * @self: an #LrgAchievement
 *
 * Checks if this achievement has progress tracking.
 *
 * Returns: %TRUE if progress is tracked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_has_progress (LrgAchievement *self);

/**
 * lrg_achievement_set_progress_value:
 * @self: an #LrgAchievement
 * @value: the new progress value
 *
 * Sets the current progress value.
 *
 * May trigger unlock if progress completes and check_unlock returns %TRUE.
 * Emits "progress-changed" signal.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_set_progress_value (LrgAchievement *self,
                                    gint64          value);

/**
 * lrg_achievement_increment_progress:
 * @self: an #LrgAchievement
 * @amount: amount to add
 *
 * Increments the progress value.
 *
 * May trigger unlock if progress completes.
 * Emits "progress-changed" signal.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_increment_progress (LrgAchievement *self,
                                    gint64          amount);

/* ==========================================================================
 * Virtual Methods
 * ========================================================================== */

/**
 * lrg_achievement_check_unlock:
 * @self: an #LrgAchievement
 *
 * Checks if the achievement should be unlocked.
 *
 * Calls the virtual method, which by default checks if progress is complete.
 *
 * Returns: %TRUE if should unlock
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_check_unlock (LrgAchievement *self);

G_END_DECLS
