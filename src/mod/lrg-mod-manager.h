/* lrg-mod-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod manager system.
 *
 * The mod manager handles mod lifecycle including discovery,
 * dependency resolution, load ordering, and state management.
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
#include "lrg-mod-loader.h"

G_BEGIN_DECLS

#define LRG_TYPE_MOD_MANAGER (lrg_mod_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgModManager, lrg_mod_manager, LRG, MOD_MANAGER, GObject)

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_mod_manager_get_default:
 *
 * Gets the default mod manager instance.
 *
 * Returns: (transfer none): the default #LrgModManager
 */
LRG_AVAILABLE_IN_ALL
LrgModManager *    lrg_mod_manager_get_default   (void);

/**
 * lrg_mod_manager_new:
 *
 * Creates a new mod manager.
 *
 * Returns: (transfer full): a new #LrgModManager
 */
LRG_AVAILABLE_IN_ALL
LrgModManager *    lrg_mod_manager_new           (void);

/* ==========================================================================
 * Loader Configuration
 * ========================================================================== */

/**
 * lrg_mod_manager_get_loader:
 * @self: a #LrgModManager
 *
 * Gets the mod loader.
 *
 * Returns: (transfer none): the #LrgModLoader
 */
LRG_AVAILABLE_IN_ALL
LrgModLoader *     lrg_mod_manager_get_loader    (LrgModManager *self);

/**
 * lrg_mod_manager_add_search_path:
 * @self: a #LrgModManager
 * @path: directory path to search
 *
 * Adds a mod search path.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manager_add_search_path (LrgModManager *self,
                                                    const gchar   *path);

/* ==========================================================================
 * Mod Management
 * ========================================================================== */

/**
 * lrg_mod_manager_discover:
 * @self: a #LrgModManager
 * @error: (nullable): return location for error
 *
 * Discovers mods from all search paths.
 *
 * Returns: number of mods discovered
 */
LRG_AVAILABLE_IN_ALL
guint              lrg_mod_manager_discover      (LrgModManager  *self,
                                                  GError        **error);

/**
 * lrg_mod_manager_load_all:
 * @self: a #LrgModManager
 * @error: (nullable): return location for error
 *
 * Loads all discovered mods in dependency order.
 *
 * Returns: %TRUE if all required mods loaded successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_load_all      (LrgModManager  *self,
                                                  GError        **error);

/**
 * lrg_mod_manager_unload_all:
 * @self: a #LrgModManager
 *
 * Unloads all mods in reverse order.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manager_unload_all    (LrgModManager *self);

/**
 * lrg_mod_manager_reload:
 * @self: a #LrgModManager
 * @error: (nullable): return location for error
 *
 * Reloads all mods.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_reload        (LrgModManager  *self,
                                                  GError        **error);

/* ==========================================================================
 * Mod Queries
 * ========================================================================== */

/**
 * lrg_mod_manager_get_mods:
 * @self: a #LrgModManager
 *
 * Gets all discovered mods.
 *
 * Returns: (transfer none) (element-type LrgMod): the mods
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manager_get_mods      (LrgModManager *self);

/**
 * lrg_mod_manager_get_loaded_mods:
 * @self: a #LrgModManager
 *
 * Gets all loaded mods in load order.
 *
 * Returns: (transfer none) (element-type LrgMod): the loaded mods
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manager_get_loaded_mods (LrgModManager *self);

/**
 * lrg_mod_manager_get_mod:
 * @self: a #LrgModManager
 * @mod_id: the mod ID
 *
 * Gets a mod by ID.
 *
 * Returns: (transfer none) (nullable): the mod, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgMod *           lrg_mod_manager_get_mod       (LrgModManager *self,
                                                  const gchar   *mod_id);

/**
 * lrg_mod_manager_has_mod:
 * @self: a #LrgModManager
 * @mod_id: the mod ID
 *
 * Checks if a mod is registered.
 *
 * Returns: %TRUE if the mod exists
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_has_mod       (LrgModManager *self,
                                                  const gchar   *mod_id);

/**
 * lrg_mod_manager_is_mod_loaded:
 * @self: a #LrgModManager
 * @mod_id: the mod ID
 *
 * Checks if a mod is loaded.
 *
 * Returns: %TRUE if the mod is loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_is_mod_loaded (LrgModManager *self,
                                                  const gchar   *mod_id);

/* ==========================================================================
 * Individual Mod Control
 * ========================================================================== */

/**
 * lrg_mod_manager_enable_mod:
 * @self: a #LrgModManager
 * @mod_id: the mod ID
 *
 * Enables a mod.
 *
 * Returns: %TRUE if the mod was found
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_enable_mod    (LrgModManager *self,
                                                  const gchar   *mod_id);

/**
 * lrg_mod_manager_disable_mod:
 * @self: a #LrgModManager
 * @mod_id: the mod ID
 *
 * Disables a mod.
 *
 * Returns: %TRUE if the mod was found
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_disable_mod   (LrgModManager *self,
                                                  const gchar   *mod_id);

/* ==========================================================================
 * Load Order
 * ========================================================================== */

/**
 * lrg_mod_manager_get_load_order:
 * @self: a #LrgModManager
 *
 * Gets the computed load order.
 *
 * Returns: (transfer full) (element-type utf8): mod IDs in load order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manager_get_load_order (LrgModManager *self);

/**
 * lrg_mod_manager_check_dependencies:
 * @self: a #LrgModManager
 * @mod: the mod to check
 * @error: (nullable): return location for error
 *
 * Checks if all dependencies for a mod are satisfied.
 *
 * Returns: %TRUE if all dependencies are met
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manager_check_dependencies (LrgModManager  *self,
                                                       LrgMod         *mod,
                                                       GError        **error);

/* ==========================================================================
 * Resource Resolution
 * ========================================================================== */

/**
 * lrg_mod_manager_resolve_path:
 * @self: a #LrgModManager
 * @path: relative resource path
 *
 * Resolves a path, checking all loaded mods in reverse order.
 * This allows mods to override base game resources.
 *
 * Returns: (transfer full) (nullable): the resolved path
 */
LRG_AVAILABLE_IN_ALL
gchar *            lrg_mod_manager_resolve_path  (LrgModManager *self,
                                                  const gchar   *path);

/* ==========================================================================
 * Provider Queries
 * ========================================================================== */

/**
 * lrg_mod_manager_collect_entity_types:
 * @self: a #LrgModManager
 *
 * Collects entity types from all loaded mods implementing #LrgEntityProvider.
 *
 * Returns: (transfer container) (element-type GType): list of entity GTypes
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_entity_types (LrgModManager *self);

/**
 * lrg_mod_manager_collect_item_defs:
 * @self: a #LrgModManager
 *
 * Collects item definitions from all loaded mods implementing #LrgItemProvider.
 *
 * Returns: (transfer container) (element-type LrgItemDef): list of #LrgItemDef
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_item_defs (LrgModManager *self);

/**
 * lrg_mod_manager_collect_dialog_trees:
 * @self: a #LrgModManager
 *
 * Collects dialog trees from all loaded mods implementing #LrgDialogProvider.
 *
 * Returns: (transfer container) (element-type LrgDialogTree): list of #LrgDialogTree
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_dialog_trees (LrgModManager *self);

/**
 * lrg_mod_manager_collect_quest_defs:
 * @self: a #LrgModManager
 *
 * Collects quest definitions from all loaded mods implementing #LrgQuestProvider.
 *
 * Returns: (transfer container) (element-type LrgQuestDef): list of #LrgQuestDef
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_quest_defs (LrgModManager *self);

/**
 * lrg_mod_manager_collect_bt_node_types:
 * @self: a #LrgModManager
 *
 * Collects behavior tree node types from all loaded mods implementing #LrgAIProvider.
 *
 * Returns: (transfer container) (element-type GType): list of BT node GTypes
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_bt_node_types (LrgModManager *self);

/**
 * lrg_mod_manager_collect_commands:
 * @self: a #LrgModManager
 *
 * Collects console commands from all loaded mods implementing #LrgCommandProvider.
 *
 * Returns: (transfer container) (element-type LrgConsoleCommand): list of commands
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_commands (LrgModManager *self);

/**
 * lrg_mod_manager_collect_locales:
 * @self: a #LrgModManager
 *
 * Collects locales from all loaded mods implementing #LrgLocaleProvider.
 *
 * Returns: (transfer container) (element-type LrgLocale): list of #LrgLocale
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_locales (LrgModManager *self);

/**
 * lrg_mod_manager_collect_scenes:
 * @self: a #LrgModManager
 *
 * Collects scenes from all loaded mods implementing #LrgSceneProvider.
 *
 * Returns: (transfer container) (element-type GObject): list of GrlScene
 */
LRG_AVAILABLE_IN_ALL
GList *            lrg_mod_manager_collect_scenes (LrgModManager *self);

G_END_DECLS
