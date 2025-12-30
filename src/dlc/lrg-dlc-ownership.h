/* lrg-dlc-ownership.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for DLC ownership verification.
 *
 * This interface allows different backends (Steam, license file, manifest)
 * to verify whether the user owns a specific DLC. Implementations can
 * use platform-specific APIs or local verification methods.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DLC_OWNERSHIP (lrg_dlc_ownership_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDlcOwnership, lrg_dlc_ownership, LRG, DLC_OWNERSHIP, GObject)

/**
 * LrgDlcOwnershipInterface:
 * @parent_iface: parent interface
 * @check_ownership: checks if the user owns a specific DLC
 * @refresh_ownership: refreshes ownership cache from the backend
 * @get_backend_id: returns an identifier for this ownership backend
 *
 * Interface structure for #LrgDlcOwnership.
 *
 * Implementations must provide the check_ownership and get_backend_id
 * methods. The refresh_ownership method is optional.
 */
struct _LrgDlcOwnershipInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgDlcOwnershipInterface::check_ownership:
     * @self: a #LrgDlcOwnership
     * @dlc_id: the unique identifier of the DLC to check
     * @error: (nullable): return location for a #GError
     *
     * Checks whether the user owns the specified DLC.
     *
     * This method may query external services (Steam, etc.) or
     * check local files (license keys, manifest flags).
     *
     * Returns: %TRUE if the user owns the DLC, %FALSE otherwise
     */
    gboolean (* check_ownership) (LrgDlcOwnership  *self,
                                  const gchar      *dlc_id,
                                  GError          **error);

    /**
     * LrgDlcOwnershipInterface::refresh_ownership:
     * @self: a #LrgDlcOwnership
     * @error: (nullable): return location for a #GError
     *
     * Refreshes the ownership cache from the backend.
     *
     * Some backends may cache ownership information. This method
     * forces a refresh from the authoritative source. Implementations
     * that don't cache may simply return %TRUE.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (* refresh_ownership) (LrgDlcOwnership  *self,
                                    GError          **error);

    /**
     * LrgDlcOwnershipInterface::get_backend_id:
     * @self: a #LrgDlcOwnership
     *
     * Gets an identifier for this ownership backend.
     *
     * This can be used for debugging or to identify which
     * verification method is being used (e.g., "steam", "license",
     * "manifest").
     *
     * Returns: (transfer none): the backend identifier string
     */
    const gchar * (* get_backend_id) (LrgDlcOwnership *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

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
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_ownership_check_ownership (LrgDlcOwnership  *self,
                                            const gchar      *dlc_id,
                                            GError          **error);

/**
 * lrg_dlc_ownership_refresh_ownership:
 * @self: a #LrgDlcOwnership
 * @error: (nullable): return location for a #GError
 *
 * Refreshes the ownership cache from the backend.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_ownership_refresh_ownership (LrgDlcOwnership  *self,
                                              GError          **error);

/**
 * lrg_dlc_ownership_get_backend_id:
 * @self: a #LrgDlcOwnership
 *
 * Gets an identifier for this ownership backend.
 *
 * Returns: (transfer none): the backend identifier string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_ownership_get_backend_id (LrgDlcOwnership *self);

G_END_DECLS
