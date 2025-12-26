/* lrg-cube3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cube/box shape.
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

#define LRG_TYPE_CUBE3D (lrg_cube3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCube3D, lrg_cube3d, LRG, CUBE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_cube3d_new:
 *
 * Creates a new unit cube at the origin.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LRG_AVAILABLE_IN_ALL
LrgCube3D * lrg_cube3d_new (void);

/**
 * lrg_cube3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: cube width (X axis)
 * @height: cube height (Y axis)
 * @depth: cube depth (Z axis)
 *
 * Creates a new cube at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LRG_AVAILABLE_IN_ALL
LrgCube3D * lrg_cube3d_new_at (gfloat x,
                               gfloat y,
                               gfloat z,
                               gfloat width,
                               gfloat height,
                               gfloat depth);

/**
 * lrg_cube3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: cube width (X axis)
 * @height: cube height (Y axis)
 * @depth: cube depth (Z axis)
 * @color: (transfer none): the cube color
 *
 * Creates a new cube with full configuration.
 *
 * Returns: (transfer full): A new #LrgCube3D
 */
LRG_AVAILABLE_IN_ALL
LrgCube3D * lrg_cube3d_new_full (gfloat    x,
                                 gfloat    y,
                                 gfloat    z,
                                 gfloat    width,
                                 gfloat    height,
                                 gfloat    depth,
                                 GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_cube3d_get_width:
 * @self: an #LrgCube3D
 *
 * Gets the cube's width (X axis).
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cube3d_get_width (LrgCube3D *self);

/**
 * lrg_cube3d_set_width:
 * @self: an #LrgCube3D
 * @width: the width value
 *
 * Sets the cube's width (X axis).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cube3d_set_width (LrgCube3D *self,
                           gfloat     width);

/**
 * lrg_cube3d_get_height:
 * @self: an #LrgCube3D
 *
 * Gets the cube's height (Y axis).
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cube3d_get_height (LrgCube3D *self);

/**
 * lrg_cube3d_set_height:
 * @self: an #LrgCube3D
 * @height: the height value
 *
 * Sets the cube's height (Y axis).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cube3d_set_height (LrgCube3D *self,
                            gfloat     height);

/**
 * lrg_cube3d_get_depth:
 * @self: an #LrgCube3D
 *
 * Gets the cube's depth (Z axis).
 *
 * Returns: The depth
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cube3d_get_depth (LrgCube3D *self);

/**
 * lrg_cube3d_set_depth:
 * @self: an #LrgCube3D
 * @depth: the depth value
 *
 * Sets the cube's depth (Z axis).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cube3d_set_depth (LrgCube3D *self,
                           gfloat     depth);

/**
 * lrg_cube3d_set_size:
 * @self: an #LrgCube3D
 * @width: the width value
 * @height: the height value
 * @depth: the depth value
 *
 * Sets all dimensions at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cube3d_set_size (LrgCube3D *self,
                          gfloat     width,
                          gfloat     height,
                          gfloat     depth);

/**
 * lrg_cube3d_set_uniform_size:
 * @self: an #LrgCube3D
 * @size: the size for all dimensions
 *
 * Sets all dimensions to the same value (true cube).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cube3d_set_uniform_size (LrgCube3D *self,
                                  gfloat     size);

G_END_DECLS
