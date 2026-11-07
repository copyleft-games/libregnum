/* lrg-ik-chain.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IK bone chain for inverse kinematics.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-skeleton.h"

G_BEGIN_DECLS

#define LRG_TYPE_IK_CHAIN (lrg_ik_chain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKChain, lrg_ik_chain, LRG, IK_CHAIN, GObject)

/**
 * lrg_ik_chain_new:
 * @skeleton: The skeleton containing the bones
 *
 * Creates a new IK chain.
 *
 * Returns: (transfer full): A new #LrgIKChain
 */
LRG_AVAILABLE_IN_ALL
LrgIKChain *    lrg_ik_chain_new                (LrgSkeleton    *skeleton);

/**
 * lrg_ik_chain_get_skeleton:
 * @self: A #LrgIKChain
 *
 * Gets the skeleton.
 *
 * Returns: (transfer none) (nullable): The skeleton
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton *   lrg_ik_chain_get_skeleton       (LrgIKChain     *self);

/**
 * lrg_ik_chain_add_bone:
 * @self: A #LrgIKChain
 * @bone_name: Bone name to add to chain
 *
 * Adds a bone to the chain. Bones should be added from
 * root to tip (base to end effector).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_add_bone           (LrgIKChain     *self,
                                                  const gchar    *bone_name);

/**
 * lrg_ik_chain_clear_bones:
 * @self: A #LrgIKChain
 *
 * Removes all bones from the chain.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_clear_bones        (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_bone_count:
 * @self: A #LrgIKChain
 *
 * Gets the number of bones in the chain.
 *
 * Returns: The bone count
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_ik_chain_get_bone_count     (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_bone_name:
 * @self: A #LrgIKChain
 * @index: Bone index in chain
 *
 * Gets the bone name at an index.
 *
 * Returns: (transfer none) (nullable): The bone name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_ik_chain_get_bone_name      (LrgIKChain     *self,
                                                  guint           index);

/**
 * lrg_ik_chain_get_bone:
 * @self: A #LrgIKChain
 * @index: Bone index in chain
 *
 * Gets the bone at an index.
 *
 * Returns: (transfer none) (nullable): The bone
 */
LRG_AVAILABLE_IN_ALL
LrgBone *       lrg_ik_chain_get_bone           (LrgIKChain     *self,
                                                  guint           index);

/**
 * lrg_ik_chain_get_target_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the target position for the end effector.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_target_position (LrgIKChain    *self,
                                                   gfloat        *x,
                                                   gfloat        *y,
                                                   gfloat        *z);

/**
 * lrg_ik_chain_set_target_position:
 * @self: A #LrgIKChain
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the target position for the end effector.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_target_position (LrgIKChain    *self,
                                                   gfloat         x,
                                                   gfloat         y,
                                                   gfloat         z);

/**
 * lrg_ik_chain_get_pole_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the pole vector position (for knee/elbow direction).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_pole_position  (LrgIKChain     *self,
                                                  gfloat         *x,
                                                  gfloat         *y,
                                                  gfloat         *z);

/**
 * lrg_ik_chain_set_pole_position:
 * @self: A #LrgIKChain
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the pole position (skeleton/model space): the chain bends toward
 * it (a knee toward a point in front of the leg). Without a pole the
 * bend keeps the plane of the current pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_pole_position  (LrgIKChain     *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_chain_get_total_length:
 * @self: A #LrgIKChain
 *
 * Gets the total length of the chain: the sum of the distances between
 * consecutive joints (and to the end effector) in the current pose, or
 * the bones' lengths before the skeleton is posed.
 *
 * Returns: Total chain length
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_ik_chain_get_total_length   (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_end_effector_position:
 * @self: A #LrgIKChain
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the current end effector position in skeleton (model) space: the
 * effector bone's position when one is set
 * (lrg_ik_chain_set_effector_bone()), otherwise the tip of the last
 * bone (its position plus its length along the bone axis).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_end_effector_position (LrgIKChain *self,
                                                         gfloat     *x,
                                                         gfloat     *y,
                                                         gfloat     *z);

/**
 * lrg_ik_chain_apply_to_skeleton:
 * @self: A #LrgIKChain
 *
 * Applies the chain's solved poses to the skeleton.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_apply_to_skeleton  (LrgIKChain     *self);

/* ==========================================================================
 * 0.9.0: effector bone, bone axis, pole flag and the joint helpers the
 * solvers are written with
 * ========================================================================== */

/**
 * lrg_ik_chain_get_effector_bone:
 * @self: A #LrgIKChain
 *
 * Returns: (transfer none) (nullable): the bone whose position is the end
 *   effector (for example the hand of an upper-arm/forearm chain)
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_ik_chain_get_effector_bone  (LrgIKChain     *self);

/**
 * lrg_ik_chain_set_effector_bone:
 * @self: A #LrgIKChain
 * @bone_name: (nullable): a child of the last chain bone, or %NULL to use
 *   the last bone's tip
 *
 * Sets the bone that has to reach the target. It is moved by the chain,
 * not rotated (see #LrgIKGoal for foot alignment).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_effector_bone  (LrgIKChain     *self,
                                                  const gchar    *bone_name);

/**
 * lrg_ik_chain_get_bone_axis:
 * @self: A #LrgIKChain
 * @x: (out) (optional): X
 * @y: (out) (optional): Y
 * @z: (out) (optional): Z
 *
 * Gets the bone-local axis bones point along (default +Y, the glTF and
 * Blender convention).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_get_bone_axis      (LrgIKChain     *self,
                                                  gfloat         *x,
                                                  gfloat         *y,
                                                  gfloat         *z);

/**
 * lrg_ik_chain_set_bone_axis:
 * @self: A #LrgIKChain
 * @x: X
 * @y: Y
 * @z: Z
 *
 * Sets the bone-local axis bones point along: it places the last bone's
 * tip without an effector bone, and is the forward axis of look-at.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_set_bone_axis      (LrgIKChain     *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_chain_has_pole:
 * @self: A #LrgIKChain
 *
 * Returns: whether lrg_ik_chain_set_pole_position() was called
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_chain_has_pole           (LrgIKChain     *self);

/**
 * lrg_ik_chain_clear_pole:
 * @self: A #LrgIKChain
 *
 * Forgets the pole: the bend keeps the plane of the current pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_chain_clear_pole         (LrgIKChain     *self);

/**
 * lrg_ik_chain_get_joint_positions:
 * @self: A #LrgIKChain
 * @out_positions: (array) (element-type gfloat) (out caller-allocates):
 *   3 floats per joint
 * @max_joints: room in @out_positions, in joints
 *
 * Reads the model-space positions of the chain's joints: one per bone,
 * then the end effector.
 *
 * Returns: the number of joints (bone count + 1), 0 when a bone is
 *   missing; only the first @max_joints are written
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_ik_chain_get_joint_positions (LrgIKChain    *self,
                                                   gfloat        *out_positions,
                                                   guint          max_joints);

/**
 * lrg_ik_chain_aim_bone:
 * @self: A #LrgIKChain
 * @index: a chain bone
 * @from: (array fixed-size=3): a model-space direction as the bone sees
 *   it now
 * @to: (array fixed-size=3): the direction it should become
 *
 * Rotates a chain bone about its joint by the shortest rotation taking
 * @from to @to (in model space), writes it back as a local rotation
 * (translation and scale stay) and recomputes the world poses. Every
 * solver is built on this.
 *
 * Returns: %FALSE if the bone is missing
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_chain_aim_bone           (LrgIKChain     *self,
                                                  guint           index,
                                                  const gfloat   *from,
                                                  const gfloat   *to);

/* ==========================================================================
 * LrgIKGoal: one entry of the animation graph's IK post-pass
 * ========================================================================== */

/**
 * LrgIKGoalKind:
 * @LRG_IK_GOAL_TWO_BONE: analytic two-bone IK (arms, legs); exact when the
 *   target is in reach
 * @LRG_IK_GOAL_CCD: cyclic coordinate descent over any number of bones
 * @LRG_IK_GOAL_FABRIK: forward and backward reaching IK over any number
 *   of bones (tails, spines, tentacles)
 * @LRG_IK_GOAL_LOOK_AT: turns one bone so its bone axis points at the
 *   target (heads, eyes, turrets)
 * @LRG_IK_GOAL_FOOT: foot placement: a raycast under the animated foot
 *   moves it onto the ground by two-bone IK and optionally aligns it to
 *   the ground normal
 *
 * The solver an #LrgIKGoal runs.
 */
typedef enum
{
    LRG_IK_GOAL_TWO_BONE,
    LRG_IK_GOAL_CCD,
    LRG_IK_GOAL_FABRIK,
    LRG_IK_GOAL_LOOK_AT,
    LRG_IK_GOAL_FOOT
} LrgIKGoalKind;

LRG_AVAILABLE_IN_ALL
GType lrg_ik_goal_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_IK_GOAL_KIND (lrg_ik_goal_kind_get_type ())

/**
 * LrgIKRaycastFunc:
 * @origin: (array fixed-size=3): ray origin, world space
 * @direction: (array fixed-size=3): unit ray direction, world space
 * @max_distance: ray length
 * @out_point: (array fixed-size=3) (out caller-allocates): the hit point
 * @out_normal: (array fixed-size=3) (out caller-allocates): the unit
 *   surface normal at the hit
 * @user_data: data given with the function
 *
 * Casts a ray into the game's collision world for foot placement.
 *
 * Returns: %TRUE on a hit within @max_distance
 */
typedef gboolean (*LrgIKRaycastFunc) (const gfloat *origin,
                                      const gfloat *direction,
                                      gfloat        max_distance,
                                      gfloat       *out_point,
                                      gfloat       *out_normal,
                                      gpointer      user_data);

#define LRG_TYPE_IK_GOAL (lrg_ik_goal_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKGoal, lrg_ik_goal, LRG, IK_GOAL, GObject)

/**
 * lrg_ik_goal_new:
 * @name: a unique name (targets are set by it)
 * @kind: the solver
 *
 * Returns: (transfer full): a new goal (weight 1, 10 iterations,
 *   tolerance 0.001, bone axis +Y, enabled)
 */
LRG_AVAILABLE_IN_ALL
LrgIKGoal *     lrg_ik_goal_new                 (const gchar    *name,
                                                  LrgIKGoalKind   kind);

/**
 * lrg_ik_goal_get_name:
 * @self: an #LrgIKGoal
 *
 * Returns: (transfer none): the goal's name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_ik_goal_get_name            (LrgIKGoal      *self);

/**
 * lrg_ik_goal_get_kind:
 * @self: an #LrgIKGoal
 *
 * Returns: the solver kind
 */
LRG_AVAILABLE_IN_ALL
LrgIKGoalKind   lrg_ik_goal_get_kind            (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_bones:
 * @self: an #LrgIKGoal
 * @bones: (array zero-terminated=1): the chain, root first (two bones
 *   for two-bone and foot, one for look-at)
 *
 * Sets the bones the goal rotates.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_bones           (LrgIKGoal          *self,
                                                  const gchar *const *bones);

/**
 * lrg_ik_goal_get_bones:
 * @self: an #LrgIKGoal
 *
 * Returns: (transfer full) (array zero-terminated=1): the chain bones
 */
LRG_AVAILABLE_IN_ALL
gchar **        lrg_ik_goal_get_bones           (LrgIKGoal      *self);

/**
 * lrg_ik_goal_get_effector:
 * @self: an #LrgIKGoal
 *
 * Returns: (transfer none) (nullable): the effector bone
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_ik_goal_get_effector        (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_effector:
 * @self: an #LrgIKGoal
 * @bone_name: (nullable): the bone that must reach the target (the foot
 *   or hand), or %NULL for the last bone's tip
 *
 * Sets the effector bone (see lrg_ik_chain_set_effector_bone()).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_effector        (LrgIKGoal      *self,
                                                  const gchar    *bone_name);

/**
 * lrg_ik_goal_set_pole:
 * @self: an #LrgIKGoal
 * @x: X (model space)
 * @y: Y
 * @z: Z
 *
 * Sets the point a two-bone or foot chain bends toward.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_pole            (LrgIKGoal      *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_goal_get_pole:
 * @self: an #LrgIKGoal
 * @out_pole: (out caller-allocates) (array fixed-size=3): the pole
 *
 * Returns: whether a pole is set
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_goal_get_pole            (LrgIKGoal      *self,
                                                  gfloat         *out_pole);

/**
 * lrg_ik_goal_clear_pole:
 * @self: an #LrgIKGoal
 *
 * Removes the pole.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_clear_pole          (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_target:
 * @self: an #LrgIKGoal
 * @x: X (world space)
 * @y: Y
 * @z: Z
 *
 * Sets the world-space point to reach or look at (foot goals find their
 * own target).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_target          (LrgIKGoal      *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_goal_get_target:
 * @self: an #LrgIKGoal
 * @out_target: (out caller-allocates) (array fixed-size=3): the target
 *
 * Returns: whether a target is set
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_goal_get_target          (LrgIKGoal      *self,
                                                  gfloat         *out_target);

/**
 * lrg_ik_goal_clear_target:
 * @self: an #LrgIKGoal
 *
 * Removes the target (the goal then does nothing, except foot goals).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_clear_target        (LrgIKGoal      *self);

/**
 * lrg_ik_goal_get_weight:
 * @self: an #LrgIKGoal
 *
 * Returns: the blend weight between the animated and the solved pose
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_ik_goal_get_weight          (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_weight:
 * @self: an #LrgIKGoal
 * @weight: 0 (animation only) .. 1 (fully solved)
 *
 * Sets how much of the solved rotation is used (fade IK in and out).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_weight          (LrgIKGoal      *self,
                                                  gfloat          weight);

/**
 * lrg_ik_goal_get_iterations:
 * @self: an #LrgIKGoal
 *
 * Returns: the iteration budget of CCD/FABRIK
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_ik_goal_get_iterations      (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_iterations:
 * @self: an #LrgIKGoal
 * @iterations: iterations (at least 1)
 *
 * Sets the iteration budget of the iterative solvers.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_iterations      (LrgIKGoal      *self,
                                                  guint           iterations);

/**
 * lrg_ik_goal_get_tolerance:
 * @self: an #LrgIKGoal
 *
 * Returns: the distance at which the iterative solvers stop
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_ik_goal_get_tolerance       (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_tolerance:
 * @self: an #LrgIKGoal
 * @tolerance: distance in model units
 *
 * Sets the early-out distance of CCD/FABRIK.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_tolerance       (LrgIKGoal      *self,
                                                  gfloat          tolerance);

/**
 * lrg_ik_goal_get_enabled:
 * @self: an #LrgIKGoal
 *
 * Returns: whether the goal runs
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_goal_get_enabled         (LrgIKGoal      *self);

/**
 * lrg_ik_goal_set_enabled:
 * @self: an #LrgIKGoal
 * @enabled: whether the goal runs
 *
 * Enables or disables the goal.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_enabled         (LrgIKGoal      *self,
                                                  gboolean        enabled);

/**
 * lrg_ik_goal_set_bone_axis:
 * @self: an #LrgIKGoal
 * @x: X
 * @y: Y
 * @z: Z
 *
 * Sets the bone-local axis (the look-at forward axis; default +Y).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_bone_axis       (LrgIKGoal      *self,
                                                  gfloat          x,
                                                  gfloat          y,
                                                  gfloat          z);

/**
 * lrg_ik_goal_get_bone_axis:
 * @self: an #LrgIKGoal
 * @out_axis: (out caller-allocates) (array fixed-size=3): the axis
 *
 * Gets the bone-local axis.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_get_bone_axis       (LrgIKGoal      *self,
                                                  gfloat         *out_axis);

/**
 * lrg_ik_goal_set_foot:
 * @self: an #LrgIKGoal
 * @ray_up: how far above the animated foot the ray starts
 * @ray_down: how far below the animated foot it reaches
 * @align: whether the effector is turned to the ground normal
 *
 * Sets the foot placement probe (%LRG_IK_GOAL_FOOT). The foot keeps its
 * animated height above the character's ground plane (lifted feet stay
 * lifted) and is moved by the height of the ground under it relative to
 * the plane.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_set_foot            (LrgIKGoal      *self,
                                                  gfloat          ray_up,
                                                  gfloat          ray_down,
                                                  gboolean        align);

/**
 * lrg_ik_goal_get_foot:
 * @self: an #LrgIKGoal
 * @out_ray_up: (out) (optional): the ray start height
 * @out_ray_down: (out) (optional): the ray reach below the foot
 * @out_align: (out) (optional): whether the foot aligns to the normal
 *
 * Gets the foot placement probe.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_ik_goal_get_foot            (LrgIKGoal      *self,
                                                  gfloat         *out_ray_up,
                                                  gfloat         *out_ray_down,
                                                  gboolean       *out_align);

/**
 * lrg_ik_goal_apply:
 * @self: an #LrgIKGoal
 * @skeleton: the posed skeleton (world poses computed); it is modified
 * @model_matrix: (array fixed-size=16) (nullable): the column-major
 *   model-to-world matrix of the character (the entity's world matrix),
 *   or %NULL for identity
 * @raycast: (nullable) (scope call) (closure user_data): the ground probe for foot goals
 * @user_data: data for @raycast
 *
 * Runs the goal on the skeleton: converts the target into model space,
 * solves, blends the solved rotations with the animated ones by the
 * weight and recomputes the world poses.
 *
 * Returns: %TRUE if the skeleton was changed
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_goal_apply               (LrgIKGoal        *self,
                                                  LrgSkeleton      *skeleton,
                                                  const gfloat     *model_matrix,
                                                  LrgIKRaycastFunc  raycast,
                                                  gpointer          user_data);

/**
 * lrg_ik_goal_get_last_target:
 * @self: an #LrgIKGoal
 * @out_target: (out caller-allocates) (array fixed-size=3): the
 *   model-space point the last apply aimed at
 *
 * For debug drawing and the editor's IK gizmos.
 *
 * Returns: whether the last apply solved anything
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_ik_goal_get_last_target     (LrgIKGoal      *self,
                                                  gfloat         *out_target);

G_END_DECLS
