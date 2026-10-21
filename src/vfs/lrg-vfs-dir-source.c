/* lrg-vfs-dir-source.c - A directory on disk as an LrgVfsSource
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Every lookup re-normalizes its path (the source may be used without
 * an LrgVfs), resolves it component by component (exact spelling
 * first, then, when case-insensitive, a scan of the directory), and
 * finally checks with realpath() that the file lies inside the root,
 * which is what keeps symbolic links from leaking files outside it.
 */

#include "config.h"
#include "lrg-vfs-dir-source.h"
#include "lrg-vfs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

/**
 * SECTION:lrg-vfs-dir-source
 * @title: LrgVfsDirSource
 * @short_description: A directory on disk as an #LrgVfsSource
 *
 * See lrg-vfs-dir-source.h.
 */

/* how deep lrg_vfs_source_list_files() descends below the prefix */
#define LRG_VFS_DIR_SOURCE_MAX_DEPTH (64)

struct _LrgVfsDirSource
{
    GObject   parent_instance;

    gchar    *root;              /* as given */
    gchar    *real_root;         /* realpath() of root at construction, or NULL */
    gboolean  case_insensitive;
};

static void lrg_vfs_dir_source_iface_init (LrgVfsSourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgVfsDirSource, lrg_vfs_dir_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_VFS_SOURCE,
                                                      lrg_vfs_dir_source_iface_init))

enum
{
    PROP_0,
    PROP_ROOT,
    PROP_CASE_INSENSITIVE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ------------------------------------------------------------------ */
/* resolution                                                         */
/* ------------------------------------------------------------------ */

/*
 * is_inside_root:
 * TRUE when @full, with every symbolic link resolved, is the root or
 * lies below it.  A root that did not exist at construction is
 * resolved now (the source stays immutable: nothing is cached).
 */
static gboolean
is_inside_root (LrgVfsDirSource *self,
                const gchar     *full)
{
    g_autofree gchar *late_root = NULL;
    g_autofree gchar *real = NULL;
    const gchar *root;
    gsize root_len;

    root = self->real_root;
    if (root == NULL)
    {
        late_root = realpath (self->root, NULL);
        if (late_root == NULL)
            return FALSE;
        root = late_root;
    }

    real = realpath (full, NULL);
    if (real == NULL)
        return FALSE;

    root_len = strlen (root);
    if (strncmp (real, root, root_len) != 0)
        return FALSE;
    /* "/" as the root contains everything */
    if (root_len > 0 && root[root_len - 1] == '/')
        return TRUE;
    return real[root_len] == '\0' || real[root_len] == '/';
}

/*
 * find_entry_ci:
 * The bytewise-smallest entry of @dir equal to @name ignoring ASCII
 * case, or NULL.  Taking the smallest keeps the answer independent of
 * the order readdir() returns.
 */
static gchar *
find_entry_ci (const gchar *dir,
               const gchar *name)
{
    g_autoptr(GDir) handle = NULL;
    const gchar *entry;
    gchar *best = NULL;

    handle = g_dir_open (dir, 0, NULL);
    if (handle == NULL)
        return NULL;
    while ((entry = g_dir_read_name (handle)) != NULL)
    {
        if (g_ascii_strcasecmp (entry, name) != 0)
            continue;
        if (best == NULL || strcmp (entry, best) < 0)
        {
            g_free (best);
            best = g_strdup (entry);
        }
    }
    return best;
}

/*
 * resolve_components:
 * Walks the normalized, non-empty @norm from the root one component at
 * a time and returns the full path on disk (which may not exist when
 * the source is case-sensitive).  @out_relative, when not NULL,
 * receives the path relative to the root in on-disk spelling.  No
 * containment check here; callers do that on the result.
 */
static gchar *
resolve_components (LrgVfsDirSource  *self,
                    const gchar      *norm,
                    gchar           **out_relative)
{
    g_auto(GStrv) parts = NULL;
    g_autoptr(GString) relative = NULL;
    g_autofree gchar *current = NULL;
    guint i;

    parts = g_strsplit (norm, "/", -1);
    relative = g_string_new (NULL);
    current = g_strdup (self->root);

    for (i = 0; parts[i] != NULL; i++)
    {
        g_autofree gchar *candidate = NULL;
        g_autofree gchar *spelled = NULL;

        /* defence in depth: normalized paths have none of these */
        if (*parts[i] == '\0' || strcmp (parts[i], ".") == 0 || strcmp (parts[i], "..") == 0)
            return NULL;

        candidate = g_build_filename (current, parts[i], NULL);
        if (g_file_test (candidate, G_FILE_TEST_EXISTS))
        {
            spelled = g_strdup (parts[i]);
        }
        else if (self->case_insensitive)
        {
            spelled = find_entry_ci (current, parts[i]);
            if (spelled == NULL)
                return NULL;
            g_free (candidate);
            candidate = g_build_filename (current, spelled, NULL);
        }
        else
        {
            return NULL;
        }

        if (relative->len > 0)
            g_string_append_c (relative, '/');
        g_string_append (relative, spelled);
        g_free (current);
        current = g_steal_pointer (&candidate);
    }

    if (out_relative != NULL)
        *out_relative = g_string_free (g_steal_pointer (&relative), FALSE);
    return g_steal_pointer (&current);
}

gchar *
lrg_vfs_dir_source_resolve (LrgVfsDirSource *self,
                            const gchar     *path)
{
    g_autofree gchar *norm = NULL;
    g_autofree gchar *full = NULL;

    g_return_val_if_fail (LRG_IS_VFS_DIR_SOURCE (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    norm = lrg_vfs_normalize_path (path, -1, NULL);
    if (norm == NULL || *norm == '\0')
        return NULL;

    full = resolve_components (self, norm, NULL);
    if (full == NULL)
        return NULL;
    if (!g_file_test (full, G_FILE_TEST_IS_REGULAR) || !is_inside_root (self, full))
        return NULL;
    return g_steal_pointer (&full);
}

/* ------------------------------------------------------------------ */
/* LrgVfsSource                                                       */
/* ------------------------------------------------------------------ */

static const gchar *
dir_get_name (LrgVfsSource *source)
{
    return LRG_VFS_DIR_SOURCE (source)->root;
}

static gboolean
dir_has_file (LrgVfsSource *source,
              const gchar  *path)
{
    g_autofree gchar *full = NULL;

    full = lrg_vfs_dir_source_resolve (LRG_VFS_DIR_SOURCE (source), path);
    return full != NULL;
}

static GBytes *
dir_read_file (LrgVfsSource  *source,
               const gchar   *path,
               GError       **error)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (source);
    g_autofree gchar *full = NULL;
    g_autoptr(GError) local_error = NULL;
    gchar *contents = NULL;
    gsize length = 0;

    full = lrg_vfs_dir_source_resolve (self, path);
    if (full == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND,
                     "\"%s\" is not a file inside %s", path, self->root);
        return NULL;
    }

    if (!g_file_get_contents (full, &contents, &length, &local_error))
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO,
                     "reading %s: %s", full, local_error->message);
        return NULL;
    }
    return g_bytes_new_take (contents, length);
}

static gboolean
dir_get_file_size (LrgVfsSource  *source,
                   const gchar   *path,
                   guint64       *out_size,
                   GError       **error)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (source);
    g_autofree gchar *full = NULL;
    GStatBuf st;

    full = lrg_vfs_dir_source_resolve (self, path);
    if (full == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND,
                     "\"%s\" is not a file inside %s", path, self->root);
        return FALSE;
    }
    if (g_stat (full, &st) != 0)
    {
        int saved = errno;

        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO,
                     "stat %s: %s", full, g_strerror (saved));
        return FALSE;
    }
    *out_size = (guint64) st.st_size;
    return TRUE;
}

/*
 * walk:
 * Appends the relative path of every regular file below @dir (whose
 * relative path is @relative, "" for the root) to @files.  Linked
 * directories are not entered; linked files are listed only when they
 * resolve inside the root.
 */
static gboolean
walk (LrgVfsDirSource  *self,
      const gchar      *dir,
      const gchar      *relative,
      guint             depth,
      GPtrArray        *files,
      GError          **error)
{
    g_autoptr(GDir) handle = NULL;
    g_autoptr(GError) local_error = NULL;
    const gchar *entry;

    if (depth > LRG_VFS_DIR_SOURCE_MAX_DEPTH)
        return TRUE;

    handle = g_dir_open (dir, 0, &local_error);
    if (handle == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO,
                     "listing %s: %s", dir, local_error->message);
        return FALSE;
    }

    while ((entry = g_dir_read_name (handle)) != NULL)
    {
        g_autofree gchar *full = NULL;
        g_autofree gchar *child = NULL;
        GStatBuf st;

        full = g_build_filename (dir, entry, NULL);
        child = *relative != '\0' ? g_strconcat (relative, "/", entry, NULL) : g_strdup (entry);
        if (g_lstat (full, &st) != 0)
            continue;   /* vanished while listing */

        if (S_ISDIR (st.st_mode))
        {
            if (!walk (self, full, child, depth + 1, files, error))
                return FALSE;
        }
        else if (S_ISREG (st.st_mode))
        {
            g_ptr_array_add (files, g_steal_pointer (&child));
        }
        else if (S_ISLNK (st.st_mode))
        {
            if (g_file_test (full, G_FILE_TEST_IS_REGULAR) && is_inside_root (self, full))
                g_ptr_array_add (files, g_steal_pointer (&child));
        }
    }
    return TRUE;
}

static GPtrArray *
dir_list_files (LrgVfsSource  *source,
                const gchar   *prefix,
                GError       **error)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (source);
    g_autoptr(GPtrArray) files = NULL;
    g_autofree gchar *norm = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *relative = NULL;

    norm = lrg_vfs_normalize_path (prefix, -1, error);
    if (norm == NULL)
        return NULL;

    files = g_ptr_array_new_with_free_func (g_free);

    /* find the prefix directory; a missing one simply has no files */
    if (*norm == '\0')
    {
        dir = g_strdup (self->root);
        relative = g_strdup ("");
    }
    else
    {
        dir = resolve_components (self, norm, &relative);
    }
    if (dir == NULL || !g_file_test (dir, G_FILE_TEST_IS_DIR) || !is_inside_root (self, dir))
        return g_steal_pointer (&files);

    if (!walk (self, dir, relative, 0, files, error))
        return NULL;
    return g_steal_pointer (&files);
}

static void
lrg_vfs_dir_source_iface_init (LrgVfsSourceInterface *iface)
{
    iface->get_name = dir_get_name;
    iface->has_file = dir_has_file;
    iface->read_file = dir_read_file;
    iface->list_files = dir_list_files;
    iface->get_file_size = dir_get_file_size;
}

/* ------------------------------------------------------------------ */
/* GObject                                                            */
/* ------------------------------------------------------------------ */

static void
lrg_vfs_dir_source_constructed (GObject *object)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (object);

    G_OBJECT_CLASS (lrg_vfs_dir_source_parent_class)->constructed (object);

    if (self->root == NULL)
        self->root = g_strdup (".");
    self->real_root = realpath (self->root, NULL);
}

static void
lrg_vfs_dir_source_finalize (GObject *object)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (object);

    g_free (self->root);
    free (self->real_root);   /* from realpath() */

    G_OBJECT_CLASS (lrg_vfs_dir_source_parent_class)->finalize (object);
}

static void
lrg_vfs_dir_source_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (object);

    switch (prop_id)
    {
    case PROP_ROOT:
        g_value_set_string (value, self->root);
        break;
    case PROP_CASE_INSENSITIVE:
        g_value_set_boolean (value, self->case_insensitive);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_vfs_dir_source_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgVfsDirSource *self = LRG_VFS_DIR_SOURCE (object);

    switch (prop_id)
    {
    case PROP_ROOT:
        g_free (self->root);
        self->root = g_value_dup_string (value);
        break;
    case PROP_CASE_INSENSITIVE:
        self->case_insensitive = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_vfs_dir_source_class_init (LrgVfsDirSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_vfs_dir_source_constructed;
    object_class->finalize = lrg_vfs_dir_source_finalize;
    object_class->get_property = lrg_vfs_dir_source_get_property;
    object_class->set_property = lrg_vfs_dir_source_set_property;

    /**
     * LrgVfsDirSource:root:
     *
     * The directory served.  Set at construction.
     */
    properties[PROP_ROOT] =
        g_param_spec_string ("root", NULL, NULL, NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgVfsDirSource:case-insensitive:
     *
     * Whether names match ignoring ASCII case.  Set at construction.
     */
    properties[PROP_CASE_INSENSITIVE] =
        g_param_spec_boolean ("case-insensitive", NULL, NULL, FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vfs_dir_source_init (LrgVfsDirSource *self)
{
}

LrgVfsDirSource *
lrg_vfs_dir_source_new (const gchar *root,
                        gboolean     case_insensitive)
{
    g_return_val_if_fail (root != NULL, NULL);

    return g_object_new (LRG_TYPE_VFS_DIR_SOURCE,
                         "root", root,
                         "case-insensitive", case_insensitive,
                         NULL);
}

const gchar *
lrg_vfs_dir_source_get_root (LrgVfsDirSource *self)
{
    g_return_val_if_fail (LRG_IS_VFS_DIR_SOURCE (self), NULL);
    return self->root;
}

gboolean
lrg_vfs_dir_source_get_case_insensitive (LrgVfsDirSource *self)
{
    g_return_val_if_fail (LRG_IS_VFS_DIR_SOURCE (self), FALSE);
    return self->case_insensitive;
}
