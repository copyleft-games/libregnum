/* lrg-asset-refs.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-asset-refs.h"
#include "../core/lrg-asset-meta.h"

#include <glib/gstdio.h>
#include <string.h>

struct _LrgAssetRefs
{
	GObject     parent_instance;
	gchar      *root;
	GHashTable *by_target;   /* relative path -> GPtrArray of referrer paths */
};

G_DEFINE_FINAL_TYPE (LrgAssetRefs, lrg_asset_refs, G_TYPE_OBJECT)

static gboolean
is_text_name (const gchar *name)
{
	g_autofree gchar *lower = NULL;

	lower = g_ascii_strdown (name, -1);
	return g_str_has_suffix (lower, ".yaml") || g_str_has_suffix (lower, ".yml") ||
	       g_str_has_suffix (lower, ".ryaml") || g_str_has_suffix (lower, ".lua") ||
	       g_str_has_suffix (lower, ".py") || g_str_has_suffix (lower, ".js") ||
	       g_str_has_suffix (lower, ".org") || g_str_has_suffix (lower, ".json") ||
	       g_str_has_suffix (lower, ".txt") || g_str_has_suffix (lower, ".c") ||
	       (strrchr (lower, '.') != NULL && ((const gchar *) strrchr (lower, '.'))[1] == 'r');
}

static gboolean
skip_dir (const gchar *name)
{
	return g_str_equal (name, ".git") || g_str_equal (name, ".hg") ||
	       g_str_equal (name, ".svn") || g_str_equal (name, "build");
}

static void
add_ref (LrgAssetRefs *self,
         const gchar  *target,
         const gchar  *referrer)
{
	GPtrArray *list;

	if (g_str_equal (target, referrer))
		return;
	list = g_hash_table_lookup (self->by_target, target);
	if (list == NULL)
	{
		list = g_ptr_array_new_with_free_func (g_free);
		g_hash_table_insert (self->by_target, g_strdup (target), list);
	}
	g_ptr_array_add (list, g_strdup (referrer));
}

static void
note_token (LrgAssetRefs *self,
            const gchar  *referrer,
            const gchar  *token)
{
	g_autofree gchar *abs = NULL;

	if (token[0] == '\0' || g_path_is_absolute (token) || strstr (token, "..") != NULL)
		return;
	if (strchr (token, '/') == NULL && strchr (token, '.') == NULL)
		return;
	abs = g_build_filename (self->root, token, NULL);
	if (g_file_test (abs, G_FILE_TEST_IS_REGULAR))
		add_ref (self, token, referrer);
}

static void
scan_text (LrgAssetRefs *self,
           const gchar  *rel,
           const gchar  *text)
{
	const gchar *p;

	for (p = text; *p != '\0'; p++)
	{
		gchar        quote;
		const gchar *start;
		g_autofree gchar *token = NULL;

		if (*p != '"' && *p != '\'')
			continue;
		quote = *p;
		start = p + 1;
		p++;
		while (*p != '\0' && *p != quote)
		{
			if (*p == '\\' && p[1] != '\0')
				p++;
			p++;
		}
		if (*p != quote)
			break;
		token = g_strndup (start, (gsize) (p - start));
		note_token (self, rel, token);
	}
}

static gboolean
scan_dir (LrgAssetRefs  *self,
          const gchar   *dir,
          const gchar   *rel,
          GError       **error)
{
	GDir        *handle;
	const gchar *name;

	handle = g_dir_open (dir, 0, error);
	if (handle == NULL)
		return FALSE;
	while ((name = g_dir_read_name (handle)) != NULL)
	{
		g_autofree gchar *child = NULL;
		g_autofree gchar *child_rel = NULL;

		if (skip_dir (name))
			continue;
		child = g_build_filename (dir, name, NULL);
		child_rel = (rel[0] == '\0') ? g_strdup (name) : g_strdup_printf ("%s/%s", rel, name);
		if (g_file_test (child, G_FILE_TEST_IS_SYMLINK))
			continue;
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
		{
			if (!scan_dir (self, child, child_rel, error))
			{
				g_dir_close (handle);
				return FALSE;
			}
			continue;
		}
		if (!is_text_name (name))
			continue;
		{
			g_autofree gchar *text = NULL;

			if (!g_file_get_contents (child, &text, NULL, error))
			{
				g_dir_close (handle);
				return FALSE;
			}
			scan_text (self, child_rel, text);
		}
	}
	g_dir_close (handle);
	return TRUE;
}

static gboolean
rel_ok (const gchar  *path,
        GError      **error)
{
	if (path == NULL || path[0] == '\0' || g_path_is_absolute (path) || strstr (path, "..") != NULL)
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "Path '%s' is not relative to the project",
		             (path != NULL) ? path : "(null)");
		return FALSE;
	}
	return TRUE;
}

/* Replace every @from with @to. @from must be non-empty. */
static gchar *
replace_all (const gchar *text,
             const gchar *from,
             const gchar *to)
{
	GString *out;
	gsize    flen;
	const gchar *p;

	out = g_string_new ("");
	flen = strlen (from);
	p = text;
	while (*p != '\0')
	{
		if (strncmp (p, from, flen) == 0)
		{
			g_string_append (out, to);
			p += flen;
		}
		else
		{
			g_string_append_c (out, *p);
			p++;
		}
	}
	return g_string_free (out, FALSE);
}

static gboolean
rewrite_file (const gchar  *abs,
              const gchar  *old_path,
              const gchar  *new_path,
              GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *quoted_old = NULL;
	g_autofree gchar *quoted_new = NULL;
	g_autofree gchar *squoted_old = NULL;
	g_autofree gchar *squoted_new = NULL;
	g_autofree gchar *step = NULL;
	gchar *updated;

	if (!g_file_get_contents (abs, &text, NULL, error))
		return FALSE;
	quoted_old = g_strdup_printf ("\"%s\"", old_path);
	quoted_new = g_strdup_printf ("\"%s\"", new_path);
	squoted_old = g_strdup_printf ("'%s'", old_path);
	squoted_new = g_strdup_printf ("'%s'", new_path);
	step = replace_all (text, quoted_old, quoted_new);
	updated = replace_all (step, squoted_old, squoted_new);
	if (!g_file_set_contents (abs, updated, -1, error))
	{
		g_free (updated);
		return FALSE;
	}
	g_free (updated);
	return TRUE;
}

static void
lrg_asset_refs_dispose (GObject *object)
{
	LrgAssetRefs *self = LRG_ASSET_REFS (object);

	g_clear_pointer (&self->root, g_free);
	g_clear_pointer (&self->by_target, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_asset_refs_parent_class)->dispose (object);
}

static void
lrg_asset_refs_class_init (LrgAssetRefsClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_asset_refs_dispose;
}

static void
lrg_asset_refs_init (LrgAssetRefs *self)
{
	self->by_target = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                         (GDestroyNotify) g_ptr_array_unref);
}

GQuark
lrg_asset_refs_error_quark (void)
{
	return g_quark_from_static_string ("lrg-asset-refs-error");
}

LrgAssetRefs *
lrg_asset_refs_new (void)
{
	return g_object_new (LRG_TYPE_ASSET_REFS, NULL);
}

gboolean
lrg_asset_refs_scan (LrgAssetRefs  *self,
                     const gchar   *root,
                     GError       **error)
{
	gchar *copy;

	g_return_val_if_fail (LRG_IS_ASSET_REFS (self), FALSE);
	g_return_val_if_fail (root != NULL, FALSE);

	/* rename() rescans with self->root. Copy first so that free is safe. */
	copy = g_strdup (root);
	g_hash_table_remove_all (self->by_target);
	g_free (self->root);
	self->root = copy;
	return scan_dir (self, self->root, "", error);
}

GStrv
lrg_asset_refs_get_referrers (LrgAssetRefs *self,
                              const gchar  *path)
{
	GPtrArray *list;
	GStrv      out;
	guint      i;

	g_return_val_if_fail (LRG_IS_ASSET_REFS (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	list = g_hash_table_lookup (self->by_target, path);
	if (list == NULL)
		return g_new0 (gchar *, 1);
	out = g_new0 (gchar *, list->len + 1);
	for (i = 0; i < list->len; i++)
		out[i] = g_strdup ((const gchar *) g_ptr_array_index (list, i));
	return out;
}

gboolean
lrg_asset_refs_rename (LrgAssetRefs  *self,
                       const gchar   *old_path,
                       const gchar   *new_path,
                       GError       **error)
{
	g_auto(GStrv) referrers = NULL;
	g_autofree gchar *old_abs = NULL;
	g_autofree gchar *new_abs = NULL;
	g_autofree gchar *old_meta = NULL;
	g_autofree gchar *new_meta = NULL;
	g_autofree gchar *parent = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_ASSET_REFS (self), FALSE);
	if (self->root == NULL)
	{
		g_set_error_literal (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		                     "Scan a project before renaming");
		return FALSE;
	}
	if (!rel_ok (old_path, error) || !rel_ok (new_path, error))
		return FALSE;
	old_abs = g_build_filename (self->root, old_path, NULL);
	new_abs = g_build_filename (self->root, new_path, NULL);
	if (!g_file_test (old_abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "No such file '%s'", old_path);
		return FALSE;
	}
	if (g_file_test (new_abs, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_EXISTS,
		             "'%s' already exists", new_path);
		return FALSE;
	}
	referrers = lrg_asset_refs_get_referrers (self, old_path);
	for (i = 0; referrers[i] != NULL; i++)
	{
		g_autofree gchar *abs = g_build_filename (self->root, referrers[i], NULL);

		if (!rewrite_file (abs, old_path, new_path, error))
			return FALSE;
	}
	parent = g_path_get_dirname (new_abs);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "Could not create '%s'", parent);
		return FALSE;
	}
	if (g_rename (old_abs, new_abs) != 0)
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "Could not move '%s'", old_path);
		return FALSE;
	}
	old_meta = lrg_asset_meta_path_for_asset (old_abs);
	new_meta = g_strconcat (new_abs, ".meta", NULL);
	if (g_file_test (old_meta, G_FILE_TEST_IS_REGULAR))
		g_rename (old_meta, new_meta);
	return lrg_asset_refs_scan (self, self->root, error);
}

gboolean
lrg_asset_refs_delete (LrgAssetRefs  *self,
                       const gchar   *path,
                       GError       **error)
{
	g_auto(GStrv) referrers = NULL;
	g_autofree gchar *abs = NULL;
	g_autofree gchar *meta = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_REFS (self), FALSE);
	if (self->root == NULL)
	{
		g_set_error_literal (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		                     "Scan a project before deleting");
		return FALSE;
	}
	if (!rel_ok (path, error))
		return FALSE;
	referrers = lrg_asset_refs_get_referrers (self, path);
	if (referrers[0] != NULL)
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_IN_USE,
		             "'%s' is still quoted by '%s'", path, referrers[0]);
		return FALSE;
	}
	abs = g_build_filename (self->root, path, NULL);
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "No such file '%s'", path);
		return FALSE;
	}
	if (g_remove (abs) != 0)
	{
		g_set_error (error, LRG_ASSET_REFS_ERROR, LRG_ASSET_REFS_ERROR_FAILED,
		             "Could not delete '%s'", path);
		return FALSE;
	}
	meta = lrg_asset_meta_path_for_asset (abs);
	if (g_file_test (meta, G_FILE_TEST_IS_REGULAR))
		g_remove (meta);
	g_hash_table_remove (self->by_target, path);
	return TRUE;
}
