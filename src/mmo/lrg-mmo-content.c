/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-content.h"
#include "lrg-mmo-service-private.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>

static gboolean
safe_path (const gchar *path)
{
    g_auto(GStrv) parts = NULL;
    guint i;
    if (path == NULL || *path == '\0' || strlen (path) > 256 || !g_utf8_validate (path, -1, NULL) ||
        g_path_is_absolute (path) || strchr (path, '\\') != NULL || strchr (path, ':') != NULL)
        return FALSE;
    parts = g_strsplit (path, "/", -1);
    for (i = 0; parts[i] != NULL; i++)
        if (*parts[i] == '\0' || g_str_equal (parts[i], ".") || g_str_equal (parts[i], ".."))
            return FALSE;
    return TRUE;
}
static gboolean
valid_manifest (GVariant *manifest, GError **error)
{
    g_autoptr(GVariant) files = NULL;
    g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GVariantIter iter;
    guint64 version, size, total = 0;
    const gchar *release, *path;
    GVariant *digest;
    if (manifest == NULL || !g_variant_is_of_type (manifest, G_VARIANT_TYPE ("(tsa(stay))")) ||
        g_variant_get_size (manifest) > 1024 * 1024 || !g_variant_is_normal_form (manifest))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid content manifest");
    g_variant_get (manifest, "(t&s@a(stay))", &version, &release, &files);
    if (version != 1 || !_lrg_mmo_id_valid (release) || g_variant_n_children (files) > 4096)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Unsupported release or manifest version");
    g_variant_iter_init (&iter, files);
    while (g_variant_iter_next (&iter, "(&st@ay)", &path, &size, &digest))
    {
        gboolean valid = safe_path (path) && size <= 1024 * 1024 * 1024 &&
                         g_variant_get_size (digest) == 32 && !g_hash_table_contains (seen, path);
        g_variant_unref (digest);
        total += size <= 1024 * 1024 * 1024 ? size : 0;
        if (!valid || total > G_GUINT64_CONSTANT (4294967296))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Unsafe, duplicate or oversized content entry");
        g_hash_table_add (seen, g_strdup (path));
    }
    return TRUE;
}

GVariant *
lrg_mmo_content_verify (GBytes *manifest, GBytes *signature, GBytes *public_key, GError **error)
{
    EVP_PKEY *key;
    EVP_MD_CTX *context;
    gint valid;
    g_autoptr(GVariant) value = NULL;
    if (manifest == NULL || signature == NULL || public_key == NULL ||
        g_bytes_get_size (manifest) > 1024 * 1024 || g_bytes_get_size (signature) != 64 ||
        g_bytes_get_size (public_key) != 32)
        goto invalid;
    key = EVP_PKEY_new_raw_public_key (EVP_PKEY_ED25519, NULL, g_bytes_get_data (public_key, NULL), 32);
    context = EVP_MD_CTX_new ();
    valid = key != NULL && context != NULL && EVP_DigestVerifyInit (context, NULL, NULL, NULL, key) == 1 &&
            EVP_DigestVerify (context, g_bytes_get_data (signature, NULL), 64,
                              g_bytes_get_data (manifest, NULL), g_bytes_get_size (manifest)) == 1;
    EVP_MD_CTX_free (context);
    EVP_PKEY_free (key);
    if (!valid)
        goto invalid;
    value = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE ("(tsa(stay))"), manifest, FALSE));
#if G_BYTE_ORDER == G_BIG_ENDIAN
    {
        GVariant *swapped = g_variant_byteswap (value);
        g_variant_unref (value);
        value = swapped;
    }
#endif
    if (!valid_manifest (value, error))
        return NULL;
    return g_steal_pointer (&value);
invalid:
    _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Invalid content signature or public key");
    return NULL;
}

static GFile *
regular_file (const gchar *directory, const gchar *relative, GError **error)
{
    g_auto(GStrv) parts = g_strsplit (relative, "/", -1);
    g_autoptr(GFile) file = g_file_new_for_path (directory);
    guint i;
    for (i = 0; parts[i] != NULL; i++)
    {
        g_autoptr(GFileInfo) info = NULL;
        GFile *child = g_file_get_child (file, parts[i]);
        g_object_unref (file);
        file = child;
        info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
        if (info == NULL)
            return NULL;
        if (g_file_info_get_file_type (info) != (parts[i + 1] == NULL ? G_FILE_TYPE_REGULAR : G_FILE_TYPE_DIRECTORY))
        {
            _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Content path is not a regular file or contains a symlink");
            return NULL;
        }
    }
    return g_steal_pointer (&file);
}

gboolean
lrg_mmo_content_verify_directory (GVariant *manifest, const gchar *directory, GError **error)
{
    g_autoptr(GVariant) files = NULL;
    GVariantIter iter;
    GVariant *digest;
    const gchar *path;
    guint64 size;
    g_return_val_if_fail (directory != NULL, FALSE);
    if (!valid_manifest (manifest, error))
        return FALSE;
    files = g_variant_get_child_value (manifest, 2);
    g_variant_iter_init (&iter, files);
    while (g_variant_iter_next (&iter, "(&st@ay)", &path, &size, &digest))
    {
        g_autoptr(GVariant) expected = digest;
        g_autoptr(GFile) file = regular_file (directory, path, error);
        g_autoptr(GFileInputStream) input = NULL;
        g_autoptr(GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA256);
        guint8 buffer[65536], actual[32];
        gsize digest_size = sizeof actual;
        guint64 received = 0;
        gssize count;
        if (file == NULL)
            return FALSE;
        input = g_file_read (file, NULL, error);
        if (input == NULL)
            return FALSE;
        while ((count = g_input_stream_read (G_INPUT_STREAM (input), buffer, sizeof buffer, NULL, error)) > 0)
        {
            received += count;
            if (received > size)
                return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Content exceeds declared size");
            g_checksum_update (checksum, buffer, count);
        }
        if (count < 0)
            return FALSE;
        g_checksum_get_digest (checksum, actual, &digest_size);
        if (received != size || CRYPTO_memcmp (actual, g_variant_get_data (expected), sizeof actual) != 0)
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Content size or digest mismatch");
    }
    return TRUE;
}
