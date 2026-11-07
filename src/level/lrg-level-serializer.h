/* lrg-level-serializer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * YAML (.rlevel) serializer for #LrgLevel documents.
 *
 * LrgLevelSerializer reads and writes the `.rlevel` authoring format: a YAML
 * document describing the level metadata and a recursive tree of nodes, each
 * with a transform, an optional tagged visual payload, component descriptions,
 * and script bindings. A single instance may be reused for many loads and
 * saves.
 *
 * Since 0.9.0 linked prefab instances may be stored as references (see
 * #LrgPrefabStorage and `docs/modules/level/prefabs-runtime.org`): the
 * #LrgLevelSerializer:prefab-storage property, the level's own
 * `store_prefab_instances` setting and the project's choose the form on
 * save, and loading expands references from their `.rprefab` files, so
 * every consumer of the loaded #LrgLevel (lrg_level_instantiate(), the
 * editor, games) sees full subtrees.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-prefab.h"
#include "../core/lrg-asset-manager.h"

G_BEGIN_DECLS

#define LRG_TYPE_LEVEL_SERIALIZER (lrg_level_serializer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLevelSerializer, lrg_level_serializer, LRG, LEVEL_SERIALIZER, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_level_serializer_new:
 *
 * Creates a new #LrgLevelSerializer.
 *
 * Returns: (transfer full): a new #LrgLevelSerializer
 */
LRG_AVAILABLE_IN_ALL
LrgLevelSerializer * lrg_level_serializer_new (void);

/* ==========================================================================
 * Loading
 * ========================================================================== */

/**
 * lrg_level_serializer_load_from_file:
 * @self: an #LrgLevelSerializer
 * @path: path to a `.rlevel` file
 * @error: (nullable): return location for an error
 *
 * Loads a level from a `.rlevel` file.
 *
 * Returns: (transfer full) (nullable): a new #LrgLevel, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_serializer_load_from_file (LrgLevelSerializer  *self,
                                                const gchar         *path,
                                                GError             **error);

/**
 * lrg_level_serializer_load_from_data:
 * @self: an #LrgLevelSerializer
 * @data: the serialized YAML data
 * @length: length of @data in bytes, or -1 if NUL-terminated
 * @error: (nullable): return location for an error
 *
 * Loads a level from in-memory `.rlevel` YAML data.
 *
 * Returns: (transfer full) (nullable): a new #LrgLevel, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_serializer_load_from_data (LrgLevelSerializer  *self,
                                                const gchar         *data,
                                                gssize               length,
                                                GError             **error);

/* ==========================================================================
 * Saving
 * ========================================================================== */

/**
 * lrg_level_serializer_save_to_file:
 * @self: an #LrgLevelSerializer
 * @level: the level to save
 * @path: destination file path
 * @error: (nullable): return location for an error
 *
 * Saves a level to a `.rlevel` file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_serializer_save_to_file (LrgLevelSerializer  *self,
                                            LrgLevel            *level,
                                            const gchar         *path,
                                            GError             **error);

/**
 * lrg_level_serializer_save_to_data:
 * @self: an #LrgLevelSerializer
 * @level: the level to save
 * @length: (out) (optional): return location for the length in bytes
 *
 * Serializes a level to a newly allocated `.rlevel` YAML string.
 *
 * Returns: (transfer full) (nullable): the YAML string, or %NULL on failure
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_level_serializer_save_to_data (LrgLevelSerializer *self,
                                           LrgLevel           *level,
                                           gsize              *length);

/* ==========================================================================
 * Prefab storage and resolution (since 0.9.0)
 * ========================================================================== */

/**
 * lrg_level_serializer_set_prefab_storage:
 * @self: an #LrgLevelSerializer
 * @storage: how saving stores linked prefab instances;
 *   %LRG_PREFAB_STORAGE_DEFAULT defers to the level, then the project
 *
 * Sets #LrgLevelSerializer:prefab-storage.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_serializer_set_prefab_storage (LrgLevelSerializer *self,
                                              LrgPrefabStorage    storage);

/**
 * lrg_level_serializer_get_prefab_storage:
 * @self: an #LrgLevelSerializer
 *
 * Returns: the #LrgLevelSerializer:prefab-storage override
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrefabStorage lrg_level_serializer_get_prefab_storage (LrgLevelSerializer *self);

/**
 * lrg_level_serializer_set_project_root:
 * @self: an #LrgLevelSerializer
 * @root: (nullable) (type filename): the project directory, or %NULL to
 *   discover it from the level path
 *
 * Sets #LrgLevelSerializer:project-root.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_serializer_set_project_root (LrgLevelSerializer *self,
                                            const gchar        *root);

/**
 * lrg_level_serializer_get_project_root:
 * @self: an #LrgLevelSerializer
 *
 * Returns: (transfer none) (nullable) (type filename): the explicit
 *   project root
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_level_serializer_get_project_root (LrgLevelSerializer *self);

/**
 * lrg_level_serializer_set_asset_manager:
 * @self: an #LrgLevelSerializer
 * @manager: (nullable): an asset manager whose guid index resolves prefab
 *   sources
 *
 * Sets #LrgLevelSerializer:asset-manager.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_serializer_set_asset_manager (LrgLevelSerializer *self,
                                             LrgAssetManager    *manager);

/**
 * lrg_level_serializer_get_asset_manager:
 * @self: an #LrgLevelSerializer
 *
 * Returns: (transfer none) (nullable): the asset manager used to resolve
 *   guid prefab sources
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_level_serializer_get_asset_manager (LrgLevelSerializer *self);

G_END_DECLS
