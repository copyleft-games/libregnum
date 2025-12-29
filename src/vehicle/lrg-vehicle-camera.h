/* lrg-vehicle-camera.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVehicleCamera - Camera specialized for vehicle following.
 *
 * Provides various camera modes: follow, hood, cockpit, and free.
 * Includes smoothing and look-ahead features.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../graphics/lrg-camera3d.h"
#include "lrg-vehicle.h"

G_BEGIN_DECLS

/* Note: LrgVehicleCameraMode is defined in lrg-enums.h */

#define LRG_TYPE_VEHICLE_CAMERA (lrg_vehicle_camera_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgVehicleCamera, lrg_vehicle_camera,
                          LRG, VEHICLE_CAMERA, LrgCamera3D)

/**
 * LrgVehicleCameraClass:
 * @parent_class: Parent class
 * @update_camera: Called to update camera position
 *
 * Virtual table for #LrgVehicleCamera.
 *
 * Since: 1.0
 */
struct _LrgVehicleCameraClass
{
    LrgCamera3DClass parent_class;

    /*< public >*/

    void (*update_camera) (LrgVehicleCamera *self,
                           gfloat            delta);

    /*< private >*/
    gpointer _reserved[4];
};

/**
 * lrg_vehicle_camera_new:
 *
 * Creates a new vehicle camera.
 *
 * Returns: (transfer full): A new #LrgVehicleCamera
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicleCamera *
lrg_vehicle_camera_new (void);

/* Vehicle binding */

/**
 * lrg_vehicle_camera_set_vehicle:
 * @self: an #LrgVehicleCamera
 * @vehicle: (nullable): Vehicle to follow
 *
 * Sets the vehicle to follow.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_vehicle (LrgVehicleCamera *self,
                                LrgVehicle       *vehicle);

/**
 * lrg_vehicle_camera_get_vehicle:
 * @self: an #LrgVehicleCamera
 *
 * Gets the followed vehicle.
 *
 * Returns: (transfer none) (nullable): The vehicle, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicle *
lrg_vehicle_camera_get_vehicle (LrgVehicleCamera *self);

/* Camera mode */

/**
 * lrg_vehicle_camera_set_mode:
 * @self: an #LrgVehicleCamera
 * @mode: Camera mode
 *
 * Sets the camera mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_mode (LrgVehicleCamera     *self,
                             LrgVehicleCameraMode  mode);

/**
 * lrg_vehicle_camera_get_mode:
 * @self: an #LrgVehicleCamera
 *
 * Gets the current camera mode.
 *
 * Returns: Camera mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicleCameraMode
lrg_vehicle_camera_get_mode (LrgVehicleCamera *self);

/**
 * lrg_vehicle_camera_cycle_mode:
 * @self: an #LrgVehicleCamera
 *
 * Cycles to the next camera mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_cycle_mode (LrgVehicleCamera *self);

/* Follow mode settings */

/**
 * lrg_vehicle_camera_set_follow_distance:
 * @self: an #LrgVehicleCamera
 * @distance: Distance behind vehicle
 *
 * Sets the follow distance.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_follow_distance (LrgVehicleCamera *self,
                                        gfloat            distance);

/**
 * lrg_vehicle_camera_get_follow_distance:
 * @self: an #LrgVehicleCamera
 *
 * Gets the follow distance.
 *
 * Returns: Follow distance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_camera_get_follow_distance (LrgVehicleCamera *self);

/**
 * lrg_vehicle_camera_set_follow_height:
 * @self: an #LrgVehicleCamera
 * @height: Height above vehicle
 *
 * Sets the follow height.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_follow_height (LrgVehicleCamera *self,
                                      gfloat            height);

/**
 * lrg_vehicle_camera_get_follow_height:
 * @self: an #LrgVehicleCamera
 *
 * Gets the follow height.
 *
 * Returns: Follow height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_camera_get_follow_height (LrgVehicleCamera *self);

/* Smoothing */

/**
 * lrg_vehicle_camera_set_smoothing:
 * @self: an #LrgVehicleCamera
 * @smoothing: Smoothing factor (0-1, 0=instant)
 *
 * Sets camera movement smoothing.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_smoothing (LrgVehicleCamera *self,
                                  gfloat            smoothing);

/**
 * lrg_vehicle_camera_get_smoothing:
 * @self: an #LrgVehicleCamera
 *
 * Gets camera smoothing factor.
 *
 * Returns: Smoothing factor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_camera_get_smoothing (LrgVehicleCamera *self);

/* Look-ahead */

/**
 * lrg_vehicle_camera_set_look_ahead:
 * @self: an #LrgVehicleCamera
 * @enabled: Whether to look ahead based on speed
 *
 * Sets whether camera looks ahead based on vehicle speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_look_ahead (LrgVehicleCamera *self,
                                   gboolean          enabled);

/**
 * lrg_vehicle_camera_get_look_ahead:
 * @self: an #LrgVehicleCamera
 *
 * Gets look-ahead setting.
 *
 * Returns: %TRUE if look-ahead is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_camera_get_look_ahead (LrgVehicleCamera *self);

/**
 * lrg_vehicle_camera_set_look_ahead_distance:
 * @self: an #LrgVehicleCamera
 * @distance: Maximum look-ahead distance
 *
 * Sets maximum look-ahead distance at full speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_look_ahead_distance (LrgVehicleCamera *self,
                                            gfloat            distance);

/**
 * lrg_vehicle_camera_get_look_ahead_distance:
 * @self: an #LrgVehicleCamera
 *
 * Gets look-ahead distance.
 *
 * Returns: Look-ahead distance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_camera_get_look_ahead_distance (LrgVehicleCamera *self);

/* Cockpit/hood offsets */

/**
 * lrg_vehicle_camera_set_hood_offset:
 * @self: an #LrgVehicleCamera
 * @x: X offset
 * @y: Y offset
 * @z: Z offset
 *
 * Sets the hood camera position offset from vehicle center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_hood_offset (LrgVehicleCamera *self,
                                    gfloat            x,
                                    gfloat            y,
                                    gfloat            z);

/**
 * lrg_vehicle_camera_set_cockpit_offset:
 * @self: an #LrgVehicleCamera
 * @x: X offset
 * @y: Y offset
 * @z: Z offset
 *
 * Sets the cockpit camera position offset from vehicle center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_set_cockpit_offset (LrgVehicleCamera *self,
                                       gfloat            x,
                                       gfloat            y,
                                       gfloat            z);

/* Free camera controls */

/**
 * lrg_vehicle_camera_rotate_free:
 * @self: an #LrgVehicleCamera
 * @yaw_delta: Yaw rotation delta
 * @pitch_delta: Pitch rotation delta
 *
 * Rotates the free camera by the given amounts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_rotate_free (LrgVehicleCamera *self,
                                gfloat            yaw_delta,
                                gfloat            pitch_delta);

/**
 * lrg_vehicle_camera_zoom_free:
 * @self: an #LrgVehicleCamera
 * @delta: Zoom delta (positive = closer)
 *
 * Zooms the free camera.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_zoom_free (LrgVehicleCamera *self,
                              gfloat            delta);

/* Update */

/**
 * lrg_vehicle_camera_update:
 * @self: an #LrgVehicleCamera
 * @delta: Time step in seconds
 *
 * Updates camera position based on vehicle state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_camera_update (LrgVehicleCamera *self,
                           gfloat            delta);

G_END_DECLS
