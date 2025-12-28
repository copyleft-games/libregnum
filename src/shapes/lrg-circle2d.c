/* lrg-circle2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D circle shape.
 */

#include "lrg-circle2d.h"

/**
 * LrgCircle2D:
 *
 * A 2D circle shape.
 *
 * Renders a circle using graylib's circle drawing functions.
 * Supports filled or outline modes.
 */
struct _LrgCircle2D
{
	LrgShape2D parent_instance;

	gfloat   radius;
	gboolean filled;
};

G_DEFINE_FINAL_TYPE (LrgCircle2D, lrg_circle2d, LRG_TYPE_SHAPE2D)

enum
{
	PROP_0,
	PROP_RADIUS,
	PROP_FILLED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_circle2d_draw (LrgShape *shape,
                   gfloat    delta)
{
	LrgCircle2D *self  = LRG_CIRCLE2D (shape);
	GrlColor    *color = lrg_shape_get_color (shape);
	gfloat       x     = lrg_shape2d_get_x (LRG_SHAPE2D (self));
	gfloat       y     = lrg_shape2d_get_y (LRG_SHAPE2D (self));

	if (self->filled)
	{
		grl_draw_circle ((gint) x, (gint) y, self->radius, color);
	}
	else
	{
		grl_draw_circle_lines ((gint) x, (gint) y, self->radius, color);
	}
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_circle2d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgCircle2D *self = LRG_CIRCLE2D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		g_value_set_float (value, self->radius);
		break;
	case PROP_FILLED:
		g_value_set_boolean (value, self->filled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_circle2d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgCircle2D *self = LRG_CIRCLE2D (object);

	switch (prop_id)
	{
	case PROP_RADIUS:
		self->radius = g_value_get_float (value);
		break;
	case PROP_FILLED:
		self->filled = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_circle2d_class_init (LrgCircle2DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_circle2d_get_property;
	object_class->set_property = lrg_circle2d_set_property;

	shape_class->draw = lrg_circle2d_draw;

	/**
	 * LrgCircle2D:radius:
	 *
	 * The circle radius.
	 */
	properties[PROP_RADIUS] =
		g_param_spec_float ("radius",
		                    "Radius",
		                    "The circle radius",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCircle2D:filled:
	 *
	 * Whether the circle is filled.
	 */
	properties[PROP_FILLED] =
		g_param_spec_boolean ("filled",
		                      "Filled",
		                      "Whether the circle is filled",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_circle2d_init (LrgCircle2D *self)
{
	self->radius = 1.0f;
	self->filled = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_circle2d_new:
 *
 * Creates a new circle at the origin with default radius (1.0).
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LrgCircle2D *
lrg_circle2d_new (void)
{
	return g_object_new (LRG_TYPE_CIRCLE2D, NULL);
}

/**
 * lrg_circle2d_new_at:
 * @x: X position (center)
 * @y: Y position (center)
 * @radius: circle radius
 *
 * Creates a new circle at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LrgCircle2D *
lrg_circle2d_new_at (gfloat x,
                     gfloat y,
                     gfloat radius)
{
	return g_object_new (LRG_TYPE_CIRCLE2D,
	                     "x", x,
	                     "y", y,
	                     "radius", radius,
	                     NULL);
}

/**
 * lrg_circle2d_new_full:
 * @x: X position (center)
 * @y: Y position (center)
 * @radius: circle radius
 * @color: (transfer none): the circle color
 *
 * Creates a new circle with full configuration.
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LrgCircle2D *
lrg_circle2d_new_full (gfloat    x,
                       gfloat    y,
                       gfloat    radius,
                       GrlColor *color)
{
	return g_object_new (LRG_TYPE_CIRCLE2D,
	                     "x", x,
	                     "y", y,
	                     "radius", radius,
	                     "color", color,
	                     NULL);
}

/* Property accessors */

/**
 * lrg_circle2d_get_radius:
 * @self: an #LrgCircle2D
 *
 * Gets the circle radius.
 *
 * Returns: The radius
 */
gfloat
lrg_circle2d_get_radius (LrgCircle2D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE2D (self), 0.0f);

	return self->radius;
}

/**
 * lrg_circle2d_set_radius:
 * @self: an #LrgCircle2D
 * @radius: the radius
 *
 * Sets the circle radius.
 */
void
lrg_circle2d_set_radius (LrgCircle2D *self,
                         gfloat       radius)
{
	g_return_if_fail (LRG_IS_CIRCLE2D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->radius != radius)
	{
		self->radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
	}
}

/**
 * lrg_circle2d_get_filled:
 * @self: an #LrgCircle2D
 *
 * Gets whether the circle is filled.
 *
 * Returns: %TRUE if filled, %FALSE for outline only
 */
gboolean
lrg_circle2d_get_filled (LrgCircle2D *self)
{
	g_return_val_if_fail (LRG_IS_CIRCLE2D (self), TRUE);

	return self->filled;
}

/**
 * lrg_circle2d_set_filled:
 * @self: an #LrgCircle2D
 * @filled: %TRUE for filled, %FALSE for outline only
 *
 * Sets whether the circle is filled.
 */
void
lrg_circle2d_set_filled (LrgCircle2D *self,
                         gboolean     filled)
{
	g_return_if_fail (LRG_IS_CIRCLE2D (self));

	if (self->filled != filled)
	{
		self->filled = filled;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILLED]);
	}
}
