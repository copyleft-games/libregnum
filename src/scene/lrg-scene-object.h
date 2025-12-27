/* lrg-scene-object.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scene object representing a single primitive in a scene entity.
 *
 * LrgSceneObject represents a single 3D primitive (sphere, cube, cylinder, etc.)
 * within a scene entity. It stores transform, material, and primitive-specific
 * parameters compatible with the Blender YAML export format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-material3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_OBJECT (lrg_scene_object_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSceneObject, lrg_scene_object, LRG, SCENE_OBJECT, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_scene_object_new:
 * @name: The object name (part name)
 * @primitive: The primitive type
 *
 * Creates a new #LrgSceneObject with default transform and material.
 *
 * Returns: (transfer full): A new #LrgSceneObject
 */
LRG_AVAILABLE_IN_ALL
LrgSceneObject * lrg_scene_object_new (const gchar     *name,
                                       LrgPrimitiveType primitive);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_scene_object_get_name:
 * @self: an #LrgSceneObject
 *
 * Gets the name of the scene object.
 *
 * Returns: (transfer none): The object name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scene_object_get_name (LrgSceneObject *self);

/**
 * lrg_scene_object_set_name:
 * @self: an #LrgSceneObject
 * @name: The new name
 *
 * Sets the name of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_name (LrgSceneObject *self,
                                const gchar    *name);

/**
 * lrg_scene_object_get_primitive:
 * @self: an #LrgSceneObject
 *
 * Gets the primitive type of the scene object.
 *
 * Returns: The primitive type
 */
LRG_AVAILABLE_IN_ALL
LrgPrimitiveType lrg_scene_object_get_primitive (LrgSceneObject *self);

/**
 * lrg_scene_object_set_primitive:
 * @self: an #LrgSceneObject
 * @primitive: The primitive type
 *
 * Sets the primitive type of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_primitive (LrgSceneObject  *self,
                                     LrgPrimitiveType primitive);

/* ==========================================================================
 * Transform
 * ========================================================================== */

/**
 * lrg_scene_object_get_location:
 * @self: an #LrgSceneObject
 *
 * Gets the local position of the scene object.
 *
 * Returns: (transfer none): The location vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_object_get_location (LrgSceneObject *self);

/**
 * lrg_scene_object_set_location:
 * @self: an #LrgSceneObject
 * @location: (transfer none): The location vector
 *
 * Sets the local position of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_location (LrgSceneObject *self,
                                    GrlVector3     *location);

/**
 * lrg_scene_object_set_location_xyz:
 * @self: an #LrgSceneObject
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Sets the local position using coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_location_xyz (LrgSceneObject *self,
                                        gfloat          x,
                                        gfloat          y,
                                        gfloat          z);

/**
 * lrg_scene_object_get_rotation:
 * @self: an #LrgSceneObject
 *
 * Gets the local rotation of the scene object (Euler angles in radians).
 *
 * Returns: (transfer none): The rotation vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_object_get_rotation (LrgSceneObject *self);

/**
 * lrg_scene_object_set_rotation:
 * @self: an #LrgSceneObject
 * @rotation: (transfer none): The rotation vector (Euler angles in radians)
 *
 * Sets the local rotation of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_rotation (LrgSceneObject *self,
                                    GrlVector3     *rotation);

/**
 * lrg_scene_object_set_rotation_xyz:
 * @self: an #LrgSceneObject
 * @rx: X rotation (pitch) in radians
 * @ry: Y rotation (yaw) in radians
 * @rz: Z rotation (roll) in radians
 *
 * Sets the local rotation using Euler angles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_rotation_xyz (LrgSceneObject *self,
                                        gfloat          rx,
                                        gfloat          ry,
                                        gfloat          rz);

/**
 * lrg_scene_object_get_scale:
 * @self: an #LrgSceneObject
 *
 * Gets the local scale of the scene object.
 *
 * Returns: (transfer none): The scale vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_scene_object_get_scale (LrgSceneObject *self);

/**
 * lrg_scene_object_set_scale:
 * @self: an #LrgSceneObject
 * @scale: (transfer none): The scale vector
 *
 * Sets the local scale of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_scale (LrgSceneObject *self,
                                 GrlVector3     *scale);

/**
 * lrg_scene_object_set_scale_xyz:
 * @self: an #LrgSceneObject
 * @sx: X scale factor
 * @sy: Y scale factor
 * @sz: Z scale factor
 *
 * Sets the local scale using individual factors.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_scale_xyz (LrgSceneObject *self,
                                     gfloat          sx,
                                     gfloat          sy,
                                     gfloat          sz);

/* ==========================================================================
 * Material
 * ========================================================================== */

/**
 * lrg_scene_object_get_material:
 * @self: an #LrgSceneObject
 *
 * Gets the material of the scene object.
 *
 * Returns: (transfer none): The material
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial3D * lrg_scene_object_get_material (LrgSceneObject *self);

/**
 * lrg_scene_object_set_material:
 * @self: an #LrgSceneObject
 * @material: (transfer none): The material
 *
 * Sets the material of the scene object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_material (LrgSceneObject *self,
                                    LrgMaterial3D  *material);

/* ==========================================================================
 * Primitive Parameters
 * ========================================================================== */

/**
 * lrg_scene_object_set_param_float:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets a float parameter for the primitive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_param_float (LrgSceneObject *self,
                                       const gchar    *name,
                                       gfloat          value);

/**
 * lrg_scene_object_get_param_float:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @default_value: Value to return if parameter not set
 *
 * Gets a float parameter for the primitive.
 *
 * Returns: The parameter value or default_value if not set
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_scene_object_get_param_float (LrgSceneObject *self,
                                         const gchar    *name,
                                         gfloat          default_value);

/**
 * lrg_scene_object_set_param_int:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets an integer parameter for the primitive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_param_int (LrgSceneObject *self,
                                     const gchar    *name,
                                     gint            value);

/**
 * lrg_scene_object_get_param_int:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @default_value: Value to return if parameter not set
 *
 * Gets an integer parameter for the primitive.
 *
 * Returns: The parameter value or default_value if not set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_scene_object_get_param_int (LrgSceneObject *self,
                                     const gchar    *name,
                                     gint            default_value);

/**
 * lrg_scene_object_set_param_bool:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets a boolean parameter for the primitive.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_object_set_param_bool (LrgSceneObject *self,
                                      const gchar    *name,
                                      gboolean        value);

/**
 * lrg_scene_object_get_param_bool:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @default_value: Value to return if parameter not set
 *
 * Gets a boolean parameter for the primitive.
 *
 * Returns: The parameter value or default_value if not set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_object_get_param_bool (LrgSceneObject *self,
                                          const gchar    *name,
                                          gboolean        default_value);

/**
 * lrg_scene_object_has_param:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 *
 * Checks if a parameter is set.
 *
 * Returns: %TRUE if the parameter exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_object_has_param (LrgSceneObject *self,
                                     const gchar    *name);

/**
 * lrg_scene_object_get_param_names:
 * @self: an #LrgSceneObject
 *
 * Gets the names of all set parameters.
 *
 * Returns: (transfer container) (element-type utf8): List of parameter names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_scene_object_get_param_names (LrgSceneObject *self);

G_END_DECLS
