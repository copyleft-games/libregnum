/* lrg-plane3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D plane shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_PLANE3D (lrg_plane3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlane3D, lrg_plane3d, LRG, PLANE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_plane3d_new:
 *
 * Creates a new plane at the origin with size 2x2.
 *
 * Returns: (transfer full): A new #LrgPlane3D
 */
LRG_AVAILABLE_IN_ALL
LrgPlane3D * lrg_plane3d_new (void);

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
LRG_AVAILABLE_IN_ALL
LrgPlane3D * lrg_plane3d_new_at (gfloat x,
                                 gfloat y,
                                 gfloat z,
                                 gfloat width,
                                 gfloat length);

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
LRG_AVAILABLE_IN_ALL
LrgPlane3D * lrg_plane3d_new_full (gfloat    x,
                                   gfloat    y,
                                   gfloat    z,
                                   gfloat    width,
                                   gfloat    length,
                                   GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_plane3d_get_width:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's width (X dimension).
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_plane3d_get_width (LrgPlane3D *self);

/**
 * lrg_plane3d_set_width:
 * @self: an #LrgPlane3D
 * @width: the width value
 *
 * Sets the plane's width (X dimension).
 */
LRG_AVAILABLE_IN_ALL
void lrg_plane3d_set_width (LrgPlane3D *self,
                            gfloat      width);

/**
 * lrg_plane3d_get_length:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's length (Z dimension).
 *
 * Returns: The length
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_plane3d_get_length (LrgPlane3D *self);

/**
 * lrg_plane3d_set_length:
 * @self: an #LrgPlane3D
 * @length: the length value
 *
 * Sets the plane's length (Z dimension).
 */
LRG_AVAILABLE_IN_ALL
void lrg_plane3d_set_length (LrgPlane3D *self,
                             gfloat      length);

/**
 * lrg_plane3d_get_size:
 * @self: an #LrgPlane3D
 *
 * Gets the plane's size as a 2D vector (width, length).
 *
 * Returns: (transfer full): The size as a #GrlVector2
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_plane3d_get_size (LrgPlane3D *self);

/**
 * lrg_plane3d_set_size:
 * @self: an #LrgPlane3D
 * @size: (transfer none): the size (width, length)
 *
 * Sets the plane's size from a 2D vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_plane3d_set_size (LrgPlane3D *self,
                           GrlVector2 *size);

G_END_DECLS
