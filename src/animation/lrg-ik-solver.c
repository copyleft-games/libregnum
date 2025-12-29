/* lrg-ik-solver.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-ik-solver.h"
#include <math.h>

/**
 * SECTION:lrg-ik-solver
 * @Title: LrgIKSolver
 * @Short_description: IK solver interface and implementations
 *
 * #LrgIKSolver is the base class for inverse kinematics solvers.
 * Several implementations are provided:
 *
 * - #LrgIKSolverFABRIK: Forward And Backward Reaching IK, works with any chain length
 * - #LrgIKSolverCCD: Cyclic Coordinate Descent, works with any chain length
 * - #LrgIKSolverTwoBone: Analytical solver for exactly 2 bones
 * - #LrgIKSolverLookAt: Simple aim constraint for a single bone
 */

G_DEFINE_TYPE (LrgIKSolver, lrg_ik_solver, G_TYPE_OBJECT)

/*
 * Helper functions
 */

static gfloat
vec3_length (gfloat x, gfloat y, gfloat z)
{
    return sqrtf (x * x + y * y + z * z);
}

static void
vec3_normalize (gfloat *x, gfloat *y, gfloat *z)
{
    gfloat len;

    len = vec3_length (*x, *y, *z);
    if (len > 0.0001f)
    {
        *x /= len;
        *y /= len;
        *z /= len;
    }
}

static gfloat
vec3_dot (gfloat ax, gfloat ay, gfloat az,
          gfloat bx, gfloat by, gfloat bz)
{
    return ax * bx + ay * by + az * bz;
}

static void
vec3_cross (gfloat ax, gfloat ay, gfloat az,
            gfloat bx, gfloat by, gfloat bz,
            gfloat *rx, gfloat *ry, gfloat *rz)
{
    *rx = ay * bz - az * by;
    *ry = az * bx - ax * bz;
    *rz = ax * by - ay * bx;
}

/*
 * LrgIKSolver base class
 */

static gboolean
lrg_ik_solver_real_solve (LrgIKSolver *solver,
                          LrgIKChain  *chain,
                          guint        max_iterations,
                          gfloat       tolerance)
{
    /* Base class does nothing */
    return FALSE;
}

static gboolean
lrg_ik_solver_real_supports_chain_length (LrgIKSolver *solver,
                                          guint        bone_count)
{
    /* Base class supports any length by default */
    return bone_count > 0;
}

static void
lrg_ik_solver_class_init (LrgIKSolverClass *klass)
{
    klass->solve = lrg_ik_solver_real_solve;
    klass->supports_chain_length = lrg_ik_solver_real_supports_chain_length;
}

static void
lrg_ik_solver_init (LrgIKSolver *self)
{
}

/*
 * Public API
 */

gboolean
lrg_ik_solver_solve (LrgIKSolver *solver,
                     LrgIKChain  *chain,
                     guint        max_iterations,
                     gfloat       tolerance)
{
    LrgIKSolverClass *klass;

    g_return_val_if_fail (LRG_IS_IK_SOLVER (solver), FALSE);
    g_return_val_if_fail (LRG_IS_IK_CHAIN (chain), FALSE);

    klass = LRG_IK_SOLVER_GET_CLASS (solver);
    return klass->solve (solver, chain, max_iterations, tolerance);
}

gboolean
lrg_ik_solver_supports_chain_length (LrgIKSolver *solver,
                                     guint        bone_count)
{
    LrgIKSolverClass *klass;

    g_return_val_if_fail (LRG_IS_IK_SOLVER (solver), FALSE);

    klass = LRG_IK_SOLVER_GET_CLASS (solver);
    return klass->supports_chain_length (solver, bone_count);
}

/*
 * ============================================================================
 * LrgIKSolverFABRIK - Forward And Backward Reaching Inverse Kinematics
 * ============================================================================
 */

struct _LrgIKSolverFABRIK
{
    LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverFABRIK, lrg_ik_solver_fabrik, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_fabrik_solve (LrgIKSolver *solver,
                            LrgIKChain  *chain,
                            guint        max_iterations,
                            gfloat       tolerance)
{
    LrgSkeleton *skeleton;
    guint bone_count;
    guint iteration;
    gfloat total_length;
    gfloat target_x, target_y, target_z;
    gfloat root_x, root_y, root_z;
    gfloat *positions_x, *positions_y, *positions_z;
    gfloat *lengths;
    gfloat dist_to_target;
    guint i;

    skeleton = lrg_ik_chain_get_skeleton (chain);
    bone_count = lrg_ik_chain_get_bone_count (chain);

    if (skeleton == NULL || bone_count == 0)
        return FALSE;

    /* Allocate position arrays (bone_count + 1 for end effector) */
    positions_x = g_new0 (gfloat, bone_count + 1);
    positions_y = g_new0 (gfloat, bone_count + 1);
    positions_z = g_new0 (gfloat, bone_count + 1);
    lengths = g_new0 (gfloat, bone_count);

    /* Get initial bone positions and lengths */
    for (i = 0; i < bone_count; i++)
    {
        LrgBone *bone;
        const LrgBonePose *world_pose;

        bone = lrg_ik_chain_get_bone (chain, i);
        if (bone == NULL)
        {
            g_free (positions_x);
            g_free (positions_y);
            g_free (positions_z);
            g_free (lengths);
            return FALSE;
        }

        world_pose = lrg_bone_get_world_pose (bone);
        positions_x[i] = world_pose->position_x;
        positions_y[i] = world_pose->position_y;
        positions_z[i] = world_pose->position_z;

        lengths[i] = lrg_bone_get_length (bone);
    }

    /* End effector position */
    lrg_ik_chain_get_end_effector_position (chain,
                                            &positions_x[bone_count],
                                            &positions_y[bone_count],
                                            &positions_z[bone_count]);

    /* Store root position */
    root_x = positions_x[0];
    root_y = positions_y[0];
    root_z = positions_z[0];

    /* Get target */
    lrg_ik_chain_get_target_position (chain, &target_x, &target_y, &target_z);

    /* Check if target is reachable */
    total_length = lrg_ik_chain_get_total_length (chain);
    dist_to_target = vec3_length (target_x - root_x,
                                  target_y - root_y,
                                  target_z - root_z);

    if (dist_to_target > total_length)
    {
        /*
         * Target unreachable - stretch towards target.
         * Point all bones directly at target.
         */
        gfloat dir_x, dir_y, dir_z;

        dir_x = target_x - root_x;
        dir_y = target_y - root_y;
        dir_z = target_z - root_z;
        vec3_normalize (&dir_x, &dir_y, &dir_z);

        positions_x[0] = root_x;
        positions_y[0] = root_y;
        positions_z[0] = root_z;

        for (i = 0; i < bone_count; i++)
        {
            positions_x[i + 1] = positions_x[i] + dir_x * lengths[i];
            positions_y[i + 1] = positions_y[i] + dir_y * lengths[i];
            positions_z[i + 1] = positions_z[i] + dir_z * lengths[i];
        }
    }
    else
    {
        /* FABRIK iterations */
        for (iteration = 0; iteration < max_iterations; iteration++)
        {
            gfloat end_x, end_y, end_z;
            gfloat diff;
            gint j;

            end_x = positions_x[bone_count];
            end_y = positions_y[bone_count];
            end_z = positions_z[bone_count];

            diff = vec3_length (end_x - target_x,
                                end_y - target_y,
                                end_z - target_z);

            if (diff < tolerance)
                break;

            /*
             * Backward pass: start from end effector, move towards root.
             * Set end effector to target, then pull each joint.
             */
            positions_x[bone_count] = target_x;
            positions_y[bone_count] = target_y;
            positions_z[bone_count] = target_z;

            for (j = bone_count - 1; j >= 0; j--)
            {
                gfloat dx, dy, dz;
                gfloat d;

                dx = positions_x[j] - positions_x[j + 1];
                dy = positions_y[j] - positions_y[j + 1];
                dz = positions_z[j] - positions_z[j + 1];
                d = vec3_length (dx, dy, dz);

                if (d > 0.0001f)
                {
                    gfloat ratio;

                    ratio = lengths[j] / d;
                    positions_x[j] = positions_x[j + 1] + dx * ratio;
                    positions_y[j] = positions_y[j + 1] + dy * ratio;
                    positions_z[j] = positions_z[j + 1] + dz * ratio;
                }
            }

            /*
             * Forward pass: start from root, move towards end effector.
             * Set root back to original position, then push each joint.
             */
            positions_x[0] = root_x;
            positions_y[0] = root_y;
            positions_z[0] = root_z;

            for (i = 0; i < bone_count; i++)
            {
                gfloat dx, dy, dz;
                gfloat d;

                dx = positions_x[i + 1] - positions_x[i];
                dy = positions_y[i + 1] - positions_y[i];
                dz = positions_z[i + 1] - positions_z[i];
                d = vec3_length (dx, dy, dz);

                if (d > 0.0001f)
                {
                    gfloat ratio;

                    ratio = lengths[i] / d;
                    positions_x[i + 1] = positions_x[i] + dx * ratio;
                    positions_y[i + 1] = positions_y[i] + dy * ratio;
                    positions_z[i + 1] = positions_z[i] + dz * ratio;
                }
            }
        }
    }

    /*
     * Convert positions back to bone rotations.
     * For each bone, calculate the rotation needed to point from its
     * position to the next joint position.
     */
    for (i = 0; i < bone_count; i++)
    {
        LrgBone *bone;
        LrgBonePose pose;
        gfloat dir_x, dir_y, dir_z;
        gfloat qw, qx, qy, qz;
        gint bone_index;

        bone = lrg_ik_chain_get_bone (chain, i);
        if (bone == NULL)
            continue;

        /* Direction from this joint to next */
        dir_x = positions_x[i + 1] - positions_x[i];
        dir_y = positions_y[i + 1] - positions_y[i];
        dir_z = positions_z[i + 1] - positions_z[i];
        vec3_normalize (&dir_x, &dir_y, &dir_z);

        /*
         * Calculate quaternion to rotate from (1, 0, 0) to direction.
         * Using axis-angle: axis = cross(from, to), angle = acos(dot(from, to))
         */
        {
            gfloat from_x, from_y, from_z;
            gfloat dot, cross_x, cross_y, cross_z;
            gfloat cross_len, half_angle, s;

            /* Default bone direction is +X */
            from_x = 1.0f;
            from_y = 0.0f;
            from_z = 0.0f;

            dot = from_x * dir_x + from_y * dir_y + from_z * dir_z;

            if (dot > 0.9999f)
            {
                /* Vectors are parallel, identity quaternion */
                qw = 1.0f;
                qx = 0.0f;
                qy = 0.0f;
                qz = 0.0f;
            }
            else if (dot < -0.9999f)
            {
                /* Vectors are opposite, rotate 180 around any perpendicular axis */
                qw = 0.0f;
                qx = 0.0f;
                qy = 1.0f;
                qz = 0.0f;
            }
            else
            {
                vec3_cross (from_x, from_y, from_z, dir_x, dir_y, dir_z,
                            &cross_x, &cross_y, &cross_z);
                cross_len = vec3_length (cross_x, cross_y, cross_z);

                half_angle = acosf (dot) * 0.5f;
                s = sinf (half_angle) / cross_len;

                qw = cosf (half_angle);
                qx = cross_x * s;
                qy = cross_y * s;
                qz = cross_z * s;
            }
        }

        /* Build local pose */
        lrg_bone_pose_set_identity (&pose);
        pose.rotation_x = qx;
        pose.rotation_y = qy;
        pose.rotation_z = qz;
        pose.rotation_w = qw;

        /* Apply to skeleton */
        bone_index = lrg_bone_get_index (bone);
        lrg_skeleton_set_pose (skeleton, bone_index, &pose);
    }

    lrg_skeleton_calculate_world_poses (skeleton);

    g_free (positions_x);
    g_free (positions_y);
    g_free (positions_z);
    g_free (lengths);

    return TRUE;
}

static void
lrg_ik_solver_fabrik_class_init (LrgIKSolverFABRIKClass *klass)
{
    LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

    solver_class->solve = lrg_ik_solver_fabrik_solve;
}

static void
lrg_ik_solver_fabrik_init (LrgIKSolverFABRIK *self)
{
}

LrgIKSolverFABRIK *
lrg_ik_solver_fabrik_new (void)
{
    return g_object_new (LRG_TYPE_IK_SOLVER_FABRIK, NULL);
}

/*
 * ============================================================================
 * LrgIKSolverCCD - Cyclic Coordinate Descent
 * ============================================================================
 */

struct _LrgIKSolverCCD
{
    LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverCCD, lrg_ik_solver_ccd, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_ccd_solve (LrgIKSolver *solver,
                         LrgIKChain  *chain,
                         guint        max_iterations,
                         gfloat       tolerance)
{
    LrgSkeleton *skeleton;
    guint bone_count;
    guint iteration;
    gfloat target_x, target_y, target_z;
    gint i;

    skeleton = lrg_ik_chain_get_skeleton (chain);
    bone_count = lrg_ik_chain_get_bone_count (chain);

    if (skeleton == NULL || bone_count == 0)
        return FALSE;

    lrg_ik_chain_get_target_position (chain, &target_x, &target_y, &target_z);

    for (iteration = 0; iteration < max_iterations; iteration++)
    {
        gfloat end_x, end_y, end_z;
        gfloat dist;

        /* Get current end effector position */
        lrg_ik_chain_get_end_effector_position (chain, &end_x, &end_y, &end_z);

        dist = vec3_length (target_x - end_x, target_y - end_y, target_z - end_z);
        if (dist < tolerance)
            return TRUE;

        /*
         * Iterate from tip to root.
         * For each bone, rotate it to bring end effector closer to target.
         */
        for (i = (gint)bone_count - 1; i >= 0; i--)
        {
            LrgBone *bone;
            const LrgBonePose *world_pose;
            LrgBonePose local_pose;
            gfloat joint_x, joint_y, joint_z;
            gfloat to_end_x, to_end_y, to_end_z;
            gfloat to_target_x, to_target_y, to_target_z;
            gfloat dot, angle;
            gfloat axis_x, axis_y, axis_z;
            gfloat axis_len;
            gfloat half_angle, s;
            gfloat dqw, dqx, dqy, dqz;
            gfloat nqw, nqx, nqy, nqz;
            gint bone_index;

            bone = lrg_ik_chain_get_bone (chain, (guint)i);
            if (bone == NULL)
                continue;

            /* Get current end effector position */
            lrg_ik_chain_get_end_effector_position (chain, &end_x, &end_y, &end_z);

            /* Get this joint's world position */
            world_pose = lrg_bone_get_world_pose (bone);
            joint_x = world_pose->position_x;
            joint_y = world_pose->position_y;
            joint_z = world_pose->position_z;

            /* Vector from joint to end effector */
            to_end_x = end_x - joint_x;
            to_end_y = end_y - joint_y;
            to_end_z = end_z - joint_z;
            vec3_normalize (&to_end_x, &to_end_y, &to_end_z);

            /* Vector from joint to target */
            to_target_x = target_x - joint_x;
            to_target_y = target_y - joint_y;
            to_target_z = target_z - joint_z;
            vec3_normalize (&to_target_x, &to_target_y, &to_target_z);

            /* Calculate rotation axis and angle */
            dot = vec3_dot (to_end_x, to_end_y, to_end_z,
                            to_target_x, to_target_y, to_target_z);
            dot = CLAMP (dot, -1.0f, 1.0f);
            angle = acosf (dot);

            if (fabsf (angle) < 0.0001f)
                continue;

            vec3_cross (to_end_x, to_end_y, to_end_z,
                        to_target_x, to_target_y, to_target_z,
                        &axis_x, &axis_y, &axis_z);
            axis_len = vec3_length (axis_x, axis_y, axis_z);

            if (axis_len < 0.0001f)
                continue;

            axis_x /= axis_len;
            axis_y /= axis_len;
            axis_z /= axis_len;

            /* Create delta rotation quaternion */
            half_angle = angle * 0.5f;
            s = sinf (half_angle);
            dqw = cosf (half_angle);
            dqx = axis_x * s;
            dqy = axis_y * s;
            dqz = axis_z * s;

            /* Get current local pose */
            local_pose = *lrg_bone_get_local_pose (bone);

            /* Multiply current rotation by delta rotation */
            nqw = dqw * local_pose.rotation_w - dqx * local_pose.rotation_x
                - dqy * local_pose.rotation_y - dqz * local_pose.rotation_z;
            nqx = dqw * local_pose.rotation_x + dqx * local_pose.rotation_w
                + dqy * local_pose.rotation_z - dqz * local_pose.rotation_y;
            nqy = dqw * local_pose.rotation_y - dqx * local_pose.rotation_z
                + dqy * local_pose.rotation_w + dqz * local_pose.rotation_x;
            nqz = dqw * local_pose.rotation_z + dqx * local_pose.rotation_y
                - dqy * local_pose.rotation_x + dqz * local_pose.rotation_w;

            local_pose.rotation_w = nqw;
            local_pose.rotation_x = nqx;
            local_pose.rotation_y = nqy;
            local_pose.rotation_z = nqz;

            lrg_bone_pose_normalize_rotation (&local_pose);

            /* Apply to skeleton */
            bone_index = lrg_bone_get_index (bone);
            lrg_skeleton_set_pose (skeleton, bone_index, &local_pose);
            lrg_skeleton_calculate_world_poses (skeleton);
        }
    }

    return FALSE;
}

static void
lrg_ik_solver_ccd_class_init (LrgIKSolverCCDClass *klass)
{
    LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

    solver_class->solve = lrg_ik_solver_ccd_solve;
}

static void
lrg_ik_solver_ccd_init (LrgIKSolverCCD *self)
{
}

LrgIKSolverCCD *
lrg_ik_solver_ccd_new (void)
{
    return g_object_new (LRG_TYPE_IK_SOLVER_CCD, NULL);
}

/*
 * ============================================================================
 * LrgIKSolverTwoBone - Analytical two-bone solver
 * ============================================================================
 */

struct _LrgIKSolverTwoBone
{
    LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverTwoBone, lrg_ik_solver_two_bone, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_two_bone_supports (LrgIKSolver *solver, guint bone_count)
{
    return bone_count == 2;
}

static gboolean
lrg_ik_solver_two_bone_solve (LrgIKSolver *solver,
                              LrgIKChain  *chain,
                              guint        max_iterations,
                              gfloat       tolerance)
{
    LrgSkeleton *skeleton;
    LrgBone *bone0, *bone1;
    gfloat len0, len1;
    gfloat target_x, target_y, target_z;
    gfloat pole_x, pole_y, pole_z;
    gfloat root_x, root_y, root_z;
    const LrgBonePose *world_pose;
    gfloat dist, dist_sq;
    gfloat len0_sq, len1_sq;
    gfloat angle0, angle1;
    LrgBonePose pose0, pose1;
    gint bone_index0, bone_index1;

    (void)max_iterations;
    (void)tolerance;

    skeleton = lrg_ik_chain_get_skeleton (chain);
    if (skeleton == NULL || lrg_ik_chain_get_bone_count (chain) != 2)
        return FALSE;

    bone0 = lrg_ik_chain_get_bone (chain, 0);
    bone1 = lrg_ik_chain_get_bone (chain, 1);
    if (bone0 == NULL || bone1 == NULL)
        return FALSE;

    len0 = lrg_bone_get_length (bone0);
    len1 = lrg_bone_get_length (bone1);

    lrg_ik_chain_get_target_position (chain, &target_x, &target_y, &target_z);
    lrg_ik_chain_get_pole_position (chain, &pole_x, &pole_y, &pole_z);

    /* Get root position */
    world_pose = lrg_bone_get_world_pose (bone0);
    root_x = world_pose->position_x;
    root_y = world_pose->position_y;
    root_z = world_pose->position_z;

    /* Distance to target */
    dist_sq = (target_x - root_x) * (target_x - root_x)
            + (target_y - root_y) * (target_y - root_y)
            + (target_z - root_z) * (target_z - root_z);
    dist = sqrtf (dist_sq);

    len0_sq = len0 * len0;
    len1_sq = len1 * len1;

    /*
     * Use law of cosines to find the angles.
     * For bone0: cos(angle0) = (len0^2 + dist^2 - len1^2) / (2 * len0 * dist)
     * For bone1: cos(angle1) = (len0^2 + len1^2 - dist^2) / (2 * len0 * len1)
     */

    if (dist >= len0 + len1)
    {
        /* Target too far - fully extend */
        angle0 = 0.0f;
        angle1 = 0.0f;
    }
    else if (dist <= fabsf (len0 - len1))
    {
        /* Target too close - fold completely */
        angle0 = 0.0f;
        angle1 = G_PI;
    }
    else
    {
        gfloat cos_angle0, cos_angle1;

        cos_angle0 = (len0_sq + dist_sq - len1_sq) / (2.0f * len0 * dist);
        cos_angle0 = CLAMP (cos_angle0, -1.0f, 1.0f);
        angle0 = acosf (cos_angle0);

        cos_angle1 = (len0_sq + len1_sq - dist_sq) / (2.0f * len0 * len1);
        cos_angle1 = CLAMP (cos_angle1, -1.0f, 1.0f);
        angle1 = G_PI - acosf (cos_angle1);
    }

    /*
     * Build rotation quaternions.
     * First, find the plane defined by root, target, and pole.
     * Then apply rotations in that plane.
     */
    {
        gfloat to_target_x, to_target_y, to_target_z;
        gfloat to_pole_x, to_pole_y, to_pole_z;
        gfloat normal_x, normal_y, normal_z;
        gfloat half0, half1, s;

        /* Direction to target */
        to_target_x = target_x - root_x;
        to_target_y = target_y - root_y;
        to_target_z = target_z - root_z;
        vec3_normalize (&to_target_x, &to_target_y, &to_target_z);

        /* Direction to pole */
        to_pole_x = pole_x - root_x;
        to_pole_y = pole_y - root_y;
        to_pole_z = pole_z - root_z;
        vec3_normalize (&to_pole_x, &to_pole_y, &to_pole_z);

        /* Normal of the IK plane (cross product) */
        vec3_cross (to_target_x, to_target_y, to_target_z,
                    to_pole_x, to_pole_y, to_pole_z,
                    &normal_x, &normal_y, &normal_z);
        vec3_normalize (&normal_x, &normal_y, &normal_z);

        /*
         * Bone 0: rotate around normal by angle0, then align to target direction.
         * Simplified: create quaternion that rotates from default to target direction,
         * with angle0 offset.
         */
        half0 = angle0 * 0.5f;
        s = sinf (half0);

        lrg_bone_pose_set_identity (&pose0);
        pose0.rotation_w = cosf (half0);
        pose0.rotation_x = normal_x * s;
        pose0.rotation_y = normal_y * s;
        pose0.rotation_z = normal_z * s;

        /* Bone 1: rotate by negative angle1 (elbow bend) */
        half1 = (-angle1) * 0.5f;
        s = sinf (half1);

        lrg_bone_pose_set_identity (&pose1);
        pose1.rotation_w = cosf (half1);
        pose1.rotation_x = normal_x * s;
        pose1.rotation_y = normal_y * s;
        pose1.rotation_z = normal_z * s;
    }

    /* Apply to skeleton */
    bone_index0 = lrg_bone_get_index (bone0);
    bone_index1 = lrg_bone_get_index (bone1);

    lrg_skeleton_set_pose (skeleton, bone_index0, &pose0);
    lrg_skeleton_set_pose (skeleton, bone_index1, &pose1);
    lrg_skeleton_calculate_world_poses (skeleton);

    return TRUE;
}

static void
lrg_ik_solver_two_bone_class_init (LrgIKSolverTwoBoneClass *klass)
{
    LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

    solver_class->solve = lrg_ik_solver_two_bone_solve;
    solver_class->supports_chain_length = lrg_ik_solver_two_bone_supports;
}

static void
lrg_ik_solver_two_bone_init (LrgIKSolverTwoBone *self)
{
}

LrgIKSolverTwoBone *
lrg_ik_solver_two_bone_new (void)
{
    return g_object_new (LRG_TYPE_IK_SOLVER_TWO_BONE, NULL);
}

/*
 * ============================================================================
 * LrgIKSolverLookAt - Simple aim constraint
 * ============================================================================
 */

struct _LrgIKSolverLookAt
{
    LrgIKSolver parent_instance;

    gfloat up_x;
    gfloat up_y;
    gfloat up_z;
};

G_DEFINE_TYPE (LrgIKSolverLookAt, lrg_ik_solver_look_at, LRG_TYPE_IK_SOLVER)

enum
{
    LOOK_AT_PROP_0,
    LOOK_AT_PROP_UP_X,
    LOOK_AT_PROP_UP_Y,
    LOOK_AT_PROP_UP_Z,
    LOOK_AT_N_PROPS
};

static GParamSpec *look_at_properties[LOOK_AT_N_PROPS];

static gboolean
lrg_ik_solver_look_at_supports (LrgIKSolver *solver, guint bone_count)
{
    return bone_count == 1;
}

static gboolean
lrg_ik_solver_look_at_solve (LrgIKSolver *solver,
                             LrgIKChain  *chain,
                             guint        max_iterations,
                             gfloat       tolerance)
{
    LrgIKSolverLookAt *self = LRG_IK_SOLVER_LOOK_AT (solver);
    LrgSkeleton *skeleton;
    LrgBone *bone;
    const LrgBonePose *world_pose;
    gfloat target_x, target_y, target_z;
    gfloat bone_x, bone_y, bone_z;
    gfloat forward_x, forward_y, forward_z;
    gfloat right_x, right_y, right_z;
    gfloat up_x, up_y, up_z;
    LrgBonePose pose;
    gint bone_index;

    (void)max_iterations;
    (void)tolerance;

    skeleton = lrg_ik_chain_get_skeleton (chain);
    if (skeleton == NULL || lrg_ik_chain_get_bone_count (chain) != 1)
        return FALSE;

    bone = lrg_ik_chain_get_bone (chain, 0);
    if (bone == NULL)
        return FALSE;

    lrg_ik_chain_get_target_position (chain, &target_x, &target_y, &target_z);

    /* Get bone world position */
    world_pose = lrg_bone_get_world_pose (bone);
    bone_x = world_pose->position_x;
    bone_y = world_pose->position_y;
    bone_z = world_pose->position_z;

    /* Forward direction (towards target) */
    forward_x = target_x - bone_x;
    forward_y = target_y - bone_y;
    forward_z = target_z - bone_z;
    vec3_normalize (&forward_x, &forward_y, &forward_z);

    /* Get up vector */
    up_x = self->up_x;
    up_y = self->up_y;
    up_z = self->up_z;

    /* Right = forward x up */
    vec3_cross (forward_x, forward_y, forward_z,
                up_x, up_y, up_z,
                &right_x, &right_y, &right_z);
    vec3_normalize (&right_x, &right_y, &right_z);

    /* Recalculate up = right x forward */
    vec3_cross (right_x, right_y, right_z,
                forward_x, forward_y, forward_z,
                &up_x, &up_y, &up_z);
    vec3_normalize (&up_x, &up_y, &up_z);

    /*
     * Build rotation matrix and convert to quaternion.
     * Matrix columns are: right, up, forward
     */
    {
        gfloat m00, m01, m02;
        gfloat m10, m11, m12;
        gfloat m20, m21, m22;
        gfloat trace, s;
        gfloat qw, qx, qy, qz;

        m00 = right_x;   m01 = up_x;   m02 = forward_x;
        m10 = right_y;   m11 = up_y;   m12 = forward_y;
        m20 = right_z;   m21 = up_z;   m22 = forward_z;

        trace = m00 + m11 + m22;

        if (trace > 0.0f)
        {
            s = sqrtf (trace + 1.0f) * 2.0f;
            qw = 0.25f * s;
            qx = (m21 - m12) / s;
            qy = (m02 - m20) / s;
            qz = (m10 - m01) / s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            s = sqrtf (1.0f + m00 - m11 - m22) * 2.0f;
            qw = (m21 - m12) / s;
            qx = 0.25f * s;
            qy = (m01 + m10) / s;
            qz = (m02 + m20) / s;
        }
        else if (m11 > m22)
        {
            s = sqrtf (1.0f + m11 - m00 - m22) * 2.0f;
            qw = (m02 - m20) / s;
            qx = (m01 + m10) / s;
            qy = 0.25f * s;
            qz = (m12 + m21) / s;
        }
        else
        {
            s = sqrtf (1.0f + m22 - m00 - m11) * 2.0f;
            qw = (m10 - m01) / s;
            qx = (m02 + m20) / s;
            qy = (m12 + m21) / s;
            qz = 0.25f * s;
        }

        lrg_bone_pose_set_identity (&pose);
        pose.rotation_w = qw;
        pose.rotation_x = qx;
        pose.rotation_y = qy;
        pose.rotation_z = qz;
        lrg_bone_pose_normalize_rotation (&pose);
    }

    /* Apply to skeleton */
    bone_index = lrg_bone_get_index (bone);
    lrg_skeleton_set_pose (skeleton, bone_index, &pose);
    lrg_skeleton_calculate_world_poses (skeleton);

    return TRUE;
}

static void
lrg_ik_solver_look_at_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgIKSolverLookAt *self = LRG_IK_SOLVER_LOOK_AT (object);

    switch (prop_id)
    {
    case LOOK_AT_PROP_UP_X:
        g_value_set_float (value, self->up_x);
        break;
    case LOOK_AT_PROP_UP_Y:
        g_value_set_float (value, self->up_y);
        break;
    case LOOK_AT_PROP_UP_Z:
        g_value_set_float (value, self->up_z);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ik_solver_look_at_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgIKSolverLookAt *self = LRG_IK_SOLVER_LOOK_AT (object);

    switch (prop_id)
    {
    case LOOK_AT_PROP_UP_X:
        self->up_x = g_value_get_float (value);
        break;
    case LOOK_AT_PROP_UP_Y:
        self->up_y = g_value_get_float (value);
        break;
    case LOOK_AT_PROP_UP_Z:
        self->up_z = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ik_solver_look_at_class_init (LrgIKSolverLookAtClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

    object_class->get_property = lrg_ik_solver_look_at_get_property;
    object_class->set_property = lrg_ik_solver_look_at_set_property;

    solver_class->solve = lrg_ik_solver_look_at_solve;
    solver_class->supports_chain_length = lrg_ik_solver_look_at_supports;

    look_at_properties[LOOK_AT_PROP_UP_X] =
        g_param_spec_float ("up-x", "Up X", "Up vector X",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    look_at_properties[LOOK_AT_PROP_UP_Y] =
        g_param_spec_float ("up-y", "Up Y", "Up vector Y",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    look_at_properties[LOOK_AT_PROP_UP_Z] =
        g_param_spec_float ("up-z", "Up Z", "Up vector Z",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, LOOK_AT_N_PROPS, look_at_properties);
}

static void
lrg_ik_solver_look_at_init (LrgIKSolverLookAt *self)
{
    /* Default up vector is +Y */
    self->up_x = 0.0f;
    self->up_y = 1.0f;
    self->up_z = 0.0f;
}

LrgIKSolverLookAt *
lrg_ik_solver_look_at_new (void)
{
    return g_object_new (LRG_TYPE_IK_SOLVER_LOOK_AT, NULL);
}

void
lrg_ik_solver_look_at_get_up_vector (LrgIKSolverLookAt *self,
                                     gfloat            *x,
                                     gfloat            *y,
                                     gfloat            *z)
{
    g_return_if_fail (LRG_IS_IK_SOLVER_LOOK_AT (self));

    if (x != NULL)
        *x = self->up_x;
    if (y != NULL)
        *y = self->up_y;
    if (z != NULL)
        *z = self->up_z;
}

void
lrg_ik_solver_look_at_set_up_vector (LrgIKSolverLookAt *self,
                                     gfloat             x,
                                     gfloat             y,
                                     gfloat             z)
{
    g_return_if_fail (LRG_IS_IK_SOLVER_LOOK_AT (self));

    self->up_x = x;
    self->up_y = y;
    self->up_z = z;
}
