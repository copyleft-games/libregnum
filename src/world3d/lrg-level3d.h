/* lrg-level3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D level container with spatial indexing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-bounding-box3d.h"
#include "lrg-spawn-point3d.h"
#include "lrg-trigger3d.h"
#include "lrg-octree.h"

G_BEGIN_DECLS

#define LRG_TYPE_LEVEL3D (lrg_level3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLevel3D, lrg_level3d, LRG, LEVEL3D, GObject)

/**
 * lrg_level3d_new:
 * @id: Unique identifier for this level
 *
 * Creates a new 3D level.
 *
 * Returns: (transfer full): A new #LrgLevel3D
 */
LRG_AVAILABLE_IN_ALL
LrgLevel3D *        lrg_level3d_new                 (const gchar            *id);

/**
 * lrg_level3d_get_id:
 * @self: An #LrgLevel3D
 *
 * Gets the level identifier.
 *
 * Returns: (transfer none): The level ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_level3d_get_id              (LrgLevel3D             *self);

/**
 * lrg_level3d_get_name:
 * @self: An #LrgLevel3D
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The display name
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_level3d_get_name            (LrgLevel3D             *self);

/**
 * lrg_level3d_set_name:
 * @self: An #LrgLevel3D
 * @name: (nullable): Display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_set_name            (LrgLevel3D             *self,
                                                     const gchar            *name);

/**
 * lrg_level3d_get_bounds:
 * @self: An #LrgLevel3D
 *
 * Gets the level bounds.
 *
 * Returns: (transfer full): The bounds
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_level3d_get_bounds          (LrgLevel3D             *self);

/**
 * lrg_level3d_set_bounds:
 * @self: An #LrgLevel3D
 * @bounds: (transfer none): New level bounds
 *
 * Sets the level bounds. This reinitializes the internal octree.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_set_bounds          (LrgLevel3D             *self,
                                                     const LrgBoundingBox3D *bounds);

/* --- Spawn Point Management --- */

/**
 * lrg_level3d_add_spawn_point:
 * @self: An #LrgLevel3D
 * @spawn: (transfer none): Spawn point to add
 *
 * Adds a spawn point to the level.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_add_spawn_point     (LrgLevel3D             *self,
                                                     const LrgSpawnPoint3D  *spawn);

/**
 * lrg_level3d_remove_spawn_point:
 * @self: An #LrgLevel3D
 * @id: Spawn point ID to remove
 *
 * Removes a spawn point from the level.
 *
 * Returns: %TRUE if the spawn point was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_level3d_remove_spawn_point  (LrgLevel3D             *self,
                                                     const gchar            *id);

/**
 * lrg_level3d_get_spawn_point:
 * @self: An #LrgLevel3D
 * @id: Spawn point ID
 *
 * Gets a spawn point by ID.
 *
 * Returns: (transfer none) (nullable): The spawn point, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const LrgSpawnPoint3D * lrg_level3d_get_spawn_point (LrgLevel3D             *self,
                                                     const gchar            *id);

/**
 * lrg_level3d_get_spawn_points:
 * @self: An #LrgLevel3D
 *
 * Gets all spawn points.
 *
 * Returns: (transfer container) (element-type LrgSpawnPoint3D): Array of spawn points
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_get_spawn_points    (LrgLevel3D             *self);

/**
 * lrg_level3d_get_spawn_points_by_type:
 * @self: An #LrgLevel3D
 * @spawn_type: Type of spawn points to get
 *
 * Gets spawn points of a specific type.
 *
 * Returns: (transfer container) (element-type LrgSpawnPoint3D): Array of matching spawn points
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_get_spawn_points_by_type (LrgLevel3D        *self,
                                                          LrgSpawnType       spawn_type);

/**
 * lrg_level3d_get_spawn_point_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of spawn points.
 *
 * Returns: Spawn point count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_level3d_get_spawn_point_count (LrgLevel3D           *self);

/* --- Trigger Management --- */

/**
 * lrg_level3d_add_trigger:
 * @self: An #LrgLevel3D
 * @trigger: (transfer none): Trigger to add
 *
 * Adds a trigger to the level.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_add_trigger         (LrgLevel3D             *self,
                                                     const LrgTrigger3D     *trigger);

/**
 * lrg_level3d_remove_trigger:
 * @self: An #LrgLevel3D
 * @id: Trigger ID to remove
 *
 * Removes a trigger from the level.
 *
 * Returns: %TRUE if the trigger was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_level3d_remove_trigger      (LrgLevel3D             *self,
                                                     const gchar            *id);

/**
 * lrg_level3d_get_trigger:
 * @self: An #LrgLevel3D
 * @id: Trigger ID
 *
 * Gets a trigger by ID.
 *
 * Returns: (transfer none) (nullable): The trigger, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const LrgTrigger3D * lrg_level3d_get_trigger        (LrgLevel3D             *self,
                                                     const gchar            *id);

/**
 * lrg_level3d_get_triggers:
 * @self: An #LrgLevel3D
 *
 * Gets all triggers.
 *
 * Returns: (transfer container) (element-type LrgTrigger3D): Array of triggers
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_get_triggers        (LrgLevel3D             *self);

/**
 * lrg_level3d_get_trigger_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of triggers.
 *
 * Returns: Trigger count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_level3d_get_trigger_count   (LrgLevel3D             *self);

/**
 * lrg_level3d_check_triggers:
 * @self: An #LrgLevel3D
 * @point: (transfer none): Point to test
 *
 * Finds all enabled triggers that contain the given point.
 *
 * Returns: (transfer container) (element-type LrgTrigger3D): Array of activated triggers
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_check_triggers      (LrgLevel3D             *self,
                                                     const GrlVector3       *point);

/* --- Model Management --- */

/**
 * lrg_level3d_add_model:
 * @self: An #LrgLevel3D
 * @model: (transfer none): Model to add
 * @bounds: (transfer none): Model bounds for spatial indexing
 *
 * Adds a model to the level.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_add_model           (LrgLevel3D             *self,
                                                     GrlModel               *model,
                                                     const LrgBoundingBox3D *bounds);

/**
 * lrg_level3d_remove_model:
 * @self: An #LrgLevel3D
 * @model: Model to remove
 *
 * Removes a model from the level.
 *
 * Returns: %TRUE if the model was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_level3d_remove_model        (LrgLevel3D             *self,
                                                     GrlModel               *model);

/**
 * lrg_level3d_get_models:
 * @self: An #LrgLevel3D
 *
 * Gets all models in the level.
 *
 * Returns: (transfer container) (element-type GrlModel): Array of models
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_get_models          (LrgLevel3D             *self);

/**
 * lrg_level3d_get_model_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of models.
 *
 * Returns: Model count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_level3d_get_model_count     (LrgLevel3D             *self);

/* --- Spatial Queries --- */

/**
 * lrg_level3d_query_box:
 * @self: An #LrgLevel3D
 * @box: (transfer none): Query bounding box
 *
 * Finds all objects (models) that intersect with the query box.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_query_box           (LrgLevel3D             *self,
                                                     const LrgBoundingBox3D *box);

/**
 * lrg_level3d_query_sphere:
 * @self: An #LrgLevel3D
 * @center: (transfer none): Sphere center
 * @radius: Sphere radius
 *
 * Finds all objects (models) that intersect with a sphere.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_level3d_query_sphere        (LrgLevel3D             *self,
                                                     const GrlVector3       *center,
                                                     gfloat                  radius);

/* --- Properties --- */

/**
 * lrg_level3d_set_property_value:
 * @self: An #LrgLevel3D
 * @key: Property key
 * @value: (transfer none): Property value
 *
 * Sets a custom property on the level.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_set_property_value  (LrgLevel3D             *self,
                                                     const gchar            *key,
                                                     const GValue           *value);

/**
 * lrg_level3d_get_property_value:
 * @self: An #LrgLevel3D
 * @key: Property key
 *
 * Gets a custom property from the level.
 *
 * Returns: (transfer none) (nullable): The property value, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const GValue *      lrg_level3d_get_property_value  (LrgLevel3D             *self,
                                                     const gchar            *key);

/**
 * lrg_level3d_has_property:
 * @self: An #LrgLevel3D
 * @key: Property key
 *
 * Checks if a property is set.
 *
 * Returns: %TRUE if the property exists
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_level3d_has_property        (LrgLevel3D             *self,
                                                     const gchar            *key);

/**
 * lrg_level3d_get_property_keys:
 * @self: An #LrgLevel3D
 *
 * Gets all property keys.
 *
 * Returns: (transfer container) (element-type utf8): List of property keys
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_level3d_get_property_keys   (LrgLevel3D             *self);

/* --- Octree Access --- */

/**
 * lrg_level3d_get_octree:
 * @self: An #LrgLevel3D
 *
 * Gets the internal octree for advanced spatial queries.
 *
 * Returns: (transfer none): The internal octree
 */
LRG_AVAILABLE_IN_ALL
LrgOctree *         lrg_level3d_get_octree          (LrgLevel3D             *self);

/**
 * lrg_level3d_rebuild_octree:
 * @self: An #LrgLevel3D
 *
 * Rebuilds the internal octree.
 * Call this after making many model changes.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_rebuild_octree      (LrgLevel3D             *self);

/* --- Utility --- */

/**
 * lrg_level3d_clear:
 * @self: An #LrgLevel3D
 *
 * Removes all content from the level.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_level3d_clear               (LrgLevel3D             *self);

G_END_DECLS
