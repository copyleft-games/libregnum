/* lrg-vfs-source.c - Something an LrgVfs reads files from
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The public wrappers check each vfunc before calling it: a missing
 * required vfunc is reported, a missing optional one is either
 * NOT_SUPPORTED (listing) or emulated (sizes, by reading the file).
 */

#include "config.h"
#include "lrg-vfs-source.h"

/**
 * SECTION:lrg-vfs-source
 * @title: LrgVfsSource
 * @short_description: One layer of an #LrgVfs
 *
 * See lrg-vfs-source.h.
 */

G_DEFINE_QUARK (lrg-vfs-error-quark, lrg_vfs_error)

G_DEFINE_ENUM_TYPE (LrgVfsError, lrg_vfs_error,
    G_DEFINE_ENUM_VALUE (LRG_VFS_ERROR_NOT_FOUND, "not-found"),
    G_DEFINE_ENUM_VALUE (LRG_VFS_ERROR_INVALID_PATH, "invalid-path"),
    G_DEFINE_ENUM_VALUE (LRG_VFS_ERROR_IO, "io"),
    G_DEFINE_ENUM_VALUE (LRG_VFS_ERROR_NOT_SUPPORTED, "not-supported"))

G_DEFINE_INTERFACE (LrgVfsSource, lrg_vfs_source, G_TYPE_OBJECT)

static void
lrg_vfs_source_default_init (LrgVfsSourceInterface *iface)
{
    /* every vfunc defaults to NULL; the wrappers handle absence */
}

const gchar *
lrg_vfs_source_get_name (LrgVfsSource *self)
{
    LrgVfsSourceInterface *iface;
    const gchar *name;

    g_return_val_if_fail (LRG_IS_VFS_SOURCE (self), NULL);

    iface = LRG_VFS_SOURCE_GET_IFACE (self);
    name = iface->get_name != NULL ? iface->get_name (self) : NULL;
    return name != NULL ? name : G_OBJECT_TYPE_NAME (self);
}

gboolean
lrg_vfs_source_has_file (LrgVfsSource *self,
                         const gchar  *path)
{
    LrgVfsSourceInterface *iface;

    g_return_val_if_fail (LRG_IS_VFS_SOURCE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    iface = LRG_VFS_SOURCE_GET_IFACE (self);
    if (iface->has_file == NULL)
    {
        g_warning ("%s does not implement LrgVfsSource.has_file",
                   G_OBJECT_TYPE_NAME (self));
        return FALSE;
    }
    return iface->has_file (self, path);
}

GBytes *
lrg_vfs_source_read_file (LrgVfsSource  *self,
                          const gchar   *path,
                          GError       **error)
{
    LrgVfsSourceInterface *iface;

    g_return_val_if_fail (LRG_IS_VFS_SOURCE (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    iface = LRG_VFS_SOURCE_GET_IFACE (self);
    if (iface->read_file == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_SUPPORTED,
                     "%s cannot read files", lrg_vfs_source_get_name (self));
        return NULL;
    }
    return iface->read_file (self, path, error);
}

GPtrArray *
lrg_vfs_source_list_files (LrgVfsSource  *self,
                           const gchar   *prefix,
                           GError       **error)
{
    LrgVfsSourceInterface *iface;

    g_return_val_if_fail (LRG_IS_VFS_SOURCE (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    iface = LRG_VFS_SOURCE_GET_IFACE (self);
    if (iface->list_files == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_SUPPORTED,
                     "%s cannot list its files", lrg_vfs_source_get_name (self));
        return NULL;
    }
    return iface->list_files (self, prefix != NULL ? prefix : "", error);
}

gboolean
lrg_vfs_source_get_file_size (LrgVfsSource  *self,
                              const gchar   *path,
                              guint64       *out_size,
                              GError       **error)
{
    LrgVfsSourceInterface *iface;
    g_autoptr(GBytes) bytes = NULL;
    guint64 size;

    g_return_val_if_fail (LRG_IS_VFS_SOURCE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    iface = LRG_VFS_SOURCE_GET_IFACE (self);
    if (iface->get_file_size != NULL)
    {
        size = 0;
        if (!iface->get_file_size (self, path, &size, error))
            return FALSE;
    }
    else
    {
        /* no cheaper way: read the file and measure it */
        bytes = lrg_vfs_source_read_file (self, path, error);
        if (bytes == NULL)
            return FALSE;
        size = g_bytes_get_size (bytes);
    }

    if (out_size != NULL)
        *out_size = size;
    return TRUE;
}
