/* lrg-icosphere3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D icosphere shape.
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

#define LRG_TYPE_ICOSPHERE3D (lrg_icosphere3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIcoSphere3D, lrg_icosphere3d, LRG, ICOSPHERE3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_icosphere3d_new:
 *
 * Creates a new icosphere at the origin with radius 1.0 and 2 subdivisions.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgIcoSphere3D * lrg_icosphere3d_new (void);

/**
 * lrg_icosphere3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 *
 * Creates a new icosphere at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgIcoSphere3D * lrg_icosphere3d_new_at (gfloat x,
                                         gfloat y,
                                         gfloat z,
                                         gfloat radius);

/**
 * lrg_icosphere3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @radius: sphere radius
 * @subdivisions: number of subdivisions (detail level)
 * @color: (transfer none): the sphere color
 *
 * Creates a new icosphere with full configuration.
 *
 * Returns: (transfer full): A new #LrgIcoSphere3D
 */
LRG_AVAILABLE_IN_ALL
LrgIcoSphere3D * lrg_icosphere3d_new_full (gfloat    x,
                                           gfloat    y,
                                           gfloat    z,
                                           gfloat    radius,
                                           gint      subdivisions,
                                           GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_icosphere3d_get_radius:
 * @self: an #LrgIcoSphere3D
 *
 * Gets the icosphere's radius.
 *
 * Returns: The radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_icosphere3d_get_radius (LrgIcoSphere3D *self);

/**
 * lrg_icosphere3d_set_radius:
 * @self: an #LrgIcoSphere3D
 * @radius: the radius value
 *
 * Sets the icosphere's radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_icosphere3d_set_radius (LrgIcoSphere3D *self,
                                 gfloat          radius);

/**
 * lrg_icosphere3d_get_subdivisions:
 * @self: an #LrgIcoSphere3D
 *
 * Gets the number of subdivisions (detail level).
 *
 * Returns: The number of subdivisions
 */
LRG_AVAILABLE_IN_ALL
gint lrg_icosphere3d_get_subdivisions (LrgIcoSphere3D *self);

/**
 * lrg_icosphere3d_set_subdivisions:
 * @self: an #LrgIcoSphere3D
 * @subdivisions: the number of subdivisions
 *
 * Sets the number of subdivisions (detail level).
 * Higher values create smoother spheres.
 */
LRG_AVAILABLE_IN_ALL
void lrg_icosphere3d_set_subdivisions (LrgIcoSphere3D *self,
                                       gint            subdivisions);

G_END_DECLS
