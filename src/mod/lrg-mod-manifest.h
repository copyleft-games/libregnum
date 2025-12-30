/* lrg-mod-manifest.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod manifest system.
 *
 * The manifest contains metadata about a mod including its ID,
 * version, dependencies, and load order preferences.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_MOD_MANIFEST (lrg_mod_manifest_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgModManifest, lrg_mod_manifest, LRG, MOD_MANIFEST, GObject)

/* ==========================================================================
 * Mod Dependency (Boxed Type)
 * ========================================================================== */

#define LRG_TYPE_MOD_DEPENDENCY (lrg_mod_dependency_get_type ())

/**
 * LrgModDependency:
 *
 * Represents a dependency on another mod.
 */
typedef struct _LrgModDependency LrgModDependency;

LRG_AVAILABLE_IN_ALL
GType              lrg_mod_dependency_get_type   (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgModDependency * lrg_mod_dependency_new        (const gchar *mod_id,
                                                  const gchar *min_version,
                                                  gboolean     optional);

LRG_AVAILABLE_IN_ALL
LrgModDependency * lrg_mod_dependency_copy       (const LrgModDependency *self);

LRG_AVAILABLE_IN_ALL
void               lrg_mod_dependency_free       (LrgModDependency *self);

/**
 * lrg_mod_dependency_get_mod_id:
 * @self: a #LrgModDependency
 *
 * Gets the required mod ID.
 *
 * Returns: (transfer none): the mod ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_dependency_get_mod_id (const LrgModDependency *self);

/**
 * lrg_mod_dependency_get_min_version:
 * @self: a #LrgModDependency
 *
 * Gets the minimum required version, if any.
 *
 * Returns: (transfer none) (nullable): the minimum version
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_dependency_get_min_version (const LrgModDependency *self);

/**
 * lrg_mod_dependency_is_optional:
 * @self: a #LrgModDependency
 *
 * Checks if this dependency is optional.
 *
 * Returns: %TRUE if optional
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_dependency_is_optional (const LrgModDependency *self);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_mod_manifest_new:
 * @mod_id: unique identifier for the mod
 *
 * Creates a new mod manifest with the given ID.
 *
 * Returns: (transfer full): a new #LrgModManifest
 */
LRG_AVAILABLE_IN_ALL
LrgModManifest *   lrg_mod_manifest_new          (const gchar *mod_id);

/**
 * lrg_mod_manifest_new_from_file:
 * @path: path to manifest file (YAML)
 * @error: (nullable): return location for error
 *
 * Loads a manifest from a YAML file.
 *
 * Returns: (transfer full) (nullable): a new #LrgModManifest, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgModManifest *   lrg_mod_manifest_new_from_file (const gchar  *path,
                                                   GError      **error);

/* ==========================================================================
 * Identity
 * ========================================================================== */

/**
 * lrg_mod_manifest_get_id:
 * @self: a #LrgModManifest
 *
 * Gets the mod's unique identifier.
 *
 * Returns: (transfer none): the mod ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_id       (LrgModManifest *self);

/**
 * lrg_mod_manifest_get_name:
 * @self: a #LrgModManifest
 *
 * Gets the mod's display name.
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_name     (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_name:
 * @self: a #LrgModManifest
 * @name: (nullable): display name
 *
 * Sets the mod's display name.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_name     (LrgModManifest *self,
                                                  const gchar    *name);

/**
 * lrg_mod_manifest_get_version:
 * @self: a #LrgModManifest
 *
 * Gets the mod version string.
 *
 * Returns: (transfer none) (nullable): the version string
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_version  (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_version:
 * @self: a #LrgModManifest
 * @version: (nullable): version string
 *
 * Sets the mod version.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_version  (LrgModManifest *self,
                                                  const gchar    *version);

/**
 * lrg_mod_manifest_get_description:
 * @self: a #LrgModManifest
 *
 * Gets the mod description.
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_description (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_description:
 * @self: a #LrgModManifest
 * @description: (nullable): description text
 *
 * Sets the mod description.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_description (LrgModManifest *self,
                                                     const gchar    *description);

/**
 * lrg_mod_manifest_get_author:
 * @self: a #LrgModManifest
 *
 * Gets the mod author.
 *
 * Returns: (transfer none) (nullable): the author name
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_author   (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_author:
 * @self: a #LrgModManifest
 * @author: (nullable): author name
 *
 * Sets the mod author.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_author   (LrgModManifest *self,
                                                  const gchar    *author);

/* ==========================================================================
 * Type and Priority
 * ========================================================================== */

/**
 * lrg_mod_manifest_get_mod_type:
 * @self: a #LrgModManifest
 *
 * Gets the mod type.
 *
 * Returns: the #LrgModType
 */
LRG_AVAILABLE_IN_ALL
LrgModType         lrg_mod_manifest_get_mod_type (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_mod_type:
 * @self: a #LrgModManifest
 * @type: the mod type
 *
 * Sets the mod type.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_mod_type (LrgModManifest *self,
                                                  LrgModType      type);

/**
 * lrg_mod_manifest_get_priority:
 * @self: a #LrgModManifest
 *
 * Gets the load priority.
 *
 * Returns: the #LrgModPriority
 */
LRG_AVAILABLE_IN_ALL
LrgModPriority     lrg_mod_manifest_get_priority (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_priority:
 * @self: a #LrgModManifest
 * @priority: the load priority
 *
 * Sets the load priority.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_priority (LrgModManifest *self,
                                                  LrgModPriority  priority);

/* ==========================================================================
 * Dependencies
 * ========================================================================== */

/**
 * lrg_mod_manifest_get_dependencies:
 * @self: a #LrgModManifest
 *
 * Gets the list of mod dependencies.
 *
 * Returns: (transfer none) (element-type LrgModDependency): the dependencies
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manifest_get_dependencies (LrgModManifest *self);

/**
 * lrg_mod_manifest_add_dependency:
 * @self: a #LrgModManifest
 * @mod_id: the required mod ID
 * @min_version: (nullable): minimum required version
 * @optional: whether the dependency is optional
 *
 * Adds a dependency on another mod.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_add_dependency (LrgModManifest *self,
                                                    const gchar    *mod_id,
                                                    const gchar    *min_version,
                                                    gboolean        optional);

/**
 * lrg_mod_manifest_has_dependency:
 * @self: a #LrgModManifest
 * @mod_id: the mod ID to check
 *
 * Checks if this mod depends on another mod.
 *
 * Returns: %TRUE if depends on the given mod
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manifest_has_dependency (LrgModManifest *self,
                                                    const gchar    *mod_id);

/* ==========================================================================
 * Load Order
 * ========================================================================== */

/**
 * lrg_mod_manifest_get_load_after:
 * @self: a #LrgModManifest
 *
 * Gets mods that should load before this one.
 *
 * Returns: (transfer none) (element-type utf8): mod IDs to load before
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manifest_get_load_after (LrgModManifest *self);

/**
 * lrg_mod_manifest_add_load_after:
 * @self: a #LrgModManifest
 * @mod_id: mod ID that should load first
 *
 * Specifies a mod that should load before this one.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_add_load_after (LrgModManifest *self,
                                                    const gchar    *mod_id);

/**
 * lrg_mod_manifest_get_load_before:
 * @self: a #LrgModManifest
 *
 * Gets mods that should load after this one.
 *
 * Returns: (transfer none) (element-type utf8): mod IDs to load after
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manifest_get_load_before (LrgModManifest *self);

/**
 * lrg_mod_manifest_add_load_before:
 * @self: a #LrgModManifest
 * @mod_id: mod ID that should load after
 *
 * Specifies a mod that should load after this one.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_add_load_before (LrgModManifest *self,
                                                     const gchar    *mod_id);

/* ==========================================================================
 * Paths
 * ========================================================================== */

/**
 * lrg_mod_manifest_get_data_path:
 * @self: a #LrgModManifest
 *
 * Gets the relative path to mod data directory.
 *
 * Returns: (transfer none) (nullable): the data path
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_data_path (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_data_path:
 * @self: a #LrgModManifest
 * @path: (nullable): relative path to data
 *
 * Sets the data directory path.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_data_path (LrgModManifest *self,
                                                   const gchar    *path);

/**
 * lrg_mod_manifest_get_entry_point:
 * @self: a #LrgModManifest
 *
 * Gets the entry point for script/native mods.
 *
 * Returns: (transfer none) (nullable): the entry point
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_entry_point (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_entry_point:
 * @self: a #LrgModManifest
 * @entry_point: (nullable): entry point path
 *
 * Sets the entry point for script/native mods.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_entry_point (LrgModManifest *self,
                                                     const gchar    *entry_point);

/* ==========================================================================
 * DLC Information
 * ========================================================================== */

/**
 * lrg_mod_manifest_is_dlc:
 * @self: a #LrgModManifest
 *
 * Checks if this manifest describes a DLC.
 *
 * Returns: %TRUE if this is a DLC manifest
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manifest_is_dlc       (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_is_dlc:
 * @self: a #LrgModManifest
 * @is_dlc: whether this is a DLC
 *
 * Sets whether this manifest describes a DLC.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_is_dlc   (LrgModManifest *self,
                                                  gboolean        is_dlc);

/**
 * lrg_mod_manifest_get_dlc_type:
 * @self: a #LrgModManifest
 *
 * Gets the DLC type.
 *
 * Returns: the #LrgDlcType
 */
LRG_AVAILABLE_IN_ALL
LrgDlcType         lrg_mod_manifest_get_dlc_type (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_dlc_type:
 * @self: a #LrgModManifest
 * @dlc_type: the DLC type
 *
 * Sets the DLC type.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_dlc_type (LrgModManifest *self,
                                                  LrgDlcType      dlc_type);

/**
 * lrg_mod_manifest_get_steam_app_id:
 * @self: a #LrgModManifest
 *
 * Gets the Steam App ID for DLC ownership verification.
 *
 * Returns: the Steam App ID, or 0 if not set
 */
LRG_AVAILABLE_IN_ALL
guint32            lrg_mod_manifest_get_steam_app_id (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_steam_app_id:
 * @self: a #LrgModManifest
 * @app_id: the Steam App ID
 *
 * Sets the Steam App ID.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_steam_app_id (LrgModManifest *self,
                                                      guint32         app_id);

/**
 * lrg_mod_manifest_get_store_id:
 * @self: a #LrgModManifest
 *
 * Gets the store ID for other platforms.
 *
 * Returns: (transfer none) (nullable): the store ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_store_id (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_store_id:
 * @self: a #LrgModManifest
 * @store_id: (nullable): the store ID
 *
 * Sets the store ID for other platforms.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_store_id (LrgModManifest *self,
                                                  const gchar    *store_id);

/**
 * lrg_mod_manifest_get_price_string:
 * @self: a #LrgModManifest
 *
 * Gets the price display string.
 *
 * Returns: (transfer none) (nullable): the price string
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_price_string (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_price_string:
 * @self: a #LrgModManifest
 * @price: (nullable): the price string
 *
 * Sets the price display string.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_price_string (LrgModManifest *self,
                                                      const gchar    *price);

/**
 * lrg_mod_manifest_get_release_date:
 * @self: a #LrgModManifest
 *
 * Gets the DLC release date.
 *
 * Returns: (transfer none) (nullable): the release date
 */
LRG_AVAILABLE_IN_ALL
GDateTime *        lrg_mod_manifest_get_release_date (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_release_date:
 * @self: a #LrgModManifest
 * @date: (nullable): the release date
 *
 * Sets the DLC release date.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_release_date (LrgModManifest *self,
                                                      GDateTime      *date);

/**
 * lrg_mod_manifest_get_min_game_version:
 * @self: a #LrgModManifest
 *
 * Gets the minimum required game version for this DLC.
 *
 * Returns: (transfer none) (nullable): the minimum version
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_min_game_version (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_min_game_version:
 * @self: a #LrgModManifest
 * @version: (nullable): the minimum game version
 *
 * Sets the minimum required game version.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_min_game_version (LrgModManifest *self,
                                                          const gchar    *version);

/**
 * lrg_mod_manifest_get_ownership_method:
 * @self: a #LrgModManifest
 *
 * Gets the ownership verification method (steam, license, manifest).
 *
 * Returns: (transfer none) (nullable): the ownership method
 */
LRG_AVAILABLE_IN_ALL
const gchar *      lrg_mod_manifest_get_ownership_method (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_ownership_method:
 * @self: a #LrgModManifest
 * @method: (nullable): ownership method (steam, license, manifest)
 *
 * Sets the ownership verification method.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_ownership_method (LrgModManifest *self,
                                                          const gchar    *method);

/**
 * lrg_mod_manifest_get_trial_enabled:
 * @self: a #LrgModManifest
 *
 * Checks if trial mode is enabled for this DLC.
 *
 * Returns: %TRUE if trial is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manifest_get_trial_enabled (LrgModManifest *self);

/**
 * lrg_mod_manifest_set_trial_enabled:
 * @self: a #LrgModManifest
 * @enabled: whether trial is enabled
 *
 * Sets whether trial mode is enabled.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_set_trial_enabled (LrgModManifest *self,
                                                       gboolean        enabled);

/**
 * lrg_mod_manifest_get_trial_content_ids:
 * @self: a #LrgModManifest
 *
 * Gets the list of content IDs available in trial mode.
 *
 * Returns: (transfer none) (element-type utf8): the trial content IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *        lrg_mod_manifest_get_trial_content_ids (LrgModManifest *self);

/**
 * lrg_mod_manifest_add_trial_content_id:
 * @self: a #LrgModManifest
 * @content_id: a content ID available in trial
 *
 * Adds a content ID to the trial content list.
 */
LRG_AVAILABLE_IN_ALL
void               lrg_mod_manifest_add_trial_content_id (LrgModManifest *self,
                                                          const gchar    *content_id);

/* ==========================================================================
 * Serialization
 * ========================================================================== */

/**
 * lrg_mod_manifest_save_to_file:
 * @self: a #LrgModManifest
 * @path: path to save to
 * @error: (nullable): return location for error
 *
 * Saves the manifest to a YAML file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean           lrg_mod_manifest_save_to_file (LrgModManifest  *self,
                                                  const gchar     *path,
                                                  GError         **error);

G_END_DECLS
