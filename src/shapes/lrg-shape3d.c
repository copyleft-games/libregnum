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
 * @wireframe: Whether to render in wireframe mode
 *
 * Private data for #LrgShape3D.
 */
typedef struct
{
	GrlVector3 *position;
	gboolean    wireframe;
} LrgShape3DPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgShape3D, lrg_shape3d, LRG_TYPE_SHAPE)

enum
{
	PROP_0,
	PROP_POSITION,
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
