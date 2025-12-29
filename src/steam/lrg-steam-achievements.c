/* lrg-steam-achievements.c - Steam achievements wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-achievements.h"
#include "lrg-steam-types.h"

/**
 * SECTION:lrg-steam-achievements
 * @title: LrgSteamAchievements
 * @short_description: Steam achievements wrapper
 *
 * #LrgSteamAchievements provides access to Steam achievements.
 * It wraps the ISteamUserStats interface for achievement operations.
 *
 * Achievements must be defined in the Steamworks app configuration
 * before they can be used. The API name used here corresponds to
 * the achievement's API name in Steamworks.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgSteamAchievements) achievements;
 * g_autoptr(GError) error = NULL;
 *
 * achievements = lrg_steam_achievements_new (client);
 *
 * if (!lrg_steam_achievements_unlock (achievements, "ACH_WIN_GAME", &error))
 * {
 *     g_warning ("Failed to unlock: %s", error->message);
 * }
 *
 * // Don't forget to store!
 * lrg_steam_achievements_store (achievements, NULL);
 * ]|
 */

struct _LrgSteamAchievements
{
    GObject         parent_instance;
    LrgSteamClient *client;
};

enum
{
    PROP_0,
    PROP_CLIENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgSteamAchievements, lrg_steam_achievements, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-steam-achievements-error-quark, lrg_steam_achievements_error)

static void
lrg_steam_achievements_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgSteamAchievements *self = LRG_STEAM_ACHIEVEMENTS (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_clear_object (&self->client);
        self->client = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_achievements_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgSteamAchievements *self = LRG_STEAM_ACHIEVEMENTS (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_value_set_object (value, self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_achievements_dispose (GObject *object)
{
    LrgSteamAchievements *self = LRG_STEAM_ACHIEVEMENTS (object);

    g_clear_object (&self->client);

    G_OBJECT_CLASS (lrg_steam_achievements_parent_class)->dispose (object);
}

static void
lrg_steam_achievements_class_init (LrgSteamAchievementsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_steam_achievements_set_property;
    object_class->get_property = lrg_steam_achievements_get_property;
    object_class->dispose = lrg_steam_achievements_dispose;

    /**
     * LrgSteamAchievements:client:
     *
     * The Steam client to use.
     */
    properties[PROP_CLIENT] =
        g_param_spec_object ("client",
                             "Client",
                             "The Steam client",
                             LRG_TYPE_STEAM_CLIENT,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_steam_achievements_init (LrgSteamAchievements *self)
{
    self->client = NULL;
}

/**
 * lrg_steam_achievements_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam achievements manager.
 *
 * Returns: (transfer full): A new #LrgSteamAchievements
 */
LrgSteamAchievements *
lrg_steam_achievements_new (LrgSteamClient *client)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (client), NULL);

    return g_object_new (LRG_TYPE_STEAM_ACHIEVEMENTS,
                         "client", client,
                         NULL);
}

/**
 * lrg_steam_achievements_unlock:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 * @error: (nullable): return location for error
 *
 * Unlocks an achievement.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_achievements_unlock (LrgSteamAchievements  *self,
                               const gchar           *achievement_id,
                               GError               **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), FALSE);
    g_return_val_if_fail (achievement_id != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return FALSE;
    }

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam user stats interface not available");
        return FALSE;
    }

    if (!SteamAPI_ISteamUserStats_SetAchievement (stats, achievement_id))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_UNLOCK_FAILED,
                     "Failed to unlock achievement: %s", achievement_id);
        return FALSE;
    }

    g_debug ("Achievement unlocked: %s", achievement_id);
    return TRUE;
#else
    g_debug ("Steam stub: unlock achievement %s (no-op)", achievement_id);
    return TRUE;
#endif
}

/**
 * lrg_steam_achievements_is_unlocked:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 *
 * Checks if an achievement has been unlocked.
 *
 * Returns: %TRUE if unlocked
 */
gboolean
lrg_steam_achievements_is_unlocked (LrgSteamAchievements *self,
                                    const gchar          *achievement_id)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), FALSE);
    g_return_val_if_fail (achievement_id != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;
    bool achieved;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    achieved = false;
    if (SteamAPI_ISteamUserStats_GetAchievement (stats, achievement_id, &achieved))
        return achieved;

    return FALSE;
#else
    return FALSE;
#endif
}

/**
 * lrg_steam_achievements_clear:
 * @self: an #LrgSteamAchievements
 * @achievement_id: the achievement API name
 * @error: (nullable): return location for error
 *
 * Clears (re-locks) an achievement.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_achievements_clear (LrgSteamAchievements  *self,
                              const gchar           *achievement_id,
                              GError               **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), FALSE);
    g_return_val_if_fail (achievement_id != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return FALSE;
    }

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam user stats interface not available");
        return FALSE;
    }

    if (!SteamAPI_ISteamUserStats_ClearAchievement (stats, achievement_id))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_UNLOCK_FAILED,
                     "Failed to clear achievement: %s", achievement_id);
        return FALSE;
    }

    g_debug ("Achievement cleared: %s", achievement_id);
    return TRUE;
#else
    g_debug ("Steam stub: clear achievement %s (no-op)", achievement_id);
    return TRUE;
#endif
}

/**
 * lrg_steam_achievements_store:
 * @self: an #LrgSteamAchievements
 * @error: (nullable): return location for error
 *
 * Stores all achievement changes to Steam.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_achievements_store (LrgSteamAchievements  *self,
                              GError               **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return FALSE;
    }

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_NOT_INITIALIZED,
                     "Steam user stats interface not available");
        return FALSE;
    }

    if (!SteamAPI_ISteamUserStats_StoreStats (stats))
    {
        g_set_error (error,
                     LRG_STEAM_ACHIEVEMENTS_ERROR,
                     LRG_STEAM_ACHIEVEMENTS_ERROR_STORE_FAILED,
                     "Failed to store stats");
        return FALSE;
    }

    g_debug ("Stats stored successfully");
    return TRUE;
#else
    g_debug ("Steam stub: store stats (no-op)");
    return TRUE;
#endif
}

/**
 * lrg_steam_achievements_get_count:
 * @self: an #LrgSteamAchievements
 *
 * Gets the total number of achievements.
 *
 * Returns: The number of achievements
 */
guint
lrg_steam_achievements_get_count (LrgSteamAchievements *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), 0);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return 0;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return 0;

    return SteamAPI_ISteamUserStats_GetNumAchievements (stats);
#else
    return 0;
#endif
}

/**
 * lrg_steam_achievements_get_name:
 * @self: an #LrgSteamAchievements
 * @index: the achievement index
 *
 * Gets the API name of an achievement by index.
 *
 * Returns: (transfer none) (nullable): The achievement API name
 */
const gchar *
lrg_steam_achievements_get_name (LrgSteamAchievements *self,
                                 guint                 index)
{
    g_return_val_if_fail (LRG_IS_STEAM_ACHIEVEMENTS (self), NULL);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return NULL;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return NULL;

    return SteamAPI_ISteamUserStats_GetAchievementName (stats, index);
#else
    return NULL;
#endif
}
