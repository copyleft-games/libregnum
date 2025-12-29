/* lrg-road.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRoad - Road segment with waypoints.
 *
 * Represents a single road segment consisting of connected waypoints.
 * Used for traffic AI navigation and pathfinding.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ROAD (lrg_road_get_type ())

/**
 * LrgRoadWaypoint:
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: Road width at this point
 * @speed_limit: Speed limit at this point
 *
 * A single waypoint on a road.
 *
 * Since: 1.0
 */
typedef struct _LrgRoadWaypoint LrgRoadWaypoint;

struct _LrgRoadWaypoint
{
    gfloat x;
    gfloat y;
    gfloat z;
    gfloat width;
    gfloat speed_limit;
};

/**
 * LrgRoad:
 *
 * A road segment consisting of waypoints.
 *
 * Since: 1.0
 */
typedef struct _LrgRoad LrgRoad;

LRG_AVAILABLE_IN_ALL
GType
lrg_road_get_type (void) G_GNUC_CONST;

/**
 * lrg_road_new:
 * @id: Unique road ID
 *
 * Creates a new empty road.
 *
 * Returns: (transfer full): A new #LrgRoad
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRoad *
lrg_road_new (const gchar *id);

/**
 * lrg_road_copy:
 * @road: an #LrgRoad
 *
 * Creates a copy of the road.
 *
 * Returns: (transfer full): A copy of @road
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRoad *
lrg_road_copy (const LrgRoad *road);

/**
 * lrg_road_free:
 * @road: an #LrgRoad
 *
 * Frees the road.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_road_free (LrgRoad *road);

/**
 * lrg_road_get_id:
 * @road: an #LrgRoad
 *
 * Gets the road ID.
 *
 * Returns: (transfer none): The road ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_road_get_id (const LrgRoad *road);

/* Waypoints */

/**
 * lrg_road_add_waypoint:
 * @road: an #LrgRoad
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @width: Road width
 * @speed_limit: Speed limit
 *
 * Adds a waypoint to the road.
 *
 * Returns: Index of added waypoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_road_add_waypoint (LrgRoad *road,
                       gfloat   x,
                       gfloat   y,
                       gfloat   z,
                       gfloat   width,
                       gfloat   speed_limit);

/**
 * lrg_road_get_waypoint:
 * @road: an #LrgRoad
 * @index: Waypoint index
 *
 * Gets a waypoint by index.
 *
 * Returns: (transfer none) (nullable): The waypoint, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgRoadWaypoint *
lrg_road_get_waypoint (const LrgRoad *road,
                       guint          index);

/**
 * lrg_road_get_waypoint_count:
 * @road: an #LrgRoad
 *
 * Gets the number of waypoints.
 *
 * Returns: Waypoint count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_road_get_waypoint_count (const LrgRoad *road);

/**
 * lrg_road_clear_waypoints:
 * @road: an #LrgRoad
 *
 * Removes all waypoints.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_road_clear_waypoints (LrgRoad *road);

/* Interpolation */

/**
 * lrg_road_interpolate:
 * @road: an #LrgRoad
 * @t: Interpolation parameter (0-1 along entire road)
 * @x: (out): Interpolated X position
 * @y: (out): Interpolated Y position
 * @z: (out): Interpolated Z position
 *
 * Interpolates a position along the road.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_interpolate (const LrgRoad *road,
                      gfloat         t,
                      gfloat        *x,
                      gfloat        *y,
                      gfloat        *z);

/**
 * lrg_road_get_direction_at:
 * @road: an #LrgRoad
 * @t: Parameter (0-1)
 * @dx: (out): X direction
 * @dy: (out): Y direction
 * @dz: (out): Z direction
 *
 * Gets the road direction at a parameter.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_get_direction_at (const LrgRoad *road,
                           gfloat         t,
                           gfloat        *dx,
                           gfloat        *dy,
                           gfloat        *dz);

/**
 * lrg_road_get_width_at:
 * @road: an #LrgRoad
 * @t: Parameter (0-1)
 *
 * Gets the interpolated road width at a parameter.
 *
 * Returns: Road width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_road_get_width_at (const LrgRoad *road,
                       gfloat         t);

/**
 * lrg_road_get_speed_limit_at:
 * @road: an #LrgRoad
 * @t: Parameter (0-1)
 *
 * Gets the interpolated speed limit at a parameter.
 *
 * Returns: Speed limit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_road_get_speed_limit_at (const LrgRoad *road,
                             gfloat         t);

/**
 * lrg_road_get_length:
 * @road: an #LrgRoad
 *
 * Gets the total road length.
 *
 * Returns: Road length
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_road_get_length (const LrgRoad *road);

/**
 * lrg_road_find_nearest_point:
 * @road: an #LrgRoad
 * @x: Query X position
 * @y: Query Y position
 * @z: Query Z position
 * @out_t: (out): Nearest parameter
 * @out_distance: (out): Distance to nearest point
 *
 * Finds the nearest point on the road to a position.
 *
 * Returns: %TRUE if found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_find_nearest_point (const LrgRoad *road,
                             gfloat         x,
                             gfloat         y,
                             gfloat         z,
                             gfloat        *out_t,
                             gfloat        *out_distance);

/* Properties */

/**
 * lrg_road_set_one_way:
 * @road: an #LrgRoad
 * @one_way: Whether road is one-way
 *
 * Sets whether the road is one-way.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_road_set_one_way (LrgRoad  *road,
                      gboolean  one_way);

/**
 * lrg_road_is_one_way:
 * @road: an #LrgRoad
 *
 * Checks if road is one-way.
 *
 * Returns: %TRUE if one-way
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_is_one_way (const LrgRoad *road);

/**
 * lrg_road_set_lane_count:
 * @road: an #LrgRoad
 * @lanes: Number of lanes
 *
 * Sets the number of lanes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_road_set_lane_count (LrgRoad *road,
                         guint    lanes);

/**
 * lrg_road_get_lane_count:
 * @road: an #LrgRoad
 *
 * Gets the number of lanes.
 *
 * Returns: Lane count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_road_get_lane_count (const LrgRoad *road);

G_END_DECLS
