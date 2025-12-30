/* lrg-dlc-ownership-steam.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Steam DLC ownership verification.
 *
 * This implementation uses the Steam API to verify DLC ownership.
 * It requires the LrgSteamClient to be initialized and available.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-dlc-ownership.h"

G_BEGIN_DECLS

#define LRG_TYPE_DLC_OWNERSHIP_STEAM (lrg_dlc_ownership_steam_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDlcOwnershipSteam, lrg_dlc_ownership_steam, LRG, DLC_OWNERSHIP_STEAM, GObject)

/**
 * lrg_dlc_ownership_steam_new:
 *
 * Creates a new Steam-based DLC ownership checker.
 *
 * The checker uses the Steam API to verify ownership of DLC
 * by their Steam App ID. The Steam client must be initialized
 * before using this checker.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipSteam
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnershipSteam * lrg_dlc_ownership_steam_new (void);

/**
 * lrg_dlc_ownership_steam_check_by_app_id:
 * @self: a #LrgDlcOwnershipSteam
 * @app_id: the Steam App ID of the DLC
 * @error: (nullable): return location for a #GError
 *
 * Checks ownership by Steam App ID directly.
 *
 * This is a convenience method that bypasses the dlc_id lookup
 * and queries Steam directly with the given App ID.
 *
 * Returns: %TRUE if the user owns the DLC, %FALSE otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_ownership_steam_check_by_app_id (LrgDlcOwnershipSteam  *self,
                                                   guint32                app_id,
                                                   GError               **error);

/**
 * lrg_dlc_ownership_steam_register_dlc:
 * @self: a #LrgDlcOwnershipSteam
 * @dlc_id: the DLC identifier
 * @app_id: the Steam App ID for this DLC
 *
 * Registers a mapping from DLC ID to Steam App ID.
 *
 * This mapping is used by check_ownership() to look up the
 * Steam App ID for a given DLC identifier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_steam_register_dlc (LrgDlcOwnershipSteam *self,
                                            const gchar          *dlc_id,
                                            guint32               app_id);

/**
 * lrg_dlc_ownership_steam_unregister_dlc:
 * @self: a #LrgDlcOwnershipSteam
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC ID to Steam App ID mapping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_steam_unregister_dlc (LrgDlcOwnershipSteam *self,
                                              const gchar          *dlc_id);

/**
 * lrg_dlc_ownership_steam_set_steam_service:
 * @self: a #LrgDlcOwnershipSteam
 * @steam_service: (nullable): a #LrgSteamService, or %NULL
 *
 * Sets the Steam service used for ownership verification.
 *
 * The ownership checker will take a reference to the service.
 * Pass %NULL to clear the service reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_steam_set_steam_service (LrgDlcOwnershipSteam *self,
                                                 LrgSteamService      *steam_service);

G_END_DECLS
