/* lrg-camera-firstperson.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * First-person camera implementation for 3D games.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-camera3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA_FIRSTPERSON (lrg_camera_firstperson_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCameraFirstPerson, lrg_camera_firstperson, LRG, CAMERA_FIRSTPERSON, LrgCamera3D)

/**
 * LrgCameraFirstPersonClass:
 * @parent_class: Parent class
 *
 * Class for first-person cameras. Inherits from #LrgCamera3D and provides:
 *
 * - Pitch/yaw rotation from mouse delta input
 * - Pitch clamping to prevent gimbal lock
 * - Configurable mouse sensitivity
 * - Optional head bob with horizontal sway during movement
 * - Eye height above body position
 * - Direction vectors for movement calculations
 *
 * Ideal for FPS games like Doom, Half-Life, and similar.
 */
struct _LrgCameraFirstPersonClass
{
	LrgCamera3DClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera_firstperson_new:
 *
 * Create a new first-person camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCameraFirstPerson
 */
LRG_AVAILABLE_IN_ALL
LrgCameraFirstPerson * lrg_camera_firstperson_new (void);

/* ==========================================================================
 * Look Angles
 * ========================================================================== */

/**
 * lrg_camera_firstperson_get_pitch:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the vertical look angle (pitch) in degrees.
 *
 * Returns: the pitch angle (-89 to 89)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_firstperson_get_pitch (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_pitch:
 * @self: an #LrgCameraFirstPerson
 * @pitch: the pitch angle in degrees
 *
 * Set the vertical look angle. Will be clamped to the pitch limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_pitch (LrgCameraFirstPerson *self,
                                       gfloat                pitch);

/**
 * lrg_camera_firstperson_get_yaw:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the horizontal look angle (yaw) in degrees.
 *
 * Returns: the yaw angle (0 to 360)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_firstperson_get_yaw (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_yaw:
 * @self: an #LrgCameraFirstPerson
 * @yaw: the yaw angle in degrees
 *
 * Set the horizontal look angle. Will wrap to 0-360 range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_yaw (LrgCameraFirstPerson *self,
                                     gfloat                yaw);

/**
 * lrg_camera_firstperson_rotate:
 * @self: an #LrgCameraFirstPerson
 * @delta_x: the horizontal mouse delta
 * @delta_y: the vertical mouse delta
 *
 * Apply mouse input to rotate the camera. Call this each frame
 * with the mouse movement delta. Sensitivity is applied automatically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_rotate (LrgCameraFirstPerson *self,
                                    gfloat                delta_x,
                                    gfloat                delta_y);

/* ==========================================================================
 * Sensitivity
 * ========================================================================== */

/**
 * lrg_camera_firstperson_get_sensitivity_x:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the horizontal mouse sensitivity.
 *
 * Returns: the horizontal sensitivity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_firstperson_get_sensitivity_x (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_sensitivity_x:
 * @self: an #LrgCameraFirstPerson
 * @sensitivity: the horizontal sensitivity
 *
 * Set the horizontal mouse sensitivity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_sensitivity_x (LrgCameraFirstPerson *self,
                                               gfloat                sensitivity);

/**
 * lrg_camera_firstperson_get_sensitivity_y:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the vertical mouse sensitivity.
 *
 * Returns: the vertical sensitivity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_firstperson_get_sensitivity_y (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_sensitivity_y:
 * @self: an #LrgCameraFirstPerson
 * @sensitivity: the vertical sensitivity
 *
 * Set the vertical mouse sensitivity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_sensitivity_y (LrgCameraFirstPerson *self,
                                               gfloat                sensitivity);

/* ==========================================================================
 * Pitch Limits
 * ========================================================================== */

/**
 * lrg_camera_firstperson_set_pitch_limits:
 * @self: an #LrgCameraFirstPerson
 * @min_pitch: the minimum pitch angle (looking up)
 * @max_pitch: the maximum pitch angle (looking down)
 *
 * Set the pitch angle limits. Default is -89 to 89 to prevent gimbal lock.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_pitch_limits (LrgCameraFirstPerson *self,
                                              gfloat                min_pitch,
                                              gfloat                max_pitch);

/**
 * lrg_camera_firstperson_get_pitch_limits:
 * @self: an #LrgCameraFirstPerson
 * @out_min: (out) (optional): return location for min pitch
 * @out_max: (out) (optional): return location for max pitch
 *
 * Get the current pitch limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_get_pitch_limits (LrgCameraFirstPerson *self,
                                              gfloat               *out_min,
                                              gfloat               *out_max);

/* ==========================================================================
 * Body Position
 * ========================================================================== */

/**
 * lrg_camera_firstperson_set_body_position:
 * @self: an #LrgCameraFirstPerson
 * @x: the body X position
 * @y: the body Y position (feet position)
 * @z: the body Z position
 *
 * Set the body (feet) position. The camera position will be
 * eye_height above this position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_body_position (LrgCameraFirstPerson *self,
                                               gfloat                x,
                                               gfloat                y,
                                               gfloat                z);

/**
 * lrg_camera_firstperson_get_eye_height:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the eye height above body position.
 *
 * Returns: the eye height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_firstperson_get_eye_height (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_eye_height:
 * @self: an #LrgCameraFirstPerson
 * @height: the eye height (must be > 0)
 *
 * Set the eye height above body position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_eye_height (LrgCameraFirstPerson *self,
                                            gfloat                height);

/* ==========================================================================
 * Head Bob
 * ========================================================================== */

/**
 * lrg_camera_firstperson_get_head_bob_enabled:
 * @self: an #LrgCameraFirstPerson
 *
 * Check if head bob is enabled.
 *
 * Returns: %TRUE if head bob is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_firstperson_get_head_bob_enabled (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_set_head_bob_enabled:
 * @self: an #LrgCameraFirstPerson
 * @enabled: whether to enable head bob
 *
 * Enable or disable head bob effect during movement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_head_bob_enabled (LrgCameraFirstPerson *self,
                                                  gboolean              enabled);

/**
 * lrg_camera_firstperson_set_head_bob:
 * @self: an #LrgCameraFirstPerson
 * @speed: the bob oscillation speed
 * @bob_amount: the vertical bob displacement
 * @sway_amount: the horizontal sway displacement
 *
 * Configure the head bob effect parameters.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_set_head_bob (LrgCameraFirstPerson *self,
                                          gfloat                speed,
                                          gfloat                bob_amount,
                                          gfloat                sway_amount);

/**
 * lrg_camera_firstperson_update_head_bob:
 * @self: an #LrgCameraFirstPerson
 * @is_moving: whether the player is moving
 * @delta_time: the frame delta time
 *
 * Update the head bob effect. Call this each frame with movement state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_firstperson_update_head_bob (LrgCameraFirstPerson *self,
                                             gboolean              is_moving,
                                             gfloat                delta_time);

/* ==========================================================================
 * Direction Vectors
 * ========================================================================== */

/**
 * lrg_camera_firstperson_get_forward:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the camera's forward direction vector (for movement calculations).
 * This is the horizontal forward direction (Y component is 0).
 *
 * Returns: (transfer full): the forward direction vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera_firstperson_get_forward (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_get_right:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the camera's right direction vector (for strafing).
 *
 * Returns: (transfer full): the right direction vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera_firstperson_get_right (LrgCameraFirstPerson *self);

/**
 * lrg_camera_firstperson_get_look_direction:
 * @self: an #LrgCameraFirstPerson
 *
 * Get the camera's look direction vector (includes pitch).
 * This is where the camera is actually looking.
 *
 * Returns: (transfer full): the look direction vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera_firstperson_get_look_direction (LrgCameraFirstPerson *self);

G_END_DECLS
