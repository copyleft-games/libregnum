/* lrg-sphere3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D sphere shape.
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

#define LRG_TYPE_SPHERE3D (lrg_sphere3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSphere3D, lrg_sphere3d, LRG, SPHERE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_sphere3d_new:
 *
 * Creates a new sphere at the origin with radius 1.0.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgSphere3D * lrg_sphere3d_new (void);

/**
 * lrg_sphere3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 *
 * Creates a new sphere at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgSphere3D * lrg_sphere3d_new_at (gfloat x,
                                   gfloat y,
                                   gfloat z,
                                   gfloat radius);

/**
 * lrg_sphere3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 * @color: (transfer none): the sphere color
 *
 * Creates a new sphere with full configuration.
 *
 * Returns: (transfer full): A new #LrgSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgSphere3D * lrg_sphere3d_new_full (gfloat    x,
                                     gfloat    y,
                                     gfloat    z,
                                     gfloat    radius,
                                     GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_sphere3d_get_radius:
 * @self: an #LrgSphere3D
 *
 * Gets the sphere's radius.
 *
 * Returns: The radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sphere3d_get_radius (LrgSphere3D *self);

/**
 * lrg_sphere3d_set_radius:
 * @self: an #LrgSphere3D
 * @radius: the radius value
 *
 * Sets the sphere's radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sphere3d_set_radius (LrgSphere3D *self,
                              gfloat       radius);

/**
 * lrg_sphere3d_get_rings:
 * @self: an #LrgSphere3D
 *
 * Gets the number of horizontal rings for tessellation.
 *
 * Returns: The number of rings
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sphere3d_get_rings (LrgSphere3D *self);

/**
 * lrg_sphere3d_set_rings:
 * @self: an #LrgSphere3D
 * @rings: the number of rings
 *
 * Sets the number of horizontal rings for tessellation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sphere3d_set_rings (LrgSphere3D *self,
                             gint         rings);

/**
 * lrg_sphere3d_get_slices:
 * @self: an #LrgSphere3D
 *
 * Gets the number of vertical slices for tessellation.
 *
 * Returns: The number of slices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_sphere3d_get_slices (LrgSphere3D *self);

/**
 * lrg_sphere3d_set_slices:
 * @self: an #LrgSphere3D
 * @slices: the number of slices
 *
 * Sets the number of vertical slices for tessellation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sphere3d_set_slices (LrgSphere3D *self,
                              gint         slices);

G_END_DECLS
