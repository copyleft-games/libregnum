/* lrg-steam-service.h - Abstract interface for Steam services
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_SERVICE_H
#define LRG_STEAM_SERVICE_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_SERVICE (lrg_steam_service_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSteamService, lrg_steam_service, LRG, STEAM_SERVICE, GObject)

/**
 * LrgSteamServiceInterface:
 * @parent_iface: the parent interface
 * @is_available: Check if Steam is available
 * @init: Initialize Steam with app ID
 * @shutdown: Shutdown Steam
 * @run_callbacks: Process Steam callbacks
 *
 * Interface for Steam service implementations.
 * This allows testing without the Steam SDK by providing
 * stub implementations.
 */
struct _LrgSteamServiceInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgSteamServiceInterface::is_available:
     * @self: an #LrgSteamService
     *
     * Checks if Steam is available (SDK loaded and Steam client running).
     *
     * Returns: %TRUE if Steam is available
     */
    gboolean (*is_available) (LrgSteamService *self);

    /**
     * LrgSteamServiceInterface::init:
     * @self: an #LrgSteamService
     * @app_id: the Steam application ID
     * @error: (nullable): return location for error
     *
     * Initializes the Steam API with the given application ID.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*init) (LrgSteamService  *self,
                      guint32           app_id,
                      GError          **error);

    /**
     * LrgSteamServiceInterface::shutdown:
     * @self: an #LrgSteamService
     *
     * Shuts down the Steam API.
     */
    void (*shutdown) (LrgSteamService *self);

    /**
     * LrgSteamServiceInterface::run_callbacks:
     * @self: an #LrgSteamService
     *
     * Processes Steam callbacks. Should be called every frame.
     */
    void (*run_callbacks) (LrgSteamService *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_steam_service_is_available:
 * @self: an #LrgSteamService
 *
 * Checks if Steam is available.
 *
 * Returns: %TRUE if Steam is available
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_service_is_available (LrgSteamService *self);

/**
 * lrg_steam_service_init:
 * @self: an #LrgSteamService
 * @app_id: the Steam application ID
 * @error: (nullable): return location for error
 *
 * Initializes the Steam API.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_service_init (LrgSteamService  *self,
                        guint32           app_id,
                        GError          **error);

/**
 * lrg_steam_service_shutdown:
 * @self: an #LrgSteamService
 *
 * Shuts down the Steam API.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_steam_service_shutdown (LrgSteamService *self);

/**
 * lrg_steam_service_run_callbacks:
 * @self: an #LrgSteamService
 *
 * Processes Steam callbacks.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_steam_service_run_callbacks (LrgSteamService *self);

G_END_DECLS

#endif /* LRG_STEAM_SERVICE_H */
