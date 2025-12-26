/* lrg-rigid-body.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rigid body for physics simulation.
 */

#ifndef LRG_RIGID_BODY_H
#define LRG_RIGID_BODY_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_RIGID_BODY (lrg_rigid_body_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgRigidBody, lrg_rigid_body, LRG, RIGID_BODY, GObject)

/**
 * LrgRigidBodyClass:
 * @on_collision: Called when this body collides with another
 * @on_trigger: Called when this body enters a trigger
 *
 * The class structure for #LrgRigidBody.
 */
struct _LrgRigidBodyClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*on_collision) (LrgRigidBody *self,
                          LrgRigidBody *other,
                          gfloat        normal_x,
                          gfloat        normal_y);

    void (*on_trigger)   (LrgRigidBody *self,
                          LrgRigidBody *other,
                          gboolean      entering);

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_rigid_body_new:
 * @body_type: The type of rigid body
 *
 * Creates a new rigid body.
 *
 * Returns: (transfer full): A new #LrgRigidBody
 */
LRG_AVAILABLE_IN_ALL
LrgRigidBody *      lrg_rigid_body_new                   (LrgRigidBodyType body_type);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_rigid_body_get_body_type:
 * @self: an #LrgRigidBody
 *
 * Gets the body type.
 *
 * Returns: The body type
 */
LRG_AVAILABLE_IN_ALL
LrgRigidBodyType    lrg_rigid_body_get_body_type         (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_body_type:
 * @self: an #LrgRigidBody
 * @body_type: The body type
 *
 * Sets the body type.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_body_type         (LrgRigidBody     *self,
                                                          LrgRigidBodyType  body_type);

/**
 * lrg_rigid_body_get_mass:
 * @self: an #LrgRigidBody
 *
 * Gets the mass of the body.
 *
 * Returns: The mass in kg
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_mass              (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_mass:
 * @self: an #LrgRigidBody
 * @mass: The mass in kg (must be > 0)
 *
 * Sets the mass of the body.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_mass              (LrgRigidBody *self,
                                                          gfloat        mass);

/**
 * lrg_rigid_body_get_restitution:
 * @self: an #LrgRigidBody
 *
 * Gets the restitution (bounciness) coefficient.
 *
 * Returns: The restitution (0.0 = no bounce, 1.0 = perfect bounce)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_restitution       (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_restitution:
 * @self: an #LrgRigidBody
 * @restitution: The restitution coefficient (0.0 - 1.0)
 *
 * Sets the restitution (bounciness) coefficient.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_restitution       (LrgRigidBody *self,
                                                          gfloat        restitution);

/**
 * lrg_rigid_body_get_friction:
 * @self: an #LrgRigidBody
 *
 * Gets the friction coefficient.
 *
 * Returns: The friction coefficient
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_friction          (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_friction:
 * @self: an #LrgRigidBody
 * @friction: The friction coefficient (0.0 - 1.0)
 *
 * Sets the friction coefficient.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_friction          (LrgRigidBody *self,
                                                          gfloat        friction);

/**
 * lrg_rigid_body_get_linear_damping:
 * @self: an #LrgRigidBody
 *
 * Gets the linear damping factor.
 *
 * Returns: The linear damping (0.0 - 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_linear_damping    (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_linear_damping:
 * @self: an #LrgRigidBody
 * @damping: The linear damping (0.0 - 1.0)
 *
 * Sets the linear damping factor.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_linear_damping    (LrgRigidBody *self,
                                                          gfloat        damping);

/**
 * lrg_rigid_body_get_angular_damping:
 * @self: an #LrgRigidBody
 *
 * Gets the angular damping factor.
 *
 * Returns: The angular damping (0.0 - 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_angular_damping   (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_angular_damping:
 * @self: an #LrgRigidBody
 * @damping: The angular damping (0.0 - 1.0)
 *
 * Sets the angular damping factor.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_angular_damping   (LrgRigidBody *self,
                                                          gfloat        damping);

/**
 * lrg_rigid_body_get_is_trigger:
 * @self: an #LrgRigidBody
 *
 * Gets whether this body is a trigger (no physical response).
 *
 * Returns: %TRUE if this is a trigger
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_rigid_body_get_is_trigger        (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_is_trigger:
 * @self: an #LrgRigidBody
 * @is_trigger: Whether this body is a trigger
 *
 * Sets whether this body is a trigger.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_is_trigger        (LrgRigidBody *self,
                                                          gboolean      is_trigger);

/**
 * lrg_rigid_body_get_gravity_scale:
 * @self: an #LrgRigidBody
 *
 * Gets the gravity scale multiplier.
 *
 * Returns: The gravity scale (1.0 = normal, 0.0 = no gravity)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_gravity_scale     (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_gravity_scale:
 * @self: an #LrgRigidBody
 * @scale: The gravity scale multiplier
 *
 * Sets the gravity scale multiplier.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_gravity_scale     (LrgRigidBody *self,
                                                          gfloat        scale);

/* ==========================================================================
 * Position and Motion
 * ========================================================================== */

/**
 * lrg_rigid_body_get_position:
 * @self: an #LrgRigidBody
 * @out_x: (out): X position
 * @out_y: (out): Y position
 *
 * Gets the current position.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_get_position          (LrgRigidBody *self,
                                                          gfloat       *out_x,
                                                          gfloat       *out_y);

/**
 * lrg_rigid_body_set_position:
 * @self: an #LrgRigidBody
 * @x: X position
 * @y: Y position
 *
 * Sets the position (teleports the body).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_position          (LrgRigidBody *self,
                                                          gfloat        x,
                                                          gfloat        y);

/**
 * lrg_rigid_body_get_rotation:
 * @self: an #LrgRigidBody
 *
 * Gets the current rotation in radians.
 *
 * Returns: The rotation in radians
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_rotation          (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_rotation:
 * @self: an #LrgRigidBody
 * @rotation: The rotation in radians
 *
 * Sets the rotation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_rotation          (LrgRigidBody *self,
                                                          gfloat        rotation);

/**
 * lrg_rigid_body_get_velocity:
 * @self: an #LrgRigidBody
 * @out_x: (out): X velocity
 * @out_y: (out): Y velocity
 *
 * Gets the linear velocity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_get_velocity          (LrgRigidBody *self,
                                                          gfloat       *out_x,
                                                          gfloat       *out_y);

/**
 * lrg_rigid_body_set_velocity:
 * @self: an #LrgRigidBody
 * @x: X velocity
 * @y: Y velocity
 *
 * Sets the linear velocity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_velocity          (LrgRigidBody *self,
                                                          gfloat        x,
                                                          gfloat        y);

/**
 * lrg_rigid_body_get_angular_velocity:
 * @self: an #LrgRigidBody
 *
 * Gets the angular velocity in radians per second.
 *
 * Returns: The angular velocity
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rigid_body_get_angular_velocity  (LrgRigidBody *self);

/**
 * lrg_rigid_body_set_angular_velocity:
 * @self: an #LrgRigidBody
 * @velocity: The angular velocity in radians per second
 *
 * Sets the angular velocity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_angular_velocity  (LrgRigidBody *self,
                                                          gfloat        velocity);

/* ==========================================================================
 * Forces and Impulses
 * ========================================================================== */

/**
 * lrg_rigid_body_add_force:
 * @self: an #LrgRigidBody
 * @force_x: Force X component
 * @force_y: Force Y component
 * @mode: How to apply the force
 *
 * Adds a force to the body.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_add_force             (LrgRigidBody *self,
                                                          gfloat        force_x,
                                                          gfloat        force_y,
                                                          LrgForceMode  mode);

/**
 * lrg_rigid_body_add_force_at_point:
 * @self: an #LrgRigidBody
 * @force_x: Force X component
 * @force_y: Force Y component
 * @point_x: Application point X (world space)
 * @point_y: Application point Y (world space)
 * @mode: How to apply the force
 *
 * Adds a force at a specific point (can cause rotation).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_add_force_at_point    (LrgRigidBody *self,
                                                          gfloat        force_x,
                                                          gfloat        force_y,
                                                          gfloat        point_x,
                                                          gfloat        point_y,
                                                          LrgForceMode  mode);

/**
 * lrg_rigid_body_add_torque:
 * @self: an #LrgRigidBody
 * @torque: Torque to apply
 * @mode: How to apply the torque
 *
 * Adds torque to the body.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_add_torque            (LrgRigidBody *self,
                                                          gfloat        torque,
                                                          LrgForceMode  mode);

/**
 * lrg_rigid_body_clear_forces:
 * @self: an #LrgRigidBody
 *
 * Clears all accumulated forces.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_clear_forces          (LrgRigidBody *self);

/* ==========================================================================
 * Collision Shape
 * ========================================================================== */

/**
 * lrg_rigid_body_set_box_shape:
 * @self: an #LrgRigidBody
 * @width: Box width
 * @height: Box height
 *
 * Sets the collision shape to a box.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_box_shape         (LrgRigidBody *self,
                                                          gfloat        width,
                                                          gfloat        height);

/**
 * lrg_rigid_body_set_circle_shape:
 * @self: an #LrgRigidBody
 * @radius: Circle radius
 *
 * Sets the collision shape to a circle.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_set_circle_shape      (LrgRigidBody *self,
                                                          gfloat        radius);

/**
 * lrg_rigid_body_get_shape_type:
 * @self: an #LrgRigidBody
 *
 * Gets the collision shape type.
 *
 * Returns: The shape type
 */
LRG_AVAILABLE_IN_ALL
LrgCollisionShape   lrg_rigid_body_get_shape_type        (LrgRigidBody *self);

/**
 * lrg_rigid_body_get_shape_bounds:
 * @self: an #LrgRigidBody
 * @out_width: (out): Bounding width
 * @out_height: (out): Bounding height
 *
 * Gets the axis-aligned bounding box dimensions.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_get_shape_bounds      (LrgRigidBody *self,
                                                          gfloat       *out_width,
                                                          gfloat       *out_height);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_rigid_body_is_sleeping:
 * @self: an #LrgRigidBody
 *
 * Checks if the body is sleeping (not being simulated).
 *
 * Returns: %TRUE if sleeping
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_rigid_body_is_sleeping           (LrgRigidBody *self);

/**
 * lrg_rigid_body_wake_up:
 * @self: an #LrgRigidBody
 *
 * Wakes up the body.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_wake_up               (LrgRigidBody *self);

/**
 * lrg_rigid_body_sleep:
 * @self: an #LrgRigidBody
 *
 * Puts the body to sleep.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rigid_body_sleep                 (LrgRigidBody *self);

G_END_DECLS

#endif /* LRG_RIGID_BODY_H */
