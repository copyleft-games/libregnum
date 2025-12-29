/* lrg-animation-keyframe.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-keyframe.h"
#include <math.h>

/**
 * SECTION:lrg-animation-keyframe
 * @Title: LrgAnimationKeyframe
 * @Short_description: Animation keyframe data
 *
 * #LrgAnimationKeyframe represents a single keyframe in an animation
 * track. It contains the time, pose data, and tangent information
 * for smooth interpolation between keyframes.
 */

G_DEFINE_BOXED_TYPE (LrgAnimationKeyframe, lrg_animation_keyframe,
                     lrg_animation_keyframe_copy,
                     lrg_animation_keyframe_free)

LrgAnimationKeyframe *
lrg_animation_keyframe_new (gfloat time)
{
    LrgAnimationKeyframe *keyframe;

    keyframe = g_slice_new0 (LrgAnimationKeyframe);
    keyframe->time = time;

    /* Initialize pose to identity */
    lrg_bone_pose_set_identity (&keyframe->pose);

    /* Linear tangents by default */
    lrg_animation_keyframe_set_linear_tangents (keyframe);

    return keyframe;
}

LrgAnimationKeyframe *
lrg_animation_keyframe_new_with_pose (gfloat              time,
                                      const LrgBonePose  *pose)
{
    LrgAnimationKeyframe *keyframe;

    g_return_val_if_fail (pose != NULL, NULL);

    keyframe = g_slice_new0 (LrgAnimationKeyframe);
    keyframe->time = time;
    keyframe->pose = *pose;

    lrg_animation_keyframe_set_linear_tangents (keyframe);

    return keyframe;
}

LrgAnimationKeyframe *
lrg_animation_keyframe_copy (const LrgAnimationKeyframe *keyframe)
{
    LrgAnimationKeyframe *copy;

    g_return_val_if_fail (keyframe != NULL, NULL);

    copy = g_slice_new (LrgAnimationKeyframe);
    *copy = *keyframe;

    return copy;
}

void
lrg_animation_keyframe_free (LrgAnimationKeyframe *keyframe)
{
    g_return_if_fail (keyframe != NULL);

    g_slice_free (LrgAnimationKeyframe, keyframe);
}

void
lrg_animation_keyframe_set_linear_tangents (LrgAnimationKeyframe *keyframe)
{
    g_return_if_fail (keyframe != NULL);

    /* Zero tangents for linear interpolation */
    keyframe->in_tangent_x = 0.0f;
    keyframe->in_tangent_y = 0.0f;
    keyframe->in_tangent_z = 0.0f;
    keyframe->out_tangent_x = 0.0f;
    keyframe->out_tangent_y = 0.0f;
    keyframe->out_tangent_z = 0.0f;

    keyframe->in_tangent_qx = 0.0f;
    keyframe->in_tangent_qy = 0.0f;
    keyframe->in_tangent_qz = 0.0f;
    keyframe->in_tangent_qw = 0.0f;
    keyframe->out_tangent_qx = 0.0f;
    keyframe->out_tangent_qy = 0.0f;
    keyframe->out_tangent_qz = 0.0f;
    keyframe->out_tangent_qw = 0.0f;

    keyframe->in_tangent_sx = 0.0f;
    keyframe->in_tangent_sy = 0.0f;
    keyframe->in_tangent_sz = 0.0f;
    keyframe->out_tangent_sx = 0.0f;
    keyframe->out_tangent_sy = 0.0f;
    keyframe->out_tangent_sz = 0.0f;
}

void
lrg_animation_keyframe_set_smooth_tangents (LrgAnimationKeyframe       *keyframe,
                                            const LrgAnimationKeyframe *prev,
                                            const LrgAnimationKeyframe *next)
{
    g_return_if_fail (keyframe != NULL);

    /*
     * Calculate Catmull-Rom tangents:
     * tangent = (next.value - prev.value) / (next.time - prev.time)
     *
     * For endpoints, use the single neighbor or zero.
     */

    if (prev != NULL && next != NULL)
    {
        gfloat dt;

        dt = next->time - prev->time;
        if (dt > 0.0001f)
        {
            /* Position tangents */
            keyframe->in_tangent_x = (next->pose.position_x - prev->pose.position_x) / dt;
            keyframe->in_tangent_y = (next->pose.position_y - prev->pose.position_y) / dt;
            keyframe->in_tangent_z = (next->pose.position_z - prev->pose.position_z) / dt;
            keyframe->out_tangent_x = keyframe->in_tangent_x;
            keyframe->out_tangent_y = keyframe->in_tangent_y;
            keyframe->out_tangent_z = keyframe->in_tangent_z;

            /* Scale tangents */
            keyframe->in_tangent_sx = (next->pose.scale_x - prev->pose.scale_x) / dt;
            keyframe->in_tangent_sy = (next->pose.scale_y - prev->pose.scale_y) / dt;
            keyframe->in_tangent_sz = (next->pose.scale_z - prev->pose.scale_z) / dt;
            keyframe->out_tangent_sx = keyframe->in_tangent_sx;
            keyframe->out_tangent_sy = keyframe->in_tangent_sy;
            keyframe->out_tangent_sz = keyframe->in_tangent_sz;

            /* Rotation tangents (simplified - quaternion derivative) */
            keyframe->in_tangent_qx = (next->pose.rotation_x - prev->pose.rotation_x) / dt;
            keyframe->in_tangent_qy = (next->pose.rotation_y - prev->pose.rotation_y) / dt;
            keyframe->in_tangent_qz = (next->pose.rotation_z - prev->pose.rotation_z) / dt;
            keyframe->in_tangent_qw = (next->pose.rotation_w - prev->pose.rotation_w) / dt;
            keyframe->out_tangent_qx = keyframe->in_tangent_qx;
            keyframe->out_tangent_qy = keyframe->in_tangent_qy;
            keyframe->out_tangent_qz = keyframe->in_tangent_qz;
            keyframe->out_tangent_qw = keyframe->in_tangent_qw;
        }
    }
    else if (prev != NULL)
    {
        gfloat dt;

        dt = keyframe->time - prev->time;
        if (dt > 0.0001f)
        {
            keyframe->in_tangent_x = (keyframe->pose.position_x - prev->pose.position_x) / dt;
            keyframe->in_tangent_y = (keyframe->pose.position_y - prev->pose.position_y) / dt;
            keyframe->in_tangent_z = (keyframe->pose.position_z - prev->pose.position_z) / dt;
            keyframe->out_tangent_x = keyframe->in_tangent_x;
            keyframe->out_tangent_y = keyframe->in_tangent_y;
            keyframe->out_tangent_z = keyframe->in_tangent_z;
        }
    }
    else if (next != NULL)
    {
        gfloat dt;

        dt = next->time - keyframe->time;
        if (dt > 0.0001f)
        {
            keyframe->out_tangent_x = (next->pose.position_x - keyframe->pose.position_x) / dt;
            keyframe->out_tangent_y = (next->pose.position_y - keyframe->pose.position_y) / dt;
            keyframe->out_tangent_z = (next->pose.position_z - keyframe->pose.position_z) / dt;
            keyframe->in_tangent_x = keyframe->out_tangent_x;
            keyframe->in_tangent_y = keyframe->out_tangent_y;
            keyframe->in_tangent_z = keyframe->out_tangent_z;
        }
    }
}

void
lrg_animation_keyframe_lerp (const LrgAnimationKeyframe *a,
                             const LrgAnimationKeyframe *b,
                             gfloat                      t,
                             LrgBonePose                *out)
{
    g_return_if_fail (a != NULL);
    g_return_if_fail (b != NULL);
    g_return_if_fail (out != NULL);

    lrg_bone_pose_lerp_to (&a->pose, &b->pose, t, out);
}

/*
 * Hermite interpolation helpers
 */
static gfloat
hermite_h00 (gfloat t)
{
    return 2.0f * t * t * t - 3.0f * t * t + 1.0f;
}

static gfloat
hermite_h10 (gfloat t)
{
    return t * t * t - 2.0f * t * t + t;
}

static gfloat
hermite_h01 (gfloat t)
{
    return -2.0f * t * t * t + 3.0f * t * t;
}

static gfloat
hermite_h11 (gfloat t)
{
    return t * t * t - t * t;
}

static gfloat
hermite_interp (gfloat p0,
                gfloat m0,
                gfloat p1,
                gfloat m1,
                gfloat t,
                gfloat dt)
{
    /*
     * Hermite spline:
     * p(t) = h00(t)*p0 + h10(t)*m0*dt + h01(t)*p1 + h11(t)*m1*dt
     */
    return hermite_h00 (t) * p0 +
           hermite_h10 (t) * m0 * dt +
           hermite_h01 (t) * p1 +
           hermite_h11 (t) * m1 * dt;
}

void
lrg_animation_keyframe_cubic (const LrgAnimationKeyframe *a,
                              const LrgAnimationKeyframe *b,
                              gfloat                      t,
                              LrgBonePose                *out)
{
    gfloat dt;

    g_return_if_fail (a != NULL);
    g_return_if_fail (b != NULL);
    g_return_if_fail (out != NULL);

    dt = b->time - a->time;
    if (dt < 0.0001f)
    {
        *out = a->pose;
        return;
    }

    /* Position with cubic interpolation */
    out->position_x = hermite_interp (a->pose.position_x, a->out_tangent_x,
                                      b->pose.position_x, b->in_tangent_x, t, dt);
    out->position_y = hermite_interp (a->pose.position_y, a->out_tangent_y,
                                      b->pose.position_y, b->in_tangent_y, t, dt);
    out->position_z = hermite_interp (a->pose.position_z, a->out_tangent_z,
                                      b->pose.position_z, b->in_tangent_z, t, dt);

    /* Scale with cubic interpolation */
    out->scale_x = hermite_interp (a->pose.scale_x, a->out_tangent_sx,
                                   b->pose.scale_x, b->in_tangent_sx, t, dt);
    out->scale_y = hermite_interp (a->pose.scale_y, a->out_tangent_sy,
                                   b->pose.scale_y, b->in_tangent_sy, t, dt);
    out->scale_z = hermite_interp (a->pose.scale_z, a->out_tangent_sz,
                                   b->pose.scale_z, b->in_tangent_sz, t, dt);

    /*
     * Rotation: Use SLERP instead of cubic interpolation
     * Cubic quaternion interpolation can produce non-unit quaternions
     */
    {
        gfloat dot;
        gfloat ax, ay, az, aw;
        gfloat bx, by, bz, bw;
        gfloat scale_a, scale_b;
        gfloat len;

        ax = a->pose.rotation_x;
        ay = a->pose.rotation_y;
        az = a->pose.rotation_z;
        aw = a->pose.rotation_w;

        bx = b->pose.rotation_x;
        by = b->pose.rotation_y;
        bz = b->pose.rotation_z;
        bw = b->pose.rotation_w;

        /* Compute dot product */
        dot = ax * bx + ay * by + az * bz + aw * bw;

        /* If negative dot, negate one quaternion to take shorter path */
        if (dot < 0.0f)
        {
            bx = -bx;
            by = -by;
            bz = -bz;
            bw = -bw;
            dot = -dot;
        }

        /* If quaternions are nearly parallel, use linear interpolation */
        if (dot > 0.9995f)
        {
            scale_a = 1.0f - t;
            scale_b = t;
        }
        else
        {
            gfloat theta;
            gfloat sin_theta;

            theta = acosf (dot);
            sin_theta = sinf (theta);
            scale_a = sinf ((1.0f - t) * theta) / sin_theta;
            scale_b = sinf (t * theta) / sin_theta;
        }

        out->rotation_x = scale_a * ax + scale_b * bx;
        out->rotation_y = scale_a * ay + scale_b * by;
        out->rotation_z = scale_a * az + scale_b * bz;
        out->rotation_w = scale_a * aw + scale_b * bw;

        /* Normalize */
        len = sqrtf (out->rotation_x * out->rotation_x +
                     out->rotation_y * out->rotation_y +
                     out->rotation_z * out->rotation_z +
                     out->rotation_w * out->rotation_w);
        if (len > 0.0001f)
        {
            out->rotation_x /= len;
            out->rotation_y /= len;
            out->rotation_z /= len;
            out->rotation_w /= len;
        }
    }
}
