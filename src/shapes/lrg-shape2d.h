/* lrg-shape2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 2D shapes.
 *
 * LrgShape2D extends LrgShape with 2D-specific properties such as
 * screen-space position (x, y).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHAPE2D (lrg_shape2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShape2D, lrg_shape2d, LRG, SHAPE2D, LrgShape)

/**
 * LrgShape2DClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgShape2D.
 *
 * Subclasses inherit x, y position properties and should
 * implement the draw() virtual method from #LrgShapeClass.
 */
struct _LrgShape2DClass
{
	LrgShapeClass parent_class;

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_shape2d_get_x:
 * @self: an #LrgShape2D
 *
 * Gets the shape's X position (screen space).
 *
 * Returns: The X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape2d_get_x (LrgShape2D *self);

/**
 * lrg_shape2d_set_x:
 * @self: an #LrgShape2D
 * @x: the X coordinate
 *
 * Sets the shape's X position (screen space).
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape2d_set_x (LrgShape2D *self,
                        gfloat      x);

/**
 * lrg_shape2d_get_y:
 * @self: an #LrgShape2D
 *
 * Gets the shape's Y position (screen space).
 *
 * Returns: The Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape2d_get_y (LrgShape2D *self);

/**
 * lrg_shape2d_set_y:
 * @self: an #LrgShape2D
 * @y: the Y coordinate
 *
 * Sets the shape's Y position (screen space).
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape2d_set_y (LrgShape2D *self,
                        gfloat      y);

/**
 * lrg_shape2d_set_position:
 * @self: an #LrgShape2D
 * @x: the X coordinate
 * @y: the Y coordinate
 *
 * Sets the shape's position using X, Y coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape2d_set_position (LrgShape2D *self,
                               gfloat      x,
                               gfloat      y);

/**
 * lrg_shape2d_get_position:
 * @self: an #LrgShape2D
 * @x: (out) (optional): return location for X coordinate
 * @y: (out) (optional): return location for Y coordinate
 *
 * Gets the shape's position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape2d_get_position (LrgShape2D *self,
                               gfloat     *x,
                               gfloat     *y);

G_END_DECLS
