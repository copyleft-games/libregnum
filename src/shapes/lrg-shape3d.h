/* lrg-shape3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for 3D shapes.
 *
 * LrgShape3D extends LrgShape with 3D-specific properties such as
 * position (GrlVector3) and wireframe rendering mode.
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

#define LRG_TYPE_SHAPE3D (lrg_shape3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShape3D, lrg_shape3d, LRG, SHAPE3D, LrgShape)

/**
 * LrgShape3DClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgShape3D.
 *
 * Subclasses inherit position and wireframe properties and should
 * implement the draw() virtual method from #LrgShapeClass.
 */
struct _LrgShape3DClass
{
	LrgShapeClass parent_class;

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_shape3d_get_position:
 * @self: an #LrgShape3D
 *
 * Gets the shape's 3D position.
 *
 * Returns: (transfer none): The position vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_shape3d_get_position (LrgShape3D *self);

/**
 * lrg_shape3d_set_position:
 * @self: an #LrgShape3D
 * @position: (transfer none): the position to set
 *
 * Sets the shape's 3D position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_position (LrgShape3D *self,
                               GrlVector3 *position);

/**
 * lrg_shape3d_set_position_xyz:
 * @self: an #LrgShape3D
 * @x: the X coordinate
 * @y: the Y coordinate
 * @z: the Z coordinate
 *
 * Sets the shape's position using X, Y, Z coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_position_xyz (LrgShape3D *self,
                                   gfloat      x,
                                   gfloat      y,
                                   gfloat      z);

/**
 * lrg_shape3d_get_x:
 * @self: an #LrgShape3D
 *
 * Gets the shape's X position.
 *
 * Returns: The X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_x (LrgShape3D *self);

/**
 * lrg_shape3d_get_y:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Y position.
 *
 * Returns: The Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_y (LrgShape3D *self);

/**
 * lrg_shape3d_get_z:
 * @self: an #LrgShape3D
 *
 * Gets the shape's Z position.
 *
 * Returns: The Z coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shape3d_get_z (LrgShape3D *self);

/**
 * lrg_shape3d_get_wireframe:
 * @self: an #LrgShape3D
 *
 * Gets whether the shape is rendered in wireframe mode.
 *
 * Returns: %TRUE if wireframe mode is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shape3d_get_wireframe (LrgShape3D *self);

/**
 * lrg_shape3d_set_wireframe:
 * @self: an #LrgShape3D
 * @wireframe: whether to enable wireframe mode
 *
 * Sets whether the shape is rendered in wireframe mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shape3d_set_wireframe (LrgShape3D *self,
                                gboolean    wireframe);

G_END_DECLS
