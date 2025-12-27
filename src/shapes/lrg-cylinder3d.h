/* lrg-cylinder3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D cylinder shape.
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

#define LRG_TYPE_CYLINDER3D (lrg_cylinder3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCylinder3D, lrg_cylinder3d, LRG, CYLINDER3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_cylinder3d_new:
 *
 * Creates a new cylinder at the origin with radius 1.0 and height 2.0.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LRG_AVAILABLE_IN_ALL
LrgCylinder3D * lrg_cylinder3d_new (void);

/**
 * lrg_cylinder3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: cylinder radius
 * @height: cylinder height (depth)
 *
 * Creates a new cylinder at the specified position with given dimensions.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LRG_AVAILABLE_IN_ALL
LrgCylinder3D * lrg_cylinder3d_new_at (gfloat x,
                                       gfloat y,
                                       gfloat z,
                                       gfloat radius,
                                       gfloat height);

/**
 * lrg_cylinder3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: cylinder radius
 * @height: cylinder height (depth)
 * @slices: number of slices around the cylinder
 * @color: (transfer none): the cylinder color
 *
 * Creates a new cylinder with full configuration.
 *
 * Returns: (transfer full): A new #LrgCylinder3D
 */
LRG_AVAILABLE_IN_ALL
LrgCylinder3D * lrg_cylinder3d_new_full (gfloat    x,
                                         gfloat    y,
                                         gfloat    z,
                                         gfloat    radius,
                                         gfloat    height,
                                         gint      slices,
                                         GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_cylinder3d_get_radius:
 * @self: an #LrgCylinder3D
 *
 * Gets the cylinder's radius.
 *
 * Returns: The radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cylinder3d_get_radius (LrgCylinder3D *self);

/**
 * lrg_cylinder3d_set_radius:
 * @self: an #LrgCylinder3D
 * @radius: the radius value
 *
 * Sets the cylinder's radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cylinder3d_set_radius (LrgCylinder3D *self,
                                gfloat         radius);

/**
 * lrg_cylinder3d_get_height:
 * @self: an #LrgCylinder3D
 *
 * Gets the cylinder's height (depth).
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_cylinder3d_get_height (LrgCylinder3D *self);

/**
 * lrg_cylinder3d_set_height:
 * @self: an #LrgCylinder3D
 * @height: the height value
 *
 * Sets the cylinder's height (depth).
 */
LRG_AVAILABLE_IN_ALL
void lrg_cylinder3d_set_height (LrgCylinder3D *self,
                                gfloat         height);

/**
 * lrg_cylinder3d_get_slices:
 * @self: an #LrgCylinder3D
 *
 * Gets the number of slices around the cylinder.
 *
 * Returns: The number of slices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_cylinder3d_get_slices (LrgCylinder3D *self);

/**
 * lrg_cylinder3d_set_slices:
 * @self: an #LrgCylinder3D
 * @slices: the number of slices
 *
 * Sets the number of slices around the cylinder.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cylinder3d_set_slices (LrgCylinder3D *self,
                                gint           slices);

/**
 * lrg_cylinder3d_get_cap_ends:
 * @self: an #LrgCylinder3D
 *
 * Gets whether the cylinder has capped ends.
 *
 * Returns: %TRUE if ends are capped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cylinder3d_get_cap_ends (LrgCylinder3D *self);

/**
 * lrg_cylinder3d_set_cap_ends:
 * @self: an #LrgCylinder3D
 * @cap_ends: whether to cap the ends
 *
 * Sets whether the cylinder has capped ends.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cylinder3d_set_cap_ends (LrgCylinder3D *self,
                                  gboolean       cap_ends);

G_END_DECLS
