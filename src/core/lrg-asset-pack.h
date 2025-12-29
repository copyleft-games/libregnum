/* lrg-asset-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Resource pack management for loading assets from rres files.
 *
 * LrgAssetPack wraps graylib's GrlResourcePack, providing a GObject
 * wrapper with game-specific features like typed asset loading
 * and integration with the data loading system.
 *
 * The rres format is a raylib resource format that supports:
 * - Multiple asset types (textures, sounds, music, raw data)
 * - Optional central directory for name-based lookups
 * - Compression and encryption support
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ASSET_PACK (lrg_asset_pack_get_type ())
#define LRG_ASSET_PACK_ERROR (lrg_asset_pack_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetPack, lrg_asset_pack, LRG, ASSET_PACK, GObject)

/**
 * LrgAssetPackError:
 * @LRG_ASSET_PACK_ERROR_FILE_NOT_FOUND: File could not be opened
 * @LRG_ASSET_PACK_ERROR_INVALID_FORMAT: Invalid rres file format
 * @LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND: Resource not found in pack
 * @LRG_ASSET_PACK_ERROR_LOAD_FAILED: Failed to load resource
 * @LRG_ASSET_PACK_ERROR_DECRYPT_FAILED: Failed to decrypt resource
 *
 * Error codes for #LrgAssetPack operations.
 */
typedef enum
{
    LRG_ASSET_PACK_ERROR_FILE_NOT_FOUND,
    LRG_ASSET_PACK_ERROR_INVALID_FORMAT,
    LRG_ASSET_PACK_ERROR_RESOURCE_NOT_FOUND,
    LRG_ASSET_PACK_ERROR_LOAD_FAILED,
    LRG_ASSET_PACK_ERROR_DECRYPT_FAILED
} LrgAssetPackError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_asset_pack_error_quark (void);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_asset_pack_new:
 * @path: (type filename): path to the rres file
 * @error: (nullable): return location for a #GError
 *
 * Opens an rres resource pack file.
 *
 * Returns: (transfer full) (nullable): A new #LrgAssetPack, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAssetPack * lrg_asset_pack_new (const gchar  *path,
                                    GError      **error);

/**
 * lrg_asset_pack_new_encrypted:
 * @path: (type filename): path to the rres file
 * @password: the decryption password
 * @error: (nullable): return location for a #GError
 *
 * Opens an encrypted rres resource pack file.
 *
 * Returns: (transfer full) (nullable): A new #LrgAssetPack, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAssetPack * lrg_asset_pack_new_encrypted (const gchar  *path,
                                              const gchar  *password,
                                              GError      **error);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_asset_pack_get_filename:
 * @self: a #LrgAssetPack
 *
 * Gets the filename of the resource pack.
 *
 * Returns: (transfer none): the filename
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_pack_get_filename (LrgAssetPack *self);

/**
 * lrg_asset_pack_get_resource_count:
 * @self: a #LrgAssetPack
 *
 * Gets the number of resource chunks in the pack.
 *
 * Returns: the resource count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_pack_get_resource_count (LrgAssetPack *self);

/**
 * lrg_asset_pack_get_version:
 * @self: a #LrgAssetPack
 *
 * Gets the rres format version.
 *
 * Returns: the version number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_pack_get_version (LrgAssetPack *self);

/**
 * lrg_asset_pack_has_directory:
 * @self: a #LrgAssetPack
 *
 * Checks if the pack has a central directory for name-based lookups.
 *
 * Returns: %TRUE if central directory is available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_pack_has_directory (LrgAssetPack *self);

/* ==========================================================================
 * Directory Access
 * ========================================================================== */

/**
 * lrg_asset_pack_list_resources:
 * @self: a #LrgAssetPack
 *
 * Gets a list of all resource names in the pack.
 *
 * Requires the pack to have a central directory.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): list of resource names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_asset_pack_list_resources (LrgAssetPack *self);

/**
 * lrg_asset_pack_get_id:
 * @self: a #LrgAssetPack
 * @name: the resource name
 *
 * Gets the resource ID for a name.
 *
 * Requires the pack to have a central directory.
 *
 * Returns: the resource ID, or 0 if not found
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_asset_pack_get_id (LrgAssetPack *self,
                                const gchar  *name);

/**
 * lrg_asset_pack_get_name:
 * @self: a #LrgAssetPack
 * @id: the resource ID
 *
 * Gets the resource name for an ID.
 *
 * Requires the pack to have a central directory.
 *
 * Returns: (transfer full) (nullable): the resource name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_pack_get_name (LrgAssetPack *self,
                                  guint32       id);

/**
 * lrg_asset_pack_contains:
 * @self: a #LrgAssetPack
 * @name: the resource name
 *
 * Checks if the pack contains a resource with the given name.
 *
 * Requires the pack to have a central directory.
 *
 * Returns: %TRUE if the resource exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_pack_contains (LrgAssetPack *self,
                                   const gchar  *name);

/* ==========================================================================
 * Raw Data Loading
 * ========================================================================== */

/**
 * lrg_asset_pack_load_raw:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @size: (out): return location for data size
 * @error: (nullable): return location for a #GError
 *
 * Loads raw resource data by name.
 *
 * Requires the pack to have a central directory.
 *
 * Returns: (transfer full) (array length=size) (nullable): the raw data bytes
 */
LRG_AVAILABLE_IN_ALL
guint8 * lrg_asset_pack_load_raw (LrgAssetPack  *self,
                                   const gchar   *name,
                                   gsize         *size,
                                   GError       **error);

/**
 * lrg_asset_pack_load_raw_by_id:
 * @self: a #LrgAssetPack
 * @id: the resource ID
 * @size: (out): return location for data size
 * @error: (nullable): return location for a #GError
 *
 * Loads raw resource data by ID.
 *
 * Returns: (transfer full) (array length=size) (nullable): the raw data bytes
 */
LRG_AVAILABLE_IN_ALL
guint8 * lrg_asset_pack_load_raw_by_id (LrgAssetPack  *self,
                                         guint32        id,
                                         gsize         *size,
                                         GError       **error);

/* ==========================================================================
 * Typed Asset Loading
 * ========================================================================== */

/**
 * lrg_asset_pack_load_texture:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads a texture from the pack.
 *
 * Returns: (transfer full) (nullable): A new #GrlTexture, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_asset_pack_load_texture (LrgAssetPack  *self,
                                           const gchar   *name,
                                           GError       **error);

/**
 * lrg_asset_pack_load_sound:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads a sound from the pack.
 *
 * Returns: (transfer full) (nullable): A new #GrlSound, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlSound * lrg_asset_pack_load_sound (LrgAssetPack  *self,
                                       const gchar   *name,
                                       GError       **error);

/**
 * lrg_asset_pack_load_wave:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads wave data from the pack.
 *
 * Returns: (transfer full) (nullable): A new #LrgWaveData, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_asset_pack_load_wave (LrgAssetPack  *self,
                                         const gchar   *name,
                                         GError       **error);

/**
 * lrg_asset_pack_load_music:
 * @self: a #LrgAssetPack
 * @name: the resource name
 * @error: (nullable): return location for a #GError
 *
 * Loads music from the pack.
 *
 * Note: Music is streamed from disk, so this loads the music
 * configuration but streaming happens from the original pack file.
 *
 * Returns: (transfer full) (nullable): A new #GrlMusic, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GrlMusic * lrg_asset_pack_load_music (LrgAssetPack  *self,
                                       const gchar   *name,
                                       GError       **error);

/**
 * lrg_asset_pack_load_object:
 * @self: a #LrgAssetPack
 * @name: the resource name (should be a YAML file)
 * @loader: the data loader to use
 * @error: (nullable): return location for a #GError
 *
 * Loads a GObject from a YAML resource in the pack.
 *
 * Returns: (transfer full) (nullable): A new #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_asset_pack_load_object (LrgAssetPack   *self,
                                       const gchar    *name,
                                       LrgDataLoader  *loader,
                                       GError        **error);

/* ==========================================================================
 * Access Underlying
 * ========================================================================== */

/**
 * lrg_asset_pack_get_resource_pack:
 * @self: a #LrgAssetPack
 *
 * Gets the underlying #GrlResourcePack.
 *
 * Returns: (transfer none): the underlying #GrlResourcePack
 */
LRG_AVAILABLE_IN_ALL
GrlResourcePack * lrg_asset_pack_get_resource_pack (LrgAssetPack *self);

G_END_DECLS
