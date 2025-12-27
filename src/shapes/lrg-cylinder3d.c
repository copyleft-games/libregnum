/* lrg-cylinder3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cylinder shape.
 */

#include "lrg-cylinder3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgCylinder3D:
 *
 * A 3D cylinder shape.
 *
 * Renders a cylinder using graylib's cylinder drawing functions.
 * The cylinder is centered at its position with height extending
 * along the Y axis.
 */
struct _LrgCylinder3D
{
	LrgShape3D parent_instance;

	gfloat   radius;
	gfloat   height;
	gint     slices;
	gboolean cap_ends;
};

G_DEFINE_FINAL_TYPE (LrgCylinder3D, lrg_cylinder3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_HEIGHT,
	PROP_SLICES,
	PROP_CAP_ENDS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_cylinder3d_draw (LrgShape *shape,
                     gfloat    delta)
{
	LrgCylinder3D         *self      = LRG_CYLINDER3D (shape);
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
		                         self->radius,
		                         self->radius,
		                         self->height,
		                         self->slices,
		                         color);
	}
	else
	{
		grl_draw_cylinder (origin,
		                   self->radius,
		                   self->radius,
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
lrg_cylinder3d_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgCylinder3D *self = LRG_CYLINDER3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case PROP_SLICES:
		g_value_set_int (value, self->slices);
		break;
	case PROP_CAP_ENDS:
		g_value_set_boolean (value, self->cap_ends);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cylinder3d_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgCylinder3D *self = LRG_CYLINDER3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		self->radius = g_value_get_float (value);
		break;
	case PROP_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	case PROP_SLICES:
		self->slices = g_value_get_int (value);
		break;
	case PROP_CAP_ENDS:
		self->cap_ends = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cylinder3d_class_init (LrgCylinder3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_cylinder3d_get_property;
	object_class->set_property = lrg_cylinder3d_set_property;

	shape_class->draw = lrg_cylinder3d_draw;

	/**
	 * LrgCylinder3D:radius:
	 *
	 * The cylinder's radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius",
		                    "Radius",
		                    "The cylinder's radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCylinder3D:height:
	 *
	 * The cylinder's height (depth along Y axis).
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_float ("height",
		                    "Height",
		                    "The cylinder's height",
		                    0.0f, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCylinder3D:slices:
	 *
	 * The number of slices (sides) around the cylinder.
	 */
	properties[PROP_SLICES] =
		g_param_spec_int ("slices",
		                  "Slices",
		                  "Number of slices around the cylinder",
		                  3, 256, 32,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCylinder3D:cap-ends:
	 *
	 * Whether the cylinder has capped ends.
	 */
	properties[PROP_CAP_ENDS] =
		g_param_spec_boolean ("cap-ends",
		                      "Cap Ends",
		                      "Whether to cap the cylinder ends",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cylinder3d_init (LrgCylinder3D *self)
{
	self->radius   = 1.0f;
	self->height   = 2.0f;
	self->slices   = 32;
	self->cap_ends = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_cylinder3d_new:
 *
 * Creates a new cylinder at the origin with radius 1.0 and height 2.0.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LrgCylinder3D *
lrg_cylinder3d_new (void)
{
	return g_object_new (LRG_TYPE_CYLINDER3D, NULL);
}

/**
 * lrg_cylinder3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: cylinder radius
 * @height: cylinder height (depth)
 *
 * Creates a new cylinder at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LrgCylinder3D *
lrg_cylinder3d_new_at (gfloat x,
                       gfloat y,
                       gfloat z,
                       gfloat radius,
                       gfloat height)
{
	LrgCylinder3D *cylinder;

	cylinder = g_object_new (LRG_TYPE_CYLINDER3D,
	                         "radius", radius,
	                         "height", height,
	                         NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cylinder), x, y, z);

	return cylinder;
}

/**
 * lrg_cylinder3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: cylinder radius
 * @height: cylinder height (depth)
 * @slices: number of slices around the cylinder
 * @color: (transfer none): the cylinder color
 *
 * Creates a new cylinder with full configuration.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LrgCylinder3D *
lrg_cylinder3d_new_full (gfloat    x,
                         gfloat    y,
                         gfloat    z,
                         gfloat    radius,
                         gfloat    height,
                         gint      slices,
                         GrlColor *color)
{
	LrgCylinder3D *cylinder;

	cylinder = g_object_new (LRG_TYPE_CYLINDER3D,
	                         "radius", radius,
	                         "height", height,
	                         "slices", slices,
	                         "color", color,
	                         NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (cylinder), x, y, z);

	return cylinder;
}

/* Property accessors */

/**
 * lrg_cylinder3d_get_radius:
 * @self: an #LrgCylinder3D
 *
 * Gets the cylinder's radius.
 *
 * Returns: The radius
 */
gfloat
lrg_cylinder3d_get_radius (LrgCylinder3D *self)
{
	g_return_val_if_fail (LRG_IS_CYLINDER3D (self), 0.0f);

	return self->radius;
}

/**
 * lrg_cylinder3d_set_radius:
 * @self: an #LrgCylinder3D
 * @radius: the radius value
 *
 * Sets the cylinder's radius.
 */
void
lrg_cylinder3d_set_radius (LrgCylinder3D *self,
                           gfloat         radius)
{
	g_return_if_fail (LRG_IS_CYLINDER3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius != radius)
	{
		self->radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
	}
}

/**
 * lrg_cylinder3d_get_height:
 * @self: an #LrgCylinder3D
 *
 * Gets the cylinder's height (depth).
 *
 * Returns: The height
 */
gfloat
lrg_cylinder3d_get_height (LrgCylinder3D *self)
{
	g_return_val_if_fail (LRG_IS_CYLINDER3D (self), 0.0f);

	return self->height;
}

/**
 * lrg_cylinder3d_set_height:
 * @self: an #LrgCylinder3D
 * @height: the height value
 *
 * Sets the cylinder's height (depth).
 */
void
lrg_cylinder3d_set_height (LrgCylinder3D *self,
                           gfloat         height)
{
	g_return_if_fail (LRG_IS_CYLINDER3D (self));
	g_return_if_fail (height >= 0.0f);

	if (self->height != height)
	{
		self->height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
	}
}

/**
 * lrg_cylinder3d_get_slices:
 * @self: an #LrgCylinder3D
 *
 * Gets the number of slices around the cylinder.
 *
 * Returns: The number of slices
 */
gint
lrg_cylinder3d_get_slices (LrgCylinder3D *self)
{
	g_return_val_if_fail (LRG_IS_CYLINDER3D (self), 0);

	return self->slices;
}

/**
 * lrg_cylinder3d_set_slices:
 * @self: an #LrgCylinder3D
 * @slices: the number of slices
 *
 * Sets the number of slices around the cylinder.
 */
void
lrg_cylinder3d_set_slices (LrgCylinder3D *self,
                           gint           slices)
{
	g_return_if_fail (LRG_IS_CYLINDER3D (self));
	g_return_if_fail (slices >= 3);

	if (self->slices != slices)
	{
		self->slices = slices;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLICES]);
	}
}

/**
 * lrg_cylinder3d_get_cap_ends:
 * @self: an #LrgCylinder3D
 *
 * Gets whether the cylinder has capped ends.
 *
 * Returns: %TRUE if ends are capped
 */
gboolean
lrg_cylinder3d_get_cap_ends (LrgCylinder3D *self)
{
	g_return_val_if_fail (LRG_IS_CYLINDER3D (self), FALSE);

	return self->cap_ends;
}

/**
 * lrg_cylinder3d_set_cap_ends:
 * @self: an #LrgCylinder3D
 * @cap_ends: whether to cap the ends
 *
 * Sets whether the cylinder has capped ends.
 */
void
lrg_cylinder3d_set_cap_ends (LrgCylinder3D *self,
                             gboolean       cap_ends)
{
	g_return_if_fail (LRG_IS_CYLINDER3D (self));

	cap_ends = !!cap_ends;

	if (self->cap_ends != cap_ends)
	{
		self->cap_ends = cap_ends;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAP_ENDS]);
	}
}
