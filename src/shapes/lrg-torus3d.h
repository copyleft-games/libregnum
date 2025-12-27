/* lrg-torus3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D torus shape.
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

#define LRG_TYPE_TORUS3D (lrg_torus3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTorus3D, lrg_torus3d, LRG, TORUS3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_torus3d_new:
 *
 * Creates a new torus at the origin with default dimensions.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LRG_AVAILABLE_IN_ALL
LrgTorus3D * lrg_torus3d_new (void);

/**
 * lrg_torus3d_new_at:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @major_radius: distance from center to tube center
 * @minor_radius: tube radius
 *
 * Creates a new torus at the specified position with given radii.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LRG_AVAILABLE_IN_ALL
LrgTorus3D * lrg_torus3d_new_at (gfloat x,
                                 gfloat y,
                                 gfloat z,
                                 gfloat major_radius,
                                 gfloat minor_radius);

/**
 * lrg_torus3d_new_full:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @major_radius: distance from center to tube center
 * @minor_radius: tube radius
 * @major_segments: segments around the torus
 * @minor_segments: segments around the tube
 * @color: (transfer none): the torus color
 *
 * Creates a new torus with full configuration.
 *
 * Returns: (transfer full): A new #LrgTorus3D
 */
LRG_AVAILABLE_IN_ALL
LrgTorus3D * lrg_torus3d_new_full (gfloat    x,
                                   gfloat    y,
                                   gfloat    z,
                                   gfloat    major_radius,
                                   gfloat    minor_radius,
                                   gint      major_segments,
                                   gint      minor_segments,
                                   GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_torus3d_get_major_radius:
 * @self: an #LrgTorus3D
 *
 * Gets the major radius (distance from center to tube center).
 *
 * Returns: The major radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_torus3d_get_major_radius (LrgTorus3D *self);

/**
 * lrg_torus3d_set_major_radius:
 * @self: an #LrgTorus3D
 * @radius: the major radius
 *
 * Sets the major radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_torus3d_set_major_radius (LrgTorus3D *self,
                                   gfloat      radius);

/**
 * lrg_torus3d_get_minor_radius:
 * @self: an #LrgTorus3D
 *
 * Gets the minor radius (tube radius).
 *
 * Returns: The minor radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_torus3d_get_minor_radius (LrgTorus3D *self);

/**
 * lrg_torus3d_set_minor_radius:
 * @self: an #LrgTorus3D
 * @radius: the minor radius
 *
 * Sets the minor radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_torus3d_set_minor_radius (LrgTorus3D *self,
                                   gfloat      radius);

/**
 * lrg_torus3d_get_major_segments:
 * @self: an #LrgTorus3D
 *
 * Gets the number of major segments.
 *
 * Returns: The number of major segments
 */
LRG_AVAILABLE_IN_ALL
gint lrg_torus3d_get_major_segments (LrgTorus3D *self);

/**
 * lrg_torus3d_set_major_segments:
 * @self: an #LrgTorus3D
 * @segments: the number of major segments
 *
 * Sets the number of major segments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_torus3d_set_major_segments (LrgTorus3D *self,
                                     gint        segments);

/**
 * lrg_torus3d_get_minor_segments:
 * @self: an #LrgTorus3D
 *
 * Gets the number of minor segments.
 *
 * Returns: The number of minor segments
 */
LRG_AVAILABLE_IN_ALL
gint lrg_torus3d_get_minor_segments (LrgTorus3D *self);

/**
 * lrg_torus3d_set_minor_segments:
 * @self: an #LrgTorus3D
 * @segments: the number of minor segments
 *
 * Sets the number of minor segments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_torus3d_set_minor_segments (LrgTorus3D *self,
                                     gint        segments);

G_END_DECLS
