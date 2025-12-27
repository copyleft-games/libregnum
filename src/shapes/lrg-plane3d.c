/* lrg-plane3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D plane shape.
 */

#include "lrg-plane3d.h"

#include <rlgl.h>

#ifndef RAD2DEG
#define RAD2DEG (180.0f / 3.14159265358979323846f)
#endif

/**
 * LrgPlane3D:
 *
 * A 3D plane shape.
 *
 * Renders a plane on the XZ plane using graylib's plane drawing functions.
 * The plane is centered at its position.
 */
struct _LrgPlane3D
{
	LrgShape3D parent_instance;

	gfloat width;
	gfloat length;
};

G_DEFINE_FINAL_TYPE (LrgPlane3D, lrg_plane3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_LENGTH,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_plane3d_draw (LrgShape *shape,
                  gfloat    delta)
{
	LrgPlane3D            *self   = LRG_PLANE3D (shape);
	GrlVector3            *pos    = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlVector3            *rot    = lrg_shape3d_get_rotation (LRG_SHAPE3D (self));
	GrlVector3            *scl    = lrg_shape3d_get_scale (LRG_SHAPE3D (self));
	GrlColor              *color  = lrg_shape_get_color (shape);
	g_autoptr(GrlVector3)  origin = grl_vector3_new (0.0f, 0.0f, 0.0f);
	g_autoptr(GrlVector2)  size   = grl_vector2_new (self->width, self->length);

	rlPushMatrix ();

	/* Apply transforms: translate, rotate (XYZ order), scale */
	rlTranslatef (pos->x, pos->y, pos->z);
	rlRotatef (rot->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	rlRotatef (rot->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	rlRotatef (rot->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	rlScalef (scl->x, scl->y, scl->z);

	/*
	 * grl_draw_plane draws a flat plane on the XZ plane.
	 * Note: graylib doesn't have a wireframe plane function,
	 * so wireframe mode is not supported for planes.
	 */
	grl_draw_plane (origin, size, color);

	rlPopMatrix ();
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_plane3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgPlane3D *self = LRG_PLANE3D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_float (value, self->width);
		break;
	case PROP_LENGTH:
		g_value_set_float (value, self->length);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_plane3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgPlane3D *self = LRG_PLANE3D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		self->width = g_value_get_float (value);
		break;
	case PROP_LENGTH:
		self->length = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_plane3d_class_init (LrgPlane3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_plane3d_get_property;
	object_class->set_property = lrg_plane3d_set_property;

	shape_class->draw = lrg_plane3d_draw;

	/**
	 * LrgPlane3D:width:
	 *
	 * The plane's width (X dimension).
	 */
	properties[PROP_WIDTH] =
		g_param_spec_float ("width",
		                    "Width",
		                    "The plane's width",
		                    0.0f, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPlane3D:length:
	 *
	 * The plane's length (Z dimension).
	 */
	properties[PROP_LENGTH] =
		g_param_spec_float ("length",
		                    "Length",
		                    "The plane's length",
		                    0.0f, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_plane3d_init (LrgPlane3D *self)
{
	self->width  = 2.0f;
	self->length = 2.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_plane3d_new:
 *
 * Creates a new plane at the origin with size 2x2.
 *
 * Returns: (transfer full): A new #LrgPlane3D
 */
LrgPlane3D *
lrg_plane3d_new (void)
{
	return g_object_new (LRG_TYPE_PLANE3D, NULL);
}

/**
 * lrg_plane3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: plane width (X dimension)
 * @length: plane length (Z dimension)
 *
 * Creates a new plane at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgPlane3D
 */
LrgPlane3D *
lrg_plane3d_new_at (gfloat x,
                    gfloat y,
                    gfloat z,
                    gfloat width,
                    gfloat length)
{
	LrgPlane3D *plane;

	plane = g_object_new (LRG_TYPE_PLANE3D,
	                      "width", width,
	                      "length", length,
	                      NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (plane), x, y, z);

	return plane;
}

/**
 * lrg_plane3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: plane width (X dimension)
 * @length: plane length (Z dimension)
 * @color: (transfer none): the plane color
 *
 * Creates a new plane with full configuration.
 *
 * Returns: (transfer full): A new #LrgPlane3D
 */
LrgPlane3D *
lrg_plane3d_new_full (gfloat    x,
                      gfloat    y,
                      gfloat    z,
                      gfloat    width,
                      gfloat    length,
                      GrlColor *color)
{
	LrgPlane3D *plane;

	plane = g_object_new (LRG_TYPE_PLANE3D,
	                      "width", width,
	                      "length", length,
	                      "color", color,
	                      NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (plane), x, y, z);

	return plane;
}

/* Property accessors */

/**
 * lrg_plane3d_get_width:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's width (X dimension).
 *
 * Returns: The width
 */
gfloat
lrg_plane3d_get_width (LrgPlane3D *self)
{
	g_return_val_if_fail (LRG_IS_PLANE3D (self), 0.0f);

	return self->width;
}

/**
 * lrg_plane3d_set_width:
 * @self: an #LrgPlane3D
 * @width: the width value
 *
 * Sets the plane's width (X dimension).
 */
void
lrg_plane3d_set_width (LrgPlane3D *self,
                       gfloat      width)
{
	g_return_if_fail (LRG_IS_PLANE3D (self));
	g_return_if_fail (width >= 0.0f);

	if (self->width != width)
	{
		self->width = width;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	}
}

/**
 * lrg_plane3d_get_length:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's length (Z dimension).
 *
 * Returns: The length
 */
gfloat
lrg_plane3d_get_length (LrgPlane3D *self)
{
	g_return_val_if_fail (LRG_IS_PLANE3D (self), 0.0f);

	return self->length;
}

/**
 * lrg_plane3d_set_length:
 * @self: an #LrgPlane3D
 * @length: the length value
 *
 * Sets the plane's length (Z dimension).
 */
void
lrg_plane3d_set_length (LrgPlane3D *self,
                        gfloat      length)
{
	g_return_if_fail (LRG_IS_PLANE3D (self));
	g_return_if_fail (length >= 0.0f);

	if (self->length != length)
	{
		self->length = length;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LENGTH]);
	}
}

/**
 * lrg_plane3d_get_size:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's size as a 2D vector (width, length).
 *
 * Returns: (transfer full): The size as a #GrlVector2
 */
GrlVector2 *
lrg_plane3d_get_size (LrgPlane3D *self)
{
	g_return_val_if_fail (LRG_IS_PLANE3D (self), NULL);

	return grl_vector2_new (self->width, self->length);
}

/**
 * lrg_plane3d_set_size:
 * @self: an #LrgPlane3D
 * @size: (transfer none): the size (width, length)
 *
 * Sets the plane's size from a 2D vector.
 */
void
lrg_plane3d_set_size (LrgPlane3D *self,
                      GrlVector2 *size)
{
	g_return_if_fail (LRG_IS_PLANE3D (self));
	g_return_if_fail (size != NULL);

	lrg_plane3d_set_width (self, size->x);
	lrg_plane3d_set_length (self, size->y);
}
