/* lrg-asset-importer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-asset-importer.h"
#include "../core/lrg-asset-meta.h"

#include <glib/gstdio.h>
#include <string.h>

G_DEFINE_TYPE (LrgAssetImporter, lrg_asset_importer, G_TYPE_OBJECT)

GType lrg_copy_importer_get_type (void);
GType lrg_fbx_importer_get_type (void);

typedef struct
{
	LrgAssetImporter parent;
} LrgCopyImporter;

typedef struct
{
	LrgAssetImporterClass parent_class;
} LrgCopyImporterClass;

G_DEFINE_TYPE (LrgCopyImporter, lrg_copy_importer, LRG_TYPE_ASSET_IMPORTER)

typedef struct
{
	LrgAssetImporter parent;
} LrgFbxImporter;

typedef struct
{
	LrgAssetImporterClass parent_class;
} LrgFbxImporterClass;

G_DEFINE_TYPE (LrgFbxImporter, lrg_fbx_importer, LRG_TYPE_ASSET_IMPORTER)

struct _LrgAssetImporterRegistry
{
	GObject    parent_instance;
	GHashTable *by_name;   /* name -> LrgAssetImporter */
};

G_DEFINE_FINAL_TYPE (LrgAssetImporterRegistry, lrg_asset_importer_registry, G_TYPE_OBJECT)

static gboolean
copy_import (LrgAssetImporter  *self,
             const gchar       *source,
             const gchar       *dest,
             LrgAssetMeta      *meta,
             GError           **error)
{
	gchar *bytes;
	gsize  size;
	g_autofree gchar *parent = NULL;

	(void) self;
	(void) meta;
	if (!g_file_get_contents (source, &bytes, &size, error))
		return FALSE;
	parent = g_path_get_dirname (dest);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_free (bytes);
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "Could not create '%s'", parent);
		return FALSE;
	}
	if (!g_file_set_contents (dest, bytes, (gssize) size, error))
	{
		g_free (bytes);
		return FALSE;
	}
	g_free (bytes);
	return TRUE;
}

/* Implemented in lrg-fbx-ufbx.c, which is compiled as gnu11 for ufbx. */
gboolean lrg_fbx_convert_to_obj (const gchar *source, const gchar *dest, GError **error);

static gboolean
fbx_import (LrgAssetImporter  *self,
            const gchar       *source,
            const gchar       *dest,
            LrgAssetMeta      *meta,
            GError           **error)
{
	(void) self;
	(void) meta;
	return lrg_fbx_convert_to_obj (source, dest, error);
}

static void
lrg_asset_importer_class_init (LrgAssetImporterClass *klass)
{
	klass->import = NULL;
}

static void
lrg_asset_importer_init (LrgAssetImporter *self)
{
	(void) self;
}

static void
lrg_copy_importer_class_init (LrgCopyImporterClass *klass)
{
	LRG_ASSET_IMPORTER_CLASS (klass)->import = copy_import;
}

static void
lrg_copy_importer_init (LrgCopyImporter *self)
{
	(void) self;
}

static void
lrg_fbx_importer_class_init (LrgFbxImporterClass *klass)
{
	LRG_ASSET_IMPORTER_CLASS (klass)->import = fbx_import;
}

static void
lrg_fbx_importer_init (LrgFbxImporter *self)
{
	(void) self;
}

GQuark
lrg_asset_importer_error_quark (void)
{
	return g_quark_from_static_string ("lrg-asset-importer-error");
}

gboolean
lrg_asset_importer_import (LrgAssetImporter  *self,
                           const gchar       *source,
                           const gchar       *dest,
                           LrgAssetMeta      *meta,
                           GError           **error)
{
	LrgAssetImporterClass *klass;

	g_return_val_if_fail (LRG_IS_ASSET_IMPORTER (self), FALSE);
	g_return_val_if_fail (source != NULL && dest != NULL, FALSE);

	klass = LRG_ASSET_IMPORTER_GET_CLASS (self);
	if (klass->import == NULL)
	{
		g_set_error_literal (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		                     "Importer has no import vfunc");
		return FALSE;
	}
	return klass->import (self, source, dest, meta, error);
}

gboolean
lrg_asset_importer_model_loadable (const gchar *path)
{
	g_autofree gchar *lower = NULL;

	if (path == NULL)
		return FALSE;
	lower = g_ascii_strdown (path, -1);
	if (g_str_has_suffix (lower, ".ply") || g_str_has_suffix (lower, ".fbx"))
		return FALSE;
	return g_str_has_suffix (lower, ".gltf") || g_str_has_suffix (lower, ".glb") ||
	       g_str_has_suffix (lower, ".obj") || g_str_has_suffix (lower, ".iqm") ||
	       g_str_has_suffix (lower, ".vox") || g_str_has_suffix (lower, ".m3d");
}

static void
lrg_asset_importer_registry_dispose (GObject *object)
{
	LrgAssetImporterRegistry *self = LRG_ASSET_IMPORTER_REGISTRY (object);

	g_clear_pointer (&self->by_name, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_asset_importer_registry_parent_class)->dispose (object);
}

static void
lrg_asset_importer_registry_class_init (LrgAssetImporterRegistryClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_asset_importer_registry_dispose;
}

static void
registry_add_builtins (LrgAssetImporterRegistry *self)
{
	lrg_asset_importer_registry_register (self, "copy", g_object_new (lrg_copy_importer_get_type (), NULL));
	lrg_asset_importer_registry_register (self, "generic", g_object_new (lrg_copy_importer_get_type (), NULL));
	lrg_asset_importer_registry_register (self, "fbx", g_object_new (lrg_fbx_importer_get_type (), NULL));
}

static void
lrg_asset_importer_registry_init (LrgAssetImporterRegistry *self)
{
	self->by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	registry_add_builtins (self);
}

LrgAssetImporterRegistry *
lrg_asset_importer_registry_new (void)
{
	return g_object_new (LRG_TYPE_ASSET_IMPORTER_REGISTRY, NULL);
}

LrgAssetImporterRegistry *
lrg_asset_importer_registry_get_default (void)
{
	static LrgAssetImporterRegistry *singleton;

	if (g_once_init_enter (&singleton))
	{
		LrgAssetImporterRegistry *created = lrg_asset_importer_registry_new ();

		g_once_init_leave (&singleton, created);
	}
	return singleton;
}

void
lrg_asset_importer_registry_register (LrgAssetImporterRegistry *self,
                                      const gchar              *name,
                                      LrgAssetImporter         *importer)
{
	g_return_if_fail (LRG_IS_ASSET_IMPORTER_REGISTRY (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (LRG_IS_ASSET_IMPORTER (importer));

	g_hash_table_insert (self->by_name, g_strdup (name), importer);
}

LrgAssetImporter *
lrg_asset_importer_registry_lookup (LrgAssetImporterRegistry *self,
                                    const gchar              *name)
{
	g_return_val_if_fail (LRG_IS_ASSET_IMPORTER_REGISTRY (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	return g_hash_table_lookup (self->by_name, name);
}

GStrv
lrg_asset_importer_registry_names (LrgAssetImporterRegistry *self)
{
	GList *keys;
	GList *l;
	GStrv  out;
	guint  i;
	guint  n;

	g_return_val_if_fail (LRG_IS_ASSET_IMPORTER_REGISTRY (self), NULL);
	keys = g_hash_table_get_keys (self->by_name);
	keys = g_list_sort (keys, (GCompareFunc) strcmp);
	n = g_list_length (keys);
	out = g_new0 (gchar *, n + 1);
	i = 0;
	for (l = keys; l != NULL; l = l->next)
		out[i++] = g_strdup ((const gchar *) l->data);
	g_list_free (keys);
	return out;
}

gboolean
lrg_asset_importer_registry_import (LrgAssetImporterRegistry  *self,
                                    const gchar               *name,
                                    const gchar               *source,
                                    const gchar               *dest,
                                    LrgAssetMeta              *meta,
                                    GError                   **error)
{
	LrgAssetImporter *importer;

	g_return_val_if_fail (LRG_IS_ASSET_IMPORTER_REGISTRY (self), FALSE);
	importer = lrg_asset_importer_registry_lookup (self, name);
	if (importer == NULL)
	{
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "No importer named '%s'", (name != NULL) ? name : "(null)");
		return FALSE;
	}
	return lrg_asset_importer_import (importer, source, dest, meta, error);
}

gboolean
lrg_asset_importer_registry_reimport_if_changed (LrgAssetImporterRegistry  *self,
                                                 const gchar               *name,
                                                 const gchar               *source,
                                                 const gchar               *dest,
                                                 LrgAssetMeta              *meta,
                                                 gboolean                  *changed,
                                                 GError                   **error)
{
	g_autofree gchar *stamp_path = NULL;
	g_autofree gchar *stored = NULL;
	gchar             mtime_text[64];
	GStatBuf          st;

	g_return_val_if_fail (LRG_IS_ASSET_IMPORTER_REGISTRY (self), FALSE);
	g_return_val_if_fail (source != NULL && dest != NULL, FALSE);
	g_return_val_if_fail (changed != NULL, FALSE);

	if (g_stat (source, &st) != 0)
	{
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "Could not stat '%s'", source);
		return FALSE;
	}
	g_snprintf (mtime_text, sizeof mtime_text, "%" G_GINT64_FORMAT, (gint64) st.st_mtime);
	stamp_path = g_strconcat (dest, ".stamp", NULL);
	if (g_file_test (dest, G_FILE_TEST_IS_REGULAR) &&
	    g_file_get_contents (stamp_path, &stored, NULL, NULL))
	{
		g_strstrip (stored);
		if (g_strcmp0 (stored, mtime_text) == 0)
		{
			*changed = FALSE;
			return TRUE;
		}
	}
	if (!lrg_asset_importer_registry_import (self, name, source, dest, meta, error))
		return FALSE;
	if (!g_file_set_contents (stamp_path, mtime_text, -1, error))
		return FALSE;
	*changed = TRUE;
	return TRUE;
}
