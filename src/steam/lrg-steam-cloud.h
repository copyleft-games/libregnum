/* lrg-steam-cloud.h - Steam Cloud remote storage wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_CLOUD_H
#define LRG_STEAM_CLOUD_H

#include <glib-object.h>
#include "lrg-steam-client.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_CLOUD (lrg_steam_cloud_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamCloud, lrg_steam_cloud, LRG, STEAM_CLOUD, GObject)

/**
 * LRG_STEAM_CLOUD_ERROR:
 *
 * Error domain for Steam Cloud errors.
 */
#define LRG_STEAM_CLOUD_ERROR (lrg_steam_cloud_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_steam_cloud_error_quark (void);

/**
 * LrgSteamCloudError:
 * @LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED: Steam not initialized
 * @LRG_STEAM_CLOUD_ERROR_NOT_ENABLED: Cloud storage not enabled
 * @LRG_STEAM_CLOUD_ERROR_WRITE_FAILED: Failed to write file
 * @LRG_STEAM_CLOUD_ERROR_READ_FAILED: Failed to read file
 * @LRG_STEAM_CLOUD_ERROR_DELETE_FAILED: Failed to delete file
 * @LRG_STEAM_CLOUD_ERROR_NOT_FOUND: File not found
 *
 * Error codes for Steam Cloud operations.
 */
typedef enum
{
    LRG_STEAM_CLOUD_ERROR_NOT_INITIALIZED,
    LRG_STEAM_CLOUD_ERROR_NOT_ENABLED,
    LRG_STEAM_CLOUD_ERROR_WRITE_FAILED,
    LRG_STEAM_CLOUD_ERROR_READ_FAILED,
    LRG_STEAM_CLOUD_ERROR_DELETE_FAILED,
    LRG_STEAM_CLOUD_ERROR_NOT_FOUND
} LrgSteamCloudError;

/**
 * lrg_steam_cloud_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam Cloud manager.
 *
 * Returns: (transfer full): A new #LrgSteamCloud
 */
LRG_AVAILABLE_IN_ALL
LrgSteamCloud *
lrg_steam_cloud_new (LrgSteamClient *client);

/**
 * lrg_steam_cloud_is_enabled:
 * @self: an #LrgSteamCloud
 *
 * Checks if Steam Cloud is enabled for this user and app.
 *
 * Returns: %TRUE if cloud storage is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_cloud_is_enabled (LrgSteamCloud *self);

/**
 * lrg_steam_cloud_write:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 * @data: the data to write
 * @error: (nullable): return location for error
 *
 * Writes data to Steam Cloud. The file will be synced to the
 * cloud automatically.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_cloud_write (LrgSteamCloud  *self,
                       const gchar    *filename,
                       GBytes         *data,
                       GError        **error);

/**
 * lrg_steam_cloud_read:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 * @error: (nullable): return location for error
 *
 * Reads data from Steam Cloud.
 *
 * Returns: (transfer full) (nullable): The file data, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_steam_cloud_read (LrgSteamCloud  *self,
                      const gchar    *filename,
                      GError        **error);

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
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_cloud_delete (LrgSteamCloud  *self,
                        const gchar    *filename,
                        GError        **error);

/**
 * lrg_steam_cloud_exists:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 *
 * Checks if a file exists in Steam Cloud.
 *
 * Returns: %TRUE if the file exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_steam_cloud_exists (LrgSteamCloud *self,
                        const gchar   *filename);

/**
 * lrg_steam_cloud_get_file_size:
 * @self: an #LrgSteamCloud
 * @filename: the remote filename
 *
 * Gets the size of a file in Steam Cloud.
 *
 * Returns: The file size in bytes, or -1 if not found
 */
LRG_AVAILABLE_IN_ALL
gint32
lrg_steam_cloud_get_file_size (LrgSteamCloud *self,
                               const gchar   *filename);

/**
 * lrg_steam_cloud_get_file_count:
 * @self: an #LrgSteamCloud
 *
 * Gets the number of files stored in Steam Cloud.
 *
 * Returns: The number of files
 */
LRG_AVAILABLE_IN_ALL
gint32
lrg_steam_cloud_get_file_count (LrgSteamCloud *self);

/**
 * lrg_steam_cloud_get_file_name:
 * @self: an #LrgSteamCloud
 * @index: the file index (0-based)
 * @size: (out) (optional): return location for file size
 *
 * Gets the name and size of a file by index.
 *
 * Returns: (transfer none) (nullable): The filename
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_steam_cloud_get_file_name (LrgSteamCloud *self,
                               gint           index,
                               gint32        *size);

G_END_DECLS

#endif /* LRG_STEAM_CLOUD_H */
