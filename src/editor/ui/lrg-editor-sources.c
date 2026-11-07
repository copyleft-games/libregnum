/* lrg-editor-sources.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Asset sources in the editor application:
 *
 *  - the registry (lrg_editor_app_add_asset_source() and friends): an
 *    ordered list of LrgAssetSource with unique ids; a serial number
 *    tells the Sources panel to rebuild its source chooser;
 *  - exports through the source's cache, optionally copied to a folder
 *    ("Export to..."), and imports into <project>/assets/<id>/<folder>/;
 *  - placing an imported entry in the level (editor_app_place_asset(),
 *    shared with the Assets panel);
 *  - the game module editor hook (lrg_editor_app_init_module());
 *  - the "source.*" IPC methods (source.list, source.ls, source.search,
 *    source.export, source.import, source.place).
 *
 * Exports run synchronously on the calling (UI) thread; they only happen
 * on an explicit request (selecting an entry in the panel, an IPC call)
 * and are cached, so repeating one is instant.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include <json-glib/json-glib.h>
#include <errno.h>
#include <string.h>

/* Default and upper bound of source.search results */
#define SEARCH_DEFAULT_LIMIT (200)
#define SEARCH_MAX_LIMIT     (5000)

/* ==========================================================================
 * Registry
 * ========================================================================== */

/* An info line in the editor log (the Console panel) */
static void log_info (LrgEditorApp *self, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

static void
log_info (LrgEditorApp *self,
          const gchar  *format,
          ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	if (self->log == NULL)
		return;
	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	lrg_editor_log_append (self->log, G_LOG_LEVEL_INFO, "sources", text);
}

/* Index of the source registered as @id, or -1. */
static gint
source_index (LrgEditorApp *self,
              const gchar  *id)
{
	guint i;

	for (i = 0; id != NULL && i < self->sources->len; i++)
		if (g_strcmp0 (lrg_asset_source_get_id (g_ptr_array_index (self->sources, i)), id) == 0)
			return (gint) i;
	return -1;
}

void
lrg_editor_app_add_asset_source (LrgEditorApp   *self,
                                 LrgAssetSource *source)
{
	const gchar *id;
	gint         at;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));
	g_return_if_fail (LRG_IS_ASSET_SOURCE (source));

	id = lrg_asset_source_get_id (source);
	g_return_if_fail (id != NULL && *id != '\0');

	/* Same id: replace in place, keeping the position */
	at = source_index (self, id);
	if (at >= 0)
	{
		g_object_unref (g_ptr_array_index (self->sources, (guint) at));
		g_ptr_array_index (self->sources, (guint) at) = g_object_ref (source);
	}
	else
		g_ptr_array_add (self->sources, g_object_ref (source));
	self->sources_serial++;
	log_info (self, "Asset source '%s' (%s) registered", id,
	                        lrg_asset_source_get_title (source));
}

gboolean
lrg_editor_app_remove_asset_source (LrgEditorApp *self,
                                    const gchar  *id)
{
	gint at;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	at = source_index (self, id);
	if (at < 0)
		return FALSE;
	g_ptr_array_remove_index (self->sources, (guint) at);
	self->sources_serial++;
	return TRUE;
}

GPtrArray *
lrg_editor_app_get_asset_sources (LrgEditorApp *self)
{
	GPtrArray *copy;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);

	copy = g_ptr_array_new_full (self->sources->len, NULL);
	for (i = 0; i < self->sources->len; i++)
		g_ptr_array_add (copy, g_ptr_array_index (self->sources, i));
	return copy;
}

LrgAssetSource *
lrg_editor_app_lookup_asset_source (LrgEditorApp *self,
                                    const gchar  *id)
{
	gint at;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);

	at = source_index (self, id);
	return (at >= 0) ? g_ptr_array_index (self->sources, (guint) at) : NULL;
}

/* The source registered as @id, or NULL with G_IO_ERROR_NOT_FOUND. */
static LrgAssetSource *
require_source (LrgEditorApp  *self,
                const gchar   *id,
                GError       **error)
{
	LrgAssetSource *source = lrg_editor_app_lookup_asset_source (self, id);

	if (source == NULL)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset source '%s'", id != NULL ? id : "(null)");
	return source;
}

/* ==========================================================================
 * Exports and imports
 * ========================================================================== */

/*
 * copy_tree:
 * @src: a folder
 * @dest: the folder to copy into (created)
 *
 * Copies the files below @src into @dest, keeping the relative layout and
 * overwriting files of the same name. The cache marker is not copied.
 * @copied collects the absolute destination paths (for thumbnail
 * invalidation).
 */
static gboolean
copy_tree (const gchar  *src,
           const gchar  *dest,
           GPtrArray    *copied,
           GError      **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar    *name;

	if (g_mkdir_with_parents (dest, 0755) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved), "Cannot create %s: %s", dest,
		             g_strerror (saved));
		return FALSE;
	}
	dir = g_dir_open (src, 0, error);
	if (dir == NULL)
		return FALSE;

	while ((name = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *from = g_build_filename (src, name, NULL);
		g_autofree gchar *to = g_build_filename (dest, name, NULL);

		if (g_strcmp0 (name, LRG_ASSET_SOURCE_CACHE_MARKER) == 0)
			continue;
		if (g_file_test (from, G_FILE_TEST_IS_DIR))
		{
			if (!copy_tree (from, to, copied, error))
				return FALSE;
		}
		else
		{
			g_autoptr(GFile) f = g_file_new_for_path (from);
			g_autoptr(GFile) t = g_file_new_for_path (to);

			if (!g_file_copy (f, t, G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS, NULL, NULL, NULL,
			                  error))
				return FALSE;
			if (copied != NULL)
				g_ptr_array_add (copied, g_steal_pointer (&to));
		}
	}
	return TRUE;
}

/*
 * export_to:
 * @cached: the main file of a cached export (inside @entry_dir)
 *
 * Copies the cached export of one entry into @dest and maps the main
 * file to its copy. Returns the copy's path.
 */
static gchar *
export_to (const gchar  *entry_dir,
           const gchar  *cached,
           const gchar  *dest,
           GPtrArray    *copied,
           GError      **error)
{
	gsize len = strlen (entry_dir);

	if (!copy_tree (entry_dir, dest, copied, error))
		return NULL;
	if (g_str_has_prefix (cached, entry_dir) && cached[len] == G_DIR_SEPARATOR)
		return g_build_filename (dest, cached + len + 1, NULL);

	/* A main file outside the entry folder (absolute marker): copy it too */
	{
		g_autofree gchar *base = g_path_get_basename (cached);
		gchar            *to = g_build_filename (dest, base, NULL);
		g_autoptr(GFile)  f = g_file_new_for_path (cached);
		g_autoptr(GFile)  t = g_file_new_for_path (to);

		if (!g_file_copy (f, t, G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS, NULL, NULL, NULL, error))
		{
			g_free (to);
			return NULL;
		}
		if (copied != NULL)
			g_ptr_array_add (copied, g_strdup (to));
		return to;
	}
}

gchar *
lrg_editor_app_export_source_entry (LrgEditorApp  *self,
                                    const gchar   *source_id,
                                    const gchar   *path,
                                    const gchar   *dest_dir,
                                    GError       **error)
{
	LrgAssetSource   *source;
	g_autofree gchar *cached = NULL;
	g_autofree gchar *entry_dir = NULL;
	g_autofree gchar *dest = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	source = require_source (self, source_id, error);
	if (source == NULL)
		return NULL;
	cached = lrg_asset_source_export_cached (source, path, NULL, error);
	if (cached == NULL)
		return NULL;
	if (dest_dir == NULL)
		return g_steal_pointer (&cached);

	/* "Export to...": copy the whole export (side files too) */
	entry_dir = lrg_asset_source_get_cache_entry_dir (source, path, NULL);
	dest = g_canonicalize_filename (dest_dir, NULL);
	return export_to (entry_dir, cached, dest, NULL, error);
}

gchar *
lrg_editor_app_import_source_entry (LrgEditorApp  *self,
                                    const gchar   *source_id,
                                    const gchar   *path,
                                    GError       **error)
{
	LrgAssetSource      *source;
	g_autofree gchar    *norm = NULL;
	g_autofree gchar    *cached = NULL;
	g_autofree gchar    *entry_dir = NULL;
	g_autofree gchar    *cache_root = NULL;
	g_autofree gchar    *id_dir = NULL;
	g_autofree gchar    *folder = NULL;
	g_autofree gchar    *dest = NULL;
	g_autofree gchar    *file = NULL;
	g_autoptr(GPtrArray) copied = g_ptr_array_new_with_free_func (g_free);
	g_auto(GStrv)        parts = NULL;
	gchar               *slash;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (self->project_dir == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		                     "Open a project first: imports go to its assets folder");
		return NULL;
	}
	source = require_source (self, source_id, error);
	if (source == NULL)
		return NULL;
	norm = lrg_asset_source_normalize_path (path, error);
	if (norm == NULL)
		return NULL;
	cached = lrg_asset_source_export_cached (source, norm, NULL, error);
	if (cached == NULL)
		return NULL;

	/* <project>/assets/<id>/<virtual folder of the entry>/ ; the id folder
	 * is named like the cache folder (the id made filesystem-safe) */
	cache_root = lrg_asset_source_get_cache_dir (source);
	id_dir = g_path_get_basename (cache_root);
	folder = g_strdup (norm);
	slash = strrchr (folder, '/');
	if (slash != NULL)
		*slash = '\0';
	else
		*folder = '\0';
	dest = g_build_filename (self->project_dir, "assets", id_dir, NULL);
	parts = g_strsplit (folder, "/", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *next;

		if (parts[i][0] == '\0')
			continue;
		next = g_build_filename (dest, parts[i], NULL);
		g_free (dest);
		dest = next;
	}

	entry_dir = lrg_asset_source_get_cache_entry_dir (source, norm, NULL);
	file = export_to (entry_dir, cached, dest, copied, error);
	if (file == NULL)
		return NULL;

	/* Re-imports overwrite: drop stale thumbnails and renders */
	for (i = 0; i < copied->len; i++)
		editor_thumbnails_file_changed (self, g_ptr_array_index (copied, i));
	log_info (self, "Imported %s:%s to %s", source_id, norm, file);
	return editor_app_relative_path (self, file);
}

LrgNode *
lrg_editor_app_place_source_entry (LrgEditorApp  *self,
                                   const gchar   *source_id,
                                   const gchar   *path,
                                   GError       **error)
{
	g_autofree gchar *rel = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	rel = lrg_editor_app_import_source_entry (self, source_id, path, error);
	if (rel == NULL)
		return NULL;
	return editor_app_place_asset (self, rel, error);
}

/* ==========================================================================
 * Game module editor hook
 * ========================================================================== */

gboolean
lrg_editor_app_init_module (LrgEditorApp   *self,
                            LrgLoadedGame  *game,
                            GError        **error)
{
	gpointer                symbol;
	LrgGameModuleEditorInit init;
	const LrgGameModuleInfo *info;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), FALSE);
	g_return_val_if_fail (LRG_IS_LOADED_GAME (game), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* No hook: the module needs nothing from the editor */
	symbol = lrg_loaded_game_lookup_symbol (game, LRG_GAME_MODULE_EDITOR_SYMBOL);
	if (symbol == NULL)
		return TRUE;

	info = lrg_loaded_game_get_info (game);
	init = (LrgGameModuleEditorInit) symbol;
	if (!init (self, error))
	{
		if (error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The editor hook of game module '%s' failed",
			             info != NULL && info->game_id != NULL ? info->game_id : "?");
		return FALSE;
	}
	log_info (self, "Game module '%s' set up the editor",
	                        info != NULL && info->game_id != NULL ? info->game_id : "?");
	return TRUE;
}

/* ==========================================================================
 * IPC: source.*
 * ========================================================================== */

/* A display name of an entry's type: "Folder", "Model", "Texture"... */
const gchar *
editor_entry_type_name (LrgAssetType type,
                        gboolean     is_folder)
{
	static const gchar * const names[] = {
		"File", "Texture", "Model", "Audio", "Font", "Script", "Level", "Prefab", "Tileset", "Scene", "CAD"
	};

	if (is_folder)
		return "Folder";
	return ((guint) type < G_N_ELEMENTS (names)) ? names[type] : "File";
}

/* The enum nick of an asset type ("model"), "unknown" when out of range. */
static const gchar *
type_nick (LrgAssetType type)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_ASSET_TYPE);
	GEnumValue           *value = g_enum_get_value (klass, (gint) type);

	/* Nicks are static strings owned by the (static) enum type */
	return value != NULL ? value->value_nick : "unknown";
}

static JsonObject *
entry_object (LrgAssetSourceEntry *e)
{
	JsonObject  *o = json_object_new ();
	const gchar *desc = lrg_asset_source_entry_get_description (e);

	json_object_set_string_member (o, "path", lrg_asset_source_entry_get_path (e));
	json_object_set_string_member (o, "name", lrg_asset_source_entry_get_name (e));
	json_object_set_boolean_member (o, "folder", lrg_asset_source_entry_is_folder (e));
	json_object_set_string_member (o, "type", lrg_asset_source_entry_is_folder (e)
	                               ? "folder" : type_nick (lrg_asset_source_entry_get_asset_type (e)));
	json_object_set_int_member (o, "size", (gint64) lrg_asset_source_entry_get_size (e));
	if (desc != NULL)
		json_object_set_string_member (o, "description", desc);
	return o;
}

static JsonArray *
entries_array (GPtrArray *entries)
{
	JsonArray *a = json_array_new ();
	guint      i;

	for (i = 0; i < entries->len; i++)
		json_array_add_object_element (a, entry_object (g_ptr_array_index (entries, i)));
	return a;
}

static JsonNode *
object_node (JsonObject *o)
{
	JsonNode *node = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (node, o);
	return node;
}

static const gchar *
param_str (JsonObject  *params,
           const gchar *name)
{
	JsonNode *n = (params != NULL && json_object_has_member (params, name))
	              ? json_object_get_member (params, name) : NULL;

	return (n != NULL && JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

/* A required string parameter, or NULL with INVALID_PARAMS. */
static const gchar *
require_str (JsonObject   *params,
             const gchar  *name,
             GError      **error)
{
	const gchar *v = param_str (params, name);

	if (v == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", name);
	return v;
}

/* Maps an error of the source / registry layer to the IPC error codes. */
static void
set_ipc_error (GError      **error,
               GError       *cause,
               const gchar  *what)
{
	gint code = LRG_EDITOR_SERVER_ERROR_FAILED;

	if (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
		code = LRG_EDITOR_SERVER_ERROR_NOT_FOUND;
	else if (g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME) ||
	         g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
	         g_error_matches (cause, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY))
		code = LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS;
	g_set_error (error, LRG_EDITOR_SERVER_ERROR, code, "%s: %s", what, cause->message);
}

/* The source named by params.source, or NULL with an IPC error. */
static LrgAssetSource *
method_source (LrgEditorApp  *self,
               JsonObject    *params,
               GError       **error)
{
	const gchar    *id = require_str (params, "source", error);
	LrgAssetSource *source;

	if (id == NULL)
		return NULL;
	source = lrg_editor_app_lookup_asset_source (self, id);
	if (source == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No asset source '%s'", id);
	return source;
}

/* source.list: [{id, title, search}] */
static JsonNode *
m_source_list (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);
	JsonArray    *a = json_array_new ();
	JsonNode     *node = json_node_new (JSON_NODE_ARRAY);
	guint         i;

	for (i = 0; i < self->sources->len; i++)
	{
		LrgAssetSource *s = g_ptr_array_index (self->sources, i);
		JsonObject     *o = json_object_new ();

		json_object_set_string_member (o, "id", lrg_asset_source_get_id (s));
		json_object_set_string_member (o, "title", lrg_asset_source_get_title (s));
		json_object_set_boolean_member (o, "search", lrg_asset_source_implements_search (s));
		json_array_add_object_element (a, o);
	}
	json_node_take_array (node, a);
	return node;
}

/* source.ls {source, folder?}: {source, folder, entries} */
static JsonNode *
m_source_ls (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgAssetSource      *source = method_source (LRG_EDITOR_APP (user_data), params, error);
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GError)    cause = NULL;
	g_autofree gchar    *folder = NULL;
	JsonObject          *o;

	if (source == NULL)
		return NULL;
	folder = lrg_asset_source_normalize_path (param_str (params, "folder"), &cause);
	if (folder != NULL)
		entries = lrg_asset_source_list (source, folder, &cause);
	if (entries == NULL)
	{
		set_ipc_error (error, cause, "List failed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "source", lrg_asset_source_get_id (source));
	json_object_set_string_member (o, "folder", folder);
	json_object_set_array_member (o, "entries", entries_array (entries));
	return object_node (o);
}

/* source.search {source, pattern, limit?}: {source, pattern, entries} */
static JsonNode *
m_source_search (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgAssetSource      *source = method_source (LRG_EDITOR_APP (user_data), params, error);
	const gchar         *pattern;
	g_autoptr(GPtrArray) entries = NULL;
	g_autoptr(GError)    cause = NULL;
	gint64               limit = SEARCH_DEFAULT_LIMIT;
	JsonObject          *o;

	if (source == NULL || (pattern = require_str (params, "pattern", error)) == NULL)
		return NULL;
	if (json_object_has_member (params, "limit"))
		limit = json_object_get_int_member (params, "limit");
	limit = CLAMP (limit, 1, SEARCH_MAX_LIMIT);
	entries = lrg_asset_source_search (source, pattern, (guint) limit, &cause);
	if (entries == NULL)
	{
		set_ipc_error (error, cause, "Search failed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "source", lrg_asset_source_get_id (source));
	json_object_set_string_member (o, "pattern", pattern);
	json_object_set_array_member (o, "entries", entries_array (entries));
	return object_node (o);
}

/* source.export {source, path, dest?}: {source, path, file} */
static JsonNode *
m_source_export (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorApp      *self = LRG_EDITOR_APP (user_data);
	LrgAssetSource    *source = method_source (self, params, error);
	const gchar       *path;
	g_autofree gchar  *file = NULL;
	g_autoptr(GError)  cause = NULL;
	JsonObject        *o;

	if (source == NULL || (path = require_str (params, "path", error)) == NULL)
		return NULL;
	file = lrg_editor_app_export_source_entry (self, lrg_asset_source_get_id (source), path,
	                                           param_str (params, "dest"), &cause);
	if (file == NULL)
	{
		set_ipc_error (error, cause, "Export failed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "source", lrg_asset_source_get_id (source));
	json_object_set_string_member (o, "path", path);
	json_object_set_string_member (o, "file", file);
	return object_node (o);
}

/* source.import {source, path}: {source, path, asset, file} */
static JsonNode *
m_source_import (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorApp      *self = LRG_EDITOR_APP (user_data);
	LrgAssetSource    *source = method_source (self, params, error);
	const gchar       *path;
	g_autofree gchar  *asset = NULL;
	g_autofree gchar  *file = NULL;
	g_autoptr(GError)  cause = NULL;
	JsonObject        *o;

	if (source == NULL || (path = require_str (params, "path", error)) == NULL)
		return NULL;
	asset = lrg_editor_app_import_source_entry (self, lrg_asset_source_get_id (source), path, &cause);
	if (asset == NULL)
	{
		set_ipc_error (error, cause, "Import failed");
		return NULL;
	}
	file = editor_app_project_path (self, asset);
	o = json_object_new ();
	json_object_set_string_member (o, "source", lrg_asset_source_get_id (source));
	json_object_set_string_member (o, "path", path);
	json_object_set_string_member (o, "asset", asset);
	json_object_set_string_member (o, "file", file);
	return object_node (o);
}

/* source.place {source, path}: {asset, node, name} */
static JsonNode *
m_source_place (LrgEditorServer *server, JsonObject *params, gpointer user_data, GError **error)
{
	LrgEditorApp      *self = LRG_EDITOR_APP (user_data);
	LrgAssetSource    *source = method_source (self, params, error);
	const gchar       *path;
	g_autofree gchar  *asset = NULL;
	g_autoptr(GError)  cause = NULL;
	LrgNode           *node = NULL;
	LrgNodeVisual     *visual;
	JsonObject        *o;

	if (source == NULL || (path = require_str (params, "path", error)) == NULL)
		return NULL;
	asset = lrg_editor_app_import_source_entry (self, lrg_asset_source_get_id (source), path, &cause);
	if (asset != NULL)
		node = editor_app_place_asset (self, asset, &cause);
	if (node == NULL)
	{
		set_ipc_error (error, cause, asset == NULL ? "Import failed" : "Place failed");
		return NULL;
	}
	visual = lrg_node_get_visual (node);
	o = json_object_new ();
	json_object_set_string_member (o, "asset", (visual != NULL && lrg_node_visual_get_asset (visual) != NULL)
	                               ? lrg_node_visual_get_asset (visual) : asset);
	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	json_object_set_string_member (o, "name", lrg_node_get_name (node));
	return object_node (o);
}

void
editor_sources_init (LrgEditorApp *self)
{
	LrgEditorServer *server = self->server;

	if (server == NULL)
		return;
	lrg_editor_server_register_method (server, "source.list", m_source_list, self, NULL);
	lrg_editor_server_register_method (server, "source.ls", m_source_ls, self, NULL);
	lrg_editor_server_register_method (server, "source.search", m_source_search, self, NULL);
	lrg_editor_server_register_method (server, "source.export", m_source_export, self, NULL);
	lrg_editor_server_register_method (server, "source.import", m_source_import, self, NULL);
	lrg_editor_server_register_method (server, "source.place", m_source_place, self, NULL);
}

void
editor_sources_dispose (LrgEditorApp *self)
{
	/* Drop the sources early: they may hold module resources (archives) */
	if (self->sources != NULL && self->sources->len > 0)
	{
		g_ptr_array_set_size (self->sources, 0);
		self->sources_serial++;
	}
}
