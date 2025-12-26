/* lrg-nav-grid.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation grid for pathfinding.
 */

#ifndef LRG_NAV_GRID_H
#define LRG_NAV_GRID_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-nav-cell.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_GRID (lrg_nav_grid_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgNavGrid, lrg_nav_grid, LRG, NAV_GRID, GObject)

/**
 * LrgNavGridClass:
 * @get_cell_cost: Virtual method to get cost of a cell
 * @is_cell_walkable: Virtual method to check if cell is walkable
 * @get_neighbors: Virtual method to get valid neighbors
 *
 * The class structure for #LrgNavGrid.
 */
struct _LrgNavGridClass
{
    GObjectClass parent_class;

    /* Virtual methods for customization */
    gfloat   (*get_cell_cost)    (LrgNavGrid *self,
                                  gint        x,
                                  gint        y);

    gboolean (*is_cell_walkable) (LrgNavGrid *self,
                                  gint        x,
                                  gint        y);

    GList *  (*get_neighbors)    (LrgNavGrid *self,
                                  gint        x,
                                  gint        y);

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_nav_grid_new:
 * @width: Grid width in cells
 * @height: Grid height in cells
 *
 * Creates a new navigation grid with all cells walkable.
 *
 * Returns: (transfer full): A new #LrgNavGrid
 */
LRG_AVAILABLE_IN_ALL
LrgNavGrid *    lrg_nav_grid_new              (guint width,
                                               guint height);

/**
 * lrg_nav_grid_get_width:
 * @self: an #LrgNavGrid
 *
 * Gets the grid width.
 *
 * Returns: Grid width in cells
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_nav_grid_get_width        (LrgNavGrid *self);

/**
 * lrg_nav_grid_get_height:
 * @self: an #LrgNavGrid
 *
 * Gets the grid height.
 *
 * Returns: Grid height in cells
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_nav_grid_get_height       (LrgNavGrid *self);

/**
 * lrg_nav_grid_is_valid:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Checks if coordinates are within grid bounds.
 *
 * Returns: %TRUE if coordinates are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_nav_grid_is_valid         (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_get_cell:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the navigation cell at the specified position.
 *
 * Returns: (transfer none) (nullable): The cell or %NULL if invalid
 */
LRG_AVAILABLE_IN_ALL
LrgNavCell *    lrg_nav_grid_get_cell         (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_set_cell_cost:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @cost: Movement cost
 *
 * Sets the movement cost for a cell.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_set_cell_cost    (LrgNavGrid *self,
                                               gint        x,
                                               gint        y,
                                               gfloat      cost);

/**
 * lrg_nav_grid_get_cell_cost:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the movement cost for a cell.
 *
 * Returns: Movement cost, or G_MAXFLOAT if invalid
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_nav_grid_get_cell_cost    (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_set_cell_flags:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @flags: Navigation flags
 *
 * Sets the navigation flags for a cell.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_set_cell_flags   (LrgNavGrid      *self,
                                               gint             x,
                                               gint             y,
                                               LrgNavCellFlags  flags);

/**
 * lrg_nav_grid_get_cell_flags:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets the navigation flags for a cell.
 *
 * Returns: Navigation flags
 */
LRG_AVAILABLE_IN_ALL
LrgNavCellFlags lrg_nav_grid_get_cell_flags   (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_set_blocked:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 * @blocked: Whether the cell is blocked
 *
 * Sets whether a cell is blocked (not walkable).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_set_blocked      (LrgNavGrid *self,
                                               gint        x,
                                               gint        y,
                                               gboolean    blocked);

/**
 * lrg_nav_grid_is_walkable:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Checks if a cell is walkable.
 *
 * Returns: %TRUE if walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_nav_grid_is_walkable      (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_get_allow_diagonal:
 * @self: an #LrgNavGrid
 *
 * Gets whether diagonal movement is allowed.
 *
 * Returns: %TRUE if diagonal movement is allowed
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_nav_grid_get_allow_diagonal (LrgNavGrid *self);

/**
 * lrg_nav_grid_set_allow_diagonal:
 * @self: an #LrgNavGrid
 * @allow: Whether to allow diagonal movement
 *
 * Sets whether diagonal movement is allowed.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_set_allow_diagonal (LrgNavGrid *self,
                                                 gboolean    allow);

/**
 * lrg_nav_grid_get_cut_corners:
 * @self: an #LrgNavGrid
 *
 * Gets whether corner cutting is allowed for diagonal movement.
 *
 * Returns: %TRUE if corner cutting is allowed
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_nav_grid_get_cut_corners  (LrgNavGrid *self);

/**
 * lrg_nav_grid_set_cut_corners:
 * @self: an #LrgNavGrid
 * @allow: Whether to allow corner cutting
 *
 * Sets whether corner cutting is allowed.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_set_cut_corners  (LrgNavGrid *self,
                                               gboolean    allow);

/**
 * lrg_nav_grid_get_neighbors:
 * @self: an #LrgNavGrid
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Gets all walkable neighbors of a cell.
 *
 * Returns: (transfer full) (element-type LrgNavCell): List of neighbor cells
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_nav_grid_get_neighbors    (LrgNavGrid *self,
                                               gint        x,
                                               gint        y);

/**
 * lrg_nav_grid_clear:
 * @self: an #LrgNavGrid
 *
 * Resets all cells to default (walkable, cost 1.0).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_clear            (LrgNavGrid *self);

/**
 * lrg_nav_grid_fill_rect:
 * @self: an #LrgNavGrid
 * @x: Starting X coordinate
 * @y: Starting Y coordinate
 * @width: Rectangle width
 * @height: Rectangle height
 * @flags: Flags to set
 * @cost: Cost to set
 *
 * Fills a rectangular area with specified flags and cost.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_nav_grid_fill_rect        (LrgNavGrid      *self,
                                               gint             x,
                                               gint             y,
                                               guint            width,
                                               guint            height,
                                               LrgNavCellFlags  flags,
                                               gfloat           cost);

G_END_DECLS

#endif /* LRG_NAV_GRID_H */
