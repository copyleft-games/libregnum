/* lrg-bone-pose.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bone-pose.h"
#include "lrg-bone-pose-private.h"
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

/* quat_rotate:
 * Rotates (x, y, z) by the unit quaternion q: v + 2w(u x v) + 2u x (u x v). */
static void
quat_rotate (gfloat qx, gfloat qy, gfloat qz, gfloat qw,
             gfloat *x, gfloat *y, gfloat *z)
{
    gfloat tx, ty, tz;

    tx = 2.0f * (qy * *z - qz * *y);
    ty = 2.0f * (qz * *x - qx * *z);
    tz = 2.0f * (qx * *y - qy * *x);
    *x += qw * tx + (qy * tz - qz * ty);
    *y += qw * ty + (qz * tx - qx * tz);
    *z += qw * tz + (qx * ty - qy * tx);
}

void
lrg_bone_pose_compose_into (const LrgBonePose *parent,
                            const LrgBonePose *local,
                            LrgBonePose       *result)
{
    LrgBonePose  out;
    gfloat       px, py, pz;
    gfloat       len;
    gfloat       qx, qy, qz, qw;

    /* Built in a local so @result may alias either input. */
    quat_multiply (parent->rotation_x, parent->rotation_y,
                   parent->rotation_z, parent->rotation_w,
                   local->rotation_x, local->rotation_y,
                   local->rotation_z, local->rotation_w,
                   &out.rotation_x, &out.rotation_y,
                   &out.rotation_z, &out.rotation_w);
    lrg_bone_pose_normalize_rotation (&out);

    /* the child's offset lives in the parent's scaled, rotated frame */
    qx = parent->rotation_x;
    qy = parent->rotation_y;
    qz = parent->rotation_z;
    qw = parent->rotation_w;
    len = sqrtf (qx * qx + qy * qy + qz * qz + qw * qw);
    if (len > 1e-8f)
    {
        qx /= len;
        qy /= len;
        qz /= len;
        qw /= len;
    }
    px = local->position_x * parent->scale_x;
    py = local->position_y * parent->scale_y;
    pz = local->position_z * parent->scale_z;
    quat_rotate (qx, qy, qz, qw, &px, &py, &pz);
    out.position_x = parent->position_x + px;
    out.position_y = parent->position_y + py;
    out.position_z = parent->position_z + pz;

    out.scale_x = parent->scale_x * local->scale_x;
    out.scale_y = parent->scale_y * local->scale_y;
    out.scale_z = parent->scale_z * local->scale_z;
    *result = out;
}

LrgBonePose *
lrg_bone_pose_compose (const LrgBonePose *parent,
                       const LrgBonePose *local)
{
    LrgBonePose *result;

    g_return_val_if_fail (parent != NULL, NULL);
    g_return_val_if_fail (local != NULL, NULL);

    result = g_new (LrgBonePose, 1);
    lrg_bone_pose_compose_into (parent, local, result);
    return result;
}

void
lrg_bone_pose_to_matrix (const LrgBonePose *self,
                         gfloat            *out_matrix)
{
    gfloat x, y, z, w;
    gfloat len;
    gfloat *m;

    g_return_if_fail (self != NULL);
    g_return_if_fail (out_matrix != NULL);

    x = self->rotation_x;
    y = self->rotation_y;
    z = self->rotation_z;
    w = self->rotation_w;
    len = sqrtf (x * x + y * y + z * z + w * w);
    if (len > 1e-8f)
    {
        x /= len;
        y /= len;
        z /= len;
        w /= len;
    }
    else
    {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        w = 1.0f;
    }

    /* column-major rotation columns, each scaled by its axis' scale */
    m = out_matrix;
    m[0] = (1.0f - 2.0f * (y * y + z * z)) * self->scale_x;
    m[1] = (2.0f * (x * y + w * z)) * self->scale_x;
    m[2] = (2.0f * (x * z - w * y)) * self->scale_x;
    m[3] = 0.0f;
    m[4] = (2.0f * (x * y - w * z)) * self->scale_y;
    m[5] = (1.0f - 2.0f * (x * x + z * z)) * self->scale_y;
    m[6] = (2.0f * (y * z + w * x)) * self->scale_y;
    m[7] = 0.0f;
    m[8] = (2.0f * (x * z + w * y)) * self->scale_z;
    m[9] = (2.0f * (y * z - w * x)) * self->scale_z;
    m[10] = (1.0f - 2.0f * (x * x + y * y)) * self->scale_z;
    m[11] = 0.0f;
    m[12] = self->position_x;
    m[13] = self->position_y;
    m[14] = self->position_z;
    m[15] = 1.0f;
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
