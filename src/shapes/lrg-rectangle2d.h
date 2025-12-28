/* lrg-rectangle2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D rectangle shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_RECTANGLE2D (lrg_rectangle2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRectangle2D, lrg_rectangle2d, LRG, RECTANGLE2D, LrgShape2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_rectangle2d_new:
 *
 * Creates a new rectangle at the origin with default size (1x1).
 *
 * Returns: (transfer full): A new #LrgRectangle2D
 */
LRG_AVAILABLE_IN_ALL
LrgRectangle2D * lrg_rectangle2d_new (void);

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
LRG_AVAILABLE_IN_ALL
LrgRectangle2D * lrg_rectangle2d_new_at (gfloat x,
                                          gfloat y,
                                          gfloat width,
                                          gfloat height);

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
LRG_AVAILABLE_IN_ALL
LrgRectangle2D * lrg_rectangle2d_new_full (gfloat    x,
                                            gfloat    y,
                                            gfloat    width,
                                            gfloat    height,
                                            GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_rectangle2d_get_width:
 * @self: an #LrgRectangle2D
 *
 * Gets the rectangle width.
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rectangle2d_get_width (LrgRectangle2D *self);

/**
 * lrg_rectangle2d_set_width:
 * @self: an #LrgRectangle2D
 * @width: the width
 *
 * Sets the rectangle width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rectangle2d_set_width (LrgRectangle2D *self,
                                 gfloat          width);

/**
 * lrg_rectangle2d_get_height:
 * @self: an #LrgRectangle2D
 *
 * Gets the rectangle height.
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rectangle2d_get_height (LrgRectangle2D *self);

/**
 * lrg_rectangle2d_set_height:
 * @self: an #LrgRectangle2D
 * @height: the height
 *
 * Sets the rectangle height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rectangle2d_set_height (LrgRectangle2D *self,
                                  gfloat          height);

/**
 * lrg_rectangle2d_get_filled:
 * @self: an #LrgRectangle2D
 *
 * Gets whether the rectangle is filled.
 *
 * Returns: %TRUE if filled, %FALSE for outline only
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rectangle2d_get_filled (LrgRectangle2D *self);

/**
 * lrg_rectangle2d_set_filled:
 * @self: an #LrgRectangle2D
 * @filled: %TRUE for filled, %FALSE for outline only
 *
 * Sets whether the rectangle is filled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rectangle2d_set_filled (LrgRectangle2D *self,
                                  gboolean        filled);

/**
 * lrg_rectangle2d_get_line_thickness:
 * @self: an #LrgRectangle2D
 *
 * Gets the line thickness for outline mode.
 *
 * Returns: The line thickness
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rectangle2d_get_line_thickness (LrgRectangle2D *self);

/**
 * lrg_rectangle2d_set_line_thickness:
 * @self: an #LrgRectangle2D
 * @thickness: the line thickness
 *
 * Sets the line thickness for outline mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rectangle2d_set_line_thickness (LrgRectangle2D *self,
                                          gfloat          thickness);

/**
 * lrg_rectangle2d_get_corner_radius:
 * @self: an #LrgRectangle2D
 *
 * Gets the corner radius for rounded rectangles.
 *
 * Returns: The corner radius (0.0 for sharp corners)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rectangle2d_get_corner_radius (LrgRectangle2D *self);

/**
 * lrg_rectangle2d_set_corner_radius:
 * @self: an #LrgRectangle2D
 * @radius: the corner radius (0.0 for sharp corners)
 *
 * Sets the corner radius for rounded rectangles.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rectangle2d_set_corner_radius (LrgRectangle2D *self,
                                         gfloat          radius);

G_END_DECLS
