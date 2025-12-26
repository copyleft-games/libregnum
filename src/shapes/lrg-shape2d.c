/* lrg-shape2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 2D shapes.
 */

#include "lrg-shape2d.h"

/**
 * LrgShape2DPrivate:
 * @x: The shape's X position (screen space)
 * @y: The shape's Y position (screen space)
 *
 * Private data for #LrgShape2D.
 */
typedef struct
{
	gfloat x;
	gfloat y;
} LrgShape2DPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgShape2D, lrg_shape2d, LRG_TYPE_SHAPE)

enum
{
	PROP_0,
	PROP_X,
	PROP_Y,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_shape2d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgShape2D        *self = LRG_SHAPE2D (object);
	LrgShape2DPrivate *priv = lrg_shape2d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_X:
		g_value_set_float (value, priv->x);
		break;
	case PROP_Y:
		g_value_set_float (value, priv->y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape2d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgShape2D        *self = LRG_SHAPE2D (object);
	LrgShape2DPrivate *priv = lrg_shape2d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_X:
		priv->x = g_value_get_float (value);
		break;
	case PROP_Y:
		priv->y = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape2d_class_init (LrgShape2DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = lrg_shape2d_get_property;
	object_class->set_property = lrg_shape2d_set_property;

	/**
	 * LrgShape2D:x:
	 *
	 * The shape's X position (screen space).
	 */
	properties[PROP_X] =
		g_param_spec_float ("x",
		                    "X",
		                    "The shape's X position (screen space)",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape2D:y:
	 *
	 * The shape's Y position (screen space).
	 */
	properties[PROP_Y] =
		g_param_spec_float ("y",
		                    "Y",
		                    "The shape's Y position (screen space)",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_shape2d_init (LrgShape2D *self)
{
	LrgShape2DPrivate *priv = lrg_shape2d_get_instance_private (self);

	priv->x = 0.0f;
	priv->y = 0.0f;
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_shape2d_get_x:
 * @self: an #LrgShape2D
 *
 * Gets the shape's X position (screen space).
 *
 * Returns: The X coordinate
 */
gfloat
lrg_shape2d_get_x (LrgShape2D *self)
{
	LrgShape2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE2D (self), 0.0f);

	priv = lrg_shape2d_get_instance_private (self);
	return priv->x;
}

/**
 * lrg_shape2d_set_x:
 * @self: an #LrgShape2D
 * @x: the X coordinate
 *
 * Sets the shape's X position (screen space).
 */
void
lrg_shape2d_set_x (LrgShape2D *self,
                   gfloat      x)
{
	LrgShape2DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE2D (self));

	priv = lrg_shape2d_get_instance_private (self);

	if (priv->x != x)
	{
		priv->x = x;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
	}
}

/**
 * lrg_shape2d_get_y:
 * @self: an #LrgShape2D
 *
 * Gets the shape's Y position (screen space).
 *
 * Returns: The Y coordinate
 */
gfloat
lrg_shape2d_get_y (LrgShape2D *self)
{
	LrgShape2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE2D (self), 0.0f);

	priv = lrg_shape2d_get_instance_private (self);
	return priv->y;
}

/**
 * lrg_shape2d_set_y:
 * @self: an #LrgShape2D
 * @y: the Y coordinate
 *
 * Sets the shape's Y position (screen space).
 */
void
lrg_shape2d_set_y (LrgShape2D *self,
                   gfloat      y)
{
	LrgShape2DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE2D (self));

	priv = lrg_shape2d_get_instance_private (self);

	if (priv->y != y)
	{
		priv->y = y;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
	}
}

/**
 * lrg_shape2d_set_position:
 * @self: an #LrgShape2D
 * @x: the X coordinate
 * @y: the Y coordinate
 *
 * Sets the shape's position using X, Y coordinates.
 */
void
lrg_shape2d_set_position (LrgShape2D *self,
                          gfloat      x,
                          gfloat      y)
{
	LrgShape2DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE2D (self));

	priv = lrg_shape2d_get_instance_private (self);

	g_object_freeze_notify (G_OBJECT (self));

	if (priv->x != x)
	{
		priv->x = x;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
	}
	if (priv->y != y)
	{
		priv->y = y;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
	}

	g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_shape2d_get_position:
 * @self: an #LrgShape2D
 * @x: (out) (optional): return location for X coordinate
 * @y: (out) (optional): return location for Y coordinate
 *
 * Gets the shape's position.
 */
void
lrg_shape2d_get_position (LrgShape2D *self,
                          gfloat     *x,
                          gfloat     *y)
{
	LrgShape2DPrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE2D (self));

	priv = lrg_shape2d_get_instance_private (self);

	if (x != NULL)
		*x = priv->x;
	if (y != NULL)
		*y = priv->y;
}
