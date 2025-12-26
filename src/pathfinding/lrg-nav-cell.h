/* lrg-nav-cell.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation cell for grid-based pathfinding.
 */

#ifndef LRG_NAV_CELL_H
#define LRG_NAV_CELL_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_CELL (lrg_nav_cell_get_type ())

/**
 * LrgNavCell:
 *
 * A navigation cell representing a single tile in the pathfinding grid.
 * This is a boxed type containing position, cost, and flag information.
 */

LRG_AVAILABLE_IN_ALL
GType         lrg_nav_cell_get_type     (void) G_GNUC_CONST;

/**
 * lrg_nav_cell_new:
 * @x: X coordinate in the grid
 * @y: Y coordinate in the grid
 * @cost: Movement cost (1.0 = normal, higher = slower)
 * @flags: Navigation flags for this cell
 *
 * Creates a new navigation cell.
 *
 * Returns: (transfer full): A new #LrgNavCell
 */
LRG_AVAILABLE_IN_ALL
LrgNavCell *  lrg_nav_cell_new          (gint            x,
                                         gint            y,
                                         gfloat          cost,
                                         LrgNavCellFlags flags);

/**
 * lrg_nav_cell_copy:
 * @self: an #LrgNavCell
 *
 * Creates a deep copy of a navigation cell.
 *
 * Returns: (transfer full): A copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgNavCell *  lrg_nav_cell_copy         (const LrgNavCell *self);

/**
 * lrg_nav_cell_free:
 * @self: an #LrgNavCell
 *
 * Frees a navigation cell.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_nav_cell_free         (LrgNavCell *self);

/**
 * lrg_nav_cell_get_x:
 * @self: an #LrgNavCell
 *
 * Gets the X coordinate.
 *
 * Returns: The X coordinate
 */
LRG_AVAILABLE_IN_ALL
gint          lrg_nav_cell_get_x        (const LrgNavCell *self);

/**
 * lrg_nav_cell_get_y:
 * @self: an #LrgNavCell
 *
 * Gets the Y coordinate.
 *
 * Returns: The Y coordinate
 */
LRG_AVAILABLE_IN_ALL
gint          lrg_nav_cell_get_y        (const LrgNavCell *self);

/**
 * lrg_nav_cell_get_cost:
 * @self: an #LrgNavCell
 *
 * Gets the movement cost.
 *
 * Returns: The movement cost multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat        lrg_nav_cell_get_cost     (const LrgNavCell *self);

/**
 * lrg_nav_cell_set_cost:
 * @self: an #LrgNavCell
 * @cost: New movement cost
 *
 * Sets the movement cost.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_nav_cell_set_cost     (LrgNavCell *self,
                                         gfloat      cost);

/**
 * lrg_nav_cell_get_flags:
 * @self: an #LrgNavCell
 *
 * Gets the navigation flags.
 *
 * Returns: The navigation flags
 */
LRG_AVAILABLE_IN_ALL
LrgNavCellFlags lrg_nav_cell_get_flags  (const LrgNavCell *self);

/**
 * lrg_nav_cell_set_flags:
 * @self: an #LrgNavCell
 * @flags: New navigation flags
 *
 * Sets the navigation flags.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_nav_cell_set_flags    (LrgNavCell      *self,
                                         LrgNavCellFlags  flags);

/**
 * lrg_nav_cell_has_flag:
 * @self: an #LrgNavCell
 * @flag: Flag to check
 *
 * Checks if the cell has a specific flag.
 *
 * Returns: %TRUE if flag is set
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_nav_cell_has_flag     (const LrgNavCell *self,
                                         LrgNavCellFlags   flag);

/**
 * lrg_nav_cell_is_walkable:
 * @self: an #LrgNavCell
 *
 * Checks if the cell is walkable (not blocked).
 *
 * Returns: %TRUE if walkable
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_nav_cell_is_walkable  (const LrgNavCell *self);

/**
 * lrg_nav_cell_get_user_data:
 * @self: an #LrgNavCell
 *
 * Gets user-defined data attached to this cell.
 *
 * Returns: (transfer none) (nullable): User data or %NULL
 */
LRG_AVAILABLE_IN_ALL
gpointer      lrg_nav_cell_get_user_data (const LrgNavCell *self);

/**
 * lrg_nav_cell_set_user_data:
 * @self: an #LrgNavCell
 * @user_data: (nullable): User data to attach
 * @destroy: (nullable): Destroy function for user data
 *
 * Sets user-defined data on this cell.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_nav_cell_set_user_data (LrgNavCell     *self,
                                          gpointer        user_data,
                                          GDestroyNotify  destroy);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgNavCell, lrg_nav_cell_free)

G_END_DECLS

#endif /* LRG_NAV_CELL_H */
