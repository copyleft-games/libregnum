/* lrg-grid.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Grid layout container.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_GRID (lrg_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGrid, lrg_grid, LRG, GRID, LrgContainer)

/**
 * lrg_grid_new:
 * @columns: number of columns
 *
 * Creates a new grid container.
 *
 * Returns: (transfer full): A new #LrgGrid
 */
LRG_AVAILABLE_IN_ALL
LrgGrid * lrg_grid_new (guint columns);

/**
 * lrg_grid_get_columns:
 * @self: an #LrgGrid
 *
 * Gets the number of columns.
 *
 * Returns: The column count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_grid_get_columns (LrgGrid *self);

/**
 * lrg_grid_set_columns:
 * @self: an #LrgGrid
 * @columns: number of columns
 *
 * Sets the number of columns.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grid_set_columns (LrgGrid *self,
                           guint    columns);

/**
 * lrg_grid_get_column_spacing:
 * @self: an #LrgGrid
 *
 * Gets the horizontal spacing between columns.
 *
 * Returns: The column spacing in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_grid_get_column_spacing (LrgGrid *self);

/**
 * lrg_grid_set_column_spacing:
 * @self: an #LrgGrid
 * @spacing: the spacing in pixels
 *
 * Sets the horizontal spacing between columns.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grid_set_column_spacing (LrgGrid *self,
                                  gfloat   spacing);

/**
 * lrg_grid_get_row_spacing:
 * @self: an #LrgGrid
 *
 * Gets the vertical spacing between rows.
 *
 * Returns: The row spacing in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_grid_get_row_spacing (LrgGrid *self);

/**
 * lrg_grid_set_row_spacing:
 * @self: an #LrgGrid
 * @spacing: the spacing in pixels
 *
 * Sets the vertical spacing between rows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grid_set_row_spacing (LrgGrid *self,
                               gfloat   spacing);

G_END_DECLS
