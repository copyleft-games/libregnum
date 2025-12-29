/* lrg-build-grid.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBuildGrid - Grid-based building placement management.
 *
 * Manages a 2D grid of cells for building placement, terrain types,
 * and occupancy tracking.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-building-def.h"
#include "lrg-building-instance.h"

G_BEGIN_DECLS

/* ========================================================================== */
/* LrgBuildCell - Boxed type for individual grid cells                        */
/* ========================================================================== */

#define LRG_TYPE_BUILD_CELL (lrg_build_cell_get_type ())

/**
 * LrgBuildCell:
 * @x: Grid X coordinate
 * @y: Grid Y coordinate
 * @terrain: Terrain type flags
 * @building: Building occupying this cell (or %NULL)
 * @blocked: Whether cell is blocked for placement
 *
 * Represents a single cell in the build grid.
 *
 * Since: 1.0
 */
typedef struct _LrgBuildCell LrgBuildCell;

struct _LrgBuildCell
{
    gint                 x;
    gint                 y;
    LrgTerrainType      terrain;
    LrgBuildingInstance *building;
    gboolean             blocked;
};

LRG_AVAILABLE_IN_ALL
GType
lrg_build_cell_get_type (void) G_GNUC_CONST;

/**
 * lrg_build_cell_new:
 * @x: Grid X coordinate
 * @y: Grid Y coordinate
 *
 * Creates a new build cell at the given coordinates.
 *
 * Returns: (transfer full): A new #LrgBuildCell
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildCell *
lrg_build_cell_new (gint x,
                    gint y);

/**
 * lrg_build_cell_copy:
 * @cell: an #LrgBuildCell
 *
 * Creates a copy of the cell.
 *
 * Returns: (transfer full): A copy of @cell
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildCell *
lrg_build_cell_copy (const LrgBuildCell *cell);

/**
 * lrg_build_cell_free:
 * @cell: an #LrgBuildCell
 *
 * Frees the cell.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_cell_free (LrgBuildCell *cell);

/**
 * lrg_build_cell_is_free:
 * @cell: an #LrgBuildCell
 *
 * Checks if the cell is available for building placement.
 *
 * Returns: %TRUE if cell has no building and is not blocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_cell_is_free (const LrgBuildCell *cell);

/* ========================================================================== */
/* LrgBuildGrid - Grid management                                             */
/* ========================================================================== */

#define LRG_TYPE_BUILD_GRID (lrg_build_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBuildGrid, lrg_build_grid, LRG, BUILD_GRID, GObject)

/* Signals */

/**
 * LrgBuildGrid::building-placed:
 * @grid: the #LrgBuildGrid
 * @building: the #LrgBuildingInstance that was placed
 *
 * Emitted when a building is placed on the grid.
 *
 * Since: 1.0
 */

/**
 * LrgBuildGrid::building-removed:
 * @grid: the #LrgBuildGrid
 * @building: the #LrgBuildingInstance that was removed
 *
 * Emitted when a building is removed from the grid.
 *
 * Since: 1.0
 */

/**
 * LrgBuildGrid::cell-changed:
 * @grid: the #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Emitted when a cell's state changes.
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_build_grid_new:
 * @width: Grid width in cells
 * @height: Grid height in cells
 * @cell_size: Size of each cell in world units
 *
 * Creates a new build grid.
 *
 * Returns: (transfer full): A new #LrgBuildGrid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildGrid *
lrg_build_grid_new (gint    width,
                    gint    height,
                    gdouble cell_size);

/* Dimensions */

/**
 * lrg_build_grid_get_width:
 * @self: an #LrgBuildGrid
 *
 * Gets the grid width in cells.
 *
 * Returns: Grid width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_build_grid_get_width (LrgBuildGrid *self);

/**
 * lrg_build_grid_get_height:
 * @self: an #LrgBuildGrid
 *
 * Gets the grid height in cells.
 *
 * Returns: Grid height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_build_grid_get_height (LrgBuildGrid *self);

/**
 * lrg_build_grid_get_cell_size:
 * @self: an #LrgBuildGrid
 *
 * Gets the size of each cell in world units.
 *
 * Returns: Cell size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_build_grid_get_cell_size (LrgBuildGrid *self);

/* Cell access */

/**
 * lrg_build_grid_get_cell:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Gets the cell at the given coordinates.
 *
 * Returns: (transfer none) (nullable): The cell, or %NULL if out of bounds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildCell *
lrg_build_grid_get_cell (LrgBuildGrid *self,
                         gint          x,
                         gint          y);

/**
 * lrg_build_grid_is_valid_cell:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Checks if coordinates are within grid bounds.
 *
 * Returns: %TRUE if valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_is_valid_cell (LrgBuildGrid *self,
                              gint          x,
                              gint          y);

/* Terrain */

/**
 * lrg_build_grid_set_terrain:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 * @terrain: Terrain type flags
 *
 * Sets the terrain type for a cell.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_set_terrain (LrgBuildGrid    *self,
                            gint             x,
                            gint             y,
                            LrgTerrainType  terrain);

/**
 * lrg_build_grid_get_terrain:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Gets the terrain type for a cell.
 *
 * Returns: Terrain type flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainType
lrg_build_grid_get_terrain (LrgBuildGrid *self,
                            gint          x,
                            gint          y);

/**
 * lrg_build_grid_fill_terrain:
 * @self: an #LrgBuildGrid
 * @terrain: Terrain type flags
 *
 * Sets all cells to the given terrain type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_fill_terrain (LrgBuildGrid    *self,
                             LrgTerrainType  terrain);

/**
 * lrg_build_grid_set_terrain_rect:
 * @self: an #LrgBuildGrid
 * @x: Start X coordinate
 * @y: Start Y coordinate
 * @width: Rectangle width
 * @height: Rectangle height
 * @terrain: Terrain type flags
 *
 * Sets terrain for a rectangular area.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_set_terrain_rect (LrgBuildGrid    *self,
                                 gint             x,
                                 gint             y,
                                 gint             width,
                                 gint             height,
                                 LrgTerrainType  terrain);

/* Blocking */

/**
 * lrg_build_grid_set_blocked:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 * @blocked: Whether to block the cell
 *
 * Sets the blocked state for a cell.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_set_blocked (LrgBuildGrid *self,
                            gint          x,
                            gint          y,
                            gboolean      blocked);

/**
 * lrg_build_grid_is_blocked:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Checks if a cell is blocked.
 *
 * Returns: %TRUE if blocked
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_is_blocked (LrgBuildGrid *self,
                           gint          x,
                           gint          y);

/* Area queries */

/**
 * lrg_build_grid_is_area_free:
 * @self: an #LrgBuildGrid
 * @x: Start X coordinate
 * @y: Start Y coordinate
 * @width: Area width
 * @height: Area height
 *
 * Checks if a rectangular area is free (no buildings, not blocked).
 *
 * Returns: %TRUE if all cells in area are free
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_is_area_free (LrgBuildGrid *self,
                             gint          x,
                             gint          y,
                             gint          width,
                             gint          height);

/**
 * lrg_build_grid_can_place:
 * @self: an #LrgBuildGrid
 * @definition: Building definition to check
 * @x: Grid X position
 * @y: Grid Y position
 * @rotation: Building rotation
 *
 * Checks if a building can be placed at the given location.
 * Validates terrain requirements and area availability.
 *
 * Returns: %TRUE if building can be placed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_can_place (LrgBuildGrid   *self,
                          LrgBuildingDef *definition,
                          gint            x,
                          gint            y,
                          LrgRotation     rotation);

/* Coordinate conversion */

/**
 * lrg_build_grid_world_to_cell:
 * @self: an #LrgBuildGrid
 * @world_x: World X coordinate
 * @world_y: World Y coordinate
 * @cell_x: (out): Cell X coordinate
 * @cell_y: (out): Cell Y coordinate
 *
 * Converts world coordinates to cell coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_world_to_cell (LrgBuildGrid *self,
                              gdouble       world_x,
                              gdouble       world_y,
                              gint         *cell_x,
                              gint         *cell_y);

/**
 * lrg_build_grid_cell_to_world:
 * @self: an #LrgBuildGrid
 * @cell_x: Cell X coordinate
 * @cell_y: Cell Y coordinate
 * @world_x: (out): World X coordinate (center of cell)
 * @world_y: (out): World Y coordinate (center of cell)
 *
 * Converts cell coordinates to world coordinates (cell center).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_cell_to_world (LrgBuildGrid *self,
                              gint          cell_x,
                              gint          cell_y,
                              gdouble      *world_x,
                              gdouble      *world_y);

/**
 * lrg_build_grid_snap_to_grid:
 * @self: an #LrgBuildGrid
 * @world_x: World X coordinate
 * @world_y: World Y coordinate
 * @snapped_x: (out): Snapped world X coordinate
 * @snapped_y: (out): Snapped world Y coordinate
 *
 * Snaps world coordinates to the nearest cell center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_snap_to_grid (LrgBuildGrid *self,
                             gdouble       world_x,
                             gdouble       world_y,
                             gdouble      *snapped_x,
                             gdouble      *snapped_y);

/* Building placement */

/**
 * lrg_build_grid_place_building:
 * @self: an #LrgBuildGrid
 * @building: Building to place
 *
 * Places a building on the grid at its current position.
 * The building's grid_x and grid_y are used.
 *
 * Returns: %TRUE if successfully placed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_place_building (LrgBuildGrid        *self,
                               LrgBuildingInstance *building);

/**
 * lrg_build_grid_remove_building:
 * @self: an #LrgBuildGrid
 * @building: Building to remove
 *
 * Removes a building from the grid.
 *
 * Returns: %TRUE if successfully removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_grid_remove_building (LrgBuildGrid        *self,
                                LrgBuildingInstance *building);

/**
 * lrg_build_grid_get_building_at:
 * @self: an #LrgBuildGrid
 * @x: Cell X coordinate
 * @y: Cell Y coordinate
 *
 * Gets the building occupying the given cell.
 *
 * Returns: (transfer none) (nullable): The building, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_build_grid_get_building_at (LrgBuildGrid *self,
                                gint          x,
                                gint          y);

/**
 * lrg_build_grid_get_all_buildings:
 * @self: an #LrgBuildGrid
 *
 * Gets all buildings on the grid.
 *
 * Returns: (transfer container) (element-type LrgBuildingInstance):
 *          List of buildings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_build_grid_get_all_buildings (LrgBuildGrid *self);

/**
 * lrg_build_grid_get_buildings_in_area:
 * @self: an #LrgBuildGrid
 * @x: Start X coordinate
 * @y: Start Y coordinate
 * @width: Area width
 * @height: Area height
 *
 * Gets all buildings within a rectangular area.
 *
 * Returns: (transfer container) (element-type LrgBuildingInstance):
 *          List of buildings in area
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_build_grid_get_buildings_in_area (LrgBuildGrid *self,
                                      gint          x,
                                      gint          y,
                                      gint          width,
                                      gint          height);

/**
 * lrg_build_grid_clear:
 * @self: an #LrgBuildGrid
 *
 * Removes all buildings from the grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_grid_clear (LrgBuildGrid *self);

G_END_DECLS
