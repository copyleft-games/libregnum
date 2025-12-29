/* lrg-vehicle-controller.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-vehicle-controller.h"

/* Default values */
#define DEFAULT_SENSITIVITY     1.0f
#define DEFAULT_DEAD_ZONE       0.1f
#define DEFAULT_SMOOTHING       0.5f
#define REVERSE_SPEED_THRESHOLD 0.5f

struct _LrgVehicleController
{
    GObject parent_instance;

    /* Target vehicle */
    LrgVehicle *vehicle;

    /* Raw input values */
    gfloat raw_throttle;
    gfloat raw_brake;
    gfloat raw_steering;
    gboolean raw_handbrake;

    /* Processed/smoothed values */
    gfloat smoothed_throttle;
    gfloat smoothed_steering;

    /* Settings */
    gfloat throttle_sensitivity;
    gfloat steering_sensitivity;
    gfloat dead_zone;
    gfloat smoothing;

    /* Reverse handling */
    gboolean auto_reverse;
    gboolean is_reversing;
};

G_DEFINE_TYPE (LrgVehicleController, lrg_vehicle_controller, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_VEHICLE,
    PROP_THROTTLE_SENSITIVITY,
    PROP_STEERING_SENSITIVITY,
    PROP_DEAD_ZONE,
    PROP_SMOOTHING,
    PROP_AUTO_REVERSE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_vehicle_controller_dispose (GObject *object)
{
    LrgVehicleController *self;

    self = LRG_VEHICLE_CONTROLLER (object);

    g_clear_object (&self->vehicle);

    G_OBJECT_CLASS (lrg_vehicle_controller_parent_class)->dispose (object);
}

static void
lrg_vehicle_controller_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgVehicleController *self;

    self = LRG_VEHICLE_CONTROLLER (object);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        g_value_set_object (value, self->vehicle);
        break;

    case PROP_THROTTLE_SENSITIVITY:
        g_value_set_float (value, self->throttle_sensitivity);
        break;

    case PROP_STEERING_SENSITIVITY:
        g_value_set_float (value, self->steering_sensitivity);
        break;

    case PROP_DEAD_ZONE:
        g_value_set_float (value, self->dead_zone);
        break;

    case PROP_SMOOTHING:
        g_value_set_float (value, self->smoothing);
        break;

    case PROP_AUTO_REVERSE:
        g_value_set_boolean (value, self->auto_reverse);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_controller_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgVehicleController *self;

    self = LRG_VEHICLE_CONTROLLER (object);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        lrg_vehicle_controller_set_vehicle (self, g_value_get_object (value));
        break;

    case PROP_THROTTLE_SENSITIVITY:
        lrg_vehicle_controller_set_throttle_sensitivity (self, g_value_get_float (value));
        break;

    case PROP_STEERING_SENSITIVITY:
        lrg_vehicle_controller_set_steering_sensitivity (self, g_value_get_float (value));
        break;

    case PROP_DEAD_ZONE:
        lrg_vehicle_controller_set_dead_zone (self, g_value_get_float (value));
        break;

    case PROP_SMOOTHING:
        lrg_vehicle_controller_set_smoothing (self, g_value_get_float (value));
        break;

    case PROP_AUTO_REVERSE:
        lrg_vehicle_controller_set_auto_reverse (self, g_value_get_boolean (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_controller_class_init (LrgVehicleControllerClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_vehicle_controller_dispose;
    object_class->get_property = lrg_vehicle_controller_get_property;
    object_class->set_property = lrg_vehicle_controller_set_property;

    properties[PROP_VEHICLE] =
        g_param_spec_object ("vehicle",
                             "Vehicle",
                             "The controlled vehicle",
                             LRG_TYPE_VEHICLE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_THROTTLE_SENSITIVITY] =
        g_param_spec_float ("throttle-sensitivity",
                            "Throttle Sensitivity",
                            "Throttle sensitivity multiplier",
                            0.1f, 5.0f, DEFAULT_SENSITIVITY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STEERING_SENSITIVITY] =
        g_param_spec_float ("steering-sensitivity",
                            "Steering Sensitivity",
                            "Steering sensitivity multiplier",
                            0.1f, 5.0f, DEFAULT_SENSITIVITY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DEAD_ZONE] =
        g_param_spec_float ("dead-zone",
                            "Dead Zone",
                            "Input dead zone threshold",
                            0.0f, 0.5f, DEFAULT_DEAD_ZONE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SMOOTHING] =
        g_param_spec_float ("smoothing",
                            "Smoothing",
                            "Input smoothing factor",
                            0.0f, 1.0f, DEFAULT_SMOOTHING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_AUTO_REVERSE] =
        g_param_spec_boolean ("auto-reverse",
                              "Auto Reverse",
                              "Auto-reverse when braking while stopped",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vehicle_controller_init (LrgVehicleController *self)
{
    self->vehicle = NULL;

    self->raw_throttle = 0.0f;
    self->raw_brake = 0.0f;
    self->raw_steering = 0.0f;
    self->raw_handbrake = FALSE;

    self->smoothed_throttle = 0.0f;
    self->smoothed_steering = 0.0f;

    self->throttle_sensitivity = DEFAULT_SENSITIVITY;
    self->steering_sensitivity = DEFAULT_SENSITIVITY;
    self->dead_zone = DEFAULT_DEAD_ZONE;
    self->smoothing = DEFAULT_SMOOTHING;

    self->auto_reverse = TRUE;
    self->is_reversing = FALSE;
}

/*
 * apply_dead_zone:
 *
 * Applies dead zone to input value with proper scaling.
 */
static gfloat
apply_dead_zone (gfloat value,
                 gfloat dead_zone)
{
    gfloat sign;
    gfloat magnitude;

    if (fabsf (value) < dead_zone)
        return 0.0f;

    sign = (value > 0.0f) ? 1.0f : -1.0f;
    magnitude = fabsf (value);

    /* Scale so that dead_zone maps to 0 and 1 maps to 1 */
    return sign * (magnitude - dead_zone) / (1.0f - dead_zone);
}

/*
 * apply_smoothing:
 *
 * Applies exponential smoothing to input.
 */
static gfloat
apply_smoothing (gfloat current,
                 gfloat target,
                 gfloat smoothing,
                 gfloat delta)
{
    gfloat rate;

    if (smoothing <= 0.0f)
        return target;

    /* Convert smoothing factor to per-second rate */
    rate = 1.0f - powf (smoothing, delta * 10.0f);

    return current + (target - current) * rate;
}

/*
 * Public API
 */

LrgVehicleController *
lrg_vehicle_controller_new (void)
{
    return g_object_new (LRG_TYPE_VEHICLE_CONTROLLER, NULL);
}

void
lrg_vehicle_controller_set_vehicle (LrgVehicleController *self,
                                    LrgVehicle           *vehicle)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));
    g_return_if_fail (vehicle == NULL || LRG_IS_VEHICLE (vehicle));

    if (self->vehicle == vehicle)
        return;

    g_clear_object (&self->vehicle);
    if (vehicle != NULL)
        self->vehicle = g_object_ref (vehicle);

    /* Reset state when changing vehicles */
    lrg_vehicle_controller_clear_input (self);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VEHICLE]);
}

LrgVehicle *
lrg_vehicle_controller_get_vehicle (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), NULL);

    return self->vehicle;
}

void
lrg_vehicle_controller_set_throttle_input (LrgVehicleController *self,
                                           gfloat                value)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->raw_throttle = CLAMP (value, -1.0f, 1.0f);
}

void
lrg_vehicle_controller_set_brake_input (LrgVehicleController *self,
                                        gfloat                value)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->raw_brake = CLAMP (value, 0.0f, 1.0f);
}

void
lrg_vehicle_controller_set_steering_input (LrgVehicleController *self,
                                           gfloat                value)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->raw_steering = CLAMP (value, -1.0f, 1.0f);
}

void
lrg_vehicle_controller_set_handbrake_input (LrgVehicleController *self,
                                            gboolean              engaged)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->raw_handbrake = engaged;
}

void
lrg_vehicle_controller_set_throttle_sensitivity (LrgVehicleController *self,
                                                 gfloat                sensitivity)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->throttle_sensitivity = CLAMP (sensitivity, 0.1f, 5.0f);
}

gfloat
lrg_vehicle_controller_get_throttle_sensitivity (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), DEFAULT_SENSITIVITY);

    return self->throttle_sensitivity;
}

void
lrg_vehicle_controller_set_steering_sensitivity (LrgVehicleController *self,
                                                 gfloat                sensitivity)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->steering_sensitivity = CLAMP (sensitivity, 0.1f, 5.0f);
}

gfloat
lrg_vehicle_controller_get_steering_sensitivity (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), DEFAULT_SENSITIVITY);

    return self->steering_sensitivity;
}

void
lrg_vehicle_controller_set_dead_zone (LrgVehicleController *self,
                                      gfloat                dead_zone)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->dead_zone = CLAMP (dead_zone, 0.0f, 0.5f);
}

gfloat
lrg_vehicle_controller_get_dead_zone (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), DEFAULT_DEAD_ZONE);

    return self->dead_zone;
}

void
lrg_vehicle_controller_set_smoothing (LrgVehicleController *self,
                                      gfloat                smoothing)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->smoothing = CLAMP (smoothing, 0.0f, 1.0f);
}

gfloat
lrg_vehicle_controller_get_smoothing (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), DEFAULT_SMOOTHING);

    return self->smoothing;
}

void
lrg_vehicle_controller_set_auto_reverse (LrgVehicleController *self,
                                         gboolean              enabled)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->auto_reverse = enabled;
}

gboolean
lrg_vehicle_controller_get_auto_reverse (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), TRUE);

    return self->auto_reverse;
}

gboolean
lrg_vehicle_controller_is_reversing (LrgVehicleController *self)
{
    g_return_val_if_fail (LRG_IS_VEHICLE_CONTROLLER (self), FALSE);

    return self->is_reversing;
}

void
lrg_vehicle_controller_update (LrgVehicleController *self,
                               gfloat                delta)
{
    gfloat processed_throttle;
    gfloat processed_steering;
    gfloat processed_brake;
    gfloat vehicle_speed;

    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));
    g_return_if_fail (delta > 0.0f);

    if (self->vehicle == NULL)
        return;

    /*
     * Process throttle input
     */
    processed_throttle = apply_dead_zone (self->raw_throttle, self->dead_zone);
    processed_throttle *= self->throttle_sensitivity;
    processed_throttle = CLAMP (processed_throttle, -1.0f, 1.0f);

    /*
     * Apply smoothing to throttle
     */
    self->smoothed_throttle = apply_smoothing (self->smoothed_throttle,
                                               processed_throttle,
                                               self->smoothing,
                                               delta);

    /*
     * Process steering input
     */
    processed_steering = apply_dead_zone (self->raw_steering, self->dead_zone);
    processed_steering *= self->steering_sensitivity;
    processed_steering = CLAMP (processed_steering, -1.0f, 1.0f);

    /*
     * Apply smoothing to steering
     */
    self->smoothed_steering = apply_smoothing (self->smoothed_steering,
                                               processed_steering,
                                               self->smoothing,
                                               delta);

    /*
     * Process brake input (no smoothing for responsiveness)
     */
    processed_brake = apply_dead_zone (self->raw_brake, self->dead_zone);
    processed_brake = CLAMP (processed_brake, 0.0f, 1.0f);

    /*
     * Handle reverse logic
     */
    vehicle_speed = lrg_vehicle_get_speed (self->vehicle);

    if (self->auto_reverse)
    {
        /*
         * Enter reverse when:
         * - Nearly stopped
         * - Braking
         * - No throttle
         */
        if (vehicle_speed < REVERSE_SPEED_THRESHOLD &&
            processed_brake > 0.1f &&
            self->smoothed_throttle < 0.1f)
        {
            self->is_reversing = TRUE;
        }

        /*
         * Exit reverse when:
         * - Accelerating forward
         */
        if (self->smoothed_throttle > 0.1f)
        {
            self->is_reversing = FALSE;
        }
    }
    else
    {
        /* Non-auto mode: reverse when throttle is negative */
        self->is_reversing = (self->smoothed_throttle < 0.0f);
    }

    /*
     * Apply to vehicle
     */
    if (self->is_reversing)
    {
        /* In reverse: brake becomes throttle (backwards) */
        lrg_vehicle_set_throttle (self->vehicle, processed_brake * 0.5f);
        lrg_vehicle_set_brake (self->vehicle, 0.0f);
        /* Invert steering for intuitive reverse */
        lrg_vehicle_set_steering (self->vehicle, -self->smoothed_steering);
    }
    else
    {
        /* Normal mode */
        if (self->smoothed_throttle >= 0.0f)
        {
            lrg_vehicle_set_throttle (self->vehicle, self->smoothed_throttle);
            lrg_vehicle_set_brake (self->vehicle, processed_brake);
        }
        else
        {
            /* Negative throttle acts as brake in non-auto mode */
            lrg_vehicle_set_throttle (self->vehicle, 0.0f);
            lrg_vehicle_set_brake (self->vehicle, -self->smoothed_throttle);
        }
        lrg_vehicle_set_steering (self->vehicle, self->smoothed_steering);
    }

    lrg_vehicle_set_handbrake (self->vehicle, self->raw_handbrake);
}

void
lrg_vehicle_controller_clear_input (LrgVehicleController *self)
{
    g_return_if_fail (LRG_IS_VEHICLE_CONTROLLER (self));

    self->raw_throttle = 0.0f;
    self->raw_brake = 0.0f;
    self->raw_steering = 0.0f;
    self->raw_handbrake = FALSE;

    self->smoothed_throttle = 0.0f;
    self->smoothed_steering = 0.0f;

    self->is_reversing = FALSE;
}
