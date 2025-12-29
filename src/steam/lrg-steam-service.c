/* lrg-steam-service.c - Abstract interface for Steam services
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-service.h"

/**
 * SECTION:lrg-steam-service
 * @title: LrgSteamService
 * @short_description: Interface for Steam services
 *
 * #LrgSteamService is an interface that provides access to Steam
 * functionality. It can be implemented by actual Steam SDK wrappers
 * or by stub implementations for testing without Steam.
 *
 * This interface provides the core Steam lifecycle methods:
 * - Initialization with app ID
 * - Shutdown
 * - Callback processing
 *
 * Concrete implementations include:
 * - #LrgSteamClient: Full Steam SDK implementation (requires STEAM=1)
 * - #LrgSteamStub: Stub implementation that returns success but does nothing
 */

G_DEFINE_INTERFACE (LrgSteamService, lrg_steam_service, G_TYPE_OBJECT)

static void
lrg_steam_service_default_init (LrgSteamServiceInterface *iface)
{
    /* Default implementations could be added here if needed */
}

/**
 * lrg_steam_service_is_available:
 * @self: an #LrgSteamService
 *
 * Checks if Steam is available (SDK loaded and Steam client running).
 *
 * Returns: %TRUE if Steam is available
 */
gboolean
lrg_steam_service_is_available (LrgSteamService *self)
{
    LrgSteamServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_STEAM_SERVICE (self), FALSE);

    iface = LRG_STEAM_SERVICE_GET_IFACE (self);
    if (iface->is_available != NULL)
        return iface->is_available (self);

    return FALSE;
}

/**
 * lrg_steam_service_init:
 * @self: an #LrgSteamService
 * @app_id: the Steam application ID
 * @error: (nullable): return location for error
 *
 * Initializes the Steam API with the given application ID.
 * This should be called early in the application startup.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_steam_service_init (LrgSteamService  *self,
                        guint32           app_id,
                        GError          **error)
{
    LrgSteamServiceInterface *iface;

    g_return_val_if_fail (LRG_IS_STEAM_SERVICE (self), FALSE);

    iface = LRG_STEAM_SERVICE_GET_IFACE (self);
    if (iface->init != NULL)
        return iface->init (self, app_id, error);

    return TRUE;
}

/**
 * lrg_steam_service_shutdown:
 * @self: an #LrgSteamService
 *
 * Shuts down the Steam API. This should be called during
 * application shutdown.
 */
void
lrg_steam_service_shutdown (LrgSteamService *self)
{
    LrgSteamServiceInterface *iface;

    g_return_if_fail (LRG_IS_STEAM_SERVICE (self));

    iface = LRG_STEAM_SERVICE_GET_IFACE (self);
    if (iface->shutdown != NULL)
        iface->shutdown (self);
}

/**
 * lrg_steam_service_run_callbacks:
 * @self: an #LrgSteamService
 *
 * Processes Steam callbacks. This should be called every frame
 * to handle asynchronous Steam events.
 */
void
lrg_steam_service_run_callbacks (LrgSteamService *self)
{
    LrgSteamServiceInterface *iface;

    g_return_if_fail (LRG_IS_STEAM_SERVICE (self));

    iface = LRG_STEAM_SERVICE_GET_IFACE (self);
    if (iface->run_callbacks != NULL)
        iface->run_callbacks (self);
}
