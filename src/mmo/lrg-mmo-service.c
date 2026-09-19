/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-service-private.h"

gboolean
_lrg_mmo_fail (GError **error, GIOErrorEnum code, const gchar *message)
{
    g_set_error_literal (error, G_IO_ERROR, code, message);
    return FALSE;
}

gboolean
_lrg_mmo_id_valid (const gchar *id)
{
    const gchar *p;
    if (id == NULL || *id == '\0' || strlen (id) > 64)
        return FALSE;
    for (p = id; *p != '\0'; p++)
        if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-')
            return FALSE;
    return TRUE;
}

GVariant *
_lrg_mmo_load (LrgMmoStore *store, const gchar *key, const gchar *type,
               guint64 *revision, GError **error)
{
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GVariant) value = NULL;
    bytes = lrg_mmo_store_read (store, key, revision, error);
    if (bytes == NULL)
        return NULL;
    value = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE (type), bytes, FALSE));
    if (!g_variant_is_normal_form (value))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Malformed stored service record");
        return NULL;
    }
#if G_BYTE_ORDER == G_BIG_ENDIAN
    return g_variant_byteswap (value);
#else
    return g_steal_pointer (&value);
#endif
}

void
_lrg_mmo_change (GVariantBuilder *batch, const gchar *key, guint64 revision, GVariant *value)
{
    g_autoptr(GVariant) owned = g_variant_ref_sink (value);
    g_autoptr(GBytes) bytes = NULL;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    g_autoptr(GVariant) swapped = g_variant_byteswap (owned);
    bytes = g_variant_get_data_as_bytes (swapped);
#else
    bytes = g_variant_get_data_as_bytes (owned);
#endif
    g_variant_builder_add (batch, "(st@ay)", key, revision,
                           g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE));
}

gboolean
_lrg_mmo_put (LrgMmoStore *store, const gchar *key, guint64 revision, GVariant *value, GError **error)
{
    GVariantBuilder batch;
    g_autoptr(GVariant) changes = NULL;
    g_variant_builder_init (&batch, G_VARIANT_TYPE ("a(stay)"));
    _lrg_mmo_change (&batch, key, revision, value);
    changes = g_variant_ref_sink (g_variant_builder_end (&batch));
    return lrg_mmo_store_commit (store, changes, error);
}

static gchar *
intent_digest (GVariant *intent)
{
    g_autoptr(GVariant) canonical = NULL;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    canonical = g_variant_byteswap (intent);
#else
    canonical = g_variant_ref (intent);
#endif
    return g_compute_checksum_for_data (G_CHECKSUM_SHA256, g_variant_get_data (canonical),
                                         g_variant_get_size (canonical));
}

gint
_lrg_mmo_operation_check (LrgMmoStore *store, const gchar *operation, GVariant *intent, GError **error)
{
    g_autofree gchar *key = NULL;
    g_autofree gchar *digest = NULL;
    g_autoptr(GVariant) receipt = NULL;
    g_autoptr(GError) local_error = NULL;
    guint64 revision;
    if (!_lrg_mmo_id_valid (operation))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid durable operation ID");
        return -1;
    }
    key = g_strconcat ("service/operation/", operation, NULL);
    receipt = _lrg_mmo_load (store, key, "s", &revision, &local_error);
    if (receipt == NULL)
    {
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return 0;
        g_propagate_error (error, g_steal_pointer (&local_error));
        return -1;
    }
    digest = intent_digest (intent);
    if (!g_str_equal (digest, g_variant_get_string (receipt, NULL)))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Operation ID reused for another intent");
        return -1;
    }
    return 1;
}

void
_lrg_mmo_operation_add (GVariantBuilder *batch, const gchar *operation, GVariant *intent)
{
    g_autofree gchar *key = g_strconcat ("service/operation/", operation, NULL);
    g_autofree gchar *digest = intent_digest (intent);
    _lrg_mmo_change (batch, key, 0, g_variant_new_string (digest));
}
