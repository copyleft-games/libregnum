/* lrg-shape3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 3D shapes.
 */

#include "lrg-shape3d.h"

/**
 * LrgShape3DPrivate:
 * @position: The shape's 3D position
 * @rotation: The shape's rotation (Euler angles in radians)
 * @scale: The shape's scale factors
 * @wireframe: Whether to render in wireframe mode
 *
 * Private data for #LrgShape3D.
 */
typedef struct
{
	GrlVector3 *position;
	GrlVector3 *rotation;
	GrlVector3 *scale;
	gboolean    wireframe;
} LrgShape3DPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgShape3D, lrg_shape3d, LRG_TYPE_SHAPE)

enum
{
	PROP_0,
	PROP_POSITION,
	PROP_ROTATION,
	PROP_SCALE,
	PROP_WIREFRAME,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_shape3d_finalize (GObject *object)
{
	LrgShape3D        *self = LRG_SHAPE3D (object);
	LrgShape3DPrivate *priv = lrg_shape3d_get_instance_private (self);

	g_clear_pointer (&priv->position, grl_vector3_free);
	g_clear_pointer (&priv->rotation, grl_vector3_free);
	g_clear_pointer (&priv->scale, grl_vector3_free);

	G_OBJECT_CLASS (lrg_shape3d_parent_class)->finalize (object);
}

static void
lrg_shape3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgShape3D        *self = LRG_SHAPE3D (object);
	LrgShape3DPrivate *priv = lrg_shape3d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_POSITION:
		g_value_set_boxed (value, priv->position);
		break;
	case PROP_ROTATION:
		g_value_set_boxed (value, priv->rotation);
		break;
	case PROP_SCALE:
		g_value_set_boxed (value, priv->scale);
		break;
	case PROP_WIREFRAME:
		g_value_set_boolean (value, priv->wireframe);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgShape3D        *self = LRG_SHAPE3D (object);
	LrgShape3DPrivate *priv = lrg_shape3d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_POSITION:
		g_clear_pointer (&priv->position, grl_vector3_free);
		priv->position = g_value_dup_boxed (value);
		break;
	case PROP_ROTATION:
		g_clear_pointer (&priv->rotation, grl_vector3_free);
		priv->rotation = g_value_dup_boxed (value);
		break;
	case PROP_SCALE:
		g_clear_pointer (&priv->scale, grl_vector3_free);
		priv->scale = g_value_dup_boxed (value);
		break;
	case PROP_WIREFRAME:
		priv->wireframe = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape3d_class_init (LrgShape3DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_shape3d_finalize;
	object_class->get_property = lrg_shape3d_get_property;
	object_class->set_property = lrg_shape3d_set_property;

	/**
	 * LrgShape3D:position:
	 *
	 * The shape's 3D position.
	 */
	properties[PROP_POSITION] =
		g_param_spec_boxed ("position",
		                    "Position",
		                    "The shape's 3D position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape3D:rotation:
	 *
	 * The shape's rotation as Euler angles in radians.
	 * The vector components represent: x=pitch, y=yaw, z=roll.
	 */
	properties[PROP_ROTATION] =
		g_param_spec_boxed ("rotation",
		                    "Rotation",
		                    "The shape's rotation (Euler angles in radians)",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape3D:scale:
	 *
	 * The shape's scale factors for each axis.
	 * Default is (1.0, 1.0, 1.0) for no scaling.
	 */
	properties[PROP_SCALE] =
		g_param_spec_boxed ("scale",
		                    "Scale",
		                    "The shape's scale factors",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape3D:wireframe:
	 *
	 * Whether to render in wireframe mode.
	 */
	properties[PROP_WIREFRAME] =
		g_param_spec_boolean ("wireframe",
		                      "Wireframe",
		                      "Whether to render in wireframe mode",
		                      FALSE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_shape3d_init (LrgShape3D *self)
{
	LrgShape3DPrivate *priv = lrg_shape3d_get_instance_private (self);

	priv->position  = grl_vector3_new (0.0f, 0.0f, 0.0f);
	priv->rotation  = grl_vector3_new (0.0f, 0.0f, 0.0f);
	priv->scale     = grl_vector3_new (1.0f, 1.0f, 1.0f);
	priv->wireframe = FALSE;
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_shape3d_get_position:
 * @self: an #LrgShape3D
 *
 * Gets the shape's 3D position.
 *
 * Returns: (transfer none): The position vector
 */
GrlVector3 *
lrg_shape3d_get_position (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), NULL);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->position;
}

/**
 * lrg_shape3d_set_position:
 * @self: an #LrgShape3D
 * @position: (transfer none): the position to set
 *
 * Sets the shape's 3D position.
 */
void
lrg_shape3d_set_position (LrgShape3D *self,
                          GrlVector3 *position)
{
	LrgShape3DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE3D (self));
	g_return_if_fail (position != NULL);

	priv = lrg_shape3d_get_instance_private (self);

	g_clear_pointer (&priv->position, grl_vector3_free);
	priv->position = grl_vector3_copy (position);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION]);
}

/**
 * lrg_shape3d_set_position_xyz:
 * @self: an #LrgShape3D
 * @x: the X coordinate
 * @y: the Y coordinate
 * @z: the Z coordinate
 *
 * Sets the shape's position using X, Y, Z coordinates.
 */
void
lrg_shape3d_set_position_xyz (LrgShape3D *self,
                              gfloat      x,
                              gfloat      y,
                              gfloat      z)
{
	LrgShape3DPrivate    *priv;
	g_autoptr(GrlVector3) new_pos = NULL;

	g_return_if_fail (LRG_IS_SHAPE3D (self));

	priv = lrg_shape3d_get_instance_private (self);

	new_pos = grl_vector3_new (x, y, z);
	g_clear_pointer (&priv->position, grl_vector3_free);
	priv->position = g_steal_pointer (&new_pos);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION]);
}

/**
 * lrg_shape3d_get_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X position.
 *
 * Returns: The X coordinate
 */
gfloat
lrg_shape3d_get_x (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->position->x;
}

/**
 * lrg_shape3d_get_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y position.
 *
 * Returns: The Y coordinate
 */
gfloat
lrg_shape3d_get_y (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->position->y;
}

/**
 * lrg_shape3d_get_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z position.
 *
 * Returns: The Z coordinate
 */
gfloat
lrg_shape3d_get_z (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->position->z;
}

/**
 * lrg_shape3d_get_wireframe:
 * @self: an #LrgShape3D
 *
 * Gets whether the shape is rendered in wireframe mode.
 *
 * Returns: %TRUE if wireframe mode is enabled
 */
gboolean
lrg_shape3d_get_wireframe (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), FALSE);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->wireframe;
}

/**
 * lrg_shape3d_set_wireframe:
 * @self: an #LrgShape3D
 * @wireframe: whether to enable wireframe mode
 *
 * Sets whether the shape is rendered in wireframe mode.
 */
void
lrg_shape3d_set_wireframe (LrgShape3D *self,
                           gboolean    wireframe)
{
	LrgShape3DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE3D (self));

	priv = lrg_shape3d_get_instance_private (self);

	if (priv->wireframe != wireframe)
	{
		priv->wireframe = wireframe;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIREFRAME]);
	}
}

/* ==========================================================================
 * Rotation Accessors
 * ========================================================================== */

/**
 * lrg_shape3d_get_rotation:
 * @self: an #LrgShape3D
 *
 * Gets the shape's rotation as Euler angles in radians.
 *
 * Returns: (transfer none): The rotation vector (x=pitch, y=yaw, z=roll)
 */
GrlVector3 *
lrg_shape3d_get_rotation (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), NULL);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->rotation;
}

/**
 * lrg_shape3d_set_rotation:
 * @self: an #LrgShape3D
 * @rotation: (transfer none): the rotation to set (Euler angles in radians)
 *
 * Sets the shape's rotation using a vector of Euler angles.
 */
void
lrg_shape3d_set_rotation (LrgShape3D *self,
                          GrlVector3 *rotation)
{
	LrgShape3DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE3D (self));
	g_return_if_fail (rotation != NULL);

	priv = lrg_shape3d_get_instance_private (self);

	g_clear_pointer (&priv->rotation, grl_vector3_free);
	priv->rotation = grl_vector3_copy (rotation);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
}

/**
 * lrg_shape3d_set_rotation_xyz:
 * @self: an #LrgShape3D
 * @rx: the X rotation (pitch) in radians
 * @ry: the Y rotation (yaw) in radians
 * @rz: the Z rotation (roll) in radians
 *
 * Sets the shape's rotation using individual Euler angles in radians.
 */
void
lrg_shape3d_set_rotation_xyz (LrgShape3D *self,
                              gfloat      rx,
                              gfloat      ry,
                              gfloat      rz)
{
	LrgShape3DPrivate    *priv;
	g_autoptr(GrlVector3) new_rot = NULL;

	g_return_if_fail (LRG_IS_SHAPE3D (self));

	priv = lrg_shape3d_get_instance_private (self);

	new_rot = grl_vector3_new (rx, ry, rz);
	g_clear_pointer (&priv->rotation, grl_vector3_free);
	priv->rotation = g_steal_pointer (&new_rot);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
}

/**
 * lrg_shape3d_get_rotation_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X rotation (pitch) in radians.
 *
 * Returns: The X rotation in radians
 */
gfloat
lrg_shape3d_get_rotation_x (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->rotation->x;
}

/**
 * lrg_shape3d_get_rotation_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y rotation (yaw) in radians.
 *
 * Returns: The Y rotation in radians
 */
gfloat
lrg_shape3d_get_rotation_y (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->rotation->y;
}

/**
 * lrg_shape3d_get_rotation_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z rotation (roll) in radians.
 *
 * Returns: The Z rotation in radians
 */
gfloat
lrg_shape3d_get_rotation_z (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 0.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->rotation->z;
}

/* ==========================================================================
 * Scale Accessors
 * ========================================================================== */

/**
 * lrg_shape3d_get_scale:
 * @self: an #LrgShape3D
 *
 * Gets the shape's scale factors.
 *
 * Returns: (transfer none): The scale vector
 */
GrlVector3 *
lrg_shape3d_get_scale (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), NULL);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->scale;
}

/**
 * lrg_shape3d_set_scale:
 * @self: an #LrgShape3D
 * @scale: (transfer none): the scale to set
 *
 * Sets the shape's scale using a vector.
 */
void
lrg_shape3d_set_scale (LrgShape3D *self,
                       GrlVector3 *scale)
{
	LrgShape3DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE3D (self));
	g_return_if_fail (scale != NULL);

	priv = lrg_shape3d_get_instance_private (self);

	g_clear_pointer (&priv->scale, grl_vector3_free);
	priv->scale = grl_vector3_copy (scale);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
}

/**
 * lrg_shape3d_set_scale_xyz:
 * @self: an #LrgShape3D
 * @sx: the X scale factor
 * @sy: the Y scale factor
 * @sz: the Z scale factor
 *
 * Sets the shape's scale using individual factors.
 */
void
lrg_shape3d_set_scale_xyz (LrgShape3D *self,
                           gfloat      sx,
                           gfloat      sy,
                           gfloat      sz)
{
	LrgShape3DPrivate    *priv;
	g_autoptr(GrlVector3) new_scale = NULL;

	g_return_if_fail (LRG_IS_SHAPE3D (self));

	priv = lrg_shape3d_get_instance_private (self);

	new_scale = grl_vector3_new (sx, sy, sz);
	g_clear_pointer (&priv->scale, grl_vector3_free);
	priv->scale = g_steal_pointer (&new_scale);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
}

/**
 * lrg_shape3d_set_scale_uniform:
 * @self: an #LrgShape3D
 * @scale: the uniform scale factor
 *
 * Sets the shape's scale uniformly on all axes.
 */
void
lrg_shape3d_set_scale_uniform (LrgShape3D *self,
                               gfloat      scale)
{
	lrg_shape3d_set_scale_xyz (self, scale, scale, scale);
}

/**
 * lrg_shape3d_get_scale_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X scale factor.
 *
 * Returns: The X scale factor
 */
gfloat
lrg_shape3d_get_scale_x (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 1.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->scale->x;
}

/**
 * lrg_shape3d_get_scale_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y scale factor.
 *
 * Returns: The Y scale factor
 */
gfloat
lrg_shape3d_get_scale_y (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 1.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->scale->y;
}

/**
 * lrg_shape3d_get_scale_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z scale factor.
 *
 * Returns: The Z scale factor
 */
gfloat
lrg_shape3d_get_scale_z (LrgShape3D *self)
{
	LrgShape3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE3D (self), 1.0f);

	priv = lrg_shape3d_get_instance_private (self);
	return priv->scale->z;
}
