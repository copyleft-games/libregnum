/* lrg-vehicle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-vehicle.h"

/* Default values */
#define DEFAULT_MASS            1200.0f  /* kg */
#define DEFAULT_MAX_SPEED       50.0f    /* units/s (~180 km/h) */
#define DEFAULT_ACCELERATION    15.0f    /* units/s^2 */
#define DEFAULT_BRAKING         30.0f    /* units/s^2 */
#define DEFAULT_MAX_STEERING    0.6f     /* radians (~35 degrees) */
#define DEFAULT_MAX_HEALTH      100.0f

/* Physics constants */
#define DRAG_COEFFICIENT        0.3f
#define ROLLING_RESISTANCE      0.015f
#define HANDBRAKE_FRICTION      0.8f
#define ENGINE_IDLE_RPM         800.0f
#define ENGINE_MAX_RPM          7000.0f

typedef struct _LrgVehiclePrivate
{
    /* Wheels array */
    GPtrArray *wheels;

    /* Vehicle properties */
    gfloat mass;
    gfloat max_speed;
    gfloat acceleration;
    gfloat braking;
    gfloat max_steering_angle;
    LrgDriveType drive_type;

    /* Position and rotation */
    gfloat pos_x;
    gfloat pos_y;
    gfloat pos_z;
    gfloat pitch;
    gfloat yaw;
    gfloat roll;

    /* Velocity */
    gfloat vel_x;
    gfloat vel_y;
    gfloat vel_z;
    gfloat angular_velocity;

    /* Input state */
    gfloat throttle;
    gfloat brake;
    gfloat steering;
    gboolean handbrake;

    /* Health */
    gfloat health;
    gfloat max_health;
    gboolean destroyed;

    /* Occupancy */
    gboolean occupied;

    /* Engine state (for audio) */
    gfloat engine_rpm;
} LrgVehiclePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgVehicle, lrg_vehicle, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_MASS,
    PROP_MAX_SPEED,
    PROP_ACCELERATION,
    PROP_BRAKING,
    PROP_MAX_STEERING_ANGLE,
    PROP_DRIVE_TYPE,
    PROP_HEALTH,
    PROP_MAX_HEALTH,
    PROP_DESTROYED,
    PROP_OCCUPIED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_COLLISION,
    SIGNAL_DAMAGED,
    SIGNAL_DESTROYED,
    SIGNAL_ENTERED,
    SIGNAL_EXITED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Forward declarations */
static void lrg_vehicle_real_update_physics (LrgVehicle *self, gfloat delta);
static void lrg_vehicle_real_on_collision (LrgVehicle *self, gfloat impact_force);
static void lrg_vehicle_real_apply_damage (LrgVehicle *self, gfloat damage);
static void lrg_vehicle_real_on_entered (LrgVehicle *self);
static void lrg_vehicle_real_on_exited (LrgVehicle *self);

static void
lrg_vehicle_finalize (GObject *object)
{
    LrgVehicle *self;
    LrgVehiclePrivate *priv;

    self = LRG_VEHICLE (object);
    priv = lrg_vehicle_get_instance_private (self);

    g_ptr_array_unref (priv->wheels);

    G_OBJECT_CLASS (lrg_vehicle_parent_class)->finalize (object);
}

static void
lrg_vehicle_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgVehicle *self;
    LrgVehiclePrivate *priv;

    self = LRG_VEHICLE (object);
    priv = lrg_vehicle_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_MASS:
        g_value_set_float (value, priv->mass);
        break;

    case PROP_MAX_SPEED:
        g_value_set_float (value, priv->max_speed);
        break;

    case PROP_ACCELERATION:
        g_value_set_float (value, priv->acceleration);
        break;

    case PROP_BRAKING:
        g_value_set_float (value, priv->braking);
        break;

    case PROP_MAX_STEERING_ANGLE:
        g_value_set_float (value, priv->max_steering_angle);
        break;

    case PROP_DRIVE_TYPE:
        g_value_set_int (value, priv->drive_type);
        break;

    case PROP_HEALTH:
        g_value_set_float (value, priv->health);
        break;

    case PROP_MAX_HEALTH:
        g_value_set_float (value, priv->max_health);
        break;

    case PROP_DESTROYED:
        g_value_set_boolean (value, priv->destroyed);
        break;

    case PROP_OCCUPIED:
        g_value_set_boolean (value, priv->occupied);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgVehicle *self;
    LrgVehiclePrivate *priv;

    self = LRG_VEHICLE (object);
    priv = lrg_vehicle_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_MASS:
        priv->mass = g_value_get_float (value);
        break;

    case PROP_MAX_SPEED:
        priv->max_speed = g_value_get_float (value);
        break;

    case PROP_ACCELERATION:
        priv->acceleration = g_value_get_float (value);
        break;

    case PROP_BRAKING:
        priv->braking = g_value_get_float (value);
        break;

    case PROP_MAX_STEERING_ANGLE:
        priv->max_steering_angle = g_value_get_float (value);
        break;

    case PROP_DRIVE_TYPE:
        priv->drive_type = g_value_get_int (value);
        break;

    case PROP_MAX_HEALTH:
        priv->max_health = g_value_get_float (value);
        if (priv->health > priv->max_health)
            priv->health = priv->max_health;
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_class_init (LrgVehicleClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_vehicle_finalize;
    object_class->get_property = lrg_vehicle_get_property;
    object_class->set_property = lrg_vehicle_set_property;

    /* Virtual methods */
    klass->update_physics = lrg_vehicle_real_update_physics;
    klass->on_collision = lrg_vehicle_real_on_collision;
    klass->apply_damage = lrg_vehicle_real_apply_damage;
    klass->on_entered = lrg_vehicle_real_on_entered;
    klass->on_exited = lrg_vehicle_real_on_exited;

    /* Properties */
    properties[PROP_MASS] =
        g_param_spec_float ("mass",
                            "Mass",
                            "Vehicle mass in kg",
                            1.0f, 100000.0f, DEFAULT_MASS,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_SPEED] =
        g_param_spec_float ("max-speed",
                            "Max Speed",
                            "Maximum speed in units/second",
                            0.0f, 1000.0f, DEFAULT_MAX_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCELERATION] =
        g_param_spec_float ("acceleration",
                            "Acceleration",
                            "Acceleration rate in units/s^2",
                            0.0f, 1000.0f, DEFAULT_ACCELERATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BRAKING] =
        g_param_spec_float ("braking",
                            "Braking",
                            "Braking deceleration",
                            0.0f, 1000.0f, DEFAULT_BRAKING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_STEERING_ANGLE] =
        g_param_spec_float ("max-steering-angle",
                            "Max Steering Angle",
                            "Maximum steering angle in radians",
                            0.0f, G_PI / 2.0f, DEFAULT_MAX_STEERING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DRIVE_TYPE] =
        g_param_spec_int ("drive-type",
                          "Drive Type",
                          "Vehicle drivetrain type",
                          LRG_DRIVE_TYPE_FRONT, LRG_DRIVE_TYPE_ALL,
                          LRG_DRIVE_TYPE_REAR,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEALTH] =
        g_param_spec_float ("health",
                            "Health",
                            "Current health",
                            0.0f, G_MAXFLOAT, DEFAULT_MAX_HEALTH,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_float ("max-health",
                            "Max Health",
                            "Maximum health",
                            0.0f, G_MAXFLOAT, DEFAULT_MAX_HEALTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DESTROYED] =
        g_param_spec_boolean ("destroyed",
                              "Destroyed",
                              "Whether vehicle is destroyed",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_OCCUPIED] =
        g_param_spec_boolean ("occupied",
                              "Occupied",
                              "Whether vehicle has a driver",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    signals[SIGNAL_COLLISION] =
        g_signal_new ("collision",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgVehicleClass, on_collision),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_FLOAT);

    signals[SIGNAL_DAMAGED] =
        g_signal_new ("damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgVehicleClass, apply_damage),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_FLOAT);

    signals[SIGNAL_DESTROYED] =
        g_signal_new ("destroyed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_ENTERED] =
        g_signal_new ("entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgVehicleClass, on_entered),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgVehicleClass, on_exited),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_vehicle_init (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    priv = lrg_vehicle_get_instance_private (self);

    priv->wheels = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_wheel_free);

    /* Properties */
    priv->mass = DEFAULT_MASS;
    priv->max_speed = DEFAULT_MAX_SPEED;
    priv->acceleration = DEFAULT_ACCELERATION;
    priv->braking = DEFAULT_BRAKING;
    priv->max_steering_angle = DEFAULT_MAX_STEERING;
    priv->drive_type = LRG_DRIVE_TYPE_REAR;

    /* Position/rotation */
    priv->pos_x = 0.0f;
    priv->pos_y = 0.0f;
    priv->pos_z = 0.0f;
    priv->pitch = 0.0f;
    priv->yaw = 0.0f;
    priv->roll = 0.0f;

    /* Velocity */
    priv->vel_x = 0.0f;
    priv->vel_y = 0.0f;
    priv->vel_z = 0.0f;
    priv->angular_velocity = 0.0f;

    /* Input */
    priv->throttle = 0.0f;
    priv->brake = 0.0f;
    priv->steering = 0.0f;
    priv->handbrake = FALSE;

    /* Health */
    priv->health = DEFAULT_MAX_HEALTH;
    priv->max_health = DEFAULT_MAX_HEALTH;
    priv->destroyed = FALSE;

    /* Occupancy */
    priv->occupied = FALSE;

    /* Engine */
    priv->engine_rpm = ENGINE_IDLE_RPM;
}

/*
 * lrg_vehicle_real_update_physics:
 *
 * Default implementation of arcade-style vehicle physics.
 * Uses simplified model suitable for casual racing games.
 */
static void
lrg_vehicle_real_update_physics (LrgVehicle *self,
                                 gfloat      delta)
{
    LrgVehiclePrivate *priv;
    gfloat speed;
    gfloat forward_x;
    gfloat forward_z;
    gfloat drive_force;
    gfloat brake_force;
    gfloat drag_force;
    gfloat rolling_force;
    gfloat net_force;
    gfloat turn_radius;
    gfloat angular_accel;
    guint i;

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->destroyed)
        return;

    /* Calculate current speed */
    speed = sqrtf (priv->vel_x * priv->vel_x + priv->vel_z * priv->vel_z);

    /* Forward direction vector from yaw angle */
    forward_x = sinf (priv->yaw);
    forward_z = cosf (priv->yaw);

    /*
     * Calculate forces
     */

    /* Drive force (throttle) */
    drive_force = priv->throttle * priv->acceleration * priv->mass;

    /* Brake force */
    brake_force = priv->brake * priv->braking * priv->mass;
    if (priv->handbrake)
        brake_force += HANDBRAKE_FRICTION * priv->mass * 10.0f;

    /* Drag force (proportional to speed squared) */
    drag_force = DRAG_COEFFICIENT * speed * speed;

    /* Rolling resistance (proportional to speed) */
    rolling_force = ROLLING_RESISTANCE * priv->mass * 10.0f;

    /*
     * Net force calculation
     */
    net_force = drive_force - drag_force - rolling_force;

    /* Apply braking */
    if (speed > 0.1f)
    {
        net_force -= brake_force;
    }

    /* Acceleration */
    if (priv->mass > 0.0f)
    {
        gfloat accel = net_force / priv->mass;

        /* Update velocity in forward direction */
        priv->vel_x += forward_x * accel * delta;
        priv->vel_z += forward_z * accel * delta;
    }

    /* Recalculate speed after force application */
    speed = sqrtf (priv->vel_x * priv->vel_x + priv->vel_z * priv->vel_z);

    /* Clamp to max speed */
    if (speed > priv->max_speed)
    {
        gfloat scale = priv->max_speed / speed;
        priv->vel_x *= scale;
        priv->vel_z *= scale;
        speed = priv->max_speed;
    }

    /* Stop if very slow and no input */
    if (speed < 0.5f && fabsf (priv->throttle) < 0.01f)
    {
        priv->vel_x *= 0.9f;
        priv->vel_z *= 0.9f;
    }

    /*
     * Steering
     */
    if (speed > 0.1f)
    {
        gfloat steering_angle;
        gfloat wheelbase;

        /* Actual steering angle based on input and max angle */
        steering_angle = priv->steering * priv->max_steering_angle;

        /* Reduce steering at high speeds for stability */
        steering_angle *= (1.0f - 0.5f * (speed / priv->max_speed));

        /* Ackermann-style steering (simplified) */
        wheelbase = 2.5f; /* default wheelbase */

        if (fabsf (steering_angle) > 0.01f)
        {
            /* Calculate turn radius from wheelbase and steering angle */
            turn_radius = wheelbase / tanf (fabsf (steering_angle));

            /* Angular velocity based on speed and turn radius */
            angular_accel = speed / turn_radius;
            if (steering_angle < 0.0f)
                angular_accel = -angular_accel;

            /* Apply angular velocity to yaw */
            priv->yaw += angular_accel * delta;

            /* Keep yaw in range [-PI, PI] */
            while (priv->yaw > G_PI)
                priv->yaw -= 2.0f * G_PI;
            while (priv->yaw < -G_PI)
                priv->yaw += 2.0f * G_PI;
        }

        /* Align velocity with heading (reduce sliding) */
        {
            gfloat vel_mag = speed;
            gfloat dot = priv->vel_x * forward_x + priv->vel_z * forward_z;
            gfloat alignment_rate = 5.0f * delta; /* How fast we align */

            if (dot < 0.0f)
            {
                /* Moving backwards */
                priv->vel_x = -forward_x * vel_mag;
                priv->vel_z = -forward_z * vel_mag;
            }
            else
            {
                /* Blend towards aligned velocity */
                priv->vel_x = priv->vel_x * (1.0f - alignment_rate) +
                              forward_x * vel_mag * alignment_rate;
                priv->vel_z = priv->vel_z * (1.0f - alignment_rate) +
                              forward_z * vel_mag * alignment_rate;
            }
        }
    }

    /*
     * Update position
     */
    priv->pos_x += priv->vel_x * delta;
    priv->pos_z += priv->vel_z * delta;

    /*
     * Update wheel state
     */
    for (i = 0; i < priv->wheels->len; i++)
    {
        LrgWheel *wheel;
        gfloat wheel_drive_torque;
        gfloat wheel_brake_torque;
        gboolean is_drive_wheel;

        wheel = g_ptr_array_index (priv->wheels, i);

        /* Determine if this wheel gets drive power */
        is_drive_wheel = FALSE;
        switch (priv->drive_type)
        {
        case LRG_DRIVE_TYPE_FRONT:
            is_drive_wheel = wheel->is_steering_wheel;
            break;
        case LRG_DRIVE_TYPE_REAR:
            is_drive_wheel = wheel->is_drive_wheel;
            break;
        case LRG_DRIVE_TYPE_ALL:
            is_drive_wheel = TRUE;
            break;
        }

        /* Calculate torques */
        wheel_drive_torque = is_drive_wheel ? drive_force * 0.25f : 0.0f;
        wheel_brake_torque = brake_force * 0.25f;

        /* Update wheel steering angle */
        if (wheel->is_steering_wheel)
            wheel->steering_angle = priv->steering * priv->max_steering_angle;

        /* Assume grounded for now (simplified) */
        lrg_wheel_update (wheel, 0.3f, wheel_drive_torque, wheel_brake_torque, delta);
    }

    /*
     * Update engine RPM (for audio)
     */
    {
        gfloat throttle_rpm;
        gfloat speed_rpm;

        throttle_rpm = ENGINE_IDLE_RPM + priv->throttle * (ENGINE_MAX_RPM - ENGINE_IDLE_RPM) * 0.3f;
        speed_rpm = ENGINE_IDLE_RPM + (speed / priv->max_speed) * (ENGINE_MAX_RPM - ENGINE_IDLE_RPM);

        /* Blend between throttle and speed-based RPM */
        priv->engine_rpm = fmaxf (throttle_rpm, speed_rpm);
        priv->engine_rpm = CLAMP (priv->engine_rpm, ENGINE_IDLE_RPM, ENGINE_MAX_RPM);
    }
}

static void
lrg_vehicle_real_on_collision (LrgVehicle *self,
                               gfloat      impact_force)
{
    /* Default: convert impact to damage */
    if (impact_force > 5.0f)
    {
        gfloat damage = (impact_force - 5.0f) * 2.0f;
        lrg_vehicle_damage (self, damage);
    }
}

static void
lrg_vehicle_real_apply_damage (LrgVehicle *self,
                               gfloat      damage)
{
    LrgVehiclePrivate *priv;

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->destroyed)
        return;

    priv->health -= damage;

    if (priv->health <= 0.0f)
    {
        priv->health = 0.0f;
        priv->destroyed = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTROYED]);
        g_signal_emit (self, signals[SIGNAL_DESTROYED], 0);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
}

static void
lrg_vehicle_real_on_entered (LrgVehicle *self)
{
    /* Default: no special behavior */
    (void)self;
}

static void
lrg_vehicle_real_on_exited (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    priv = lrg_vehicle_get_instance_private (self);

    /* Clear inputs when player exits */
    priv->throttle = 0.0f;
    priv->brake = 0.0f;
    priv->steering = 0.0f;
    priv->handbrake = TRUE;
}

/*
 * Public API
 */

LrgVehicle *
lrg_vehicle_new (void)
{
    return g_object_new (LRG_TYPE_VEHICLE, NULL);
}

guint
lrg_vehicle_add_wheel (LrgVehicle *self,
                       LrgWheel   *wheel)
{
    LrgVehiclePrivate *priv;
    guint index;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0);
    g_return_val_if_fail (wheel != NULL, 0);

    priv = lrg_vehicle_get_instance_private (self);

    index = priv->wheels->len;
    g_ptr_array_add (priv->wheels, wheel);

    return index;
}

LrgWheel *
lrg_vehicle_get_wheel (LrgVehicle *self,
                       guint       index)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), NULL);

    priv = lrg_vehicle_get_instance_private (self);

    if (index >= priv->wheels->len)
        return NULL;

    return g_ptr_array_index (priv->wheels, index);
}

guint
lrg_vehicle_get_wheel_count (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->wheels->len;
}

void
lrg_vehicle_setup_standard_wheels (LrgVehicle *self,
                                   gfloat      wheelbase,
                                   gfloat      track_width,
                                   gfloat      wheel_radius)
{
    LrgWheel *wheel;
    gfloat half_wheelbase;
    gfloat half_track;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (wheelbase > 0.0f);
    g_return_if_fail (track_width > 0.0f);
    g_return_if_fail (wheel_radius > 0.0f);

    half_wheelbase = wheelbase / 2.0f;
    half_track = track_width / 2.0f;

    /* Front left - steering wheel */
    wheel = lrg_wheel_new (-half_track, 0.0f, half_wheelbase, wheel_radius);
    lrg_wheel_set_steering (wheel, TRUE);
    lrg_vehicle_add_wheel (self, wheel);

    /* Front right - steering wheel */
    wheel = lrg_wheel_new (half_track, 0.0f, half_wheelbase, wheel_radius);
    lrg_wheel_set_steering (wheel, TRUE);
    lrg_vehicle_add_wheel (self, wheel);

    /* Rear left - drive wheel */
    wheel = lrg_wheel_new (-half_track, 0.0f, -half_wheelbase, wheel_radius);
    lrg_wheel_set_drive (wheel, TRUE);
    lrg_vehicle_add_wheel (self, wheel);

    /* Rear right - drive wheel */
    wheel = lrg_wheel_new (half_track, 0.0f, -half_wheelbase, wheel_radius);
    lrg_wheel_set_drive (wheel, TRUE);
    lrg_vehicle_add_wheel (self, wheel);
}

gfloat
lrg_vehicle_get_mass (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->mass;
}

void
lrg_vehicle_set_mass (LrgVehicle *self,
                      gfloat      mass)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (mass > 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->mass != mass)
    {
        priv->mass = mass;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASS]);
    }
}

gfloat
lrg_vehicle_get_max_speed (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->max_speed;
}

void
lrg_vehicle_set_max_speed (LrgVehicle *self,
                           gfloat      max_speed)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (max_speed >= 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->max_speed != max_speed)
    {
        priv->max_speed = max_speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_SPEED]);
    }
}

gfloat
lrg_vehicle_get_acceleration (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->acceleration;
}

void
lrg_vehicle_set_acceleration (LrgVehicle *self,
                              gfloat      acceleration)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (acceleration >= 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->acceleration != acceleration)
    {
        priv->acceleration = acceleration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCELERATION]);
    }
}

gfloat
lrg_vehicle_get_braking (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->braking;
}

void
lrg_vehicle_set_braking (LrgVehicle *self,
                         gfloat      braking)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (braking >= 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->braking != braking)
    {
        priv->braking = braking;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BRAKING]);
    }
}

gfloat
lrg_vehicle_get_max_steering_angle (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->max_steering_angle;
}

void
lrg_vehicle_set_max_steering_angle (LrgVehicle *self,
                                    gfloat      angle)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (angle >= 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->max_steering_angle != angle)
    {
        priv->max_steering_angle = angle;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_STEERING_ANGLE]);
    }
}

LrgDriveType
lrg_vehicle_get_drive_type (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), LRG_DRIVE_TYPE_REAR);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->drive_type;
}

void
lrg_vehicle_set_drive_type (LrgVehicle   *self,
                            LrgDriveType  drive_type)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->drive_type != drive_type)
    {
        priv->drive_type = drive_type;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRIVE_TYPE]);
    }
}

void
lrg_vehicle_set_throttle (LrgVehicle *self,
                          gfloat      throttle)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->throttle = CLAMP (throttle, 0.0f, 1.0f);
}

void
lrg_vehicle_set_brake (LrgVehicle *self,
                       gfloat      brake)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->brake = CLAMP (brake, 0.0f, 1.0f);
}

void
lrg_vehicle_set_steering (LrgVehicle *self,
                          gfloat      steering)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->steering = CLAMP (steering, -1.0f, 1.0f);
}

void
lrg_vehicle_set_handbrake (LrgVehicle *self,
                           gboolean    engaged)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->handbrake = engaged;
}

void
lrg_vehicle_get_position (LrgVehicle *self,
                          gfloat     *x,
                          gfloat     *y,
                          gfloat     *z)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (x != NULL)
        *x = priv->pos_x;
    if (y != NULL)
        *y = priv->pos_y;
    if (z != NULL)
        *z = priv->pos_z;
}

void
lrg_vehicle_set_position (LrgVehicle *self,
                          gfloat      x,
                          gfloat      y,
                          gfloat      z)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->pos_x = x;
    priv->pos_y = y;
    priv->pos_z = z;
}

void
lrg_vehicle_get_rotation (LrgVehicle *self,
                          gfloat     *pitch,
                          gfloat     *yaw,
                          gfloat     *roll)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (pitch != NULL)
        *pitch = priv->pitch;
    if (yaw != NULL)
        *yaw = priv->yaw;
    if (roll != NULL)
        *roll = priv->roll;
}

void
lrg_vehicle_set_rotation (LrgVehicle *self,
                          gfloat      pitch,
                          gfloat      yaw,
                          gfloat      roll)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    priv->pitch = pitch;
    priv->yaw = yaw;
    priv->roll = roll;
}

void
lrg_vehicle_get_velocity (LrgVehicle *self,
                          gfloat     *vx,
                          gfloat     *vy,
                          gfloat     *vz)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (vx != NULL)
        *vx = priv->vel_x;
    if (vy != NULL)
        *vy = priv->vel_y;
    if (vz != NULL)
        *vz = priv->vel_z;
}

gfloat
lrg_vehicle_get_speed (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return sqrtf (priv->vel_x * priv->vel_x +
                  priv->vel_y * priv->vel_y +
                  priv->vel_z * priv->vel_z);
}

gfloat
lrg_vehicle_get_heading (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->yaw;
}

void
lrg_vehicle_get_forward_vector (LrgVehicle *self,
                                gfloat     *x,
                                gfloat     *y,
                                gfloat     *z)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (x != NULL)
        *x = sinf (priv->yaw);
    if (y != NULL)
        *y = 0.0f;
    if (z != NULL)
        *z = cosf (priv->yaw);
}

gfloat
lrg_vehicle_get_rpm (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->engine_rpm;
}

gfloat
lrg_vehicle_get_health (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->health;
}

gfloat
lrg_vehicle_get_max_health (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->max_health;
}

void
lrg_vehicle_set_max_health (LrgVehicle *self,
                            gfloat      max_health)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (max_health > 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->max_health != max_health)
    {
        priv->max_health = max_health;
        if (priv->health > max_health)
        {
            priv->health = max_health;
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
        }
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HEALTH]);
    }
}

gboolean
lrg_vehicle_damage (LrgVehicle *self,
                    gfloat      amount)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), FALSE);
    g_return_val_if_fail (amount >= 0.0f, FALSE);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->destroyed)
        return TRUE;

    g_signal_emit (self, signals[SIGNAL_DAMAGED], 0, amount);

    return priv->destroyed;
}

void
lrg_vehicle_repair (LrgVehicle *self,
                    gfloat      amount)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (amount >= 0.0f);

    priv = lrg_vehicle_get_instance_private (self);

    priv->health += amount;
    if (priv->health > priv->max_health)
        priv->health = priv->max_health;

    if (priv->destroyed && priv->health > 0.0f)
    {
        priv->destroyed = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DESTROYED]);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEALTH]);
}

gboolean
lrg_vehicle_is_destroyed (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), FALSE);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->destroyed;
}

gboolean
lrg_vehicle_is_occupied (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), FALSE);

    priv = lrg_vehicle_get_instance_private (self);

    return priv->occupied;
}

gboolean
lrg_vehicle_enter (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE (self), FALSE);

    priv = lrg_vehicle_get_instance_private (self);

    if (priv->occupied || priv->destroyed)
        return FALSE;

    priv->occupied = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OCCUPIED]);
    g_signal_emit (self, signals[SIGNAL_ENTERED], 0);

    return TRUE;
}

void
lrg_vehicle_exit (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    if (!priv->occupied)
        return;

    priv->occupied = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OCCUPIED]);
    g_signal_emit (self, signals[SIGNAL_EXITED], 0);
}

void
lrg_vehicle_update (LrgVehicle *self,
                    gfloat      delta)
{
    LrgVehicleClass *klass;

    g_return_if_fail (LRG_IS_VEHICLE (self));
    g_return_if_fail (delta > 0.0f);

    klass = LRG_VEHICLE_GET_CLASS (self);

    if (klass->update_physics != NULL)
        klass->update_physics (self, delta);
}

void
lrg_vehicle_reset (LrgVehicle *self)
{
    LrgVehiclePrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_VEHICLE (self));

    priv = lrg_vehicle_get_instance_private (self);

    /* Reset velocity */
    priv->vel_x = 0.0f;
    priv->vel_y = 0.0f;
    priv->vel_z = 0.0f;
    priv->angular_velocity = 0.0f;

    /* Reset inputs */
    priv->throttle = 0.0f;
    priv->brake = 0.0f;
    priv->steering = 0.0f;
    priv->handbrake = FALSE;

    /* Reset engine */
    priv->engine_rpm = ENGINE_IDLE_RPM;

    /* Reset wheels */
    for (i = 0; i < priv->wheels->len; i++)
    {
        LrgWheel *wheel = g_ptr_array_index (priv->wheels, i);
        lrg_wheel_reset_state (wheel);
    }
}
