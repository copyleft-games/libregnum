/* lrg-vehicle.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVehicle - Base vehicle with arcade-style physics.
 *
 * Provides a simplified vehicle physics model suitable for
 * arcade racing and driving games.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-wheel.h"

G_BEGIN_DECLS

/**
 * LrgDriveType:
 * @LRG_DRIVE_TYPE_FRONT: Front-wheel drive
 * @LRG_DRIVE_TYPE_REAR: Rear-wheel drive
 * @LRG_DRIVE_TYPE_ALL: All-wheel drive
 *
 * Vehicle drivetrain configuration.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_DRIVE_TYPE_FRONT,
    LRG_DRIVE_TYPE_REAR,
    LRG_DRIVE_TYPE_ALL
} LrgDriveType;

#define LRG_TYPE_VEHICLE (lrg_vehicle_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgVehicle, lrg_vehicle, LRG, VEHICLE, GObject)

/**
 * LrgVehicleClass:
 * @parent_class: Parent class
 * @update_physics: Called each frame to update physics
 * @on_collision: Called when vehicle collides
 * @apply_damage: Called when vehicle takes damage
 * @on_entered: Called when player enters vehicle
 * @on_exited: Called when player exits vehicle
 *
 * Virtual table for #LrgVehicle.
 *
 * Since: 1.0
 */
struct _LrgVehicleClass
{
    GObjectClass parent_class;

    /*< public >*/

    void (*update_physics) (LrgVehicle *self,
                            gfloat      delta);
    void (*on_collision)   (LrgVehicle *self,
                            gfloat      impact_force);
    void (*apply_damage)   (LrgVehicle *self,
                            gfloat      damage);
    void (*on_entered)     (LrgVehicle *self);
    void (*on_exited)      (LrgVehicle *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Signals */

/**
 * LrgVehicle::collision:
 * @vehicle: the #LrgVehicle
 * @impact_force: Force of collision
 *
 * Emitted when the vehicle collides with something.
 *
 * Since: 1.0
 */

/**
 * LrgVehicle::damaged:
 * @vehicle: the #LrgVehicle
 * @damage: Damage amount
 *
 * Emitted when the vehicle takes damage.
 *
 * Since: 1.0
 */

/**
 * LrgVehicle::destroyed:
 * @vehicle: the #LrgVehicle
 *
 * Emitted when the vehicle is destroyed.
 *
 * Since: 1.0
 */

/**
 * LrgVehicle::entered:
 * @vehicle: the #LrgVehicle
 *
 * Emitted when a player enters the vehicle.
 *
 * Since: 1.0
 */

/**
 * LrgVehicle::exited:
 * @vehicle: the #LrgVehicle
 *
 * Emitted when a player exits the vehicle.
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_vehicle_new:
 *
 * Creates a new vehicle with default properties.
 *
 * Returns: (transfer full): A new #LrgVehicle
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicle *
lrg_vehicle_new (void);

/* Wheels */

/**
 * lrg_vehicle_add_wheel:
 * @self: an #LrgVehicle
 * @wheel: (transfer full): Wheel to add
 *
 * Adds a wheel to the vehicle.
 *
 * Returns: Index of added wheel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_vehicle_add_wheel (LrgVehicle *self,
                       LrgWheel   *wheel);

/**
 * lrg_vehicle_get_wheel:
 * @self: an #LrgVehicle
 * @index: Wheel index
 *
 * Gets a wheel by index.
 *
 * Returns: (transfer none) (nullable): The wheel, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWheel *
lrg_vehicle_get_wheel (LrgVehicle *self,
                       guint       index);

/**
 * lrg_vehicle_get_wheel_count:
 * @self: an #LrgVehicle
 *
 * Gets the number of wheels.
 *
 * Returns: Wheel count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_vehicle_get_wheel_count (LrgVehicle *self);

/**
 * lrg_vehicle_setup_standard_wheels:
 * @self: an #LrgVehicle
 * @wheelbase: Distance front to rear
 * @track_width: Distance left to right
 * @wheel_radius: Wheel radius
 *
 * Sets up a standard 4-wheel configuration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_setup_standard_wheels (LrgVehicle *self,
                                   gfloat      wheelbase,
                                   gfloat      track_width,
                                   gfloat      wheel_radius);

/* Vehicle properties */

/**
 * lrg_vehicle_get_mass:
 * @self: an #LrgVehicle
 *
 * Gets the vehicle mass.
 *
 * Returns: Mass in kg
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_mass (LrgVehicle *self);

/**
 * lrg_vehicle_set_mass:
 * @self: an #LrgVehicle
 * @mass: Mass in kg
 *
 * Sets the vehicle mass.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_mass (LrgVehicle *self,
                      gfloat      mass);

/**
 * lrg_vehicle_get_max_speed:
 * @self: an #LrgVehicle
 *
 * Gets the maximum speed.
 *
 * Returns: Max speed in units/second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_max_speed (LrgVehicle *self);

/**
 * lrg_vehicle_set_max_speed:
 * @self: an #LrgVehicle
 * @max_speed: Max speed in units/second
 *
 * Sets the maximum speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_max_speed (LrgVehicle *self,
                           gfloat      max_speed);

/**
 * lrg_vehicle_get_acceleration:
 * @self: an #LrgVehicle
 *
 * Gets the acceleration rate.
 *
 * Returns: Acceleration in units/s^2
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_acceleration (LrgVehicle *self);

/**
 * lrg_vehicle_set_acceleration:
 * @self: an #LrgVehicle
 * @acceleration: Acceleration in units/s^2
 *
 * Sets the acceleration rate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_acceleration (LrgVehicle *self,
                              gfloat      acceleration);

/**
 * lrg_vehicle_get_braking:
 * @self: an #LrgVehicle
 *
 * Gets the braking power.
 *
 * Returns: Braking deceleration
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_braking (LrgVehicle *self);

/**
 * lrg_vehicle_set_braking:
 * @self: an #LrgVehicle
 * @braking: Braking deceleration
 *
 * Sets the braking power.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_braking (LrgVehicle *self,
                         gfloat      braking);

/**
 * lrg_vehicle_get_max_steering_angle:
 * @self: an #LrgVehicle
 *
 * Gets the maximum steering angle.
 *
 * Returns: Max steering angle in radians
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_max_steering_angle (LrgVehicle *self);

/**
 * lrg_vehicle_set_max_steering_angle:
 * @self: an #LrgVehicle
 * @angle: Max steering angle in radians
 *
 * Sets the maximum steering angle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_max_steering_angle (LrgVehicle *self,
                                    gfloat      angle);

/**
 * lrg_vehicle_get_drive_type:
 * @self: an #LrgVehicle
 *
 * Gets the drive type.
 *
 * Returns: Drive type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDriveType
lrg_vehicle_get_drive_type (LrgVehicle *self);

/**
 * lrg_vehicle_set_drive_type:
 * @self: an #LrgVehicle
 * @drive_type: Drive type
 *
 * Sets the drive type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_drive_type (LrgVehicle   *self,
                            LrgDriveType  drive_type);

/* Input */

/**
 * lrg_vehicle_set_throttle:
 * @self: an #LrgVehicle
 * @throttle: Throttle value (0-1)
 *
 * Sets the throttle input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_throttle (LrgVehicle *self,
                          gfloat      throttle);

/**
 * lrg_vehicle_set_brake:
 * @self: an #LrgVehicle
 * @brake: Brake value (0-1)
 *
 * Sets the brake input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_brake (LrgVehicle *self,
                       gfloat      brake);

/**
 * lrg_vehicle_set_steering:
 * @self: an #LrgVehicle
 * @steering: Steering value (-1 to 1, left to right)
 *
 * Sets the steering input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_steering (LrgVehicle *self,
                          gfloat      steering);

/**
 * lrg_vehicle_set_handbrake:
 * @self: an #LrgVehicle
 * @engaged: Whether handbrake is engaged
 *
 * Sets the handbrake state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_handbrake (LrgVehicle *self,
                           gboolean    engaged);

/* State */

/**
 * lrg_vehicle_get_position:
 * @self: an #LrgVehicle
 * @x: (out): X position
 * @y: (out): Y position
 * @z: (out): Z position
 *
 * Gets the vehicle position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_get_position (LrgVehicle *self,
                          gfloat     *x,
                          gfloat     *y,
                          gfloat     *z);

/**
 * lrg_vehicle_set_position:
 * @self: an #LrgVehicle
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the vehicle position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_position (LrgVehicle *self,
                          gfloat      x,
                          gfloat      y,
                          gfloat      z);

/**
 * lrg_vehicle_get_rotation:
 * @self: an #LrgVehicle
 * @pitch: (out): Pitch angle
 * @yaw: (out): Yaw angle (heading)
 * @roll: (out): Roll angle
 *
 * Gets the vehicle rotation in Euler angles.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_get_rotation (LrgVehicle *self,
                          gfloat     *pitch,
                          gfloat     *yaw,
                          gfloat     *roll);

/**
 * lrg_vehicle_set_rotation:
 * @self: an #LrgVehicle
 * @pitch: Pitch angle
 * @yaw: Yaw angle
 * @roll: Roll angle
 *
 * Sets the vehicle rotation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_rotation (LrgVehicle *self,
                          gfloat      pitch,
                          gfloat      yaw,
                          gfloat      roll);

/**
 * lrg_vehicle_get_velocity:
 * @self: an #LrgVehicle
 * @vx: (out): X velocity
 * @vy: (out): Y velocity
 * @vz: (out): Z velocity
 *
 * Gets the vehicle velocity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_get_velocity (LrgVehicle *self,
                          gfloat     *vx,
                          gfloat     *vy,
                          gfloat     *vz);

/**
 * lrg_vehicle_get_speed:
 * @self: an #LrgVehicle
 *
 * Gets the current speed (magnitude of velocity).
 *
 * Returns: Current speed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_speed (LrgVehicle *self);

/**
 * lrg_vehicle_get_heading:
 * @self: an #LrgVehicle
 *
 * Gets the heading angle (yaw).
 *
 * Returns: Heading in radians
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_heading (LrgVehicle *self);

/**
 * lrg_vehicle_get_forward_vector:
 * @self: an #LrgVehicle
 * @x: (out): X component
 * @y: (out): Y component
 * @z: (out): Z component
 *
 * Gets the normalized forward direction vector.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_get_forward_vector (LrgVehicle *self,
                                gfloat     *x,
                                gfloat     *y,
                                gfloat     *z);

/**
 * lrg_vehicle_get_rpm:
 * @self: an #LrgVehicle
 *
 * Gets the engine RPM (for audio/visuals).
 *
 * Returns: Engine RPM
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_rpm (LrgVehicle *self);

/* Health */

/**
 * lrg_vehicle_get_health:
 * @self: an #LrgVehicle
 *
 * Gets current health.
 *
 * Returns: Current health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_health (LrgVehicle *self);

/**
 * lrg_vehicle_get_max_health:
 * @self: an #LrgVehicle
 *
 * Gets maximum health.
 *
 * Returns: Maximum health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_get_max_health (LrgVehicle *self);

/**
 * lrg_vehicle_set_max_health:
 * @self: an #LrgVehicle
 * @max_health: Maximum health
 *
 * Sets maximum health.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_set_max_health (LrgVehicle *self,
                            gfloat      max_health);

/**
 * lrg_vehicle_damage:
 * @self: an #LrgVehicle
 * @amount: Damage amount
 *
 * Applies damage to the vehicle.
 *
 * Returns: %TRUE if vehicle was destroyed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_damage (LrgVehicle *self,
                    gfloat      amount);

/**
 * lrg_vehicle_repair:
 * @self: an #LrgVehicle
 * @amount: Repair amount
 *
 * Repairs the vehicle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_repair (LrgVehicle *self,
                    gfloat      amount);

/**
 * lrg_vehicle_is_destroyed:
 * @self: an #LrgVehicle
 *
 * Checks if vehicle is destroyed.
 *
 * Returns: %TRUE if destroyed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_is_destroyed (LrgVehicle *self);

/* Occupancy */

/**
 * lrg_vehicle_is_occupied:
 * @self: an #LrgVehicle
 *
 * Checks if vehicle has a driver.
 *
 * Returns: %TRUE if occupied
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_is_occupied (LrgVehicle *self);

/**
 * lrg_vehicle_enter:
 * @self: an #LrgVehicle
 *
 * Marks vehicle as entered by player.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_enter (LrgVehicle *self);

/**
 * lrg_vehicle_exit:
 * @self: an #LrgVehicle
 *
 * Marks vehicle as exited.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_exit (LrgVehicle *self);

/* Physics update */

/**
 * lrg_vehicle_update:
 * @self: an #LrgVehicle
 * @delta: Time step in seconds
 *
 * Updates vehicle physics.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_update (LrgVehicle *self,
                    gfloat      delta);

/**
 * lrg_vehicle_reset:
 * @self: an #LrgVehicle
 *
 * Resets vehicle state (stops motion, resets inputs).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_reset (LrgVehicle *self);

G_END_DECLS
