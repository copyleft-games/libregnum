/* lrg-dlc-ownership-manifest.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manifest-based DLC ownership verification.
 *
 * This is a simple trust-based ownership checker that uses a flag
 * from the DLC manifest to determine ownership. Useful for development,
 * testing, or when all DLC should be unlocked.
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

#define LRG_TYPE_DLC_OWNERSHIP_MANIFEST (lrg_dlc_ownership_manifest_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDlcOwnershipManifest, lrg_dlc_ownership_manifest, LRG, DLC_OWNERSHIP_MANIFEST, GObject)

/**
 * lrg_dlc_ownership_manifest_new:
 *
 * Creates a new manifest-based DLC ownership checker.
 *
 * This checker uses a simple flag stored per-DLC to determine
 * ownership. It's useful for development or when DLC ownership
 * is determined by configuration rather than external services.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipManifest
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnershipManifest * lrg_dlc_ownership_manifest_new (void);

/**
 * lrg_dlc_ownership_manifest_set_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 * @owned: whether the DLC is owned
 *
 * Sets the ownership state for a DLC.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_manifest_set_owned (LrgDlcOwnershipManifest *self,
                                            const gchar             *dlc_id,
                                            gboolean                 owned);

/**
 * lrg_dlc_ownership_manifest_get_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 *
 * Gets the ownership state for a DLC.
 *
 * Returns: %TRUE if owned, %FALSE if not owned or not registered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_ownership_manifest_get_owned (LrgDlcOwnershipManifest *self,
                                                const gchar             *dlc_id);

/**
 * lrg_dlc_ownership_manifest_set_all_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @owned: whether all DLCs should be marked as owned
 *
 * Sets the default ownership state for unregistered DLCs.
 *
 * When this is set to %TRUE, any DLC not explicitly registered
 * will be considered owned. This is useful for development or
 * unlocking all content.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_manifest_set_all_owned (LrgDlcOwnershipManifest *self,
                                                gboolean                 owned);

/**
 * lrg_dlc_ownership_manifest_get_all_owned:
 * @self: a #LrgDlcOwnershipManifest
 *
 * Gets the default ownership state for unregistered DLCs.
 *
 * Returns: %TRUE if unregistered DLCs are considered owned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_ownership_manifest_get_all_owned (LrgDlcOwnershipManifest *self);

/**
 * lrg_dlc_ownership_manifest_unregister_dlc:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC registration, causing it to fall back to the
 * default all_owned setting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_manifest_unregister_dlc (LrgDlcOwnershipManifest *self,
                                                 const gchar             *dlc_id);

G_END_DECLS
