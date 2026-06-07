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
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

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

/* ==========================================================================
 * LrgAssetDatabase
 * ========================================================================== */

#define LRG_TYPE_ASSET_DATABASE (lrg_asset_database_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetDatabase, lrg_asset_database, LRG, ASSET_DATABASE, GObject)

/**
 * lrg_asset_database_new:
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
 * classifying every regular file.
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

G_END_DECLS
