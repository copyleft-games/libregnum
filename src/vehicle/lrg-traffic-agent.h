/* lrg-traffic-agent.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTrafficAgent - AI traffic participant.
 *
 * Represents an AI-controlled vehicle that navigates along roads,
 * avoids obstacles, and follows traffic rules.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-vehicle.h"
#include "lrg-road-network.h"

G_BEGIN_DECLS

/* Note: LrgTrafficBehavior is defined in lrg-enums.h */

/**
 * LrgTrafficState:
 * @LRG_TRAFFIC_STATE_IDLE: Stationary
 * @LRG_TRAFFIC_STATE_DRIVING: Following road
 * @LRG_TRAFFIC_STATE_STOPPED: Stopped (traffic light, obstacle)
 * @LRG_TRAFFIC_STATE_AVOIDING: Avoiding obstacle
 * @LRG_TRAFFIC_STATE_ARRIVED: Reached destination
 *
 * Traffic agent states.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_TRAFFIC_STATE_IDLE,
    LRG_TRAFFIC_STATE_DRIVING,
    LRG_TRAFFIC_STATE_STOPPED,
    LRG_TRAFFIC_STATE_AVOIDING,
    LRG_TRAFFIC_STATE_ARRIVED
} LrgTrafficState;

#define LRG_TYPE_TRAFFIC_AGENT (lrg_traffic_agent_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTrafficAgent, lrg_traffic_agent,
                          LRG, TRAFFIC_AGENT, GObject)

/**
 * LrgTrafficAgentClass:
 * @parent_class: Parent class
 * @update_ai: Called to update AI behavior
 * @on_obstacle_detected: Called when obstacle is detected
 * @on_destination_reached: Called when destination is reached
 *
 * Virtual table for #LrgTrafficAgent.
 *
 * Since: 1.0
 */
struct _LrgTrafficAgentClass
{
    GObjectClass parent_class;

    /*< public >*/

    void (*update_ai)              (LrgTrafficAgent *self,
                                    gfloat           delta);
    void (*on_obstacle_detected)   (LrgTrafficAgent *self,
                                    gfloat           distance);
    void (*on_destination_reached) (LrgTrafficAgent *self);

    /*< private >*/
    gpointer _reserved[4];
};

/* Signals */

/**
 * LrgTrafficAgent::destination-reached:
 * @agent: the #LrgTrafficAgent
 *
 * Emitted when the agent reaches its destination.
 *
 * Since: 1.0
 */

/**
 * LrgTrafficAgent::obstacle-detected:
 * @agent: the #LrgTrafficAgent
 * @distance: Distance to obstacle
 *
 * Emitted when an obstacle is detected.
 *
 * Since: 1.0
 */

/**
 * lrg_traffic_agent_new:
 *
 * Creates a new traffic agent.
 *
 * Returns: (transfer full): A new #LrgTrafficAgent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTrafficAgent *
lrg_traffic_agent_new (void);

/* Vehicle */

/**
 * lrg_traffic_agent_set_vehicle:
 * @self: an #LrgTrafficAgent
 * @vehicle: (transfer none): Vehicle to control
 *
 * Sets the vehicle this agent controls.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_set_vehicle (LrgTrafficAgent *self,
                               LrgVehicle      *vehicle);

/**
 * lrg_traffic_agent_get_vehicle:
 * @self: an #LrgTrafficAgent
 *
 * Gets the controlled vehicle.
 *
 * Returns: (transfer none) (nullable): The vehicle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicle *
lrg_traffic_agent_get_vehicle (LrgTrafficAgent *self);

/* Road network */

/**
 * lrg_traffic_agent_set_road_network:
 * @self: an #LrgTrafficAgent
 * @network: (transfer none): Road network to navigate
 *
 * Sets the road network for navigation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_set_road_network (LrgTrafficAgent *self,
                                    LrgRoadNetwork  *network);

/**
 * lrg_traffic_agent_get_road_network:
 * @self: an #LrgTrafficAgent
 *
 * Gets the road network.
 *
 * Returns: (transfer none) (nullable): The road network
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRoadNetwork *
lrg_traffic_agent_get_road_network (LrgTrafficAgent *self);

/* Navigation */

/**
 * lrg_traffic_agent_set_destination:
 * @self: an #LrgTrafficAgent
 * @road_id: Destination road ID
 * @t: Position on road (0-1)
 *
 * Sets the navigation destination.
 *
 * Returns: %TRUE if route found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_traffic_agent_set_destination (LrgTrafficAgent *self,
                                   const gchar     *road_id,
                                   gfloat           t);

/**
 * lrg_traffic_agent_set_random_destination:
 * @self: an #LrgTrafficAgent
 *
 * Sets a random destination on the road network.
 *
 * Returns: %TRUE if destination set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_traffic_agent_set_random_destination (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_clear_destination:
 * @self: an #LrgTrafficAgent
 *
 * Clears the current destination.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_clear_destination (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_has_destination:
 * @self: an #LrgTrafficAgent
 *
 * Checks if agent has a destination.
 *
 * Returns: %TRUE if has destination
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_traffic_agent_has_destination (LrgTrafficAgent *self);

/* Behavior */

/**
 * lrg_traffic_agent_set_behavior:
 * @self: an #LrgTrafficAgent
 * @behavior: Driving behavior
 *
 * Sets the driving behavior.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_set_behavior (LrgTrafficAgent   *self,
                                LrgTrafficBehavior behavior);

/**
 * lrg_traffic_agent_get_behavior:
 * @self: an #LrgTrafficAgent
 *
 * Gets the driving behavior.
 *
 * Returns: Driving behavior
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTrafficBehavior
lrg_traffic_agent_get_behavior (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_set_max_speed:
 * @self: an #LrgTrafficAgent
 * @speed: Maximum speed
 *
 * Sets the agent's maximum speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_set_max_speed (LrgTrafficAgent *self,
                                 gfloat           speed);

/**
 * lrg_traffic_agent_get_max_speed:
 * @self: an #LrgTrafficAgent
 *
 * Gets the maximum speed.
 *
 * Returns: Maximum speed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_traffic_agent_get_max_speed (LrgTrafficAgent *self);

/* State */

/**
 * lrg_traffic_agent_get_state:
 * @self: an #LrgTrafficAgent
 *
 * Gets the current state.
 *
 * Returns: Current state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTrafficState
lrg_traffic_agent_get_state (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_get_current_road:
 * @self: an #LrgTrafficAgent
 *
 * Gets the current road ID.
 *
 * Returns: (transfer none) (nullable): Current road ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_traffic_agent_get_current_road (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_get_current_position:
 * @self: an #LrgTrafficAgent
 *
 * Gets position on current road (0-1).
 *
 * Returns: Position on road
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_traffic_agent_get_current_position (LrgTrafficAgent *self);

/* Obstacle avoidance */

/**
 * lrg_traffic_agent_set_obstacle_detection_range:
 * @self: an #LrgTrafficAgent
 * @range: Detection range
 *
 * Sets obstacle detection range.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_set_obstacle_detection_range (LrgTrafficAgent *self,
                                                gfloat           range);

/**
 * lrg_traffic_agent_add_obstacle:
 * @self: an #LrgTrafficAgent
 * @x: Obstacle X position
 * @y: Obstacle Y position
 * @z: Obstacle Z position
 * @radius: Obstacle radius
 *
 * Adds a temporary obstacle to avoid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_add_obstacle (LrgTrafficAgent *self,
                                gfloat           x,
                                gfloat           y,
                                gfloat           z,
                                gfloat           radius);

/**
 * lrg_traffic_agent_clear_obstacles:
 * @self: an #LrgTrafficAgent
 *
 * Clears all temporary obstacles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_clear_obstacles (LrgTrafficAgent *self);

/* Update */

/**
 * lrg_traffic_agent_update:
 * @self: an #LrgTrafficAgent
 * @delta: Time step in seconds
 *
 * Updates the traffic agent AI.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_update (LrgTrafficAgent *self,
                          gfloat           delta);

/**
 * lrg_traffic_agent_start:
 * @self: an #LrgTrafficAgent
 *
 * Starts the agent (begins driving).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_start (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_stop:
 * @self: an #LrgTrafficAgent
 *
 * Stops the agent (stops driving).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_traffic_agent_stop (LrgTrafficAgent *self);

/**
 * lrg_traffic_agent_is_active:
 * @self: an #LrgTrafficAgent
 *
 * Checks if agent is active.
 *
 * Returns: %TRUE if active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_traffic_agent_is_active (LrgTrafficAgent *self);

G_END_DECLS
