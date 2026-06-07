/* lrg-asset-database.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Discovery and classification of project assets.
 */

#include "lrg-asset-database.h"

/* ==========================================================================
 * LrgAssetEntry
 * ========================================================================== */

struct _LrgAssetEntry
{
	GObject parent_instance;

	gchar        *path;
	gchar        *name;
	LrgAssetType  type;
	gchar        *guid;
};

G_DEFINE_FINAL_TYPE (LrgAssetEntry, lrg_asset_entry, G_TYPE_OBJECT)

static void
lrg_asset_entry_finalize (GObject *object)
{
	LrgAssetEntry *self = LRG_ASSET_ENTRY (object);

	g_clear_pointer (&self->path, g_free);
	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->guid, g_free);

	G_OBJECT_CLASS (lrg_asset_entry_parent_class)->finalize (object);
}

static void
lrg_asset_entry_class_init (LrgAssetEntryClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_asset_entry_finalize;
}

static void
lrg_asset_entry_init (LrgAssetEntry *self)
{
}

LrgAssetEntry *
lrg_asset_entry_new (const gchar  *path,
                     const gchar  *name,
                     LrgAssetType  type,
                     const gchar  *guid)
{
	LrgAssetEntry *self = g_object_new (LRG_TYPE_ASSET_ENTRY, NULL);

	self->path = g_strdup (path);
	self->name = g_strdup (name);
	self->type = type;
	self->guid = g_strdup (guid);

	return self;
}

const gchar *
lrg_asset_entry_get_path (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->path;
}

const gchar *
lrg_asset_entry_get_name (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->name;
}

LrgAssetType
lrg_asset_entry_get_asset_type (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), LRG_ASSET_TYPE_UNKNOWN);
	return self->type;
}

const gchar *
lrg_asset_entry_get_guid (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->guid;
}

/* ==========================================================================
 * LrgAssetDatabase
 * ========================================================================== */

struct _LrgAssetDatabase
{
	GObject parent_instance;

	GPtrArray *search_dirs;  /* gchar* */
	GPtrArray *entries;      /* LrgAssetEntry* */
};

G_DEFINE_FINAL_TYPE (LrgAssetDatabase, lrg_asset_database, G_TYPE_OBJECT)

static void
lrg_asset_database_finalize (GObject *object)
{
	LrgAssetDatabase *self = LRG_ASSET_DATABASE (object);

	g_clear_pointer (&self->search_dirs, g_ptr_array_unref);
	g_clear_pointer (&self->entries, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_asset_database_parent_class)->finalize (object);
}

static void
lrg_asset_database_class_init (LrgAssetDatabaseClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_asset_database_finalize;
}

static void
lrg_asset_database_init (LrgAssetDatabase *self)
{
	self->search_dirs = g_ptr_array_new_with_free_func (g_free);
	self->entries = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgAssetDatabase *
lrg_asset_database_new (void)
{
	return g_object_new (LRG_TYPE_ASSET_DATABASE, NULL);
}

static gboolean
has_ext (const gchar *lower, const gchar * const *exts)
{
	guint i;

	for (i = 0; exts[i] != NULL; i++)
		if (g_str_has_suffix (lower, exts[i]))
			return TRUE;

	return FALSE;
}

LrgAssetType
lrg_asset_database_classify (const gchar *path)
{
	g_autofree gchar *lower = NULL;

	static const gchar * const tex[]   = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", NULL };
	static const gchar * const model[] = { ".gltf", ".glb", ".obj", ".fbx", ".ply", NULL };
	static const gchar * const audio[] = { ".wav", ".ogg", ".flac", ".mp3", NULL };
	static const gchar * const font[]  = { ".ttf", ".otf", NULL };
	static const gchar * const script[] = { ".lua", ".py", ".js", ".c", NULL };
	static const gchar * const tileset[] = { ".tileset", ".tsx", NULL };

	if (path == NULL)
		return LRG_ASSET_TYPE_UNKNOWN;

	lower = g_ascii_strdown (path, -1);

	if (has_ext (lower, tex))
		return LRG_ASSET_TYPE_TEXTURE;
	if (has_ext (lower, model))
		return LRG_ASSET_TYPE_MODEL;
	if (has_ext (lower, audio))
		return LRG_ASSET_TYPE_AUDIO;
	if (has_ext (lower, font))
		return LRG_ASSET_TYPE_FONT;
	if (g_str_has_suffix (lower, ".rlevel"))
		return LRG_ASSET_TYPE_LEVEL;
	if (g_str_has_suffix (lower, ".rprefab"))
		return LRG_ASSET_TYPE_PREFAB;
	if (has_ext (lower, tileset))
		return LRG_ASSET_TYPE_TILESET;
	if (g_str_has_suffix (lower, ".scene"))
		return LRG_ASSET_TYPE_SCENE;
	if (has_ext (lower, script))
		return LRG_ASSET_TYPE_SCRIPT;

	return LRG_ASSET_TYPE_UNKNOWN;
}

void
lrg_asset_database_add_search_dir (LrgAssetDatabase *self,
                                   const gchar      *dir)
{
	g_return_if_fail (LRG_IS_ASSET_DATABASE (self));
	g_return_if_fail (dir != NULL);

	g_ptr_array_add (self->search_dirs, g_strdup (dir));
}

static void
scan_dir (LrgAssetDatabase *self,
          const gchar      *base,
          const gchar      *rel)
{
	g_autofree gchar *full = (rel != NULL) ? g_build_filename (base, rel, NULL)
	                                       : g_strdup (base);
	g_autoptr(GDir)   dir = NULL;
	const gchar      *name;

	dir = g_dir_open (full, 0, NULL);
	if (dir == NULL)
		return;

	while ((name = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *child_rel = (rel != NULL) ? g_build_filename (rel, name, NULL)
		                                            : g_strdup (name);
		g_autofree gchar *child_full = g_build_filename (full, name, NULL);

		if (g_file_test (child_full, G_FILE_TEST_IS_DIR))
		{
			scan_dir (self, base, child_rel);
		}
		else if (g_file_test (child_full, G_FILE_TEST_IS_REGULAR))
		{
			g_autofree gchar *guid = g_compute_checksum_for_string (G_CHECKSUM_SHA256,
			                                                        child_rel, -1);
			LrgAssetType      type = lrg_asset_database_classify (name);
			LrgAssetEntry    *entry = lrg_asset_entry_new (child_rel, name, type, guid);

			g_ptr_array_add (self->entries, entry);
		}
	}
}

gboolean
lrg_asset_database_scan (LrgAssetDatabase  *self,
                         GError           **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);

	g_ptr_array_set_size (self->entries, 0);

	for (i = 0; i < self->search_dirs->len; i++)
	{
		const gchar *base = g_ptr_array_index (self->search_dirs, i);

		if (g_file_test (base, G_FILE_TEST_IS_DIR))
			scan_dir (self, base, NULL);
	}

	return TRUE;
}

guint
lrg_asset_database_get_count (LrgAssetDatabase *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), 0);
	return self->entries->len;
}

LrgAssetEntry *
lrg_asset_database_get_entry (LrgAssetDatabase *self,
                              guint             index)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);

	if (index >= self->entries->len)
		return NULL;

	return g_ptr_array_index (self->entries, index);
}

GPtrArray *
lrg_asset_database_get_entries (LrgAssetDatabase *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);
	return self->entries;
}

guint
lrg_asset_database_count_of_type (LrgAssetDatabase *self,
                                  LrgAssetType      type)
{
	guint i, count = 0;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), 0);

	for (i = 0; i < self->entries->len; i++)
	{
		LrgAssetEntry *entry = g_ptr_array_index (self->entries, i);

		if (lrg_asset_entry_get_asset_type (entry) == type)
			count++;
	}

	return count;
}
