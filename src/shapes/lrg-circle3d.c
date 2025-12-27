/* lrg-circle3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D circle shape.
 */

#include "lrg-circle3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgCircle3D:
 *
 * A 3D circle shape.
 *
 * Renders a circle in 3D space using graylib's circle drawing function.
 * The circle can be rotated around an axis to orient it in any direction.
 */
struct _LrgCircle3D
{
	LrgShape3D parent_instance;

	gfloat            radius;
	gint              vertices;
	LrgCircleFillType fill_type;
	GrlVector3       *rotation_axis;
	gfloat            rotation_angle;
};

G_DEFINE_FINAL_TYPE (LrgCircle3D, lrg_circle3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_VERTICES,
	PROP_FILL_TYPE,
	PROP_ROTATION_AXIS,
	PROP_ROTATION_ANGLE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_circle3d_draw (LrgShape *shape,
                   gfloat    delta)
{
	LrgCircle3D           *self   = LRG_CIRCLE3D (shape);
	GrlVector3            *pos    = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot    = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl    = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color  = lrg_shape_get_color (shape);
	g_autoptr(GrlVector3)  origin = grl_vector3_new (0.0f, 0.0f, 0.0f);

	rlPushMatrix ();

	/* Apply transforms: translate, rotate (XYZ order), scale */
	rlTranslatef (pos->x, pos->y, pos->z);
	rlRotatef (rot->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	rlRotatef (rot->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	rlRotatef (rot->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	rlScalef (scl->x, scl->y, scl->z);

	/* Draw at origin with circle's own rotation axis/angle */
	grl_draw_circle_3d (origin,
	                    self->radius,
	                    self->rotation_axis,
	                    self->rotation_angle,
	                    color);

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_circle3d_finalize (GObject *object)
{
	LrgCircle3D *self = LRG_CIRCLE3D (object);

	g_clear_pointer (&self->rotation_axis, grl_vector3_free);

	G_OBJECT_CLASS (lrg_circle3d_parent_class)->finalize (object);
}

static void
lrg_circle3d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgCircle3D *self = LRG_CIRCLE3D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_VERTICES:
		g_value_set_int (value, self->vertices);
		break;
	case PROP_FILL_TYPE:
		g_value_set_enum (value, self->fill_type);
		break;
	case PROP_ROTATION_AXIS:
		g_value_set_boxed (value, self->rotation_axis);
		break;
	case PROP_ROTATION_ANGLE:
		g_value_set_float (value, self->rotation_angle);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_circle3d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgCircle3D *self = LRG_CIRCLE3D (object);
	GrlVector3  *vec;

	switch (prop_id)
	{
	case PROP_RADIUS:
		self->radius = g_value_get_float (value);
		break;
	case PROP_VERTICES:
		self->vertices = g_value_get_int (value);
		break;
	case PROP_FILL_TYPE:
		self->fill_type = g_value_get_enum (value);
		break;
	case PROP_ROTATION_AXIS:
		vec = g_value_get_boxed (value);
		if (vec != NULL)
		{
			g_clear_pointer (&self->rotation_axis, grl_vector3_free);
			self->rotation_axis = grl_vector3_copy (vec);
		}
		break;
	case PROP_ROTATION_ANGLE:
		self->rotation_angle = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_circle3d_class_init (LrgCircle3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->finalize     = lrg_circle3d_finalize;
	object_class->get_property = lrg_circle3d_get_property;
	object_class->set_property = lrg_circle3d_set_property;

	shape_class->draw = lrg_circle3d_draw;

	/**
	 * LrgCircle3D:radius:
	 *
	 * The circle's radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius",
		                    "Radius",
		                    "The circle's radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCircle3D:vertices:
	 *
	 * The number of vertices around the circle.
	 */
	properties[PROP_VERTICES] =
		g_param_spec_int ("vertices",
		                  "Vertices",
		                  "Number of vertices",
		                  3, 256, 32,
		                  G_PARAM_READWRITE |
		                  G_PARAM_CONSTRUCT |
		                  G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCircle3D:fill-type:
	 *
	 * The circle's fill type.
	 */
	properties[PROP_FILL_TYPE] =
		g_param_spec_enum ("fill-type",
		                   "Fill Type",
		                   "The circle fill type",
		                   LRG_TYPE_CIRCLE_FILL_TYPE,
		                   LRG_CIRCLE_FILL_NOTHING,
		                   G_PARAM_READWRITE |
		                   G_PARAM_CONSTRUCT |
		                   G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCircle3D:rotation-axis:
	 *
	 * The axis around which the circle is rotated.
	 */
	properties[PROP_ROTATION_AXIS] =
		g_param_spec_boxed ("rotation-axis",
		                    "Rotation Axis",
		                    "The rotation axis",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCircle3D:rotation-angle:
	 *
	 * The rotation angle in degrees.
	 */
	properties[PROP_ROTATION_ANGLE] =
		g_param_spec_float ("rotation-angle",
		                    "Rotation Angle",
		                    "The rotation angle in degrees",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_circle3d_init (LrgCircle3D *self)
{
	self->radius         = 1.0f;
	self->vertices       = 32;
	self->fill_type      = LRG_CIRCLE_FILL_NOTHING;
	self->rotation_axis  = grl_vector3_new (0.0f, 1.0f, 0.0f);
	self->rotation_angle = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_circle3d_new:
 *
 * Creates a new circle at the origin with radius 1.0 on the XZ plane.
 *
 * Returns: (transfer full): A new #LrgCircle3D
 */
LrgCircle3D *
lrg_circle3d_new (void)
{
	return g_object_new (LRG_TYPE_CIRCLE3D, NULL);
}

/**
 * lrg_circle3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: circle radius
 *
 * Creates a new circle at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgCircle3D
 */
LrgCircle3D *
lrg_circle3d_new_at (gfloat x,
                     gfloat y,
                     gfloat z,
                     gfloat radius)
{
	LrgCircle3D *circle;

	circle = g_object_new (LRG_TYPE_CIRCLE3D,
	                       "radius", radius,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (circle), x, y, z);

	return circle;
}

/**
 * lrg_circle3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: circle radius
 * @vertices: number of vertices
 * @color: (transfer none): the circle color
 *
 * Creates a new circle with full configuration.
 *
 * Returns: (transfer full): A new #LrgCircle3D
 */
LrgCircle3D *
lrg_circle3d_new_full (gfloat    x,
                       gfloat    y,
                       gfloat    z,
                       gfloat    radius,
                       gint      vertices,
                       GrlColor *color)
{
	LrgCircle3D *circle;

	circle = g_object_new (LRG_TYPE_CIRCLE3D,
	                       "radius", radius,
	                       "vertices", vertices,
	                       "color", color,
	                       NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (circle), x, y, z);

	return circle;
}

/* Property accessors */

/**
 * lrg_circle3d_get_radius:
 * @self: an #LrgCircle3D
 *
 * Gets the circle's radius.
 *
 * Returns: The radius
 */
gfloat
lrg_circle3d_get_radius (LrgCircle3D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE3D (self), 0.0f);

	return self->radius;
}

/**
 * lrg_circle3d_set_radius:
 * @self: an #LrgCircle3D
 * @radius: the radius value
 *
 * Sets the circle's radius.
 */
void
lrg_circle3d_set_radius (LrgCircle3D *self,
                         gfloat       radius)
{
	g_return_if_fail (LRG_IS_CIRCLE3D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius != radius)
	{
		self->radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
	}
}

/**
 * lrg_circle3d_get_vertices:
 * @self: an #LrgCircle3D
 *
 * Gets the number of vertices.
 *
 * Returns: The number of vertices
 */
gint
lrg_circle3d_get_vertices (LrgCircle3D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE3D (self), 0);

	return self->vertices;
}

/**
 * lrg_circle3d_set_vertices:
 * @self: an #LrgCircle3D
 * @vertices: the number of vertices
 *
 * Sets the number of vertices.
 */
void
lrg_circle3d_set_vertices (LrgCircle3D *self,
                           gint         vertices)
{
	g_return_if_fail (LRG_IS_CIRCLE3D (self));
	g_return_if_fail (vertices >= 3);

	if (self->vertices != vertices)
	{
		self->vertices = vertices;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTICES]);
	}
}

/**
 * lrg_circle3d_get_fill_type:
 * @self: an #LrgCircle3D
 *
 * Gets the fill type.
 *
 * Returns: The #LrgCircleFillType
 */
LrgCircleFillType
lrg_circle3d_get_fill_type (LrgCircle3D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE3D (self), LRG_CIRCLE_FILL_NOTHING);

	return self->fill_type;
}

/**
 * lrg_circle3d_set_fill_type:
 * @self: an #LrgCircle3D
 * @fill_type: the fill type
 *
 * Sets the fill type.
 */
void
lrg_circle3d_set_fill_type (LrgCircle3D       *self,
                            LrgCircleFillType  fill_type)
{
	g_return_if_fail (LRG_IS_CIRCLE3D (self));

	if (self->fill_type != fill_type)
	{
		self->fill_type = fill_type;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILL_TYPE]);
	}
}

/**
 * lrg_circle3d_get_rotation_axis:
 * @self: an #LrgCircle3D
 *
 * Gets the rotation axis.
 *
 * Returns: (transfer full): The rotation axis
 */
GrlVector3 *
lrg_circle3d_get_rotation_axis (LrgCircle3D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE3D (self), NULL);

	return grl_vector3_copy (self->rotation_axis);
}

/**
 * lrg_circle3d_set_rotation_axis:
 * @self: an #LrgCircle3D
 * @axis: (transfer none): the rotation axis
 *
 * Sets the rotation axis.
 */
void
lrg_circle3d_set_rotation_axis (LrgCircle3D *self,
                                GrlVector3  *axis)
{
	g_return_if_fail (LRG_IS_CIRCLE3D (self));
	g_return_if_fail (axis != NULL);

	g_clear_pointer (&self->rotation_axis, grl_vector3_free);
	self->rotation_axis = grl_vector3_copy (axis);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION_AXIS]);
}

/**
 * lrg_circle3d_get_rotation_angle:
 * @self: an #LrgCircle3D
 *
 * Gets the rotation angle in degrees.
 *
 * Returns: The rotation angle
 */
gfloat
lrg_circle3d_get_rotation_angle (LrgCircle3D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE3D (self), 0.0f);

	return self->rotation_angle;
}

/**
 * lrg_circle3d_set_rotation_angle:
 * @self: an #LrgCircle3D
 * @angle: the rotation angle in degrees
 *
 * Sets the rotation angle.
 */
void
lrg_circle3d_set_rotation_angle (LrgCircle3D *self,
                                 gfloat       angle)
{
	g_return_if_fail (LRG_IS_CIRCLE3D (self));

	if (self->rotation_angle != angle)
	{
		self->rotation_angle = angle;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION_ANGLE]);
	}
}
