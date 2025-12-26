/* lrg-mod-loader.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod loader system.
 *
 * The mod loader discovers and loads mods from the filesystem.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-mod.h"

G_BEGIN_DECLS

#define LRG_TYPE_MOD_LOADER (lrg_mod_loader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgModLoader, lrg_mod_loader, LRG, MOD_LOADER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_mod_loader_new:
 *
 * Creates a new mod loader.
 *
 * Returns: (transfer full): a new #LrgModLoader
 */
LRG_AVAILABLE_IN_ALL
LrgModLoader *     lrg_mod_loader_new            (void);

/* ==========================================================================
 * Search Paths
 * ========================================================================== */

/**
 * lrg_mod_loader_add_search_path:
 * @self: a #LrgModLoader
 * @path: directory path to search for mods
 *
 * Adds a directory to search for mods.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_loader_add_search_path (LrgModLoader *self,
                                                   const gchar  *path);

/**
 * lrg_mod_loader_get_search_paths:
 * @self: a #LrgModLoader
 *
 * Gets the list of search paths.
 *
 * Returns: (transfer none) (element-type utf8): the search paths
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_loader_get_search_paths (LrgModLoader *self);

/**
 * lrg_mod_loader_clear_search_paths:
 * @self: a #LrgModLoader
 *
 * Removes all search paths.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_loader_clear_search_paths (LrgModLoader *self);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_mod_loader_get_manifest_filename:
 * @self: a #LrgModLoader
 *
 * Gets the filename used for mod manifests.
 *
 * Returns: (transfer none): the manifest filename
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_loader_get_manifest_filename (LrgModLoader *self);

/**
 * lrg_mod_loader_set_manifest_filename:
 * @self: a #LrgModLoader
 * @filename: the manifest filename (e.g., "mod.yaml")
 *
 * Sets the filename used for mod manifests.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_loader_set_manifest_filename (LrgModLoader *self,
                                                         const gchar  *filename);

/* ==========================================================================
 * Discovery
 * ========================================================================== */

/**
 * lrg_mod_loader_discover:
 * @self: a #LrgModLoader
 * @error: (nullable): return location for error
 *
 * Discovers mods in all search paths.
 *
 * Returns: (transfer full) (element-type LrgMod): discovered mods
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_loader_discover       (LrgModLoader  *self,
                                                  GError       **error);

/**
 * lrg_mod_loader_discover_at:
 * @self: a #LrgModLoader
 * @path: directory path to search
 * @error: (nullable): return location for error
 *
 * Discovers mods at a specific path.
 *
 * Returns: (transfer full) (element-type LrgMod): discovered mods
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_loader_discover_at    (LrgModLoader  *self,
                                                  const gchar   *path,
                                                  GError       **error);

/**
 * lrg_mod_loader_load_mod:
 * @self: a #LrgModLoader
 * @path: path to mod directory
 * @error: (nullable): return location for error
 *
 * Loads a single mod from a directory.
 *
 * Returns: (transfer full) (nullable): the loaded mod, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMod *           lrg_mod_loader_load_mod       (LrgModLoader  *self,
                                                  const gchar   *path,
                                                  GError       **error);

G_END_DECLS
