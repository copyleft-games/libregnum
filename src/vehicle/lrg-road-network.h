/* lrg-road-network.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRoadNetwork - Connected road system.
 *
 * Manages a collection of roads with connections between them.
 * Provides pathfinding for traffic AI navigation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-road.h"

G_BEGIN_DECLS

#define LRG_TYPE_ROAD_NETWORK (lrg_road_network_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRoadNetwork, lrg_road_network,
                      LRG, ROAD_NETWORK, GObject)

/**
 * lrg_road_network_new:
 *
 * Creates a new empty road network.
 *
 * Returns: (transfer full): A new #LrgRoadNetwork
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRoadNetwork *
lrg_road_network_new (void);

/* Road management */

/**
 * lrg_road_network_add_road:
 * @self: an #LrgRoadNetwork
 * @road: (transfer full): Road to add
 *
 * Adds a road to the network. Takes ownership.
 *
 * Returns: %TRUE if added successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_add_road (LrgRoadNetwork *self,
                           LrgRoad        *road);

/**
 * lrg_road_network_get_road:
 * @self: an #LrgRoadNetwork
 * @road_id: Road ID
 *
 * Gets a road by ID.
 *
 * Returns: (transfer none) (nullable): The road, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRoad *
lrg_road_network_get_road (LrgRoadNetwork *self,
                           const gchar    *road_id);

/**
 * lrg_road_network_remove_road:
 * @self: an #LrgRoadNetwork
 * @road_id: Road ID to remove
 *
 * Removes a road from the network.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_remove_road (LrgRoadNetwork *self,
                              const gchar    *road_id);

/**
 * lrg_road_network_get_road_count:
 * @self: an #LrgRoadNetwork
 *
 * Gets the number of roads.
 *
 * Returns: Road count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_road_network_get_road_count (LrgRoadNetwork *self);

/**
 * lrg_road_network_get_roads:
 * @self: an #LrgRoadNetwork
 *
 * Gets all roads in the network.
 *
 * Returns: (element-type LrgRoad) (transfer none): List of roads
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_road_network_get_roads (LrgRoadNetwork *self);

/* Connections */

/**
 * lrg_road_network_connect:
 * @self: an #LrgRoadNetwork
 * @from_road_id: Source road ID
 * @from_end: Which end to connect (TRUE=end, FALSE=start)
 * @to_road_id: Destination road ID
 * @to_end: Which end to connect to (TRUE=end, FALSE=start)
 *
 * Connects two roads at their endpoints.
 *
 * Returns: %TRUE if connected successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_connect (LrgRoadNetwork *self,
                          const gchar    *from_road_id,
                          gboolean        from_end,
                          const gchar    *to_road_id,
                          gboolean        to_end);

/**
 * lrg_road_network_disconnect:
 * @self: an #LrgRoadNetwork
 * @from_road_id: Source road ID
 * @from_end: Which end
 * @to_road_id: Destination road ID
 * @to_end: Which end
 *
 * Removes a connection between roads.
 *
 * Returns: %TRUE if disconnected
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_disconnect (LrgRoadNetwork *self,
                             const gchar    *from_road_id,
                             gboolean        from_end,
                             const gchar    *to_road_id,
                             gboolean        to_end);

/**
 * lrg_road_network_get_connections:
 * @self: an #LrgRoadNetwork
 * @road_id: Road ID
 * @from_end: Which end
 *
 * Gets connected roads at an endpoint.
 *
 * Returns: (element-type utf8) (transfer container): List of connected road IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList *
lrg_road_network_get_connections (LrgRoadNetwork *self,
                                  const gchar    *road_id,
                                  gboolean        from_end);

/* Pathfinding */

/**
 * lrg_road_network_find_route:
 * @self: an #LrgRoadNetwork
 * @from_road_id: Starting road
 * @from_t: Starting position (0-1)
 * @to_road_id: Destination road
 * @to_t: Destination position (0-1)
 * @out_road_sequence: (out) (element-type utf8) (transfer container): Sequence of road IDs
 *
 * Finds a route between two points.
 *
 * Returns: %TRUE if route found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_find_route (LrgRoadNetwork *self,
                             const gchar    *from_road_id,
                             gfloat          from_t,
                             const gchar    *to_road_id,
                             gfloat          to_t,
                             GList         **out_road_sequence);

/**
 * lrg_road_network_get_route_length:
 * @self: an #LrgRoadNetwork
 * @road_sequence: (element-type utf8): Sequence of road IDs
 *
 * Calculates the length of a route.
 *
 * Returns: Total route length
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_road_network_get_route_length (LrgRoadNetwork *self,
                                   GList          *road_sequence);

/* Queries */

/**
 * lrg_road_network_get_nearest_road:
 * @self: an #LrgRoadNetwork
 * @x: Query X position
 * @y: Query Y position
 * @z: Query Z position
 * @out_road_id: (out) (transfer none): Nearest road ID
 * @out_t: (out): Position on road (0-1)
 * @out_distance: (out): Distance to road
 *
 * Finds the nearest road to a position.
 *
 * Returns: %TRUE if found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_get_nearest_road (LrgRoadNetwork *self,
                                   gfloat          x,
                                   gfloat          y,
                                   gfloat          z,
                                   const gchar   **out_road_id,
                                   gfloat         *out_t,
                                   gfloat         *out_distance);

/**
 * lrg_road_network_get_random_spawn_point:
 * @self: an #LrgRoadNetwork
 * @x: (out): Spawn X position
 * @y: (out): Spawn Y position
 * @z: (out): Spawn Z position
 * @heading: (out): Heading angle
 * @out_road_id: (out) (transfer none): Road ID
 * @out_t: (out): Position on road
 *
 * Gets a random spawn point on the road network.
 *
 * Returns: %TRUE if found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_road_network_get_random_spawn_point (LrgRoadNetwork *self,
                                         gfloat         *x,
                                         gfloat         *y,
                                         gfloat         *z,
                                         gfloat         *heading,
                                         const gchar   **out_road_id,
                                         gfloat         *out_t);

/**
 * lrg_road_network_clear:
 * @self: an #LrgRoadNetwork
 *
 * Removes all roads and connections.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_road_network_clear (LrgRoadNetwork *self);

G_END_DECLS
