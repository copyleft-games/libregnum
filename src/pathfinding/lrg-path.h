/* lrg-path.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Path result from pathfinding operations.
 */

#ifndef LRG_PATH_H
#define LRG_PATH_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_PATH (lrg_path_get_type ())

/**
 * LrgPathPoint:
 * @x: X coordinate
 * @y: Y coordinate
 *
 * A single point/waypoint in a path.
 */
typedef struct
{
    gint x;
    gint y;
} LrgPathPoint;

/**
 * LrgPath:
 *
 * A path result containing a sequence of waypoints from start to goal.
 * This is a boxed type.
 */

LRG_AVAILABLE_IN_ALL
GType         lrg_path_get_type       (void) G_GNUC_CONST;

/**
 * lrg_path_new:
 *
 * Creates a new empty path.
 *
 * Returns: (transfer full): A new #LrgPath
 */
LRG_AVAILABLE_IN_ALL
LrgPath *     lrg_path_new            (void);

/**
 * lrg_path_copy:
 * @self: an #LrgPath
 *
 * Creates a deep copy of a path.
 *
 * Returns: (transfer full): A copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPath *     lrg_path_copy           (const LrgPath *self);

/**
 * lrg_path_free:
 * @self: an #LrgPath
 *
 * Frees a path.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_free           (LrgPath *self);

/**
 * lrg_path_append:
 * @self: an #LrgPath
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Appends a point to the path.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_append         (LrgPath *self,
                                       gint     x,
                                       gint     y);

/**
 * lrg_path_prepend:
 * @self: an #LrgPath
 * @x: X coordinate
 * @y: Y coordinate
 *
 * Prepends a point to the path.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_prepend        (LrgPath *self,
                                       gint     x,
                                       gint     y);

/**
 * lrg_path_get_length:
 * @self: an #LrgPath
 *
 * Gets the number of points in the path.
 *
 * Returns: Number of points
 */
LRG_AVAILABLE_IN_ALL
guint         lrg_path_get_length     (const LrgPath *self);

/**
 * lrg_path_is_empty:
 * @self: an #LrgPath
 *
 * Checks if the path is empty.
 *
 * Returns: %TRUE if empty
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_path_is_empty       (const LrgPath *self);

/**
 * lrg_path_get_point:
 * @self: an #LrgPath
 * @index: Point index
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets a point at the specified index.
 *
 * Returns: %TRUE if index is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_path_get_point      (const LrgPath *self,
                                       guint          index,
                                       gint          *x,
                                       gint          *y);

/**
 * lrg_path_get_start:
 * @self: an #LrgPath
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets the starting point of the path.
 *
 * Returns: %TRUE if path is not empty
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_path_get_start      (const LrgPath *self,
                                       gint          *x,
                                       gint          *y);

/**
 * lrg_path_get_end:
 * @self: an #LrgPath
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 *
 * Gets the ending point of the path.
 *
 * Returns: %TRUE if path is not empty
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_path_get_end        (const LrgPath *self,
                                       gint          *x,
                                       gint          *y);

/**
 * lrg_path_reverse:
 * @self: an #LrgPath
 *
 * Reverses the path in place.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_reverse        (LrgPath *self);

/**
 * lrg_path_clear:
 * @self: an #LrgPath
 *
 * Removes all points from the path.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_clear          (LrgPath *self);

/**
 * lrg_path_get_total_cost:
 * @self: an #LrgPath
 *
 * Gets the total path cost.
 *
 * Returns: Total cost
 */
LRG_AVAILABLE_IN_ALL
gfloat        lrg_path_get_total_cost (const LrgPath *self);

/**
 * lrg_path_set_total_cost:
 * @self: an #LrgPath
 * @cost: Total cost
 *
 * Sets the total path cost.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_path_set_total_cost (LrgPath *self,
                                       gfloat   cost);

/**
 * lrg_path_foreach:
 * @self: an #LrgPath
 * @func: (scope call) (closure user_data): Callback function
 * @user_data: User data for callback
 *
 * Visits a snapshot of the waypoints in their original order and with their
 * original indices. Callbacks may modify or free @self without affecting this
 * iteration. Nested calls take their own snapshot of the current path.
 * The snapshot requires temporary memory proportional to the waypoint count.
 */
typedef void (*LrgPathForeachFunc) (gint     x,
                                    gint     y,
                                    guint    index,
                                    gpointer user_data);

LRG_AVAILABLE_IN_ALL
void          lrg_path_foreach        (const LrgPath      *self,
                                       LrgPathForeachFunc  func,
                                       gpointer            user_data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPath, lrg_path_free)

/**
 * lrg_path_set_point:
 * @self: a path
 * @index: waypoint index
 * @x: new X coordinate
 * @y: new Y coordinate
 *
 * Updates a waypoint. A changed coordinate resets total cost to zero because
 * terrain costs cannot be inferred here. Invalid indices leave the path intact.
 *
 * Returns: whether the index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_path_set_point (LrgPath *self,
                    guint   index,
                    gint    x,
                    gint    y);

/**
 * lrg_path_remove_point:
 * @self: a path
 * @index: waypoint index
 *
 * Removes a waypoint while preserving order and resets total cost to zero.
 * Invalid indices leave the path unchanged.
 *
 * Returns: whether a waypoint was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_path_remove_point (LrgPath *self,
                       guint   index);

/**
 * lrg_path_truncate:
 * @self: a path
 * @length: number of leading waypoints to keep
 *
 * Shortens a path and resets total cost to zero if points are removed.
 * A length at least as large as the current length is a no-op.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_path_truncate (LrgPath *self,
                   guint   length);

/**
 * lrg_path_append_path:
 * @self: destination path
 * @other: path whose waypoints are copied
 *
 * Appends every waypoint, including duplicate endpoints. Self-append is safe.
 * A nonempty append resets total cost to zero: connecting terrain costs are
 * unknown. An empty append is a no-op. No navigation validity is checked.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_path_append_path (LrgPath       *self,
                      const LrgPath *other);

/**
 * lrg_path_get_distance:
 * @self: a path
 *
 * Sums Euclidean distances between adjacent waypoints in grid-coordinate units.
 * Uses double arithmetic before coordinate subtraction. This is geometric
 * distance, independent of terrain cost and waypoint count.
 *
 * Returns: distance, or zero for fewer than two waypoints
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_path_get_distance (const LrgPath *self);

G_END_DECLS

#endif /* LRG_PATH_H */
