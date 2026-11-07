/* lrg-arrangement-grid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "grid" arrangement: a wall of panels in rows and columns, sorted by slot
 * (group, ordinal) and centred on the origin.  Each slot group starts on a new
 * row so groups read as bands.  The wall bends gently around the viewer so the
 * outer columns still face the camera.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_GRID (lrg_arrangement_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementGrid, lrg_arrangement_grid, LRG, ARRANGEMENT_GRID, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementGrid * lrg_arrangement_grid_new (void);

/**
 * lrg_arrangement_grid_set_columns:
 * @self: a #LrgArrangementGrid
 * @columns: the number of columns, or 0 to pick one from the panel count
 *   (about square); defaults to 0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_arrangement_grid_set_columns (LrgArrangementGrid *self,
                                       guint               columns);

LRG_AVAILABLE_IN_ALL
guint lrg_arrangement_grid_get_columns (LrgArrangementGrid *self);

/**
 * lrg_arrangement_grid_set_curve:
 * @self: a #LrgArrangementGrid
 * @radius: the radius the wall bends around in world units, or 0 for a flat
 *   wall; defaults to 14
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_arrangement_grid_set_curve (LrgArrangementGrid *self,
                                     gfloat              radius);

LRG_AVAILABLE_IN_ALL
gfloat lrg_arrangement_grid_get_curve (LrgArrangementGrid *self);

G_END_DECLS
