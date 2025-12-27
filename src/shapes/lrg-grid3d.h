/* lrg-grid3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D grid shape.
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

#define LRG_TYPE_GRID3D (lrg_grid3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGrid3D, lrg_grid3d, LRG, GRID3D, LrgShape3D)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_grid3d_new:
 *
 * Creates a new grid with 10 slices and 1.0 spacing.
 *
 * Returns: (transfer full): A new #LrgGrid3D
 */
LRG_AVAILABLE_IN_ALL
LrgGrid3D * lrg_grid3d_new (void);

/**
 * lrg_grid3d_new_sized:
 * @slices: number of grid divisions
 * @spacing: spacing between grid lines
 *
 * Creates a new grid with specified dimensions.
 *
 * Returns: (transfer full): A new #LrgGrid3D
 */
LRG_AVAILABLE_IN_ALL
LrgGrid3D * lrg_grid3d_new_sized (gint   slices,
                                  gfloat spacing);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_grid3d_get_slices:
 * @self: an #LrgGrid3D
 *
 * Gets the number of grid divisions.
 *
 * Returns: The number of slices
 */
LRG_AVAILABLE_IN_ALL
gint lrg_grid3d_get_slices (LrgGrid3D *self);

/**
 * lrg_grid3d_set_slices:
 * @self: an #LrgGrid3D
 * @slices: the number of slices
 *
 * Sets the number of grid divisions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grid3d_set_slices (LrgGrid3D *self,
                            gint       slices);

/**
 * lrg_grid3d_get_spacing:
 * @self: an #LrgGrid3D
 *
 * Gets the spacing between grid lines.
 *
 * Returns: The spacing
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_grid3d_get_spacing (LrgGrid3D *self);

/**
 * lrg_grid3d_set_spacing:
 * @self: an #LrgGrid3D
 * @spacing: the spacing value
 *
 * Sets the spacing between grid lines.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grid3d_set_spacing (LrgGrid3D *self,
                             gfloat     spacing);

G_END_DECLS
