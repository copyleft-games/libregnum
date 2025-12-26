/* lrg-line3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D line segment shape.
 */

#include "lrg-line3d.h"

/**
 * LrgLine3D:
 *
 * A 3D line segment shape.
 *
 * Renders a line from start (position) to end using graylib's
 * line drawing functions. The start point is inherited from
 * LrgShape3D's position property.
 */
struct _LrgLine3D
{
	LrgShape3D parent_instance;

	GrlVector3 *end;
};

G_DEFINE_FINAL_TYPE (LrgLine3D, lrg_line3d, LRG_TYPE_SHAPE3D)

enum
{
	PROP_0,
	PROP_END,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_line3d_draw (LrgShape *shape,
                 gfloat    delta)
{
	LrgLine3D   *self  = LRG_LINE3D (shape);
	GrlVector3  *start = lrg_shape3d_get_position (LRG_SHAPE3D (self));
	GrlColor    *color = lrg_shape_get_color (shape);

	grl_draw_line_3d (start, self->end, color);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_line3d_finalize (GObject *object)
{
	LrgLine3D *self = LRG_LINE3D (object);

	g_clear_pointer (&self->end, grl_vector3_free);

	G_OBJECT_CLASS (lrg_line3d_parent_class)->finalize (object);
}

static void
lrg_line3d_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgLine3D *self = LRG_LINE3D (object);

	switch (prop_id)
	{
	case PROP_END:
		g_value_set_boxed (value, self->end);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_line3d_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgLine3D *self = LRG_LINE3D (object);

	switch (prop_id)
	{
	case PROP_END:
		g_clear_pointer (&self->end, grl_vector3_free);
		self->end = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_line3d_class_init (LrgLine3DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->finalize     = lrg_line3d_finalize;
	object_class->get_property = lrg_line3d_get_property;
	object_class->set_property = lrg_line3d_set_property;

	shape_class->draw = lrg_line3d_draw;

	/**
	 * LrgLine3D:end:
	 *
	 * The line's end position.
	 */
	properties[PROP_END] =
		g_param_spec_boxed ("end",
		                    "End",
		                    "The line's end position",
		                    GRL_TYPE_VECTOR3,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_line3d_init (LrgLine3D *self)
{
	self->end = grl_vector3_new (1.0f, 0.0f, 0.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_line3d_new:
 *
 * Creates a new line from origin to (1, 0, 0).
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LrgLine3D *
lrg_line3d_new (void)
{
	return g_object_new (LRG_TYPE_LINE3D, NULL);
}

/**
 * lrg_line3d_new_from_to:
 * @start_x: start X position
 * @start_y: start Y position
 * @start_z: start Z position
 * @end_x: end X position
 * @end_y: end Y position
 * @end_z: end Z position
 *
 * Creates a new line from start to end point.
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LrgLine3D *
lrg_line3d_new_from_to (gfloat start_x,
                        gfloat start_y,
                        gfloat start_z,
                        gfloat end_x,
                        gfloat end_y,
                        gfloat end_z)
{
	LrgLine3D *line;

	line = g_object_new (LRG_TYPE_LINE3D, NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (line), start_x, start_y, start_z);
	lrg_line3d_set_end_xyz (line, end_x, end_y, end_z);

	return line;
}

/**
 * lrg_line3d_new_from_to_v:
 * @start: (transfer none): start position
 * @end: (transfer none): end position
 *
 * Creates a new line from start to end vectors.
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LrgLine3D *
lrg_line3d_new_from_to_v (GrlVector3 *start,
                          GrlVector3 *end)
{
	LrgLine3D *line;

	g_return_val_if_fail (start != NULL, NULL);
	g_return_val_if_fail (end != NULL, NULL);

	line = g_object_new (LRG_TYPE_LINE3D,
	                     "end", end,
	                     NULL);
	lrg_shape3d_set_position (LRG_SHAPE3D (line), start);

	return line;
}

/**
 * lrg_line3d_new_full:
 * @start_x: start X position
 * @start_y: start Y position
 * @start_z: start Z position
 * @end_x: end X position
 * @end_y: end Y position
 * @end_z: end Z position
 * @color: (transfer none): the line color
 *
 * Creates a new line with full configuration.
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LrgLine3D *
lrg_line3d_new_full (gfloat    start_x,
                     gfloat    start_y,
                     gfloat    start_z,
                     gfloat    end_x,
                     gfloat    end_y,
                     gfloat    end_z,
                     GrlColor *color)
{
	LrgLine3D *line;

	line = g_object_new (LRG_TYPE_LINE3D,
	                     "color", color,
	                     NULL);
	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (line), start_x, start_y, start_z);
	lrg_line3d_set_end_xyz (line, end_x, end_y, end_z);

	return line;
}

/* Property accessors */

/**
 * lrg_line3d_get_end:
 * @self: an #LrgLine3D
 *
 * Gets the line's end position.
 *
 * Returns: (transfer none): The end position vector
 */
GrlVector3 *
lrg_line3d_get_end (LrgLine3D *self)
{
	g_return_val_if_fail (LRG_IS_LINE3D (self), NULL);

	return self->end;
}

/**
 * lrg_line3d_set_end:
 * @self: an #LrgLine3D
 * @end: (transfer none): the end position to set
 *
 * Sets the line's end position.
 */
void
lrg_line3d_set_end (LrgLine3D  *self,
                    GrlVector3 *end)
{
	g_return_if_fail (LRG_IS_LINE3D (self));
	g_return_if_fail (end != NULL);

	g_clear_pointer (&self->end, grl_vector3_free);
	self->end = grl_vector3_copy (end);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END]);
}

/**
 * lrg_line3d_set_end_xyz:
 * @self: an #LrgLine3D
 * @x: the end X coordinate
 * @y: the end Y coordinate
 * @z: the end Z coordinate
 *
 * Sets the line's end position using X, Y, Z coordinates.
 */
void
lrg_line3d_set_end_xyz (LrgLine3D *self,
                        gfloat     x,
                        gfloat     y,
                        gfloat     z)
{
	g_autoptr(GrlVector3) new_end = NULL;

	g_return_if_fail (LRG_IS_LINE3D (self));

	new_end = grl_vector3_new (x, y, z);
	g_clear_pointer (&self->end, grl_vector3_free);
	self->end = g_steal_pointer (&new_end);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END]);
}

/**
 * lrg_line3d_get_end_x:
 * @self: an #LrgLine3D
 *
 * Gets the line's end X position.
 *
 * Returns: The end X coordinate
 */
gfloat
lrg_line3d_get_end_x (LrgLine3D *self)
{
	g_return_val_if_fail (LRG_IS_LINE3D (self), 0.0f);

	return self->end->x;
}

/**
 * lrg_line3d_get_end_y:
 * @self: an #LrgLine3D
 *
 * Gets the line's end Y position.
 *
 * Returns: The end Y coordinate
 */
gfloat
lrg_line3d_get_end_y (LrgLine3D *self)
{
	g_return_val_if_fail (LRG_IS_LINE3D (self), 0.0f);

	return self->end->y;
}

/**
 * lrg_line3d_get_end_z:
 * @self: an #LrgLine3D
 *
 * Gets the line's end Z position.
 *
 * Returns: The end Z coordinate
 */
gfloat
lrg_line3d_get_end_z (LrgLine3D *self)
{
	g_return_val_if_fail (LRG_IS_LINE3D (self), 0.0f);

	return self->end->z;
}

/**
 * lrg_line3d_set_points:
 * @self: an #LrgLine3D
 * @start_x: start X position
 * @start_y: start Y position
 * @start_z: start Z position
 * @end_x: end X position
 * @end_y: end Y position
 * @end_z: end Z position
 *
 * Sets both start and end points at once.
 */
void
lrg_line3d_set_points (LrgLine3D *self,
                       gfloat     start_x,
                       gfloat     start_y,
                       gfloat     start_z,
                       gfloat     end_x,
                       gfloat     end_y,
                       gfloat     end_z)
{
	g_return_if_fail (LRG_IS_LINE3D (self));

	g_object_freeze_notify (G_OBJECT (self));

	lrg_shape3d_set_position_xyz (LRG_SHAPE3D (self), start_x, start_y, start_z);
	lrg_line3d_set_end_xyz (self, end_x, end_y, end_z);

	g_object_thaw_notify (G_OBJECT (self));
}
