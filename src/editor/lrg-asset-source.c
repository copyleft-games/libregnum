/* lrg-asset-source.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAssetSource (a virtual asset tree) and LrgAssetSourceEntry (one
 * folder or asset of it).
 *
 * The interface wrappers normalise virtual paths before they reach an
 * implementation, so a source only ever sees "" or "a/b/c". On top of
 * the three required virtual functions this file provides:
 *
 *  - a fallback search that walks the tree with list() (breadth first,
 *    case-insensitive substring match on names, bounded depth);
 *  - the export cache: one folder per entry under the source's cache
 *    root, a marker file naming the main file once an export finished,
 *    and an in-process memo (object data on the source) so repeated
 *    lookups do not even touch the disk.
 */

#include "config.h"

#include "lrg-asset-source.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

/* The fallback search does not descend deeper than this (cycles in a
 * badly written source must not hang the editor). */
#define SEARCH_MAX_DEPTH (64)

/* Object data key of the per-source export memo (GHashTable). */
#define MEMO_KEY "lrg-asset-source-export-memo"

/* ==========================================================================
 * LrgAssetSourceEntry
 * ========================================================================== */

struct _LrgAssetSourceEntry
{
	GObject       parent_instance;

	gchar        *path;
	gchar        *name;         /* points into path */
	gboolean      is_folder;
	LrgAssetType  type;
	guint64       size;
	gchar        *description;
};

G_DEFINE_FINAL_TYPE (LrgAssetSourceEntry, lrg_asset_source_entry, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_PATH,
	PROP_NAME,
	PROP_IS_FOLDER,
	PROP_ASSET_TYPE,
	PROP_SIZE,
	PROP_DESCRIPTION,
	N_PROPS
};

static GParamSpec *entry_props[N_PROPS];

static void
lrg_asset_source_entry_finalize (GObject *object)
{
	LrgAssetSourceEntry *self = LRG_ASSET_SOURCE_ENTRY (object);

	g_free (self->path);
	g_free (self->description);
	G_OBJECT_CLASS (lrg_asset_source_entry_parent_class)->finalize (object);
}

static void
lrg_asset_source_entry_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgAssetSourceEntry *self = LRG_ASSET_SOURCE_ENTRY (object);

	switch (prop_id)
	{
	case PROP_PATH:
		g_value_set_string (value, self->path);
		break;
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_IS_FOLDER:
		g_value_set_boolean (value, self->is_folder);
		break;
	case PROP_ASSET_TYPE:
		g_value_set_enum (value, (gint) self->type);
		break;
	case PROP_SIZE:
		g_value_set_uint64 (value, self->size);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, self->description);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_asset_source_entry_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgAssetSourceEntry *self = LRG_ASSET_SOURCE_ENTRY (object);
	const gchar         *slash;

	switch (prop_id)
	{
	case PROP_PATH:
		/* The name is the last component; it points into the path */
		g_free (self->path);
		self->path = g_strdup (g_value_get_string (value) != NULL ? g_value_get_string (value) : "");
		slash = strrchr (self->path, '/');
		self->name = (slash != NULL) ? (gchar *) slash + 1 : self->path;
		break;
	case PROP_IS_FOLDER:
		self->is_folder = g_value_get_boolean (value);
		break;
	case PROP_ASSET_TYPE:
		self->type = (LrgAssetType) g_value_get_enum (value);
		break;
	case PROP_SIZE:
		self->size = g_value_get_uint64 (value);
		break;
	case PROP_DESCRIPTION:
		lrg_asset_source_entry_set_description (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_asset_source_entry_class_init (LrgAssetSourceEntryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_asset_source_entry_finalize;
	object_class->get_property = lrg_asset_source_entry_get_property;
	object_class->set_property = lrg_asset_source_entry_set_property;

	/**
	 * LrgAssetSourceEntry:path:
	 *
	 * The full virtual path.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_PATH] =
		g_param_spec_string ("path", "Path", "Full virtual path", "",
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAssetSourceEntry:name:
	 *
	 * The last component of the path.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_NAME] =
		g_param_spec_string ("name", "Name", "Last path component", "",
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAssetSourceEntry:is-folder:
	 *
	 * %TRUE for a folder.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_IS_FOLDER] =
		g_param_spec_boolean ("is-folder", "Is Folder", "Whether the entry is a folder", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAssetSourceEntry:asset-type:
	 *
	 * The type of the file the entry exports to.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_ASSET_TYPE] =
		g_param_spec_enum ("asset-type", "Asset Type", "Type of the exported file", LRG_TYPE_ASSET_TYPE,
		                   LRG_ASSET_TYPE_UNKNOWN,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAssetSourceEntry:size:
	 *
	 * The size of the source data in bytes, 0 when unknown.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_SIZE] =
		g_param_spec_uint64 ("size", "Size", "Source data size in bytes", 0, G_MAXUINT64, 0,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAssetSourceEntry:description:
	 *
	 * A one-line description, or %NULL.
	 *
	 * Since: 0.8.2
	 */
	entry_props[PROP_DESCRIPTION] =
		g_param_spec_string ("description", "Description", "One-line description", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, entry_props);
}

static void
lrg_asset_source_entry_init (LrgAssetSourceEntry *self)
{
	self->path = g_strdup ("");
	self->name = self->path;
	self->type = LRG_ASSET_TYPE_UNKNOWN;
}

LrgAssetSourceEntry *
lrg_asset_source_entry_new (const gchar  *path,
                            gboolean      is_folder,
                            LrgAssetType  type,
                            guint64       size)
{
	g_return_val_if_fail (path != NULL, NULL);

	return g_object_new (LRG_TYPE_ASSET_SOURCE_ENTRY,
	                     "path", path,
	                     "is-folder", is_folder,
	                     "asset-type", type,
	                     "size", size,
	                     NULL);
}

const gchar *
lrg_asset_source_entry_get_path (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), NULL);
	return self->path;
}

const gchar *
lrg_asset_source_entry_get_name (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), NULL);
	return self->name;
}

gboolean
lrg_asset_source_entry_is_folder (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), FALSE);
	return self->is_folder;
}

LrgAssetType
lrg_asset_source_entry_get_asset_type (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), LRG_ASSET_TYPE_UNKNOWN);
	return self->type;
}

guint64
lrg_asset_source_entry_get_size (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), 0);
	return self->size;
}

const gchar *
lrg_asset_source_entry_get_description (LrgAssetSourceEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self), NULL);
	return self->description;
}

void
lrg_asset_source_entry_set_description (LrgAssetSourceEntry *self,
                                        const gchar         *description)
{
	g_return_if_fail (LRG_IS_ASSET_SOURCE_ENTRY (self));

	if (g_strcmp0 (self->description, description) == 0)
		return;
	g_free (self->description);
	self->description = g_strdup (description);
	g_object_notify_by_pspec (G_OBJECT (self), entry_props[PROP_DESCRIPTION]);
}

/* ==========================================================================
 * LrgAssetSource
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgAssetSource, lrg_asset_source, G_TYPE_OBJECT)

static void
lrg_asset_source_default_init (LrgAssetSourceInterface *iface)
{
	/* Every virtual function defaults to NULL: get_id, list and
	 * export_entry are required, get_title and search optional. */
}

gchar *
lrg_asset_source_normalize_path (const gchar  *path,
                                 GError      **error)
{
	g_auto(GStrv) parts = NULL;
	GString      *out;
	guint         i;

	if (path == NULL)
		return g_strdup ("");
	if (strchr (path, '\\') != NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		             "Invalid virtual path '%s': backslashes are not allowed", path);
		return NULL;
	}

	/* Split on '/', drop empty components, refuse "." and ".." */
	parts = g_strsplit (path, "/", -1);
	out = g_string_new (NULL);
	for (i = 0; parts[i] != NULL; i++)
	{
		if (parts[i][0] == '\0')
			continue;
		if (g_strcmp0 (parts[i], ".") == 0 || g_strcmp0 (parts[i], "..") == 0)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
			             "Invalid virtual path '%s': '.' and '..' are not allowed", path);
			g_string_free (out, TRUE);
			return NULL;
		}
		if (out->len > 0)
			g_string_append_c (out, '/');
		g_string_append (out, parts[i]);
	}
	return g_string_free (out, FALSE);
}

const gchar *
lrg_asset_source_get_id (LrgAssetSource *self)
{
	LrgAssetSourceInterface *iface;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);

	iface = LRG_ASSET_SOURCE_GET_IFACE (self);
	g_return_val_if_fail (iface->get_id != NULL, NULL);
	return iface->get_id (self);
}

const gchar *
lrg_asset_source_get_title (LrgAssetSource *self)
{
	LrgAssetSourceInterface *iface;
	const gchar             *title = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);

	iface = LRG_ASSET_SOURCE_GET_IFACE (self);
	if (iface->get_title != NULL)
		title = iface->get_title (self);
	return (title != NULL && *title != '\0') ? title : lrg_asset_source_get_id (self);
}

GPtrArray *
lrg_asset_source_list (LrgAssetSource  *self,
                       const gchar     *folder,
                       GError         **error)
{
	LrgAssetSourceInterface *iface;
	g_autofree gchar        *norm = NULL;
	GPtrArray               *entries;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	iface = LRG_ASSET_SOURCE_GET_IFACE (self);
	if (iface->list == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Asset source '%s' cannot list folders", lrg_asset_source_get_id (self));
		return NULL;
	}
	norm = lrg_asset_source_normalize_path (folder, error);
	if (norm == NULL)
		return NULL;

	entries = iface->list (self, norm, error);
	if (entries == NULL && error != NULL && *error == NULL)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Asset source '%s' failed to list '%s'", lrg_asset_source_get_id (self), norm);
	return entries;
}

gboolean
lrg_asset_source_implements_search (LrgAssetSource *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), FALSE);
	return LRG_ASSET_SOURCE_GET_IFACE (self)->search != NULL;
}

/* Empties the walk queue: (folder string, depth) pairs. */
static void
drain_queue (GQueue *queue)
{
	while (!g_queue_is_empty (queue))
	{
		g_free (g_queue_pop_head (queue));
		g_queue_pop_head (queue);
	}
}

/*
 * search_walk:
 *
 * The fallback search: a breadth-first walk with list(), collecting
 * entries whose case-folded name contains the case-folded @needle,
 * until @limit results (0 = no limit). Folders that fail to list are
 * skipped (a broken branch must not hide the rest of the tree); the
 * root failing is an error.
 */
static GPtrArray *
search_walk (LrgAssetSource  *self,
             const gchar     *needle,
             guint            limit,
             GError         **error)
{
	g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func (g_object_unref);
	GQueue               queue = G_QUEUE_INIT;
	gboolean             first = TRUE;

	g_queue_push_tail (&queue, g_strdup (""));
	g_queue_push_tail (&queue, GUINT_TO_POINTER (0));
	while (!g_queue_is_empty (&queue))
	{
		g_autofree gchar    *folder = g_queue_pop_head (&queue);
		guint                depth = GPOINTER_TO_UINT (g_queue_pop_head (&queue));
		g_autoptr(GPtrArray) entries = NULL;
		g_autoptr(GError)    local = NULL;
		guint                i;

		entries = lrg_asset_source_list (self, folder, &local);
		if (entries == NULL)
		{
			if (first)
			{
				g_propagate_error (error, g_steal_pointer (&local));
				drain_queue (&queue);
				return NULL;
			}
			continue;
		}
		first = FALSE;

		for (i = 0; i < entries->len; i++)
		{
			LrgAssetSourceEntry *e = g_ptr_array_index (entries, i);
			g_autofree gchar    *name = g_utf8_casefold (e->name, -1);

			if (strstr (name, needle) != NULL)
			{
				g_ptr_array_add (result, g_object_ref (e));
				if (limit > 0 && result->len >= limit)
				{
					drain_queue (&queue);
					return g_steal_pointer (&result);
				}
			}
			if (e->is_folder && depth + 1 < SEARCH_MAX_DEPTH)
			{
				g_queue_push_tail (&queue, g_strdup (e->path));
				g_queue_push_tail (&queue, GUINT_TO_POINTER (depth + 1));
			}
		}
	}
	return g_steal_pointer (&result);
}

GPtrArray *
lrg_asset_source_search (LrgAssetSource  *self,
                         const gchar     *pattern,
                         guint            limit,
                         GError         **error)
{
	LrgAssetSourceInterface *iface;
	g_autofree gchar        *needle = NULL;
	GPtrArray               *entries;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);
	g_return_val_if_fail (pattern != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	iface = LRG_ASSET_SOURCE_GET_IFACE (self);
	if (iface->search != NULL)
	{
		entries = iface->search (self, pattern, limit, error);
		if (entries == NULL && error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			             "Asset source '%s' failed to search for '%s'", lrg_asset_source_get_id (self), pattern);
		/* Honour the limit even when the source does not */
		if (entries != NULL && limit > 0 && entries->len > limit)
			g_ptr_array_set_size (entries, limit);
		return entries;
	}

	needle = g_utf8_casefold (pattern, -1);
	return search_walk (self, needle, limit, error);
}

LrgAssetSourceEntry *
lrg_asset_source_find_entry (LrgAssetSource  *self,
                             const gchar     *path,
                             GError         **error)
{
	g_autofree gchar    *norm = NULL;
	g_autofree gchar    *parent = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	gchar               *slash;
	guint                i;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);

	norm = lrg_asset_source_normalize_path (path, error);
	if (norm == NULL)
		return NULL;
	if (*norm == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The root folder is not an entry");
		return NULL;
	}

	/* List the parent folder and pick the entry out of it */
	parent = g_strdup (norm);
	slash = strrchr (parent, '/');
	if (slash != NULL)
		*slash = '\0';
	else
		*parent = '\0';
	entries = lrg_asset_source_list (self, parent, error);
	if (entries == NULL)
		return NULL;
	for (i = 0; i < entries->len; i++)
	{
		LrgAssetSourceEntry *e = g_ptr_array_index (entries, i);

		if (g_strcmp0 (e->path, norm) == 0)
			return g_object_ref (e);
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No entry '%s' in asset source '%s'", norm,
	             lrg_asset_source_get_id (self));
	return NULL;
}

gboolean
lrg_asset_source_export_entry (LrgAssetSource  *self,
                               const gchar     *path,
                               const gchar     *dest_dir,
                               gchar          **out_file,
                               GError         **error)
{
	LrgAssetSourceInterface *iface;
	g_autofree gchar        *norm = NULL;
	g_autofree gchar        *file = NULL;
	gboolean                 ok;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), FALSE);
	g_return_val_if_fail (dest_dir != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (out_file != NULL)
		*out_file = NULL;
	iface = LRG_ASSET_SOURCE_GET_IFACE (self);
	if (iface->export_entry == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Asset source '%s' cannot export", lrg_asset_source_get_id (self));
		return FALSE;
	}
	norm = lrg_asset_source_normalize_path (path, error);
	if (norm == NULL)
		return FALSE;
	if (*norm == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, "The root folder cannot be exported");
		return FALSE;
	}

	ok = iface->export_entry (self, norm, dest_dir, &file, error);
	if (!ok)
	{
		if (error != NULL && *error == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Asset source '%s' failed to export '%s'",
			             lrg_asset_source_get_id (self), norm);
		return FALSE;
	}

	/* A relative main file is relative to the destination; it must exist */
	if (file != NULL && !g_path_is_absolute (file))
	{
		gchar *abs = g_build_filename (dest_dir, file, NULL);

		g_free (file);
		file = abs;
	}
	if (file == NULL || !g_file_test (file, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Asset source '%s' exported '%s' but produced no file%s%s",
		             lrg_asset_source_get_id (self), norm, file != NULL ? " at " : "", file != NULL ? file : "");
		return FALSE;
	}
	if (out_file != NULL)
		*out_file = g_steal_pointer (&file);
	return TRUE;
}

/* The id made safe as one folder name. */
static gchar *
safe_id (LrgAssetSource *self)
{
	const gchar *id = lrg_asset_source_get_id (self);
	gchar       *safe = g_strdup (id != NULL && *id != '\0' ? id : "source");
	gchar       *p;

	for (p = safe; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p) && *p != '-' && *p != '_' && *p != '.')
			*p = '_';
	if (g_strcmp0 (safe, ".") == 0 || g_strcmp0 (safe, "..") == 0)
	{
		g_free (safe);
		safe = g_strdup ("_");
	}
	return safe;
}

gchar *
lrg_asset_source_get_cache_dir (LrgAssetSource *self)
{
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);

	id = safe_id (self);
	return g_build_filename (g_get_user_cache_dir (), "libregnum", "editor", "sources", id, NULL);
}

gchar *
lrg_asset_source_get_cache_entry_dir (LrgAssetSource *self,
                                      const gchar    *path,
                                      const gchar    *cache_dir)
{
	g_autofree gchar *norm = NULL;
	g_autofree gchar *root = NULL;
	g_auto(GStrv)     parts = NULL;
	gchar            *dir;
	guint             i;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);

	norm = lrg_asset_source_normalize_path (path, NULL);
	if (norm == NULL || *norm == '\0')
		return NULL;
	root = (cache_dir != NULL) ? g_strdup (cache_dir) : lrg_asset_source_get_cache_dir (self);

	/* Join component by component so the separator is the platform's */
	parts = g_strsplit (norm, "/", -1);
	dir = g_strdup (root);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *next = g_build_filename (dir, parts[i], NULL);

		g_free (dir);
		dir = next;
	}
	return dir;
}

/* The memo of exports done in this process: "cache-root\npath" -> file. */
static GHashTable *
memo_of (LrgAssetSource *self)
{
	GHashTable *memo = g_object_get_data (G_OBJECT (self), MEMO_KEY);

	if (memo == NULL)
	{
		memo = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		g_object_set_data_full (G_OBJECT (self), MEMO_KEY, memo, (GDestroyNotify) g_hash_table_unref);
	}
	return memo;
}

/* Reads a finished export's main file from the entry folder's marker. */
static gchar *
read_marker (const gchar *entry_dir)
{
	g_autofree gchar *marker = g_build_filename (entry_dir, LRG_ASSET_SOURCE_CACHE_MARKER, NULL);
	g_autofree gchar *text = NULL;
	gchar            *file;

	if (!g_file_get_contents (marker, &text, NULL, NULL))
		return NULL;
	g_strstrip (text);
	if (*text == '\0')
		return NULL;
	file = g_path_is_absolute (text) ? g_strdup (text) : g_build_filename (entry_dir, text, NULL);
	if (!g_file_test (file, G_FILE_TEST_IS_REGULAR))
	{
		g_free (file);
		return NULL;
	}
	return file;
}

gchar *
lrg_asset_source_export_cached (LrgAssetSource  *self,
                                const gchar     *path,
                                const gchar     *cache_dir,
                                GError         **error)
{
	g_autofree gchar *norm = NULL;
	g_autofree gchar *root = NULL;
	g_autofree gchar *entry_dir = NULL;
	g_autofree gchar *key = NULL;
	g_autofree gchar *file = NULL;
	g_autofree gchar *marker = NULL;
	g_autofree gchar *record = NULL;
	GHashTable       *memo;
	const gchar      *known;

	g_return_val_if_fail (LRG_IS_ASSET_SOURCE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	norm = lrg_asset_source_normalize_path (path, error);
	if (norm == NULL)
		return NULL;
	if (*norm == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, "The root folder cannot be exported");
		return NULL;
	}
	root = (cache_dir != NULL) ? g_strdup (cache_dir) : lrg_asset_source_get_cache_dir (self);
	entry_dir = lrg_asset_source_get_cache_entry_dir (self, norm, root);

	/* 1. This process already exported it and the file is still there */
	memo = memo_of (self);
	key = g_strconcat (root, "\n", norm, NULL);
	known = g_hash_table_lookup (memo, key);
	if (known != NULL && g_file_test (known, G_FILE_TEST_IS_REGULAR))
		return g_strdup (known);

	/* 2. An earlier run finished an export (the marker names the file) */
	file = read_marker (entry_dir);
	if (file != NULL)
	{
		g_hash_table_replace (memo, g_strdup (key), g_strdup (file));
		return g_steal_pointer (&file);
	}

	/* 3. Export now, then record the main file in the marker */
	if (g_mkdir_with_parents (entry_dir, 0755) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved), "Cannot create the cache folder %s: %s",
		             entry_dir, g_strerror (saved));
		return NULL;
	}
	if (!lrg_asset_source_export_entry (self, norm, entry_dir, &file, error))
		return NULL;

	marker = g_build_filename (entry_dir, LRG_ASSET_SOURCE_CACHE_MARKER, NULL);
	if (g_str_has_prefix (file, entry_dir) && file[strlen (entry_dir)] == G_DIR_SEPARATOR)
		record = g_strconcat (file + strlen (entry_dir) + 1, "\n", NULL);
	else
		record = g_strconcat (file, "\n", NULL);
	if (!g_file_set_contents (marker, record, -1, error))
		return NULL;

	g_hash_table_replace (memo, g_strdup (key), g_strdup (file));
	return g_steal_pointer (&file);
}

/* Removes the marker of one entry folder (the export's own bookkeeping
 * file in the cache, not user data). */
static void
remove_marker (const gchar *entry_dir)
{
	g_autofree gchar *marker = NULL;

	if (entry_dir == NULL)
		return;
	marker = g_build_filename (entry_dir, LRG_ASSET_SOURCE_CACHE_MARKER, NULL);
	if (g_file_test (marker, G_FILE_TEST_EXISTS))
		g_unlink (marker);
}

void
lrg_asset_source_invalidate_cache (LrgAssetSource *self,
                                   const gchar    *path,
                                   const gchar    *cache_dir)
{
	g_autofree gchar *root = NULL;
	GHashTable       *memo;
	GHashTableIter    iter;
	gpointer          key;

	g_return_if_fail (LRG_IS_ASSET_SOURCE (self));

	root = (cache_dir != NULL) ? g_strdup (cache_dir) : lrg_asset_source_get_cache_dir (self);
	memo = memo_of (self);

	if (path != NULL)
	{
		g_autofree gchar *norm = lrg_asset_source_normalize_path (path, NULL);
		g_autofree gchar *mkey = NULL;
		g_autofree gchar *dir = NULL;

		if (norm == NULL || *norm == '\0')
			return;
		mkey = g_strconcat (root, "\n", norm, NULL);
		g_hash_table_remove (memo, mkey);
		dir = lrg_asset_source_get_cache_entry_dir (self, norm, root);
		remove_marker (dir);
		return;
	}

	/* Every export of this process under @root */
	g_hash_table_iter_init (&iter, memo);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		const gchar      *nl = strchr (key, '\n');
		g_autofree gchar *dir = NULL;

		if (nl == NULL || strncmp (key, root, (gsize) (nl - (const gchar *) key)) != 0 ||
		    root[nl - (const gchar *) key] != '\0')
			continue;
		dir = lrg_asset_source_get_cache_entry_dir (self, nl + 1, root);
		remove_marker (dir);
		g_hash_table_iter_remove (&iter);
	}
}
