/* lrg-animation-keyframe.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation keyframe with time, pose, and tangent data.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-bone-pose.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATION_KEYFRAME (lrg_animation_keyframe_get_type ())

typedef struct _LrgAnimationKeyframe LrgAnimationKeyframe;

/**
 * LrgAnimationKeyframe:
 *
 * A keyframe in an animation track containing time, pose,
 * and tangent information for smooth interpolation.
 *
 * Since: 1.0
 */
struct _LrgAnimationKeyframe
{
    /*< public >*/
    gfloat       time;            /* Time in seconds */
    LrgBonePose  pose;            /* Transform at this keyframe */

    /* Tangents for cubic interpolation */
    gfloat       in_tangent_x;
    gfloat       in_tangent_y;
    gfloat       in_tangent_z;
    gfloat       out_tangent_x;
    gfloat       out_tangent_y;
    gfloat       out_tangent_z;

    /* Rotation tangents (quaternion) */
    gfloat       in_tangent_qx;
    gfloat       in_tangent_qy;
    gfloat       in_tangent_qz;
    gfloat       in_tangent_qw;
    gfloat       out_tangent_qx;
    gfloat       out_tangent_qy;
    gfloat       out_tangent_qz;
    gfloat       out_tangent_qw;

    /* Scale tangents */
    gfloat       in_tangent_sx;
    gfloat       in_tangent_sy;
    gfloat       in_tangent_sz;
    gfloat       out_tangent_sx;
    gfloat       out_tangent_sy;
    gfloat       out_tangent_sz;
};

LRG_AVAILABLE_IN_ALL
GType                   lrg_animation_keyframe_get_type     (void) G_GNUC_CONST;

/**
 * lrg_animation_keyframe_new:
 * @time: The time in seconds for this keyframe
 *
 * Creates a new animation keyframe at the specified time
 * with identity pose.
 *
 * Returns: (transfer full): A new #LrgAnimationKeyframe
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationKeyframe *  lrg_animation_keyframe_new          (gfloat          time);

/**
 * lrg_animation_keyframe_new_with_pose:
 * @time: The time in seconds for this keyframe
 * @pose: The bone pose at this keyframe
 *
 * Creates a new animation keyframe with the specified pose.
 *
 * Returns: (transfer full): A new #LrgAnimationKeyframe
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationKeyframe *  lrg_animation_keyframe_new_with_pose (gfloat              time,
                                                              const LrgBonePose  *pose);

/**
 * lrg_animation_keyframe_copy:
 * @keyframe: A #LrgAnimationKeyframe
 *
 * Creates a deep copy of the keyframe.
 *
 * Returns: (transfer full): A new #LrgAnimationKeyframe
 */
LRG_AVAILABLE_IN_ALL
LrgAnimationKeyframe *  lrg_animation_keyframe_copy         (const LrgAnimationKeyframe *keyframe);

/**
 * lrg_animation_keyframe_free:
 * @keyframe: A #LrgAnimationKeyframe
 *
 * Frees a keyframe.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_keyframe_free         (LrgAnimationKeyframe *keyframe);

/**
 * lrg_animation_keyframe_set_linear_tangents:
 * @keyframe: A #LrgAnimationKeyframe
 *
 * Sets tangents for linear interpolation.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_keyframe_set_linear_tangents (LrgAnimationKeyframe *keyframe);

/**
 * lrg_animation_keyframe_set_smooth_tangents:
 * @keyframe: A #LrgAnimationKeyframe
 * @prev: (nullable): The previous keyframe
 * @next: (nullable): The next keyframe
 *
 * Calculates smooth (Catmull-Rom) tangents based on
 * neighboring keyframes.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_keyframe_set_smooth_tangents (LrgAnimationKeyframe       *keyframe,
                                                                    const LrgAnimationKeyframe *prev,
                                                                    const LrgAnimationKeyframe *next);

/**
 * lrg_animation_keyframe_lerp:
 * @a: First keyframe
 * @b: Second keyframe
 * @t: Interpolation factor (0.0 to 1.0)
 * @out: (out): Output pose
 *
 * Performs linear interpolation between two keyframes.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_keyframe_lerp         (const LrgAnimationKeyframe *a,
                                                             const LrgAnimationKeyframe *b,
                                                             gfloat                      t,
                                                             LrgBonePose                *out);

/**
 * lrg_animation_keyframe_cubic:
 * @a: First keyframe
 * @b: Second keyframe
 * @t: Interpolation factor (0.0 to 1.0)
 * @out: (out): Output pose
 *
 * Performs cubic (Hermite) interpolation using tangents.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_animation_keyframe_cubic        (const LrgAnimationKeyframe *a,
                                                             const LrgAnimationKeyframe *b,
                                                             gfloat                      t,
                                                             LrgBonePose                *out);

G_END_DECLS
