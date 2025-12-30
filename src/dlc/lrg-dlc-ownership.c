/* lrg-dlc-ownership.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the LrgDlcOwnership interface.
 */

#include "lrg-dlc-ownership.h"
#include "../lrg-log.h"

G_DEFINE_INTERFACE (LrgDlcOwnership, lrg_dlc_ownership, G_TYPE_OBJECT)

static void
lrg_dlc_ownership_default_init (LrgDlcOwnershipInterface *iface)
{
    /* No default implementations - methods must be provided by implementors */
}

/**
 * lrg_dlc_ownership_check_ownership:
 * @self: a #LrgDlcOwnership
 * @dlc_id: the unique identifier of the DLC to check
 * @error: (nullable): return location for a #GError
 *
 * Checks whether the user owns the specified DLC.
 *
 * Returns: %TRUE if the user owns the DLC, %FALSE otherwise
 */
gboolean
lrg_dlc_ownership_check_ownership (LrgDlcOwnership  *self,
                                   const gchar      *dlc_id,
                                   GError          **error)
{
    LrgDlcOwnershipInterface *iface;

    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP (self), FALSE);
    g_return_val_if_fail (dlc_id != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_DLC_OWNERSHIP_GET_IFACE (self);

    if (iface->check_ownership == NULL)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_FAILED,
                     "Type %s does not implement check_ownership()",
                     G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }

    return iface->check_ownership (self, dlc_id, error);
}

/**
 * lrg_dlc_ownership_refresh_ownership:
 * @self: a #LrgDlcOwnership
 * @error: (nullable): return location for a #GError
 *
 * Refreshes the ownership cache from the backend.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_dlc_ownership_refresh_ownership (LrgDlcOwnership  *self,
                                     GError          **error)
{
    LrgDlcOwnershipInterface *iface;

    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_DLC_OWNERSHIP_GET_IFACE (self);

    /* refresh_ownership is optional - if not implemented, succeed silently */
    if (iface->refresh_ownership == NULL)
        return TRUE;

    return iface->refresh_ownership (self, error);
}

/**
 * lrg_dlc_ownership_get_backend_id:
 * @self: a #LrgDlcOwnership
 *
 * Gets an identifier for this ownership backend.
 *
 * Returns: (transfer none): the backend identifier string
 */
const gchar *
lrg_dlc_ownership_get_backend_id (LrgDlcOwnership *self)
{
    LrgDlcOwnershipInterface *iface;

    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP (self), NULL);

    iface = LRG_DLC_OWNERSHIP_GET_IFACE (self);

    g_return_val_if_fail (iface->get_backend_id != NULL, NULL);

    return iface->get_backend_id (self);
}
