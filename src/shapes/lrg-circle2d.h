/* lrg-circle2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D circle shape.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shape2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CIRCLE2D (lrg_circle2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCircle2D, lrg_circle2d, LRG, CIRCLE2D, LrgShape2D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_circle2d_new:
 *
 * Creates a new circle at the origin with default radius (1.0).
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LRG_AVAILABLE_IN_ALL
LrgCircle2D * lrg_circle2d_new (void);

/**
 * lrg_circle2d_new_at:
 * @x: X position (center)
 * @y: Y position (center)
 * @radius: circle radius
 *
 * Creates a new circle at the specified position with given radius.
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LRG_AVAILABLE_IN_ALL
LrgCircle2D * lrg_circle2d_new_at (gfloat x,
                                    gfloat y,
                                    gfloat radius);

/**
 * lrg_circle2d_new_full:
 * @x: X position (center)
 * @y: Y position (center)
 * @radius: circle radius
 * @color: (transfer none): the circle color
 *
 * Creates a new circle with full configuration.
 *
 * Returns: (transfer full): A new #LrgCircle2D
 */
LRG_AVAILABLE_IN_ALL
LrgCircle2D * lrg_circle2d_new_full (gfloat    x,
                                      gfloat    y,
                                      gfloat    radius,
                                      GrlColor *color);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_circle2d_get_radius:
 * @self: an #LrgCircle2D
 *
 * Gets the circle radius.
 *
 * Returns: The radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_circle2d_get_radius (LrgCircle2D *self);

/**
 * lrg_circle2d_set_radius:
 * @self: an #LrgCircle2D
 * @radius: the radius
 *
 * Sets the circle radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle2d_set_radius (LrgCircle2D *self,
                               gfloat       radius);

/**
 * lrg_circle2d_get_filled:
 * @self: an #LrgCircle2D
 *
 * Gets whether the circle is filled.
 *
 * Returns: %TRUE if filled, %FALSE for outline only
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_circle2d_get_filled (LrgCircle2D *self);

/**
 * lrg_circle2d_set_filled:
 * @self: an #LrgCircle2D
 * @filled: %TRUE for filled, %FALSE for outline only
 *
 * Sets whether the circle is filled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_circle2d_set_filled (LrgCircle2D *self,
                               gboolean     filled);

G_END_DECLS
