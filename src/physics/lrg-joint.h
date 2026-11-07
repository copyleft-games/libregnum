/* lrg-joint.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJoint: a two-body constraint for LrgDynamicsWorld. Fixed, point
 * (ball), hinge, slider, distance (with spring/damper), cone, swing-twist
 * and six-degree-of-freedom joints share one implementation: every kind
 * except distance is a preset of the 6-DOF joint, whose three translation
 * axes and three rotation axes are each free, locked or limited, may
 * carry a soft (spring) limit, a friction and a velocity or position
 * motor. A joint may break when the force or torque it applies exceeds a
 * threshold and then emits LrgJoint::broken.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-dynamics.h"
#include "lrg-contact-solver.h"

G_BEGIN_DECLS

/**
 * LRG_JOINT_ERROR:
 *
 * Error domain for #LrgJoint. Errors carry an #LrgJointError code.
 */
#define LRG_JOINT_ERROR (lrg_joint_error_quark ())

/**
 * LrgJointError:
 * @LRG_JOINT_ERROR_INVALID: an argument is non-finite, zero where a
 *   direction is needed, or out of range
 * @LRG_JOINT_ERROR_BODY: a body id is unknown to the world or both ends
 *   name the same body
 *
 * Error codes for the %LRG_JOINT_ERROR domain.
 */
typedef enum {
	LRG_JOINT_ERROR_INVALID = 1,
	LRG_JOINT_ERROR_BODY = 2
} LrgJointError;

/**
 * LrgJointKind:
 * @LRG_JOINT_FIXED: all six axes locked: the bodies move as one
 * @LRG_JOINT_POINT: a ball joint: translation locked, rotation free
 * @LRG_JOINT_HINGE: translation locked, rotation only about the frame X
 *   axis (the hinge axis), optionally limited
 * @LRG_JOINT_SLIDER: rotation locked, translation only along the frame X
 *   axis (the slider axis), optionally limited
 * @LRG_JOINT_DISTANCE: keeps the two anchors between a minimum and a
 *   maximum distance; with a limit spring it is a spring/damper
 * @LRG_JOINT_CONE: translation locked, the frame X axis of body B stays
 *   inside a cone around the X axis of body A, twist free
 * @LRG_JOINT_SWING_TWIST: translation locked, swing limited by a cone
 *   (or pyramid) and twist about X limited: the ragdoll joint
 * @LRG_JOINT_SIX_DOF: every axis set separately; starts all free
 *
 * The joint kinds. All kinds except %LRG_JOINT_DISTANCE are presets of
 * the 6-DOF joint and accept every axis call; the kind only sets the
 * initial axis modes.
 */
typedef enum {
	LRG_JOINT_FIXED = 0,
	LRG_JOINT_POINT = 1,
	LRG_JOINT_HINGE = 2,
	LRG_JOINT_SLIDER = 3,
	LRG_JOINT_DISTANCE = 4,
	LRG_JOINT_CONE = 5,
	LRG_JOINT_SWING_TWIST = 6,
	LRG_JOINT_SIX_DOF = 7
} LrgJointKind;

/**
 * LrgJointAxis:
 * @LRG_JOINT_AXIS_TRANSLATION_X: translation along the frame X axis
 * @LRG_JOINT_AXIS_TRANSLATION_Y: translation along the frame Y axis
 * @LRG_JOINT_AXIS_TRANSLATION_Z: translation along the frame Z axis
 * @LRG_JOINT_AXIS_ROTATION_X: twist about the frame X axis
 * @LRG_JOINT_AXIS_ROTATION_Y: swing about the frame Y axis
 * @LRG_JOINT_AXIS_ROTATION_Z: swing about the frame Z axis
 *
 * One degree of freedom of a joint, measured in the joint frame of body
 * A. Rotation is split into swing (Y, Z) and twist (X).
 */
typedef enum {
	LRG_JOINT_AXIS_TRANSLATION_X = 0,
	LRG_JOINT_AXIS_TRANSLATION_Y = 1,
	LRG_JOINT_AXIS_TRANSLATION_Z = 2,
	LRG_JOINT_AXIS_ROTATION_X = 3,
	LRG_JOINT_AXIS_ROTATION_Y = 4,
	LRG_JOINT_AXIS_ROTATION_Z = 5
} LrgJointAxis;

/**
 * LrgJointAxisMode:
 * @LRG_JOINT_AXIS_FREE: the axis moves freely
 * @LRG_JOINT_AXIS_LOCKED: the axis is held at its minimum limit (zero
 *   unless lrg_joint_set_limits() said otherwise)
 * @LRG_JOINT_AXIS_LIMITED: the axis moves between its limits
 *
 * How one axis of a joint is constrained.
 */
typedef enum {
	LRG_JOINT_AXIS_FREE = 0,
	LRG_JOINT_AXIS_LOCKED = 1,
	LRG_JOINT_AXIS_LIMITED = 2
} LrgJointAxisMode;

/**
 * LrgJointMotorMode:
 * @LRG_JOINT_MOTOR_OFF: no motor
 * @LRG_JOINT_MOTOR_VELOCITY: drive the axis at a target velocity (m/s or
 *   rad/s) with at most the motor's force or torque
 * @LRG_JOINT_MOTOR_POSITION: drive the axis toward a target position (m
 *   or rad) with a spring of the motor's frequency and damping, capped
 *   by the motor's force or torque
 *
 * What a motor on one axis does.
 */
typedef enum {
	LRG_JOINT_MOTOR_OFF = 0,
	LRG_JOINT_MOTOR_VELOCITY = 1,
	LRG_JOINT_MOTOR_POSITION = 2
} LrgJointMotorMode;

/**
 * LrgJointSwingType:
 * @LRG_JOINT_SWING_CONE: swing Y and Z share an elliptic cone; limits are
 *   symmetric (the maximum is used, the minimum mirrors it)
 * @LRG_JOINT_SWING_PYRAMID: swing Y and Z are limited independently and
 *   may be asymmetric
 *
 * The shape of the swing limit when both swing axes are limited.
 */
typedef enum {
	LRG_JOINT_SWING_CONE = 0,
	LRG_JOINT_SWING_PYRAMID = 1
} LrgJointSwingType;

LRG_AVAILABLE_IN_ALL
GQuark lrg_joint_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_joint_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOINT_KIND (lrg_joint_kind_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_joint_axis_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOINT_AXIS (lrg_joint_axis_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_joint_axis_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOINT_AXIS_MODE (lrg_joint_axis_mode_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_joint_motor_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOINT_MOTOR_MODE (lrg_joint_motor_mode_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_joint_swing_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_JOINT_SWING_TYPE (lrg_joint_swing_type_get_type ())

#define LRG_TYPE_JOINT (lrg_joint_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgJoint, lrg_joint, LRG, JOINT, LrgPhysicsConstraint)

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/**
 * lrg_joint_new:
 * @kind: the joint kind; sets the initial axis modes
 * @body_a: the first body (the "parent")
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY for the
 *   immovable world frame
 * @position_a: (array fixed-size=3): joint origin in body A's local frame
 * @rotation_a: (array fixed-size=4) (nullable): xyzw rotation from the
 *   joint frame to body A's local frame, or %NULL for identity
 * @position_b: (array fixed-size=3): joint origin in body B's local
 *   frame (world space when @body_b is the world)
 * @rotation_b: (array fixed-size=4) (nullable): xyzw rotation from the
 *   joint frame to body B's local frame (or world), or %NULL for identity
 *
 * Creates a joint from frames already expressed in the bodies' local
 * spaces: the form a physics asset or an editor stores. The joint is at
 * rest (every axis at zero) when the two frames coincide in world space.
 * Rotations are normalised. Add it to a world with
 * lrg_dynamics_world_add_constraint().
 *
 * Returns: (transfer full): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new (LrgJointKind  kind,
                         guint         body_a,
                         guint         body_b,
                         const gfloat *position_a,
                         const gfloat *rotation_a,
                         const gfloat *position_b,
                         const gfloat *rotation_b);

/**
 * lrg_joint_new_in_world:
 * @world: the world that holds both bodies
 * @kind: the joint kind
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space joint origin
 * @axis_x: (array fixed-size=3): world-space frame X axis (hinge axis,
 *   slider axis, twist axis); normalised
 * @axis_y: (array fixed-size=3) (nullable): world-space frame Y axis
 *   (first swing axis); made perpendicular to @axis_x. %NULL picks any
 *   perpendicular
 * @error: (nullable): return location for a #GError
 *
 * Creates a joint whose frame is given once in world space and stored
 * in each body's local frame from the bodies' current poses. The joint
 * is at rest in the current configuration.
 *
 * Returns: (transfer full) (nullable): a new joint, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_in_world (LrgDynamicsWorld  *world,
                                  LrgJointKind       kind,
                                  guint              body_a,
                                  guint              body_b,
                                  const gfloat      *anchor,
                                  const gfloat      *axis_x,
                                  const gfloat      *axis_y,
                                  GError           **error);

/**
 * lrg_joint_new_fixed:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @error: (nullable): return location for a #GError
 *
 * Welds the bodies in their current relative pose. The joint origin is
 * the midpoint of the two body centres.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_fixed (LrgDynamicsWorld  *world,
                               guint              body_a,
                               guint              body_b,
                               GError           **error);

/**
 * lrg_joint_new_point:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space pivot
 * @error: (nullable): return location for a #GError
 *
 * A ball joint: the two bodies share @anchor and rotate freely.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_point (LrgDynamicsWorld  *world,
                               guint              body_a,
                               guint              body_b,
                               const gfloat      *anchor,
                               GError           **error);

/**
 * lrg_joint_new_hinge:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space pivot
 * @axis: (array fixed-size=3): world-space hinge axis
 * @error: (nullable): return location for a #GError
 *
 * A hinge about @axis through @anchor, free until
 * lrg_joint_set_limits() is called on %LRG_JOINT_AXIS_ROTATION_X. The
 * hinge angle is positive for a right-handed rotation of body B about
 * @axis relative to body A.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_hinge (LrgDynamicsWorld  *world,
                               guint              body_a,
                               guint              body_b,
                               const gfloat      *anchor,
                               const gfloat      *axis,
                               GError           **error);

/**
 * lrg_joint_new_slider:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space origin of the slide
 * @axis: (array fixed-size=3): world-space slide direction
 * @error: (nullable): return location for a #GError
 *
 * A prismatic joint along @axis, free until lrg_joint_set_limits() is
 * called on %LRG_JOINT_AXIS_TRANSLATION_X. The position is the offset of
 * body B's frame from body A's frame along the axis.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_slider (LrgDynamicsWorld  *world,
                                guint              body_a,
                                guint              body_b,
                                const gfloat      *anchor,
                                const gfloat      *axis,
                                GError           **error);

/**
 * lrg_joint_new_distance:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor_a: (array fixed-size=3): world-space anchor on body A
 * @anchor_b: (array fixed-size=3): world-space anchor on body B
 * @error: (nullable): return location for a #GError
 *
 * Keeps the anchors at their current distance (a rigid rod). Change the
 * range with lrg_joint_set_distance_range() and turn it into a spring
 * with lrg_joint_set_limit_spring() on %LRG_JOINT_AXIS_TRANSLATION_X.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_distance (LrgDynamicsWorld  *world,
                                  guint              body_a,
                                  guint              body_b,
                                  const gfloat      *anchor_a,
                                  const gfloat      *anchor_b,
                                  GError           **error);

/**
 * lrg_joint_new_cone:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space pivot
 * @twist_axis: (array fixed-size=3): world-space cone axis
 * @half_angle: cone half angle in radians, in [0, pi]
 * @error: (nullable): return location for a #GError
 *
 * A ball joint whose twist axis stays within @half_angle of its rest
 * direction. Twist about the axis is free.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_cone (LrgDynamicsWorld  *world,
                              guint              body_a,
                              guint              body_b,
                              const gfloat      *anchor,
                              const gfloat      *twist_axis,
                              gfloat             half_angle,
                              GError           **error);

/**
 * lrg_joint_new_swing_twist:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space pivot
 * @twist_axis: (array fixed-size=3): world-space twist axis
 * @plane_axis: (array fixed-size=3) (nullable): world-space swing Y
 *   axis, or %NULL for any perpendicular
 * @swing_y: swing half angle about Y, radians in [0, pi]
 * @swing_z: swing half angle about Z, radians in [0, pi]
 * @twist_min: minimum twist, radians in [-pi, pi]
 * @twist_max: maximum twist, radians in [@twist_min, pi]
 * @error: (nullable): return location for a #GError
 *
 * The ragdoll joint: an elliptic swing cone plus a twist range.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_swing_twist (LrgDynamicsWorld  *world,
                                     guint              body_a,
                                     guint              body_b,
                                     const gfloat      *anchor,
                                     const gfloat      *twist_axis,
                                     const gfloat      *plane_axis,
                                     gfloat             swing_y,
                                     gfloat             swing_z,
                                     gfloat             twist_min,
                                     gfloat             twist_max,
                                     GError           **error);

/**
 * lrg_joint_new_six_dof:
 * @world: the world that holds both bodies
 * @body_a: the first body
 * @body_b: the second body, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor: (array fixed-size=3): world-space joint origin
 * @axis_x: (array fixed-size=3): world-space frame X axis
 * @axis_y: (array fixed-size=3) (nullable): world-space frame Y axis
 * @error: (nullable): return location for a #GError
 *
 * A 6-DOF joint with every axis free. Configure each axis with
 * lrg_joint_set_axis_mode(), lrg_joint_set_limits() and the motor calls.
 *
 * Returns: (transfer full) (nullable): a new joint
 */
LRG_AVAILABLE_IN_ALL
LrgJoint *lrg_joint_new_six_dof (LrgDynamicsWorld  *world,
                                 guint              body_a,
                                 guint              body_b,
                                 const gfloat      *anchor,
                                 const gfloat      *axis_x,
                                 const gfloat      *axis_y,
                                 GError           **error);

/* ------------------------------------------------------------------ */
/* Identity and frames                                                 */
/* ------------------------------------------------------------------ */

/**
 * lrg_joint_get_kind:
 * @self: a joint
 *
 * Returns: the kind the joint was created with
 */
LRG_AVAILABLE_IN_ALL
LrgJointKind lrg_joint_get_kind (LrgJoint *self);

/**
 * lrg_joint_get_frame_a:
 * @self: a joint
 * @position: (out caller-allocates) (array fixed-size=3): joint origin
 *   in body A's local frame
 * @rotation: (out caller-allocates) (array fixed-size=4): xyzw rotation
 *   from the joint frame to body A
 *
 * Reads the joint frame on body A.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_frame_a (LrgJoint *self,
                            gfloat   *position,
                            gfloat   *rotation);

/**
 * lrg_joint_get_frame_b:
 * @self: a joint
 * @position: (out caller-allocates) (array fixed-size=3): joint origin
 *   in body B's local frame (world when B is the world)
 * @rotation: (out caller-allocates) (array fixed-size=4): xyzw rotation
 *   from the joint frame to body B
 *
 * Reads the joint frame on body B.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_frame_b (LrgJoint *self,
                            gfloat   *position,
                            gfloat   *rotation);

/* ------------------------------------------------------------------ */
/* Axes, limits, springs, friction                                     */
/* ------------------------------------------------------------------ */

/**
 * lrg_joint_set_axis_mode:
 * @self: a joint
 * @axis: the axis
 * @mode: free, locked or limited
 *
 * Changes how one axis is constrained. %LRG_JOINT_AXIS_LIMITED uses the
 * limits last given to lrg_joint_set_limits() (zero range until then).
 * Ignored on a distance joint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_axis_mode (LrgJoint         *self,
                              LrgJointAxis      axis,
                              LrgJointAxisMode  mode);

/**
 * lrg_joint_get_axis_mode:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the axis mode
 */
LRG_AVAILABLE_IN_ALL
LrgJointAxisMode lrg_joint_get_axis_mode (LrgJoint     *self,
                                          LrgJointAxis  axis);

/**
 * lrg_joint_set_limits:
 * @self: a joint
 * @axis: the axis
 * @minimum: lower limit, metres or radians
 * @maximum: upper limit, at least @minimum
 *
 * Limits one axis and sets its mode to %LRG_JOINT_AXIS_LIMITED (or
 * %LRG_JOINT_AXIS_LOCKED when the two are equal). Rotation limits are
 * clamped to [-pi, pi]. With %LRG_JOINT_SWING_CONE the swing axes use
 * @maximum symmetrically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_limits (LrgJoint     *self,
                           LrgJointAxis  axis,
                           gfloat        minimum,
                           gfloat        maximum);

/**
 * lrg_joint_get_limits:
 * @self: a joint
 * @axis: the axis
 * @minimum: (out) (optional): lower limit
 * @maximum: (out) (optional): upper limit
 *
 * Reads the stored limits of an axis (meaningful when it is limited or
 * locked).
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_limits (LrgJoint     *self,
                           LrgJointAxis  axis,
                           gfloat       *minimum,
                           gfloat       *maximum);

/**
 * lrg_joint_set_limit_spring:
 * @self: a joint
 * @axis: a translation axis (for a distance joint:
 *   %LRG_JOINT_AXIS_TRANSLATION_X)
 * @frequency: spring frequency in Hz; zero makes the limit hard
 * @damping: damping ratio, zero or positive (1 is critical)
 *
 * Makes a translation limit soft: past the limit the axis is pulled
 * back by a spring instead of being stopped. On a distance joint whose
 * minimum equals its maximum this is a spring/damper whose oscillation
 * frequency is @frequency for the joint's effective mass.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_limit_spring (LrgJoint     *self,
                                 LrgJointAxis  axis,
                                 gfloat        frequency,
                                 gfloat        damping);

/**
 * lrg_joint_get_limit_spring:
 * @self: a joint
 * @axis: the axis
 * @frequency: (out) (optional): spring frequency in Hz
 * @damping: (out) (optional): damping ratio
 *
 * Reads the limit spring of an axis.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_limit_spring (LrgJoint     *self,
                                 LrgJointAxis  axis,
                                 gfloat       *frequency,
                                 gfloat       *damping);

/**
 * lrg_joint_set_friction:
 * @self: a joint
 * @axis: the axis
 * @friction: largest force (N) or torque (N m) that resists motion on a
 *   free or limited axis without a motor; zero or positive
 *
 * Joint friction: a velocity motor with target zero and this cap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_friction (LrgJoint     *self,
                             LrgJointAxis  axis,
                             gfloat        friction);

/**
 * lrg_joint_get_friction:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the friction force or torque of the axis
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_friction (LrgJoint     *self,
                               LrgJointAxis  axis);

/**
 * lrg_joint_set_swing_type:
 * @self: a joint
 * @swing_type: cone or pyramid
 *
 * Chooses how limited swing Y and swing Z combine.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_swing_type (LrgJoint          *self,
                               LrgJointSwingType  swing_type);

/**
 * lrg_joint_get_swing_type:
 * @self: a joint
 *
 * Returns: the swing type
 */
LRG_AVAILABLE_IN_ALL
LrgJointSwingType lrg_joint_get_swing_type (LrgJoint *self);

/**
 * lrg_joint_set_distance_range:
 * @self: a distance joint
 * @minimum: zero or positive minimum anchor distance
 * @maximum: maximum anchor distance, at least @minimum
 *
 * Changes the allowed anchor distance of a distance joint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_distance_range (LrgJoint *self,
                                   gfloat    minimum,
                                   gfloat    maximum);

/**
 * lrg_joint_get_distance_range:
 * @self: a distance joint
 * @minimum: (out) (optional): minimum distance
 * @maximum: (out) (optional): maximum distance
 *
 * Reads the distance range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_distance_range (LrgJoint *self,
                                   gfloat   *minimum,
                                   gfloat   *maximum);

/* ------------------------------------------------------------------ */
/* Motors                                                              */
/* ------------------------------------------------------------------ */

/**
 * lrg_joint_set_motor:
 * @self: a joint (not a distance joint)
 * @axis: the axis to drive
 * @mode: off, velocity or position
 * @max_force: largest force (N, translation) or torque (N m, rotation)
 *   the motor applies; must be positive to have an effect
 *
 * Turns a motor on or off. A motor works on free and limited axes; the
 * limits still win.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_motor (LrgJoint          *self,
                          LrgJointAxis       axis,
                          LrgJointMotorMode  mode,
                          gfloat             max_force);

/**
 * lrg_joint_get_motor_mode:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the motor mode of the axis
 */
LRG_AVAILABLE_IN_ALL
LrgJointMotorMode lrg_joint_get_motor_mode (LrgJoint     *self,
                                            LrgJointAxis  axis);

/**
 * lrg_joint_get_motor_max_force:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the motor's force or torque cap
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_motor_max_force (LrgJoint     *self,
                                      LrgJointAxis  axis);

/**
 * lrg_joint_set_motor_spring:
 * @self: a joint
 * @axis: the axis
 * @frequency: position-motor spring frequency in Hz, positive
 * @damping: damping ratio, zero or positive
 *
 * Sets how stiffly a position motor pulls toward its target. The default
 * is 2 Hz, critically damped (1.0).
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_motor_spring (LrgJoint     *self,
                                 LrgJointAxis  axis,
                                 gfloat        frequency,
                                 gfloat        damping);

/**
 * lrg_joint_get_motor_spring:
 * @self: a joint
 * @axis: the axis
 * @frequency: (out) (optional): spring frequency in Hz
 * @damping: (out) (optional): damping ratio
 *
 * Reads a position motor's spring.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_motor_spring (LrgJoint     *self,
                                 LrgJointAxis  axis,
                                 gfloat       *frequency,
                                 gfloat       *damping);

/**
 * lrg_joint_set_motor_target_velocity:
 * @self: a joint
 * @axis: the axis
 * @velocity: target velocity, m/s or rad/s
 *
 * Sets the velocity a velocity motor drives toward. For rotation axes
 * the velocity is about the joint frame axis of body B.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_motor_target_velocity (LrgJoint     *self,
                                          LrgJointAxis  axis,
                                          gfloat        velocity);

/**
 * lrg_joint_get_motor_target_velocity:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the target velocity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_motor_target_velocity (LrgJoint     *self,
                                            LrgJointAxis  axis);

/**
 * lrg_joint_set_motor_target_position:
 * @self: a joint
 * @axis: the axis
 * @position: target position, metres or radians
 *
 * Sets the position a position motor drives toward. The three rotation
 * targets together form the target orientation Rz * Ry * Rx (see
 * lrg_joint_set_target_orientation()); a single rotation axis target is
 * exact.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_motor_target_position (LrgJoint     *self,
                                          LrgJointAxis  axis,
                                          gfloat        position);

/**
 * lrg_joint_get_motor_target_position:
 * @self: a joint
 * @axis: the axis
 *
 * Returns: the target position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_motor_target_position (LrgJoint     *self,
                                            LrgJointAxis  axis);

/**
 * lrg_joint_set_target_orientation:
 * @self: a joint
 * @rotation: (array fixed-size=4): xyzw rotation of body B's joint frame
 *   relative to body A's joint frame
 *
 * Sets the orientation the rotation position motors drive toward. A
 * target outside the swing/twist limits is clamped onto them. Used by
 * powered ragdolls.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_target_orientation (LrgJoint     *self,
                                       const gfloat *rotation);

/**
 * lrg_joint_get_target_orientation:
 * @self: a joint
 * @rotation: (out caller-allocates) (array fixed-size=4): xyzw target
 *
 * Reads the rotation motor target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_target_orientation (LrgJoint *self,
                                       gfloat   *rotation);

/* ------------------------------------------------------------------ */
/* Breaking, enabling, readback                                        */
/* ------------------------------------------------------------------ */

/**
 * lrg_joint_set_break_force:
 * @self: a joint
 * @force: force in N above which the joint breaks; zero never breaks
 *
 * Makes the joint breakable by linear load.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_break_force (LrgJoint *self,
                                gfloat    force);

/**
 * lrg_joint_get_break_force:
 * @self: a joint
 *
 * Returns: the break force, zero when unbreakable by force
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_break_force (LrgJoint *self);

/**
 * lrg_joint_set_break_torque:
 * @self: a joint
 * @torque: torque in N m above which the joint breaks; zero never breaks
 *
 * Makes the joint breakable by angular load.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_set_break_torque (LrgJoint *self,
                                 gfloat    torque);

/**
 * lrg_joint_get_break_torque:
 * @self: a joint
 *
 * Returns: the break torque, zero when unbreakable by torque
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_joint_get_break_torque (LrgJoint *self);

/**
 * lrg_joint_is_broken:
 * @self: a joint
 *
 * A broken joint is disabled (lrg_physics_constraint_set_enabled()) and
 * applies no impulses until lrg_joint_repair().
 *
 * Returns: whether the joint broke
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_joint_is_broken (LrgJoint *self);

/**
 * lrg_joint_repair:
 * @self: a joint
 *
 * Clears the broken state and enables the joint again; it acts from the
 * next step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_repair (LrgJoint *self);

/**
 * lrg_joint_get_applied_force:
 * @self: a joint
 * @force: (out caller-allocates) (array fixed-size=3): world-space force
 *   in N that the joint applied to body B over the last substep
 *
 * The constraint (limit and lock) force; motor and friction forces are
 * included. Zero before the first step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_applied_force (LrgJoint *self,
                                  gfloat   *force);

/**
 * lrg_joint_get_applied_torque:
 * @self: a joint
 * @torque: (out caller-allocates) (array fixed-size=3): world-space
 *   torque in N m that the joint applied to body B over the last substep
 *
 * The angular counterpart of lrg_joint_get_applied_force().
 */
LRG_AVAILABLE_IN_ALL
void lrg_joint_get_applied_torque (LrgJoint *self,
                                   gfloat   *torque);

/**
 * lrg_joint_measure:
 * @self: a joint
 * @world: the world that holds the joint's bodies
 * @translation: (out caller-allocates) (array fixed-size=3) (optional):
 *   offset of B's frame origin from A's, in A's joint frame (metres). For
 *   a distance joint X is the anchor distance and Y, Z are zero
 * @angles: (out caller-allocates) (array fixed-size=3) (optional): twist
 *   about X, then swing about Y and Z, in radians, of B's joint frame
 *   relative to A's
 * @error: (nullable): return location for a #GError
 *
 * Measures the current joint coordinates from the world's body poses:
 * the values the limits and motors act on.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_joint_measure (LrgJoint          *self,
                            LrgDynamicsWorld  *world,
                            gfloat            *translation,
                            gfloat            *angles,
                            GError           **error);

G_END_DECLS
