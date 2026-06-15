/* lrg-pose.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A camera pose value type: eye position, look-at target, up vector and vertical
 * field of view.  Used by #LrgSpatialCamera (and the #Lrg3DSurface camera-pose
 * property) as a copyable, introspectable snapshot that can be interpolated for
 * smooth camera tweens.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_POSE (lrg_pose_get_type ())

/**
 * LrgPose:
 *
 * An immutable-by-convention camera pose: (@position, @target, @up, @fovy).
 * Distances are world units; @fovy is the vertical field of view in degrees.
 *
 * Since: 1.0
 */
typedef struct _LrgPose LrgPose;

LRG_AVAILABLE_IN_ALL
GType lrg_pose_get_type (void) G_GNUC_CONST;

/**
 * lrg_pose_new:
 * @px: eye position x
 * @py: eye position y
 * @pz: eye position z
 * @tx: look-at target x
 * @ty: look-at target y
 * @tz: look-at target z
 * @ux: up vector x
 * @uy: up vector y
 * @uz: up vector z
 * @fovy: vertical field of view in degrees
 *
 * Returns: (transfer full): a new #LrgPose
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPose * lrg_pose_new (gfloat px, gfloat py, gfloat pz,
                        gfloat tx, gfloat ty, gfloat tz,
                        gfloat ux, gfloat uy, gfloat uz,
                        gfloat fovy);

LRG_AVAILABLE_IN_ALL
LrgPose * lrg_pose_copy (const LrgPose *self);

LRG_AVAILABLE_IN_ALL
void lrg_pose_free (LrgPose *self);

/**
 * lrg_pose_get_position:
 * @self: a #LrgPose
 * @x: (out) (optional): return location for x
 * @y: (out) (optional): return location for y
 * @z: (out) (optional): return location for z
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_pose_get_position (const LrgPose *self,
                            gfloat        *x,
                            gfloat        *y,
                            gfloat        *z);

/**
 * lrg_pose_get_target:
 * @self: a #LrgPose
 * @x: (out) (optional): return location for x
 * @y: (out) (optional): return location for y
 * @z: (out) (optional): return location for z
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_pose_get_target (const LrgPose *self,
                          gfloat        *x,
                          gfloat        *y,
                          gfloat        *z);

/**
 * lrg_pose_get_up:
 * @self: a #LrgPose
 * @x: (out) (optional): return location for x
 * @y: (out) (optional): return location for y
 * @z: (out) (optional): return location for z
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_pose_get_up (const LrgPose *self,
                      gfloat        *x,
                      gfloat        *y,
                      gfloat        *z);

LRG_AVAILABLE_IN_ALL
gfloat lrg_pose_get_fovy (const LrgPose *self);

/**
 * lrg_pose_lerp:
 * @a: the start pose
 * @b: the end pose
 * @t: interpolation factor in [0, 1] (clamped)
 *
 * Component-wise linear interpolation between @a and @b (position, target, up
 * and fovy).  Used to ease a camera from its current pose toward a target.
 *
 * Returns: (transfer full): a new interpolated #LrgPose
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPose * lrg_pose_lerp (const LrgPose *a,
                         const LrgPose *b,
                         gfloat         t);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPose, lrg_pose_free)

G_END_DECLS
