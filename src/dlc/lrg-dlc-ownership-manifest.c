/* lrg-dlc-ownership-manifest.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manifest-based DLC ownership verification implementation.
 */

#include "lrg-dlc-ownership-manifest.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

/**
 * SECTION:lrg-dlc-ownership-manifest
 * @title: LrgDlcOwnershipManifest
 * @short_description: Simple trust-based DLC ownership verification
 *
 * #LrgDlcOwnershipManifest implements the #LrgDlcOwnership interface
 * using simple boolean flags stored in memory.
 *
 * This is useful for:
 * - Development and testing (set all_owned = TRUE)
 * - Configuration-based unlocks
 * - DRM-free bundles where all DLC is included
 *
 * Since: 1.0
 */

struct _LrgDlcOwnershipManifest
{
    GObject parent_instance;

    /* DLC ID -> owned (gboolean as gpointer) */
    GHashTable *ownership;

    /* Default ownership for unregistered DLCs */
    gboolean all_owned;
};

static void lrg_dlc_ownership_manifest_ownership_init (LrgDlcOwnershipInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgDlcOwnershipManifest, lrg_dlc_ownership_manifest, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_DLC_OWNERSHIP,
                                                lrg_dlc_ownership_manifest_ownership_init))

/* ==========================================================================
 * LrgDlcOwnership Interface Implementation
 * ========================================================================== */

static gboolean
lrg_dlc_ownership_manifest_check_ownership_impl (LrgDlcOwnership  *ownership,
                                                  const gchar      *dlc_id,
                                                  GError          **error)
{
    LrgDlcOwnershipManifest *self = LRG_DLC_OWNERSHIP_MANIFEST (ownership);
    gpointer value;

    /* Check if DLC is registered */
    if (g_hash_table_lookup_extended (self->ownership, dlc_id, NULL, &value))
        return GPOINTER_TO_INT (value);

    /* Fall back to all_owned setting */
    if (!self->all_owned)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_NOT_OWNED,
                     "DLC '%s' is not owned (manifest verification)",
                     dlc_id);
    }

    return self->all_owned;
}

static const gchar *
lrg_dlc_ownership_manifest_get_backend_id_impl (LrgDlcOwnership *ownership)
{
    return "manifest";
}

static void
lrg_dlc_ownership_manifest_ownership_init (LrgDlcOwnershipInterface *iface)
{
    iface->check_ownership = lrg_dlc_ownership_manifest_check_ownership_impl;
    iface->get_backend_id = lrg_dlc_ownership_manifest_get_backend_id_impl;
    /* refresh_ownership not implemented - just uses default (TRUE) */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_dlc_ownership_manifest_dispose (GObject *object)
{
    LrgDlcOwnershipManifest *self = LRG_DLC_OWNERSHIP_MANIFEST (object);

    g_clear_pointer (&self->ownership, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_dlc_ownership_manifest_parent_class)->dispose (object);
}

static void
lrg_dlc_ownership_manifest_class_init (LrgDlcOwnershipManifestClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_dlc_ownership_manifest_dispose;
}

static void
lrg_dlc_ownership_manifest_init (LrgDlcOwnershipManifest *self)
{
    self->ownership = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->all_owned = FALSE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_dlc_ownership_manifest_new:
 *
 * Creates a new manifest-based DLC ownership checker.
 *
 * Returns: (transfer full): a new #LrgDlcOwnershipManifest
 */
LrgDlcOwnershipManifest *
lrg_dlc_ownership_manifest_new (void)
{
    return g_object_new (LRG_TYPE_DLC_OWNERSHIP_MANIFEST, NULL);
}

/**
 * lrg_dlc_ownership_manifest_set_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 * @owned: whether the DLC is owned
 *
 * Sets the ownership state for a DLC.
 */
void
lrg_dlc_ownership_manifest_set_owned (LrgDlcOwnershipManifest *self,
                                       const gchar             *dlc_id,
                                       gboolean                 owned)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_MANIFEST (self));
    g_return_if_fail (dlc_id != NULL);

    g_hash_table_insert (self->ownership,
                         g_strdup (dlc_id),
                         GINT_TO_POINTER (owned));
}

/**
 * lrg_dlc_ownership_manifest_get_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 *
 * Gets the ownership state for a DLC.
 *
 * Returns: %TRUE if owned, %FALSE if not owned or not registered
 */
gboolean
lrg_dlc_ownership_manifest_get_owned (LrgDlcOwnershipManifest *self,
                                       const gchar             *dlc_id)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP_MANIFEST (self), FALSE);
    g_return_val_if_fail (dlc_id != NULL, FALSE);

    if (g_hash_table_lookup_extended (self->ownership, dlc_id, NULL, &value))
        return GPOINTER_TO_INT (value);

    return self->all_owned;
}

/**
 * lrg_dlc_ownership_manifest_set_all_owned:
 * @self: a #LrgDlcOwnershipManifest
 * @owned: whether all DLCs should be marked as owned
 *
 * Sets the default ownership state for unregistered DLCs.
 */
void
lrg_dlc_ownership_manifest_set_all_owned (LrgDlcOwnershipManifest *self,
                                           gboolean                 owned)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_MANIFEST (self));

    self->all_owned = owned;
}

/**
 * lrg_dlc_ownership_manifest_get_all_owned:
 * @self: a #LrgDlcOwnershipManifest
 *
 * Gets the default ownership state for unregistered DLCs.
 *
 * Returns: %TRUE if unregistered DLCs are considered owned
 */
gboolean
lrg_dlc_ownership_manifest_get_all_owned (LrgDlcOwnershipManifest *self)
{
    g_return_val_if_fail (LRG_IS_DLC_OWNERSHIP_MANIFEST (self), FALSE);

    return self->all_owned;
}

/**
 * lrg_dlc_ownership_manifest_unregister_dlc:
 * @self: a #LrgDlcOwnershipManifest
 * @dlc_id: the DLC identifier
 *
 * Removes a DLC registration.
 */
void
lrg_dlc_ownership_manifest_unregister_dlc (LrgDlcOwnershipManifest *self,
                                            const gchar             *dlc_id)
{
    g_return_if_fail (LRG_IS_DLC_OWNERSHIP_MANIFEST (self));
    g_return_if_fail (dlc_id != NULL);

    g_hash_table_remove (self->ownership, dlc_id);
}
