/* lrg-dlc-ownership-license.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * License file-based DLC ownership verification.
 *
 * This implementation verifies DLC ownership by checking for the
 * presence and validity of a license file within the DLC directory.
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

#define LRG_TYPE_DLC_OWNERSHIP_LICENSE (lrg_dlc_ownership_license_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDlcOwnershipLicense, lrg_dlc_ownership_license, LRG, DLC_OWNERSHIP_LICENSE, GObject)

/**
 * lrg_dlc_ownership_license_new:
 * @license_filename: (nullable): the license filename (default: "license.key")
 *
 * Creates a new license file-based DLC ownership checker.
 *
 * The checker looks for the specified license file within the DLC's
 * base directory. If the file exists and is valid, ownership is granted.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipLicense
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnershipLicense * lrg_dlc_ownership_license_new (const gchar *license_filename);

/**
 * lrg_dlc_ownership_license_get_license_filename:
 * @self: a #LrgDlcOwnershipLicense
 *
 * Gets the license filename being used.
 *
 * Returns: (transfer none): the license filename
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_ownership_license_get_license_filename (LrgDlcOwnershipLicense *self);

/**
 * lrg_dlc_ownership_license_set_license_filename:
 * @self: a #LrgDlcOwnershipLicense
 * @filename: the license filename to use
 *
 * Sets the license filename to look for.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_license_set_license_filename (LrgDlcOwnershipLicense *self,
                                                      const gchar            *filename);

/**
 * lrg_dlc_ownership_license_register_dlc:
 * @self: a #LrgDlcOwnershipLicense
 * @dlc_id: the DLC identifier
 * @base_path: the base directory path for this DLC
 *
 * Registers a DLC with its base directory path.
 *
 * When check_ownership() is called, the checker will look for
 * the license file at @base_path/@license_filename.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_license_register_dlc (LrgDlcOwnershipLicense *self,
                                              const gchar            *dlc_id,
                                              const gchar            *base_path);

/**
 * lrg_dlc_ownership_license_unregister_dlc:
 * @self: a #LrgDlcOwnershipLicense
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC registration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_license_unregister_dlc (LrgDlcOwnershipLicense *self,
                                                const gchar            *dlc_id);

/**
 * LrgDlcLicenseValidateFunc:
 * @license_data: the contents of the license file
 * @user_data: user data passed to the function
 *
 * Callback function for custom license validation.
 *
 * Returns: %TRUE if the license is valid, %FALSE otherwise
 */
typedef gboolean (*LrgDlcLicenseValidateFunc) (const gchar *license_data,
                                                gpointer     user_data);

/**
 * lrg_dlc_ownership_license_set_validator:
 * @self: a #LrgDlcOwnershipLicense
 * @validator: (nullable) (scope notified): the validation function
 * @user_data: (nullable): user data for the callback
 * @destroy: (nullable): destroy notify for @user_data
 *
 * Sets a custom validation function for license files.
 *
 * If no validator is set, the checker simply verifies that the
 * license file exists and is non-empty.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_ownership_license_set_validator (LrgDlcOwnershipLicense    *self,
                                               LrgDlcLicenseValidateFunc  validator,
                                               gpointer                   user_data,
                                               GDestroyNotify             destroy);

G_END_DECLS
