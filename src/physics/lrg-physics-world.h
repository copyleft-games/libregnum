/* lrg-physics-world.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Physics simulation world.
 */

#ifndef LRG_PHYSICS_WORLD_H
#define LRG_PHYSICS_WORLD_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-rigid-body.h"
#include "lrg-collision-info.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHYSICS_WORLD (lrg_physics_world_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgPhysicsWorld, lrg_physics_world, LRG, PHYSICS_WORLD, GObject)

/**
 * LrgPhysicsWorldClass:
 * @pre_step: Called before physics step
 * @post_step: Called after physics step
 *
 * The class structure for #LrgPhysicsWorld.
 */
struct _LrgPhysicsWorldClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*pre_step)  (LrgPhysicsWorld *self,
                       gfloat           delta_time);

    void (*post_step) (LrgPhysicsWorld *self,
                       gfloat           delta_time);

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_physics_world_new:
 *
 * Creates a new physics world with default settings.
 *
 * Returns: (transfer full): A new #LrgPhysicsWorld
 */
LRG_AVAILABLE_IN_ALL
LrgPhysicsWorld *   lrg_physics_world_new                (void);

/* ==========================================================================
 * World Properties
 * ========================================================================== */

/**
 * lrg_physics_world_get_gravity:
 * @self: an #LrgPhysicsWorld
 * @out_x: (out): Gravity X component
 * @out_y: (out): Gravity Y component
 *
 * Gets the world gravity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_get_gravity        (LrgPhysicsWorld *self,
                                                          gfloat          *out_x,
                                                          gfloat          *out_y);

/**
 * lrg_physics_world_set_gravity:
 * @self: an #LrgPhysicsWorld
 * @x: Gravity X component
 * @y: Gravity Y component
 *
 * Sets the world gravity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_set_gravity        (LrgPhysicsWorld *self,
                                                          gfloat           x,
                                                          gfloat           y);

/**
 * lrg_physics_world_get_time_step:
 * @self: an #LrgPhysicsWorld
 *
 * Gets the fixed time step for physics simulation.
 *
 * Returns: The time step in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_physics_world_get_time_step      (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_set_time_step:
 * @self: an #LrgPhysicsWorld
 * @time_step: Time step in seconds (e.g., 1/60.0 for 60 Hz)
 *
 * Sets the fixed time step for physics simulation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_set_time_step      (LrgPhysicsWorld *self,
                                                          gfloat           time_step);

/**
 * lrg_physics_world_get_velocity_iterations:
 * @self: an #LrgPhysicsWorld
 *
 * Gets the number of velocity constraint iterations.
 *
 * Returns: Number of iterations
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_physics_world_get_velocity_iterations (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_set_velocity_iterations:
 * @self: an #LrgPhysicsWorld
 * @iterations: Number of iterations
 *
 * Sets the number of velocity constraint iterations.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_set_velocity_iterations (LrgPhysicsWorld *self,
                                                               guint            iterations);

/**
 * lrg_physics_world_get_position_iterations:
 * @self: an #LrgPhysicsWorld
 *
 * Gets the number of position constraint iterations.
 *
 * Returns: Number of iterations
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_physics_world_get_position_iterations (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_set_position_iterations:
 * @self: an #LrgPhysicsWorld
 * @iterations: Number of iterations
 *
 * Sets the number of position constraint iterations.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_set_position_iterations (LrgPhysicsWorld *self,
                                                               guint            iterations);

/* ==========================================================================
 * Body Management
 * ========================================================================== */

/**
 * lrg_physics_world_add_body:
 * @self: an #LrgPhysicsWorld
 * @body: (transfer none): The body to add
 *
 * Adds a rigid body to the world.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_add_body           (LrgPhysicsWorld *self,
                                                          LrgRigidBody    *body);

/**
 * lrg_physics_world_remove_body:
 * @self: an #LrgPhysicsWorld
 * @body: The body to remove
 *
 * Removes a rigid body from the world.
 *
 * Returns: %TRUE if the body was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_physics_world_remove_body        (LrgPhysicsWorld *self,
                                                          LrgRigidBody    *body);

/**
 * lrg_physics_world_get_body_count:
 * @self: an #LrgPhysicsWorld
 *
 * Gets the number of bodies in the world.
 *
 * Returns: Number of bodies
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_physics_world_get_body_count     (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_get_bodies:
 * @self: an #LrgPhysicsWorld
 *
 * Gets all bodies in the world.
 *
 * Returns: (transfer none) (element-type LrgRigidBody): The bodies
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_physics_world_get_bodies         (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_clear:
 * @self: an #LrgPhysicsWorld
 *
 * Removes all bodies from the world.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_clear              (LrgPhysicsWorld *self);

/* ==========================================================================
 * Simulation
 * ========================================================================== */

/**
 * lrg_physics_world_step:
 * @self: an #LrgPhysicsWorld
 * @delta_time: Time since last step
 *
 * Advances the physics simulation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_step               (LrgPhysicsWorld *self,
                                                          gfloat           delta_time);

/**
 * lrg_physics_world_is_paused:
 * @self: an #LrgPhysicsWorld
 *
 * Checks if the simulation is paused.
 *
 * Returns: %TRUE if paused
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_physics_world_is_paused          (LrgPhysicsWorld *self);

/**
 * lrg_physics_world_set_paused:
 * @self: an #LrgPhysicsWorld
 * @paused: Whether to pause the simulation
 *
 * Pauses or resumes the simulation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_physics_world_set_paused         (LrgPhysicsWorld *self,
                                                          gboolean         paused);

/* ==========================================================================
 * Queries
 * ========================================================================== */

/**
 * lrg_physics_world_raycast:
 * @self: an #LrgPhysicsWorld
 * @start_x: Ray start X
 * @start_y: Ray start Y
 * @end_x: Ray end X
 * @end_y: Ray end Y
 * @out_hit_body: (out) (transfer none) (nullable): Hit body
 * @out_hit_x: (out) (nullable): Hit point X
 * @out_hit_y: (out) (nullable): Hit point Y
 * @out_hit_normal_x: (out) (nullable): Hit normal X
 * @out_hit_normal_y: (out) (nullable): Hit normal Y
 *
 * Casts a ray and returns the first hit.
 *
 * Returns: %TRUE if something was hit
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_physics_world_raycast            (LrgPhysicsWorld *self,
                                                          gfloat           start_x,
                                                          gfloat           start_y,
                                                          gfloat           end_x,
                                                          gfloat           end_y,
                                                          LrgRigidBody   **out_hit_body,
                                                          gfloat          *out_hit_x,
                                                          gfloat          *out_hit_y,
                                                          gfloat          *out_hit_normal_x,
                                                          gfloat          *out_hit_normal_y);

/**
 * lrg_physics_world_query_aabb:
 * @self: an #LrgPhysicsWorld
 * @min_x: Minimum X
 * @min_y: Minimum Y
 * @max_x: Maximum X
 * @max_y: Maximum Y
 *
 * Queries all bodies overlapping an axis-aligned bounding box.
 *
 * Returns: (transfer container) (element-type LrgRigidBody): Bodies in the AABB
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_physics_world_query_aabb         (LrgPhysicsWorld *self,
                                                          gfloat           min_x,
                                                          gfloat           min_y,
                                                          gfloat           max_x,
                                                          gfloat           max_y);

/**
 * lrg_physics_world_query_point:
 * @self: an #LrgPhysicsWorld
 * @x: Point X
 * @y: Point Y
 *
 * Queries all bodies containing a point.
 *
 * Returns: (transfer container) (element-type LrgRigidBody): Bodies containing the point
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_physics_world_query_point        (LrgPhysicsWorld *self,
                                                          gfloat           x,
                                                          gfloat           y);

G_END_DECLS

#endif /* LRG_PHYSICS_WORLD_H */
