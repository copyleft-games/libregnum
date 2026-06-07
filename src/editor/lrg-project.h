/* lrg-project.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A libregnum editor project.
 *
 * LrgProject describes a game project on disk: a root directory plus a
 * `project.ryaml` manifest naming the project, its default level, its asset
 * directories, and the game module build output. It can build an
 * #LrgAssetDatabase over its asset directories.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROJECT (lrg_project_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProject, lrg_project, LRG, PROJECT, GObject)

/**
 * LRG_PROJECT_MANIFEST:
 *
 * The manifest file name within a project root.
 */
#define LRG_PROJECT_MANIFEST "project.ryaml"

/**
 * lrg_project_new:
 * @root: the project root directory
 * @name: (nullable): the project name
 *
 * Returns: (transfer full): a new #LrgProject
 */
LRG_AVAILABLE_IN_ALL
LrgProject * lrg_project_new (const gchar *root,
                              const gchar *name);

/**
 * lrg_project_open:
 * @dir: a directory containing a `project.ryaml`
 * @error: (nullable): return location for an error
 *
 * Opens a project by reading its manifest.
 *
 * Returns: (transfer full) (nullable): the project, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProject * lrg_project_open (const gchar  *dir,
                              GError      **error);

/**
 * lrg_project_save:
 * @self: an #LrgProject
 * @error: (nullable): return location for an error
 *
 * Writes the manifest to `<root>/project.ryaml`.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_project_save (LrgProject  *self,
                           GError     **error);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_root (LrgProject *self);
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_name (LrgProject *self);
LRG_AVAILABLE_IN_ALL
void lrg_project_set_name (LrgProject *self, const gchar *name);
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_default_level (LrgProject *self);
LRG_AVAILABLE_IN_ALL
void lrg_project_set_default_level (LrgProject *self, const gchar *level);
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_game_output (LrgProject *self);
LRG_AVAILABLE_IN_ALL
void lrg_project_set_game_output (LrgProject *self, const gchar *output);

/**
 * lrg_project_add_asset_dir:
 * @self: an #LrgProject
 * @dir: an asset directory relative to the project root
 */
LRG_AVAILABLE_IN_ALL
void lrg_project_add_asset_dir (LrgProject  *self,
                                const gchar *dir);

/**
 * lrg_project_get_asset_dirs:
 * @self: an #LrgProject
 *
 * Returns: (transfer none) (element-type utf8): the asset directories
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_project_get_asset_dirs (LrgProject *self);

/**
 * lrg_project_create_asset_database:
 * @self: an #LrgProject
 *
 * Builds an #LrgAssetDatabase whose search directories are the project's asset
 * directories resolved against the root. The caller scans it.
 *
 * Returns: (transfer full): a new #LrgAssetDatabase
 */
LRG_AVAILABLE_IN_ALL
LrgAssetDatabase * lrg_project_create_asset_database (LrgProject *self);

G_END_DECLS
