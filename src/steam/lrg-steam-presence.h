/* lrg-steam-presence.h - Steam Rich Presence wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_PRESENCE_H
#define LRG_STEAM_PRESENCE_H

#include <glib-object.h>
#include "lrg-steam-client.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_PRESENCE (lrg_steam_presence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamPresence, lrg_steam_presence, LRG, STEAM_PRESENCE, GObject)

/**
 * lrg_steam_presence_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam presence manager.
 *
 * Returns: (transfer full): A new #LrgSteamPresence
 */
LRG_AVAILABLE_IN_ALL
LrgSteamPresence *
lrg_steam_presence_new (LrgSteamClient *client);

/**
 * lrg_steam_presence_set:
 * @self: an #LrgSteamPresence
 * @key: the rich presence key
 * @value: the value (or %NULL to clear)
 *
 * Sets a rich presence key-value pair. Common keys include:
 * - "status": Short status string
 * - "connect": Connection string for join game
 * - "steam_display": Localization token for display
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_presence_set (LrgSteamPresence *self,
                        const gchar      *key,
                        const gchar      *value);

/**
 * lrg_steam_presence_set_status:
 * @self: an #LrgSteamPresence
 * @status: the status string
 *
 * Convenience function to set the "status" rich presence key.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_presence_set_status (LrgSteamPresence *self,
                               const gchar      *status);

/**
 * lrg_steam_presence_clear:
 * @self: an #LrgSteamPresence
 *
 * Clears all rich presence data.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_steam_presence_clear (LrgSteamPresence *self);

G_END_DECLS

#endif /* LRG_STEAM_PRESENCE_H */
