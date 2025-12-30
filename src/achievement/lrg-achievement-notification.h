/* lrg-achievement-notification.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementNotification - UI popup for achievement unlocks.
 *
 * Displays a notification when an achievement is unlocked,
 * with configurable duration, animation, and display position.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../ui/lrg-container.h"
#include "lrg-achievement.h"

G_BEGIN_DECLS

#define LRG_TYPE_ACHIEVEMENT_NOTIFICATION (lrg_achievement_notification_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAchievementNotification, lrg_achievement_notification, LRG, ACHIEVEMENT_NOTIFICATION, LrgContainer)

/* LrgNotificationPosition enum is defined in lrg-enums.h */

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_achievement_notification_new:
 *
 * Creates a new achievement notification widget.
 *
 * Returns: (transfer full): a new #LrgAchievementNotification
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementNotification *
lrg_achievement_notification_new (void);

/* ==========================================================================
 * Display
 * ========================================================================== */

/**
 * lrg_achievement_notification_show:
 * @self: an #LrgAchievementNotification
 * @achievement: the unlocked achievement
 *
 * Shows the notification for an achievement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_show (LrgAchievementNotification *self,
                                   LrgAchievement             *achievement);

/**
 * lrg_achievement_notification_hide:
 * @self: an #LrgAchievementNotification
 *
 * Immediately hides the notification.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_hide (LrgAchievementNotification *self);

/**
 * lrg_achievement_notification_is_visible:
 * @self: an #LrgAchievementNotification
 *
 * Checks if the notification is currently visible.
 *
 * Returns: %TRUE if visible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_achievement_notification_is_visible (LrgAchievementNotification *self);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_achievement_notification_get_duration:
 * @self: an #LrgAchievementNotification
 *
 * Gets the display duration in seconds.
 *
 * Returns: duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_achievement_notification_get_duration (LrgAchievementNotification *self);

/**
 * lrg_achievement_notification_set_duration:
 * @self: an #LrgAchievementNotification
 * @duration: duration in seconds
 *
 * Sets the display duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_set_duration (LrgAchievementNotification *self,
                                           gfloat                      duration);

/**
 * lrg_achievement_notification_get_position:
 * @self: an #LrgAchievementNotification
 *
 * Gets the screen position.
 *
 * Returns: the #LrgNotificationPosition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgNotificationPosition
lrg_achievement_notification_get_position (LrgAchievementNotification *self);

/**
 * lrg_achievement_notification_set_position:
 * @self: an #LrgAchievementNotification
 * @position: the #LrgNotificationPosition
 *
 * Sets the screen position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_set_position (LrgAchievementNotification *self,
                                           LrgNotificationPosition     position);

/**
 * lrg_achievement_notification_get_fade_duration:
 * @self: an #LrgAchievementNotification
 *
 * Gets the fade in/out duration in seconds.
 *
 * Returns: fade duration
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_achievement_notification_get_fade_duration (LrgAchievementNotification *self);

/**
 * lrg_achievement_notification_set_fade_duration:
 * @self: an #LrgAchievementNotification
 * @duration: fade duration in seconds
 *
 * Sets the fade in/out duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_set_fade_duration (LrgAchievementNotification *self,
                                                gfloat                      duration);

/**
 * lrg_achievement_notification_set_margin:
 * @self: an #LrgAchievementNotification
 * @margin: margin from screen edge in pixels
 *
 * Sets the margin from the screen edge.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_set_margin (LrgAchievementNotification *self,
                                         gint                        margin);

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_achievement_notification_update:
 * @self: an #LrgAchievementNotification
 * @delta: time elapsed since last update
 *
 * Updates the notification animation.
 *
 * Call this each frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_notification_update (LrgAchievementNotification *self,
                                     gfloat                      delta);

G_END_DECLS
