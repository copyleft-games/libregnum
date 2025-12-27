/* lrg-scene-entity.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scene entity representing a group of related scene objects.
 *
 * LrgSceneEntity groups multiple LrgSceneObject primitives that form
 * a logical unit (e.g., a character with body parts, a tree with trunk
 * and foliage). The entity has its own world-space transform, and child
 * objects use local transforms relative to the entity.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scene-object.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_ENTITY (lrg_scene_entity_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSceneEntity, lrg_scene_entity, LRG, SCENE_ENTITY, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_scene_entity_new:
 * @name: The entity name
 *
 * Creates a new #LrgSceneEntity with default transform.
 *
 * Returns: (transfer full): A new #LrgSceneEntity
 */
LRG_AVAILABLE_IN_ALL
LrgSceneEntity * lrg_scene_entity_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_scene_entity_get_name:
 * @self: an #LrgSceneEntity
 *
 * Gets the name of the entity.
 *
 * Returns: (transfer none): The entity name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scene_entity_get_name (LrgSceneEntity *self);

/**
 * lrg_scene_entity_set_name:
 * @self: an #LrgSceneEntity
 * @name: The new name
 *
 * Sets the name of the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_name (LrgSceneEntity *self,
                                const gchar    *name);

/* ==========================================================================
 * Transform
 * ========================================================================== */

/**
 * lrg_scene_entity_get_location:
 * @self: an #LrgSceneEntity
 *
 * Gets the world position of the entity.
 *
 * Returns: (transfer none): The location vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_entity_get_location (LrgSceneEntity *self);

/**
 * lrg_scene_entity_set_location:
 * @self: an #LrgSceneEntity
 * @location: (transfer none): The location vector
 *
 * Sets the world position of the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_location (LrgSceneEntity *self,
                                    GrlVector3     *location);

/**
 * lrg_scene_entity_set_location_xyz:
 * @self: an #LrgSceneEntity
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Sets the world position using coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_location_xyz (LrgSceneEntity *self,
                                        gfloat          x,
                                        gfloat          y,
                                        gfloat          z);

/**
 * lrg_scene_entity_get_rotation:
 * @self: an #LrgSceneEntity
 *
 * Gets the world rotation of the entity (Euler angles in radians).
 *
 * Returns: (transfer none): The rotation vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_entity_get_rotation (LrgSceneEntity *self);

/**
 * lrg_scene_entity_set_rotation:
 * @self: an #LrgSceneEntity
 * @rotation: (transfer none): The rotation vector (Euler angles in radians)
 *
 * Sets the world rotation of the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_rotation (LrgSceneEntity *self,
                                    GrlVector3     *rotation);

/**
 * lrg_scene_entity_set_rotation_xyz:
 * @self: an #LrgSceneEntity
 * @rx: X rotation in radians
 * @ry: Y rotation in radians
 * @rz: Z rotation in radians
 *
 * Sets the world rotation using Euler angles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_rotation_xyz (LrgSceneEntity *self,
                                        gfloat          rx,
                                        gfloat          ry,
                                        gfloat          rz);

/**
 * lrg_scene_entity_get_scale:
 * @self: an #LrgSceneEntity
 *
 * Gets the world scale of the entity.
 *
 * Returns: (transfer none): The scale vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_entity_get_scale (LrgSceneEntity *self);

/**
 * lrg_scene_entity_set_scale:
 * @self: an #LrgSceneEntity
 * @scale: (transfer none): The scale vector
 *
 * Sets the world scale of the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_scale (LrgSceneEntity *self,
                                 GrlVector3     *scale);

/**
 * lrg_scene_entity_set_scale_xyz:
 * @self: an #LrgSceneEntity
 * @sx: X scale factor
 * @sy: Y scale factor
 * @sz: Z scale factor
 *
 * Sets the world scale using individual factors.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_set_scale_xyz (LrgSceneEntity *self,
                                     gfloat          sx,
                                     gfloat          sy,
                                     gfloat          sz);

/* ==========================================================================
 * Objects
 * ========================================================================== */

/**
 * lrg_scene_entity_add_object:
 * @self: an #LrgSceneEntity
 * @object: (transfer none): The object to add
 *
 * Adds a scene object to the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_add_object (LrgSceneEntity *self,
                                  LrgSceneObject *object);

/**
 * lrg_scene_entity_remove_object:
 * @self: an #LrgSceneEntity
 * @object: The object to remove
 *
 * Removes a scene object from the entity.
 *
 * Returns: %TRUE if the object was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_entity_remove_object (LrgSceneEntity *self,
                                         LrgSceneObject *object);

/**
 * lrg_scene_entity_get_objects:
 * @self: an #LrgSceneEntity
 *
 * Gets all scene objects in the entity.
 *
 * Returns: (transfer none) (element-type LrgSceneObject): The objects array
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_scene_entity_get_objects (LrgSceneEntity *self);

/**
 * lrg_scene_entity_get_object_count:
 * @self: an #LrgSceneEntity
 *
 * Gets the number of objects in the entity.
 *
 * Returns: The object count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_scene_entity_get_object_count (LrgSceneEntity *self);

/**
 * lrg_scene_entity_find_object:
 * @self: an #LrgSceneEntity
 * @name: The object name to find
 *
 * Finds a scene object by name.
 *
 * Returns: (transfer none) (nullable): The object or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgSceneObject * lrg_scene_entity_find_object (LrgSceneEntity *self,
                                               const gchar    *name);

/**
 * lrg_scene_entity_foreach_object:
 * @self: an #LrgSceneEntity
 * @func: (scope call): The function to call for each object
 * @user_data: User data to pass to the function
 *
 * Iterates over all objects in the entity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_entity_foreach_object (LrgSceneEntity *self,
                                      GFunc           func,
                                      gpointer        user_data);

G_END_DECLS
