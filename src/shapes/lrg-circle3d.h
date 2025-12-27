/* lrg-circle3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D circle shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-shape3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CIRCLE3D (lrg_circle3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCircle3D, lrg_circle3d, LRG, CIRCLE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_circle3d_new:
 *
 * Creates a new circle at the origin with radius 1.0 on the XZ plane.
 *
 * Returns: (transfer full): A new #LrgCircle3D
 */
LRG_AVAILABLE_IN_ALL
LrgCircle3D * lrg_circle3d_new (void);

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
LRG_AVAILABLE_IN_ALL
LrgCircle3D * lrg_circle3d_new_at (gfloat x,
                                   gfloat y,
                                   gfloat z,
                                   gfloat radius);

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
LRG_AVAILABLE_IN_ALL
LrgCircle3D * lrg_circle3d_new_full (gfloat    x,
                                     gfloat    y,
                                     gfloat    z,
                                     gfloat    radius,
                                     gint      vertices,
                                     GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_circle3d_get_radius:
 * @self: an #LrgCircle3D
 *
 * Gets the circle's radius.
 *
 * Returns: The radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_circle3d_get_radius (LrgCircle3D *self);

/**
 * lrg_circle3d_set_radius:
 * @self: an #LrgCircle3D
 * @radius: the radius value
 *
 * Sets the circle's radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle3d_set_radius (LrgCircle3D *self,
                              gfloat       radius);

/**
 * lrg_circle3d_get_vertices:
 * @self: an #LrgCircle3D
 *
 * Gets the number of vertices.
 *
 * Returns: The number of vertices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_circle3d_get_vertices (LrgCircle3D *self);

/**
 * lrg_circle3d_set_vertices:
 * @self: an #LrgCircle3D
 * @vertices: the number of vertices
 *
 * Sets the number of vertices.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle3d_set_vertices (LrgCircle3D *self,
                                gint         vertices);

/**
 * lrg_circle3d_get_fill_type:
 * @self: an #LrgCircle3D
 *
 * Gets the fill type.
 *
 * Returns: The #LrgCircleFillType
 */
LRG_AVAILABLE_IN_ALL
LrgCircleFillType lrg_circle3d_get_fill_type (LrgCircle3D *self);

/**
 * lrg_circle3d_set_fill_type:
 * @self: an #LrgCircle3D
 * @fill_type: the fill type
 *
 * Sets the fill type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle3d_set_fill_type (LrgCircle3D       *self,
                                 LrgCircleFillType  fill_type);

/**
 * lrg_circle3d_get_rotation_axis:
 * @self: an #LrgCircle3D
 *
 * Gets the rotation axis.
 *
 * Returns: (transfer full): The rotation axis
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_circle3d_get_rotation_axis (LrgCircle3D *self);

/**
 * lrg_circle3d_set_rotation_axis:
 * @self: an #LrgCircle3D
 * @axis: (transfer none): the rotation axis
 *
 * Sets the rotation axis.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle3d_set_rotation_axis (LrgCircle3D *self,
                                     GrlVector3  *axis);

/**
 * lrg_circle3d_get_rotation_angle:
 * @self: an #LrgCircle3D
 *
 * Gets the rotation angle in degrees.
 *
 * Returns: The rotation angle
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_circle3d_get_rotation_angle (LrgCircle3D *self);

/**
 * lrg_circle3d_set_rotation_angle:
 * @self: an #LrgCircle3D
 * @angle: the rotation angle in degrees
 *
 * Sets the rotation angle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle3d_set_rotation_angle (LrgCircle3D *self,
                                      gfloat       angle);

G_END_DECLS
