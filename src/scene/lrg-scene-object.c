/* lrg-scene-object.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scene object representing a single primitive in a scene entity.
 */

#include "lrg-scene-object.h"

/**
 * LrgSceneObject:
 *
 * Represents a single 3D primitive within a scene entity.
 * Stores local transform relative to parent entity, material,
 * and primitive-specific parameters.
 */
struct _LrgSceneObject
{
	GObject parent_instance;

	gchar           *name;
	LrgPrimitiveType primitive;
	GrlVector3      *location;
	GrlVector3      *rotation;
	GrlVector3      *scale;
	LrgMaterial3D   *material;
	GHashTable      *params;     /* gchar* -> GValue* */
};

G_DEFINE_FINAL_TYPE (LrgSceneObject, lrg_scene_object, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_PRIMITIVE,
	PROP_LOCATION,
	PROP_ROTATION,
	PROP_SCALE,
	PROP_MATERIAL,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static void
free_gvalue (gpointer data)
{
	GValue *value = data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_scene_object_finalize (GObject *object)
{
	LrgSceneObject *self = LRG_SCENE_OBJECT (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->location, grl_vector3_free);
	g_clear_pointer (&self->rotation, grl_vector3_free);
	g_clear_pointer (&self->scale, grl_vector3_free);
	g_clear_object (&self->material);
	g_clear_pointer (&self->params, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_scene_object_parent_class)->finalize (object);
}

static void
lrg_scene_object_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgSceneObject *self = LRG_SCENE_OBJECT (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_PRIMITIVE:
		g_value_set_int (value, self->primitive);
		break;
	case PROP_LOCATION:
		g_value_set_boxed (value, self->location);
		break;
	case PROP_ROTATION:
		g_value_set_boxed (value, self->rotation);
		break;
	case PROP_SCALE:
		g_value_set_boxed (value, self->scale);
		break;
	case PROP_MATERIAL:
		g_value_set_object (value, self->material);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_object_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgSceneObject *self = LRG_SCENE_OBJECT (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_clear_pointer (&self->name, g_free);
		self->name = g_value_dup_string (value);
		break;
	case PROP_PRIMITIVE:
		self->primitive = g_value_get_int (value);
		break;
	case PROP_LOCATION:
		g_clear_pointer (&self->location, grl_vector3_free);
		self->location = g_value_dup_boxed (value);
		break;
	case PROP_ROTATION:
		g_clear_pointer (&self->rotation, grl_vector3_free);
		self->rotation = g_value_dup_boxed (value);
		break;
	case PROP_SCALE:
		g_clear_pointer (&self->scale, grl_vector3_free);
		self->scale = g_value_dup_boxed (value);
		break;
	case PROP_MATERIAL:
		g_clear_object (&self->material);
		self->material = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_object_class_init (LrgSceneObjectClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_scene_object_finalize;
	object_class->get_property = lrg_scene_object_get_property;
	object_class->set_property = lrg_scene_object_set_property;

	/**
	 * LrgSceneObject:name:
	 *
	 * The name of this scene object (part name).
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "Object name",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneObject:primitive:
	 *
	 * The primitive type of this scene object.
	 */
	properties[PROP_PRIMITIVE] =
		g_param_spec_int ("primitive",
		                  "Primitive",
		                  "Primitive type",
		                  LRG_PRIMITIVE_PLANE,
		                  LRG_PRIMITIVE_GRID,
		                  LRG_PRIMITIVE_CUBE,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneObject:location:
	 *
	 * The local position of this scene object.
	 */
	properties[PROP_LOCATION] =
		g_param_spec_boxed ("location",
		                    "Location",
		                    "Local position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneObject:rotation:
	 *
	 * The local rotation of this scene object (Euler angles in radians).
	 */
	properties[PROP_ROTATION] =
		g_param_spec_boxed ("rotation",
		                    "Rotation",
		                    "Local rotation (Euler angles)",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneObject:scale:
	 *
	 * The local scale of this scene object.
	 */
	properties[PROP_SCALE] =
		g_param_spec_boxed ("scale",
		                    "Scale",
		                    "Local scale",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneObject:material:
	 *
	 * The material of this scene object.
	 */
	properties[PROP_MATERIAL] =
		g_param_spec_object ("material",
		                     "Material",
		                     "PBR material",
		                     LRG_TYPE_MATERIAL3D,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scene_object_init (LrgSceneObject *self)
{
	self->name      = NULL;
	self->primitive = LRG_PRIMITIVE_CUBE;
	self->location  = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->rotation  = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->scale     = grl_vector3_new (1.0f, 1.0f, 1.0f);
	self->material  = lrg_material3d_new ();
	self->params    = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         g_free, free_gvalue);
}

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
LrgSceneObject *
lrg_scene_object_new (const gchar     *name,
                      LrgPrimitiveType primitive)
{
	return g_object_new (LRG_TYPE_SCENE_OBJECT,
	                     "name", name,
	                     "primitive", primitive,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_scene_object_get_name:
 * @self: an #LrgSceneObject
 *
 * Gets the name of the scene object.
 *
 * Returns: (transfer none): The object name
 */
const gchar *
lrg_scene_object_get_name (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return self->name;
}

/**
 * lrg_scene_object_set_name:
 * @self: an #LrgSceneObject
 * @name: The new name
 *
 * Sets the name of the scene object.
 */
void
lrg_scene_object_set_name (LrgSceneObject *self,
                           const gchar    *name)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));

	g_clear_pointer (&self->name, g_free);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

/**
 * lrg_scene_object_get_primitive:
 * @self: an #LrgSceneObject
 *
 * Gets the primitive type of the scene object.
 *
 * Returns: The primitive type
 */
LrgPrimitiveType
lrg_scene_object_get_primitive (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), LRG_PRIMITIVE_CUBE);

	return self->primitive;
}

/**
 * lrg_scene_object_set_primitive:
 * @self: an #LrgSceneObject
 * @primitive: The primitive type
 *
 * Sets the primitive type of the scene object.
 */
void
lrg_scene_object_set_primitive (LrgSceneObject  *self,
                                LrgPrimitiveType primitive)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));

	if (self->primitive != primitive)
	{
		self->primitive = primitive;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMITIVE]);
	}
}

/* ==========================================================================
 * Transform Accessors
 * ========================================================================== */

/**
 * lrg_scene_object_get_location:
 * @self: an #LrgSceneObject
 *
 * Gets the local position of the scene object.
 *
 * Returns: (transfer none): The location vector
 */
GrlVector3 *
lrg_scene_object_get_location (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return self->location;
}

/**
 * lrg_scene_object_set_location:
 * @self: an #LrgSceneObject
 * @location: (transfer none): The location vector
 *
 * Sets the local position of the scene object.
 */
void
lrg_scene_object_set_location (LrgSceneObject *self,
                               GrlVector3     *location)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (location != NULL);

	g_clear_pointer (&self->location, grl_vector3_free);
	self->location = grl_vector3_copy (location);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCATION]);
}

/**
 * lrg_scene_object_set_location_xyz:
 * @self: an #LrgSceneObject
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Sets the local position using coordinates.
 */
void
lrg_scene_object_set_location_xyz (LrgSceneObject *self,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z)
{
	g_autoptr(GrlVector3) loc = NULL;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));

	loc = grl_vector3_new (x, y, z);
	lrg_scene_object_set_location (self, loc);
}

/**
 * lrg_scene_object_get_rotation:
 * @self: an #LrgSceneObject
 *
 * Gets the local rotation of the scene object.
 *
 * Returns: (transfer none): The rotation vector
 */
GrlVector3 *
lrg_scene_object_get_rotation (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return self->rotation;
}

/**
 * lrg_scene_object_set_rotation:
 * @self: an #LrgSceneObject
 * @rotation: (transfer none): The rotation vector
 *
 * Sets the local rotation of the scene object.
 */
void
lrg_scene_object_set_rotation (LrgSceneObject *self,
                               GrlVector3     *rotation)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (rotation != NULL);

	g_clear_pointer (&self->rotation, grl_vector3_free);
	self->rotation = grl_vector3_copy (rotation);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
}

/**
 * lrg_scene_object_set_rotation_xyz:
 * @self: an #LrgSceneObject
 * @rx: X rotation in radians
 * @ry: Y rotation in radians
 * @rz: Z rotation in radians
 *
 * Sets the local rotation using Euler angles.
 */
void
lrg_scene_object_set_rotation_xyz (LrgSceneObject *self,
                                   gfloat          rx,
                                   gfloat          ry,
                                   gfloat          rz)
{
	g_autoptr(GrlVector3) rot = NULL;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));

	rot = grl_vector3_new (rx, ry, rz);
	lrg_scene_object_set_rotation (self, rot);
}

/**
 * lrg_scene_object_get_scale:
 * @self: an #LrgSceneObject
 *
 * Gets the local scale of the scene object.
 *
 * Returns: (transfer none): The scale vector
 */
GrlVector3 *
lrg_scene_object_get_scale (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return self->scale;
}

/**
 * lrg_scene_object_set_scale:
 * @self: an #LrgSceneObject
 * @scale: (transfer none): The scale vector
 *
 * Sets the local scale of the scene object.
 */
void
lrg_scene_object_set_scale (LrgSceneObject *self,
                            GrlVector3     *scale)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (scale != NULL);

	g_clear_pointer (&self->scale, grl_vector3_free);
	self->scale = grl_vector3_copy (scale);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
}

/**
 * lrg_scene_object_set_scale_xyz:
 * @self: an #LrgSceneObject
 * @sx: X scale factor
 * @sy: Y scale factor
 * @sz: Z scale factor
 *
 * Sets the local scale using individual factors.
 */
void
lrg_scene_object_set_scale_xyz (LrgSceneObject *self,
                                gfloat          sx,
                                gfloat          sy,
                                gfloat          sz)
{
	g_autoptr(GrlVector3) s = NULL;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));

	s = grl_vector3_new (sx, sy, sz);
	lrg_scene_object_set_scale (self, s);
}

/* ==========================================================================
 * Material Accessors
 * ========================================================================== */

/**
 * lrg_scene_object_get_material:
 * @self: an #LrgSceneObject
 *
 * Gets the material of the scene object.
 *
 * Returns: (transfer none): The material
 */
LrgMaterial3D *
lrg_scene_object_get_material (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return self->material;
}

/**
 * lrg_scene_object_set_material:
 * @self: an #LrgSceneObject
 * @material: (transfer none): The material
 *
 * Sets the material of the scene object.
 */
void
lrg_scene_object_set_material (LrgSceneObject *self,
                               LrgMaterial3D  *material)
{
	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (LRG_IS_MATERIAL3D (material));

	if (self->material != material)
	{
		g_clear_object (&self->material);
		self->material = g_object_ref (material);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATERIAL]);
	}
}

/* ==========================================================================
 * Parameter Accessors
 * ========================================================================== */

/**
 * lrg_scene_object_set_param_float:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets a float parameter for the primitive.
 */
void
lrg_scene_object_set_param_float (LrgSceneObject *self,
                                  const gchar    *name,
                                  gfloat          value)
{
	GValue *gval;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (name != NULL);

	gval = g_slice_new0 (GValue);
	g_value_init (gval, G_TYPE_FLOAT);
	g_value_set_float (gval, value);

	g_hash_table_insert (self->params, g_strdup (name), gval);
}

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
gfloat
lrg_scene_object_get_param_float (LrgSceneObject *self,
                                  const gchar    *name,
                                  gfloat          default_value)
{
	GValue *gval;

	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), default_value);
	g_return_val_if_fail (name != NULL, default_value);

	gval = g_hash_table_lookup (self->params, name);
	if (gval == NULL || !G_VALUE_HOLDS_FLOAT (gval))
		return default_value;

	return g_value_get_float (gval);
}

/**
 * lrg_scene_object_set_param_int:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets an integer parameter for the primitive.
 */
void
lrg_scene_object_set_param_int (LrgSceneObject *self,
                                const gchar    *name,
                                gint            value)
{
	GValue *gval;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (name != NULL);

	gval = g_slice_new0 (GValue);
	g_value_init (gval, G_TYPE_INT);
	g_value_set_int (gval, value);

	g_hash_table_insert (self->params, g_strdup (name), gval);
}

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
gint
lrg_scene_object_get_param_int (LrgSceneObject *self,
                                const gchar    *name,
                                gint            default_value)
{
	GValue *gval;

	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), default_value);
	g_return_val_if_fail (name != NULL, default_value);

	gval = g_hash_table_lookup (self->params, name);
	if (gval == NULL || !G_VALUE_HOLDS_INT (gval))
		return default_value;

	return g_value_get_int (gval);
}

/**
 * lrg_scene_object_set_param_bool:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 * @value: Parameter value
 *
 * Sets a boolean parameter for the primitive.
 */
void
lrg_scene_object_set_param_bool (LrgSceneObject *self,
                                 const gchar    *name,
                                 gboolean        value)
{
	GValue *gval;

	g_return_if_fail (LRG_IS_SCENE_OBJECT (self));
	g_return_if_fail (name != NULL);

	gval = g_slice_new0 (GValue);
	g_value_init (gval, G_TYPE_BOOLEAN);
	g_value_set_boolean (gval, value);

	g_hash_table_insert (self->params, g_strdup (name), gval);
}

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
gboolean
lrg_scene_object_get_param_bool (LrgSceneObject *self,
                                 const gchar    *name,
                                 gboolean        default_value)
{
	GValue *gval;

	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), default_value);
	g_return_val_if_fail (name != NULL, default_value);

	gval = g_hash_table_lookup (self->params, name);
	if (gval == NULL || !G_VALUE_HOLDS_BOOLEAN (gval))
		return default_value;

	return g_value_get_boolean (gval);
}

/**
 * lrg_scene_object_has_param:
 * @self: an #LrgSceneObject
 * @name: Parameter name
 *
 * Checks if a parameter is set.
 *
 * Returns: %TRUE if the parameter exists
 */
gboolean
lrg_scene_object_has_param (LrgSceneObject *self,
                            const gchar    *name)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return g_hash_table_contains (self->params, name);
}

/**
 * lrg_scene_object_get_param_names:
 * @self: an #LrgSceneObject
 *
 * Gets the names of all set parameters.
 *
 * Returns: (transfer container) (element-type utf8): List of parameter names
 */
GList *
lrg_scene_object_get_param_names (LrgSceneObject *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (self), NULL);

	return g_hash_table_get_keys (self->params);
}
