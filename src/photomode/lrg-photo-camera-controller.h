/* lrg-photo-camera-controller.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoCameraController - Free camera controls for photo mode.
 *
 * Provides smooth free camera movement independent of game camera,
 * with support for rotation, panning, and zoom.
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
#include "../graphics/lrg-camera3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_CAMERA_CONTROLLER (lrg_photo_camera_controller_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPhotoCameraController, lrg_photo_camera_controller,
                      LRG, PHOTO_CAMERA_CONTROLLER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_new:
 *
 * Creates a new photo camera controller.
 *
 * Returns: (transfer full): a new #LrgPhotoCameraController
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoCameraController *
lrg_photo_camera_controller_new (void);

/**
 * lrg_photo_camera_controller_new_from_camera:
 * @camera: a #LrgCamera3D to copy initial state from
 *
 * Creates a new photo camera controller initialized from an existing camera.
 *
 * Returns: (transfer full): a new #LrgPhotoCameraController
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoCameraController *
lrg_photo_camera_controller_new_from_camera (LrgCamera3D *camera);

/* ==========================================================================
 * Camera Access
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_get_camera:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the internal camera used for photo mode.
 *
 * Returns: (transfer none): the #LrgCamera3D
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCamera3D *
lrg_photo_camera_controller_get_camera (LrgPhotoCameraController *self);

/* ==========================================================================
 * Position and Orientation
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_get_position:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the camera position.
 *
 * Returns: (transfer full): the position as a #GrlVector3
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *
lrg_photo_camera_controller_get_position (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_position:
 * @self: an #LrgPhotoCameraController
 * @position: the new position
 *
 * Sets the camera position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_position (LrgPhotoCameraController *self,
                                          GrlVector3               *position);

/**
 * lrg_photo_camera_controller_get_yaw:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the camera yaw (horizontal rotation) in degrees.
 *
 * Returns: the yaw angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_yaw (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_yaw:
 * @self: an #LrgPhotoCameraController
 * @yaw: the yaw angle in degrees
 *
 * Sets the camera yaw (horizontal rotation).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_yaw (LrgPhotoCameraController *self,
                                     gfloat                    yaw);

/**
 * lrg_photo_camera_controller_get_pitch:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the camera pitch (vertical rotation) in degrees.
 *
 * Returns: the pitch angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_pitch (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_pitch:
 * @self: an #LrgPhotoCameraController
 * @pitch: the pitch angle in degrees
 *
 * Sets the camera pitch (vertical rotation).
 * Clamped to -89 to 89 degrees.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_pitch (LrgPhotoCameraController *self,
                                       gfloat                    pitch);

/**
 * lrg_photo_camera_controller_get_roll:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the camera roll (tilt) in degrees.
 *
 * Returns: the roll angle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_roll (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_roll:
 * @self: an #LrgPhotoCameraController
 * @roll: the roll angle in degrees
 *
 * Sets the camera roll (tilt).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_roll (LrgPhotoCameraController *self,
                                      gfloat                    roll);

/* ==========================================================================
 * Movement Configuration
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_get_move_speed:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the movement speed in units per second.
 *
 * Returns: the move speed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_move_speed (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_move_speed:
 * @self: an #LrgPhotoCameraController
 * @speed: the movement speed
 *
 * Sets the movement speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_move_speed (LrgPhotoCameraController *self,
                                            gfloat                    speed);

/**
 * lrg_photo_camera_controller_get_look_sensitivity:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the mouse look sensitivity.
 *
 * Returns: the sensitivity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_look_sensitivity (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_look_sensitivity:
 * @self: an #LrgPhotoCameraController
 * @sensitivity: the sensitivity
 *
 * Sets the mouse look sensitivity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_look_sensitivity (LrgPhotoCameraController *self,
                                                  gfloat                    sensitivity);

/**
 * lrg_photo_camera_controller_get_smoothing:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the movement smoothing factor (0 = instant, 1 = very smooth).
 *
 * Returns: the smoothing factor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_smoothing (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_smoothing:
 * @self: an #LrgPhotoCameraController
 * @smoothing: the smoothing factor (0-1)
 *
 * Sets the movement smoothing factor.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_smoothing (LrgPhotoCameraController *self,
                                           gfloat                    smoothing);

/* ==========================================================================
 * Field of View
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_get_fov:
 * @self: an #LrgPhotoCameraController
 *
 * Gets the field of view in degrees.
 *
 * Returns: the FOV
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_fov (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_fov:
 * @self: an #LrgPhotoCameraController
 * @fov: the field of view in degrees
 *
 * Sets the field of view.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_fov (LrgPhotoCameraController *self,
                                     gfloat                    fov);

/* ==========================================================================
 * Movement
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_move_forward:
 * @self: an #LrgPhotoCameraController
 * @amount: movement amount (-1 to 1, negative = backward)
 *
 * Moves the camera forward/backward relative to its facing direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_move_forward (LrgPhotoCameraController *self,
                                          gfloat                    amount);

/**
 * lrg_photo_camera_controller_move_right:
 * @self: an #LrgPhotoCameraController
 * @amount: movement amount (-1 to 1, negative = left)
 *
 * Moves the camera left/right (strafe).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_move_right (LrgPhotoCameraController *self,
                                        gfloat                    amount);

/**
 * lrg_photo_camera_controller_move_up:
 * @self: an #LrgPhotoCameraController
 * @amount: movement amount (-1 to 1, negative = down)
 *
 * Moves the camera up/down in world space.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_move_up (LrgPhotoCameraController *self,
                                     gfloat                    amount);

/**
 * lrg_photo_camera_controller_rotate:
 * @self: an #LrgPhotoCameraController
 * @delta_yaw: change in yaw (horizontal rotation)
 * @delta_pitch: change in pitch (vertical rotation)
 *
 * Rotates the camera by the given amounts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_rotate (LrgPhotoCameraController *self,
                                    gfloat                    delta_yaw,
                                    gfloat                    delta_pitch);

/**
 * lrg_photo_camera_controller_reset:
 * @self: an #LrgPhotoCameraController
 *
 * Resets the camera to its initial position and orientation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_reset (LrgPhotoCameraController *self);

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_photo_camera_controller_update:
 * @self: an #LrgPhotoCameraController
 * @delta: time elapsed since last update
 *
 * Updates the camera controller, applying smoothing and movement.
 * Call this each frame while photo mode is active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_update (LrgPhotoCameraController *self,
                                    gfloat                    delta);

/* ==========================================================================
 * Orbit mode and limits
 * ========================================================================== */

/**
 * LrgPhotoCollisionFunc:
 * @from: (array fixed-size=3): where the sweep starts: the previous camera
 *   position in free mode, the subject in orbit mode
 * @to: (array fixed-size=3): where the camera wants to be
 * @radius: the camera's collision radius
 * @out_position: (out caller-allocates) (array fixed-size=3): receives the
 *   allowed position when the sweep was blocked
 * @user_data: the data given to
 *   lrg_photo_camera_controller_set_collision_func()
 *
 * Asks the game whether the camera may move from @from to @to (a ray or
 * sphere cast into its collision world).
 *
 * Returns: %TRUE when blocked, with @out_position set to where the
 *   camera may be (before the obstacle); %FALSE leaves @to as it is
 *
 * Since: 1.0
 */
typedef gboolean (*LrgPhotoCollisionFunc) (const gfloat *from,
                                           const gfloat *to,
                                           gfloat        radius,
                                           gfloat       *out_position,
                                           gpointer      user_data);

/**
 * lrg_photo_camera_controller_get_mode:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: the camera mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPhotoCameraMode
lrg_photo_camera_controller_get_mode (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_mode:
 * @self: an #LrgPhotoCameraController
 * @mode: the mode
 *
 * Switches between the free camera and orbit mode.  The view does not
 * jump: entering orbit mode puts the subject orbit-distance ahead of the
 * camera; leaving it keeps the camera where it is.  In orbit mode
 * forward and back zoom, right and up pan the subject, rotate swings the
 * camera round it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_mode (LrgPhotoCameraController *self,
                                      LrgPhotoCameraMode         mode);

/**
 * lrg_photo_camera_controller_get_orbit_distance:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: the distance from the subject in orbit mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_orbit_distance (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_orbit_distance:
 * @self: an #LrgPhotoCameraController
 * @distance: the distance, clamped to 0.5 .. 500
 *
 * Sets the orbit distance at once.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_orbit_distance (LrgPhotoCameraController *self,
                                                gfloat                    distance);

/**
 * lrg_photo_camera_controller_focus_on:
 * @self: an #LrgPhotoCameraController
 * @x: the subject's x
 * @y: the subject's y
 * @z: the subject's z
 *
 * Enters orbit mode round a point: the camera stays where it is, turns
 * to face the subject and orbits it at the current distance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_focus_on (LrgPhotoCameraController *self,
                                      gfloat                    x,
                                      gfloat                    y,
                                      gfloat                    z);

/**
 * lrg_photo_camera_controller_orbit:
 * @self: an #LrgPhotoCameraController
 * @delta_yaw: the turn in degrees, positive to the left (before sensitivity)
 * @delta_pitch: the turn upwards in degrees (before sensitivity)
 *
 * Swings the camera round the subject (the same as
 * lrg_photo_camera_controller_rotate(), named for orbit mode).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_orbit (LrgPhotoCameraController *self,
                                   gfloat                    delta_yaw,
                                   gfloat                    delta_pitch);

/**
 * lrg_photo_camera_controller_zoom:
 * @self: an #LrgPhotoCameraController
 * @amount: positive zooms in
 *
 * In orbit mode moves the camera towards the subject; in free mode
 * narrows the field of view (10 degrees per unit).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_zoom (LrgPhotoCameraController *self,
                                  gfloat                    amount);

/**
 * lrg_photo_camera_controller_set_anchor:
 * @self: an #LrgPhotoCameraController
 * @x: the anchor's x
 * @y: the anchor's y
 * @z: the anchor's z
 *
 * Sets the centre of the maximum distance sphere (the player, usually).
 * A controller made from a camera anchors at that camera.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_anchor (LrgPhotoCameraController *self,
                                        gfloat                    x,
                                        gfloat                    y,
                                        gfloat                    z);

/**
 * lrg_photo_camera_controller_get_anchor:
 * @self: an #LrgPhotoCameraController
 * @x: (out) (optional): the anchor's x
 * @y: (out) (optional): the anchor's y
 * @z: (out) (optional): the anchor's z
 *
 * Gets the anchor.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_get_anchor (LrgPhotoCameraController *self,
                                        gfloat                   *x,
                                        gfloat                   *y,
                                        gfloat                   *z);

/**
 * lrg_photo_camera_controller_get_max_distance:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: the largest distance from the anchor, 0 for no limit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_max_distance (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_max_distance:
 * @self: an #LrgPhotoCameraController
 * @distance: world units, 0 for no limit
 *
 * Keeps the camera (and in orbit mode the subject) inside a sphere of
 * this radius round the anchor, so a photo cannot leave the area the
 * game wants to show.  Moving against the edge slides along it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_max_distance (LrgPhotoCameraController *self,
                                              gfloat                    distance);

/**
 * lrg_photo_camera_controller_get_distance_to_anchor:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: how far the camera is from the anchor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_distance_to_anchor (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_get_collision_enabled:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: whether the collision function is used
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_camera_controller_get_collision_enabled (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_collision_enabled:
 * @self: an #LrgPhotoCameraController
 * @enabled: whether to ask the collision function
 *
 * Collision is optional: off, the camera passes through walls (the
 * default); on, it is stopped by what the game's function reports.
 * Without a function nothing blocks it either way.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_collision_enabled (LrgPhotoCameraController *self,
                                                   gboolean                  enabled);

/**
 * lrg_photo_camera_controller_get_collision_radius:
 * @self: an #LrgPhotoCameraController
 *
 * Returns: the camera's collision radius
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_photo_camera_controller_get_collision_radius (LrgPhotoCameraController *self);

/**
 * lrg_photo_camera_controller_set_collision_radius:
 * @self: an #LrgPhotoCameraController
 * @radius: world units, clamped to 0 .. 100
 *
 * Sets the radius handed to the collision function.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_collision_radius (LrgPhotoCameraController *self,
                                                  gfloat                    radius);

/**
 * lrg_photo_camera_controller_set_collision_func:
 * @self: an #LrgPhotoCameraController
 * @func: (nullable) (scope notified) (closure user_data) (destroy notify): the
 *   collision query, or %NULL
 * @user_data: data for @func
 * @notify: called with @user_data when @func is replaced or the
 *   controller is finalized
 *
 * Sets the game's collision query.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_camera_controller_set_collision_func (LrgPhotoCameraController *self,
                                                LrgPhotoCollisionFunc     func,
                                                gpointer                  user_data,
                                                GDestroyNotify            notify);

G_END_DECLS
