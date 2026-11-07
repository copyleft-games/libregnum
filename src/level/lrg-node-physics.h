/* lrg-node-physics.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `physics:` block of an #LrgNode: the authored description of a
 * physics body (#LrgNodePhysics), its collision shapes
 * (#LrgNodePhysicsShape) and its joints (#LrgNodePhysicsJoint).
 *
 * lrg_level_instantiate_3d() turns the block into an
 * #LrgRigidBodyComponent, one #LrgColliderComponent3D per shape and one
 * #LrgJointComponent per joint on the node's entity.  The same block is used
 * by prefabs (`.rprefab` files use the node format).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../physics/lrg-dynamics.h"
#include "../world3d/components/lrg-collider-component3d.h"
#include "../world3d/components/lrg-joint-component.h"
#include "../world3d/components/lrg-rigid-body-component.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgNodePhysicsShape
 * ========================================================================== */

#define LRG_TYPE_NODE_PHYSICS_SHAPE (lrg_node_physics_shape_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodePhysicsShape, lrg_node_physics_shape, LRG, NODE_PHYSICS_SHAPE, GObject)

/**
 * lrg_node_physics_shape_new:
 * @kind: the shape kind
 *
 * Creates a shape description with the defaults of
 * lrg_collider_component3d_new().
 *
 * Returns: (transfer full): a new #LrgNodePhysicsShape
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysicsShape * lrg_node_physics_shape_new (LrgCollider3DKind kind);

/**
 * lrg_node_physics_shape_copy:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysicsShape * lrg_node_physics_shape_copy (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_equal:
 * @a: (nullable): a shape
 * @b: (nullable): another shape
 *
 * Returns: %TRUE when both are %NULL or describe the same shape
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_shape_equal (LrgNodePhysicsShape *a,
                                       LrgNodePhysicsShape *b);

/**
 * lrg_node_physics_shape_get_kind:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: the shape kind
 */
LRG_AVAILABLE_IN_ALL
LrgCollider3DKind lrg_node_physics_shape_get_kind (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_kind:
 * @self: an #LrgNodePhysicsShape
 * @kind: the shape kind
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_kind (LrgNodePhysicsShape *self,
                                      LrgCollider3DKind    kind);

/**
 * lrg_node_physics_shape_get_half_extents:
 * @self: an #LrgNodePhysicsShape
 * @out: (out caller-allocates) (array fixed-size=3): box half extents
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_get_half_extents (LrgNodePhysicsShape *self,
                                              gfloat              *out);

/**
 * lrg_node_physics_shape_set_half_extents:
 * @self: an #LrgNodePhysicsShape
 * @hx: half extent X
 * @hy: half extent Y
 * @hz: half extent Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_half_extents (LrgNodePhysicsShape *self,
                                              gfloat               hx,
                                              gfloat               hy,
                                              gfloat               hz);

/**
 * lrg_node_physics_shape_get_radius:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: the sphere / capsule / cylinder radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_shape_get_radius (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_radius:
 * @self: an #LrgNodePhysicsShape
 * @radius: the radius
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_radius (LrgNodePhysicsShape *self,
                                        gfloat               radius);

/**
 * lrg_node_physics_shape_get_half_height:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: the capsule / cylinder half height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_shape_get_half_height (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_half_height:
 * @self: an #LrgNodePhysicsShape
 * @half_height: the half height
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_half_height (LrgNodePhysicsShape *self,
                                             gfloat               half_height);

/**
 * lrg_node_physics_shape_get_offset:
 * @self: an #LrgNodePhysicsShape
 * @out: (out caller-allocates) (array fixed-size=3): local offset
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_get_offset (LrgNodePhysicsShape *self,
                                        gfloat              *out);

/**
 * lrg_node_physics_shape_set_offset:
 * @self: an #LrgNodePhysicsShape
 * @x: local X
 * @y: local Y
 * @z: local Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_offset (LrgNodePhysicsShape *self,
                                        gfloat               x,
                                        gfloat               y,
                                        gfloat               z);

/**
 * lrg_node_physics_shape_get_rotation:
 * @self: an #LrgNodePhysicsShape
 * @out: (out caller-allocates) (array fixed-size=3): local Euler degrees
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_get_rotation (LrgNodePhysicsShape *self,
                                          gfloat              *out);

/**
 * lrg_node_physics_shape_set_rotation:
 * @self: an #LrgNodePhysicsShape
 * @x: degrees about X
 * @y: degrees about Y
 * @z: degrees about Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_rotation (LrgNodePhysicsShape *self,
                                          gfloat               x,
                                          gfloat               y,
                                          gfloat               z);

/**
 * lrg_node_physics_shape_get_trigger:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: %TRUE for a trigger shape
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_shape_get_trigger (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_trigger:
 * @self: an #LrgNodePhysicsShape
 * @trigger: trigger flag
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_trigger (LrgNodePhysicsShape *self,
                                         gboolean             trigger);

/**
 * lrg_node_physics_shape_get_friction:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: the friction override, negative for the body's
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_shape_get_friction (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_friction:
 * @self: an #LrgNodePhysicsShape
 * @friction: friction override, negative for the body's
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_friction (LrgNodePhysicsShape *self,
                                          gfloat               friction);

/**
 * lrg_node_physics_shape_get_restitution:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: the restitution override, negative for the body's
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_shape_get_restitution (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_restitution:
 * @self: an #LrgNodePhysicsShape
 * @restitution: restitution override, negative for the body's
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_restitution (LrgNodePhysicsShape *self,
                                             gfloat               restitution);

/**
 * lrg_node_physics_shape_get_layer:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: (transfer none) (nullable): the layer name, %NULL for the body's
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_physics_shape_get_layer (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_layer:
 * @self: an #LrgNodePhysicsShape
 * @layer: (nullable): the layer name, %NULL for the body's
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_layer (LrgNodePhysicsShape *self,
                                       const gchar         *layer);

/**
 * lrg_node_physics_shape_get_mesh:
 * @self: an #LrgNodePhysicsShape
 *
 * Returns: (transfer none) (nullable): the mesh source of a convex/mesh
 *   shape, %NULL for the node's own model
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_physics_shape_get_mesh (LrgNodePhysicsShape *self);

/**
 * lrg_node_physics_shape_set_mesh:
 * @self: an #LrgNodePhysicsShape
 * @mesh: (nullable): a model path or `guid:` reference, %NULL for the node's
 *   own model
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_set_mesh (LrgNodePhysicsShape *self,
                                      const gchar         *mesh);

/**
 * lrg_node_physics_shape_apply:
 * @self: an #LrgNodePhysicsShape
 * @collider: the collider to configure
 *
 * Copies the description into @collider (what instantiation does).
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_shape_apply (LrgNodePhysicsShape    *self,
                                   LrgColliderComponent3D *collider);

/* ==========================================================================
 * LrgNodePhysicsJoint
 * ========================================================================== */

#define LRG_TYPE_NODE_PHYSICS_JOINT (lrg_node_physics_joint_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodePhysicsJoint, lrg_node_physics_joint, LRG, NODE_PHYSICS_JOINT, GObject)

/**
 * lrg_node_physics_joint_new:
 * @kind: the joint type
 *
 * Creates a joint description with the defaults of
 * lrg_joint_component_new().
 *
 * Returns: (transfer full): a new #LrgNodePhysicsJoint
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysicsJoint * lrg_node_physics_joint_new (LrgJointKind kind);

/**
 * lrg_node_physics_joint_copy:
 * @self: an #LrgNodePhysicsJoint
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysicsJoint * lrg_node_physics_joint_copy (LrgNodePhysicsJoint *self);

/**
 * lrg_node_physics_joint_equal:
 * @a: (nullable): a joint
 * @b: (nullable): another joint
 *
 * Returns: %TRUE when both are %NULL or describe the same joint
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_joint_equal (LrgNodePhysicsJoint *a,
                                       LrgNodePhysicsJoint *b);

/**
 * lrg_node_physics_joint_get_kind:
 * @self: an #LrgNodePhysicsJoint
 *
 * Returns: the joint type
 */
LRG_AVAILABLE_IN_ALL
LrgJointKind lrg_node_physics_joint_get_kind (LrgNodePhysicsJoint *self);

/**
 * lrg_node_physics_joint_set_kind:
 * @self: an #LrgNodePhysicsJoint
 * @kind: the joint type
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_kind (LrgNodePhysicsJoint   *self,
                                      LrgJointKind  kind);

/**
 * lrg_node_physics_joint_get_target:
 * @self: an #LrgNodePhysicsJoint
 *
 * Returns: (transfer none) (nullable): GUID of the target node, %NULL for
 *   the world
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_physics_joint_get_target (LrgNodePhysicsJoint *self);

/**
 * lrg_node_physics_joint_set_target:
 * @self: an #LrgNodePhysicsJoint
 * @guid: (nullable): GUID of the target node, %NULL for the world
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_target (LrgNodePhysicsJoint *self,
                                        const gchar         *guid);

/**
 * lrg_node_physics_joint_get_anchor:
 * @self: an #LrgNodePhysicsJoint
 * @out: (out caller-allocates) (array fixed-size=3): local anchor
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_get_anchor (LrgNodePhysicsJoint *self,
                                        gfloat              *out);

/**
 * lrg_node_physics_joint_set_anchor:
 * @self: an #LrgNodePhysicsJoint
 * @x: local X
 * @y: local Y
 * @z: local Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_anchor (LrgNodePhysicsJoint *self,
                                        gfloat               x,
                                        gfloat               y,
                                        gfloat               z);

/**
 * lrg_node_physics_joint_get_axis:
 * @self: an #LrgNodePhysicsJoint
 * @out: (out caller-allocates) (array fixed-size=3): local axis
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_get_axis (LrgNodePhysicsJoint *self,
                                      gfloat              *out);

/**
 * lrg_node_physics_joint_set_axis:
 * @self: an #LrgNodePhysicsJoint
 * @x: local X
 * @y: local Y
 * @z: local Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_axis (LrgNodePhysicsJoint *self,
                                      gfloat               x,
                                      gfloat               y,
                                      gfloat               z);

/**
 * lrg_node_physics_joint_get_limits:
 * @self: an #LrgNodePhysicsJoint
 * @min: (out) (optional): lower limit
 * @max: (out) (optional): upper limit
 *
 * Returns: %TRUE when limits are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_joint_get_limits (LrgNodePhysicsJoint *self,
                                            gfloat              *min,
                                            gfloat              *max);

/**
 * lrg_node_physics_joint_set_limits:
 * @self: an #LrgNodePhysicsJoint
 * @enabled: whether limits apply
 * @min: lower limit
 * @max: upper limit
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_limits (LrgNodePhysicsJoint *self,
                                        gboolean             enabled,
                                        gfloat               min,
                                        gfloat               max);

/**
 * lrg_node_physics_joint_get_spring:
 * @self: an #LrgNodePhysicsJoint
 * @frequency: (out) (optional): Hz, 0 for rigid
 * @damping: (out) (optional): damping ratio
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_get_spring (LrgNodePhysicsJoint *self,
                                        gfloat              *frequency,
                                        gfloat              *damping);

/**
 * lrg_node_physics_joint_set_spring:
 * @self: an #LrgNodePhysicsJoint
 * @frequency: Hz, 0 for rigid
 * @damping: damping ratio
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_spring (LrgNodePhysicsJoint *self,
                                        gfloat               frequency,
                                        gfloat               damping);

/**
 * lrg_node_physics_joint_get_motor:
 * @self: an #LrgNodePhysicsJoint
 * @velocity: (out) (optional): target velocity
 * @max_force: (out) (optional): max force, 0 when off
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_get_motor (LrgNodePhysicsJoint *self,
                                       gfloat              *velocity,
                                       gfloat              *max_force);

/**
 * lrg_node_physics_joint_set_motor:
 * @self: an #LrgNodePhysicsJoint
 * @velocity: target velocity
 * @max_force: max force, 0 for off
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_motor (LrgNodePhysicsJoint *self,
                                       gfloat               velocity,
                                       gfloat               max_force);

/**
 * lrg_node_physics_joint_get_break_force:
 * @self: an #LrgNodePhysicsJoint
 *
 * Returns: the break force, 0 for unbreakable
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_joint_get_break_force (LrgNodePhysicsJoint *self);

/**
 * lrg_node_physics_joint_set_break_force:
 * @self: an #LrgNodePhysicsJoint
 * @force: break force, 0 for unbreakable
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_set_break_force (LrgNodePhysicsJoint *self,
                                             gfloat               force);

/**
 * lrg_node_physics_joint_apply:
 * @self: an #LrgNodePhysicsJoint
 * @joint: the joint component to configure
 *
 * Copies the description into @joint (what instantiation does).
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_joint_apply (LrgNodePhysicsJoint *self,
                                   LrgJointComponent   *joint);

/* ==========================================================================
 * LrgNodePhysics
 * ========================================================================== */

#define LRG_TYPE_NODE_PHYSICS (lrg_node_physics_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodePhysics, lrg_node_physics, LRG, NODE_PHYSICS, GObject)

/**
 * lrg_node_physics_new:
 * @kind: the body kind
 *
 * Creates a body description with the defaults of
 * lrg_rigid_body_component_new() and no shapes or joints.
 *
 * Returns: (transfer full): a new #LrgNodePhysics
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysics * lrg_node_physics_new (LrgDynamicsBodyKind kind);

/**
 * lrg_node_physics_copy:
 * @self: an #LrgNodePhysics
 *
 * Returns: (transfer full): a deep copy (shapes and joints copied too)
 */
LRG_AVAILABLE_IN_ALL
LrgNodePhysics * lrg_node_physics_copy (LrgNodePhysics *self);

/**
 * lrg_node_physics_equal:
 * @a: (nullable): a description
 * @b: (nullable): another description
 *
 * Returns: %TRUE when both are %NULL or equal field by field (shapes and
 *   joints in order)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_equal (LrgNodePhysics *a,
                                 LrgNodePhysics *b);

/**
 * lrg_node_physics_get_body_kind:
 * @self: an #LrgNodePhysics
 *
 * Returns: the body kind
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsBodyKind lrg_node_physics_get_body_kind (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_body_kind:
 * @self: an #LrgNodePhysics
 * @kind: the body kind
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_body_kind (LrgNodePhysics      *self,
                                     LrgDynamicsBodyKind  kind);

/**
 * lrg_node_physics_get_mass:
 * @self: an #LrgNodePhysics
 *
 * Returns: the mass
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_get_mass (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_mass:
 * @self: an #LrgNodePhysics
 * @mass: the mass (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_mass (LrgNodePhysics *self,
                                gfloat          mass);

/**
 * lrg_node_physics_get_layer:
 * @self: an #LrgNodePhysics
 *
 * Returns: (transfer none) (nullable): the layer name, %NULL for the default
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_physics_get_layer (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_layer:
 * @self: an #LrgNodePhysics
 * @layer: (nullable): the layer name, %NULL for the default
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_layer (LrgNodePhysics *self,
                                 const gchar    *layer);

/**
 * lrg_node_physics_get_friction:
 * @self: an #LrgNodePhysics
 *
 * Returns: the friction
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_get_friction (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_friction:
 * @self: an #LrgNodePhysics
 * @friction: the friction
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_friction (LrgNodePhysics *self,
                                    gfloat          friction);

/**
 * lrg_node_physics_get_restitution:
 * @self: an #LrgNodePhysics
 *
 * Returns: the restitution
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_get_restitution (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_restitution:
 * @self: an #LrgNodePhysics
 * @restitution: the restitution
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_restitution (LrgNodePhysics *self,
                                       gfloat          restitution);

/**
 * lrg_node_physics_get_damping:
 * @self: an #LrgNodePhysics
 * @linear: (out) (optional): linear damping
 * @angular: (out) (optional): angular damping
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_get_damping (LrgNodePhysics *self,
                                   gfloat         *linear,
                                   gfloat         *angular);

/**
 * lrg_node_physics_set_damping:
 * @self: an #LrgNodePhysics
 * @linear: linear damping
 * @angular: angular damping
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_damping (LrgNodePhysics *self,
                                   gfloat          linear,
                                   gfloat          angular);

/**
 * lrg_node_physics_get_gravity_factor:
 * @self: an #LrgNodePhysics
 *
 * Returns: the gravity factor
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_node_physics_get_gravity_factor (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_gravity_factor:
 * @self: an #LrgNodePhysics
 * @factor: the gravity factor
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_gravity_factor (LrgNodePhysics *self,
                                          gfloat          factor);

/**
 * lrg_node_physics_get_ccd:
 * @self: an #LrgNodePhysics
 *
 * Returns: whether CCD is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_get_ccd (LrgNodePhysics *self);

/**
 * lrg_node_physics_set_ccd:
 * @self: an #LrgNodePhysics
 * @ccd: whether CCD is on
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_set_ccd (LrgNodePhysics *self,
                               gboolean        ccd);

/**
 * lrg_node_physics_get_shapes:
 * @self: an #LrgNodePhysics
 *
 * Returns: (transfer none) (element-type LrgNodePhysicsShape): the shapes,
 *   in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_node_physics_get_shapes (LrgNodePhysics *self);

/**
 * lrg_node_physics_add_shape:
 * @self: an #LrgNodePhysics
 * @shape: (transfer none): a shape to append (a reference is taken)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_add_shape (LrgNodePhysics      *self,
                                 LrgNodePhysicsShape *shape);

/**
 * lrg_node_physics_remove_shape_at:
 * @self: an #LrgNodePhysics
 * @index: shape index
 *
 * Returns: %TRUE when a shape was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_remove_shape_at (LrgNodePhysics *self,
                                           guint           index);

/**
 * lrg_node_physics_get_joints:
 * @self: an #LrgNodePhysics
 *
 * Returns: (transfer none) (element-type LrgNodePhysicsJoint): the joints,
 *   in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_node_physics_get_joints (LrgNodePhysics *self);

/**
 * lrg_node_physics_add_joint:
 * @self: an #LrgNodePhysics
 * @joint: (transfer none): a joint to append (a reference is taken)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_add_joint (LrgNodePhysics      *self,
                                 LrgNodePhysicsJoint *joint);

/**
 * lrg_node_physics_remove_joint_at:
 * @self: an #LrgNodePhysics
 * @index: joint index
 *
 * Returns: %TRUE when a joint was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_physics_remove_joint_at (LrgNodePhysics *self,
                                           guint           index);

/**
 * lrg_node_physics_apply:
 * @self: an #LrgNodePhysics
 * @body: the rigid body component to configure
 *
 * Copies the body settings (not the shapes or joints) into @body.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_physics_apply (LrgNodePhysics        *self,
                             LrgRigidBodyComponent *body);

G_END_DECLS
