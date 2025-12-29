/* lrg-wheel.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWheel - Wheel physics data for vehicles.
 *
 * Represents a single wheel with suspension, grip, and runtime state.
 * Used as a boxed type for attachment to LrgVehicle.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_WHEEL (lrg_wheel_get_type ())

/**
 * LrgWheel:
 *
 * Represents a single wheel with physics properties.
 *
 * Since: 1.0
 */
typedef struct _LrgWheel LrgWheel;

struct _LrgWheel
{
    /* Position relative to vehicle center */
    gfloat offset_x;
    gfloat offset_y;
    gfloat offset_z;

    /* Wheel dimensions */
    gfloat radius;
    gfloat width;

    /* Suspension */
    gfloat suspension_length;    /* Rest length */
    gfloat suspension_stiffness; /* Spring constant */
    gfloat suspension_damping;   /* Damping coefficient */

    /* Grip */
    gfloat friction;             /* Base friction coefficient */
    gfloat grip_multiplier;      /* Current grip (wet, ice, etc.) */

    /* Configuration */
    gboolean is_drive_wheel;     /* Receives power */
    gboolean is_steering_wheel;  /* Responds to steering */

    /* Runtime state */
    gfloat compression;          /* Current suspension compression (0-1) */
    gfloat rotation_angle;       /* Wheel rotation in radians */
    gfloat steering_angle;       /* Current steering angle */
    gfloat slip_ratio;           /* Longitudinal slip (acceleration/braking) */
    gfloat slip_angle;           /* Lateral slip angle */
    gfloat angular_velocity;     /* Wheel spin speed */
    gboolean is_grounded;        /* Touching ground */
};

LRG_AVAILABLE_IN_ALL
GType
lrg_wheel_get_type (void) G_GNUC_CONST;

/**
 * lrg_wheel_new:
 * @offset_x: X offset from vehicle center
 * @offset_y: Y offset (height)
 * @offset_z: Z offset from vehicle center
 * @radius: Wheel radius
 *
 * Creates a new wheel with default properties.
 *
 * Returns: (transfer full): A new #LrgWheel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWheel *
lrg_wheel_new (gfloat offset_x,
               gfloat offset_y,
               gfloat offset_z,
               gfloat radius);

/**
 * lrg_wheel_copy:
 * @wheel: an #LrgWheel
 *
 * Creates a copy of the wheel.
 *
 * Returns: (transfer full): A copy of @wheel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWheel *
lrg_wheel_copy (const LrgWheel *wheel);

/**
 * lrg_wheel_free:
 * @wheel: an #LrgWheel
 *
 * Frees the wheel.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_free (LrgWheel *wheel);

/* Configuration */

/**
 * lrg_wheel_set_suspension:
 * @wheel: an #LrgWheel
 * @length: Rest length
 * @stiffness: Spring constant
 * @damping: Damping coefficient
 *
 * Configures wheel suspension.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_set_suspension (LrgWheel *wheel,
                          gfloat    length,
                          gfloat    stiffness,
                          gfloat    damping);

/**
 * lrg_wheel_set_friction:
 * @wheel: an #LrgWheel
 * @friction: Base friction coefficient
 *
 * Sets the base friction coefficient.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_set_friction (LrgWheel *wheel,
                        gfloat    friction);

/**
 * lrg_wheel_set_drive:
 * @wheel: an #LrgWheel
 * @is_drive: Whether wheel receives power
 *
 * Sets whether this wheel receives drive power.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_set_drive (LrgWheel *wheel,
                     gboolean  is_drive);

/**
 * lrg_wheel_set_steering:
 * @wheel: an #LrgWheel
 * @is_steering: Whether wheel responds to steering
 *
 * Sets whether this wheel responds to steering input.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_set_steering (LrgWheel *wheel,
                        gboolean  is_steering);

/* Physics calculations */

/**
 * lrg_wheel_calculate_grip:
 * @wheel: an #LrgWheel
 *
 * Calculates current grip based on slip.
 * Uses simplified Pacejka-style tire model.
 *
 * Returns: Current grip force multiplier (0-1)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_wheel_calculate_grip (const LrgWheel *wheel);

/**
 * lrg_wheel_update:
 * @wheel: an #LrgWheel
 * @ground_distance: Distance to ground
 * @drive_torque: Applied drive torque
 * @brake_torque: Applied brake torque
 * @delta: Time step
 *
 * Updates wheel physics state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_update (LrgWheel *wheel,
                  gfloat    ground_distance,
                  gfloat    drive_torque,
                  gfloat    brake_torque,
                  gfloat    delta);

/**
 * lrg_wheel_get_suspension_force:
 * @wheel: an #LrgWheel
 *
 * Gets the current suspension force.
 *
 * Returns: Upward suspension force
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_wheel_get_suspension_force (const LrgWheel *wheel);

/**
 * lrg_wheel_is_slipping:
 * @wheel: an #LrgWheel
 *
 * Checks if wheel is experiencing significant slip.
 *
 * Returns: %TRUE if slipping
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_wheel_is_slipping (const LrgWheel *wheel);

/**
 * lrg_wheel_reset_state:
 * @wheel: an #LrgWheel
 *
 * Resets runtime state to defaults.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_wheel_reset_state (LrgWheel *wheel);

G_END_DECLS
