/* lrg-asset-database.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Discovery and classification of project assets, with ".meta" sidecars
 * (guid + importer settings) and sidecar-aware file operations.
 */

#include "lrg-asset-database.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

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
	gchar        *full_path;    /* nullable: search dir + path */
	gchar        *search_dir;   /* nullable */
	LrgAssetMeta *meta;         /* nullable */
	gboolean      meta_on_disk;
};

G_DEFINE_FINAL_TYPE (LrgAssetEntry, lrg_asset_entry, G_TYPE_OBJECT)

static void
lrg_asset_entry_finalize (GObject *object)
{
	LrgAssetEntry *self = LRG_ASSET_ENTRY (object);

	g_clear_pointer (&self->path, g_free);
	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->guid, g_free);
	g_clear_pointer (&self->full_path, g_free);
	g_clear_pointer (&self->search_dir, g_free);
	g_clear_object (&self->meta);

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

const gchar *
lrg_asset_entry_get_full_path (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->full_path;
}

const gchar *
lrg_asset_entry_get_search_dir (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->search_dir;
}

LrgAssetMeta *
lrg_asset_entry_get_meta (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), NULL);
	return self->meta;
}

gboolean
lrg_asset_entry_has_meta_file (LrgAssetEntry *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (self), FALSE);
	return self->meta_on_disk;
}

/* ==========================================================================
 * LrgAssetDatabase
 * ========================================================================== */

struct _LrgAssetDatabase
{
	GObject parent_instance;

	GPtrArray  *search_dirs;  /* gchar* */
	GPtrArray  *entries;      /* LrgAssetEntry* */
	GHashTable *by_guid;      /* guid -> LrgAssetEntry* (borrowed) */
	GHashTable *by_path;      /* canonical full path -> LrgAssetEntry* (borrowed) */
	GPtrArray  *orphans;      /* gchar* absolute .meta paths */
	gboolean    write_meta;
};

enum
{
	PROP_0,
	PROP_WRITE_META,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgAssetDatabase, lrg_asset_database, G_TYPE_OBJECT)

static void
lrg_asset_database_finalize (GObject *object)
{
	LrgAssetDatabase *self = LRG_ASSET_DATABASE (object);

	g_clear_pointer (&self->by_guid, g_hash_table_unref);
	g_clear_pointer (&self->by_path, g_hash_table_unref);
	g_clear_pointer (&self->orphans, g_ptr_array_unref);
	g_clear_pointer (&self->search_dirs, g_ptr_array_unref);
	g_clear_pointer (&self->entries, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_asset_database_parent_class)->finalize (object);
}

static void
lrg_asset_database_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgAssetDatabase *self = LRG_ASSET_DATABASE (object);

	switch (prop_id)
	{
	case PROP_WRITE_META:
		g_value_set_boolean (value, self->write_meta);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_asset_database_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgAssetDatabase *self = LRG_ASSET_DATABASE (object);

	switch (prop_id)
	{
	case PROP_WRITE_META:
		lrg_asset_database_set_write_meta (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_asset_database_class_init (LrgAssetDatabaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_asset_database_finalize;
	object_class->get_property = lrg_asset_database_get_property;
	object_class->set_property = lrg_asset_database_set_property;

	/**
	 * LrgAssetDatabase:write-meta:
	 *
	 * Whether lrg_asset_database_scan() writes sidecars that are missing
	 * (or were changed in memory) to disk. The editor turns it on;
	 * read-only tools (lrgeditorctl, validators, asset listings) keep the
	 * default %FALSE so a scan never touches the project.
	 */
	properties[PROP_WRITE_META] =
		g_param_spec_boolean ("write-meta", "Write meta", "Write missing .meta sidecars on scan", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_asset_database_init (LrgAssetDatabase *self)
{
	self->search_dirs = g_ptr_array_new_with_free_func (g_free);
	self->entries = g_ptr_array_new_with_free_func (g_object_unref);
	self->by_guid = g_hash_table_new (g_str_hash, g_str_equal);
	self->by_path = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->orphans = g_ptr_array_new_with_free_func (g_free);
}

LrgAssetDatabase *
lrg_asset_database_new (void)
{
	return g_object_new (LRG_TYPE_ASSET_DATABASE, NULL);
}

/* The extension table lives in the core sidecar reader since 0.9.0 so the
 * runtime asset manager classifies files exactly like the editor. */
LrgAssetType
lrg_asset_database_classify (const gchar *path)
{
	return lrg_asset_meta_classify (path);
}

void
lrg_asset_database_add_search_dir (LrgAssetDatabase *self,
                                   const gchar      *dir)
{
	g_return_if_fail (LRG_IS_ASSET_DATABASE (self));
	g_return_if_fail (dir != NULL);

	g_ptr_array_add (self->search_dirs, g_strdup (dir));
}

/* ==========================================================================
 * Paths and the write guard
 * ========================================================================== */

/* Directories never descended into: VCS metadata is not project content
 * (and must never receive sidecars). */
static gboolean
is_vcs_dir (const gchar *name)
{
	return g_str_equal (name, ".git") || g_str_equal (name, ".hg") || g_str_equal (name, ".svn");
}

/* Another repository inside the project (a git submodule or a nested
 * checkout): its files belong to it, so the scan does not descend. */
static gboolean
is_nested_repo (const gchar *dir)
{
	g_autofree gchar *git = g_build_filename (dir, ".git", NULL);

	return g_file_test (git, G_FILE_TEST_EXISTS);
}

gboolean
lrg_asset_database_wants_sidecar (const gchar *path)
{
	static const gchar * const names[] = {
		"readme", "license", "licence", "copying", "authors", "changelog", "news", "makefile",
		"gnumakefile", "containerfile", "dockerfile", "justfile", NULL
	};
	static const gchar * const exts[] = {
		".md", ".org", ".txt", ".rst", ".adoc", ".h", ".hpp", ".mk", ".o", ".a", ".so", ".la", ".lo",
		".d", ".gir", ".typelib", ".pc", ".log", ".in", ".patch", ".diff", NULL
	};
	g_autofree gchar *base = NULL;
	g_autofree gchar *lower = NULL;
	const gchar      *dot;
	guint             i;

	g_return_val_if_fail (path != NULL, FALSE);
	base = g_path_get_basename (path);
	lower = g_ascii_strdown (base, -1);

	/* README, README.org, LICENSE.txt ...: the name before any extension */
	for (i = 0; names[i] != NULL; i++)
		if (g_str_has_prefix (lower, names[i])
		    && (lower[strlen (names[i])] == '\0' || lower[strlen (names[i])] == '.'))
			return FALSE;
	dot = strrchr (lower, '.');
	for (i = 0; dot != NULL && exts[i] != NULL; i++)
		if (g_str_equal (dot, exts[i]))
			return FALSE;
	/* shared libraries with versions: libfoo.so.1.2 */
	if (strstr (lower, ".so.") != NULL)
		return FALSE;
	return TRUE;
}

/* realpath(3) into a g_malloc'd string, NULL if it does not resolve. */
static gchar *
real_path (const gchar *path)
{
	char  *r = realpath (path, NULL);
	gchar *g;

	if (r == NULL)
		return NULL;
	g = g_strdup (r);
	free (r);
	return g;
}

/* Lexical canonical form (no symlink resolution) used as a lookup key. */
static gchar *
canonical (const gchar *path)
{
	return g_canonicalize_filename (path, NULL);
}

gboolean
lrg_asset_database_is_writable_path (LrgAssetDatabase *self,
                                     const gchar      *path)
{
	g_autofree gchar *base = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *real_dir = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* Hidden files ("x/.gitignore.meta") are never written. */
	base = g_path_get_basename (path);
	if (base[0] == '.')
		return FALSE;

	/* Resolve the parent directory (the file itself may not exist yet) so
	 * a symlink inside an asset dir cannot point a write elsewhere. */
	dir = g_path_get_dirname (path);
	real_dir = real_path (dir);
	if (real_dir == NULL)
		return FALSE;

	for (i = 0; i < self->search_dirs->len; i++)
	{
		g_autofree gchar *root = real_path (g_ptr_array_index (self->search_dirs, i));
		gsize             n;
		g_auto(GStrv)     parts = NULL;
		guint             k;
		gboolean          hidden = FALSE;

		if (root == NULL)
			continue;
		n = strlen (root);
		if (strncmp (real_dir, root, n) != 0 || (real_dir[n] != '\0' && real_dir[n] != G_DIR_SEPARATOR &&
		                                          !(n == 1 && root[0] == G_DIR_SEPARATOR)))
			continue;

		/* Inside this root: no hidden directory on the way down. */
		parts = g_strsplit (real_dir + n, G_DIR_SEPARATOR_S, -1);
		for (k = 0; parts[k] != NULL; k++)
			if (parts[k][0] == '.')
				hidden = TRUE;
		if (!hidden)
			return TRUE;
	}
	return FALSE;
}

/* Resolves an absolute path or one relative to a search directory to an
 * existing absolute path (lexically canonical). */
static gchar *
resolve (LrgAssetDatabase *self,
         const gchar      *path)
{
	guint i;

	if (g_path_is_absolute (path))
		return g_file_test (path, G_FILE_TEST_EXISTS) ? canonical (path) : NULL;
	for (i = 0; i < self->search_dirs->len; i++)
	{
		g_autofree gchar *full = g_build_filename (g_ptr_array_index (self->search_dirs, i), path, NULL);

		if (g_file_test (full, G_FILE_TEST_EXISTS))
			return canonical (full);
	}
	return NULL;
}

/* ==========================================================================
 * Scanning
 * ========================================================================== */

static gint
cmp_names (gconstpointer a,
           gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* Builds the entry for one asset file: loads its sidecar or makes an
 * in-memory one with the legacy guid. */
static LrgAssetEntry *
make_entry (const gchar *base,
            const gchar *rel,
            const gchar *name,
            const gchar *full)
{
	g_autofree gchar     *meta_path = lrg_asset_meta_path_for_asset (full);
	g_autofree gchar     *legacy = lrg_asset_meta_compute_legacy_guid (rel);
	g_autoptr(LrgAssetMeta) meta = NULL;
	LrgAssetEntry        *entry;
	gboolean              on_disk = FALSE;

	if (g_file_test (meta_path, G_FILE_TEST_IS_REGULAR))
	{
		g_autoptr(GError) error = NULL;

		meta = lrg_asset_meta_new_from_file (meta_path, &error);
		if (meta == NULL)
		{
			/* A broken sidecar is reported and left alone: never replace
			 * it, the user may be resolving a merge conflict in it. */
			g_warning ("Ignoring unreadable asset sidecar %s: %s", meta_path, error->message);
			meta = lrg_asset_meta_new_for_asset (name);
			lrg_asset_meta_set_guid (meta, legacy);
			lrg_asset_meta_set_dirty (meta, FALSE);
			on_disk = TRUE;   /* do not overwrite */
		}
		else
		{
			on_disk = TRUE;
			/* A sidecar without a guid (hand-written) adopts the legacy
			 * one; dirty, so write-meta completes the file. */
			if (lrg_asset_meta_get_guid (meta) == NULL)
				lrg_asset_meta_set_guid (meta, legacy);
		}
	}
	else
	{
		meta = lrg_asset_meta_new_for_asset (name);
		lrg_asset_meta_set_guid (meta, legacy);
	}

	entry = lrg_asset_entry_new (rel, name, lrg_asset_database_classify (name), lrg_asset_meta_get_guid (meta));
	entry->full_path = g_strdup (full);
	entry->search_dir = g_strdup (base);
	entry->meta = g_steal_pointer (&meta);
	entry->meta_on_disk = on_disk;
	return entry;
}

static void
scan_dir (LrgAssetDatabase *self,
          const gchar      *base,
          const gchar      *rel,
          GPtrArray        *metas)
{
	g_autofree gchar     *full = (rel != NULL) ? g_build_filename (base, rel, NULL) : g_strdup (base);
	g_autoptr(GDir)       dir = NULL;
	g_autoptr(GPtrArray)  names = g_ptr_array_new_with_free_func (g_free);
	const gchar          *name;
	guint                 i;

	dir = g_dir_open (full, 0, NULL);
	if (dir == NULL)
		return;

	/* Sorted so entry order (and duplicate-guid resolution) is stable. */
	while ((name = g_dir_read_name (dir)) != NULL)
		g_ptr_array_add (names, g_strdup (name));
	g_ptr_array_sort (names, cmp_names);

	for (i = 0; i < names->len; i++)
	{
		const gchar      *child = g_ptr_array_index (names, i);
		g_autofree gchar *child_rel = (rel != NULL) ? g_build_filename (rel, child, NULL) : g_strdup (child);
		g_autofree gchar *child_full = g_build_filename (full, child, NULL);

		if (g_file_test (child_full, G_FILE_TEST_IS_DIR))
		{
			if (!is_vcs_dir (child) && !is_nested_repo (child_full))
				scan_dir (self, base, child_rel, metas);
		}
		else if (g_file_test (child_full, G_FILE_TEST_IS_REGULAR))
		{
			if (lrg_asset_meta_is_meta_path (child))
				g_ptr_array_add (metas, canonical (child_full));
			else
				g_ptr_array_add (self->entries, make_entry (base, child_rel, child, child_full));
		}
	}
}

/* Indexes entries by guid and path. A guid seen twice (an asset copied
 * with its sidecar outside the editor) keeps the first entry; the later
 * one gets a fresh guid, written on the next sidecar write. */
static void
index_entries (LrgAssetDatabase *self)
{
	guint i;

	g_hash_table_remove_all (self->by_guid);
	g_hash_table_remove_all (self->by_path);
	for (i = 0; i < self->entries->len; i++)
	{
		LrgAssetEntry *e = g_ptr_array_index (self->entries, i);

		if (e->meta != NULL && g_hash_table_contains (self->by_guid, e->guid))
		{
			g_message ("Asset %s duplicates guid %s; assigning a new one", e->full_path, e->guid);
			lrg_asset_meta_regenerate_guid (e->meta);
			g_free (e->guid);
			e->guid = g_strdup (lrg_asset_meta_get_guid (e->meta));
		}
		g_hash_table_insert (self->by_guid, e->guid, e);
		if (e->full_path != NULL)
			g_hash_table_insert (self->by_path, canonical (e->full_path), e);
	}
}

/* Writes one entry's sidecar if allowed. */
static gboolean
write_entry_meta (LrgAssetDatabase  *self,
                  LrgAssetEntry     *e,
                  GError           **error)
{
	g_autofree gchar *meta_path = NULL;

	if (e->meta == NULL || e->full_path == NULL)
		return TRUE;
	meta_path = lrg_asset_meta_path_for_asset (e->full_path);
	if (!lrg_asset_database_is_writable_path (self, meta_path))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
		             "Refusing to write %s: outside the asset directories or hidden", meta_path);
		return FALSE;
	}
	if (!lrg_asset_meta_save_to_file (e->meta, meta_path, error))
		return FALSE;
	e->meta_on_disk = TRUE;
	return TRUE;
}

gint
lrg_asset_database_write_pending_metas (LrgAssetDatabase  *self,
                                        GError           **error)
{
	guint i;
	gint  written = 0;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	for (i = 0; i < self->entries->len; i++)
	{
		LrgAssetEntry    *e = g_ptr_array_index (self->entries, i);
		g_autofree gchar *meta_path = NULL;

		if (e->meta == NULL || e->full_path == NULL)
			continue;
		if (e->meta_on_disk && !lrg_asset_meta_is_dirty (e->meta))
			continue;
		/* documents and build files never get one written for them */
		if (!e->meta_on_disk && !lrg_asset_database_wants_sidecar (e->full_path))
			continue;
		meta_path = lrg_asset_meta_path_for_asset (e->full_path);
		if (!lrg_asset_database_is_writable_path (self, meta_path))
			continue;
		if (!write_entry_meta (self, e, error))
			return -1;
		written++;
	}
	return written;
}

gboolean
lrg_asset_database_scan (LrgAssetDatabase  *self,
                         GError           **error)
{
	g_autoptr(GPtrArray) metas = g_ptr_array_new_with_free_func (g_free);
	guint                i;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);

	g_hash_table_remove_all (self->by_guid);
	g_hash_table_remove_all (self->by_path);
	g_ptr_array_set_size (self->orphans, 0);
	g_ptr_array_set_size (self->entries, 0);

	for (i = 0; i < self->search_dirs->len; i++)
	{
		const gchar *base = g_ptr_array_index (self->search_dirs, i);

		if (g_file_test (base, G_FILE_TEST_IS_DIR))
			scan_dir (self, base, NULL, metas);
	}
	index_entries (self);

	/* Orphans: sidecars whose asset (file or folder) is gone. */
	for (i = 0; i < metas->len; i++)
	{
		const gchar      *meta = g_ptr_array_index (metas, i);
		g_autofree gchar *asset = g_strndup (meta, strlen (meta) - strlen (LRG_ASSET_META_SUFFIX));

		if (*asset != '\0' && !g_file_test (asset, G_FILE_TEST_EXISTS))
			g_ptr_array_add (self->orphans, g_strdup (meta));
	}

	/* Lazily persist new/changed sidecars; a failure here must not hide
	 * the assets, so it is logged rather than returned. */
	if (self->write_meta)
	{
		g_autoptr(GError) werr = NULL;

		if (lrg_asset_database_write_pending_metas (self, &werr) < 0)
			g_warning ("Cannot write asset sidecar: %s", werr->message);
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

gboolean
lrg_asset_database_get_write_meta (LrgAssetDatabase *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	return self->write_meta;
}

void
lrg_asset_database_set_write_meta (LrgAssetDatabase *self,
                                   gboolean          write_meta)
{
	g_return_if_fail (LRG_IS_ASSET_DATABASE (self));

	write_meta = !!write_meta;
	if (self->write_meta == write_meta)
		return;
	self->write_meta = write_meta;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WRITE_META]);
}

LrgAssetEntry *
lrg_asset_database_lookup_guid (LrgAssetDatabase *self,
                                const gchar      *guid)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);
	return g_hash_table_lookup (self->by_guid, guid);
}

LrgAssetEntry *
lrg_asset_database_lookup_path (LrgAssetDatabase *self,
                                const gchar      *path)
{
	guint i;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (g_path_is_absolute (path))
	{
		g_autofree gchar *key = canonical (path);

		return g_hash_table_lookup (self->by_path, key);
	}
	/* Relative: try each search directory in order. */
	for (i = 0; i < self->search_dirs->len; i++)
	{
		g_autofree gchar *full = g_build_filename (g_ptr_array_index (self->search_dirs, i), path, NULL);
		g_autofree gchar *key = canonical (full);
		LrgAssetEntry    *e = g_hash_table_lookup (self->by_path, key);

		if (e != NULL)
			return e;
	}
	return NULL;
}

GPtrArray *
lrg_asset_database_get_orphaned_metas (LrgAssetDatabase *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);
	return self->orphans;
}

gboolean
lrg_asset_database_save_meta (LrgAssetDatabase  *self,
                              LrgAssetEntry     *entry,
                              GError           **error)
{
	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	g_return_val_if_fail (LRG_IS_ASSET_ENTRY (entry), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (entry->meta == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Entry %s has no sidecar", entry->path);
		return FALSE;
	}
	return write_entry_meta (self, entry, error);
}

/* ==========================================================================
 * File operations
 * ========================================================================== */

/* Before moving @abs (file or folder), make sure every affected asset's
 * sidecar is on disk so its guid travels with it. */
static gboolean
persist_metas_under (LrgAssetDatabase  *self,
                     const gchar       *abs,
                     GError           **error)
{
	g_autofree gchar *prefix = g_strconcat (abs, G_DIR_SEPARATOR_S, NULL);
	guint             i;

	for (i = 0; i < self->entries->len; i++)
	{
		LrgAssetEntry    *e = g_ptr_array_index (self->entries, i);
		g_autofree gchar *key = NULL;

		if (e->full_path == NULL || e->meta == NULL)
			continue;
		key = canonical (e->full_path);
		if (!g_str_equal (key, abs) && !g_str_has_prefix (key, prefix))
			continue;
		if (e->meta_on_disk && !lrg_asset_meta_is_dirty (e->meta))
			continue;
		if (!write_entry_meta (self, e, error))
			return FALSE;
	}
	return TRUE;
}

/* Validates a destination: inside the guard, not existing. */
static gboolean
check_destination (LrgAssetDatabase  *self,
                   const gchar       *dest,
                   GError           **error)
{
	if (g_file_test (dest, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", dest);
		return FALSE;
	}
	if (!lrg_asset_database_is_writable_path (self, dest))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
		             "%s is outside the asset directories or hidden", dest);
		return FALSE;
	}
	return TRUE;
}

/* Moves @src to @dest along with its sidecar, then rescans. */
static gboolean
move_with_meta (LrgAssetDatabase  *self,
                const gchar       *src,
                const gchar       *dest,
                GError           **error)
{
	g_autofree gchar *src_meta = lrg_asset_meta_path_for_asset (src);
	g_autofree gchar *dest_meta = lrg_asset_meta_path_for_asset (dest);
	gboolean          is_dir = g_file_test (src, G_FILE_TEST_IS_DIR);

	if (g_str_equal (src, dest))
		return TRUE;
	if (!check_destination (self, dest, error))
		return FALSE;
	if (is_dir)
	{
		g_autofree gchar *prefix = g_strconcat (src, G_DIR_SEPARATOR_S, NULL);

		if (g_str_has_prefix (dest, prefix))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot move %s into itself", src);
			return FALSE;
		}
	}
	if (g_file_test (dest_meta, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s already exists", dest_meta);
		return FALSE;
	}

	/* 1. Sidecars of everything that moves are made durable first. */
	if (!persist_metas_under (self, src, error))
		return FALSE;

	/* 2. The asset itself. */
	if (g_rename (src, dest) != 0)
	{
		int saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved), "Cannot move %s to %s: %s", src, dest,
		             g_strerror (saved));
		return FALSE;
	}

	/* 3. Its sidecar (a folder's own "dir.meta" too, when present). On
	 * failure put the asset back so the pair never splits. */
	if (g_file_test (src_meta, G_FILE_TEST_IS_REGULAR) && g_rename (src_meta, dest_meta) != 0)
	{
		int saved = errno;

		if (g_rename (dest, src) != 0)
			g_warning ("Cannot restore %s after a failed sidecar move", src);
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved), "Cannot move %s to %s: %s", src_meta,
		             dest_meta, g_strerror (saved));
		return FALSE;
	}
	return lrg_asset_database_scan (self, error);
}

gboolean
lrg_asset_database_rename (LrgAssetDatabase  *self,
                           const gchar       *path,
                           const gchar       *new_name,
                           GError           **error)
{
	g_autofree gchar *src = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *dest = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (new_name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (*new_name == '\0' || strchr (new_name, G_DIR_SEPARATOR) != NULL || g_str_equal (new_name, ".") ||
	    g_str_equal (new_name, "..") || lrg_asset_meta_is_meta_path (new_name))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, "Invalid asset name '%s'", new_name);
		return FALSE;
	}
	src = resolve (self, path);
	if (src == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset '%s'", path);
		return FALSE;
	}
	dir = g_path_get_dirname (src);
	dest = g_build_filename (dir, new_name, NULL);
	return move_with_meta (self, src, dest, error);
}

gboolean
lrg_asset_database_move (LrgAssetDatabase  *self,
                         const gchar       *path,
                         const gchar       *dest_dir,
                         GError           **error)
{
	g_autofree gchar *src = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *base = NULL;
	g_autofree gchar *dest = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (dest_dir != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	src = resolve (self, path);
	if (src == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset '%s'", path);
		return FALSE;
	}
	/* "" or "." means the first search directory's root. */
	if (*dest_dir == '\0' || g_str_equal (dest_dir, "."))
		dir = self->search_dirs->len > 0 ? canonical (g_ptr_array_index (self->search_dirs, 0)) : NULL;
	else
		dir = resolve (self, dest_dir);
	if (dir == NULL || !g_file_test (dir, G_FILE_TEST_IS_DIR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "No folder '%s'", dest_dir);
		return FALSE;
	}
	base = g_path_get_basename (src);
	dest = g_build_filename (dir, base, NULL);
	return move_with_meta (self, src, dest, error);
}

/* "name.ext" -> "name copy.ext", "name copy 2.ext", ... (first free). */
static gchar *
duplicate_name (const gchar *src)
{
	g_autofree gchar *dir = g_path_get_dirname (src);
	g_autofree gchar *base = g_path_get_basename (src);
	const gchar      *dot = strrchr (base, '.');
	g_autofree gchar *stem = NULL;
	const gchar      *ext;
	guint             n;

	if (dot == NULL || dot == base)
		dot = base + strlen (base);
	stem = g_strndup (base, (gsize) (dot - base));
	ext = dot;
	for (n = 1; n < 10000; n++)
	{
		g_autofree gchar *name = (n == 1) ? g_strdup_printf ("%s copy%s", stem, ext)
		                                  : g_strdup_printf ("%s copy %u%s", stem, n, ext);
		g_autofree gchar *full = g_build_filename (dir, name, NULL);
		g_autofree gchar *meta = lrg_asset_meta_path_for_asset (full);

		if (!g_file_test (full, G_FILE_TEST_EXISTS) && !g_file_test (meta, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&full);
	}
	return NULL;
}

LrgAssetEntry *
lrg_asset_database_duplicate (LrgAssetDatabase  *self,
                              const gchar       *path,
                              GError           **error)
{
	g_autofree gchar       *src = NULL;
	g_autofree gchar       *dest = NULL;
	g_autofree gchar       *dest_meta = NULL;
	g_autofree gchar       *guid = NULL;
	g_autoptr(GFile)        from = NULL;
	g_autoptr(GFile)        to = NULL;
	g_autoptr(LrgAssetMeta) meta = NULL;
	LrgAssetEntry          *entry;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	src = resolve (self, path);
	if (src == NULL || !g_file_test (src, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, src == NULL ? G_IO_ERROR_NOT_FOUND : G_IO_ERROR_NOT_SUPPORTED,
		             src == NULL ? "No asset '%s'" : "Only files can be duplicated ('%s')", path);
		return NULL;
	}
	dest = duplicate_name (src);
	if (dest == NULL || !check_destination (self, dest, error))
	{
		if (dest == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "No free name to duplicate %s", src);
		return NULL;
	}

	/* The copy's sidecar: the original's settings under a fresh guid. */
	entry = g_hash_table_lookup (self->by_path, src);
	if (entry != NULL && entry->meta != NULL)
		meta = lrg_asset_meta_copy (entry->meta);
	else
	{
		g_autoptr(GError) lerr = NULL;

		meta = lrg_asset_meta_load_for_asset (src, &lerr);
		if (meta == NULL)
			meta = lrg_asset_meta_new_for_asset (src);
	}
	lrg_asset_meta_regenerate_guid (meta);
	guid = g_strdup (lrg_asset_meta_get_guid (meta));

	from = g_file_new_for_path (src);
	to = g_file_new_for_path (dest);
	if (!g_file_copy (from, to, G_FILE_COPY_NONE, NULL, NULL, NULL, error))
		return NULL;
	dest_meta = lrg_asset_meta_path_for_asset (dest);
	if (!lrg_asset_meta_save_to_file (meta, dest_meta, error))
	{
		g_unlink (dest);
		return NULL;
	}
	if (!lrg_asset_database_scan (self, error))
		return NULL;
	return lrg_asset_database_lookup_guid (self, guid);
}

gboolean
lrg_asset_database_trash (LrgAssetDatabase  *self,
                          const gchar       *path,
                          GError           **error)
{
	g_autofree gchar *src = NULL;
	g_autofree gchar *src_meta = NULL;
	g_autoptr(GFile)  file = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_DATABASE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	src = resolve (self, path);
	if (src == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No asset '%s'", path);
		return FALSE;
	}
	if (!lrg_asset_database_is_writable_path (self, src))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
		             "%s is outside the asset directories or hidden", src);
		return FALSE;
	}

	/* The asset first: if that fails nothing changed. A sidecar left
	 * behind by a later failure shows up as an orphan, never the reverse
	 * (an asset that silently lost its guid). */
	file = g_file_new_for_path (src);
	if (!g_file_trash (file, NULL, error))
		return FALSE;
	src_meta = lrg_asset_meta_path_for_asset (src);
	if (g_file_test (src_meta, G_FILE_TEST_IS_REGULAR))
	{
		g_autoptr(GFile) meta = g_file_new_for_path (src_meta);

		if (!g_file_trash (meta, NULL, error))
		{
			lrg_asset_database_scan (self, NULL);
			return FALSE;
		}
	}
	return lrg_asset_database_scan (self, error);
}
