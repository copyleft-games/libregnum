/* lrg-sphere3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D sphere shape.
 */

#include "lrg-sphere3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgSphere3D:
 *
 * A 3D sphere shape.
 *
 * Renders a sphere using graylib's sphere drawing functions.
 */
struct _LrgSphere3D
{
	LrgShape3D parent_instance;

	gfloat radius;
	gint   rings;
	gint   slices;
};

G_DEFINE_FINAL_TYPE (LrgSphere3D, lrg_sphere3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_RINGS,
	PROP_SLICES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_sphere3d_draw (LrgShape *shape,
                   gfloat    delta)
{
	LrgSphere3D           *self = LRG_SPHERE3D (shape);
	GrlVector3            *pos  = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot  = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl  = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color = lrg_shape_get_color (shape);
	gboolean               wireframe = lrg_shape3d_get_wireframe (LRG_SHAPE3D (self));
	g_autoptr(GrlVector3)  origin = grl_vector3_new (0.0f, 0.0f, 0.0f);

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
		grl_draw_sphere_wires (origin,
		                       self->radius,
		                       self->rings,
		                       self->slices,
		                       color);
	}
	else
	{
		grl_draw_sphere_ex (origin,
		                    self->radius,
		                    self->rings,
		                    self->slices,
		                    color);
	}

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_sphere3d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgSphere3D *self = LRG_SPHERE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_RINGS:
		g_value_set_int (value, self->rings);
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
lrg_sphere3d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgSphere3D *self = LRG_SPHERE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		self->radius = g_value_get_float (value);
		break;
	case PROP_RINGS:
		self->rings = g_value_get_int (value);
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
lrg_sphere3d_class_init (LrgSphere3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_sphere3d_get_property;
	object_class->set_property = lrg_sphere3d_set_property;

	shape_class->draw = lrg_sphere3d_draw;

	/**
	 * LrgSphere3D:radius:
	 *
	 * The sphere's radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius",
		                    "Radius",
		                    "The sphere's radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSphere3D:rings:
	 *
	 * The number of horizontal rings for tessellation.
	 */
	properties[PROP_RINGS] =
		g_param_spec_int ("rings",
		                  "Rings",
		                  "Number of horizontal rings",
		                  1, 256, 16,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSphere3D:slices:
	 *
	 * The number of vertical slices for tessellation.
	 */
	properties[PROP_SLICES] =
		g_param_spec_int ("slices",
		                  "Slices",
		                  "Number of vertical slices",
		                  2, 256, 16,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sphere3d_init (LrgSphere3D *self)
{
	self->radius = 1.0f;
	self->rings  = 16;
	self->slices = 16;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_sphere3d_new:
 *
 * Creates a new sphere at the origin with radius 1.0.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LrgSphere3D *
lrg_sphere3d_new (void)
{
	return g_object_new (LRG_TYPE_SPHERE3D, NULL);
}

/**
 * lrg_sphere3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 *
 * Creates a new sphere at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LrgSphere3D *
lrg_sphere3d_new_at (gfloat x,
                     gfloat y,
                     gfloat z,
                     gfloat radius)
{
	LrgSphere3D *sphere;

	sphere = g_object_new (LRG_TYPE_SPHERE3D,
	                       "radius", radius,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (sphere), x, y, z);

	return sphere;
}

/**
 * lrg_sphere3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 * @color: (transfer none): the sphere color
 *
 * Creates a new sphere with full configuration.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LrgSphere3D *
lrg_sphere3d_new_full (gfloat    x,
                       gfloat    y,
                       gfloat    z,
                       gfloat    radius,
                       GrlColor *color)
{
	LrgSphere3D *sphere;

	sphere = g_object_new (LRG_TYPE_SPHERE3D,
	                       "radius", radius,
	                       "color", color,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (sphere), x, y, z);

	return sphere;
}

/* Property accessors */

/**
 * lrg_sphere3d_get_radius:
 * @self: an #LrgSphere3D
 *
 * Gets the sphere's radius.
 *
 * Returns: The radius
 */
gfloat
lrg_sphere3d_get_radius (LrgSphere3D *self)
{
	g_return_val_if_fail (LRG_IS_SPHERE3D (self), 0.0f);

	return self->radius;
}

/**
 * lrg_sphere3d_set_radius:
 * @self: an #LrgSphere3D
 * @radius: the radius value
 *
 * Sets the sphere's radius.
 */
void
lrg_sphere3d_set_radius (LrgSphere3D *self,
                         gfloat       radius)
{
	g_return_if_fail (LRG_IS_SPHERE3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius != radius)
	{
		self->radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
	}
}

/**
 * lrg_sphere3d_get_rings:
 * @self: an #LrgSphere3D
 *
 * Gets the number of horizontal rings.
 *
 * Returns: The number of rings
 */
gint
lrg_sphere3d_get_rings (LrgSphere3D *self)
{
	g_return_val_if_fail (LRG_IS_SPHERE3D (self), 0);

	return self->rings;
}

/**
 * lrg_sphere3d_set_rings:
 * @self: an #LrgSphere3D
 * @rings: the number of rings
 *
 * Sets the number of horizontal rings.
 */
void
lrg_sphere3d_set_rings (LrgSphere3D *self,
                        gint         rings)
{
	g_return_if_fail (LRG_IS_SPHERE3D (self));
	g_return_if_fail (rings >= 1);

	if (self->rings != rings)
	{
		self->rings = rings;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RINGS]);
	}
}

/**
 * lrg_sphere3d_get_slices:
 * @self: an #LrgSphere3D
 *
 * Gets the number of vertical slices.
 *
 * Returns: The number of slices
 */
gint
lrg_sphere3d_get_slices (LrgSphere3D *self)
{
	g_return_val_if_fail (LRG_IS_SPHERE3D (self), 0);

	return self->slices;
}

/**
 * lrg_sphere3d_set_slices:
 * @self: an #LrgSphere3D
 * @slices: the number of slices
 *
 * Sets the number of vertical slices.
 */
void
lrg_sphere3d_set_slices (LrgSphere3D *self,
                         gint         slices)
{
	g_return_if_fail (LRG_IS_SPHERE3D (self));
	g_return_if_fail (slices >= 2);

	if (self->slices != slices)
	{
		self->slices = slices;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SLICES]);
	}
}
