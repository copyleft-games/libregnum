/* lrg-achievement-toasts.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementToasts - the model of "achievement earned" toasts on a
 * HUD: a queue of unlocks shown a few at a time, each sliding in,
 * staying and fading, headless (a game draws them).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-achievement.h"
#include "lrg-achievement-manager.h"

G_BEGIN_DECLS

/**
 * LRG_ACHIEVEMENT_TOASTS_DEFAULT_SECONDS:
 *
 * How long a toast shows by default, its fades included.
 */
#define LRG_ACHIEVEMENT_TOASTS_DEFAULT_SECONDS (5.0)

/**
 * LRG_ACHIEVEMENT_TOASTS_FADE_SECONDS:
 *
 * The seconds a toast takes to fade in at its start and out at its end.
 */
#define LRG_ACHIEVEMENT_TOASTS_FADE_SECONDS (0.5)

#define LRG_TYPE_ACHIEVEMENT_TOAST (lrg_achievement_toast_get_type ())

/**
 * LrgAchievementToast:
 *
 * One toast: what was earned (copied when it was pushed, so the toast
 * outlives the achievement) and how long it has shown.  Reference
 * counted.
 *
 * Since: 1.0
 */
typedef struct _LrgAchievementToast LrgAchievementToast;

LRG_AVAILABLE_IN_ALL
GType lrg_achievement_toast_get_type (void) G_GNUC_CONST;

/**
 * lrg_achievement_toast_ref:
 * @self: an #LrgAchievementToast
 *
 * Returns: (transfer full): @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementToast *
lrg_achievement_toast_ref (LrgAchievementToast *self);

/**
 * lrg_achievement_toast_unref:
 * @self: (transfer full): an #LrgAchievementToast
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_toast_unref (LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_id:
 * @self: an #LrgAchievementToast
 *
 * Returns: (transfer none): the achievement's id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_toast_get_id (const LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_name:
 * @self: an #LrgAchievementToast
 *
 * Returns: (transfer none): the achievement's name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_toast_get_name (const LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_description:
 * @self: an #LrgAchievementToast
 *
 * Returns: (transfer none) (nullable): the achievement's description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_toast_get_description (const LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_icon:
 * @self: an #LrgAchievementToast
 *
 * Returns: (transfer none) (nullable): the achievement's icon
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_achievement_toast_get_icon (const LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_points:
 * @self: an #LrgAchievementToast
 *
 * Returns: the achievement's points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_toast_get_points (const LrgAchievementToast *self);

/**
 * lrg_achievement_toast_get_age:
 * @self: an #LrgAchievementToast
 *
 * Returns: seconds it has shown (0 while it waits in the queue)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_toast_get_age (const LrgAchievementToast *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAchievementToast, lrg_achievement_toast_unref)

#define LRG_TYPE_ACHIEVEMENT_TOASTS (lrg_achievement_toasts_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgAchievementToasts, lrg_achievement_toasts, LRG, ACHIEVEMENT_TOASTS, GObject)
#pragma GCC visibility pop

/**
 * LrgAchievementToasts:
 *
 * Unlocks wait in a queue; at most #LrgAchievementToasts:max-visible
 * show at once, oldest first, each for #LrgAchievementToasts:duration
 * seconds; when one ends the next waiting one shows.
 * lrg_achievement_toasts_follow() pushes every unlock of a manager.
 *
 * Signals: #LrgAchievementToasts::shown when a toast starts showing.
 *
 * Since: 1.0
 */

/**
 * LrgAchievementToasts::shown:
 * @self: the toasts
 * @toast: the toast that started showing
 *
 * Since: 1.0
 */

/**
 * lrg_achievement_toasts_new:
 *
 * Returns: (transfer full): empty toasts, 5 seconds each, one at a time
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementToasts *
lrg_achievement_toasts_new (void);

/**
 * lrg_achievement_toasts_push:
 * @self: an #LrgAchievementToasts
 * @achievement: an achievement just earned
 *
 * Queues a toast of @achievement (shown at once when there is room).
 *
 * Returns: (transfer none): the toast
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementToast *
lrg_achievement_toasts_push (LrgAchievementToasts *self,
                             LrgAchievement       *achievement);

/**
 * lrg_achievement_toasts_follow:
 * @self: an #LrgAchievementToasts
 * @manager: (nullable): the manager whose unlocks are pushed, %NULL to
 *   stop following
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_toasts_follow (LrgAchievementToasts  *self,
                               LrgAchievementManager *manager);

/**
 * lrg_achievement_toasts_update:
 * @self: an #LrgAchievementToasts
 * @delta: seconds passed (negative counts as 0)
 *
 * Ages the shown toasts, drops the ended ones and shows waiting ones in
 * their place.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_toasts_update (LrgAchievementToasts *self,
                               gdouble               delta);

/**
 * lrg_achievement_toasts_get_count:
 * @self: an #LrgAchievementToasts
 *
 * Returns: the toasts showing now
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_toasts_get_count (LrgAchievementToasts *self);

/**
 * lrg_achievement_toasts_get_waiting:
 * @self: an #LrgAchievementToasts
 *
 * Returns: the toasts waiting for room
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_achievement_toasts_get_waiting (LrgAchievementToasts *self);

/**
 * lrg_achievement_toasts_get_toast:
 * @self: an #LrgAchievementToasts
 * @index: 0 .. lrg_achievement_toasts_get_count() - 1, oldest first
 *
 * Returns: (transfer none) (nullable): the toast
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAchievementToast *
lrg_achievement_toasts_get_toast (LrgAchievementToasts *self,
                                  guint                 index);

/**
 * lrg_achievement_toasts_get_alpha:
 * @self: an #LrgAchievementToasts
 * @index: a shown toast
 *
 * Returns: its opacity, 0.0 .. 1.0: rising over the first
 *   #LrgAchievementToasts:fade seconds, falling over the last
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_toasts_get_alpha (LrgAchievementToasts *self,
                                  guint                 index);

/**
 * lrg_achievement_toasts_get_slide:
 * @self: an #LrgAchievementToasts
 * @index: a shown toast
 *
 * Returns: how far it slid in, 0.0 (just started) .. 1.0 (in place),
 *   eased out over the fade-in
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_achievement_toasts_get_slide (LrgAchievementToasts *self,
                                  guint                 index);

/**
 * lrg_achievement_toasts_clear:
 * @self: an #LrgAchievementToasts
 *
 * Drops every shown and waiting toast.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_achievement_toasts_clear (LrgAchievementToasts *self);

G_END_DECLS
