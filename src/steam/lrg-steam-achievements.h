/* lrg-steam-achievements.h - Steam achievements wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_ACHIEVEMENTS_H
#define LRG_STEAM_ACHIEVEMENTS_H

#include <glib-object.h>
#include "lrg-steam-client.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_ACHIEVEMENTS (lrg_steam_achievements_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamAchievements, lrg_steam_achievements, LRG, STEAM_ACHIEVEMENTS, GObject)

/**
 * LRG_STEAM_ACHIEVEMENTS_ERROR:
 *
 * Error domain for Steam achievements errors.
 */
#define LRG_STEAM_ACHIEVEMENTS_ERROR (lrg_steam_achievements_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_steam_achievements_error_quark (void);

/**
 * LrgSteamAchievementsError:
 * @LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED: Steam not initialized
 * @LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_FOUND: Achievement not found
 * @LRG_STEAM_ACHIEVEMENTS_ERROR_UNLOCK_FAILED: Failed to unlock achievement
 * @LRG_STEAM_ACHIEVEMENTS_ERROR_STORE_FAILED: Failed to store stats
 *
 * Error codes for Steam achievements operations.
 */
typedef enum
{
    LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
    LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_FOUND,
    LRG_STEAM_ACHIEVEMENTS_ERROR_UNLOCK_FAILED,
    LRG_STEAM_ACHIEVEMENTS_ERROR_STORE_FAILED
} LrgSteamAchievementsError;

/**
 * lrg_steam_achievements_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam achievements manager.
 *
 * Returns: (transfer full): A new #LrgSteamAchievements
 */
LRG_AVAILABLE_IN_ALL
LrgSteamAchievements *
lrg_steam_achievements_new (LrgSteamClient *client);

/**
 * lrg_steam_achievements_unlock:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 * @error: (nullable): return location for error
 *
 * Unlocks an achievement. Call lrg_steam_achievements_store()
 * to persist the changes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_achievements_unlock (LrgSteamAchievements  *self,
                               const gchar           *achievement_id,
                               GError               **error);

/**
 * lrg_steam_achievements_is_unlocked:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 *
 * Checks if an achievement has been unlocked.
 *
 * Returns: %TRUE if unlocked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_achievements_is_unlocked (LrgSteamAchievements *self,
                                    const gchar          *achievement_id);

/**
 * lrg_steam_achievements_clear:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 * @error: (nullable): return location for error
 *
 * Clears (re-locks) an achievement. Primarily for testing.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_achievements_clear (LrgSteamAchievements  *self,
                              const gchar           *achievement_id,
                              GError               **error);

/**
 * lrg_steam_achievements_store:
 * @self: an #LrgSteamAchievements
 * @error: (nullable): return location for error
 *
 * Stores all achievement changes to Steam. Must be called after
 * unlocking achievements for changes to persist.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_achievements_store (LrgSteamAchievements  *self,
                              GError               **error);

/**
 * lrg_steam_achievements_get_count:
 * @self: an #LrgSteamAchievements
 *
 * Gets the total number of achievements for this game.
 *
 * Returns: The number of achievements
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_steam_achievements_get_count (LrgSteamAchievements *self);

/**
 * lrg_steam_achievements_get_name:
 * @self: an #LrgSteamAchievements
 * @index: the achievement index (0-based)
 *
 * Gets the API name of an achievement by index.
 *
 * Returns: (transfer none) (nullable): The achievement API name
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_steam_achievements_get_name (LrgSteamAchievements *self,
                                 guint                 index);

G_END_DECLS

#endif /* LRG_STEAM_ACHIEVEMENTS_H */
