/* lrg-pathfinder.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A* pathfinding algorithm implementation.
 */

#ifndef LRG_PATHFINDER_H
#define LRG_PATHFINDER_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-enums.h"
#include "lrg-nav-grid.h"
#include "lrg-path.h"

G_BEGIN_DECLS

#define LRG_TYPE_PATHFINDER (lrg_pathfinder_get_type ())

G_DECLARE_FINAL_TYPE (LrgPathfinder, lrg_pathfinder, LRG, PATHFINDER, GObject)

/**
 * LrgHeuristicFunc:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: User data
 *
 * Custom heuristic function for A*.
 *
 * Returns: Estimated cost from (x1,y1) to (x2,y2)
 */
typedef gfloat (*LrgHeuristicFunc) (gint     x1,
                                    gint     y1,
                                    gint     x2,
                                    gint     y2,
                                    gpointer user_data);

/**
 * lrg_pathfinder_new:
 * @grid: The navigation grid to use
 *
 * Creates a new pathfinder for the given grid.
 *
 * Returns: (transfer full): A new #LrgPathfinder
 */
LRG_AVAILABLE_IN_ALL
LrgPathfinder *     lrg_pathfinder_new               (LrgNavGrid *grid);

/**
 * lrg_pathfinder_get_grid:
 * @self: an #LrgPathfinder
 *
 * Gets the navigation grid.
 *
 * Returns: (transfer none): The navigation grid
 */
LRG_AVAILABLE_IN_ALL
LrgNavGrid *        lrg_pathfinder_get_grid          (LrgPathfinder *self);

/**
 * lrg_pathfinder_set_grid:
 * @self: an #LrgPathfinder
 * @grid: The navigation grid
 *
 * Sets the navigation grid.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_pathfinder_set_grid          (LrgPathfinder *self,
                                                      LrgNavGrid    *grid);

/**
 * lrg_pathfinder_find_path:
 * @self: an #LrgPathfinder
 * @start_x: Start X coordinate
 * @start_y: Start Y coordinate
 * @end_x: End X coordinate
 * @end_y: End Y coordinate
 * @error: (nullable): Return location for error
 *
 * Finds a path from start to end using A*.
 *
 * Returns: (transfer full) (nullable): The path, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgPath *           lrg_pathfinder_find_path         (LrgPathfinder  *self,
                                                      gint            start_x,
                                                      gint            start_y,
                                                      gint            end_x,
                                                      gint            end_y,
                                                      GError        **error);

/**
 * lrg_pathfinder_get_smoothing:
 * @self: an #LrgPathfinder
 *
 * Gets the path smoothing mode.
 *
 * Returns: The smoothing mode
 */
LRG_AVAILABLE_IN_ALL
LrgPathSmoothingMode lrg_pathfinder_get_smoothing    (LrgPathfinder *self);

/**
 * lrg_pathfinder_set_smoothing:
 * @self: an #LrgPathfinder
 * @mode: Smoothing mode
 *
 * Sets the path smoothing mode.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_pathfinder_set_smoothing     (LrgPathfinder        *self,
                                                      LrgPathSmoothingMode  mode);

/**
 * lrg_pathfinder_get_max_iterations:
 * @self: an #LrgPathfinder
 *
 * Gets the maximum number of iterations before giving up.
 *
 * Returns: Maximum iterations (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_pathfinder_get_max_iterations (LrgPathfinder *self);

/**
 * lrg_pathfinder_set_max_iterations:
 * @self: an #LrgPathfinder
 * @max_iterations: Maximum iterations (0 = unlimited)
 *
 * Sets the maximum number of iterations.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_pathfinder_set_max_iterations (LrgPathfinder *self,
                                                       guint          max_iterations);

/**
 * lrg_pathfinder_set_heuristic:
 * @self: an #LrgPathfinder
 * @func: (nullable) (scope notified): Heuristic function
 * @user_data: (closure): User data for function
 * @destroy: (nullable): Destroy function for user data
 *
 * Sets a custom heuristic function. If NULL, uses Manhattan distance.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_pathfinder_set_heuristic     (LrgPathfinder    *self,
                                                      LrgHeuristicFunc  func,
                                                      gpointer          user_data,
                                                      GDestroyNotify    destroy);

/**
 * lrg_pathfinder_get_last_nodes_explored:
 * @self: an #LrgPathfinder
 *
 * Gets the number of nodes explored in the last pathfinding operation.
 *
 * Returns: Number of nodes explored
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_pathfinder_get_last_nodes_explored (LrgPathfinder *self);

/**
 * lrg_pathfinder_is_reachable:
 * @self: an #LrgPathfinder
 * @start_x: Start X coordinate
 * @start_y: Start Y coordinate
 * @end_x: End X coordinate
 * @end_y: End Y coordinate
 *
 * Checks if a path exists between two points.
 *
 * Returns: %TRUE if a path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_pathfinder_is_reachable      (LrgPathfinder *self,
                                                      gint           start_x,
                                                      gint           start_y,
                                                      gint           end_x,
                                                      gint           end_y);

/* Built-in heuristic functions */

/**
 * lrg_heuristic_manhattan:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Manhattan distance heuristic.
 *
 * Returns: Manhattan distance
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_heuristic_manhattan          (gint     x1,
                                                      gint     y1,
                                                      gint     x2,
                                                      gint     y2,
                                                      gpointer user_data);

/**
 * lrg_heuristic_euclidean:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Euclidean distance heuristic.
 *
 * Returns: Euclidean distance
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_heuristic_euclidean          (gint     x1,
                                                      gint     y1,
                                                      gint     x2,
                                                      gint     y2,
                                                      gpointer user_data);

/**
 * lrg_heuristic_chebyshev:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Chebyshev distance heuristic (diagonal movement cost = 1).
 *
 * Returns: Chebyshev distance
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_heuristic_chebyshev          (gint     x1,
                                                      gint     y1,
                                                      gint     x2,
                                                      gint     y2,
                                                      gpointer user_data);

/**
 * lrg_heuristic_octile:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Octile distance heuristic (diagonal movement cost = sqrt(2)).
 *
 * Returns: Octile distance
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_heuristic_octile             (gint     x1,
                                                      gint     y1,
                                                      gint     x2,
                                                      gint     y2,
                                                      gpointer user_data);

G_END_DECLS

#endif /* LRG_PATHFINDER_H */
