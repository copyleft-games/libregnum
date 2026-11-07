/* lrg-asset-source.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAssetSource: a virtual tree of assets that are not files in the
 * project, and LrgAssetSourceEntry, one folder or asset of such a tree.
 *
 * A game module (or any host) implements the interface to expose assets
 * the editor cannot list from the disk: models and textures packed in
 * archives, generated content, a remote catalogue. The tree is made of
 * '/'-separated virtual paths ("" is the root, "World/Azeroth/tree.m2" an
 * entry); every entry says which #LrgAssetType its export produces. The
 * editor lists folders with lrg_asset_source_list(), finds entries with
 * lrg_asset_source_search() and asks the source to convert an entry into
 * real files with lrg_asset_source_export_entry(). Exports may be slow
 * (seconds for a large model), so the editor caches them
 * (lrg_asset_source_export_cached()) and only exports on explicit user
 * action.
 *
 * The editor application keeps a registry of sources
 * (lrg_editor_app_add_asset_source()) and shows them in its Sources
 * panel; see docs/editor/asset-sources.org.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_ASSET_SOURCE_CACHE_MARKER:
 *
 * The name of the file lrg_asset_source_export_cached() writes into an
 * entry's cache folder: one line, the exported main file relative to
 * that folder. Its presence means the export finished.
 *
 * Since: 0.8.2
 */
#define LRG_ASSET_SOURCE_CACHE_MARKER ".lrg-export"

/* ==========================================================================
 * LrgAssetSourceEntry
 * ========================================================================== */

#define LRG_TYPE_ASSET_SOURCE_ENTRY (lrg_asset_source_entry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetSourceEntry, lrg_asset_source_entry, LRG, ASSET_SOURCE_ENTRY, GObject)

/**
 * lrg_asset_source_entry_new:
 * @path: the entry's full virtual path, '/'-separated, without a leading
 *   or trailing '/' (e.g. "World/Azeroth/tree.m2")
 * @is_folder: %TRUE for a folder
 * @type: the #LrgAssetType of the file the entry exports to (e.g.
 *   %LRG_ASSET_TYPE_MODEL for a model that exports a .glb);
 *   %LRG_ASSET_TYPE_UNKNOWN for folders
 * @size: the size in bytes of the source data, or 0 when unknown
 *
 * Creates an entry. Its name is the last component of @path.
 *
 * Returns: (transfer full): a new #LrgAssetSourceEntry
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSourceEntry * lrg_asset_source_entry_new (const gchar  *path,
                                                  gboolean      is_folder,
                                                  LrgAssetType  type,
                                                  guint64       size);

/**
 * lrg_asset_source_entry_get_path:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: (transfer none): the full virtual path
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_source_entry_get_path (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_get_name:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: (transfer none): the last component of the path
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_source_entry_get_name (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_is_folder:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: %TRUE for a folder
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_source_entry_is_folder (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_get_asset_type:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: the type of the file the entry exports to
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetType lrg_asset_source_entry_get_asset_type (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_get_size:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: the size of the source data in bytes, 0 when unknown
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_asset_source_entry_get_size (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_get_description:
 * @self: an #LrgAssetSourceEntry
 *
 * Returns: (transfer none) (nullable): a one-line description shown in
 *   the Sources panel's preview, or %NULL
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_source_entry_get_description (LrgAssetSourceEntry *self);

/**
 * lrg_asset_source_entry_set_description:
 * @self: an #LrgAssetSourceEntry
 * @description: (nullable): a one-line description
 *
 * Sets the description (e.g. "M2 model, 3 textures").
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_source_entry_set_description (LrgAssetSourceEntry *self,
                                             const gchar         *description);

/* ==========================================================================
 * LrgAssetSource
 * ========================================================================== */

#define LRG_TYPE_ASSET_SOURCE (lrg_asset_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgAssetSource, lrg_asset_source, LRG, ASSET_SOURCE, GObject)

/**
 * LrgAssetSourceInterface:
 * @parent_iface: the parent interface
 * @get_id: returns a stable identifier made of letters, digits, '-', '_'
 *   and '.' (e.g. "librewow"); it names the cache folder and the
 *   project folder imports go to. Required.
 * @get_title: returns the display name; optional (defaults to the id)
 * @list: returns the entries of a virtual folder ("" is the root) as a
 *   #GPtrArray of #LrgAssetSourceEntry (transfer full, with a free
 *   function), or %NULL with @error set (e.g. %G_IO_ERROR_NOT_FOUND for
 *   a folder that does not exist). Required.
 * @search: returns up to @limit entries anywhere in the tree whose name
 *   matches @pattern; optional. Without it lrg_asset_source_search()
 *   walks the tree with @list, which is fine for small trees only.
 * @export_entry: converts the entry at a path into real files inside
 *   an existing folder (the main file plus any side files it needs, such
 *   as textures next to a .gltf) and returns the main file's absolute
 *   path in @out_file; may take seconds. Required.
 *
 * The virtual functions of an asset source. Every function runs on the
 * thread that calls the wrapper (the editor's UI thread).
 *
 * Since: 0.8.2
 */
struct _LrgAssetSourceInterface
{
	GTypeInterface parent_iface;

	const gchar * (*get_id)       (LrgAssetSource  *self);
	const gchar * (*get_title)    (LrgAssetSource  *self);
	GPtrArray *   (*list)         (LrgAssetSource  *self,
	                               const gchar     *folder,
	                               GError         **error);
	GPtrArray *   (*search)       (LrgAssetSource  *self,
	                               const gchar     *pattern,
	                               guint            limit,
	                               GError         **error);
	gboolean      (*export_entry) (LrgAssetSource  *self,
	                               const gchar     *path,
	                               const gchar     *dest_dir,
	                               gchar          **out_file,
	                               GError         **error);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_asset_source_get_id:
 * @self: an #LrgAssetSource
 *
 * Returns: (transfer none): the source's stable identifier
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_source_get_id (LrgAssetSource *self);

/**
 * lrg_asset_source_get_title:
 * @self: an #LrgAssetSource
 *
 * Returns: (transfer none): the display name (the id when the source has
 *   no title)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_asset_source_get_title (LrgAssetSource *self);

/**
 * lrg_asset_source_list:
 * @self: an #LrgAssetSource
 * @folder: (nullable): a virtual folder path, "" or %NULL for the root
 * @error: (nullable): return location for an error
 *
 * Lists one folder (not recursive). The path is normalised first
 * (leading, trailing and doubled '/' removed); "." and ".." components
 * are refused with %G_IO_ERROR_INVALID_FILENAME.
 *
 * Returns: (transfer full) (element-type LrgAssetSourceEntry) (nullable):
 *   the entries, or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_asset_source_list (LrgAssetSource  *self,
                                   const gchar     *folder,
                                   GError         **error);

/**
 * lrg_asset_source_implements_search:
 * @self: an #LrgAssetSource
 *
 * Returns: %TRUE when the source has its own (fast) search; the Sources
 *   panel then offers a search over the whole tree
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_source_implements_search (LrgAssetSource *self);

/**
 * lrg_asset_source_search:
 * @self: an #LrgAssetSource
 * @pattern: the text to look for (case-insensitive)
 * @limit: the maximum number of results, 0 for no limit
 * @error: (nullable): return location for an error
 *
 * Finds entries anywhere in the tree. A source without its own search
 * is walked folder by folder with lrg_asset_source_list(), matching
 * @pattern as a case-insensitive substring of each entry's name
 * (folders included).
 *
 * Returns: (transfer full) (element-type LrgAssetSourceEntry) (nullable):
 *   the matches, or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_asset_source_search (LrgAssetSource  *self,
                                     const gchar     *pattern,
                                     guint            limit,
                                     GError         **error);

/**
 * lrg_asset_source_find_entry:
 * @self: an #LrgAssetSource
 * @path: a virtual path
 * @error: (nullable): return location for an error
 *
 * Looks one entry up by listing its parent folder.
 *
 * Returns: (transfer full) (nullable): the entry, or %NULL with
 *   %G_IO_ERROR_NOT_FOUND
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
LrgAssetSourceEntry * lrg_asset_source_find_entry (LrgAssetSource  *self,
                                                   const gchar     *path,
                                                   GError         **error);

/**
 * lrg_asset_source_export_entry:
 * @self: an #LrgAssetSource
 * @path: the virtual path of an entry (not a folder)
 * @dest_dir: an existing folder to write the files into
 * @out_file: (out) (optional) (transfer full): the main file written
 * @error: (nullable): return location for an error
 *
 * Converts the entry into real files, uncached. This may take seconds;
 * the editor uses lrg_asset_source_export_cached() instead.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_source_export_entry (LrgAssetSource  *self,
                                        const gchar     *path,
                                        const gchar     *dest_dir,
                                        gchar          **out_file,
                                        GError         **error);

/**
 * lrg_asset_source_get_cache_dir:
 * @self: an #LrgAssetSource
 *
 * The default export cache of the source:
 * `$XDG_CACHE_HOME/libregnum/editor/sources/<id>`.
 *
 * Returns: (transfer full): the absolute folder path (not created)
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_source_get_cache_dir (LrgAssetSource *self);

/**
 * lrg_asset_source_get_cache_entry_dir:
 * @self: an #LrgAssetSource
 * @path: the virtual path of an entry
 * @cache_dir: (nullable): the cache root, %NULL for
 *   lrg_asset_source_get_cache_dir()
 *
 * The folder the cached export of @path lives in: the cache root plus
 * the virtual path, so every entry gets its own folder for its main and
 * side files.
 *
 * Returns: (transfer full) (nullable): the absolute folder path, or %NULL
 *   for an invalid path
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_source_get_cache_entry_dir (LrgAssetSource *self,
                                              const gchar    *path,
                                              const gchar    *cache_dir);

/**
 * lrg_asset_source_export_cached:
 * @self: an #LrgAssetSource
 * @path: the virtual path of an entry (not a folder)
 * @cache_dir: (nullable): the cache root, %NULL for
 *   lrg_asset_source_get_cache_dir()
 * @error: (nullable): return location for an error
 *
 * Exports @path once and reuses the result: the first call creates the
 * entry's cache folder (lrg_asset_source_get_cache_entry_dir()), calls
 * lrg_asset_source_export_entry() into it and records the main file in
 * a #LRG_ASSET_SOURCE_CACHE_MARKER file; later calls (also from later
 * runs of the editor) return the recorded file while it exists, without
 * exporting again. lrg_asset_source_invalidate_cache() forgets an
 * export.
 *
 * Returns: (transfer full) (nullable): the absolute path of the exported
 *   main file, or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_source_export_cached (LrgAssetSource  *self,
                                        const gchar     *path,
                                        const gchar     *cache_dir,
                                        GError         **error);

/**
 * lrg_asset_source_invalidate_cache:
 * @self: an #LrgAssetSource
 * @path: (nullable): the virtual path of an entry, or %NULL for every
 *   entry exported in this process
 * @cache_dir: (nullable): the cache root, %NULL for
 *   lrg_asset_source_get_cache_dir()
 *
 * Forgets cached exports so the next lrg_asset_source_export_cached()
 * exports again: removes the entry's marker file (the exported files
 * stay until the export overwrites them). Call it when the data behind
 * an entry changed.
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_source_invalidate_cache (LrgAssetSource *self,
                                        const gchar    *path,
                                        const gchar    *cache_dir);

/**
 * lrg_asset_source_normalize_path:
 * @path: (nullable): a virtual path
 * @error: (nullable): return location for an error
 *
 * Normalises a virtual path: drops leading, trailing and doubled '/',
 * refuses "." and ".." components and backslashes
 * (%G_IO_ERROR_INVALID_FILENAME). %NULL becomes "".
 *
 * Returns: (transfer full) (nullable): the normalised path ("" for the
 *   root), or %NULL on error
 *
 * Since: 0.8.2
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_asset_source_normalize_path (const gchar  *path,
                                         GError      **error);

G_END_DECLS
