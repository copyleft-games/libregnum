/* lrg-line3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D line segment shape.
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

#define LRG_TYPE_LINE3D (lrg_line3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLine3D, lrg_line3d, LRG, LINE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_line3d_new:
 *
 * Creates a new line from origin to (1, 0, 0).
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LRG_AVAILABLE_IN_ALL
LrgLine3D * lrg_line3d_new (void);

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
LRG_AVAILABLE_IN_ALL
LrgLine3D * lrg_line3d_new_from_to (gfloat start_x,
                                    gfloat start_y,
                                    gfloat start_z,
                                    gfloat end_x,
                                    gfloat end_y,
                                    gfloat end_z);

/**
 * lrg_line3d_new_from_to_v:
 * @start: (transfer none): start position
 * @end: (transfer none): end position
 *
 * Creates a new line from start to end vectors.
 *
 * Returns: (transfer full): A new #LrgLine3D
 */
LRG_AVAILABLE_IN_ALL
LrgLine3D * lrg_line3d_new_from_to_v (GrlVector3 *start,
                                      GrlVector3 *end);

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
LRG_AVAILABLE_IN_ALL
LrgLine3D * lrg_line3d_new_full (gfloat    start_x,
                                 gfloat    start_y,
                                 gfloat    start_z,
                                 gfloat    end_x,
                                 gfloat    end_y,
                                 gfloat    end_z,
                                 GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_line3d_get_end:
 * @self: an #LrgLine3D
 *
 * Gets the line's end position.
 *
 * Returns: (transfer none): The end position vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_line3d_get_end (LrgLine3D *self);

/**
 * lrg_line3d_set_end:
 * @self: an #LrgLine3D
 * @end: (transfer none): the end position to set
 *
 * Sets the line's end position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_line3d_set_end (LrgLine3D  *self,
                         GrlVector3 *end);

/**
 * lrg_line3d_set_end_xyz:
 * @self: an #LrgLine3D
 * @x: the end X coordinate
 * @y: the end Y coordinate
 * @z: the end Z coordinate
 *
 * Sets the line's end position using X, Y, Z coordinates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_line3d_set_end_xyz (LrgLine3D *self,
                             gfloat     x,
                             gfloat     y,
                             gfloat     z);

/**
 * lrg_line3d_get_end_x:
 * @self: an #LrgLine3D
 *
 * Gets the line's end X position.
 *
 * Returns: The end X coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_line3d_get_end_x (LrgLine3D *self);

/**
 * lrg_line3d_get_end_y:
 * @self: an #LrgLine3D
 *
 * Gets the line's end Y position.
 *
 * Returns: The end Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_line3d_get_end_y (LrgLine3D *self);

/**
 * lrg_line3d_get_end_z:
 * @self: an #LrgLine3D
 *
 * Gets the line's end Z position.
 *
 * Returns: The end Z coordinate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_line3d_get_end_z (LrgLine3D *self);

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
LRG_AVAILABLE_IN_ALL
void lrg_line3d_set_points (LrgLine3D *self,
                            gfloat     start_x,
                            gfloat     start_y,
                            gfloat     start_z,
                            gfloat     end_x,
                            gfloat     end_y,
                            gfloat     end_z);

G_END_DECLS
