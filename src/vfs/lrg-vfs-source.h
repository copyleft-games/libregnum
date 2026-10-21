/* lrg-vfs-source.h - Something an LrgVfs reads files from
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * An #LrgVfsSource is one layer of a virtual file system: a directory
 * on disk (#LrgVfsDirSource), an archive, a mod's overlay, an in-memory
 * table.  It answers "do you have this path?" and "give me its bytes";
 * listing and sizes are optional.  Sources are mounted in an #LrgVfs,
 * which decides which source wins for a path.
 */

#ifndef LRG_VFS_SOURCE_H
#define LRG_VFS_SOURCE_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_VFS_ERROR:
 *
 * Error domain for #LrgVfs and #LrgVfsSource.  Errors carry an
 * #LrgVfsError code.
 */
#define LRG_VFS_ERROR (lrg_vfs_error_quark ())

/**
 * LrgVfsError:
 * @LRG_VFS_ERROR_NOT_FOUND: no source has the path
 * @LRG_VFS_ERROR_INVALID_PATH: the path is empty, contains a ".."
 *   component or an embedded NUL
 * @LRG_VFS_ERROR_IO: the source has the file but could not read it
 * @LRG_VFS_ERROR_NOT_SUPPORTED: the source does not implement the
 *   operation (listing, for example)
 *
 * Error codes for the %LRG_VFS_ERROR domain.
 */
typedef enum
{
    LRG_VFS_ERROR_NOT_FOUND,
    LRG_VFS_ERROR_INVALID_PATH,
    LRG_VFS_ERROR_IO,
    LRG_VFS_ERROR_NOT_SUPPORTED
} LrgVfsError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_vfs_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_vfs_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_VFS_ERROR (lrg_vfs_error_get_type ())

#define LRG_TYPE_VFS_SOURCE (lrg_vfs_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgVfsSource, lrg_vfs_source, LRG, VFS_SOURCE, GObject)

/**
 * LrgVfsSourceInterface:
 * @parent_iface: the parent interface
 * @get_name: a short human-readable name for messages (optional)
 * @has_file: whether the source holds a regular file at a path (required)
 * @read_file: the whole contents of a file (required)
 * @list_files: the files under a directory prefix (optional)
 * @get_file_size: the size of a file without reading it (optional)
 *
 * The virtual functions of an #LrgVfsSource.
 *
 * Paths are '/'-separated and relative, with no leading '/', no empty,
 * "." or ".." components: #LrgVfs normalizes every path
 * (lrg_vfs_normalize_path()) before it reaches a source.  How a source
 * compares names (exactly, or ignoring ASCII case like an MPQ archive)
 * is its own business; a case-insensitive #LrgVfs should be given
 * case-insensitive sources.
 *
 * A source used from several threads at once must be thread-safe
 * itself; #LrgVfs does not serialize calls into sources.
 */
struct _LrgVfsSourceInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgVfsSourceInterface::get_name:
     * @self: an #LrgVfsSource
     *
     * Returns: (transfer none) (nullable): the source's name
     */
    const gchar * (*get_name) (LrgVfsSource *self);

    /**
     * LrgVfsSourceInterface::has_file:
     * @self: an #LrgVfsSource
     * @path: a normalized path
     *
     * Returns: %TRUE when the source holds a regular file at @path
     */
    gboolean (*has_file) (LrgVfsSource *self,
                          const gchar  *path);

    /**
     * LrgVfsSourceInterface::read_file:
     * @self: an #LrgVfsSource
     * @path: a normalized path
     * @error: return location for a #GError
     *
     * Returns: (transfer full) (nullable): the contents, or %NULL with
     *   %LRG_VFS_ERROR_NOT_FOUND or %LRG_VFS_ERROR_IO
     */
    GBytes * (*read_file) (LrgVfsSource  *self,
                           const gchar   *path,
                           GError       **error);

    /**
     * LrgVfsSourceInterface::list_files:
     * @self: an #LrgVfsSource
     * @prefix: a normalized directory path, or "" for every file
     * @error: return location for a #GError
     *
     * Returns: (transfer full) (nullable) (element-type utf8): the
     *   paths of the files below @prefix, in the source's own spelling
     */
    GPtrArray * (*list_files) (LrgVfsSource  *self,
                               const gchar   *prefix,
                               GError       **error);

    /**
     * LrgVfsSourceInterface::get_file_size:
     * @self: an #LrgVfsSource
     * @path: a normalized path
     * @out_size: (out): the size in bytes
     * @error: return location for a #GError
     *
     * Returns: %TRUE on success
     */
    gboolean (*get_file_size) (LrgVfsSource  *self,
                               const gchar   *path,
                               guint64       *out_size,
                               GError       **error);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_vfs_source_get_name:
 * @self: an #LrgVfsSource
 *
 * A name for log and error messages: the vfunc's answer, or the type
 * name when the source has none.
 *
 * Returns: (transfer none): the source's name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_vfs_source_get_name (LrgVfsSource *self);

/**
 * lrg_vfs_source_has_file:
 * @self: an #LrgVfsSource
 * @path: a normalized path
 *
 * Returns: %TRUE when the source holds a regular file at @path; %FALSE
 *   when it does not, or does not implement the vfunc
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_source_has_file (LrgVfsSource *self,
                                  const gchar  *path);

/**
 * lrg_vfs_source_read_file:
 * @self: an #LrgVfsSource
 * @path: a normalized path
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the file's contents, or %NULL
 *   with an %LRG_VFS_ERROR (%LRG_VFS_ERROR_NOT_SUPPORTED when the
 *   source lacks the vfunc)
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_vfs_source_read_file (LrgVfsSource  *self,
                                  const gchar   *path,
                                  GError       **error);

/**
 * lrg_vfs_source_list_files:
 * @self: an #LrgVfsSource
 * @prefix: (nullable): a normalized directory path; %NULL or "" lists
 *   every file
 * @error: return location for a #GError
 *
 * A source without a list_files vfunc fails with
 * %LRG_VFS_ERROR_NOT_SUPPORTED; it never pretends to be empty.
 *
 * Returns: (transfer full) (nullable) (element-type utf8): the paths
 *   of the files below @prefix, unsorted
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_vfs_source_list_files (LrgVfsSource  *self,
                                      const gchar   *prefix,
                                      GError       **error);

/**
 * lrg_vfs_source_get_file_size:
 * @self: an #LrgVfsSource
 * @path: a normalized path
 * @out_size: (out) (optional): the size in bytes
 * @error: return location for a #GError
 *
 * A source without a get_file_size vfunc is answered by reading the
 * whole file and measuring it, which is correct but as expensive as
 * the read; sources that can do better (a stat(), an archive's table)
 * implement the vfunc.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_source_get_file_size (LrgVfsSource  *self,
                                       const gchar   *path,
                                       guint64       *out_size,
                                       GError       **error);

G_END_DECLS

#endif /* LRG_VFS_SOURCE_H */
