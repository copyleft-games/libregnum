/* lrg-dlc-ownership-steam.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Steam DLC ownership verification implementation.
 */

#include "lrg-dlc-ownership-steam.h"
#include "../steam/lrg-steam-client.h"
#include "../steam/lrg-steam-service.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

/**
 * SECTION:lrg-dlc-ownership-steam
 * @title: LrgDlcOwnershipSteam
 * @short_description: Steam-based DLC ownership verification
 *
 * #LrgDlcOwnershipSteam implements the #LrgDlcOwnership interface
 * using the Steam API to verify DLC ownership.
 *
 * The checker maintains a mapping from DLC IDs to Steam App IDs.
 * When check_ownership() is called, it looks up the App ID and
 * queries Steam to verify that the user owns the DLC.
 *
 * Note: The actual Steam DLC ownership check requires the Steam SDK.
 * Without the SDK, this implementation will assume ownership if
 * Steam is available and the DLC is registered.
 *
 * Since: 1.0
 */

struct _LrgDlcOwnershipSteam
{
    GObject parent_instance;

    /* DLC ID -> Steam App ID mapping */
    GHashTable *dlc_app_ids;

    /* Reference to steam service for availability checks */
    LrgSteamService *steam_service;
};

static void lrg_dlc_ownership_steam_ownership_init (LrgDlcOwnershipInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgDlcOwnershipSteam, lrg_dlc_ownership_steam, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DLC_OWNERSHIP,
                                                lrg_dlc_ownership_steam_ownership_init))

/* ==========================================================================
 * LrgDlcOwnership Interface Implementation
 * ========================================================================== */

static gboolean
lrg_dlc_ownership_steam_check_ownership_impl (LrgDlcOwnership  *ownership,
                                               const gchar      *dlc_id,
                                               GError          **error)
{
    LrgDlcOwnershipSteam *self = LRG_DLC_OWNERSHIP_STEAM (ownership);
    gpointer app_id_ptr;
    guint32 app_id;

    /* Look up the Steam App ID for this DLC */
    if (!g_hash_table_lookup_extended (self->dlc_app_ids, dlc_id, NULL, &app_id_ptr))
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_FAILED,
                     "DLC '%s' is not registered with Steam ownership checker",
                     dlc_id);
        return FALSE;
    }

    app_id = GPOINTER_TO_UINT (app_id_ptr);

    return lrg_dlc_ownership_steam_check_by_app_id (self, app_id, error);
}

static gboolean
lrg_dlc_ownership_steam_refresh_ownership_impl (LrgDlcOwnership  *ownership,
                                                 GError          **error)
{
    LrgDlcOwnershipSteam *self = LRG_DLC_OWNERSHIP_STEAM (ownership);

    /* Check if we have a steam service */
    if (self->steam_service == NULL ||
        !lrg_steam_service_is_available (self->steam_service))
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_STEAM_UNAVAILABLE,
                     "Steam service is not available");
        return FALSE;
    }

    /*
     * Run Steam callbacks to update ownership state.
     * Steam caches DLC ownership and updates it via callbacks.
     */
    lrg_steam_service_run_callbacks (self->steam_service);

    return TRUE;
}

static const gchar *
lrg_dlc_ownership_steam_get_backend_id_impl (LrgDlcOwnership *ownership)
{
    return "steam";
}

static void
lrg_dlc_ownership_steam_ownership_init (LrgDlcOwnershipInterface *iface)
{
    iface->check_ownership = lrg_dlc_ownership_steam_check_ownership_impl;
    iface->refresh_ownership = lrg_dlc_ownership_steam_refresh_ownership_impl;
    iface->get_backend_id = lrg_dlc_ownership_steam_get_backend_id_impl;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_dlc_ownership_steam_dispose (GObject *object)
{
    LrgDlcOwnershipSteam *self = LRG_DLC_OWNERSHIP_STEAM (object);

    g_clear_pointer (&self->dlc_app_ids, g_hash_table_unref);
    g_clear_object (&self->steam_service);

    G_OBJECT_CLASS (lrg_dlc_ownership_steam_parent_class)->dispose (object);
}

static void
lrg_dlc_ownership_steam_class_init (LrgDlcOwnershipSteamClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_dlc_ownership_steam_dispose;
}

static void
lrg_dlc_ownership_steam_init (LrgDlcOwnershipSteam *self)
{
    self->dlc_app_ids = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_dlc_ownership_steam_new:
 *
 * Creates a new Steam-based DLC ownership checker.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipSteam
 */
LrgDlcOwnershipSteam *
lrg_dlc_ownership_steam_new (void)
{
    return g_object_new (LRG_TYPE_DLC_OWNERSHIP_STEAM, NULL);
}

/**
 * lrg_dlc_ownership_steam_check_by_app_id:
 * @self: a #LrgDlcOwnershipSteam
 * @app_id: the Steam App ID of the DLC
 * @error: (nullable): return location for a #GError
 *
 * Checks ownership by Steam App ID directly.
 *
 * Note: The actual DLC ownership check requires the Steam SDK.
 * If Steam is available and the DLC is registered, this returns
 * %TRUE. Real DLC ownership verification happens through Steam's
 * DLC API which is not exposed here.
 *
 * Returns: %TRUE if the user owns the DLC, %FALSE otherwise
 */
gboolean
lrg_dlc_ownership_steam_check_by_app_id (LrgDlcOwnershipSteam  *self,
                                          guint32                app_id,
                                          GError               **error)
{
    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP_STEAM (self), FALSE);
    g_return_val_if_fail (app_id != 0, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Check if steam service is available */
    if (self->steam_service == NULL ||
        !lrg_steam_service_is_available (self->steam_service))
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_STEAM_UNAVAILABLE,
                     "Steam service is not available");
        return FALSE;
    }

    /*
     * Note: The actual Steam DLC ownership check (SteamApps()->BIsDlcInstalled)
     * requires direct Steam SDK integration. For now, if Steam is available
     * and the DLC is registered, we assume ownership. Real implementations
     * should extend LrgSteamService with DLC checking methods.
     */
    return TRUE;
}

/**
 * lrg_dlc_ownership_steam_register_dlc:
 * @self: a #LrgDlcOwnershipSteam
 * @dlc_id: the DLC identifier
 * @app_id: the Steam App ID for this DLC
 *
 * Registers a mapping from DLC ID to Steam App ID.
 */
void
lrg_dlc_ownership_steam_register_dlc (LrgDlcOwnershipSteam *self,
                                       const gchar          *dlc_id,
                                       guint32               app_id)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_STEAM (self));
    g_return_if_fail (dlc_id != NULL);
    g_return_if_fail (app_id != 0);

    g_hash_table_insert (self->dlc_app_ids,
                         g_strdup (dlc_id),
                         GUINT_TO_POINTER (app_id));
}

/**
 * lrg_dlc_ownership_steam_unregister_dlc:
 * @self: a #LrgDlcOwnershipSteam
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC ID to Steam App ID mapping.
 */
void
lrg_dlc_ownership_steam_unregister_dlc (LrgDlcOwnershipSteam *self,
                                         const gchar          *dlc_id)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_STEAM (self));
    g_return_if_fail (dlc_id != NULL);

    g_hash_table_remove (self->dlc_app_ids, dlc_id);
}

/**
 * lrg_dlc_ownership_steam_set_steam_service:
 * @self: a #LrgDlcOwnershipSteam
 * @steam_service: (nullable): a #LrgSteamService, or %NULL
 *
 * Sets the Steam service used for ownership verification.
 */
void
lrg_dlc_ownership_steam_set_steam_service (LrgDlcOwnershipSteam *self,
                                            LrgSteamService      *steam_service)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_STEAM (self));
    g_return_if_fail (steam_service == NULL || LRG_IS_STEAM_SERVICE (steam_service));

    if (self->steam_service == steam_service)
        return;

    g_clear_object (&self->steam_service);

    if (steam_service != NULL)
        self->steam_service = g_object_ref (steam_service);
}
