/* lrg-joint-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJointComponent: a physics joint between the rigid body of its entity
 * and the rigid body of a target entity (or the fixed world).
 *
 * The joint frame is authored in the entity's local space (an anchor point
 * and an axis) and turned into an #LrgJoint when both bodies exist; the
 * target may be given as an entity or as a GUID that is resolved lazily, so
 * levels may list joints to nodes that are instantiated later.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../physics/lrg-joint.h"

G_BEGIN_DECLS

#define LRG_TYPE_JOINT_COMPONENT (lrg_joint_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgJointComponent, lrg_joint_component, LRG, JOINT_COMPONENT, LrgComponent)

/**
 * lrg_joint_component_new:
 * @kind: the joint type
 *
 * Creates a joint to the fixed world (no target) with its anchor at the
 * entity origin, its axis local +Y, no limits, no motor and no break force.
 *
 * How the generic settings map onto each #LrgJointKind:
 *
 * - limits: hinge angle (radians) about the axis, slider offset along the
 *   axis, distance-joint range (min, max), cone half angle (max only),
 *   swing-twist: swing half angle = max and twist range = -|min|..|min|,
 *   6-DOF: ignored (configure the #LrgJoint);
 * - spring: soft limits (the distance spring / damper on distance joints);
 * - motor: velocity motor on the hinge or slider axis.
 *
 * Returns: (transfer full): a new #LrgJointComponent
 */
LRG_AVAILABLE_IN_ALL
LrgJointComponent * lrg_joint_component_new (LrgJointKind kind);

/**
 * lrg_joint_component_get_kind:
 * @self: an #LrgJointComponent
 *
 * Returns: the joint type
 */
LRG_AVAILABLE_IN_ALL
LrgJointKind lrg_joint_component_get_kind (LrgJointComponent *self);

/**
 * lrg_joint_component_set_kind:
 * @self: an #LrgJointComponent
 * @kind: the joint kind; an existing joint is rebuilt
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_kind (LrgJointComponent     *self,
                                   LrgJointKind  kind);

/**
 * lrg_joint_component_get_target:
 * @self: an #LrgJointComponent
 *
 * Returns: (transfer none) (nullable): the target entity once resolved, %NULL
 *   for the world (or a GUID not resolved yet)
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_joint_component_get_target (LrgJointComponent *self);

/**
 * lrg_joint_component_set_target:
 * @self: an #LrgJointComponent
 * @target: (nullable): the other entity (it needs an
 *   #LrgRigidBodyComponent), %NULL to pin to the world
 *
 * Also sets #LrgJointComponent:target-guid to the target's GUID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_target (LrgJointComponent *self,
                                     LrgEntity3D       *target);

/**
 * lrg_joint_component_get_target_guid:
 * @self: an #LrgJointComponent
 *
 * Returns: (transfer none) (nullable): the target's GUID, %NULL for the world
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_joint_component_get_target_guid (LrgJointComponent *self);

/**
 * lrg_joint_component_set_target_guid:
 * @self: an #LrgJointComponent
 * @guid: (nullable): GUID of the target entity, looked up in the world when
 *   the joint is built (each fixed update until found), %NULL for the world
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_target_guid (LrgJointComponent *self,
                                          const gchar       *guid);

/**
 * lrg_joint_component_get_anchor:
 * @self: an #LrgJointComponent
 * @out: (out caller-allocates) (array fixed-size=3): the anchor in the
 *   entity's local space
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_get_anchor (LrgJointComponent *self,
                                     gfloat            *out);

/**
 * lrg_joint_component_set_anchor:
 * @self: an #LrgJointComponent
 * @x: local X
 * @y: local Y
 * @z: local Z
 *
 * The joint pivot in the entity's local space.  The target's anchor is the
 * same world point at the moment the joint is built.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_anchor (LrgJointComponent *self,
                                     gfloat             x,
                                     gfloat             y,
                                     gfloat             z);

/**
 * lrg_joint_component_get_axis:
 * @self: an #LrgJointComponent
 * @out: (out caller-allocates) (array fixed-size=3): the unit axis in the
 *   entity's local space
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_get_axis (LrgJointComponent *self,
                                   gfloat            *out);

/**
 * lrg_joint_component_set_axis:
 * @self: an #LrgJointComponent
 * @x: local X
 * @y: local Y
 * @z: local Z
 *
 * Hinge / slider / cone axis in the entity's local space (normalised; a zero
 * vector is ignored).
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_axis (LrgJointComponent *self,
                                   gfloat             x,
                                   gfloat             y,
                                   gfloat             z);

/**
 * lrg_joint_component_get_limits:
 * @self: an #LrgJointComponent
 * @min: (out) (optional): lower limit
 * @max: (out) (optional): upper limit
 *
 * Returns: %TRUE when limits are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_joint_component_get_limits (LrgJointComponent *self,
                                         gfloat            *min,
                                         gfloat            *max);

/**
 * lrg_joint_component_set_limits:
 * @self: an #LrgJointComponent
 * @enabled: whether the limits apply
 * @min: lower limit (radians for hinges, distance for sliders and distance
 *   joints)
 * @max: upper limit (the cone half angle for cone joints)
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_limits (LrgJointComponent *self,
                                     gboolean           enabled,
                                     gfloat             min,
                                     gfloat             max);

/**
 * lrg_joint_component_set_spring:
 * @self: an #LrgJointComponent
 * @frequency: spring frequency in Hz, 0 for rigid
 * @damping: damping ratio (1 = critical)
 *
 * Makes the limits (or a distance joint) soft.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_spring (LrgJointComponent *self,
                                     gfloat             frequency,
                                     gfloat             damping);

/**
 * lrg_joint_component_get_spring:
 * @self: an #LrgJointComponent
 * @frequency: (out) (optional): spring frequency in Hz
 * @damping: (out) (optional): damping ratio
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_get_spring (LrgJointComponent *self,
                                     gfloat            *frequency,
                                     gfloat            *damping);

/**
 * lrg_joint_component_set_motor:
 * @self: an #LrgJointComponent
 * @velocity: target velocity (rad/s for hinges, m/s for sliders)
 * @max_force: maximum force / torque, 0 turns the motor off
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_motor (LrgJointComponent *self,
                                    gfloat             velocity,
                                    gfloat             max_force);

/**
 * lrg_joint_component_get_motor:
 * @self: an #LrgJointComponent
 * @velocity: (out) (optional): target velocity
 * @max_force: (out) (optional): maximum force, 0 when off
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_get_motor (LrgJointComponent *self,
                                    gfloat            *velocity,
                                    gfloat            *max_force);

/**
 * lrg_joint_component_get_break_force:
 * @self: an #LrgJointComponent
 *
 * Returns: the impulse-per-second above which the joint breaks, 0 for never
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_component_get_break_force (LrgJointComponent *self);

/**
 * lrg_joint_component_set_break_force:
 * @self: an #LrgJointComponent
 * @force: break threshold (force units), 0 for unbreakable
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_set_break_force (LrgJointComponent *self,
                                          gfloat             force);

/**
 * lrg_joint_component_get_joint:
 * @self: an #LrgJointComponent
 *
 * Returns: (transfer none) (nullable): the live joint, %NULL until both
 *   bodies exist (or after it broke)
 */
LRG_AVAILABLE_IN_ALL
LrgJoint * lrg_joint_component_get_joint (LrgJointComponent *self);

/**
 * lrg_joint_component_get_broken:
 * @self: an #LrgJointComponent
 *
 * Returns: %TRUE once the joint exceeded its break force (it stays broken
 *   until lrg_joint_component_rebuild())
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_joint_component_get_broken (LrgJointComponent *self);

/**
 * lrg_joint_component_rebuild:
 * @self: an #LrgJointComponent
 *
 * Drops the live joint (and the broken state); it is built again on the
 * next fixed update from the bodies' poses at that time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_component_rebuild (LrgJointComponent *self);

G_END_DECLS
