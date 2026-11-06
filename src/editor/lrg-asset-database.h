/* lrg-asset-database.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Discovery and classification of project assets.
 *
 * LrgAssetDatabase scans one or more asset directories, classifies each file by
 * extension into an #LrgAssetType, and exposes the results as a queryable list
 * of #LrgAssetEntry records (path relative to the search root, display name,
 * type, and a stable guid). It backs the editor's asset browser.
 *
 * Since 0.8.1 every asset may carry a "<file>.meta" sidecar (#LrgAssetMeta)
 * holding its guid and importer settings. A scan loads existing sidecars;
 * an asset without one gets an in-memory sidecar whose guid is the legacy
 * sha256 of its relative path, so references made before sidecars existed
 * keep resolving. With #LrgAssetDatabase:write-meta set (the editor sets
 * it; read-only tools leave it off) those sidecars are written to disk at
 * the end of the scan, but only inside the search directories and never
 * inside hidden (dot) directories or for hidden files. Sidecars are not
 * listed as assets; sidecars whose asset is gone are reported by
 * lrg_asset_database_get_orphaned_metas().
 *
 * The rename/move/duplicate/trash helpers carry the sidecar along with the
 * asset (a duplicate gets a fresh guid) and rescan afterwards.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-asset-meta.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgAssetEntry
 * ========================================================================== */

#define LRG_TYPE_ASSET_ENTRY (lrg_asset_entry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetEntry, lrg_asset_entry, LRG, ASSET_ENTRY, GObject)

/**
 * lrg_asset_entry_new:
 * @path: path relative to the asset search root
 * @name: display name (typically the basename)
 * @type: the classified asset type
 * @guid: a stable identifier
 *
 * Returns: (transfer full): a new #LrgAssetEntry
 */
LRG_AVAILABLE_IN_ALL
LrgAssetEntry * lrg_asset_entry_new (const gchar  *path,
                                     const gchar  *name,
                                     LrgAssetType  type,
                                     const gchar  *guid);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_entry_get_path (LrgAssetEntry *self);
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_entry_get_name (LrgAssetEntry *self);
LRG_AVAILABLE_IN_ALL
LrgAssetType lrg_asset_entry_get_asset_type (LrgAssetEntry *self);
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_entry_get_guid (LrgAssetEntry *self);

/**
 * lrg_asset_entry_get_full_path:
 * @self: an #LrgAssetEntry
 *
 * Returns: (transfer none) (nullable): the asset path joined to its search
 *   directory, or %NULL for an entry made with lrg_asset_entry_new()
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_entry_get_full_path (LrgAssetEntry *self);

/**
 * lrg_asset_entry_get_search_dir:
 * @self: an #LrgAssetEntry
 *
 * Returns: (transfer none) (nullable): the search directory the entry was
 *   found in, or %NULL for an entry made with lrg_asset_entry_new()
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_entry_get_search_dir (LrgAssetEntry *self);

/**
 * lrg_asset_entry_get_meta:
 * @self: an #LrgAssetEntry
 *
 * Returns the entry's sidecar: loaded from "<file>.meta", or created in
 * memory (legacy guid) when the asset has none yet.
 *
 * Returns: (transfer none) (nullable): the sidecar, or %NULL for an entry
 *   made with lrg_asset_entry_new()
 */
LRG_AVAILABLE_IN_ALL
LrgAssetMeta * lrg_asset_entry_get_meta (LrgAssetEntry *self);

/**
 * lrg_asset_entry_has_meta_file:
 * @self: an #LrgAssetEntry
 *
 * Returns: whether the sidecar exists on disk (as of the last scan or
 *   write)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_entry_has_meta_file (LrgAssetEntry *self);

/* ==========================================================================
 * LrgAssetDatabase
 * ========================================================================== */

#define LRG_TYPE_ASSET_DATABASE (lrg_asset_database_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetDatabase, lrg_asset_database, LRG, ASSET_DATABASE, GObject)

/**
 * lrg_asset_database_new:
 *
 * Creates an empty database with #LrgAssetDatabase:write-meta off.
 *
 * Returns: (transfer full): a new, empty #LrgAssetDatabase
 */
LRG_AVAILABLE_IN_ALL
LrgAssetDatabase * lrg_asset_database_new (void);

/**
 * lrg_asset_database_classify:
 * @path: a file path or name
 *
 * Classifies @path by its extension.
 *
 * Returns: the #LrgAssetType for @path
 */
LRG_AVAILABLE_IN_ALL
LrgAssetType lrg_asset_database_classify (const gchar *path);

/**
 * lrg_asset_database_add_search_dir:
 * @self: an #LrgAssetDatabase
 * @dir: a directory to scan on the next lrg_asset_database_scan()
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_database_add_search_dir (LrgAssetDatabase *self,
                                        const gchar      *dir);

/**
 * lrg_asset_database_scan:
 * @self: an #LrgAssetDatabase
 * @error: (nullable): return location for an error
 *
 * Clears any prior entries and recursively scans the search directories,
 * classifying every regular file. VCS metadata directories (".git", ".hg",
 * ".svn") are skipped and ".meta" sidecars are not listed. Each entry's
 * sidecar is loaded or created in memory; with
 * #LrgAssetDatabase:write-meta set, new or changed sidecars are written
 * (a failed write is logged, not fatal). Entries are sorted by path within
 * each search directory.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_scan (LrgAssetDatabase  *self,
                                  GError           **error);

/**
 * lrg_asset_database_get_count:
 * @self: an #LrgAssetDatabase
 *
 * Returns: the number of discovered entries
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_database_get_count (LrgAssetDatabase *self);

/**
 * lrg_asset_database_get_entry:
 * @self: an #LrgAssetDatabase
 * @index: entry index
 *
 * Returns: (transfer none) (nullable): the entry at @index, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgAssetEntry * lrg_asset_database_get_entry (LrgAssetDatabase *self,
                                              guint             index);

/**
 * lrg_asset_database_get_entries:
 * @self: an #LrgAssetDatabase
 *
 * Returns: (transfer none) (element-type LrgAssetEntry): all entries
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_asset_database_get_entries (LrgAssetDatabase *self);

/**
 * lrg_asset_database_count_of_type:
 * @self: an #LrgAssetDatabase
 * @type: an asset type
 *
 * Returns: the number of entries of @type
 */
LRG_AVAILABLE_IN_ALL
guint lrg_asset_database_count_of_type (LrgAssetDatabase *self,
                                        LrgAssetType      type);

/**
 * lrg_asset_database_get_write_meta:
 * @self: an #LrgAssetDatabase
 *
 * Returns: whether scans write missing sidecars to disk
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_get_write_meta (LrgAssetDatabase *self);

/**
 * lrg_asset_database_set_write_meta:
 * @self: an #LrgAssetDatabase
 * @write_meta: whether scans write missing sidecars to disk
 *
 * Turns sidecar writing on (editor) or off (read-only tools, the
 * default). Affects the next scan.
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_database_set_write_meta (LrgAssetDatabase *self,
                                        gboolean          write_meta);

/**
 * lrg_asset_database_lookup_guid:
 * @self: an #LrgAssetDatabase
 * @guid: an asset guid
 *
 * Returns: (transfer none) (nullable): the entry with @guid, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgAssetEntry * lrg_asset_database_lookup_guid (LrgAssetDatabase *self,
                                                const gchar      *guid);

/**
 * lrg_asset_database_lookup_path:
 * @self: an #LrgAssetDatabase
 * @path: an absolute path, or a path relative to a search directory
 *
 * Returns: (transfer none) (nullable): the entry for @path, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgAssetEntry * lrg_asset_database_lookup_path (LrgAssetDatabase *self,
                                                const gchar      *path);

/**
 * lrg_asset_database_get_orphaned_metas:
 * @self: an #LrgAssetDatabase
 *
 * Lists sidecars found by the last scan whose asset no longer exists
 * (e.g. the asset was deleted or renamed outside the editor).
 *
 * Returns: (transfer none) (element-type filename): absolute paths of the
 *   orphaned ".meta" files
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_asset_database_get_orphaned_metas (LrgAssetDatabase *self);

/**
 * lrg_asset_database_is_writable_path:
 * @self: an #LrgAssetDatabase
 * @path: a file path
 *
 * Checks the sidecar write guard: @path must lie inside a search
 * directory (symlinks resolved), and neither the file nor any directory
 * between it and the search directory may be hidden (start with ".").
 *
 * Returns: whether the database may write @path
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_is_writable_path (LrgAssetDatabase *self,
                                              const gchar      *path);

/**
 * lrg_asset_database_write_pending_metas:
 * @self: an #LrgAssetDatabase
 * @error: return location for a #GError
 *
 * Writes every sidecar that is missing on disk or modified in memory,
 * whatever #LrgAssetDatabase:write-meta says, skipping paths that fail
 * lrg_asset_database_is_writable_path(). Stops at the first failure.
 *
 * Returns: the number of sidecars written, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_asset_database_write_pending_metas (LrgAssetDatabase  *self,
                                             GError           **error);

/**
 * lrg_asset_database_save_meta:
 * @self: an #LrgAssetDatabase
 * @entry: an entry of @self
 * @error: return location for a #GError
 *
 * Writes @entry's sidecar now (e.g. after asset.meta.set). Fails with
 * %G_IO_ERROR_PERMISSION_DENIED outside the write guard.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_save_meta (LrgAssetDatabase  *self,
                                       LrgAssetEntry     *entry,
                                       GError           **error);

/**
 * lrg_asset_database_rename:
 * @self: an #LrgAssetDatabase
 * @path: the asset or folder (absolute or relative to a search directory)
 * @new_name: the new base name (no directory separators)
 * @error: return location for a #GError
 *
 * Renames an asset and its sidecar (or a folder with everything in it)
 * and rescans. The sidecar is written first if it only existed in memory,
 * so the guid survives the new path. Fails with %G_IO_ERROR_EXISTS when
 * the target exists.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_rename (LrgAssetDatabase  *self,
                                    const gchar       *path,
                                    const gchar       *new_name,
                                    GError           **error);

/**
 * lrg_asset_database_move:
 * @self: an #LrgAssetDatabase
 * @path: the asset or folder (absolute or relative to a search directory)
 * @dest_dir: an existing directory inside a search directory (absolute or
 *   relative to a search directory)
 * @error: return location for a #GError
 *
 * Moves an asset and its sidecar (or a folder) into @dest_dir, keeping
 * the guid, and rescans.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_move (LrgAssetDatabase  *self,
                                  const gchar       *path,
                                  const gchar       *dest_dir,
                                  GError           **error);

/**
 * lrg_asset_database_duplicate:
 * @self: an #LrgAssetDatabase
 * @path: an asset file (absolute or relative to a search directory)
 * @error: return location for a #GError
 *
 * Copies an asset next to itself as "name copy.ext" ("name copy 2.ext",
 * ...), writes a copy of its sidecar with a fresh guid (settings kept)
 * and rescans. Folders are not supported.
 *
 * Returns: (transfer none) (nullable): the new entry, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgAssetEntry * lrg_asset_database_duplicate (LrgAssetDatabase  *self,
                                              const gchar       *path,
                                              GError           **error);

/**
 * lrg_asset_database_trash:
 * @self: an #LrgAssetDatabase
 * @path: the asset or folder (absolute or relative to a search directory)
 * @error: return location for a #GError
 *
 * Moves an asset and its sidecar (or a folder) to the desktop trash
 * (g_file_trash()) and rescans.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_database_trash (LrgAssetDatabase  *self,
                                   const gchar       *path,
                                   GError           **error);

G_END_DECLS
