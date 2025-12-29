/* lrg-steam-client.h - Steam client implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_CLIENT_H
#define LRG_STEAM_CLIENT_H

#include <glib-object.h>
#include "lrg-steam-service.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_CLIENT (lrg_steam_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamClient, lrg_steam_client, LRG, STEAM_CLIENT, GObject)

/**
 * LRG_STEAM_CLIENT_ERROR:
 *
 * Error domain for Steam client errors.
 */
#define LRG_STEAM_CLIENT_ERROR (lrg_steam_client_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_steam_client_error_quark (void);

/**
 * LrgSteamClientError:
 * @LRG_STEAM_CLIENT_ERROR_INIT_FAILED: Steam initialization failed
 * @LRG_STEAM_CLIENT_ERROR_NO_STEAM_CLIENT: Steam client not running
 * @LRG_STEAM_CLIENT_ERROR_VERSION_MISMATCH: Steam SDK version mismatch
 * @LRG_STEAM_CLIENT_ERROR_NOT_INITIALIZED: Steam not initialized
 * @LRG_STEAM_CLIENT_ERROR_NOT_SUPPORTED: Steam not supported (built without STEAM=1)
 *
 * Error codes for Steam client operations.
 */
typedef enum
{
    LRG_STEAM_CLIENT_ERROR_INIT_FAILED,
    LRG_STEAM_CLIENT_ERROR_NO_STEAM_CLIENT,
    LRG_STEAM_CLIENT_ERROR_VERSION_MISMATCH,
    LRG_STEAM_CLIENT_ERROR_NOT_INITIALIZED,
    LRG_STEAM_CLIENT_ERROR_NOT_SUPPORTED
} LrgSteamClientError;

/**
 * lrg_steam_client_new:
 *
 * Creates a new Steam client. If the library was built without
 * Steam support (STEAM=0), this returns a stub implementation
 * that logs warnings but allows the game to run.
 *
 * Returns: (transfer full): A new #LrgSteamClient
 */
LRG_AVAILABLE_IN_ALL
LrgSteamClient *
lrg_steam_client_new (void);

/**
 * lrg_steam_client_is_logged_on:
 * @self: an #LrgSteamClient
 *
 * Checks if the current user is logged into Steam.
 *
 * Returns: %TRUE if logged in
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_client_is_logged_on (LrgSteamClient *self);

/**
 * lrg_steam_client_get_steam_id:
 * @self: an #LrgSteamClient
 *
 * Gets the current user's Steam ID.
 *
 * Returns: The 64-bit Steam ID, or 0 if not available
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_steam_client_get_steam_id (LrgSteamClient *self);

/**
 * lrg_steam_client_get_persona_name:
 * @self: an #LrgSteamClient
 *
 * Gets the current user's display name (persona name).
 *
 * Returns: (transfer none) (nullable): The persona name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_steam_client_get_persona_name (LrgSteamClient *self);

/**
 * lrg_steam_client_get_app_id:
 * @self: an #LrgSteamClient
 *
 * Gets the application's Steam App ID.
 *
 * Returns: The app ID
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_steam_client_get_app_id (LrgSteamClient *self);

G_END_DECLS

#endif /* LRG_STEAM_CLIENT_H */
