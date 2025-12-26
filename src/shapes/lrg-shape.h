/* lrg-shape.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for all drawable shapes.
 *
 * LrgShape provides common functionality for all shapes including
 * visibility, color, and z-index. It implements the LrgDrawable
 * interface so shapes can be rendered through the standard draw API.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../graphics/lrg-drawable.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHAPE (lrg_shape_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShape, lrg_shape, LRG, SHAPE, GObject)

/**
 * LrgShapeClass:
 * @parent_class: The parent class
 * @draw: Virtual method to render the shape
 * @get_bounds: Virtual method to get shape bounds (optional)
 *
 * The class structure for #LrgShape.
 *
 * Subclasses must implement the draw() method. The get_bounds() method
 * is optional and defaults to returning zeros.
 */
struct _LrgShapeClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgShapeClass::draw:
	 * @self: an #LrgShape
	 * @delta: time since last frame in seconds
	 *
	 * Render the shape.
	 *
	 * Subclasses must implement this to actually draw the shape using
	 * graylib drawing functions.
	 */
	void (*draw) (LrgShape *self,
	              gfloat    delta);

	/**
	 * LrgShapeClass::get_bounds:
	 * @self: an #LrgShape
	 * @out_bounds: (out): location to store the bounding rectangle
	 *
	 * Get the bounding rectangle of the shape.
	 *
	 * Optional; default implementation returns zeros.
	 */
	void (*get_bounds) (LrgShape     *self,
	                    GrlRectangle *out_bounds);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_shape_get_visible:
 * @self: an #LrgShape
 *
 * Gets whether the shape is visible.
 *
 * Invisible shapes are not rendered.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape_get_visible (LrgShape *self);

/**
 * lrg_shape_set_visible:
 * @self: an #LrgShape
 * @visible: whether the shape should be visible
 *
 * Sets whether the shape is visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape_set_visible (LrgShape *self,
                            gboolean  visible);

/**
 * lrg_shape_get_color:
 * @self: an #LrgShape
 *
 * Gets the shape's color.
 *
 * Returns: (transfer none): The color
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_shape_get_color (LrgShape *self);

/**
 * lrg_shape_set_color:
 * @self: an #LrgShape
 * @color: (transfer none): the color to set
 *
 * Sets the shape's color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape_set_color (LrgShape *self,
                          GrlColor *color);

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
LRG_AVAILABLE_IN_ALL
void lrg_shape_set_color_rgba (LrgShape *self,
                               guint8    r,
                               guint8    g,
                               guint8    b,
                               guint8    a);

/**
 * lrg_shape_get_z_index:
 * @self: an #LrgShape
 *
 * Gets the shape's z-index for draw ordering.
 *
 * Higher z-index shapes are drawn later (on top).
 *
 * Returns: The z-index
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shape_get_z_index (LrgShape *self);

/**
 * lrg_shape_set_z_index:
 * @self: an #LrgShape
 * @z_index: the z-index value
 *
 * Sets the shape's z-index for draw ordering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape_set_z_index (LrgShape *self,
                            gint      z_index);

G_END_DECLS
