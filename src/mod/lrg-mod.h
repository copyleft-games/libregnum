/* lrg-mod.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod representation.
 *
 * A mod represents a single modification package that can add
 * or override game content.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-mod-manifest.h"

G_BEGIN_DECLS

#define LRG_TYPE_MOD (lrg_mod_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMod, lrg_mod, LRG, MOD, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_mod_new:
 * @manifest: the mod manifest
 * @base_path: the mod's base directory path
 *
 * Creates a new mod from a manifest.
 *
 * Returns: (transfer full): a new #LrgMod
 */
LRG_AVAILABLE_IN_ALL
LrgMod *           lrg_mod_new                   (LrgModManifest *manifest,
                                                  const gchar    *base_path);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_mod_get_manifest:
 * @self: a #LrgMod
 *
 * Gets the mod's manifest.
 *
 * Returns: (transfer none): the #LrgModManifest
 */
LRG_AVAILABLE_IN_ALL
LrgModManifest *   lrg_mod_get_manifest          (LrgMod *self);

/**
 * lrg_mod_get_id:
 * @self: a #LrgMod
 *
 * Gets the mod's unique identifier.
 *
 * Returns: (transfer none): the mod ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_get_id                (LrgMod *self);

/**
 * lrg_mod_get_base_path:
 * @self: a #LrgMod
 *
 * Gets the mod's base directory path.
 *
 * Returns: (transfer none): the base path
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_get_base_path         (LrgMod *self);

/**
 * lrg_mod_get_data_path:
 * @self: a #LrgMod
 *
 * Gets the full path to the mod's data directory.
 *
 * Returns: (transfer none) (nullable): the data path
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_get_data_path         (LrgMod *self);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_mod_get_state:
 * @self: a #LrgMod
 *
 * Gets the mod's current state.
 *
 * Returns: the #LrgModState
 */
LRG_AVAILABLE_IN_ALL
LrgModState        lrg_mod_get_state             (LrgMod *self);

/**
 * lrg_mod_is_loaded:
 * @self: a #LrgMod
 *
 * Checks if the mod is fully loaded.
 *
 * Returns: %TRUE if loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_is_loaded             (LrgMod *self);

/**
 * lrg_mod_is_enabled:
 * @self: a #LrgMod
 *
 * Checks if the mod is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_is_enabled            (LrgMod *self);

/**
 * lrg_mod_set_enabled:
 * @self: a #LrgMod
 * @enabled: whether to enable the mod
 *
 * Enables or disables the mod.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_set_enabled           (LrgMod   *self,
                                                  gboolean  enabled);

/**
 * lrg_mod_get_error:
 * @self: a #LrgMod
 *
 * Gets the error if the mod failed to load.
 *
 * Returns: (transfer none) (nullable): the error message
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_get_error             (LrgMod *self);

/* ==========================================================================
 * Loading
 * ========================================================================== */

/**
 * lrg_mod_load:
 * @self: a #LrgMod
 * @error: (nullable): return location for error
 *
 * Loads the mod.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_load                  (LrgMod   *self,
                                                  GError  **error);

/**
 * lrg_mod_unload:
 * @self: a #LrgMod
 *
 * Unloads the mod.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_unload                (LrgMod *self);

/* ==========================================================================
 * Resources
 * ========================================================================== */

/**
 * lrg_mod_resolve_path:
 * @self: a #LrgMod
 * @relative_path: path relative to mod's data directory
 *
 * Resolves a relative path to an absolute path within the mod.
 *
 * Returns: (transfer full) (nullable): the absolute path
 */
LRG_AVAILABLE_IN_ALL
gchar *            lrg_mod_resolve_path          (LrgMod      *self,
                                                  const gchar *relative_path);

/**
 * lrg_mod_list_files:
 * @self: a #LrgMod
 * @subdir: (nullable): subdirectory to list, or %NULL for root
 * @pattern: (nullable): glob pattern, or %NULL for all files
 *
 * Lists files in the mod's data directory.
 *
 * Returns: (transfer full) (element-type utf8): list of file paths
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_list_files            (LrgMod      *self,
                                                  const gchar *subdir,
                                                  const gchar *pattern);

G_END_DECLS
