/* lrg-scene-entity.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Scene entity representing a group of related scene objects.
 */

#include "lrg-scene-entity.h"

/**
 * LrgSceneEntity:
 *
 * Groups multiple LrgSceneObject primitives that form a logical unit.
 * Corresponds to entities in the Blender YAML export format, where
 * objects are grouped by the entity prefix in their names.
 */
struct _LrgSceneEntity
{
	GObject parent_instance;

	gchar      *name;
	GrlVector3 *location;
	GrlVector3 *rotation;
	GrlVector3 *scale;
	GPtrArray  *objects;    /* LrgSceneObject* */
};

G_DEFINE_FINAL_TYPE (LrgSceneEntity, lrg_scene_entity, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_LOCATION,
	PROP_ROTATION,
	PROP_SCALE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_scene_entity_finalize (GObject *object)
{
	LrgSceneEntity *self = LRG_SCENE_ENTITY (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->location, grl_vector3_free);
	g_clear_pointer (&self->rotation, grl_vector3_free);
	g_clear_pointer (&self->scale, grl_vector3_free);
	g_clear_pointer (&self->objects, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_scene_entity_parent_class)->finalize (object);
}

static void
lrg_scene_entity_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgSceneEntity *self = LRG_SCENE_ENTITY (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_entity_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgSceneEntity *self = LRG_SCENE_ENTITY (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_clear_pointer (&self->name, g_free);
		self->name = g_value_dup_string (value);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_entity_class_init (LrgSceneEntityClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_scene_entity_finalize;
	object_class->get_property = lrg_scene_entity_get_property;
	object_class->set_property = lrg_scene_entity_set_property;

	/**
	 * LrgSceneEntity:name:
	 *
	 * The name of this entity.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "Entity name",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneEntity:location:
	 *
	 * The world position of this entity.
	 */
	properties[PROP_LOCATION] =
		g_param_spec_boxed ("location",
		                    "Location",
		                    "World position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneEntity:rotation:
	 *
	 * The world rotation of this entity (Euler angles in radians).
	 */
	properties[PROP_ROTATION] =
		g_param_spec_boxed ("rotation",
		                    "Rotation",
		                    "World rotation (Euler angles)",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSceneEntity:scale:
	 *
	 * The world scale of this entity.
	 */
	properties[PROP_SCALE] =
		g_param_spec_boxed ("scale",
		                    "Scale",
		                    "World scale",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scene_entity_init (LrgSceneEntity *self)
{
	self->name     = NULL;
	self->location = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->rotation = grl_vector3_new (0.0f, 0.0f, 0.0f);
	self->scale    = grl_vector3_new (1.0f, 1.0f, 1.0f);
	self->objects  = g_ptr_array_new_with_free_func (g_object_unref);
}

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
LrgSceneEntity *
lrg_scene_entity_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_SCENE_ENTITY,
	                     "name", name,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_scene_entity_get_name:
 * @self: an #LrgSceneEntity
 *
 * Gets the name of the entity.
 *
 * Returns: (transfer none): The entity name
 */
const gchar *
lrg_scene_entity_get_name (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);

	return self->name;
}

/**
 * lrg_scene_entity_set_name:
 * @self: an #LrgSceneEntity
 * @name: The new name
 *
 * Sets the name of the entity.
 */
void
lrg_scene_entity_set_name (LrgSceneEntity *self,
                           const gchar    *name)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));

	g_clear_pointer (&self->name, g_free);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

/* ==========================================================================
 * Transform Accessors
 * ========================================================================== */

/**
 * lrg_scene_entity_get_location:
 * @self: an #LrgSceneEntity
 *
 * Gets the world position of the entity.
 *
 * Returns: (transfer none): The location vector
 */
GrlVector3 *
lrg_scene_entity_get_location (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);

	return self->location;
}

/**
 * lrg_scene_entity_set_location:
 * @self: an #LrgSceneEntity
 * @location: (transfer none): The location vector
 *
 * Sets the world position of the entity.
 */
void
lrg_scene_entity_set_location (LrgSceneEntity *self,
                               GrlVector3     *location)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));
	g_return_if_fail (location != NULL);

	g_clear_pointer (&self->location, grl_vector3_free);
	self->location = grl_vector3_copy (location);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCATION]);
}

/**
 * lrg_scene_entity_set_location_xyz:
 * @self: an #LrgSceneEntity
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Sets the world position using coordinates.
 */
void
lrg_scene_entity_set_location_xyz (LrgSceneEntity *self,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z)
{
	g_autoptr(GrlVector3) loc = NULL;

	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));

	loc = grl_vector3_new (x, y, z);
	lrg_scene_entity_set_location (self, loc);
}

/**
 * lrg_scene_entity_get_rotation:
 * @self: an #LrgSceneEntity
 *
 * Gets the world rotation of the entity.
 *
 * Returns: (transfer none): The rotation vector
 */
GrlVector3 *
lrg_scene_entity_get_rotation (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);

	return self->rotation;
}

/**
 * lrg_scene_entity_set_rotation:
 * @self: an #LrgSceneEntity
 * @rotation: (transfer none): The rotation vector
 *
 * Sets the world rotation of the entity.
 */
void
lrg_scene_entity_set_rotation (LrgSceneEntity *self,
                               GrlVector3     *rotation)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));
	g_return_if_fail (rotation != NULL);

	g_clear_pointer (&self->rotation, grl_vector3_free);
	self->rotation = grl_vector3_copy (rotation);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
}

/**
 * lrg_scene_entity_set_rotation_xyz:
 * @self: an #LrgSceneEntity
 * @rx: X rotation in radians
 * @ry: Y rotation in radians
 * @rz: Z rotation in radians
 *
 * Sets the world rotation using Euler angles.
 */
void
lrg_scene_entity_set_rotation_xyz (LrgSceneEntity *self,
                                   gfloat          rx,
                                   gfloat          ry,
                                   gfloat          rz)
{
	g_autoptr(GrlVector3) rot = NULL;

	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));

	rot = grl_vector3_new (rx, ry, rz);
	lrg_scene_entity_set_rotation (self, rot);
}

/**
 * lrg_scene_entity_get_scale:
 * @self: an #LrgSceneEntity
 *
 * Gets the world scale of the entity.
 *
 * Returns: (transfer none): The scale vector
 */
GrlVector3 *
lrg_scene_entity_get_scale (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);

	return self->scale;
}

/**
 * lrg_scene_entity_set_scale:
 * @self: an #LrgSceneEntity
 * @scale: (transfer none): The scale vector
 *
 * Sets the world scale of the entity.
 */
void
lrg_scene_entity_set_scale (LrgSceneEntity *self,
                            GrlVector3     *scale)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));
	g_return_if_fail (scale != NULL);

	g_clear_pointer (&self->scale, grl_vector3_free);
	self->scale = grl_vector3_copy (scale);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
}

/**
 * lrg_scene_entity_set_scale_xyz:
 * @self: an #LrgSceneEntity
 * @sx: X scale factor
 * @sy: Y scale factor
 * @sz: Z scale factor
 *
 * Sets the world scale using individual factors.
 */
void
lrg_scene_entity_set_scale_xyz (LrgSceneEntity *self,
                                gfloat          sx,
                                gfloat          sy,
                                gfloat          sz)
{
	g_autoptr(GrlVector3) s = NULL;

	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));

	s = grl_vector3_new (sx, sy, sz);
	lrg_scene_entity_set_scale (self, s);
}

/* ==========================================================================
 * Object Management
 * ========================================================================== */

/**
 * lrg_scene_entity_add_object:
 * @self: an #LrgSceneEntity
 * @object: (transfer none): The object to add
 *
 * Adds a scene object to the entity.
 */
void
lrg_scene_entity_add_object (LrgSceneEntity *self,
                             LrgSceneObject *object)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));
	g_return_if_fail (LRG_IS_SCENE_OBJECT (object));

	g_ptr_array_add (self->objects, g_object_ref (object));
}

/**
 * lrg_scene_entity_remove_object:
 * @self: an #LrgSceneEntity
 * @object: The object to remove
 *
 * Removes a scene object from the entity.
 *
 * Returns: %TRUE if the object was found and removed
 */
gboolean
lrg_scene_entity_remove_object (LrgSceneEntity *self,
                                LrgSceneObject *object)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), FALSE);
	g_return_val_if_fail (LRG_IS_SCENE_OBJECT (object), FALSE);

	return g_ptr_array_remove (self->objects, object);
}

/**
 * lrg_scene_entity_get_objects:
 * @self: an #LrgSceneEntity
 *
 * Gets all scene objects in the entity.
 *
 * Returns: (transfer none) (element-type LrgSceneObject): The objects array
 */
GPtrArray *
lrg_scene_entity_get_objects (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);

	return self->objects;
}

/**
 * lrg_scene_entity_get_object_count:
 * @self: an #LrgSceneEntity
 *
 * Gets the number of objects in the entity.
 *
 * Returns: The object count
 */
guint
lrg_scene_entity_get_object_count (LrgSceneEntity *self)
{
	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), 0);

	return self->objects->len;
}

/**
 * lrg_scene_entity_find_object:
 * @self: an #LrgSceneEntity
 * @name: The object name to find
 *
 * Finds a scene object by name.
 *
 * Returns: (transfer none) (nullable): The object or %NULL if not found
 */
LrgSceneObject *
lrg_scene_entity_find_object (LrgSceneEntity *self,
                              const gchar    *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SCENE_ENTITY (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (i = 0; i < self->objects->len; i++)
	{
		LrgSceneObject *obj = g_ptr_array_index (self->objects, i);
		const gchar    *obj_name = lrg_scene_object_get_name (obj);

		if (obj_name != NULL && g_strcmp0 (obj_name, name) == 0)
			return obj;
	}

	return NULL;
}

/**
 * lrg_scene_entity_foreach_object:
 * @self: an #LrgSceneEntity
 * @func: (scope call): The function to call for each object
 * @user_data: User data to pass to the function
 *
 * Iterates over all objects in the entity.
 */
void
lrg_scene_entity_foreach_object (LrgSceneEntity *self,
                                 GFunc           func,
                                 gpointer        user_data)
{
	g_return_if_fail (LRG_IS_SCENE_ENTITY (self));
	g_return_if_fail (func != NULL);

	g_ptr_array_foreach (self->objects, func, user_data);
}
