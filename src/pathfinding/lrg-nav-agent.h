/* lrg-nav-agent.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgent: an agent that walks an #LrgNavPolyMesh: it plans a
 * string-pulled path to its destination, follows the corners with
 * arrive steering, crosses off-mesh links, and (inside an #LrgNavCrowd)
 * avoids other agents and the mesh's walls with ORCA (optimal reciprocal
 * collision avoidance, the RVO2 algorithm).
 *
 * LrgNavCrowd: the agents of one mesh, stepped together and
 * deterministically: the same agents, destinations and step lengths give
 * the same positions on every run.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-nav-poly-mesh.h"
#include "../debug/lrg-debug-draw.h"

G_BEGIN_DECLS

/**
 * LrgNavAgentState:
 * @LRG_NAV_AGENT_STATE_IDLE: no destination
 * @LRG_NAV_AGENT_STATE_MOVING: following its path
 * @LRG_NAV_AGENT_STATE_ON_LINK: crossing an off-mesh link (avoidance off)
 * @LRG_NAV_AGENT_STATE_ARRIVED: within #LrgNavAgent:stopping-distance of
 *   its destination
 * @LRG_NAV_AGENT_STATE_FAILED: no path to the destination (off the mesh,
 *   another region, no mesh); it is planned again when the mesh changes
 *
 * What an #LrgNavAgent is doing.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_NAV_AGENT_STATE_IDLE,
	LRG_NAV_AGENT_STATE_MOVING,
	LRG_NAV_AGENT_STATE_ON_LINK,
	LRG_NAV_AGENT_STATE_ARRIVED,
	LRG_NAV_AGENT_STATE_FAILED
} LrgNavAgentState;

LRG_AVAILABLE_IN_ALL
GType lrg_nav_agent_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_NAV_AGENT_STATE (lrg_nav_agent_state_get_type ())

#define LRG_TYPE_NAV_AGENT (lrg_nav_agent_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavAgent, lrg_nav_agent, LRG, NAV_AGENT, GObject)

#define LRG_TYPE_NAV_CROWD (lrg_nav_crowd_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavCrowd, lrg_nav_crowd, LRG, NAV_CROWD, GObject)

/* ==========================================================================
 * LrgNavAgent
 * ========================================================================== */

/**
 * lrg_nav_agent_new:
 *
 * Creates an agent at the origin with radius 0.3, height 1.8, max speed
 * 3.5 m/s, max acceleration 8 m/s², avoidance on with priority 0.5,
 * neighbour distance 8, 10 neighbours, time horizons 2 s (agents) and
 * 1 s (walls), stopping distance 0.1 and slowing distance 1.
 *
 * Returns: (transfer full): a new #LrgNavAgent
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgent * lrg_nav_agent_new (void);

/**
 * lrg_nav_agent_get_mesh:
 * @self: an #LrgNavAgent
 *
 * Returns: (transfer none) (nullable): the mesh it walks
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh * lrg_nav_agent_get_mesh (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_mesh:
 * @self: an #LrgNavAgent
 * @mesh: (nullable): the mesh to walk (an #LrgNavCrowd sets its own)
 *
 * A destination already set is planned again on the new mesh.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_mesh (LrgNavAgent    *self,
                             LrgNavPolyMesh *mesh);

/**
 * lrg_nav_agent_get_position:
 * @self: an #LrgNavAgent
 * @out: (out caller-allocates) (array fixed-size=3): its feet
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_get_position (LrgNavAgent *self,
                                 gfloat      *out);

/**
 * lrg_nav_agent_warp:
 * @self: an #LrgNavAgent
 * @position: (array fixed-size=3): the new position (feet)
 *
 * Teleports the agent (snapped to the mesh when it has one, within one
 * metre plus its radius), stops it and plans its destination again.
 *
 * Returns: %TRUE when the position was on (or near) the mesh
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_warp (LrgNavAgent  *self,
                             const gfloat *position);

/**
 * lrg_nav_agent_get_velocity:
 * @self: an #LrgNavAgent
 * @out: (out caller-allocates) (array fixed-size=3): its velocity (Y 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_get_velocity (LrgNavAgent *self,
                                 gfloat      *out);

/**
 * lrg_nav_agent_get_desired_velocity:
 * @self: an #LrgNavAgent
 * @out: (out caller-allocates) (array fixed-size=3): the velocity its
 *   steering wanted in the last step, before avoidance
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_get_desired_velocity (LrgNavAgent *self,
                                         gfloat      *out);

/**
 * lrg_nav_agent_set_destination:
 * @self: an #LrgNavAgent
 * @destination: (array fixed-size=3): where to go
 *
 * Plans a path at once (lrg_nav_poly_mesh_find_path() through the
 * agent's filter).  On success the agent is
 * %LRG_NAV_AGENT_STATE_MOVING; without a path it is
 * %LRG_NAV_AGENT_STATE_FAILED and #LrgNavAgent::path-failed is emitted.
 *
 * Returns: %TRUE when a path was found
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_set_destination (LrgNavAgent  *self,
                                        const gfloat *destination);

/**
 * lrg_nav_agent_get_destination:
 * @self: an #LrgNavAgent
 * @out: (out caller-allocates) (array fixed-size=3) (optional): the
 *   destination (snapped to the mesh once a path was found)
 *
 * Returns: %TRUE when it has one
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_get_destination (LrgNavAgent *self,
                                        gfloat      *out);

/**
 * lrg_nav_agent_stop:
 * @self: an #LrgNavAgent
 *
 * Drops the destination and path; the agent brakes to a halt (still
 * avoiding others) and becomes %LRG_NAV_AGENT_STATE_IDLE.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_stop (LrgNavAgent *self);

/**
 * lrg_nav_agent_request_repath:
 * @self: an #LrgNavAgent
 *
 * Plans the path to the destination again on the next update (a door
 * opened, a tag was switched).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_request_repath (LrgNavAgent *self);

/**
 * lrg_nav_agent_get_state:
 * @self: an #LrgNavAgent
 *
 * Returns: what the agent is doing
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgentState lrg_nav_agent_get_state (LrgNavAgent *self);

/**
 * lrg_nav_agent_get_path:
 * @self: an #LrgNavAgent
 * @n_floats: (out): number of floats, three per point
 *
 * The current path, from the planned start to the destination.
 *
 * Returns: (transfer none) (array length=n_floats) (nullable): the
 *   points, %NULL without a path; valid until the path changes
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_nav_agent_get_path (LrgNavAgent *self,
                                       guint       *n_floats);

/**
 * lrg_nav_agent_get_corner_index:
 * @self: an #LrgNavAgent
 *
 * Returns: the index (in points) of the path corner it steers to
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_agent_get_corner_index (LrgNavAgent *self);

/**
 * lrg_nav_agent_get_remaining_distance:
 * @self: an #LrgNavAgent
 *
 * Returns: the length of the rest of the path from the agent, 0 without
 *   a path
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_remaining_distance (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_filter:
 * @self: an #LrgNavAgent
 * @filter: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   decides which tagged polygons and links the agent may use, %NULL
 *   for all
 * @user_data: data for @filter
 * @destroy: (nullable): frees @user_data
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_filter (LrgNavAgent          *self,
                               LrgNavPolyFilterFunc  filter,
                               gpointer              user_data,
                               GDestroyNotify        destroy);

/**
 * lrg_nav_agent_get_radius:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:radius
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_radius (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_radius:
 * @self: an #LrgNavAgent
 * @radius: the agent's radius for avoidance (> 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_radius (LrgNavAgent *self,
                               gfloat       radius);

/**
 * lrg_nav_agent_get_height:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:height
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_height (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_height:
 * @self: an #LrgNavAgent
 * @height: the agent's height (> 0): agents on floors further apart
 *   than their heights ignore each other
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_height (LrgNavAgent *self,
                               gfloat       height);

/**
 * lrg_nav_agent_get_max_speed:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:max-speed
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_max_speed (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_max_speed:
 * @self: an #LrgNavAgent
 * @speed: top speed in m/s (>= 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_max_speed (LrgNavAgent *self,
                                  gfloat       speed);

/**
 * lrg_nav_agent_get_max_acceleration:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:max-acceleration
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_max_acceleration (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_max_acceleration:
 * @self: an #LrgNavAgent
 * @acceleration: most velocity change per second in m/s² (> 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_max_acceleration (LrgNavAgent *self,
                                         gfloat       acceleration);

/**
 * lrg_nav_agent_get_avoidance_priority:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:avoidance-priority
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_get_avoidance_priority (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_avoidance_priority:
 * @self: an #LrgNavAgent
 * @priority: 0 .. 1; of two agents the one with the higher priority
 *   gives way less (equal priorities share the avoidance evenly)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_avoidance_priority (LrgNavAgent *self,
                                           gfloat       priority);

/**
 * lrg_nav_agent_get_avoidance_enabled:
 * @self: an #LrgNavAgent
 *
 * Returns: #LrgNavAgent:avoidance-enabled
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_get_avoidance_enabled (LrgNavAgent *self);

/**
 * lrg_nav_agent_set_avoidance_enabled:
 * @self: an #LrgNavAgent
 * @enabled: whether it avoids others; others avoid it either way (and
 *   take all of the avoidance when it does not)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_set_avoidance_enabled (LrgNavAgent *self,
                                          gboolean     enabled);

/**
 * lrg_nav_agent_update:
 * @self: an #LrgNavAgent
 * @delta: seconds to advance (> 0)
 *
 * Steps an agent that is not in a crowd: path following, arrive and link
 * traversal, without avoidance.  Agents in an #LrgNavCrowd are stepped by
 * lrg_nav_crowd_step(); do not call this for them.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_update (LrgNavAgent *self,
                           gfloat       delta);

/**
 * lrg_nav_agent_get_crowd:
 * @self: an #LrgNavAgent
 *
 * Returns: (transfer none) (nullable): the crowd it belongs to
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavCrowd * lrg_nav_agent_get_crowd (LrgNavAgent *self);

/* ==========================================================================
 * LrgNavCrowd
 * ========================================================================== */

/**
 * lrg_nav_crowd_new:
 * @mesh: (nullable): the mesh its agents walk
 *
 * Returns: (transfer full): a new, empty crowd
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavCrowd * lrg_nav_crowd_new (LrgNavPolyMesh *mesh);

/**
 * lrg_nav_crowd_get_mesh:
 * @self: an #LrgNavCrowd
 *
 * Returns: (transfer none) (nullable): #LrgNavCrowd:mesh
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavPolyMesh * lrg_nav_crowd_get_mesh (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_set_mesh:
 * @self: an #LrgNavCrowd
 * @mesh: (nullable): the mesh; every agent moves to it (and plans again)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_crowd_set_mesh (LrgNavCrowd    *self,
                             LrgNavPolyMesh *mesh);

/**
 * lrg_nav_crowd_add_agent:
 * @self: an #LrgNavCrowd
 * @agent: an agent in no crowd
 *
 * Adds @agent (the crowd holds a reference and gives it its mesh).
 * Agents are stepped in the order they were added.
 *
 * Returns: %FALSE when @agent already belongs to a crowd
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_crowd_add_agent (LrgNavCrowd *self,
                                  LrgNavAgent *agent);

/**
 * lrg_nav_crowd_remove_agent:
 * @self: an #LrgNavCrowd
 * @agent: an agent of this crowd
 *
 * Returns: %FALSE when @agent is not in this crowd
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_crowd_remove_agent (LrgNavCrowd *self,
                                     LrgNavAgent *agent);

/**
 * lrg_nav_crowd_get_n_agents:
 * @self: an #LrgNavCrowd
 *
 * Returns: the number of agents
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_nav_crowd_get_n_agents (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_get_agent:
 * @self: an #LrgNavCrowd
 * @index: an agent index (insertion order)
 *
 * Returns: (transfer none) (nullable): the agent
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgent * lrg_nav_crowd_get_agent (LrgNavCrowd *self,
                                       guint        index);

/**
 * lrg_nav_crowd_clear:
 * @self: an #LrgNavCrowd
 *
 * Removes every agent.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_crowd_clear (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_set_debug_agent:
 * @self: an #LrgNavCrowd
 * @agent: (nullable): an agent of this crowd, %NULL for all
 *
 * Restricts the per-agent detail of lrg_nav_crowd_debug_draw()
 * (%LRG_NAV_DEBUG_ORCA and %LRG_NAV_DEBUG_NEIGHBOURS) to one agent, the
 * one an agent debug view has selected.  Cleared when the agent leaves.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_crowd_set_debug_agent (LrgNavCrowd *self,
                                    LrgNavAgent *agent);

/**
 * lrg_nav_crowd_get_debug_agent:
 * @self: an #LrgNavCrowd
 *
 * Returns: (transfer none) (nullable): the agent the detail is drawn for
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgent * lrg_nav_crowd_get_debug_agent (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_step:
 * @self: an #LrgNavCrowd
 * @delta: seconds to advance (> 0); fixed steps give reproducible runs
 *
 * One deterministic step of every agent, in insertion order per phase:
 * paths are (re)planned and steered (preferred velocities, separation),
 * every agent's new velocity is solved with ORCA against its nearest
 * neighbours and walls from the state before the step, the velocities
 * are limited by the acceleration and integrated, the agents are kept on
 * the mesh, overlaps left over are pushed apart, and finally the
 * #LrgNavAgent::arrived and other signals and #LrgNavCrowd::stepped are
 * emitted.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_crowd_step (LrgNavCrowd *self,
                         gfloat       delta);

/**
 * lrg_nav_crowd_get_step_count:
 * @self: an #LrgNavCrowd
 *
 * Returns: how many steps ran
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_nav_crowd_get_step_count (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_get_last_step_seconds:
 * @self: an #LrgNavCrowd
 *
 * Returns: how long the last lrg_nav_crowd_step() took (monotonic clock)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_nav_crowd_get_last_step_seconds (LrgNavCrowd *self);

/**
 * lrg_nav_crowd_debug_draw:
 * @self: an #LrgNavCrowd
 * @draw: the batch to record into (one frame)
 * @flags: what to draw: the agent flags (%LRG_NAV_DEBUG_AGENTS,
 *   %LRG_NAV_DEBUG_PATHS, %LRG_NAV_DEBUG_VELOCITIES, %LRG_NAV_DEBUG_ORCA,
 *   %LRG_NAV_DEBUG_NEIGHBOURS) for the agents and the mesh flags for
 *   its mesh (lrg_nav_poly_mesh_debug_draw())
 *
 * Records the crowd in %LRG_DEBUG_CATEGORY_NAVIGATION.  Agents are
 * cylinders coloured by state (moving blue, on a link cyan, arrived
 * green, failed red, idle grey).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_crowd_debug_draw (LrgNavCrowd      *self,
                               LrgDebugDraw     *draw,
                               LrgNavDebugFlags  flags);

G_END_DECLS
