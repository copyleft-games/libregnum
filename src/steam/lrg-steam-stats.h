/* lrg-steam-stats.h - Steam statistics wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_STATS_H
#define LRG_STEAM_STATS_H

#include <glib-object.h>
#include "lrg-steam-client.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_STATS (lrg_steam_stats_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamStats, lrg_steam_stats, LRG, STEAM_STATS, GObject)

/**
 * lrg_steam_stats_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam stats manager.
 *
 * Returns: (transfer full): A new #LrgSteamStats
 */
LRG_AVAILABLE_IN_ALL
LrgSteamStats *
lrg_steam_stats_new (LrgSteamClient *client);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_stats_get_int (LrgSteamStats *self,
                         const gchar   *stat_name,
                         gint32        *value);

/**
 * lrg_steam_stats_set_int:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: the value to set
 *
 * Sets an integer stat value. Call lrg_steam_stats_store() to persist.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_stats_set_int (LrgSteamStats *self,
                         const gchar   *stat_name,
                         gint32         value);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_stats_get_float (LrgSteamStats *self,
                           const gchar   *stat_name,
                           gfloat        *value);

/**
 * lrg_steam_stats_set_float:
 * @self: an #LrgSteamStats
 * @stat_name: the stat API name
 * @value: the value to set
 *
 * Sets a float stat value. Call lrg_steam_stats_store() to persist.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_stats_set_float (LrgSteamStats *self,
                           const gchar   *stat_name,
                           gfloat         value);

/**
 * lrg_steam_stats_store:
 * @self: an #LrgSteamStats
 *
 * Stores all stat changes to Steam.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_stats_store (LrgSteamStats *self);

G_END_DECLS

#endif /* LRG_STEAM_STATS_H */
