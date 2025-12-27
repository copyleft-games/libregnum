/* lrg-scene.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-level scene container for Blender-exported 3D scenes.
 *
 * LrgScene is the root container that holds all entities and metadata
 * from a Blender scene export. It provides methods to access and
 * manipulate entities, and supports round-trip serialization to/from
 * the YAML format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scene-entity.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE (lrg_scene_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScene, lrg_scene, LRG, SCENE, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_scene_new:
 * @name: The scene name
 *
 * Creates a new #LrgScene.
 *
 * Returns: (transfer full): A new #LrgScene
 */
LRG_AVAILABLE_IN_ALL
LrgScene * lrg_scene_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_scene_get_name:
 * @self: an #LrgScene
 *
 * Gets the name of the scene.
 *
 * Returns: (transfer none): The scene name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scene_get_name (LrgScene *self);

/**
 * lrg_scene_set_name:
 * @self: an #LrgScene
 * @name: The new name
 *
 * Sets the name of the scene.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_set_name (LrgScene    *self,
                         const gchar *name);

/**
 * lrg_scene_get_exported_from:
 * @self: an #LrgScene
 *
 * Gets the application that exported the scene (e.g., "Blender 4.3").
 *
 * Returns: (transfer none) (nullable): The exporter name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scene_get_exported_from (LrgScene *self);

/**
 * lrg_scene_set_exported_from:
 * @self: an #LrgScene
 * @exported_from: (nullable): The exporter name
 *
 * Sets the application that exported the scene.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_set_exported_from (LrgScene    *self,
                                  const gchar *exported_from);

/**
 * lrg_scene_get_export_date:
 * @self: an #LrgScene
 *
 * Gets the date/time when the scene was exported.
 *
 * Returns: (transfer none) (nullable): The export date
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_scene_get_export_date (LrgScene *self);

/**
 * lrg_scene_set_export_date:
 * @self: an #LrgScene
 * @export_date: (nullable) (transfer none): The export date
 *
 * Sets the export date.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_set_export_date (LrgScene  *self,
                                GDateTime *export_date);

/**
 * lrg_scene_set_export_date_iso:
 * @self: an #LrgScene
 * @iso_string: An ISO 8601 date string
 *
 * Sets the export date from an ISO 8601 string.
 *
 * Returns: %TRUE if the string was parsed successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_set_export_date_iso (LrgScene    *self,
                                        const gchar *iso_string);

/* ==========================================================================
 * Entities
 * ========================================================================== */

/**
 * lrg_scene_add_entity:
 * @self: an #LrgScene
 * @entity: (transfer none): The entity to add
 *
 * Adds an entity to the scene. If an entity with the same name
 * already exists, it will be replaced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_add_entity (LrgScene       *self,
                           LrgSceneEntity *entity);

/**
 * lrg_scene_remove_entity:
 * @self: an #LrgScene
 * @name: The entity name to remove
 *
 * Removes an entity from the scene by name.
 *
 * Returns: %TRUE if the entity was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_remove_entity (LrgScene    *self,
                                  const gchar *name);

/**
 * lrg_scene_get_entity:
 * @self: an #LrgScene
 * @name: The entity name
 *
 * Gets an entity by name.
 *
 * Returns: (transfer none) (nullable): The entity or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgSceneEntity * lrg_scene_get_entity (LrgScene    *self,
                                       const gchar *name);

/**
 * lrg_scene_get_entity_names:
 * @self: an #LrgScene
 *
 * Gets a list of all entity names.
 *
 * Returns: (transfer container) (element-type utf8): List of entity names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_scene_get_entity_names (LrgScene *self);

/**
 * lrg_scene_get_entity_count:
 * @self: an #LrgScene
 *
 * Gets the number of entities in the scene.
 *
 * Returns: The entity count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_scene_get_entity_count (LrgScene *self);

/**
 * LrgSceneForeachFunc:
 * @name: The entity name
 * @entity: The entity
 * @user_data: User-provided data
 *
 * Callback function for lrg_scene_foreach_entity().
 */
typedef void (*LrgSceneForeachFunc) (const gchar    *name,
                                     LrgSceneEntity *entity,
                                     gpointer        user_data);

/**
 * lrg_scene_foreach_entity:
 * @self: an #LrgScene
 * @func: (scope call): The function to call for each entity
 * @user_data: User data to pass to the function
 *
 * Iterates over all entities in the scene.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_foreach_entity (LrgScene            *self,
                               LrgSceneForeachFunc  func,
                               gpointer             user_data);

G_END_DECLS
