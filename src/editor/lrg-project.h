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
#include "../physics/lrg-physics-layers.h"

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

/**
 * lrg_project_get_run_command:
 * @self: an #LrgProject
 *
 * Returns: (transfer none) (nullable): the command the editor's "Run Game"
 *   launches, split shell-style with `{project}`, `{level}` and `{module}`
 *   substituted (default when unset: `lrgldr {module} -- --level {level}`)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_run_command (LrgProject *self);

/**
 * lrg_project_set_run_command:
 * @self: an #LrgProject
 * @command: (nullable): the run command template
 */
LRG_AVAILABLE_IN_ALL
void lrg_project_set_run_command (LrgProject  *self,
                                  const gchar *command);

/**
 * lrg_project_get_scripts_dir:
 * @self: an #LrgProject
 *
 * Returns: (transfer none): where new scripts are created, relative to the
 *   root (default "scripts")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_scripts_dir (LrgProject *self);

/**
 * lrg_project_set_scripts_dir:
 * @self: an #LrgProject
 * @dir: (nullable): the scripts directory, relative to the root
 */
LRG_AVAILABLE_IN_ALL
void lrg_project_set_scripts_dir (LrgProject  *self,
                                  const gchar *dir);

/**
 * lrg_project_clear_asset_dirs:
 * @self: an #LrgProject
 *
 * Removes every asset directory (the project settings dialog rebuilds the
 * list).
 */
LRG_AVAILABLE_IN_ALL
void lrg_project_clear_asset_dirs (LrgProject *self);

/**
 * lrg_project_resolve:
 * @self: an #LrgProject
 * @path: a path relative to the project root, or absolute
 *
 * Returns: (transfer full): the absolute path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_project_resolve (LrgProject  *self,
                             const gchar *path);

/**
 * lrg_project_relativize:
 * @self: an #LrgProject
 * @path: an absolute path
 *
 * Returns: (transfer full): @path relative to the project root when it is
 *   inside it, otherwise a copy of @path (asset references are stored
 *   relative so projects can move)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_project_relativize (LrgProject  *self,
                                const gchar *path);

/**
 * lrg_project_get_physics_layers:
 * @self: an #LrgProject
 *
 * The project's collision layers and collide matrix, read from the
 * root `physics:` block of project.ryaml (a `physics:` inside
 * `project:` is read too). A new project starts with the default layer
 * set (lrg_physics_layers_reset_default()); an opened project without a
 * block has no named layers and everything collides. Edit the returned
 * object in place; lrg_project_save() writes it back, keeping any other
 * keys of the block.
 *
 * Returns: (transfer none): the layer table
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsLayers * lrg_project_get_physics_layers (LrgProject *self);

/**
 * lrg_project_get_physics_settings:
 * @self: an #LrgProject
 *
 * The project's physics world settings (gravity, fixed substep and cap,
 * solver iterations, sleep), read from the same `physics:` block as
 * lrg_project_get_physics_layers(). Missing keys, a missing block and a
 * new project give the defaults (lrg_physics_settings_init_default()); a
 * broken block is reported with g_warning() and reads as the defaults.
 * Edit the fields in place (or use lrg_project_set_physics_settings());
 * lrg_project_save() writes them back next to the layers, keeping every
 * other key of the block. Hand both to lrg_world3d_set_physics_settings()
 * or lrg_dynamics_world_apply_settings() to run a world with them.
 *
 * Returns: (transfer none): the settings, owned by @self
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsSettings * lrg_project_get_physics_settings (LrgProject *self);

/**
 * lrg_project_set_physics_settings:
 * @self: an #LrgProject
 * @settings: the new settings (copied)
 * @error: return location for a #GError
 *
 * Validates @settings (lrg_physics_settings_validate()) and copies them
 * into the project. Call lrg_project_save() to write them.
 *
 * Returns: %TRUE on success; on error the project is unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_project_set_physics_settings (LrgProject                *self,
                                           const LrgPhysicsSettings  *settings,
                                           GError                   **error);

/**
 * lrg_project_get_audio_mixer:
 * @self: an #LrgProject
 *
 * The project's default mixer layout, `audio: { mixer: <path> }` in
 * project.ryaml (read at the root or inside `project:`): a `.rmixer`
 * path relative to the project root.  Games load it with
 * lrg_audio_manager_load_project_mixer(); play-in-editor does the same.
 *
 * Returns: (transfer none) (nullable) (type filename): the path, %NULL
 *   for none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_get_audio_mixer (LrgProject *self);

/**
 * lrg_project_set_audio_mixer:
 * @self: an #LrgProject
 * @path: (nullable) (type filename): a `.rmixer` path relative to the
 *   project root, %NULL (or empty) for none
 *
 * Sets the default mixer; lrg_project_save() writes it to the root
 * `audio:` block, keeping the block's other keys.
 */
LRG_AVAILABLE_IN_ALL
void lrg_project_set_audio_mixer (LrgProject  *self,
                                  const gchar *path);

G_END_DECLS
