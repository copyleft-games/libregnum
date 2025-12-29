/* lrg-bone-pose.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bone transformation data for skeletal animation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

typedef struct _LrgBonePose LrgBonePose;

/**
 * LrgBonePose:
 * @position_x: X translation
 * @position_y: Y translation
 * @position_z: Z translation
 * @rotation_x: X component of rotation quaternion
 * @rotation_y: Y component of rotation quaternion
 * @rotation_z: Z component of rotation quaternion
 * @rotation_w: W component of rotation quaternion
 * @scale_x: X scale factor
 * @scale_y: Y scale factor
 * @scale_z: Z scale factor
 *
 * Represents a bone's local transformation (position, rotation, scale).
 * Rotation is stored as a quaternion for smooth interpolation.
 */
struct _LrgBonePose
{
    /* Position (translation) */
    gfloat position_x;
    gfloat position_y;
    gfloat position_z;

    /* Rotation (quaternion) */
    gfloat rotation_x;
    gfloat rotation_y;
    gfloat rotation_z;
    gfloat rotation_w;

    /* Scale */
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale_z;

    /*< private >*/
    gpointer _reserved[2];
};

#define LRG_TYPE_BONE_POSE (lrg_bone_pose_get_type ())

LRG_AVAILABLE_IN_ALL
GType           lrg_bone_pose_get_type          (void) G_GNUC_CONST;

/**
 * lrg_bone_pose_new:
 *
 * Creates a new bone pose with identity transformation
 * (position = 0, rotation = identity, scale = 1).
 *
 * Returns: (transfer full): A newly allocated #LrgBonePose
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_new               (void);

/**
 * lrg_bone_pose_new_with_values:
 * @px: X position
 * @py: Y position
 * @pz: Z position
 * @rx: X rotation (quaternion)
 * @ry: Y rotation (quaternion)
 * @rz: Z rotation (quaternion)
 * @rw: W rotation (quaternion)
 * @sx: X scale
 * @sy: Y scale
 * @sz: Z scale
 *
 * Creates a new bone pose with specified values.
 *
 * Returns: (transfer full): A newly allocated #LrgBonePose
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_new_with_values   (gfloat              px,
                                                 gfloat              py,
                                                 gfloat              pz,
                                                 gfloat              rx,
                                                 gfloat              ry,
                                                 gfloat              rz,
                                                 gfloat              rw,
                                                 gfloat              sx,
                                                 gfloat              sy,
                                                 gfloat              sz);

/**
 * lrg_bone_pose_copy:
 * @self: (nullable): A #LrgBonePose
 *
 * Creates a copy of the bone pose.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_copy              (const LrgBonePose  *self);

/**
 * lrg_bone_pose_free:
 * @self: (nullable): A #LrgBonePose
 *
 * Frees a bone pose.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_free              (LrgBonePose        *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBonePose, lrg_bone_pose_free)

/**
 * lrg_bone_pose_set_identity:
 * @self: A #LrgBonePose
 *
 * Resets the pose to identity (no transformation).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_identity      (LrgBonePose        *self);

/**
 * lrg_bone_pose_set_position:
 * @self: A #LrgBonePose
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the position component.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_position      (LrgBonePose        *self,
                                                 gfloat              x,
                                                 gfloat              y,
                                                 gfloat              z);

/**
 * lrg_bone_pose_set_rotation:
 * @self: A #LrgBonePose
 * @x: X quaternion component
 * @y: Y quaternion component
 * @z: Z quaternion component
 * @w: W quaternion component
 *
 * Sets the rotation component as a quaternion.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_rotation      (LrgBonePose        *self,
                                                 gfloat              x,
                                                 gfloat              y,
                                                 gfloat              z,
                                                 gfloat              w);

/**
 * lrg_bone_pose_set_rotation_euler:
 * @self: A #LrgBonePose
 * @pitch: Pitch angle in radians (X axis)
 * @yaw: Yaw angle in radians (Y axis)
 * @roll: Roll angle in radians (Z axis)
 *
 * Sets the rotation from Euler angles.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_rotation_euler (LrgBonePose       *self,
                                                  gfloat             pitch,
                                                  gfloat             yaw,
                                                  gfloat             roll);

/**
 * lrg_bone_pose_set_scale:
 * @self: A #LrgBonePose
 * @x: X scale
 * @y: Y scale
 * @z: Z scale
 *
 * Sets the scale component.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_scale         (LrgBonePose        *self,
                                                 gfloat              x,
                                                 gfloat              y,
                                                 gfloat              z);

/**
 * lrg_bone_pose_set_uniform_scale:
 * @self: A #LrgBonePose
 * @scale: Uniform scale factor
 *
 * Sets uniform scale on all axes.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_set_uniform_scale (LrgBonePose        *self,
                                                 gfloat              scale);

/**
 * lrg_bone_pose_lerp:
 * @a: First pose
 * @b: Second pose
 * @t: Interpolation factor (0.0 to 1.0)
 *
 * Linearly interpolates between two poses.
 * Position and scale use linear interpolation.
 * Rotation uses spherical linear interpolation (slerp).
 *
 * Returns: (transfer full): A new interpolated pose
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_lerp              (const LrgBonePose  *a,
                                                 const LrgBonePose  *b,
                                                 gfloat              t);

/**
 * lrg_bone_pose_lerp_to:
 * @a: First pose
 * @b: Second pose
 * @t: Interpolation factor (0.0 to 1.0)
 * @result: (out): Result pose
 *
 * Interpolates between two poses, storing result in @result.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_lerp_to           (const LrgBonePose  *a,
                                                 const LrgBonePose  *b,
                                                 gfloat              t,
                                                 LrgBonePose        *result);

/**
 * lrg_bone_pose_blend:
 * @a: First pose
 * @b: Second pose
 * @weight: Weight for pose b (0.0 to 1.0)
 *
 * Blends two poses (additive blending).
 *
 * Returns: (transfer full): A new blended pose
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_blend             (const LrgBonePose  *a,
                                                 const LrgBonePose  *b,
                                                 gfloat              weight);

/**
 * lrg_bone_pose_multiply:
 * @parent: Parent pose (applied first)
 * @local: Local pose (applied second)
 *
 * Combines two poses: result = parent * local.
 * Used for hierarchical bone chains.
 *
 * Returns: (transfer full): A new combined pose
 */
LRG_AVAILABLE_IN_ALL
LrgBonePose *   lrg_bone_pose_multiply          (const LrgBonePose  *parent,
                                                 const LrgBonePose  *local);

/**
 * lrg_bone_pose_normalize_rotation:
 * @self: A #LrgBonePose
 *
 * Normalizes the rotation quaternion.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_bone_pose_normalize_rotation (LrgBonePose       *self);

/**
 * lrg_bone_pose_equal:
 * @a: First pose
 * @b: Second pose
 *
 * Checks if two poses are equal.
 *
 * Returns: %TRUE if equal
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_bone_pose_equal             (const LrgBonePose  *a,
                                                 const LrgBonePose  *b);

G_END_DECLS
