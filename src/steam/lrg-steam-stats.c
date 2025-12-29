/* lrg-steam-stats.c - Steam statistics wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-stats.h"
#include "lrg-steam-types.h"

/**
 * SECTION:lrg-steam-stats
 * @title: LrgSteamStats
 * @short_description: Steam statistics wrapper
 *
 * #LrgSteamStats provides access to Steam stats for tracking
 * player progress and game metrics. Stats must be defined in
 * the Steamworks app configuration.
 */

struct _LrgSteamStats
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

G_DEFINE_TYPE (LrgSteamStats, lrg_steam_stats, G_TYPE_OBJECT)

static void
lrg_steam_stats_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgSteamStats *self = LRG_STEAM_STATS (object);

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
lrg_steam_stats_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgSteamStats *self = LRG_STEAM_STATS (object);

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
lrg_steam_stats_dispose (GObject *object)
{
    LrgSteamStats *self = LRG_STEAM_STATS (object);

    g_clear_object (&self->client);

    G_OBJECT_CLASS (lrg_steam_stats_parent_class)->dispose (object);
}

static void
lrg_steam_stats_class_init (LrgSteamStatsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_steam_stats_set_property;
    object_class->get_property = lrg_steam_stats_get_property;
    object_class->dispose = lrg_steam_stats_dispose;

    properties[PROP_CLIENT] =
        g_param_spec_object ("client",
                             "Client",
                             "The Steam client",
                             LRG_TYPE_STEAM_CLIENT,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_steam_stats_init (LrgSteamStats *self)
{
    self->client = NULL;
}

/**
 * lrg_steam_stats_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam stats manager.
 *
 * Returns: (transfer full): A new #LrgSteamStats
 */
LrgSteamStats *
lrg_steam_stats_new (LrgSteamClient *client)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (client), NULL);

    return g_object_new (LRG_TYPE_STEAM_STATS,
                         "client", client,
                         NULL);
}

/**
 * lrg_steam_stats_get_int:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: (out): return location for the value
 *
 * Gets an integer stat value.
 *
 * Returns: %TRUE if the stat exists
 */
gboolean
lrg_steam_stats_get_int (LrgSteamStats *self,
                         const gchar   *stat_name,
                         gint32        *value)
{
    g_return_val_if_fail (LRG_IS_STEAM_STATS (self), FALSE);
    g_return_val_if_fail (stat_name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    return SteamAPI_ISteamUserStats_GetStatInt32 (stats, stat_name, value);
#else
    *value = 0;
    return FALSE;
#endif
}

/**
 * lrg_steam_stats_set_int:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: the value to set
 *
 * Sets an integer stat value.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_stats_set_int (LrgSteamStats *self,
                         const gchar   *stat_name,
                         gint32         value)
{
    g_return_val_if_fail (LRG_IS_STEAM_STATS (self), FALSE);
    g_return_val_if_fail (stat_name != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    return SteamAPI_ISteamUserStats_SetStatInt32 (stats, stat_name, value);
#else
    g_debug ("Steam stub: set stat %s = %d (no-op)", stat_name, value);
    return TRUE;
#endif
}

/**
 * lrg_steam_stats_get_float:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: (out): return location for the value
 *
 * Gets a float stat value.
 *
 * Returns: %TRUE if the stat exists
 */
gboolean
lrg_steam_stats_get_float (LrgSteamStats *self,
                           const gchar   *stat_name,
                           gfloat        *value)
{
    g_return_val_if_fail (LRG_IS_STEAM_STATS (self), FALSE);
    g_return_val_if_fail (stat_name != NULL, FALSE);
    g_return_val_if_fail (value != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    return SteamAPI_ISteamUserStats_GetStatFloat (stats, stat_name, value);
#else
    *value = 0.0f;
    return FALSE;
#endif
}

/**
 * lrg_steam_stats_set_float:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: the value to set
 *
 * Sets a float stat value.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_stats_set_float (LrgSteamStats *self,
                           const gchar   *stat_name,
                           gfloat         value)
{
    g_return_val_if_fail (LRG_IS_STEAM_STATS (self), FALSE);
    g_return_val_if_fail (stat_name != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    return SteamAPI_ISteamUserStats_SetStatFloat (stats, stat_name, value);
#else
    g_debug ("Steam stub: set stat %s = %f (no-op)", stat_name, value);
    return TRUE;
#endif
}

/**
 * lrg_steam_stats_store:
 * @self: an #LrgSteamStats
 *
 * Stores all stat changes to Steam.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_stats_store (LrgSteamStats *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_STATS (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUserStats *stats;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    stats = SteamAPI_SteamUserStats_v013 ();
    if (stats == NULL)
        return FALSE;

    return SteamAPI_ISteamUserStats_StoreStats (stats);
#else
    g_debug ("Steam stub: store stats (no-op)");
    return TRUE;
#endif
}
