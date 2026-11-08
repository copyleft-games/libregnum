/* lrg-level-merge.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Three-way merge of levels by node GUID. A node changed on only one
 * side is taken from that side. A node changed on both sides to
 * different states is left as ours and reported. The result is a new
 * level; the inputs are not modified.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_level_merge:
 * @base: the common ancestor
 * @ours: our level
 * @theirs: their level
 * @conflicts: (out) (transfer full) (array zero-terminated=1) (optional):
 *   GUIDs that changed on both sides
 * @error: (nullable): return location for a #GError
 *
 * Returns: (transfer full) (nullable): the merged level, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_merge (LrgLevel  *base,
                            LrgLevel  *ours,
                            LrgLevel  *theirs,
                            GStrv     *conflicts,
                            GError   **error);

/**
 * lrg_level_merge_files:
 * @base_path: (type filename): the common ancestor `.rlevel`
 * @ours_path: (type filename): our `.rlevel`
 * @theirs_path: (type filename): their `.rlevel`
 * @out_path: (type filename): where to write the merge
 * @conflicts: (out) (transfer full) (array zero-terminated=1) (optional):
 *   GUIDs that changed on both sides
 * @error: (nullable): return location for a #GError
 *
 * Loads the three files, merges by GUID and writes @out_path. A conflict
 * keeps our node and is listed; that is still a successful write.
 *
 * Returns: %TRUE when @out_path was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_merge_files (const gchar  *base_path,
                                const gchar  *ours_path,
                                const gchar  *theirs_path,
                                const gchar  *out_path,
                                GStrv        *conflicts,
                                GError      **error);

/**
 * lrg_level_merge_install_driver:
 * @repo: (type filename): a git working tree
 * @driver_command: (nullable): the driver command, or %NULL for
 *   `lrgeditor --merge %O %A %B`
 * @error: (nullable): return location for a #GError
 *
 * Sets `merge.rlevel.driver` and appends `*.rlevel merge=rlevel` to the
 * work tree's `.gitattributes` when that line is absent.
 *
 * Returns: %TRUE when the driver is installed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_merge_install_driver (const gchar  *repo,
                                         const gchar  *driver_command,
                                         GError      **error);

G_END_DECLS
