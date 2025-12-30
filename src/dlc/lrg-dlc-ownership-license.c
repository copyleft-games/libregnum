/* lrg-dlc-ownership-license.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * License file-based DLC ownership verification implementation.
 */

#include "lrg-dlc-ownership-license.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

/**
 * SECTION:lrg-dlc-ownership-license
 * @title: LrgDlcOwnershipLicense
 * @short_description: License file-based DLC ownership verification
 *
 * #LrgDlcOwnershipLicense implements the #LrgDlcOwnership interface
 * by checking for the presence and validity of license files.
 *
 * This is useful for DRM-free distribution where users receive
 * a license key file with their purchase.
 *
 * Since: 1.0
 */

struct _LrgDlcOwnershipLicense
{
    GObject parent_instance;

    /* License filename to look for (default: "license.key") */
    gchar *license_filename;

    /* DLC ID -> base path mapping */
    GHashTable *dlc_paths;

    /* Custom validation function */
    LrgDlcLicenseValidateFunc validator;
    gpointer validator_data;
    GDestroyNotify validator_destroy;
};

static void lrg_dlc_ownership_license_ownership_init (LrgDlcOwnershipInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgDlcOwnershipLicense, lrg_dlc_ownership_license, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DLC_OWNERSHIP,
                                                lrg_dlc_ownership_license_ownership_init))

#define DEFAULT_LICENSE_FILENAME "license.key"

/* ==========================================================================
 * Private Functions
 * ========================================================================== */

static gboolean
validate_license_file (LrgDlcOwnershipLicense *self,
                       const gchar            *license_path,
                       GError                **error)
{
    g_autofree gchar *contents = NULL;
    gsize length;
    GError *local_error = NULL;

    /* Try to read the license file */
    if (!g_file_get_contents (license_path, &contents, &length, &local_error))
    {
        if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
            g_set_error (error,
                         LRG_DLC_ERROR,
                         LRG_DLC_ERROR_NOT_OWNED,
                         "License file not found: %s",
                         license_path);
        }
        else
        {
            g_set_error (error,
                         LRG_DLC_ERROR,
                         LRG_DLC_ERROR_FAILED,
                         "Failed to read license file: %s",
                         local_error->message);
        }
        g_error_free (local_error);
        return FALSE;
    }

    /* Check if file is empty */
    if (length == 0)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_INVALID_LICENSE,
                     "License file is empty: %s",
                     license_path);
        return FALSE;
    }

    /* Use custom validator if provided */
    if (self->validator != NULL)
    {
        if (!self->validator (contents, self->validator_data))
        {
            g_set_error (error,
                         LRG_DLC_ERROR,
                         LRG_DLC_ERROR_INVALID_LICENSE,
                         "License validation failed for: %s",
                         license_path);
            return FALSE;
        }
    }

    return TRUE;
}

/* ==========================================================================
 * LrgDlcOwnership Interface Implementation
 * ========================================================================== */

static gboolean
lrg_dlc_ownership_license_check_ownership_impl (LrgDlcOwnership  *ownership,
                                                 const gchar      *dlc_id,
                                                 GError          **error)
{
    LrgDlcOwnershipLicense *self = LRG_DLC_OWNERSHIP_LICENSE (ownership);
    const gchar *base_path;
    g_autofree gchar *license_path = NULL;

    /* Look up the base path for this DLC */
    base_path = g_hash_table_lookup (self->dlc_paths, dlc_id);
    if (base_path == NULL)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_FAILED,
                     "DLC '%s' is not registered with license ownership checker",
                     dlc_id);
        return FALSE;
    }

    /* Build the license file path */
    license_path = g_build_filename (base_path, self->license_filename, NULL);

    return validate_license_file (self, license_path, error);
}

static const gchar *
lrg_dlc_ownership_license_get_backend_id_impl (LrgDlcOwnership *ownership)
{
    return "license";
}

static void
lrg_dlc_ownership_license_ownership_init (LrgDlcOwnershipInterface *iface)
{
    iface->check_ownership = lrg_dlc_ownership_license_check_ownership_impl;
    iface->get_backend_id = lrg_dlc_ownership_license_get_backend_id_impl;
    /* refresh_ownership not implemented - just uses default (TRUE) */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_dlc_ownership_license_dispose (GObject *object)
{
    LrgDlcOwnershipLicense *self = LRG_DLC_OWNERSHIP_LICENSE (object);

    g_clear_pointer (&self->dlc_paths, g_hash_table_unref);

    if (self->validator_destroy != NULL && self->validator_data != NULL)
    {
        self->validator_destroy (self->validator_data);
        self->validator_data = NULL;
    }
    self->validator = NULL;
    self->validator_destroy = NULL;

    G_OBJECT_CLASS (lrg_dlc_ownership_license_parent_class)->dispose (object);
}

static void
lrg_dlc_ownership_license_finalize (GObject *object)
{
    LrgDlcOwnershipLicense *self = LRG_DLC_OWNERSHIP_LICENSE (object);

    g_clear_pointer (&self->license_filename, g_free);

    G_OBJECT_CLASS (lrg_dlc_ownership_license_parent_class)->finalize (object);
}

static void
lrg_dlc_ownership_license_class_init (LrgDlcOwnershipLicenseClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_dlc_ownership_license_dispose;
    object_class->finalize = lrg_dlc_ownership_license_finalize;
}

static void
lrg_dlc_ownership_license_init (LrgDlcOwnershipLicense *self)
{
    self->license_filename = g_strdup (DEFAULT_LICENSE_FILENAME);
    self->dlc_paths = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, g_free);
    self->validator = NULL;
    self->validator_data = NULL;
    self->validator_destroy = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_dlc_ownership_license_new:
 * @license_filename: (nullable): the license filename (default: "license.key")
 *
 * Creates a new license file-based DLC ownership checker.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipLicense
 */
LrgDlcOwnershipLicense *
lrg_dlc_ownership_license_new (const gchar *license_filename)
{
    LrgDlcOwnershipLicense *self;

    self = g_object_new (LRG_TYPE_DLC_OWNERSHIP_LICENSE, NULL);

    if (license_filename != NULL)
        lrg_dlc_ownership_license_set_license_filename (self, license_filename);

    return self;
}

/**
 * lrg_dlc_ownership_license_get_license_filename:
 * @self: a #LrgDlcOwnershipLicense
 *
 * Gets the license filename being used.
 *
 * Returns: (transfer none): the license filename
 */
const gchar *
lrg_dlc_ownership_license_get_license_filename (LrgDlcOwnershipLicense *self)
{
    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP_LICENSE (self), NULL);

    return self->license_filename;
}

/**
 * lrg_dlc_ownership_license_set_license_filename:
 * @self: a #LrgDlcOwnershipLicense
 * @filename: the license filename to use
 *
 * Sets the license filename to look for.
 */
void
lrg_dlc_ownership_license_set_license_filename (LrgDlcOwnershipLicense *self,
                                                 const gchar            *filename)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_LICENSE (self));
    g_return_if_fail (filename != NULL);

    g_free (self->license_filename);
    self->license_filename = g_strdup (filename);
}

/**
 * lrg_dlc_ownership_license_register_dlc:
 * @self: a #LrgDlcOwnershipLicense
 * @dlc_id: the DLC identifier
 * @base_path: the base directory path for this DLC
 *
 * Registers a DLC with its base directory path.
 */
void
lrg_dlc_ownership_license_register_dlc (LrgDlcOwnershipLicense *self,
                                         const gchar            *dlc_id,
                                         const gchar            *base_path)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_LICENSE (self));
    g_return_if_fail (dlc_id != NULL);
    g_return_if_fail (base_path != NULL);

    g_hash_table_insert (self->dlc_paths,
                         g_strdup (dlc_id),
                         g_strdup (base_path));
}

/**
 * lrg_dlc_ownership_license_unregister_dlc:
 * @self: a #LrgDlcOwnershipLicense
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC registration.
 */
void
lrg_dlc_ownership_license_unregister_dlc (LrgDlcOwnershipLicense *self,
                                           const gchar            *dlc_id)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_LICENSE (self));
    g_return_if_fail (dlc_id != NULL);

    g_hash_table_remove (self->dlc_paths, dlc_id);
}

/**
 * lrg_dlc_ownership_license_set_validator:
 * @self: a #LrgDlcOwnershipLicense
 * @validator: (nullable) (scope notified): the validation function
 * @user_data: (nullable): user data for the callback
 * @destroy: (nullable): destroy notify for @user_data
 *
 * Sets a custom validation function for license files.
 */
void
lrg_dlc_ownership_license_set_validator (LrgDlcOwnershipLicense    *self,
                                          LrgDlcLicenseValidateFunc  validator,
                                          gpointer                   user_data,
                                          GDestroyNotify             destroy)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_LICENSE (self));

    /* Clean up old validator */
    if (self->validator_destroy != NULL && self->validator_data != NULL)
        self->validator_destroy (self->validator_data);

    self->validator = validator;
    self->validator_data = user_data;
    self->validator_destroy = destroy;
}
