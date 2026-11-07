/* lrg-rigid-body-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRigidBodyComponent: makes its #LrgEntity3D a body of the world's
 * #LrgDynamicsWorld.
 *
 * The body's shape is made of the non-trigger #LrgColliderComponent3D
 * components on the same entity (one shape, or a compound of all of them).
 * #LrgWorld3D keeps body and entity in step every fixed update:
 *
 * - static and kinematic bodies follow the entity (a kinematic body is
 *   moved to the entity's new pose with the velocity that gets it there in
 *   one step, so it pushes dynamic bodies);
 * - dynamic bodies drive the entity (and an entity moved by game code is
 *   teleported: its body takes the new pose);
 * - with #LrgRigidBodyComponent:interpolate the rendered transform is
 *   blended between the last two physics states by
 *   lrg_world3d_get_fixed_alpha().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../physics/lrg-dynamics.h"

G_BEGIN_DECLS

#define LRG_TYPE_RIGID_BODY_COMPONENT (lrg_rigid_body_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRigidBodyComponent, lrg_rigid_body_component, LRG, RIGID_BODY_COMPONENT, LrgComponent)

/**
 * lrg_rigid_body_component_new:
 * @kind: static, kinematic or dynamic
 *
 * Creates a body of mass 1, friction 0.5, restitution 0, damping 0.05,
 * gravity factor 1, the default layer, no CCD, interpolated.
 *
 * Returns: (transfer full): a new #LrgRigidBodyComponent
 */
LRG_AVAILABLE_IN_ALL
LrgRigidBodyComponent * lrg_rigid_body_component_new (LrgDynamicsBodyKind kind);

/**
 * lrg_rigid_body_component_get_body_kind:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the body kind
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsBodyKind lrg_rigid_body_component_get_body_kind (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_body_kind:
 * @self: an #LrgRigidBodyComponent
 * @kind: the new kind; the body is rebuilt on the next fixed update
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_body_kind (LrgRigidBodyComponent *self,
                                             LrgDynamicsBodyKind    kind);

/**
 * lrg_rigid_body_component_get_mass:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the mass in kg (dynamic bodies)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rigid_body_component_get_mass (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_mass:
 * @self: an #LrgRigidBodyComponent
 * @mass: mass in kg (> 0); the inertia comes from the shapes, scaled to it
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_mass (LrgRigidBodyComponent *self,
                                        gfloat                 mass);

/**
 * lrg_rigid_body_component_get_friction:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the body's friction coefficient
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rigid_body_component_get_friction (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_friction:
 * @self: an #LrgRigidBodyComponent
 * @friction: friction coefficient (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_friction (LrgRigidBodyComponent *self,
                                            gfloat                 friction);

/**
 * lrg_rigid_body_component_get_restitution:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the body's restitution
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rigid_body_component_get_restitution (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_restitution:
 * @self: an #LrgRigidBodyComponent
 * @restitution: bounciness 0..1
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_restitution (LrgRigidBodyComponent *self,
                                               gfloat                 restitution);

/**
 * lrg_rigid_body_component_set_damping:
 * @self: an #LrgRigidBodyComponent
 * @linear: linear damping (>= 0, per second)
 * @angular: angular damping (>= 0, per second)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_damping (LrgRigidBodyComponent *self,
                                           gfloat                 linear,
                                           gfloat                 angular);

/**
 * lrg_rigid_body_component_get_damping:
 * @self: an #LrgRigidBodyComponent
 * @linear: (out) (optional): linear damping
 * @angular: (out) (optional): angular damping
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_get_damping (LrgRigidBodyComponent *self,
                                           gfloat                *linear,
                                           gfloat                *angular);

/**
 * lrg_rigid_body_component_get_gravity_factor:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the multiplier on the world gravity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_rigid_body_component_get_gravity_factor (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_gravity_factor:
 * @self: an #LrgRigidBodyComponent
 * @factor: multiplier on the world gravity (0 floats)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_gravity_factor (LrgRigidBodyComponent *self,
                                                  gfloat                 factor);

/**
 * lrg_rigid_body_component_get_ccd:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: %TRUE when continuous collision detection is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rigid_body_component_get_ccd (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_ccd:
 * @self: an #LrgRigidBodyComponent
 * @ccd: sweep the body between steps so fast bodies cannot tunnel
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_ccd (LrgRigidBodyComponent *self,
                                       gboolean               ccd);

/**
 * lrg_rigid_body_component_get_layer:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: (transfer none) (nullable): the collision layer name, %NULL for
 *   the default layer
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_rigid_body_component_get_layer (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_layer:
 * @self: an #LrgRigidBodyComponent
 * @layer: (nullable): a layer name of the physics world's layer table
 *   (unknown names fall back to the default layer with a warning), %NULL for
 *   the default layer
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_layer (LrgRigidBodyComponent *self,
                                         const gchar           *layer);

/**
 * lrg_rigid_body_component_get_interpolate:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: %TRUE when the rendered transform is interpolated
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rigid_body_component_get_interpolate (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_set_interpolate:
 * @self: an #LrgRigidBodyComponent
 * @interpolate: blend the entity between the last two physics states
 *   (dynamic bodies; needs #LrgWorld3D:physics-interpolation too)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_interpolate (LrgRigidBodyComponent *self,
                                               gboolean               interpolate);

/**
 * lrg_rigid_body_component_get_body_id:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: the body id in the physics world, %LRG_PHYSICS_NO_BODY while
 *   there is no body
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rigid_body_component_get_body_id (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_get_physics:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: (transfer none) (nullable): the physics world the body lives in
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld * lrg_rigid_body_component_get_physics (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_rebuild:
 * @self: an #LrgRigidBodyComponent
 *
 * Marks the body for rebuilding from the current colliders and settings on
 * the next fixed update (done automatically when colliders are added,
 * removed or changed).  Velocities are kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_rebuild (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_get_linear_velocity:
 * @self: an #LrgRigidBodyComponent
 * @out: (out caller-allocates) (array fixed-size=3): the velocity, zero
 *   without a body
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_get_linear_velocity (LrgRigidBodyComponent *self,
                                                   gfloat                *out);

/**
 * lrg_rigid_body_component_set_linear_velocity:
 * @self: an #LrgRigidBodyComponent
 * @velocity: (array fixed-size=3): the new velocity (wakes the body);
 *   remembered and applied when the body is created
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_linear_velocity (LrgRigidBodyComponent *self,
                                                   const gfloat          *velocity);

/**
 * lrg_rigid_body_component_get_angular_velocity:
 * @self: an #LrgRigidBodyComponent
 * @out: (out caller-allocates) (array fixed-size=3): the angular velocity
 *   (rad/s, world axes)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_get_angular_velocity (LrgRigidBodyComponent *self,
                                                    gfloat                *out);

/**
 * lrg_rigid_body_component_set_angular_velocity:
 * @self: an #LrgRigidBodyComponent
 * @velocity: (array fixed-size=3): the new angular velocity (rad/s)
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_set_angular_velocity (LrgRigidBodyComponent *self,
                                                    const gfloat          *velocity);

/**
 * lrg_rigid_body_component_apply_force:
 * @self: an #LrgRigidBodyComponent
 * @force: (array fixed-size=3): world force in newtons, for the next step
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_apply_force (LrgRigidBodyComponent *self,
                                           const gfloat          *force);

/**
 * lrg_rigid_body_component_apply_impulse:
 * @self: an #LrgRigidBodyComponent
 * @impulse: (array fixed-size=3): world impulse (N s)
 * @point: (array fixed-size=3) (nullable): world point it acts at, %NULL for
 *   the centre of mass
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_apply_impulse (LrgRigidBodyComponent *self,
                                             const gfloat          *impulse,
                                             const gfloat          *point);

/**
 * lrg_rigid_body_component_apply_torque:
 * @self: an #LrgRigidBodyComponent
 * @torque: (array fixed-size=3): world torque (N m), for the next step
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_apply_torque (LrgRigidBodyComponent *self,
                                            const gfloat          *torque);

/**
 * lrg_rigid_body_component_wake:
 * @self: an #LrgRigidBodyComponent
 *
 * Wakes a sleeping body.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rigid_body_component_wake (LrgRigidBodyComponent *self);

/**
 * lrg_rigid_body_component_is_sleeping:
 * @self: an #LrgRigidBodyComponent
 *
 * Returns: %TRUE when the body is asleep (or has no body)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rigid_body_component_is_sleeping (LrgRigidBodyComponent *self);

G_END_DECLS
