/* lrg-vfs-dir-source.h - A directory on disk as an LrgVfsSource
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * #LrgVfsDirSource serves the regular files below a root directory.
 * It can match names ignoring ASCII case, for data authored on
 * case-insensitive file systems (a game's "Data/World/Maps" shipped as
 * "data/world/maps"), and it never serves anything outside its root.
 */

#ifndef LRG_VFS_DIR_SOURCE_H
#define LRG_VFS_DIR_SOURCE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-vfs-source.h"

G_BEGIN_DECLS

#define LRG_TYPE_VFS_DIR_SOURCE (lrg_vfs_dir_source_get_type ())

/**
 * LrgVfsDirSource:
 *
 * A directory tree as an #LrgVfsSource.
 *
 * Lookups resolve one path component at a time.  With
 * #LrgVfsDirSource:case-insensitive set, a component that does not
 * exist with the exact spelling is looked up by scanning its directory
 * for a name equal ignoring ASCII case; when several match (say
 * "Foo" and "FOO", with "foo" asked for) the bytewise-smallest name is
 * used, so the answer does not depend on directory order.  An exact
 * match is always preferred.
 *
 * Containment: ".." never reaches a source (#LrgVfs and the source's
 * own checks reject it), and every resolved file is checked with
 * realpath() to lie inside the root's real path, so a symbolic link
 * pointing outside the root is treated as absent.  Links inside the
 * root are followed for files; listing does not descend into linked
 * directories (which avoids loops), and stops 64 levels deep.
 *
 * The source is immutable after construction and therefore
 * thread-safe.  It reads whole files with g_file_get_contents().
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVfsDirSource, lrg_vfs_dir_source, LRG, VFS_DIR_SOURCE, GObject)

/**
 * lrg_vfs_dir_source_new:
 * @root: (type filename): the directory to serve
 * @case_insensitive: whether names match ignoring ASCII case
 *
 * The root need not exist yet; until it does the source has no files.
 *
 * Returns: (transfer full): the source
 */
LRG_AVAILABLE_IN_ALL
LrgVfsDirSource *lrg_vfs_dir_source_new (const gchar *root,
                                         gboolean     case_insensitive);

/**
 * lrg_vfs_dir_source_get_root:
 * @self: an #LrgVfsDirSource
 *
 * Returns: (transfer none) (type filename): the root directory, as given
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_vfs_dir_source_get_root (LrgVfsDirSource *self);

/**
 * lrg_vfs_dir_source_get_case_insensitive:
 * @self: an #LrgVfsDirSource
 *
 * Returns: whether names match ignoring ASCII case
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vfs_dir_source_get_case_insensitive (LrgVfsDirSource *self);

/**
 * lrg_vfs_dir_source_resolve:
 * @self: an #LrgVfsDirSource
 * @path: a path inside the root, in any form lrg_vfs_normalize_path()
 *   accepts
 *
 * The file on disk that @path names, applying the source's case rule
 * and containment check.
 *
 * Returns: (transfer full) (nullable) (type filename): the full path of
 *   an existing regular file, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_vfs_dir_source_resolve (LrgVfsDirSource *self,
                                   const gchar     *path);

G_END_DECLS

#endif /* LRG_VFS_DIR_SOURCE_H */
