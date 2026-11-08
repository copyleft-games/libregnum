/* lrg-nav-agent-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavAgentComponent: makes its #LrgEntity3D a navigation agent of the
 * world's crowd (lrg_world3d_get_nav_crowd()).  Give it a destination
 * and it walks there over the world's navigation mesh
 * (lrg_world3d_set_navigation()), steering around other agents and
 * walls.  After every crowd step it writes the agent back:
 *
 * - by default it moves the entity (the agent's feet plus
 *   #LrgNavAgentComponent:base-offset) and turns it to face its
 *   velocity; a kinematic #LrgRigidBodyComponent on the entity follows
 *   the entity, so it pushes dynamic bodies;
 * - with a dynamic #LrgRigidBodyComponent it sets the body's horizontal
 *   velocity instead (gravity and collisions stay physical) and the
 *   agent follows the body.
 *
 * An entity moved by game code is noticed and the agent is warped there.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../pathfinding/lrg-nav-agent.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAV_AGENT_COMPONENT (lrg_nav_agent_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavAgentComponent, lrg_nav_agent_component, LRG, NAV_AGENT_COMPONENT, LrgComponent)

/**
 * lrg_nav_agent_component_new:
 *
 * Creates an agent component with the #LrgNavAgent defaults (speed 3.5,
 * acceleration 8, radius 0.3, height 1.8, priority 0.5, avoidance on,
 * stopping distance 0.1), no base offset, turning to face its velocity
 * at 720 degrees per second.
 *
 * Returns: (transfer full): a new #LrgNavAgentComponent
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgentComponent * lrg_nav_agent_component_new (void);

/**
 * lrg_nav_agent_component_get_agent:
 * @self: an #LrgNavAgentComponent
 *
 * The agent behind the component (state, path, velocity, filter); its
 * settings mirror the component's properties.
 *
 * Returns: (transfer none): the agent
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgent * lrg_nav_agent_component_get_agent (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_destination:
 * @self: an #LrgNavAgentComponent
 * @destination: (array fixed-size=3): where to walk (world space)
 *
 * Sends the agent somewhere.  In a world with a navigation mesh the path
 * is planned at once; before the entity joins a world (or before the
 * world has a mesh) the destination is kept and planned when it can be.
 *
 * Returns: %FALSE when a path was planned and none was found
 *   (#LrgNavAgentComponent::path-failed is emitted too)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_component_set_destination (LrgNavAgentComponent *self,
                                                  const gfloat         *destination);

/**
 * lrg_nav_agent_component_get_destination:
 * @self: an #LrgNavAgentComponent
 * @out: (out caller-allocates) (array fixed-size=3) (optional): the destination
 *
 * Returns: %TRUE when it has one
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_component_get_destination (LrgNavAgentComponent *self,
                                                  gfloat               *out);

/**
 * lrg_nav_agent_component_stop:
 * @self: an #LrgNavAgentComponent
 *
 * Drops the destination; the agent brakes to a halt.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_stop (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_get_state:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: the agent's state
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgNavAgentState lrg_nav_agent_component_get_state (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_has_arrived:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: %TRUE when the agent reached its destination
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nav_agent_component_has_arrived (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_get_remaining_distance:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: the length of the rest of the path, 0 without one
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_remaining_distance (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_get_speed:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: #LrgNavAgentComponent:speed
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_speed (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_speed:
 * @self: an #LrgNavAgentComponent
 * @speed: top speed in m/s
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_set_speed (LrgNavAgentComponent *self,
                                        gfloat                speed);

/**
 * lrg_nav_agent_component_get_acceleration:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: #LrgNavAgentComponent:acceleration
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_acceleration (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_acceleration:
 * @self: an #LrgNavAgentComponent
 * @acceleration: most velocity change in m/s²
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_set_acceleration (LrgNavAgentComponent *self,
                                               gfloat                acceleration);

/**
 * lrg_nav_agent_component_get_radius:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: #LrgNavAgentComponent:radius
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_radius (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_radius:
 * @self: an #LrgNavAgentComponent
 * @radius: avoidance radius in metres
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_set_radius (LrgNavAgentComponent *self,
                                         gfloat                radius);

/**
 * lrg_nav_agent_component_get_height:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: #LrgNavAgentComponent:height
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_height (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_height:
 * @self: an #LrgNavAgentComponent
 * @height: the agent's height in metres
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_set_height (LrgNavAgentComponent *self,
                                         gfloat                height);

/**
 * lrg_nav_agent_component_get_avoidance_priority:
 * @self: an #LrgNavAgentComponent
 *
 * Returns: #LrgNavAgentComponent:avoidance-priority
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_nav_agent_component_get_avoidance_priority (LrgNavAgentComponent *self);

/**
 * lrg_nav_agent_component_set_avoidance_priority:
 * @self: an #LrgNavAgentComponent
 * @priority: 0 .. 1, higher gives way less
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nav_agent_component_set_avoidance_priority (LrgNavAgentComponent *self,
                                                     gfloat                priority);

G_END_DECLS
