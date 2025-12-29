/* lrg-wheel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-wheel.h"

/* Slip threshold for "slipping" detection */
#define SLIP_THRESHOLD 0.15f

/* Default values */
#define DEFAULT_WIDTH           0.2f
#define DEFAULT_SUSPENSION_LEN  0.3f
#define DEFAULT_STIFFNESS       50000.0f
#define DEFAULT_DAMPING         4500.0f
#define DEFAULT_FRICTION        1.0f

G_DEFINE_BOXED_TYPE (LrgWheel, lrg_wheel, lrg_wheel_copy, lrg_wheel_free)

LrgWheel *
lrg_wheel_new (gfloat offset_x,
               gfloat offset_y,
               gfloat offset_z,
               gfloat radius)
{
    LrgWheel *wheel;

    wheel = g_slice_new0 (LrgWheel);

    /* Position */
    wheel->offset_x = offset_x;
    wheel->offset_y = offset_y;
    wheel->offset_z = offset_z;

    /* Dimensions */
    wheel->radius = radius;
    wheel->width = DEFAULT_WIDTH;

    /* Suspension */
    wheel->suspension_length = DEFAULT_SUSPENSION_LEN;
    wheel->suspension_stiffness = DEFAULT_STIFFNESS;
    wheel->suspension_damping = DEFAULT_DAMPING;

    /* Grip */
    wheel->friction = DEFAULT_FRICTION;
    wheel->grip_multiplier = 1.0f;

    /* Configuration */
    wheel->is_drive_wheel = FALSE;
    wheel->is_steering_wheel = FALSE;

    /* Runtime state */
    wheel->compression = 0.0f;
    wheel->rotation_angle = 0.0f;
    wheel->steering_angle = 0.0f;
    wheel->slip_ratio = 0.0f;
    wheel->slip_angle = 0.0f;
    wheel->angular_velocity = 0.0f;
    wheel->is_grounded = FALSE;

    return wheel;
}

LrgWheel *
lrg_wheel_copy (const LrgWheel *wheel)
{
    LrgWheel *copy;

    g_return_val_if_fail (wheel != NULL, NULL);

    copy = g_slice_new (LrgWheel);
    *copy = *wheel;

    return copy;
}

void
lrg_wheel_free (LrgWheel *wheel)
{
    if (wheel == NULL)
        return;

    g_slice_free (LrgWheel, wheel);
}

void
lrg_wheel_set_suspension (LrgWheel *wheel,
                          gfloat    length,
                          gfloat    stiffness,
                          gfloat    damping)
{
    g_return_if_fail (wheel != NULL);
    g_return_if_fail (length > 0.0f);
    g_return_if_fail (stiffness > 0.0f);
    g_return_if_fail (damping >= 0.0f);

    wheel->suspension_length = length;
    wheel->suspension_stiffness = stiffness;
    wheel->suspension_damping = damping;
}

void
lrg_wheel_set_friction (LrgWheel *wheel,
                        gfloat    friction)
{
    g_return_if_fail (wheel != NULL);
    g_return_if_fail (friction >= 0.0f);

    wheel->friction = friction;
}

void
lrg_wheel_set_drive (LrgWheel *wheel,
                     gboolean  is_drive)
{
    g_return_if_fail (wheel != NULL);

    wheel->is_drive_wheel = is_drive;
}

void
lrg_wheel_set_steering (LrgWheel *wheel,
                        gboolean  is_steering)
{
    g_return_if_fail (wheel != NULL);

    wheel->is_steering_wheel = is_steering;
}

gfloat
lrg_wheel_calculate_grip (const LrgWheel *wheel)
{
    gfloat combined_slip;
    gfloat grip;

    g_return_val_if_fail (wheel != NULL, 0.0f);

    if (!wheel->is_grounded)
        return 0.0f;

    /*
     * Simplified tire grip model:
     * Grip increases linearly with slip up to optimal point,
     * then decreases. This approximates the Pacejka curve.
     */

    combined_slip = sqrtf (wheel->slip_ratio * wheel->slip_ratio +
                           wheel->slip_angle * wheel->slip_angle);

    if (combined_slip < 0.1f)
    {
        /* Linear region */
        grip = combined_slip * 10.0f;
    }
    else if (combined_slip < 0.3f)
    {
        /* Peak region */
        grip = 1.0f;
    }
    else
    {
        /* Sliding region - grip decreases */
        grip = 1.0f - (combined_slip - 0.3f) * 0.5f;
        if (grip < 0.4f)
            grip = 0.4f;
    }

    return grip * wheel->friction * wheel->grip_multiplier;
}

void
lrg_wheel_update (LrgWheel *wheel,
                  gfloat    ground_distance,
                  gfloat    drive_torque,
                  gfloat    brake_torque,
                  gfloat    delta)
{
    gfloat suspension_travel;
    gfloat prev_compression;
    gfloat compression_velocity;
    gfloat net_torque;
    gfloat wheel_inertia;
    gfloat angular_accel;

    g_return_if_fail (wheel != NULL);
    g_return_if_fail (delta > 0.0f);

    prev_compression = wheel->compression;

    /* Calculate suspension state */
    if (ground_distance < wheel->suspension_length + wheel->radius)
    {
        wheel->is_grounded = TRUE;

        /* How much is the suspension compressed */
        suspension_travel = wheel->suspension_length + wheel->radius - ground_distance;
        wheel->compression = CLAMP (suspension_travel / wheel->suspension_length, 0.0f, 1.0f);
    }
    else
    {
        wheel->is_grounded = FALSE;
        wheel->compression = 0.0f;
    }

    /* Calculate wheel spin */
    if (wheel->is_grounded)
    {
        /* Apply drive and brake torques */
        net_torque = drive_torque - brake_torque * (wheel->angular_velocity > 0.0f ? 1.0f : -1.0f);

        /* Simplified wheel inertia */
        wheel_inertia = 0.5f * 10.0f * wheel->radius * wheel->radius; /* ~10kg wheel */

        angular_accel = net_torque / wheel_inertia;
        wheel->angular_velocity += angular_accel * delta;

        /* Friction slows the wheel when no input */
        if (fabsf (drive_torque) < 0.1f && fabsf (brake_torque) < 0.1f)
        {
            wheel->angular_velocity *= (1.0f - 2.0f * delta);
        }
    }
    else
    {
        /* In air - wheel spins freely, slowing due to air resistance */
        wheel->angular_velocity *= (1.0f - 0.5f * delta);
    }

    /* Update rotation angle */
    wheel->rotation_angle += wheel->angular_velocity * delta;
    if (wheel->rotation_angle > G_PI * 2.0f)
        wheel->rotation_angle -= G_PI * 2.0f;
    if (wheel->rotation_angle < 0.0f)
        wheel->rotation_angle += G_PI * 2.0f;

    (void)compression_velocity; /* Reserved for damping calculations */
    (void)prev_compression;     /* Reserved for damping calculations */
}

gfloat
lrg_wheel_get_suspension_force (const LrgWheel *wheel)
{
    gfloat spring_force;
    gfloat damping_force;

    g_return_val_if_fail (wheel != NULL, 0.0f);

    if (!wheel->is_grounded || wheel->compression <= 0.0f)
        return 0.0f;

    /* Hooke's law: F = k * x */
    spring_force = wheel->suspension_stiffness * wheel->compression * wheel->suspension_length;

    /* Damping force (simplified - would need velocity for proper calculation) */
    damping_force = 0.0f;

    return spring_force + damping_force;
}

gboolean
lrg_wheel_is_slipping (const LrgWheel *wheel)
{
    gfloat combined_slip;

    g_return_val_if_fail (wheel != NULL, FALSE);

    combined_slip = sqrtf (wheel->slip_ratio * wheel->slip_ratio +
                           wheel->slip_angle * wheel->slip_angle);

    return combined_slip > SLIP_THRESHOLD;
}

void
lrg_wheel_reset_state (LrgWheel *wheel)
{
    g_return_if_fail (wheel != NULL);

    wheel->compression = 0.0f;
    wheel->rotation_angle = 0.0f;
    wheel->steering_angle = 0.0f;
    wheel->slip_ratio = 0.0f;
    wheel->slip_angle = 0.0f;
    wheel->angular_velocity = 0.0f;
    wheel->is_grounded = FALSE;
}
