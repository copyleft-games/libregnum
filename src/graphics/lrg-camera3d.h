/* lrg-camera3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D camera implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-camera.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA3D (lrg_camera3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCamera3D, lrg_camera3d, LRG, CAMERA3D, LrgCamera)

/**
 * LrgCamera3DClass:
 * @parent_class: Parent class
 *
 * Class for 3D cameras. Can be subclassed for custom behavior
 * like orbit cameras, first-person cameras, or cinematic cameras.
 */
struct _LrgCamera3DClass
{
	LrgCameraClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera3d_new:
 *
 * Create a new 3D camera with default settings.
 * Default position is (0, 10, 10), target is (0, 0, 0),
 * up is (0, 1, 0), fovy is 45, and projection is perspective.
 *
 * Returns: (transfer full): a new #LrgCamera3D
 */
LRG_AVAILABLE_IN_ALL
LrgCamera3D * lrg_camera3d_new (void);

/* ==========================================================================
 * Position
 * ========================================================================== */

/**
 * lrg_camera3d_get_position:
 * @self: an #LrgCamera3D
 *
 * Get the camera position in world space.
 *
 * Returns: (transfer full): the position as a #GrlVector3
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera3d_get_position (LrgCamera3D *self);

/**
 * lrg_camera3d_set_position:
 * @self: an #LrgCamera3D
 * @position: the new position
 *
 * Set the camera position in world space.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_position (LrgCamera3D *self,
                                GrlVector3  *position);

/**
 * lrg_camera3d_set_position_xyz:
 * @self: an #LrgCamera3D
 * @x: the x position
 * @y: the y position
 * @z: the z position
 *
 * Set the camera position using separate x, y, and z values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_position_xyz (LrgCamera3D *self,
                                    gfloat       x,
                                    gfloat       y,
                                    gfloat       z);

/* ==========================================================================
 * Target
 * ========================================================================== */

/**
 * lrg_camera3d_get_target:
 * @self: an #LrgCamera3D
 *
 * Get the camera target (the point the camera looks at).
 *
 * Returns: (transfer full): the target as a #GrlVector3
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera3d_get_target (LrgCamera3D *self);

/**
 * lrg_camera3d_set_target:
 * @self: an #LrgCamera3D
 * @target: the new target
 *
 * Set the camera target (the point the camera looks at).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_target (LrgCamera3D *self,
                              GrlVector3  *target);

/**
 * lrg_camera3d_set_target_xyz:
 * @self: an #LrgCamera3D
 * @x: the x target
 * @y: the y target
 * @z: the z target
 *
 * Set the camera target using separate x, y, and z values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_target_xyz (LrgCamera3D *self,
                                  gfloat       x,
                                  gfloat       y,
                                  gfloat       z);

/* ==========================================================================
 * Up Vector
 * ========================================================================== */

/**
 * lrg_camera3d_get_up:
 * @self: an #LrgCamera3D
 *
 * Get the camera up vector.
 *
 * Returns: (transfer full): the up vector as a #GrlVector3
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera3d_get_up (LrgCamera3D *self);

/**
 * lrg_camera3d_set_up:
 * @self: an #LrgCamera3D
 * @up: the new up vector
 *
 * Set the camera up vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_up (LrgCamera3D *self,
                          GrlVector3  *up);

/**
 * lrg_camera3d_set_up_xyz:
 * @self: an #LrgCamera3D
 * @x: the x component of up vector
 * @y: the y component of up vector
 * @z: the z component of up vector
 *
 * Set the camera up vector using separate x, y, and z values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_up_xyz (LrgCamera3D *self,
                              gfloat       x,
                              gfloat       y,
                              gfloat       z);

/* ==========================================================================
 * Field of View & Projection
 * ========================================================================== */

/**
 * lrg_camera3d_get_fovy:
 * @self: an #LrgCamera3D
 *
 * Get the camera field of view (Y-axis, in degrees).
 * In orthographic mode, this is used as the near plane width.
 *
 * Returns: the field of view in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera3d_get_fovy (LrgCamera3D *self);

/**
 * lrg_camera3d_set_fovy:
 * @self: an #LrgCamera3D
 * @fovy: the field of view in degrees (must be > 0)
 *
 * Set the camera field of view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_fovy (LrgCamera3D *self,
                            gfloat       fovy);

/**
 * lrg_camera3d_get_projection:
 * @self: an #LrgCamera3D
 *
 * Get the camera projection mode.
 *
 * Returns: the #LrgProjectionType
 */
LRG_AVAILABLE_IN_ALL
LrgProjectionType lrg_camera3d_get_projection (LrgCamera3D *self);

/**
 * lrg_camera3d_set_projection:
 * @self: an #LrgCamera3D
 * @projection: the projection mode
 *
 * Set the camera projection mode (perspective or orthographic).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_projection (LrgCamera3D       *self,
                                  LrgProjectionType  projection);

/* ==========================================================================
 * Orientation (Quaternion)
 * ========================================================================== */

/**
 * lrg_camera3d_get_orientation:
 * @self: an #LrgCamera3D
 *
 * Gets the camera orientation as a quaternion.
 *
 * The orientation is computed from the camera's look direction
 * (position to target) and up vector.
 *
 * Returns: (transfer full): A new #GrlQuaternion with the camera orientation
 */
LRG_AVAILABLE_IN_ALL
GrlQuaternion * lrg_camera3d_get_orientation (LrgCamera3D *self);

/**
 * lrg_camera3d_set_orientation:
 * @self: an #LrgCamera3D
 * @quaternion: The orientation quaternion
 *
 * Sets the camera orientation from a quaternion.
 *
 * This updates the camera's target position and up vector based on
 * the orientation while keeping the camera position unchanged.
 * The camera will look in the forward direction defined by the quaternion.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_set_orientation (LrgCamera3D       *self,
                                   const GrlQuaternion *quaternion);

/**
 * lrg_camera3d_slerp_to:
 * @self: an #LrgCamera3D
 * @target: Target orientation quaternion
 * @amount: Interpolation amount (0.0 to 1.0)
 *
 * Spherically interpolates the camera orientation toward the target.
 *
 * Uses SLERP (spherical linear interpolation) for smooth camera
 * rotation transitions that maintain constant angular velocity.
 * This is useful for smooth camera movement and cinematics.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera3d_slerp_to (LrgCamera3D         *self,
                            const GrlQuaternion *target,
                            gfloat               amount);

G_END_DECLS
