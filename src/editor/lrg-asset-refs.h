/* lrg-asset-refs.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Reference scan for safe rename and delete.
 *
 * A scan walks text files under a project root and records every quoted
 * relative path that names a file which exists. Rename rewrites those
 * quotes and moves the file (and its .meta sidecar). Delete refuses while
 * any quote still points at the file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ASSET_REFS (lrg_asset_refs_get_type ())
#define LRG_ASSET_REFS_ERROR (lrg_asset_refs_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetRefs, lrg_asset_refs, LRG, ASSET_REFS, GObject)

/**
 * LrgAssetRefsError:
 * @LRG_ASSET_REFS_ERROR_FAILED: the operation failed
 * @LRG_ASSET_REFS_ERROR_IN_USE: delete was refused because something still quotes the path
 * @LRG_ASSET_REFS_ERROR_EXISTS: the destination already exists
 */
typedef enum
{
	LRG_ASSET_REFS_ERROR_FAILED,
	LRG_ASSET_REFS_ERROR_IN_USE,
	LRG_ASSET_REFS_ERROR_EXISTS
} LrgAssetRefsError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_asset_refs_error_quark (void);

LRG_AVAILABLE_IN_ALL
LrgAssetRefs * lrg_asset_refs_new (void);

/**
 * lrg_asset_refs_scan:
 * @self: a reference index
 * @root: (type filename): project root
 * @error: (nullable): return location for a #GError
 *
 * Replaces the index with quotes found under @root.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_refs_scan (LrgAssetRefs  *self,
                              const gchar   *root,
                              GError       **error);

/**
 * lrg_asset_refs_get_referrers:
 * @self: a reference index
 * @path: a path relative to the scanned root
 *
 * Returns: (transfer full) (array zero-terminated=1): files that quote @path
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_asset_refs_get_referrers (LrgAssetRefs *self,
                                    const gchar  *path);

/**
 * lrg_asset_refs_rename:
 * @self: a reference index
 * @old_path: current path, relative to the scanned root
 * @new_path: new path, relative to the scanned root
 * @error: (nullable): return location for a #GError
 *
 * Rewrites quoted mentions, moves the file and its `.meta` sidecar, then
 * scans again. The root is the one passed to lrg_asset_refs_scan().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_refs_rename (LrgAssetRefs  *self,
                                const gchar   *old_path,
                                const gchar   *new_path,
                                GError       **error);

/**
 * lrg_asset_refs_delete:
 * @self: a reference index
 * @path: path relative to the scanned root
 * @error: (nullable): return location for a #GError
 *
 * Deletes the file and its `.meta` sidecar when nothing quotes @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_refs_delete (LrgAssetRefs  *self,
                                const gchar   *path,
                                GError       **error);

G_END_DECLS
