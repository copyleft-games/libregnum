/* lrg-cone3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cone shape.
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

#define LRG_TYPE_CONE3D (lrg_cone3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCone3D, lrg_cone3d, LRG, CONE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_cone3d_new:
 *
 * Creates a new cone at the origin with base radius 1.0, top radius 0.0,
 * and height 2.0.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LRG_AVAILABLE_IN_ALL
LrgCone3D * lrg_cone3d_new (void);

/**
 * lrg_cone3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius_bottom: base radius
 * @height: cone height
 *
 * Creates a new cone at the specified position with given dimensions.
 * The top radius is set to 0 for a pointed cone.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LRG_AVAILABLE_IN_ALL
LrgCone3D * lrg_cone3d_new_at (gfloat x,
                               gfloat y,
                               gfloat z,
                               gfloat radius_bottom,
                               gfloat height);

/**
 * lrg_cone3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius_bottom: base radius
 * @radius_top: top radius (0 for pointed cone)
 * @height: cone height
 * @slices: number of slices around the cone
 * @color: (transfer none): the cone color
 *
 * Creates a new cone with full configuration.
 *
 * Returns: (transfer full): A new #LrgCone3D
 */
LRG_AVAILABLE_IN_ALL
LrgCone3D * lrg_cone3d_new_full (gfloat    x,
                                 gfloat    y,
                                 gfloat    z,
                                 gfloat    radius_bottom,
                                 gfloat    radius_top,
                                 gfloat    height,
                                 gint      slices,
                                 GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_cone3d_get_radius_bottom:
 * @self: an #LrgCone3D
 *
 * Gets the cone's base radius.
 *
 * Returns: The base radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cone3d_get_radius_bottom (LrgCone3D *self);

/**
 * lrg_cone3d_set_radius_bottom:
 * @self: an #LrgCone3D
 * @radius: the base radius
 *
 * Sets the cone's base radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cone3d_set_radius_bottom (LrgCone3D *self,
                                   gfloat     radius);

/**
 * lrg_cone3d_get_radius_top:
 * @self: an #LrgCone3D
 *
 * Gets the cone's top radius.
 *
 * Returns: The top radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cone3d_get_radius_top (LrgCone3D *self);

/**
 * lrg_cone3d_set_radius_top:
 * @self: an #LrgCone3D
 * @radius: the top radius (0 for pointed cone)
 *
 * Sets the cone's top radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cone3d_set_radius_top (LrgCone3D *self,
                                gfloat     radius);

/**
 * lrg_cone3d_get_height:
 * @self: an #LrgCone3D
 *
 * Gets the cone's height.
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cone3d_get_height (LrgCone3D *self);

/**
 * lrg_cone3d_set_height:
 * @self: an #LrgCone3D
 * @height: the height value
 *
 * Sets the cone's height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cone3d_set_height (LrgCone3D *self,
                            gfloat     height);

/**
 * lrg_cone3d_get_slices:
 * @self: an #LrgCone3D
 *
 * Gets the number of slices around the cone.
 *
 * Returns: The number of slices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_cone3d_get_slices (LrgCone3D *self);

/**
 * lrg_cone3d_set_slices:
 * @self: an #LrgCone3D
 * @slices: the number of slices
 *
 * Sets the number of slices around the cone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cone3d_set_slices (LrgCone3D *self,
                            gint       slices);

G_END_DECLS
