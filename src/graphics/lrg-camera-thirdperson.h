/* lrg-camera-thirdperson.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Third-person camera implementation for 3D games.
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

#define LRG_TYPE_CAMERA_THIRDPERSON (lrg_camera_thirdperson_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCameraThirdPerson, lrg_camera_thirdperson, LRG, CAMERA_THIRDPERSON, LrgCamera3D)

/**
 * LrgCameraThirdPersonClass:
 * @parent_class: Parent class
 *
 * Class for third-person cameras. Inherits from #LrgCamera3D and provides:
 *
 * - Spherical orbit around target position
 * - Configurable orbit distance with min/max limits
 * - Shoulder offset for over-the-shoulder view
 * - Smooth orbit rotation with yaw wrap-around handling
 * - Smooth target following
 * - Pitch clamping to prevent camera flip
 * - Collision avoidance via sphere-casting
 *
 * Ideal for action games like Dark Souls, God of War, and similar.
 */
struct _LrgCameraThirdPersonClass
{
	LrgCamera3DClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera_thirdperson_new:
 *
 * Create a new third-person camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCameraThirdPerson
 */
LRG_AVAILABLE_IN_ALL
LrgCameraThirdPerson * lrg_camera_thirdperson_new (void);

/* ==========================================================================
 * Orbit Distance
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_distance:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the desired orbit distance from target.
 *
 * Returns: the orbit distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_distance (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_distance:
 * @self: an #LrgCameraThirdPerson
 * @distance: the orbit distance (must be > 0)
 *
 * Set the desired orbit distance from target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_distance (LrgCameraThirdPerson *self,
                                          gfloat                distance);

/**
 * lrg_camera_thirdperson_get_actual_distance:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the actual current distance, which may be less than desired
 * distance due to collision avoidance.
 *
 * Returns: the actual distance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_actual_distance (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_distance_limits:
 * @self: an #LrgCameraThirdPerson
 * @min_distance: minimum allowed distance
 * @max_distance: maximum allowed distance
 *
 * Set the distance limits for orbit distance and zoom.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_distance_limits (LrgCameraThirdPerson *self,
                                                 gfloat                min_distance,
                                                 gfloat                max_distance);

/**
 * lrg_camera_thirdperson_get_distance_limits:
 * @self: an #LrgCameraThirdPerson
 * @out_min: (out) (optional): return location for min distance
 * @out_max: (out) (optional): return location for max distance
 *
 * Get the current distance limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_get_distance_limits (LrgCameraThirdPerson *self,
                                                 gfloat               *out_min,
                                                 gfloat               *out_max);

/* ==========================================================================
 * Orbit Angles
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_pitch:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the vertical orbit angle (pitch) in degrees.
 *
 * Returns: the pitch angle
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_pitch (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_pitch:
 * @self: an #LrgCameraThirdPerson
 * @pitch: the pitch angle in degrees
 *
 * Set the vertical orbit angle. Will be clamped to pitch limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_pitch (LrgCameraThirdPerson *self,
                                       gfloat                pitch);

/**
 * lrg_camera_thirdperson_get_yaw:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the horizontal orbit angle (yaw) in degrees.
 *
 * Returns: the yaw angle (0 to 360)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_yaw (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_yaw:
 * @self: an #LrgCameraThirdPerson
 * @yaw: the yaw angle in degrees
 *
 * Set the horizontal orbit angle. Will wrap to 0-360 range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_yaw (LrgCameraThirdPerson *self,
                                     gfloat                yaw);

/**
 * lrg_camera_thirdperson_orbit:
 * @self: an #LrgCameraThirdPerson
 * @delta_x: the horizontal input delta
 * @delta_y: the vertical input delta
 *
 * Apply input to orbit the camera around the target. Call this
 * with mouse delta or right stick input each frame.
 * Sensitivity is applied automatically.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_orbit (LrgCameraThirdPerson *self,
                                   gfloat                delta_x,
                                   gfloat                delta_y);

/* ==========================================================================
 * Pitch Limits
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_set_pitch_limits:
 * @self: an #LrgCameraThirdPerson
 * @min_pitch: the minimum pitch angle (looking up from behind)
 * @max_pitch: the maximum pitch angle (looking down from above)
 *
 * Set the pitch angle limits. Default is -30 to 60.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_pitch_limits (LrgCameraThirdPerson *self,
                                              gfloat                min_pitch,
                                              gfloat                max_pitch);

/**
 * lrg_camera_thirdperson_get_pitch_limits:
 * @self: an #LrgCameraThirdPerson
 * @out_min: (out) (optional): return location for min pitch
 * @out_max: (out) (optional): return location for max pitch
 *
 * Get the current pitch limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_get_pitch_limits (LrgCameraThirdPerson *self,
                                              gfloat               *out_min,
                                              gfloat               *out_max);

/* ==========================================================================
 * Sensitivity
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_sensitivity_x:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the horizontal orbit sensitivity.
 *
 * Returns: the horizontal sensitivity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_sensitivity_x (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_sensitivity_x:
 * @self: an #LrgCameraThirdPerson
 * @sensitivity: the horizontal sensitivity
 *
 * Set the horizontal orbit sensitivity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_sensitivity_x (LrgCameraThirdPerson *self,
                                               gfloat                sensitivity);

/**
 * lrg_camera_thirdperson_get_sensitivity_y:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the vertical orbit sensitivity.
 *
 * Returns: the vertical sensitivity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_sensitivity_y (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_sensitivity_y:
 * @self: an #LrgCameraThirdPerson
 * @sensitivity: the vertical sensitivity
 *
 * Set the vertical orbit sensitivity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_sensitivity_y (LrgCameraThirdPerson *self,
                                               gfloat                sensitivity);

/* ==========================================================================
 * Offsets
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_height_offset:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the height offset above target position.
 *
 * Returns: the height offset
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_height_offset (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_height_offset:
 * @self: an #LrgCameraThirdPerson
 * @offset: the height offset
 *
 * Set the height offset above target position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_height_offset (LrgCameraThirdPerson *self,
                                               gfloat                offset);

/**
 * lrg_camera_thirdperson_get_shoulder_offset:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the shoulder offset (left/right displacement).
 *
 * Returns: the shoulder offset (positive = right)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_shoulder_offset (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_shoulder_offset:
 * @self: an #LrgCameraThirdPerson
 * @offset: the shoulder offset (positive = right, negative = left)
 *
 * Set the shoulder offset for over-the-shoulder view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_shoulder_offset (LrgCameraThirdPerson *self,
                                                 gfloat                offset);

/* ==========================================================================
 * Smoothing
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_orbit_smoothing:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the orbit rotation smoothing speed.
 *
 * Returns: the orbit smoothing speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_orbit_smoothing (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_orbit_smoothing:
 * @self: an #LrgCameraThirdPerson
 * @speed: the orbit smoothing speed (0 = instant, higher = smoother)
 *
 * Set the orbit rotation smoothing speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_orbit_smoothing (LrgCameraThirdPerson *self,
                                                 gfloat                speed);

/**
 * lrg_camera_thirdperson_get_follow_smoothing:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the target follow smoothing speed.
 *
 * Returns: the follow smoothing speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_follow_smoothing (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_follow_smoothing:
 * @self: an #LrgCameraThirdPerson
 * @speed: the follow smoothing speed (0 = instant, higher = smoother)
 *
 * Set the target follow smoothing speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_follow_smoothing (LrgCameraThirdPerson *self,
                                                  gfloat                speed);

/* ==========================================================================
 * Target Following
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_follow:
 * @self: an #LrgCameraThirdPerson
 * @target_x: the target X position in world space
 * @target_y: the target Y position in world space
 * @target_z: the target Z position in world space
 * @delta_time: the frame delta time in seconds
 *
 * Update the camera to follow a target position. Call this each frame.
 * The camera will orbit around this position at the configured distance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_follow (LrgCameraThirdPerson *self,
                                    gfloat                target_x,
                                    gfloat                target_y,
                                    gfloat                target_z,
                                    gfloat                delta_time);

/**
 * lrg_camera_thirdperson_snap_to_target:
 * @self: an #LrgCameraThirdPerson
 * @target_x: the target X position
 * @target_y: the target Y position
 * @target_z: the target Z position
 *
 * Instantly snap the camera to orbit the target with no smoothing.
 * Useful for initialization or teleportation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_snap_to_target (LrgCameraThirdPerson *self,
                                            gfloat                target_x,
                                            gfloat                target_y,
                                            gfloat                target_z);

/* ==========================================================================
 * Collision Avoidance
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_collision_enabled:
 * @self: an #LrgCameraThirdPerson
 *
 * Check if collision avoidance is enabled.
 *
 * Returns: %TRUE if collision avoidance is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_thirdperson_get_collision_enabled (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_collision_enabled:
 * @self: an #LrgCameraThirdPerson
 * @enabled: whether to enable collision avoidance
 *
 * Enable or disable collision avoidance. When enabled, the camera
 * will pull closer to the target to avoid clipping through geometry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_collision_enabled (LrgCameraThirdPerson *self,
                                                   gboolean              enabled);

/**
 * lrg_camera_thirdperson_get_collision_radius:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the collision sphere radius.
 *
 * Returns: the collision radius
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_thirdperson_get_collision_radius (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_collision_radius:
 * @self: an #LrgCameraThirdPerson
 * @radius: the collision sphere radius
 *
 * Set the collision sphere radius used for collision detection.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_collision_radius (LrgCameraThirdPerson *self,
                                                  gfloat                radius);

/**
 * lrg_camera_thirdperson_get_collision_layers:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the collision layer mask.
 *
 * Returns: the collision layer mask
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_camera_thirdperson_get_collision_layers (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_set_collision_layers:
 * @self: an #LrgCameraThirdPerson
 * @layers: the collision layer mask
 *
 * Set which collision layers the camera checks against.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_collision_layers (LrgCameraThirdPerson *self,
                                                  guint32               layers);

/**
 * LrgCameraCollisionCallback:
 * @camera: the camera performing the check
 * @start_x: ray start X position
 * @start_y: ray start Y position
 * @start_z: ray start Z position
 * @end_x: ray end X position
 * @end_y: ray end Y position
 * @end_z: ray end Z position
 * @radius: sphere radius for the cast
 * @layers: collision layer mask
 * @out_hit_distance: (out): return location for hit distance
 * @user_data: user data passed to the callback
 *
 * Callback function for custom collision detection. The game provides
 * this to integrate with its physics/collision system.
 *
 * Returns: %TRUE if a collision was found, %FALSE otherwise
 */
typedef gboolean (*LrgCameraCollisionCallback) (LrgCameraThirdPerson *camera,
                                                gfloat                start_x,
                                                gfloat                start_y,
                                                gfloat                start_z,
                                                gfloat                end_x,
                                                gfloat                end_y,
                                                gfloat                end_z,
                                                gfloat                radius,
                                                guint32               layers,
                                                gfloat               *out_hit_distance,
                                                gpointer              user_data);

/**
 * lrg_camera_thirdperson_set_collision_callback:
 * @self: an #LrgCameraThirdPerson
 * @callback: (nullable) (scope notified): the collision callback
 * @user_data: (nullable): user data for the callback
 * @destroy: (nullable): destroy notify for user_data
 *
 * Set a custom collision callback for collision detection.
 * The callback performs sphere-casting from target to camera position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_thirdperson_set_collision_callback (LrgCameraThirdPerson        *self,
                                                    LrgCameraCollisionCallback   callback,
                                                    gpointer                     user_data,
                                                    GDestroyNotify               destroy);

/* ==========================================================================
 * Direction Vectors
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_get_forward:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the camera's forward direction (from camera toward target).
 * This is useful for character-relative movement.
 *
 * Returns: (transfer full): the forward direction vector (Y=0, normalized)
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera_thirdperson_get_forward (LrgCameraThirdPerson *self);

/**
 * lrg_camera_thirdperson_get_right:
 * @self: an #LrgCameraThirdPerson
 *
 * Get the camera's right direction vector.
 *
 * Returns: (transfer full): the right direction vector (Y=0, normalized)
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_camera_thirdperson_get_right (LrgCameraThirdPerson *self);

G_END_DECLS
