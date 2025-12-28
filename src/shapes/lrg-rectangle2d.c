/* lrg-rectangle2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D rectangle shape.
 */

#include "lrg-rectangle2d.h"

/**
 * LrgRectangle2D:
 *
 * A 2D rectangle shape.
 *
 * Renders a rectangle using graylib's rectangle drawing functions.
 * Supports filled or outline modes, rounded corners, and configurable
 * line thickness for outline mode.
 */
struct _LrgRectangle2D
{
	LrgShape2D parent_instance;

	gfloat   width;
	gfloat   height;
	gboolean filled;
	gfloat   line_thickness;
	gfloat   corner_radius;
};

G_DEFINE_FINAL_TYPE (LrgRectangle2D, lrg_rectangle2d, LRG_TYPE_SHAPE2D)

enum
{
	PROP_0,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_FILLED,
	PROP_LINE_THICKNESS,
	PROP_CORNER_RADIUS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Virtual Method Implementation
 * ========================================================================== */

static void
lrg_rectangle2d_draw (LrgShape *shape,
                      gfloat    delta)
{
	LrgRectangle2D *self  = LRG_RECTANGLE2D (shape);
	GrlColor       *color = lrg_shape_get_color (shape);
	gfloat          x     = lrg_shape2d_get_x (LRG_SHAPE2D (self));
	gfloat          y     = lrg_shape2d_get_y (LRG_SHAPE2D (self));

	if (self->corner_radius > 0.0f)
	{
		/* Rounded rectangle */
		g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y,
		                                                   self->width,
		                                                   self->height);
		gfloat roundness = self->corner_radius / (self->width < self->height
		                                          ? self->width
		                                          : self->height);

		if (self->filled)
		{
			grl_draw_rectangle_rounded (rect, roundness, 8, color);
		}
		else
		{
			grl_draw_rectangle_rounded_lines_ex (rect, roundness, 8,
			                                     self->line_thickness, color);
		}
	}
	else
	{
		/* Sharp corners */
		if (self->filled)
		{
			grl_draw_rectangle ((gint) x, (gint) y,
			                    (gint) self->width, (gint) self->height,
			                    color);
		}
		else
		{
			g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y,
			                                                   self->width,
			                                                   self->height);
			grl_draw_rectangle_lines_ex (rect, self->line_thickness, color);
		}
	}
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_rectangle2d_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgRectangle2D *self = LRG_RECTANGLE2D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		g_value_set_float (value, self->width);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case PROP_FILLED:
		g_value_set_boolean (value, self->filled);
		break;
	case PROP_LINE_THICKNESS:
		g_value_set_float (value, self->line_thickness);
		break;
	case PROP_CORNER_RADIUS:
		g_value_set_float (value, self->corner_radius);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_rectangle2d_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgRectangle2D *self = LRG_RECTANGLE2D (object);

	switch (prop_id)
	{
	case PROP_WIDTH:
		self->width = g_value_get_float (value);
		break;
	case PROP_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	case PROP_FILLED:
		self->filled = g_value_get_boolean (value);
		break;
	case PROP_LINE_THICKNESS:
		self->line_thickness = g_value_get_float (value);
		break;
	case PROP_CORNER_RADIUS:
		self->corner_radius = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_rectangle2d_class_init (LrgRectangle2DClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgShapeClass *shape_class  = LRG_SHAPE_CLASS (klass);

	object_class->get_property = lrg_rectangle2d_get_property;
	object_class->set_property = lrg_rectangle2d_set_property;

	shape_class->draw = lrg_rectangle2d_draw;

	/**
	 * LrgRectangle2D:width:
	 *
	 * The rectangle width.
	 */
	properties[PROP_WIDTH] =
		g_param_spec_float ("width",
		                    "Width",
		                    "The rectangle width",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRectangle2D:height:
	 *
	 * The rectangle height.
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_float ("height",
		                    "Height",
		                    "The rectangle height",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRectangle2D:filled:
	 *
	 * Whether the rectangle is filled.
	 */
	properties[PROP_FILLED] =
		g_param_spec_boolean ("filled",
		                      "Filled",
		                      "Whether the rectangle is filled",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRectangle2D:line-thickness:
	 *
	 * The line thickness for outline mode.
	 */
	properties[PROP_LINE_THICKNESS] =
		g_param_spec_float ("line-thickness",
		                    "Line Thickness",
		                    "The line thickness for outline mode",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRectangle2D:corner-radius:
	 *
	 * The corner radius for rounded rectangles.
	 */
	properties[PROP_CORNER_RADIUS] =
		g_param_spec_float ("corner-radius",
		                    "Corner Radius",
		                    "The corner radius for rounded rectangles",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_rectangle2d_init (LrgRectangle2D *self)
{
	self->width          = 1.0f;
	self->height         = 1.0f;
	self->filled         = TRUE;
	self->line_thickness = 1.0f;
	self->corner_radius  = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_rectangle2d_new:
 *
 * Creates a new rectangle at the origin with default size (1x1).
 *
 * Returns: (transfer full): A new #LrgRectangle2D
 */
LrgRectangle2D *
lrg_rectangle2d_new (void)
{
	return g_object_new (LRG_TYPE_RECTANGLE2D, NULL);
}

/**
 * lrg_rectangle2d_new_at:
 * @x: X position
 * @y: Y position
 * @width: rectangle width
 * @height: rectangle height
 *
 * Creates a new rectangle at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgRectangle2D
 */
LrgRectangle2D *
lrg_rectangle2d_new_at (gfloat x,
                        gfloat y,
                        gfloat width,
                        gfloat height)
{
	return g_object_new (LRG_TYPE_RECTANGLE2D,
	                     "x", x,
	                     "y", y,
	                     "width", width,
	                     "height", height,
	                     NULL);
}

/**
 * lrg_rectangle2d_new_full:
 * @x: X position
 * @y: Y position
 * @width: rectangle width
 * @height: rectangle height
 * @color: (transfer none): the rectangle color
 *
 * Creates a new rectangle with full configuration.
 *
 * Returns: (transfer full): A new #LrgRectangle2D
 */
LrgRectangle2D *
lrg_rectangle2d_new_full (gfloat    x,
                          gfloat    y,
                          gfloat    width,
                          gfloat    height,
                          GrlColor *color)
{
	return g_object_new (LRG_TYPE_RECTANGLE2D,
	                     "x", x,
	                     "y", y,
	                     "width", width,
	                     "height", height,
	                     "color", color,
	                     NULL);
}

/* Property accessors */

/**
 * lrg_rectangle2d_get_width:
 * @self: an #LrgRectangle2D
 *
 * Gets the rectangle width.
 *
 * Returns: The width
 */
gfloat
lrg_rectangle2d_get_width (LrgRectangle2D *self)
{
	g_return_val_if_fail (LRG_IS_RECTANGLE2D (self), 0.0f);

	return self->width;
}

/**
 * lrg_rectangle2d_set_width:
 * @self: an #LrgRectangle2D
 * @width: the width
 *
 * Sets the rectangle width.
 */
void
lrg_rectangle2d_set_width (LrgRectangle2D *self,
                           gfloat          width)
{
	g_return_if_fail (LRG_IS_RECTANGLE2D (self));
	g_return_if_fail (width >= 0.0f);

	if (self->width != width)
	{
		self->width = width;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
	}
}

/**
 * lrg_rectangle2d_get_height:
 * @self: an #LrgRectangle2D
 *
 * Gets the rectangle height.
 *
 * Returns: The height
 */
gfloat
lrg_rectangle2d_get_height (LrgRectangle2D *self)
{
	g_return_val_if_fail (LRG_IS_RECTANGLE2D (self), 0.0f);

	return self->height;
}

/**
 * lrg_rectangle2d_set_height:
 * @self: an #LrgRectangle2D
 * @height: the height
 *
 * Sets the rectangle height.
 */
void
lrg_rectangle2d_set_height (LrgRectangle2D *self,
                            gfloat          height)
{
	g_return_if_fail (LRG_IS_RECTANGLE2D (self));
	g_return_if_fail (height >= 0.0f);

	if (self->height != height)
	{
		self->height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
	}
}

/**
 * lrg_rectangle2d_get_filled:
 * @self: an #LrgRectangle2D
 *
 * Gets whether the rectangle is filled.
 *
 * Returns: %TRUE if filled, %FALSE for outline only
 */
gboolean
lrg_rectangle2d_get_filled (LrgRectangle2D *self)
{
	g_return_val_if_fail (LRG_IS_RECTANGLE2D (self), TRUE);

	return self->filled;
}

/**
 * lrg_rectangle2d_set_filled:
 * @self: an #LrgRectangle2D
 * @filled: %TRUE for filled, %FALSE for outline only
 *
 * Sets whether the rectangle is filled.
 */
void
lrg_rectangle2d_set_filled (LrgRectangle2D *self,
                            gboolean        filled)
{
	g_return_if_fail (LRG_IS_RECTANGLE2D (self));

	if (self->filled != filled)
	{
		self->filled = filled;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILLED]);
	}
}

/**
 * lrg_rectangle2d_get_line_thickness:
 * @self: an #LrgRectangle2D
 *
 * Gets the line thickness for outline mode.
 *
 * Returns: The line thickness
 */
gfloat
lrg_rectangle2d_get_line_thickness (LrgRectangle2D *self)
{
	g_return_val_if_fail (LRG_IS_RECTANGLE2D (self), 1.0f);

	return self->line_thickness;
}

/**
 * lrg_rectangle2d_set_line_thickness:
 * @self: an #LrgRectangle2D
 * @thickness: the line thickness
 *
 * Sets the line thickness for outline mode.
 */
void
lrg_rectangle2d_set_line_thickness (LrgRectangle2D *self,
                                    gfloat          thickness)
{
	g_return_if_fail (LRG_IS_RECTANGLE2D (self));
	g_return_if_fail (thickness >= 0.0f);

	if (self->line_thickness != thickness)
	{
		self->line_thickness = thickness;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_THICKNESS]);
	}
}

/**
 * lrg_rectangle2d_get_corner_radius:
 * @self: an #LrgRectangle2D
 *
 * Gets the corner radius for rounded rectangles.
 *
 * Returns: The corner radius (0.0 for sharp corners)
 */
gfloat
lrg_rectangle2d_get_corner_radius (LrgRectangle2D *self)
{
	g_return_val_if_fail (LRG_IS_RECTANGLE2D (self), 0.0f);

	return self->corner_radius;
}

/**
 * lrg_rectangle2d_set_corner_radius:
 * @self: an #LrgRectangle2D
 * @radius: the corner radius (0.0 for sharp corners)
 *
 * Sets the corner radius for rounded rectangles.
 */
void
lrg_rectangle2d_set_corner_radius (LrgRectangle2D *self,
                                   gfloat          radius)
{
	g_return_if_fail (LRG_IS_RECTANGLE2D (self));
	g_return_if_fail (radius >= 0.0f);

	if (self->corner_radius != radius)
	{
		self->corner_radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
	}
}
