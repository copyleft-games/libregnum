/* lrg-vehicle-controller.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVehicleController - Translates player input to vehicle controls.
 *
 * Provides input processing, sensitivity adjustment, and dead zone
 * handling for vehicle control.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-vehicle.h"

G_BEGIN_DECLS

#define LRG_TYPE_VEHICLE_CONTROLLER (lrg_vehicle_controller_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVehicleController, lrg_vehicle_controller,
                      LRG, VEHICLE_CONTROLLER, GObject)

/**
 * lrg_vehicle_controller_new:
 *
 * Creates a new vehicle controller.
 *
 * Returns: (transfer full): A new #LrgVehicleController
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicleController *
lrg_vehicle_controller_new (void);

/* Vehicle binding */

/**
 * lrg_vehicle_controller_set_vehicle:
 * @self: an #LrgVehicleController
 * @vehicle: (nullable): Vehicle to control
 *
 * Sets the vehicle to control.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_vehicle (LrgVehicleController *self,
                                    LrgVehicle           *vehicle);

/**
 * lrg_vehicle_controller_get_vehicle:
 * @self: an #LrgVehicleController
 *
 * Gets the controlled vehicle.
 *
 * Returns: (transfer none) (nullable): The vehicle, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVehicle *
lrg_vehicle_controller_get_vehicle (LrgVehicleController *self);

/* Input */

/**
 * lrg_vehicle_controller_set_throttle_input:
 * @self: an #LrgVehicleController
 * @value: Input value (-1 to 1 for reverse/forward, or 0-1)
 *
 * Sets raw throttle input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_throttle_input (LrgVehicleController *self,
                                           gfloat                value);

/**
 * lrg_vehicle_controller_set_brake_input:
 * @self: an #LrgVehicleController
 * @value: Input value (0-1)
 *
 * Sets raw brake input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_brake_input (LrgVehicleController *self,
                                        gfloat                value);

/**
 * lrg_vehicle_controller_set_steering_input:
 * @self: an #LrgVehicleController
 * @value: Input value (-1 to 1, left to right)
 *
 * Sets raw steering input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_steering_input (LrgVehicleController *self,
                                           gfloat                value);

/**
 * lrg_vehicle_controller_set_handbrake_input:
 * @self: an #LrgVehicleController
 * @engaged: Whether handbrake is engaged
 *
 * Sets handbrake state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_handbrake_input (LrgVehicleController *self,
                                            gboolean              engaged);

/* Sensitivity */

/**
 * lrg_vehicle_controller_set_throttle_sensitivity:
 * @self: an #LrgVehicleController
 * @sensitivity: Sensitivity multiplier (0.1-5.0)
 *
 * Sets throttle sensitivity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_throttle_sensitivity (LrgVehicleController *self,
                                                 gfloat                sensitivity);

/**
 * lrg_vehicle_controller_get_throttle_sensitivity:
 * @self: an #LrgVehicleController
 *
 * Gets throttle sensitivity.
 *
 * Returns: Sensitivity multiplier
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_controller_get_throttle_sensitivity (LrgVehicleController *self);

/**
 * lrg_vehicle_controller_set_steering_sensitivity:
 * @self: an #LrgVehicleController
 * @sensitivity: Sensitivity multiplier (0.1-5.0)
 *
 * Sets steering sensitivity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_steering_sensitivity (LrgVehicleController *self,
                                                 gfloat                sensitivity);

/**
 * lrg_vehicle_controller_get_steering_sensitivity:
 * @self: an #LrgVehicleController
 *
 * Gets steering sensitivity.
 *
 * Returns: Sensitivity multiplier
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_controller_get_steering_sensitivity (LrgVehicleController *self);

/* Dead zones */

/**
 * lrg_vehicle_controller_set_dead_zone:
 * @self: an #LrgVehicleController
 * @dead_zone: Dead zone threshold (0-0.5)
 *
 * Sets input dead zone for all axes.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_dead_zone (LrgVehicleController *self,
                                      gfloat                dead_zone);

/**
 * lrg_vehicle_controller_get_dead_zone:
 * @self: an #LrgVehicleController
 *
 * Gets dead zone threshold.
 *
 * Returns: Dead zone threshold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_controller_get_dead_zone (LrgVehicleController *self);

/* Smoothing */

/**
 * lrg_vehicle_controller_set_smoothing:
 * @self: an #LrgVehicleController
 * @smoothing: Smoothing factor (0-1, 0=none)
 *
 * Sets input smoothing.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_smoothing (LrgVehicleController *self,
                                      gfloat                smoothing);

/**
 * lrg_vehicle_controller_get_smoothing:
 * @self: an #LrgVehicleController
 *
 * Gets input smoothing factor.
 *
 * Returns: Smoothing factor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_vehicle_controller_get_smoothing (LrgVehicleController *self);

/* Reverse mode */

/**
 * lrg_vehicle_controller_set_auto_reverse:
 * @self: an #LrgVehicleController
 * @enabled: Whether to auto-reverse when braking while stopped
 *
 * Sets auto-reverse mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_set_auto_reverse (LrgVehicleController *self,
                                         gboolean              enabled);

/**
 * lrg_vehicle_controller_get_auto_reverse:
 * @self: an #LrgVehicleController
 *
 * Gets auto-reverse setting.
 *
 * Returns: %TRUE if auto-reverse is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_controller_get_auto_reverse (LrgVehicleController *self);

/**
 * lrg_vehicle_controller_is_reversing:
 * @self: an #LrgVehicleController
 *
 * Checks if currently in reverse mode.
 *
 * Returns: %TRUE if reversing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_vehicle_controller_is_reversing (LrgVehicleController *self);

/* Update */

/**
 * lrg_vehicle_controller_update:
 * @self: an #LrgVehicleController
 * @delta: Time step in seconds
 *
 * Processes input and updates vehicle controls.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_update (LrgVehicleController *self,
                               gfloat                delta);

/**
 * lrg_vehicle_controller_clear_input:
 * @self: an #LrgVehicleController
 *
 * Clears all input values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_vehicle_controller_clear_input (LrgVehicleController *self);

G_END_DECLS
