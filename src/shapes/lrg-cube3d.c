/* lrg-cube3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cube/box shape.
 */

#include "lrg-cube3d.h"

/**
 * LrgCube3D:
 *
 * A 3D cube/box shape.
 *
 * Renders a cube using graylib's cube drawing functions.
 * Supports variable width, height, and depth for rectangular boxes.
 */
struct _LrgCube3D
{
	LrgShape3D parent_instance;

	gfloat width;
	gfloat height;
	gfloat depth;
};

G_DEFINE_FINAL_TYPE (LrgCube3D, lrg_cube3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_DEPTH,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_cube3d_draw (LrgShape *shape,
                 gfloat    delta)
{
	LrgCube3D   *self     = LRG_CUBE3D (shape);
	GrlVector3  *position = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlColor    *color    = lrg_shape_get_color (shape);
	gboolean     wireframe = lrg_shape3d_get_wireframe (LRG_SHAPE3D (self));

	if (wireframe)
	{
		grl_draw_cube_wires (position,
		                     self->width,
		                     self->height,
		                     self->depth,
		                     color);
	}
	else
	{
		grl_draw_cube (position,
		               self->width,
		               self->height,
		               self->depth,
		               color);
	}
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_cube3d_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgCube3D *self = LRG_CUBE3D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_float (value, self->width);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case PROP_DEPTH:
		g_value_set_float (value, self->depth);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cube3d_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgCube3D *self = LRG_CUBE3D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		self->width = g_value_get_float (value);
		break;
	case PROP_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	case PROP_DEPTH:
		self->depth = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cube3d_class_init (LrgCube3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_cube3d_get_property;
	object_class->set_property = lrg_cube3d_set_property;

	shape_class->draw = lrg_cube3d_draw;

	/**
	 * LrgCube3D:width:
	 *
	 * The cube's width (X axis).
	 */
	properties[PROP_WIDTH] =
		g_param_spec_float ("width",
		                    "Width",
		                    "The cube's width (X axis)",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCube3D:height:
	 *
	 * The cube's height (Y axis).
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_float ("height",
		                    "Height",
		                    "The cube's height (Y axis)",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCube3D:depth:
	 *
	 * The cube's depth (Z axis).
	 */
	properties[PROP_DEPTH] =
		g_param_spec_float ("depth",
		                    "Depth",
		                    "The cube's depth (Z axis)",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cube3d_init (LrgCube3D *self)
{
	self->width  = 1.0f;
	self->height = 1.0f;
	self->depth  = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_cube3d_new:
 *
 * Creates a new unit cube at the origin.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LrgCube3D *
lrg_cube3d_new (void)
{
	return g_object_new (LRG_TYPE_CUBE3D, NULL);
}

/**
 * lrg_cube3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: cube width (X axis)
 * @height: cube height (Y axis)
 * @depth: cube depth (Z axis)
 *
 * Creates a new cube at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LrgCube3D *
lrg_cube3d_new_at (gfloat x,
                   gfloat y,
                   gfloat z,
                   gfloat width,
                   gfloat height,
                   gfloat depth)
{
	LrgCube3D *cube;

	cube = g_object_new (LRG_TYPE_CUBE3D,
	                     "width", width,
	                     "height", height,
	                     "depth", depth,
	                     NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cube), x, y, z);

	return cube;
}

/**
 * lrg_cube3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: cube width (X axis)
 * @height: cube height (Y axis)
 * @depth: cube depth (Z axis)
 * @color: (transfer none): the cube color
 *
 * Creates a new cube with full configuration.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LrgCube3D *
lrg_cube3d_new_full (gfloat    x,
                     gfloat    y,
                     gfloat    z,
                     gfloat    width,
                     gfloat    height,
                     gfloat    depth,
                     GrlColor *color)
{
	LrgCube3D *cube;

	cube = g_object_new (LRG_TYPE_CUBE3D,
	                     "width", width,
	                     "height", height,
	                     "depth", depth,
	                     "color", color,
	                     NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cube), x, y, z);

	return cube;
}

/* Property accessors */

/**
 * lrg_cube3d_get_width:
 * @self: an #LrgCube3D
 *
 * Gets the cube's width (X axis).
 *
 * Returns: The width
 */
gfloat
lrg_cube3d_get_width (LrgCube3D *self)
{
	g_return_val_if_fail (LRG_IS_CUBE3D (self), 0.0f);

	return self->width;
}

/**
 * lrg_cube3d_set_width:
 * @self: an #LrgCube3D
 * @width: the width value
 *
 * Sets the cube's width (X axis).
 */
void
lrg_cube3d_set_width (LrgCube3D *self,
                      gfloat     width)
{
	g_return_if_fail (LRG_IS_CUBE3D (self));
	g_return_if_fail (width >= 0.0f);

	if (self->width != width)
	{
		self->width = width;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	}
}

/**
 * lrg_cube3d_get_height:
 * @self: an #LrgCube3D
 *
 * Gets the cube's height (Y axis).
 *
 * Returns: The height
 */
gfloat
lrg_cube3d_get_height (LrgCube3D *self)
{
	g_return_val_if_fail (LRG_IS_CUBE3D (self), 0.0f);

	return self->height;
}

/**
 * lrg_cube3d_set_height:
 * @self: an #LrgCube3D
 * @height: the height value
 *
 * Sets the cube's height (Y axis).
 */
void
lrg_cube3d_set_height (LrgCube3D *self,
                       gfloat     height)
{
	g_return_if_fail (LRG_IS_CUBE3D (self));
	g_return_if_fail (height >= 0.0f);

	if (self->height != height)
	{
		self->height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
	}
}

/**
 * lrg_cube3d_get_depth:
 * @self: an #LrgCube3D
 *
 * Gets the cube's depth (Z axis).
 *
 * Returns: The depth
 */
gfloat
lrg_cube3d_get_depth (LrgCube3D *self)
{
	g_return_val_if_fail (LRG_IS_CUBE3D (self), 0.0f);

	return self->depth;
}

/**
 * lrg_cube3d_set_depth:
 * @self: an #LrgCube3D
 * @depth: the depth value
 *
 * Sets the cube's depth (Z axis).
 */
void
lrg_cube3d_set_depth (LrgCube3D *self,
                      gfloat     depth)
{
	g_return_if_fail (LRG_IS_CUBE3D (self));
	g_return_if_fail (depth >= 0.0f);

	if (self->depth != depth)
	{
		self->depth = depth;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH]);
	}
}

/**
 * lrg_cube3d_set_size:
 * @self: an #LrgCube3D
 * @width: the width value
 * @height: the height value
 * @depth: the depth value
 *
 * Sets all dimensions at once.
 */
void
lrg_cube3d_set_size (LrgCube3D *self,
                     gfloat     width,
                     gfloat     height,
                     gfloat     depth)
{
	g_return_if_fail (LRG_IS_CUBE3D (self));
	g_return_if_fail (width >= 0.0f);
	g_return_if_fail (height >= 0.0f);
	g_return_if_fail (depth >= 0.0f);

	g_object_freeze_notify (G_OBJECT (self));

	if (self->width != width)
	{
		self->width = width;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	}
	if (self->height != height)
	{
		self->height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
	}
	if (self->depth != depth)
	{
		self->depth = depth;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEPTH]);
	}

	g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_cube3d_set_uniform_size:
 * @self: an #LrgCube3D
 * @size: the size for all dimensions
 *
 * Sets all dimensions to the same value (true cube).
 */
void
lrg_cube3d_set_uniform_size (LrgCube3D *self,
                             gfloat     size)
{
	lrg_cube3d_set_size (self, size, size, size);
}
