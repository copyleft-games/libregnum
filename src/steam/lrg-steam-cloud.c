/* lrg-steam-cloud.c - Steam Cloud remote storage wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-cloud.h"
#include "lrg-steam-types.h"

/**
 * SECTION:lrg-steam-cloud
 * @title: LrgSteamCloud
 * @short_description: Steam Cloud remote storage
 *
 * #LrgSteamCloud provides access to Steam Cloud for save game
 * synchronization. Files written to Steam Cloud are automatically
 * synced across the user's devices.
 *
 * Steam Cloud must be enabled in the Steamworks app configuration
 * and the user must have cloud saves enabled in their Steam settings.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgSteamCloud) cloud;
 * g_autoptr(GBytes) save_data = NULL;
 * g_autoptr(GError) error = NULL;
 *
 * cloud = lrg_steam_cloud_new (client);
 *
 * // Write save file
 * save_data = g_bytes_new (data, data_len);
 * if (!lrg_steam_cloud_write (cloud, "save1.dat", save_data, &error))
 * {
 *     g_warning ("Cloud save failed: %s", error->message);
 * }
 *
 * // Read save file
 * save_data = lrg_steam_cloud_read (cloud, "save1.dat", &error);
 * if (save_data == NULL)
 * {
 *     g_warning ("Cloud load failed: %s", error->message);
 * }
 * ]|
 */

struct _LrgSteamCloud
{
    GObject         parent_instance;
    LrgSteamClient *client;
};

enum
{
    PROP_0,
    PROP_CLIENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgSteamCloud, lrg_steam_cloud, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-steam-cloud-error-quark, lrg_steam_cloud_error)

static void
lrg_steam_cloud_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgSteamCloud *self = LRG_STEAM_CLOUD (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_clear_object (&self->client);
        self->client = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_cloud_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgSteamCloud *self = LRG_STEAM_CLOUD (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_value_set_object (value, self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_cloud_dispose (GObject *object)
{
    LrgSteamCloud *self = LRG_STEAM_CLOUD (object);

    g_clear_object (&self->client);

    G_OBJECT_CLASS (lrg_steam_cloud_parent_class)->dispose (object);
}

static void
lrg_steam_cloud_class_init (LrgSteamCloudClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_steam_cloud_set_property;
    object_class->get_property = lrg_steam_cloud_get_property;
    object_class->dispose = lrg_steam_cloud_dispose;

    /**
     * LrgSteamCloud:client:
     *
     * The Steam client to use.
     */
    properties[PROP_CLIENT] =
        g_param_spec_object ("client",
                             "Client",
                             "The Steam client",
                             LRG_TYPE_STEAM_CLIENT,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_steam_cloud_init (LrgSteamCloud *self)
{
    self->client = NULL;
}

/**
 * lrg_steam_cloud_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam Cloud manager.
 *
 * Returns: (transfer full): A new #LrgSteamCloud
 */
LrgSteamCloud *
lrg_steam_cloud_new (LrgSteamClient *client)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (client), NULL);

    return g_object_new (LRG_TYPE_STEAM_CLOUD,
                         "client", client,
                         NULL);
}

/**
 * lrg_steam_cloud_is_enabled:
 * @self: an #LrgSteamCloud
 *
 * Checks if Steam Cloud is enabled.
 *
 * Returns: %TRUE if cloud storage is enabled
 */
gboolean
lrg_steam_cloud_is_enabled (LrgSteamCloud *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
        return FALSE;

    return SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount (storage) &&
           SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp (storage);
#else
    return FALSE;
#endif
}

/**
 * lrg_steam_cloud_write:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 * @data: the data to write
 * @error: (nullable): return location for error
 *
 * Writes data to Steam Cloud.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_cloud_write (LrgSteamCloud  *self,
                       const gchar    *filename,
                       GBytes         *data,
                       GError        **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    g_return_val_if_fail (data != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;
    gsize size;
    gconstpointer bytes_data;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return FALSE;
    }

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam remote storage interface not available");
        return FALSE;
    }

    if (!lrg_steam_cloud_is_enabled (self))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_ENABLED,
                     "Steam Cloud is not enabled");
        return FALSE;
    }

    bytes_data = g_bytes_get_data (data, &size);

    if (!SteamAPI_ISteamRemoteStorage_FileWrite (storage, filename, bytes_data, (int32_t)size))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_WRITE_FAILED,
                     "Failed to write file: %s", filename);
        return FALSE;
    }

    g_debug ("Cloud file written: %s (%zu bytes)", filename, size);
    return TRUE;
#else
    g_debug ("Steam stub: write cloud file %s (no-op)", filename);
    return TRUE;
#endif
}

/**
 * lrg_steam_cloud_read:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 * @error: (nullable): return location for error
 *
 * Reads data from Steam Cloud.
 *
 * Returns: (transfer full) (nullable): The file data
 */
GBytes *
lrg_steam_cloud_read (LrgSteamCloud  *self,
                      const gchar    *filename,
                      GError        **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), NULL);
    g_return_val_if_fail (filename != NULL, NULL);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;
    int32_t file_size;
    int32_t bytes_read;
    gchar *buffer;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return NULL;
    }

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam remote storage interface not available");
        return NULL;
    }

    if (!SteamAPI_ISteamRemoteStorage_FileExists (storage, filename))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_FOUND,
                     "File not found: %s", filename);
        return NULL;
    }

    file_size = SteamAPI_ISteamRemoteStorage_GetFileSize (storage, filename);
    if (file_size <= 0)
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_READ_FAILED,
                     "Invalid file size for: %s", filename);
        return NULL;
    }

    buffer = g_malloc (file_size);
    bytes_read = SteamAPI_ISteamRemoteStorage_FileRead (storage, filename, buffer, file_size);

    if (bytes_read != file_size)
    {
        g_free (buffer);
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_READ_FAILED,
                     "Failed to read file: %s (expected %d, got %d)",
                     filename, file_size, bytes_read);
        return NULL;
    }

    g_debug ("Cloud file read: %s (%d bytes)", filename, bytes_read);
    return g_bytes_new_take (buffer, bytes_read);
#else
    g_set_error (error,
                 LRG_STEAM_CLOUD_ERROR,
                 LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                 "Steam not supported (build with STEAM=1)");
    return NULL;
#endif
}

/**
 * lrg_steam_cloud_delete:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 * @error: (nullable): return location for error
 *
 * Deletes a file from Steam Cloud.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_cloud_delete (LrgSteamCloud  *self,
                        const gchar    *filename,
                        GError        **error)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam not initialized");
        return FALSE;
    }

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
                     "Steam remote storage interface not available");
        return FALSE;
    }

    if (!SteamAPI_ISteamRemoteStorage_FileDelete (storage, filename))
    {
        g_set_error (error,
                     LRG_STEAM_CLOUD_ERROR,
                     LRG_STEAM_CLOUD_ERROR_DELETE_FAILED,
                     "Failed to delete file: %s", filename);
        return FALSE;
    }

    g_debug ("Cloud file deleted: %s", filename);
    return TRUE;
#else
    g_debug ("Steam stub: delete cloud file %s (no-op)", filename);
    return TRUE;
#endif
}

/**
 * lrg_steam_cloud_exists:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 *
 * Checks if a file exists in Steam Cloud.
 *
 * Returns: %TRUE if the file exists
 */
gboolean
lrg_steam_cloud_exists (LrgSteamCloud *self,
                        const gchar   *filename)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
        return FALSE;

    return SteamAPI_ISteamRemoteStorage_FileExists (storage, filename);
#else
    return FALSE;
#endif
}

/**
 * lrg_steam_cloud_get_file_size:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 *
 * Gets the size of a file in Steam Cloud.
 *
 * Returns: The file size in bytes, or -1 if not found
 */
gint32
lrg_steam_cloud_get_file_size (LrgSteamCloud *self,
                               const gchar   *filename)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), -1);
    g_return_val_if_fail (filename != NULL, -1);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return -1;

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
        return -1;

    if (!SteamAPI_ISteamRemoteStorage_FileExists (storage, filename))
        return -1;

    return SteamAPI_ISteamRemoteStorage_GetFileSize (storage, filename);
#else
    return -1;
#endif
}

/**
 * lrg_steam_cloud_get_file_count:
 * @self: an #LrgSteamCloud
 *
 * Gets the number of files in Steam Cloud.
 *
 * Returns: The number of files
 */
gint32
lrg_steam_cloud_get_file_count (LrgSteamCloud *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), 0);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return 0;

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
        return 0;

    return SteamAPI_ISteamRemoteStorage_GetFileCount (storage);
#else
    return 0;
#endif
}

/**
 * lrg_steam_cloud_get_file_name:
 * @self: an #LrgSteamCloud
 * @index: the file index
 * @size: (out) (optional): return location for file size
 *
 * Gets the name and size of a file by index.
 *
 * Returns: (transfer none) (nullable): The filename
 */
const gchar *
lrg_steam_cloud_get_file_name (LrgSteamCloud *self,
                               gint           index,
                               gint32        *size)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLOUD (self), NULL);

#ifdef LRG_ENABLE_STEAM
    ISteamRemoteStorage *storage;
    int32_t file_size;
    const char *name;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return NULL;

    storage = SteamAPI_SteamRemoteStorage_v016 ();
    if (storage == NULL)
        return NULL;

    file_size = 0;
    name = SteamAPI_ISteamRemoteStorage_GetFileNameAndSize (storage, index, &file_size);

    if (size != NULL)
        *size = file_size;

    return name;
#else
    if (size != NULL)
        *size = 0;
    return NULL;
#endif
}
