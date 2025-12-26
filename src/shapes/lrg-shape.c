/* lrg-shape.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for all drawable shapes.
 */

#include "lrg-shape.h"

/**
 * LrgShapePrivate:
 * @visible: Whether the shape should be rendered
 * @color: The shape's color
 * @z_index: Draw ordering index
 *
 * Private data for #LrgShape.
 */
typedef struct
{
	gboolean  visible;
	GrlColor *color;
	gint      z_index;
} LrgShapePrivate;

static void lrg_drawable_iface_init (LrgDrawableInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (LrgShape, lrg_shape, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (LrgShape)
                                  G_IMPLEMENT_INTERFACE (LRG_TYPE_DRAWABLE,
                                                         lrg_drawable_iface_init))

enum
{
	PROP_0,
	PROP_VISIBLE,
	PROP_COLOR,
	PROP_Z_INDEX,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * LrgDrawable Interface Implementation
 * ========================================================================== */

static void
lrg_shape_drawable_draw (LrgDrawable *drawable,
                         gfloat       delta)
{
	LrgShape        *self = LRG_SHAPE (drawable);
	LrgShapePrivate *priv = lrg_shape_get_instance_private (self);
	LrgShapeClass   *klass;

	/* Don't draw if not visible */
	if (!priv->visible)
		return;

	klass = LRG_SHAPE_GET_CLASS (self);
	if (klass->draw != NULL)
		klass->draw (self, delta);
}

static void
lrg_shape_drawable_get_bounds (LrgDrawable  *drawable,
                               GrlRectangle *out_bounds)
{
	LrgShape      *self = LRG_SHAPE (drawable);
	LrgShapeClass *klass;

	klass = LRG_SHAPE_GET_CLASS (self);
	if (klass->get_bounds != NULL)
		klass->get_bounds (self, out_bounds);
	else
	{
		/* Default: zero bounds */
		if (out_bounds != NULL)
		{
			out_bounds->x      = 0.0f;
			out_bounds->y      = 0.0f;
			out_bounds->width  = 0.0f;
			out_bounds->height = 0.0f;
		}
	}
}

static void
lrg_drawable_iface_init (LrgDrawableInterface *iface)
{
	iface->draw       = lrg_shape_drawable_draw;
	iface->get_bounds = lrg_shape_drawable_get_bounds;
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_shape_finalize (GObject *object)
{
	LrgShape        *self = LRG_SHAPE (object);
	LrgShapePrivate *priv = lrg_shape_get_instance_private (self);

	g_clear_pointer (&priv->color, grl_color_free);

	G_OBJECT_CLASS (lrg_shape_parent_class)->finalize (object);
}

static void
lrg_shape_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgShape        *self = LRG_SHAPE (object);
	LrgShapePrivate *priv = lrg_shape_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_COLOR:
		g_value_set_boxed (value, priv->color);
		break;
	case PROP_Z_INDEX:
		g_value_set_int (value, priv->z_index);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgShape        *self = LRG_SHAPE (object);
	LrgShapePrivate *priv = lrg_shape_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_VISIBLE:
		priv->visible = g_value_get_boolean (value);
		break;
	case PROP_COLOR:
		g_clear_pointer (&priv->color, grl_color_free);
		priv->color = g_value_dup_boxed (value);
		break;
	case PROP_Z_INDEX:
		priv->z_index = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_shape_class_init (LrgShapeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_shape_finalize;
	object_class->get_property = lrg_shape_get_property;
	object_class->set_property = lrg_shape_set_property;

	/* Default virtual method implementations */
	klass->draw       = NULL;  /* Must be overridden by subclasses */
	klass->get_bounds = NULL;  /* Optional */

	/**
	 * LrgShape:visible:
	 *
	 * Whether the shape is visible.
	 *
	 * Invisible shapes are not rendered.
	 */
	properties[PROP_VISIBLE] =
		g_param_spec_boolean ("visible",
		                      "Visible",
		                      "Whether the shape is visible",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape:color:
	 *
	 * The shape's color.
	 */
	properties[PROP_COLOR] =
		g_param_spec_boxed ("color",
		                    "Color",
		                    "The shape's color",
		                    GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgShape:z-index:
	 *
	 * The shape's z-index for draw ordering.
	 *
	 * Higher z-index shapes are drawn later (on top).
	 */
	properties[PROP_Z_INDEX] =
		g_param_spec_int ("z-index",
		                  "Z Index",
		                  "Draw ordering index (higher = on top)",
		                  G_MININT, G_MAXINT, 0,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_shape_init (LrgShape *self)
{
	LrgShapePrivate *priv = lrg_shape_get_instance_private (self);

	priv->visible = TRUE;
	priv->color   = grl_color_new (255, 255, 255, 255);  /* White default */
	priv->z_index = 0;
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_shape_get_visible:
 * @self: an #LrgShape
 *
 * Gets whether the shape is visible.
 *
 * Returns: %TRUE if visible
 */
gboolean
lrg_shape_get_visible (LrgShape *self)
{
	LrgShapePrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE (self), FALSE);

	priv = lrg_shape_get_instance_private (self);
	return priv->visible;
}

/**
 * lrg_shape_set_visible:
 * @self: an #LrgShape
 * @visible: whether the shape should be visible
 *
 * Sets whether the shape is visible.
 */
void
lrg_shape_set_visible (LrgShape *self,
                       gboolean  visible)
{
	LrgShapePrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE (self));

	priv = lrg_shape_get_instance_private (self);

	if (priv->visible != visible)
	{
		priv->visible = visible;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
	}
}

/**
 * lrg_shape_get_color:
 * @self: an #LrgShape
 *
 * Gets the shape's color.
 *
 * Returns: (transfer none): The color
 */
GrlColor *
lrg_shape_get_color (LrgShape *self)
{
	LrgShapePrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE (self), NULL);

	priv = lrg_shape_get_instance_private (self);
	return priv->color;
}

/**
 * lrg_shape_set_color:
 * @self: an #LrgShape
 * @color: (transfer none): the color to set
 *
 * Sets the shape's color.
 */
void
lrg_shape_set_color (LrgShape *self,
                     GrlColor *color)
{
	LrgShapePrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE (self));
	g_return_if_fail (color != NULL);

	priv = lrg_shape_get_instance_private (self);

	g_clear_pointer (&priv->color, grl_color_free);
	priv->color = grl_color_copy (color);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

/**
 * lrg_shape_set_color_rgba:
 * @self: an #LrgShape
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 * @a: alpha component (0-255)
 *
 * Sets the shape's color using RGBA values.
 */
void
lrg_shape_set_color_rgba (LrgShape *self,
                          guint8    r,
                          guint8    g,
                          guint8    b,
                          guint8    a)
{
	LrgShapePrivate   *priv;
	g_autoptr(GrlColor) new_color = NULL;

	g_return_if_fail (LRG_IS_SHAPE (self));

	priv = lrg_shape_get_instance_private (self);

	new_color = grl_color_new (r, g, b, a);
	g_clear_pointer (&priv->color, grl_color_free);
	priv->color = g_steal_pointer (&new_color);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

/**
 * lrg_shape_get_z_index:
 * @self: an #LrgShape
 *
 * Gets the shape's z-index.
 *
 * Returns: The z-index
 */
gint
lrg_shape_get_z_index (LrgShape *self)
{
	LrgShapePrivate *priv;

	g_return_val_if_fail (LRG_IS_SHAPE (self), 0);

	priv = lrg_shape_get_instance_private (self);
	return priv->z_index;
}

/**
 * lrg_shape_set_z_index:
 * @self: an #LrgShape
 * @z_index: the z-index value
 *
 * Sets the shape's z-index.
 */
void
lrg_shape_set_z_index (LrgShape *self,
                       gint      z_index)
{
	LrgShapePrivate *priv;

	g_return_if_fail (LRG_IS_SHAPE (self));

	priv = lrg_shape_get_instance_private (self);

	if (priv->z_index != z_index)
	{
		priv->z_index = z_index;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Z_INDEX]);
	}
}
