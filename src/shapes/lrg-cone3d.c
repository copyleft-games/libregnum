/* lrg-cone3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cone shape.
 */

#include "lrg-cone3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgCone3D:
 *
 * A 3D cone shape.
 *
 * Renders a cone using graylib's cylinder drawing functions with
 * different top and bottom radii. A true cone has top radius of 0.
 * The cone is centered at its position with height extending along
 * the Y axis.
 */
struct _LrgCone3D
{
	LrgShape3D parent_instance;

	gfloat radius_bottom;
	gfloat radius_top;
	gfloat height;
	gint   slices;
};

G_DEFINE_FINAL_TYPE (LrgCone3D, lrg_cone3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_RADIUS_BOTTOM,
	PROP_RADIUS_TOP,
	PROP_HEIGHT,
	PROP_SLICES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_cone3d_draw (LrgShape *shape,
                 gfloat    delta)
{
	LrgCone3D             *self      = LRG_CONE3D (shape);
	GrlVector3            *pos       = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot       = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl       = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color     = lrg_shape_get_color (shape);
	gboolean               wireframe = lrg_shape3d_get_wireframe (LRG_SHAPE3D (self));
	g_autoptr(GrlVector3)  origin    = grl_vector3_new (0.0f, 0.0f, 0.0f);

	rlPushMatrix ();

	/* Apply transforms: translate, rotate (XYZ order), scale */
	rlTranslatef (pos->x, pos->y, pos->z);
	rlRotatef (rot->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	rlRotatef (rot->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	rlRotatef (rot->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	rlScalef (scl->x, scl->y, scl->z);

	/* Draw at origin (position handled by matrix) */
	if (wireframe)
	{
		grl_draw_cylinder_wires (origin,
		                         self->radius_top,
		                         self->radius_bottom,
		                         self->height,
		                         self->slices,
		                         color);
	}
	else
	{
		grl_draw_cylinder (origin,
		                   self->radius_top,
		                   self->radius_bottom,
		                   self->height,
		                   self->slices,
		                   color);
	}

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_cone3d_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgCone3D *self = LRG_CONE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS_BOTTOM:
		g_value_set_float (value, self->radius_bottom);
		break;
	case PROP_RADIUS_TOP:
		g_value_set_float (value, self->radius_top);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case PROP_SLICES:
		g_value_set_int (value, self->slices);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cone3d_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgCone3D *self = LRG_CONE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS_BOTTOM:
		self->radius_bottom = g_value_get_float (value);
		break;
	case PROP_RADIUS_TOP:
		self->radius_top = g_value_get_float (value);
		break;
	case PROP_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	case PROP_SLICES:
		self->slices = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cone3d_class_init (LrgCone3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_cone3d_get_property;
	object_class->set_property = lrg_cone3d_set_property;

	shape_class->draw = lrg_cone3d_draw;

	/**
	 * LrgCone3D:radius-bottom:
	 *
	 * The cone's base radius.
	 */
	properties[PROP_RADIUS_BOTTOM] =
		g_param_spec_float ("radius-bottom",
		                    "Radius Bottom",
		                    "The cone's base radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCone3D:radius-top:
	 *
	 * The cone's top radius. Set to 0 for a pointed cone.
	 */
	properties[PROP_RADIUS_TOP] =
		g_param_spec_float ("radius-top",
		                    "Radius Top",
		                    "The cone's top radius (0 for pointed cone)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCone3D:height:
	 *
	 * The cone's height (depth along Y axis).
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_float ("height",
		                    "Height",
		                    "The cone's height",
		                    0.0f, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCone3D:slices:
	 *
	 * The number of slices (sides) around the cone.
	 */
	properties[PROP_SLICES] =
		g_param_spec_int ("slices",
		                  "Slices",
		                  "Number of slices around the cone",
		                  3, 256, 32,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cone3d_init (LrgCone3D *self)
{
	self->radius_bottom = 1.0f;
	self->radius_top    = 0.0f;
	self->height        = 2.0f;
	self->slices        = 32;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_cone3d_new:
 *
 * Creates a new cone at the origin with base radius 1.0, top radius 0.0,
 * and height 2.0.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LrgCone3D *
lrg_cone3d_new (void)
{
	return g_object_new (LRG_TYPE_CONE3D, NULL);
}

/**
 * lrg_cone3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius_bottom: base radius
 * @height: cone height
 *
 * Creates a new cone at the specified position with given dimensions.
 * The top radius is set to 0 for a pointed cone.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LrgCone3D *
lrg_cone3d_new_at (gfloat x,
                   gfloat y,
                   gfloat z,
                   gfloat radius_bottom,
                   gfloat height)
{
	LrgCone3D *cone;

	cone = g_object_new (LRG_TYPE_CONE3D,
	                     "radius-bottom", radius_bottom,
	                     "height", height,
	                     NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cone), x, y, z);

	return cone;
}

/**
 * lrg_cone3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius_bottom: base radius
 * @radius_top: top radius (0 for pointed cone)
 * @height: cone height
 * @slices: number of slices around the cone
 * @color: (transfer none): the cone color
 *
 * Creates a new cone with full configuration.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LrgCone3D *
lrg_cone3d_new_full (gfloat    x,
                     gfloat    y,
                     gfloat    z,
                     gfloat    radius_bottom,
                     gfloat    radius_top,
                     gfloat    height,
                     gint      slices,
                     GrlColor *color)
{
	LrgCone3D *cone;

	cone = g_object_new (LRG_TYPE_CONE3D,
	                     "radius-bottom", radius_bottom,
	                     "radius-top", radius_top,
	                     "height", height,
	                     "slices", slices,
	                     "color", color,
	                     NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cone), x, y, z);

	return cone;
}

/* Property accessors */

/**
 * lrg_cone3d_get_radius_bottom:
 * @self: an #LrgCone3D
 *
 * Gets the cone's base radius.
 *
 * Returns: The base radius
 */
gfloat
lrg_cone3d_get_radius_bottom (LrgCone3D *self)
{
	g_return_val_if_fail (LRG_IS_CONE3D (self), 0.0f);

	return self->radius_bottom;
}

/**
 * lrg_cone3d_set_radius_bottom:
 * @self: an #LrgCone3D
 * @radius: the base radius
 *
 * Sets the cone's base radius.
 */
void
lrg_cone3d_set_radius_bottom (LrgCone3D *self,
                              gfloat     radius)
{
	g_return_if_fail (LRG_IS_CONE3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius_bottom != radius)
	{
		self->radius_bottom = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS_BOTTOM]);
	}
}

/**
 * lrg_cone3d_get_radius_top:
 * @self: an #LrgCone3D
 *
 * Gets the cone's top radius.
 *
 * Returns: The top radius
 */
gfloat
lrg_cone3d_get_radius_top (LrgCone3D *self)
{
	g_return_val_if_fail (LRG_IS_CONE3D (self), 0.0f);

	return self->radius_top;
}

/**
 * lrg_cone3d_set_radius_top:
 * @self: an #LrgCone3D
 * @radius: the top radius (0 for pointed cone)
 *
 * Sets the cone's top radius.
 */
void
lrg_cone3d_set_radius_top (LrgCone3D *self,
                           gfloat     radius)
{
	g_return_if_fail (LRG_IS_CONE3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius_top != radius)
	{
		self->radius_top = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS_TOP]);
	}
}

/**
 * lrg_cone3d_get_height:
 * @self: an #LrgCone3D
 *
 * Gets the cone's height.
 *
 * Returns: The height
 */
gfloat
lrg_cone3d_get_height (LrgCone3D *self)
{
	g_return_val_if_fail (LRG_IS_CONE3D (self), 0.0f);

	return self->height;
}

/**
 * lrg_cone3d_set_height:
 * @self: an #LrgCone3D
 * @height: the height value
 *
 * Sets the cone's height.
 */
void
lrg_cone3d_set_height (LrgCone3D *self,
                       gfloat     height)
{
	g_return_if_fail (LRG_IS_CONE3D (self));
	g_return_if_fail (height >= 0.0f);

	if (self->height != height)
	{
		self->height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
	}
}

/**
 * lrg_cone3d_get_slices:
 * @self: an #LrgCone3D
 *
 * Gets the number of slices around the cone.
 *
 * Returns: The number of slices
 */
gint
lrg_cone3d_get_slices (LrgCone3D *self)
{
	g_return_val_if_fail (LRG_IS_CONE3D (self), 0);

	return self->slices;
}

/**
 * lrg_cone3d_set_slices:
 * @self: an #LrgCone3D
 * @slices: the number of slices
 *
 * Sets the number of slices around the cone.
 */
void
lrg_cone3d_set_slices (LrgCone3D *self,
                       gint       slices)
{
	g_return_if_fail (LRG_IS_CONE3D (self));
	g_return_if_fail (slices >= 3);

	if (self->slices != slices)
	{
		self->slices = slices;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLICES]);
	}
}
