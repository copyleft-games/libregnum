/* lrg-vfs.c - A layered virtual file system
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The mounts live in one array kept in lookup order (descending
 * priority, newest first among equals), so a lookup is a walk from the
 * front.  Mounts are reference-counted boxes: a lookup copies the array
 * of references under the mutex and then talks to the sources without
 * holding it, so a slow source (an archive decompressing a large file)
 * never blocks mounting, and a source unmounted mid-lookup stays alive
 * until the lookup is done with it.
 */

#include "config.h"
#include "lrg-vfs.h"

#include <string.h>

/**
 * SECTION:lrg-vfs
 * @title: LrgVfs
 * @short_description: A layered virtual file system
 *
 * See lrg-vfs.h.
 */

typedef struct
{
    guint         id;
    gint          priority;
    LrgVfsSource *source;
    gchar        *owner_tag;
} Mount;

struct _LrgVfs
{
    GObject    parent_instance;

    gboolean   case_insensitive;
    GMutex     lock;
    GPtrArray *mounts;     /* Mount* (rc boxes), in lookup order */
    guint      next_id;
};

G_DEFINE_FINAL_TYPE (LrgVfs, lrg_vfs, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CASE_INSENSITIVE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_SOURCE_ADDED,
    SIGNAL_SOURCE_REMOVED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ------------------------------------------------------------------ */
/* mounts                                                             */
/* ------------------------------------------------------------------ */

static void
mount_clear (gpointer data)
{
    Mount *mount = data;

    g_clear_object (&mount->source);
    g_clear_pointer (&mount->owner_tag, g_free);
}

static void
mount_release (gpointer data)
{
    g_atomic_rc_box_release_full (data, mount_clear);
}

/*
 * snapshot:
 * A new array holding a reference to every mount, in lookup order.
 * Lookups iterate this instead of the live list, without the lock.
 */
static GPtrArray *
snapshot (LrgVfs *self)
{
    GPtrArray *copy;
    guint i;

    g_mutex_lock (&self->lock);
    copy = g_ptr_array_new_full (self->mounts->len, mount_release);
    for (i = 0; i < self->mounts->len; i++)
        g_ptr_array_add (copy, g_atomic_rc_box_acquire (g_ptr_array_index (self->mounts, i)));
    g_mutex_unlock (&self->lock);
    return copy;
}

/* ------------------------------------------------------------------ */
/* GObject                                                            */
/* ------------------------------------------------------------------ */

static void
lrg_vfs_finalize (GObject *object)
{
    LrgVfs *self = LRG_VFS (object);

    g_ptr_array_unref (self->mounts);
    g_mutex_clear (&self->lock);

    G_OBJECT_CLASS (lrg_vfs_parent_class)->finalize (object);
}

static void
lrg_vfs_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    LrgVfs *self = LRG_VFS (object);

    switch (prop_id)
    {
    case PROP_CASE_INSENSITIVE:
        g_value_set_boolean (value, self->case_insensitive);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_vfs_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    LrgVfs *self = LRG_VFS (object);

    switch (prop_id)
    {
    case PROP_CASE_INSENSITIVE:
        self->case_insensitive = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_vfs_class_init (LrgVfsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_vfs_finalize;
    object_class->get_property = lrg_vfs_get_property;
    object_class->set_property = lrg_vfs_set_property;

    /**
     * LrgVfs:case-insensitive:
     *
     * Whether paths compare ignoring ASCII case (listings are filtered,
     * de-duplicated and sorted on folded paths).  Set at construction.
     */
    properties[PROP_CASE_INSENSITIVE] =
        g_param_spec_boolean ("case-insensitive", NULL, NULL, FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgVfs::source-added:
     * @self: the VFS
     * @source: the mounted source
     * @mount_id: its mount id
     */
    signals[SIGNAL_SOURCE_ADDED] =
        g_signal_new ("source-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                      LRG_TYPE_VFS_SOURCE, G_TYPE_UINT);

    /**
     * LrgVfs::source-removed:
     * @self: the VFS
     * @source: the unmounted source
     * @mount_id: the id it was mounted under
     */
    signals[SIGNAL_SOURCE_REMOVED] =
        g_signal_new ("source-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                      LRG_TYPE_VFS_SOURCE, G_TYPE_UINT);
}

static void
lrg_vfs_init (LrgVfs *self)
{
    g_mutex_init (&self->lock);
    self->mounts = g_ptr_array_new_with_free_func (mount_release);
    self->next_id = 1;
}

LrgVfs *
lrg_vfs_new (gboolean case_insensitive)
{
    return g_object_new (LRG_TYPE_VFS, "case-insensitive", case_insensitive, NULL);
}

gboolean
lrg_vfs_get_case_insensitive (LrgVfs *self)
{
    g_return_val_if_fail (LRG_IS_VFS (self), FALSE);
    return self->case_insensitive;
}

/* ------------------------------------------------------------------ */
/* paths                                                              */
/* ------------------------------------------------------------------ */

gchar *
lrg_vfs_normalize_path (const gchar  *path,
                        gssize        length,
                        GError      **error)
{
    g_autoptr(GString) out = NULL;
    gsize len;
    gsize start;
    gsize i;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    len = length < 0 ? strlen (path) : (gsize) length;
    out = g_string_sized_new (len);

    /* walk the components between separators ('/' or '\\'); i == len
     * closes the last one */
    start = 0;
    for (i = 0; i <= len; i++)
    {
        gsize clen;

        if (i < len && path[i] == '\0')
        {
            g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH,
                         "path contains a NUL byte at offset %" G_GSIZE_FORMAT, i);
            return NULL;
        }
        if (i < len && path[i] != '/' && path[i] != '\\')
            continue;

        clen = i - start;
        if (clen == 0 || (clen == 1 && path[start] == '.'))
        {
            /* empty (leading or repeated slash) or "." */
        }
        else if (clen == 2 && path[start] == '.' && path[start + 1] == '.')
        {
            g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH,
                         "path \"%.*s\" contains a \"..\" component", (int) len, path);
            return NULL;
        }
        else
        {
            if (out->len > 0)
                g_string_append_c (out, '/');
            g_string_append_len (out, path + start, (gssize) clen);
        }
        start = i + 1;
    }
    return g_string_free (g_steal_pointer (&out), FALSE);
}

/*
 * normalize_file_path:
 * lrg_vfs_normalize_path() for a path that must name a file: the root
 * ("" after normalization) is invalid too.
 */
static gchar *
normalize_file_path (const gchar  *path,
                     GError      **error)
{
    g_autofree gchar *norm = NULL;

    norm = lrg_vfs_normalize_path (path, -1, error);
    if (norm == NULL)
        return NULL;
    if (*norm == '\0')
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH,
                     "path \"%s\" names no file", path);
        return NULL;
    }
    return g_steal_pointer (&norm);
}

/* the comparison key of a normalized path */
static gchar *
path_key (LrgVfs      *self,
          const gchar *path)
{
    return self->case_insensitive ? g_ascii_strdown (path, -1) : g_strdup (path);
}

/* ------------------------------------------------------------------ */
/* mounting                                                           */
/* ------------------------------------------------------------------ */

guint
lrg_vfs_add_source (LrgVfs       *self,
                    LrgVfsSource *source,
                    gint          priority,
                    const gchar  *owner_tag)
{
    Mount *mount;
    guint id;
    guint i;

    g_return_val_if_fail (LRG_IS_VFS (self), 0);
    g_return_val_if_fail (LRG_IS_VFS_SOURCE (source), 0);

    mount = g_atomic_rc_box_new0 (Mount);
    mount->priority = priority;
    mount->source = g_object_ref (source);
    mount->owner_tag = g_strdup (owner_tag);

    g_mutex_lock (&self->lock);
    id = self->next_id++;
    mount->id = id;
    /* in front of the first mount it does not lose to: lower priority,
     * or equal priority (the newer mount wins ties) */
    for (i = 0; i < self->mounts->len; i++)
    {
        Mount *other = g_ptr_array_index (self->mounts, i);

        if (other->priority <= priority)
            break;
    }
    g_ptr_array_insert (self->mounts, (gint) i, mount);
    g_mutex_unlock (&self->lock);

    g_signal_emit (self, signals[SIGNAL_SOURCE_ADDED], 0, source, id);
    return id;
}

/*
 * remove_where:
 * Takes every mount matching (@mount_id, or @owner_tag when it is not
 * NULL) out of the list under the lock, then emits source-removed for
 * each outside it.
 */
static guint
remove_where (LrgVfs      *self,
              guint        mount_id,
              const gchar *owner_tag)
{
    g_autoptr(GPtrArray) removed = NULL;
    guint i;

    removed = g_ptr_array_new_with_free_func (mount_release);

    g_mutex_lock (&self->lock);
    i = 0;
    while (i < self->mounts->len)
    {
        Mount *mount = g_ptr_array_index (self->mounts, i);
        gboolean match;

        match = owner_tag != NULL ? g_strcmp0 (mount->owner_tag, owner_tag) == 0
                                  : mount->id == mount_id;
        if (match)
            g_ptr_array_add (removed, g_ptr_array_steal_index (self->mounts, i));
        else
            i++;
    }
    g_mutex_unlock (&self->lock);

    for (i = 0; i < removed->len; i++)
    {
        Mount *mount = g_ptr_array_index (removed, i);

        g_signal_emit (self, signals[SIGNAL_SOURCE_REMOVED], 0, mount->source, mount->id);
    }
    return removed->len;
}

gboolean
lrg_vfs_remove_source (LrgVfs *self,
                       guint   mount_id)
{
    g_return_val_if_fail (LRG_IS_VFS (self), FALSE);

    if (mount_id == 0)
        return FALSE;
    return remove_where (self, mount_id, NULL) > 0;
}

guint
lrg_vfs_remove_owner (LrgVfs      *self,
                      const gchar *owner_tag)
{
    g_return_val_if_fail (LRG_IS_VFS (self), 0);
    g_return_val_if_fail (owner_tag != NULL, 0);

    return remove_where (self, 0, owner_tag);
}

guint
lrg_vfs_get_n_sources (LrgVfs *self)
{
    guint n;

    g_return_val_if_fail (LRG_IS_VFS (self), 0);

    g_mutex_lock (&self->lock);
    n = self->mounts->len;
    g_mutex_unlock (&self->lock);
    return n;
}

LrgVfsSource *
lrg_vfs_get_source (LrgVfs *self,
                    guint   mount_id)
{
    LrgVfsSource *source = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_VFS (self), NULL);

    g_mutex_lock (&self->lock);
    for (i = 0; i < self->mounts->len; i++)
    {
        Mount *mount = g_ptr_array_index (self->mounts, i);

        if (mount->id == mount_id)
        {
            source = g_object_ref (mount->source);
            break;
        }
    }
    g_mutex_unlock (&self->lock);
    return source;
}

GArray *
lrg_vfs_get_mount_ids (LrgVfs *self)
{
    GArray *ids;
    guint i;

    g_return_val_if_fail (LRG_IS_VFS (self), NULL);

    ids = g_array_new (FALSE, FALSE, sizeof (guint));
    g_mutex_lock (&self->lock);
    for (i = 0; i < self->mounts->len; i++)
    {
        Mount *mount = g_ptr_array_index (self->mounts, i);

        g_array_append_val (ids, mount->id);
    }
    g_mutex_unlock (&self->lock);
    return ids;
}

/* ------------------------------------------------------------------ */
/* lookups                                                            */
/* ------------------------------------------------------------------ */

/*
 * find_mount:
 * The first mount (a new reference) whose source has the normalized
 * @norm, or NULL.
 */
static Mount *
find_mount (LrgVfs      *self,
            const gchar *norm)
{
    g_autoptr(GPtrArray) mounts = NULL;
    guint i;

    mounts = snapshot (self);
    for (i = 0; i < mounts->len; i++)
    {
        Mount *mount = g_ptr_array_index (mounts, i);

        if (lrg_vfs_source_has_file (mount->source, norm))
            return g_atomic_rc_box_acquire (mount);
    }
    return NULL;
}

/*
 * find_mount_or_error:
 * Normalizes @path and finds its mount, or sets INVALID_PATH or
 * NOT_FOUND.  On success *out_norm holds the normalized path.
 */
static Mount *
find_mount_or_error (LrgVfs       *self,
                     const gchar  *path,
                     gchar       **out_norm,
                     GError      **error)
{
    g_autofree gchar *norm = NULL;
    Mount *mount;

    norm = normalize_file_path (path, error);
    if (norm == NULL)
        return NULL;

    mount = find_mount (self, norm);
    if (mount == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND,
                     "\"%s\" is in none of the %u mounted sources",
                     norm, lrg_vfs_get_n_sources (self));
        return NULL;
    }
    *out_norm = g_steal_pointer (&norm);
    return mount;
}

gboolean
lrg_vfs_has_file (LrgVfs      *self,
                  const gchar *path)
{
    g_autofree gchar *norm = NULL;
    Mount *mount;

    g_return_val_if_fail (LRG_IS_VFS (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    norm = normalize_file_path (path, NULL);
    if (norm == NULL)
        return FALSE;
    mount = find_mount (self, norm);
    if (mount == NULL)
        return FALSE;
    mount_release (mount);
    return TRUE;
}

LrgVfsSource *
lrg_vfs_find_source (LrgVfs       *self,
                     const gchar  *path,
                     guint        *out_mount_id,
                     GError      **error)
{
    g_autofree gchar *norm = NULL;
    LrgVfsSource *source;
    Mount *mount;

    g_return_val_if_fail (LRG_IS_VFS (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (out_mount_id != NULL)
        *out_mount_id = 0;

    mount = find_mount_or_error (self, path, &norm, error);
    if (mount == NULL)
        return NULL;

    source = g_object_ref (mount->source);
    if (out_mount_id != NULL)
        *out_mount_id = mount->id;
    mount_release (mount);
    return source;
}

GBytes *
lrg_vfs_read_file (LrgVfs       *self,
                   const gchar  *path,
                   GError      **error)
{
    g_autofree gchar *norm = NULL;
    GError *local_error = NULL;
    GBytes *bytes;
    Mount *mount;

    g_return_val_if_fail (LRG_IS_VFS (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    mount = find_mount_or_error (self, path, &norm, error);
    if (mount == NULL)
        return NULL;

    bytes = lrg_vfs_source_read_file (mount->source, norm, &local_error);
    if (bytes == NULL)
    {
        /* say which mount failed: the source's message may not */
        if (local_error != NULL)
            g_prefix_error (&local_error, "%s (mount %u): ",
                            lrg_vfs_source_get_name (mount->source), mount->id);
        else
            g_set_error (&local_error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO,
                         "%s (mount %u) failed to read \"%s\" without saying why",
                         lrg_vfs_source_get_name (mount->source), mount->id, norm);
        g_propagate_error (error, local_error);
    }
    mount_release (mount);
    return bytes;
}

gboolean
lrg_vfs_get_file_size (LrgVfs       *self,
                       const gchar  *path,
                       guint64      *out_size,
                       GError      **error)
{
    g_autofree gchar *norm = NULL;
    gboolean ok;
    Mount *mount;

    g_return_val_if_fail (LRG_IS_VFS (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    mount = find_mount_or_error (self, path, &norm, error);
    if (mount == NULL)
        return FALSE;

    ok = lrg_vfs_source_get_file_size (mount->source, norm, out_size, error);
    mount_release (mount);
    return ok;
}

/* sorts an array of gchar* keys bytewise */
static gint
compare_keys (gconstpointer a,
              gconstpointer b)
{
    return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

GPtrArray *
lrg_vfs_list_files (LrgVfs       *self,
                    const gchar  *prefix,
                    GError      **error)
{
    g_autoptr(GPtrArray) mounts = NULL;
    g_autoptr(GHashTable) seen = NULL;   /* key -> normalized path */
    g_autoptr(GPtrArray) keys = NULL;
    g_autofree gchar *norm_prefix = NULL;
    g_autofree gchar *prefix_key = NULL;
    GPtrArray *result;
    gsize prefix_len;
    guint i;

    g_return_val_if_fail (LRG_IS_VFS (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    norm_prefix = lrg_vfs_normalize_path (prefix != NULL ? prefix : "", -1, error);
    if (norm_prefix == NULL)
        return NULL;
    prefix_key = path_key (self, norm_prefix);
    prefix_len = strlen (prefix_key);

    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    keys = g_ptr_array_new ();

    /* highest priority first, so the first spelling seen is kept */
    mounts = snapshot (self);
    for (i = 0; i < mounts->len; i++)
    {
        Mount *mount = g_ptr_array_index (mounts, i);
        g_autoptr(GPtrArray) files = NULL;
        GError *local_error = NULL;
        guint f;

        files = lrg_vfs_source_list_files (mount->source, norm_prefix, &local_error);
        if (files == NULL)
        {
            if (g_error_matches (local_error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_SUPPORTED))
            {
                g_clear_error (&local_error);
                continue;
            }
            if (local_error != NULL)
            {
                g_propagate_prefixed_error (error, local_error, "%s (mount %u): ",
                                            lrg_vfs_source_get_name (mount->source),
                                            mount->id);
                return NULL;
            }
            continue;
        }

        for (f = 0; f < files->len; f++)
        {
            g_autofree gchar *norm = NULL;
            g_autofree gchar *key = NULL;

            /* sources are not trusted to spell paths canonically; a
             * path that cannot be normalized names nothing reachable */
            norm = lrg_vfs_normalize_path (g_ptr_array_index (files, f), -1, NULL);
            if (norm == NULL || *norm == '\0')
                continue;
            key = path_key (self, norm);

            /* below the prefix directory, not merely sharing a prefix */
            if (prefix_len > 0 &&
                (strncmp (key, prefix_key, prefix_len) != 0 || key[prefix_len] != '/'))
                continue;
            if (g_hash_table_contains (seen, key))
                continue;

            g_ptr_array_add (keys, key);
            g_hash_table_insert (seen, g_steal_pointer (&key), g_steal_pointer (&norm));
        }
    }

    /* sorted on the key: the folded path when case-insensitive */
    g_ptr_array_sort (keys, compare_keys);
    result = g_ptr_array_new_full (keys->len, g_free);
    for (i = 0; i < keys->len; i++)
        g_ptr_array_add (result, g_strdup (g_hash_table_lookup (seen, g_ptr_array_index (keys, i))));
    return result;
}
