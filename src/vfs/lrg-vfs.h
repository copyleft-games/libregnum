/* lrg-vfs.h - A layered virtual file system
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * An #LrgVfs stacks #LrgVfsSource<!-- -->s (directories, archives, mod
 * overlays) and answers every lookup from the highest-priority source
 * that has the path, so a patch archive or a mod replaces a base file
 * just by being mounted above it.  Paths are normalized once, here:
 * '\\' becomes '/', leading and repeated slashes and "." components
 * are dropped, and ".." or an embedded NUL is rejected with
 * %LRG_VFS_ERROR_INVALID_PATH, so no source ever sees a path that
 * climbs out of it.
 */

#ifndef LRG_VFS_H
#define LRG_VFS_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-vfs-source.h"

G_BEGIN_DECLS

#define LRG_TYPE_VFS (lrg_vfs_get_type ())

/**
 * LrgVfs:
 *
 * A stack of mounted sources.  Mounts are ordered by descending
 * priority; among equal priorities the source added most recently
 * comes first (so a later mount overrides an earlier one, as with
 * overlays).  Each mount has an id (never 0, never reused by the same
 * VFS) and an optional owner tag, so all of a mod's mounts can be
 * removed at once with lrg_vfs_remove_owner().
 *
 * With #LrgVfs:case-insensitive set, paths compare ignoring ASCII case:
 * listings are filtered, de-duplicated and sorted on ASCII-folded
 * paths.  Lookups pass the path as spelled to each source, which
 * matches it its own way, so such a VFS should be given
 * case-insensitive sources.
 *
 * Thread safety: the mount list is guarded by a mutex, so mounting,
 * unmounting and lookups may happen from several threads.  A lookup
 * works on a snapshot of the mounts taken when it starts; calls into
 * sources happen outside the lock, so sources shared between threads
 * must be thread-safe themselves.  Signals are emitted, outside the
 * lock, in the thread that changed the mounts.
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVfs, lrg_vfs, LRG, VFS, GObject)

/**
 * lrg_vfs_new:
 * @case_insensitive: whether paths compare ignoring ASCII case
 *
 * Returns: (transfer full): an empty VFS
 */
LRG_AVAILABLE_IN_ALL
LrgVfs *lrg_vfs_new (gboolean case_insensitive);

/**
 * lrg_vfs_get_case_insensitive:
 * @self: an #LrgVfs
 *
 * Returns: whether paths compare ignoring ASCII case
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_get_case_insensitive (LrgVfs *self);

/**
 * lrg_vfs_normalize_path:
 * @path: a path
 * @length: the length of @path in bytes, or -1 when it is NUL-terminated
 * @error: return location for a #GError
 *
 * The canonical form of @path: '\\' becomes '/', leading, trailing and
 * repeated slashes and "." components are dropped.  Case is kept.  A
 * ".." component, or a NUL inside the first @length bytes, is
 * %LRG_VFS_ERROR_INVALID_PATH.  The result may be "" (the root), which
 * names no file.  Bytes are not otherwise validated (archive listings
 * are not always UTF-8).
 *
 * Returns: (transfer full) (nullable): the normalized path
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_vfs_normalize_path (const gchar  *path,
                               gssize        length,
                               GError      **error);

/**
 * lrg_vfs_add_source:
 * @self: an #LrgVfs
 * @source: (transfer none): the source to mount (referenced)
 * @priority: its priority; higher wins
 * @owner_tag: (nullable): a tag for lrg_vfs_remove_owner(), such as
 *   "mod:example"
 *
 * Mounts @source and emits #LrgVfs::source-added.  The same source
 * may be mounted more than once.
 *
 * Returns: the mount's id, never 0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vfs_add_source (LrgVfs       *self,
                          LrgVfsSource *source,
                          gint          priority,
                          const gchar  *owner_tag);

/**
 * lrg_vfs_remove_source:
 * @self: an #LrgVfs
 * @mount_id: an id returned by lrg_vfs_add_source()
 *
 * Unmounts and emits #LrgVfs::source-removed.
 *
 * Returns: %TRUE when the mount existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_remove_source (LrgVfs *self,
                                guint   mount_id);

/**
 * lrg_vfs_remove_owner:
 * @self: an #LrgVfs
 * @owner_tag: the tag given to lrg_vfs_add_source()
 *
 * Unmounts every source added with @owner_tag (compared exactly),
 * emitting #LrgVfs::source-removed for each, highest priority first.
 *
 * Returns: how many mounts were removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vfs_remove_owner (LrgVfs      *self,
                            const gchar *owner_tag);

/**
 * lrg_vfs_get_n_sources:
 * @self: an #LrgVfs
 *
 * Returns: the number of mounts
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vfs_get_n_sources (LrgVfs *self);

/**
 * lrg_vfs_get_source:
 * @self: an #LrgVfs
 * @mount_id: a mount id
 *
 * Returns: (transfer full) (nullable): the mounted source, or %NULL
 *   when there is no such mount
 */
LRG_AVAILABLE_IN_ALL
LrgVfsSource *lrg_vfs_get_source (LrgVfs *self,
                                  guint   mount_id);

/**
 * lrg_vfs_get_mount_ids:
 * @self: an #LrgVfs
 *
 * Returns: (transfer full) (element-type guint): the mount ids in
 *   lookup order (the first one wins)
 */
LRG_AVAILABLE_IN_ALL
GArray *lrg_vfs_get_mount_ids (LrgVfs *self);

/**
 * lrg_vfs_has_file:
 * @self: an #LrgVfs
 * @path: a path in any accepted form
 *
 * Returns: %TRUE when some source has @path; %FALSE also for an
 *   invalid path
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_has_file (LrgVfs      *self,
                           const gchar *path);

/**
 * lrg_vfs_find_source:
 * @self: an #LrgVfs
 * @path: a path in any accepted form
 * @out_mount_id: (out) (optional): the winning mount's id, or 0
 * @error: return location for a #GError
 *
 * Which mount answers @path: the first, in lookup order, whose source
 * has it.
 *
 * Returns: (transfer full) (nullable): the winning source, or %NULL
 *   with %LRG_VFS_ERROR_NOT_FOUND or %LRG_VFS_ERROR_INVALID_PATH
 */
LRG_AVAILABLE_IN_ALL
LrgVfsSource *lrg_vfs_find_source (LrgVfs       *self,
                                   const gchar  *path,
                                   guint        *out_mount_id,
                                   GError      **error);

/**
 * lrg_vfs_read_file:
 * @self: an #LrgVfs
 * @path: a path in any accepted form
 * @error: return location for a #GError
 *
 * Reads @path from the winning source (lrg_vfs_find_source()).  If
 * that source fails to read it, its error is returned: a lower source
 * is not tried, since silently serving the overridden file would hide
 * a broken override.
 *
 * Returns: (transfer full) (nullable): the contents
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_vfs_read_file (LrgVfs       *self,
                           const gchar  *path,
                           GError      **error);

/**
 * lrg_vfs_get_file_size:
 * @self: an #LrgVfs
 * @path: a path in any accepted form
 * @out_size: (out) (optional): the size in bytes
 * @error: return location for a #GError
 *
 * The size of @path in the winning source
 * (lrg_vfs_source_get_file_size()).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_get_file_size (LrgVfs       *self,
                                const gchar  *path,
                                guint64      *out_size,
                                GError      **error);

/**
 * lrg_vfs_list_files:
 * @self: an #LrgVfs
 * @prefix: (nullable): a directory path in any accepted form; %NULL or
 *   "" lists everything
 * @error: return location for a #GError
 *
 * The files below the directory @prefix (a file named exactly @prefix
 * is not below it), merged across all sources, normalized,
 * de-duplicated (ignoring ASCII case when the VFS does; the spelling of
 * the highest-priority source is kept) and sorted (on the folded path
 * when case-insensitive).  Sources that cannot list
 * (%LRG_VFS_ERROR_NOT_SUPPORTED) contribute nothing; any other source
 * error fails the whole call.
 *
 * Returns: (transfer full) (nullable) (element-type utf8): the paths
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_vfs_list_files (LrgVfs       *self,
                               const gchar  *prefix,
                               GError      **error);

G_END_DECLS

#endif /* LRG_VFS_H */
