/* lrg-bone-pose.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bone-pose.h"
#include <math.h>

/**
 * SECTION:lrg-bone-pose
 * @Title: LrgBonePose
 * @Short_description: Bone transformation data
 *
 * #LrgBonePose represents a bone's local transformation in a skeleton.
 * It stores position (translation), rotation (as a quaternion), and scale.
 *
 * The quaternion representation for rotation enables smooth interpolation
 * between poses without gimbal lock issues.
 */

G_DEFINE_BOXED_TYPE (LrgBonePose, lrg_bone_pose,
                     lrg_bone_pose_copy, lrg_bone_pose_free)

LrgBonePose *
lrg_bone_pose_new (void)
{
    LrgBonePose *self;

    self = g_new0 (LrgBonePose, 1);
    lrg_bone_pose_set_identity (self);

    return self;
}

LrgBonePose *
lrg_bone_pose_new_with_values (gfloat px, gfloat py, gfloat pz,
                               gfloat rx, gfloat ry, gfloat rz, gfloat rw,
                               gfloat sx, gfloat sy, gfloat sz)
{
    LrgBonePose *self;

    self = g_new (LrgBonePose, 1);
    self->position_x = px;
    self->position_y = py;
    self->position_z = pz;
    self->rotation_x = rx;
    self->rotation_y = ry;
    self->rotation_z = rz;
    self->rotation_w = rw;
    self->scale_x = sx;
    self->scale_y = sy;
    self->scale_z = sz;

    return self;
}

LrgBonePose *
lrg_bone_pose_copy (const LrgBonePose *self)
{
    LrgBonePose *copy;

    if (self == NULL)
        return NULL;

    copy = g_new (LrgBonePose, 1);
    *copy = *self;

    return copy;
}

void
lrg_bone_pose_free (LrgBonePose *self)
{
    g_free (self);
}

void
lrg_bone_pose_set_identity (LrgBonePose *self)
{
    g_return_if_fail (self != NULL);

    self->position_x = 0.0f;
    self->position_y = 0.0f;
    self->position_z = 0.0f;
    self->rotation_x = 0.0f;
    self->rotation_y = 0.0f;
    self->rotation_z = 0.0f;
    self->rotation_w = 1.0f;  /* Identity quaternion */
    self->scale_x = 1.0f;
    self->scale_y = 1.0f;
    self->scale_z = 1.0f;
}

void
lrg_bone_pose_set_position (LrgBonePose *self,
                            gfloat       x,
                            gfloat       y,
                            gfloat       z)
{
    g_return_if_fail (self != NULL);

    self->position_x = x;
    self->position_y = y;
    self->position_z = z;
}

void
lrg_bone_pose_set_rotation (LrgBonePose *self,
                            gfloat       x,
                            gfloat       y,
                            gfloat       z,
                            gfloat       w)
{
    g_return_if_fail (self != NULL);

    self->rotation_x = x;
    self->rotation_y = y;
    self->rotation_z = z;
    self->rotation_w = w;
}

void
lrg_bone_pose_set_rotation_euler (LrgBonePose *self,
                                  gfloat       pitch,
                                  gfloat       yaw,
                                  gfloat       roll)
{
    gfloat cy, sy, cp, sp, cr, sr;
    gfloat qx, qy, qz, qw;

    g_return_if_fail (self != NULL);

    /* Convert Euler angles to quaternion */
    cy = cosf (yaw * 0.5f);
    sy = sinf (yaw * 0.5f);
    cp = cosf (pitch * 0.5f);
    sp = sinf (pitch * 0.5f);
    cr = cosf (roll * 0.5f);
    sr = sinf (roll * 0.5f);

    qw = cr * cp * cy + sr * sp * sy;
    qx = sr * cp * cy - cr * sp * sy;
    qy = cr * sp * cy + sr * cp * sy;
    qz = cr * cp * sy - sr * sp * cy;

    self->rotation_x = qx;
    self->rotation_y = qy;
    self->rotation_z = qz;
    self->rotation_w = qw;
}

void
lrg_bone_pose_set_scale (LrgBonePose *self,
                         gfloat       x,
                         gfloat       y,
                         gfloat       z)
{
    g_return_if_fail (self != NULL);

    self->scale_x = x;
    self->scale_y = y;
    self->scale_z = z;
}

void
lrg_bone_pose_set_uniform_scale (LrgBonePose *self,
                                 gfloat       scale)
{
    g_return_if_fail (self != NULL);

    self->scale_x = scale;
    self->scale_y = scale;
    self->scale_z = scale;
}

/* Quaternion slerp helper */
static void
quat_slerp (gfloat ax, gfloat ay, gfloat az, gfloat aw,
            gfloat bx, gfloat by, gfloat bz, gfloat bw,
            gfloat t,
            gfloat *rx, gfloat *ry, gfloat *rz, gfloat *rw)
{
    gfloat dot;
    gfloat scale0, scale1;
    gfloat omega, sin_omega;

    /* Compute dot product */
    dot = ax * bx + ay * by + az * bz + aw * bw;

    /* If dot is negative, negate one quaternion to take shortest path */
    if (dot < 0.0f)
    {
        dot = -dot;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }

    /* If quaternions are very close, use linear interpolation */
    if (dot > 0.9995f)
    {
        scale0 = 1.0f - t;
        scale1 = t;
    }
    else
    {
        omega = acosf (dot);
        sin_omega = sinf (omega);
        scale0 = sinf ((1.0f - t) * omega) / sin_omega;
        scale1 = sinf (t * omega) / sin_omega;
    }

    *rx = scale0 * ax + scale1 * bx;
    *ry = scale0 * ay + scale1 * by;
    *rz = scale0 * az + scale1 * bz;
    *rw = scale0 * aw + scale1 * bw;
}

LrgBonePose *
lrg_bone_pose_lerp (const LrgBonePose *a,
                    const LrgBonePose *b,
                    gfloat             t)
{
    LrgBonePose *result;

    g_return_val_if_fail (a != NULL, NULL);
    g_return_val_if_fail (b != NULL, NULL);

    result = lrg_bone_pose_new ();
    lrg_bone_pose_lerp_to (a, b, t, result);

    return result;
}

void
lrg_bone_pose_lerp_to (const LrgBonePose *a,
                       const LrgBonePose *b,
                       gfloat             t,
                       LrgBonePose       *result)
{
    gfloat one_minus_t;

    g_return_if_fail (a != NULL);
    g_return_if_fail (b != NULL);
    g_return_if_fail (result != NULL);

    t = CLAMP (t, 0.0f, 1.0f);
    one_minus_t = 1.0f - t;

    /* Lerp position */
    result->position_x = a->position_x * one_minus_t + b->position_x * t;
    result->position_y = a->position_y * one_minus_t + b->position_y * t;
    result->position_z = a->position_z * one_minus_t + b->position_z * t;

    /* Slerp rotation */
    quat_slerp (a->rotation_x, a->rotation_y, a->rotation_z, a->rotation_w,
                b->rotation_x, b->rotation_y, b->rotation_z, b->rotation_w,
                t,
                &result->rotation_x, &result->rotation_y,
                &result->rotation_z, &result->rotation_w);

    /* Lerp scale */
    result->scale_x = a->scale_x * one_minus_t + b->scale_x * t;
    result->scale_y = a->scale_y * one_minus_t + b->scale_y * t;
    result->scale_z = a->scale_z * one_minus_t + b->scale_z * t;
}

LrgBonePose *
lrg_bone_pose_blend (const LrgBonePose *a,
                     const LrgBonePose *b,
                     gfloat             weight)
{
    /* For additive blending, we lerp toward b with the given weight */
    return lrg_bone_pose_lerp (a, b, weight);
}

/* Quaternion multiplication helper */
static void
quat_multiply (gfloat ax, gfloat ay, gfloat az, gfloat aw,
               gfloat bx, gfloat by, gfloat bz, gfloat bw,
               gfloat *rx, gfloat *ry, gfloat *rz, gfloat *rw)
{
    *rw = aw * bw - ax * bx - ay * by - az * bz;
    *rx = aw * bx + ax * bw + ay * bz - az * by;
    *ry = aw * by - ax * bz + ay * bw + az * bx;
    *rz = aw * bz + ax * by - ay * bx + az * bw;
}

LrgBonePose *
lrg_bone_pose_multiply (const LrgBonePose *parent,
                        const LrgBonePose *local)
{
    LrgBonePose *result;
    gfloat px, py, pz;

    g_return_val_if_fail (parent != NULL, NULL);
    g_return_val_if_fail (local != NULL, NULL);

    result = lrg_bone_pose_new ();

    /* Multiply rotations */
    quat_multiply (parent->rotation_x, parent->rotation_y,
                   parent->rotation_z, parent->rotation_w,
                   local->rotation_x, local->rotation_y,
                   local->rotation_z, local->rotation_w,
                   &result->rotation_x, &result->rotation_y,
                   &result->rotation_z, &result->rotation_w);

    /* Rotate local position by parent rotation, then add parent position */
    /* This is a simplified version - full implementation would use quaternion rotation */
    px = local->position_x * parent->scale_x;
    py = local->position_y * parent->scale_y;
    pz = local->position_z * parent->scale_z;

    result->position_x = parent->position_x + px;
    result->position_y = parent->position_y + py;
    result->position_z = parent->position_z + pz;

    /* Multiply scales */
    result->scale_x = parent->scale_x * local->scale_x;
    result->scale_y = parent->scale_y * local->scale_y;
    result->scale_z = parent->scale_z * local->scale_z;

    return result;
}

void
lrg_bone_pose_normalize_rotation (LrgBonePose *self)
{
    gfloat len;

    g_return_if_fail (self != NULL);

    len = sqrtf (self->rotation_x * self->rotation_x +
                 self->rotation_y * self->rotation_y +
                 self->rotation_z * self->rotation_z +
                 self->rotation_w * self->rotation_w);

    if (len > 0.0f)
    {
        self->rotation_x /= len;
        self->rotation_y /= len;
        self->rotation_z /= len;
        self->rotation_w /= len;
    }
    else
    {
        /* Reset to identity quaternion */
        self->rotation_x = 0.0f;
        self->rotation_y = 0.0f;
        self->rotation_z = 0.0f;
        self->rotation_w = 1.0f;
    }
}

gboolean
lrg_bone_pose_equal (const LrgBonePose *a,
                     const LrgBonePose *b)
{
    if (a == b)
        return TRUE;

    if (a == NULL || b == NULL)
        return FALSE;

    return (a->position_x == b->position_x &&
            a->position_y == b->position_y &&
            a->position_z == b->position_z &&
            a->rotation_x == b->rotation_x &&
            a->rotation_y == b->rotation_y &&
            a->rotation_z == b->rotation_z &&
            a->rotation_w == b->rotation_w &&
            a->scale_x == b->scale_x &&
            a->scale_y == b->scale_y &&
            a->scale_z == b->scale_z);
}
