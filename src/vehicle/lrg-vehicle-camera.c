/* lrg-vehicle-camera.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#include "lrg-vehicle-camera.h"

/* Default values */
#define DEFAULT_FOLLOW_DISTANCE     8.0f
#define DEFAULT_FOLLOW_HEIGHT       3.0f
#define DEFAULT_SMOOTHING           0.8f
#define DEFAULT_LOOK_AHEAD_DISTANCE 5.0f
#define DEFAULT_FREE_DISTANCE       12.0f
#define DEFAULT_FREE_PITCH          0.3f /* radians */

typedef struct _LrgVehicleCameraPrivate
{
    /* Target vehicle */
    LrgVehicle *vehicle;

    /* Camera mode */
    LrgVehicleCameraMode mode;

    /* Follow mode settings */
    gfloat follow_distance;
    gfloat follow_height;

    /* Smoothing */
    gfloat smoothing;

    /* Look-ahead */
    gboolean look_ahead;
    gfloat look_ahead_distance;

    /* Fixed camera offsets */
    gfloat hood_offset_x;
    gfloat hood_offset_y;
    gfloat hood_offset_z;
    gfloat cockpit_offset_x;
    gfloat cockpit_offset_y;
    gfloat cockpit_offset_z;

    /* Free camera state */
    gfloat free_yaw;
    gfloat free_pitch;
    gfloat free_distance;

    /* Current smoothed position */
    gfloat current_x;
    gfloat current_y;
    gfloat current_z;
} LrgVehicleCameraPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgVehicleCamera, lrg_vehicle_camera, LRG_TYPE_CAMERA3D)

enum
{
    PROP_0,
    PROP_VEHICLE,
    PROP_MODE,
    PROP_FOLLOW_DISTANCE,
    PROP_FOLLOW_HEIGHT,
    PROP_SMOOTHING,
    PROP_LOOK_AHEAD,
    PROP_LOOK_AHEAD_DISTANCE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Forward declaration */
static void lrg_vehicle_camera_real_update (LrgVehicleCamera *self, gfloat delta);

static void
lrg_vehicle_camera_dispose (GObject *object)
{
    LrgVehicleCamera *self;
    LrgVehicleCameraPrivate *priv;

    self = LRG_VEHICLE_CAMERA (object);
    priv = lrg_vehicle_camera_get_instance_private (self);

    g_clear_object (&priv->vehicle);

    G_OBJECT_CLASS (lrg_vehicle_camera_parent_class)->dispose (object);
}

static void
lrg_vehicle_camera_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgVehicleCamera *self;
    LrgVehicleCameraPrivate *priv;

    self = LRG_VEHICLE_CAMERA (object);
    priv = lrg_vehicle_camera_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        g_value_set_object (value, priv->vehicle);
        break;

    case PROP_MODE:
        g_value_set_int (value, priv->mode);
        break;

    case PROP_FOLLOW_DISTANCE:
        g_value_set_float (value, priv->follow_distance);
        break;

    case PROP_FOLLOW_HEIGHT:
        g_value_set_float (value, priv->follow_height);
        break;

    case PROP_SMOOTHING:
        g_value_set_float (value, priv->smoothing);
        break;

    case PROP_LOOK_AHEAD:
        g_value_set_boolean (value, priv->look_ahead);
        break;

    case PROP_LOOK_AHEAD_DISTANCE:
        g_value_set_float (value, priv->look_ahead_distance);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_camera_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgVehicleCamera *self;
    LrgVehicleCameraPrivate *priv;

    self = LRG_VEHICLE_CAMERA (object);
    priv = lrg_vehicle_camera_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_VEHICLE:
        lrg_vehicle_camera_set_vehicle (self, g_value_get_object (value));
        break;

    case PROP_MODE:
        lrg_vehicle_camera_set_mode (self, g_value_get_int (value));
        break;

    case PROP_FOLLOW_DISTANCE:
        priv->follow_distance = g_value_get_float (value);
        break;

    case PROP_FOLLOW_HEIGHT:
        priv->follow_height = g_value_get_float (value);
        break;

    case PROP_SMOOTHING:
        priv->smoothing = g_value_get_float (value);
        break;

    case PROP_LOOK_AHEAD:
        priv->look_ahead = g_value_get_boolean (value);
        break;

    case PROP_LOOK_AHEAD_DISTANCE:
        priv->look_ahead_distance = g_value_get_float (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_vehicle_camera_class_init (LrgVehicleCameraClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_vehicle_camera_dispose;
    object_class->get_property = lrg_vehicle_camera_get_property;
    object_class->set_property = lrg_vehicle_camera_set_property;

    klass->update_camera = lrg_vehicle_camera_real_update;

    properties[PROP_VEHICLE] =
        g_param_spec_object ("vehicle",
                             "Vehicle",
                             "Vehicle to follow",
                             LRG_TYPE_VEHICLE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    properties[PROP_MODE] =
        g_param_spec_int ("mode",
                          "Mode",
                          "Camera mode",
                          LRG_VEHICLE_CAMERA_FOLLOW, LRG_VEHICLE_CAMERA_FREE,
                          LRG_VEHICLE_CAMERA_FOLLOW,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FOLLOW_DISTANCE] =
        g_param_spec_float ("follow-distance",
                            "Follow Distance",
                            "Distance behind vehicle in follow mode",
                            1.0f, 100.0f, DEFAULT_FOLLOW_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FOLLOW_HEIGHT] =
        g_param_spec_float ("follow-height",
                            "Follow Height",
                            "Height above vehicle in follow mode",
                            0.0f, 50.0f, DEFAULT_FOLLOW_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SMOOTHING] =
        g_param_spec_float ("smoothing",
                            "Smoothing",
                            "Camera movement smoothing (0=instant, 1=max)",
                            0.0f, 1.0f, DEFAULT_SMOOTHING,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOK_AHEAD] =
        g_param_spec_boolean ("look-ahead",
                              "Look Ahead",
                              "Whether camera looks ahead based on speed",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LOOK_AHEAD_DISTANCE] =
        g_param_spec_float ("look-ahead-distance",
                            "Look Ahead Distance",
                            "Maximum look-ahead distance at full speed",
                            0.0f, 50.0f, DEFAULT_LOOK_AHEAD_DISTANCE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_vehicle_camera_init (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->vehicle = NULL;
    priv->mode = LRG_VEHICLE_CAMERA_FOLLOW;

    priv->follow_distance = DEFAULT_FOLLOW_DISTANCE;
    priv->follow_height = DEFAULT_FOLLOW_HEIGHT;

    priv->smoothing = DEFAULT_SMOOTHING;

    priv->look_ahead = TRUE;
    priv->look_ahead_distance = DEFAULT_LOOK_AHEAD_DISTANCE;

    /* Default hood offset: front of car, hood level */
    priv->hood_offset_x = 0.0f;
    priv->hood_offset_y = 1.2f;
    priv->hood_offset_z = 1.5f;

    /* Default cockpit offset: driver's seat */
    priv->cockpit_offset_x = -0.3f;
    priv->cockpit_offset_y = 1.0f;
    priv->cockpit_offset_z = 0.5f;

    /* Free camera initial state */
    priv->free_yaw = 0.0f;
    priv->free_pitch = DEFAULT_FREE_PITCH;
    priv->free_distance = DEFAULT_FREE_DISTANCE;

    /* Current position */
    priv->current_x = 0.0f;
    priv->current_y = 10.0f;
    priv->current_z = 10.0f;
}

/*
 * smooth_lerp:
 *
 * Smooth interpolation with delta time compensation.
 */
static gfloat
smooth_lerp (gfloat current,
             gfloat target,
             gfloat smoothing,
             gfloat delta)
{
    gfloat rate;

    if (smoothing <= 0.0f)
        return target;

    rate = 1.0f - powf (smoothing, delta * 10.0f);

    return current + (target - current) * rate;
}

/*
 * lrg_vehicle_camera_real_update:
 *
 * Default implementation of camera update.
 */
static void
lrg_vehicle_camera_real_update (LrgVehicleCamera *self,
                                gfloat            delta)
{
    LrgVehicleCameraPrivate *priv;
    gfloat veh_x, veh_y, veh_z;
    gfloat forward_x, forward_y, forward_z;
    gfloat target_cam_x, target_cam_y, target_cam_z;
    gfloat target_look_x, target_look_y, target_look_z;
    gfloat heading;
    gfloat speed;
    gfloat max_speed;

    priv = lrg_vehicle_camera_get_instance_private (self);

    if (priv->vehicle == NULL)
        return;

    /* Get vehicle state */
    lrg_vehicle_get_position (priv->vehicle, &veh_x, &veh_y, &veh_z);
    lrg_vehicle_get_forward_vector (priv->vehicle, &forward_x, &forward_y, &forward_z);
    heading = lrg_vehicle_get_heading (priv->vehicle);
    speed = lrg_vehicle_get_speed (priv->vehicle);
    max_speed = lrg_vehicle_get_max_speed (priv->vehicle);

    switch (priv->mode)
    {
    case LRG_VEHICLE_CAMERA_FOLLOW:
        {
            gfloat look_ahead_offset;

            /* Calculate camera position behind vehicle */
            target_cam_x = veh_x - forward_x * priv->follow_distance;
            target_cam_y = veh_y + priv->follow_height;
            target_cam_z = veh_z - forward_z * priv->follow_distance;

            /* Look-ahead based on speed */
            look_ahead_offset = 0.0f;
            if (priv->look_ahead && max_speed > 0.0f)
            {
                look_ahead_offset = (speed / max_speed) * priv->look_ahead_distance;
            }

            target_look_x = veh_x + forward_x * look_ahead_offset;
            target_look_y = veh_y + 1.0f; /* Look at roof level */
            target_look_z = veh_z + forward_z * look_ahead_offset;
        }
        break;

    case LRG_VEHICLE_CAMERA_HOOD:
        {
            /* Camera attached to hood, rotates with vehicle */
            gfloat cos_yaw = cosf (heading);
            gfloat sin_yaw = sinf (heading);

            /* Rotate hood offset by vehicle heading */
            target_cam_x = veh_x + priv->hood_offset_x * cos_yaw -
                           priv->hood_offset_z * sin_yaw;
            target_cam_y = veh_y + priv->hood_offset_y;
            target_cam_z = veh_z + priv->hood_offset_x * sin_yaw +
                           priv->hood_offset_z * cos_yaw;

            /* Look ahead in vehicle's forward direction */
            target_look_x = veh_x + forward_x * 50.0f;
            target_look_y = veh_y + 1.0f;
            target_look_z = veh_z + forward_z * 50.0f;
        }
        break;

    case LRG_VEHICLE_CAMERA_COCKPIT:
        {
            /* Camera inside cockpit */
            gfloat cos_yaw = cosf (heading);
            gfloat sin_yaw = sinf (heading);

            /* Rotate cockpit offset by vehicle heading */
            target_cam_x = veh_x + priv->cockpit_offset_x * cos_yaw -
                           priv->cockpit_offset_z * sin_yaw;
            target_cam_y = veh_y + priv->cockpit_offset_y;
            target_cam_z = veh_z + priv->cockpit_offset_x * sin_yaw +
                           priv->cockpit_offset_z * cos_yaw;

            /* Look ahead in vehicle's forward direction */
            target_look_x = veh_x + forward_x * 50.0f;
            target_look_y = veh_y + 1.0f;
            target_look_z = veh_z + forward_z * 50.0f;
        }
        break;

    case LRG_VEHICLE_CAMERA_FREE:
        {
            /* Orbit camera around vehicle */
            gfloat cos_yaw = cosf (priv->free_yaw);
            gfloat sin_yaw = sinf (priv->free_yaw);
            gfloat cos_pitch = cosf (priv->free_pitch);
            gfloat sin_pitch = sinf (priv->free_pitch);

            target_cam_x = veh_x + sin_yaw * cos_pitch * priv->free_distance;
            target_cam_y = veh_y + sin_pitch * priv->free_distance;
            target_cam_z = veh_z + cos_yaw * cos_pitch * priv->free_distance;

            target_look_x = veh_x;
            target_look_y = veh_y + 1.0f;
            target_look_z = veh_z;
        }
        break;

    default:
        return;
    }

    /* Apply smoothing to camera position */
    priv->current_x = smooth_lerp (priv->current_x, target_cam_x, priv->smoothing, delta);
    priv->current_y = smooth_lerp (priv->current_y, target_cam_y, priv->smoothing, delta);
    priv->current_z = smooth_lerp (priv->current_z, target_cam_z, priv->smoothing, delta);

    /* Update base camera */
    lrg_camera3d_set_position_xyz (LRG_CAMERA3D (self),
                                   priv->current_x, priv->current_y, priv->current_z);
    lrg_camera3d_set_target_xyz (LRG_CAMERA3D (self),
                                 target_look_x, target_look_y, target_look_z);
}

/*
 * Public API
 */

LrgVehicleCamera *
lrg_vehicle_camera_new (void)
{
    return g_object_new (LRG_TYPE_VEHICLE_CAMERA, NULL);
}

void
lrg_vehicle_camera_set_vehicle (LrgVehicleCamera *self,
                                LrgVehicle       *vehicle)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));
    g_return_if_fail (vehicle == NULL || LRG_IS_VEHICLE (vehicle));

    priv = lrg_vehicle_camera_get_instance_private (self);

    if (priv->vehicle == vehicle)
        return;

    g_clear_object (&priv->vehicle);
    if (vehicle != NULL)
        priv->vehicle = g_object_ref (vehicle);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VEHICLE]);
}

LrgVehicle *
lrg_vehicle_camera_get_vehicle (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), NULL);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->vehicle;
}

void
lrg_vehicle_camera_set_mode (LrgVehicleCamera     *self,
                             LrgVehicleCameraMode  mode)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    if (priv->mode != mode)
    {
        priv->mode = mode;

        /* Reset free camera when switching to free mode */
        if (mode == LRG_VEHICLE_CAMERA_FREE)
        {
            priv->free_yaw = lrg_vehicle_get_heading (priv->vehicle) + G_PI;
            priv->free_pitch = DEFAULT_FREE_PITCH;
            priv->free_distance = DEFAULT_FREE_DISTANCE;
        }

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
    }
}

LrgVehicleCameraMode
lrg_vehicle_camera_get_mode (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), LRG_VEHICLE_CAMERA_FOLLOW);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->mode;
}

void
lrg_vehicle_camera_cycle_mode (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;
    LrgVehicleCameraMode new_mode;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    new_mode = (priv->mode + 1) % 4;
    lrg_vehicle_camera_set_mode (self, new_mode);
}

void
lrg_vehicle_camera_set_follow_distance (LrgVehicleCamera *self,
                                        gfloat            distance)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));
    g_return_if_fail (distance > 0.0f);

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->follow_distance = distance;
}

gfloat
lrg_vehicle_camera_get_follow_distance (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), DEFAULT_FOLLOW_DISTANCE);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->follow_distance;
}

void
lrg_vehicle_camera_set_follow_height (LrgVehicleCamera *self,
                                      gfloat            height)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->follow_height = height;
}

gfloat
lrg_vehicle_camera_get_follow_height (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), DEFAULT_FOLLOW_HEIGHT);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->follow_height;
}

void
lrg_vehicle_camera_set_smoothing (LrgVehicleCamera *self,
                                  gfloat            smoothing)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->smoothing = CLAMP (smoothing, 0.0f, 1.0f);
}

gfloat
lrg_vehicle_camera_get_smoothing (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), DEFAULT_SMOOTHING);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->smoothing;
}

void
lrg_vehicle_camera_set_look_ahead (LrgVehicleCamera *self,
                                   gboolean          enabled)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->look_ahead = enabled;
}

gboolean
lrg_vehicle_camera_get_look_ahead (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), TRUE);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->look_ahead;
}

void
lrg_vehicle_camera_set_look_ahead_distance (LrgVehicleCamera *self,
                                            gfloat            distance)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));
    g_return_if_fail (distance >= 0.0f);

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->look_ahead_distance = distance;
}

gfloat
lrg_vehicle_camera_get_look_ahead_distance (LrgVehicleCamera *self)
{
    LrgVehicleCameraPrivate *priv;

    g_return_val_if_fail (LRG_IS_VEHICLE_CAMERA (self), DEFAULT_LOOK_AHEAD_DISTANCE);

    priv = lrg_vehicle_camera_get_instance_private (self);

    return priv->look_ahead_distance;
}

void
lrg_vehicle_camera_set_hood_offset (LrgVehicleCamera *self,
                                    gfloat            x,
                                    gfloat            y,
                                    gfloat            z)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->hood_offset_x = x;
    priv->hood_offset_y = y;
    priv->hood_offset_z = z;
}

void
lrg_vehicle_camera_set_cockpit_offset (LrgVehicleCamera *self,
                                       gfloat            x,
                                       gfloat            y,
                                       gfloat            z)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->cockpit_offset_x = x;
    priv->cockpit_offset_y = y;
    priv->cockpit_offset_z = z;
}

void
lrg_vehicle_camera_rotate_free (LrgVehicleCamera *self,
                                gfloat            yaw_delta,
                                gfloat            pitch_delta)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->free_yaw += yaw_delta;
    priv->free_pitch += pitch_delta;

    /* Clamp pitch to prevent flipping */
    priv->free_pitch = CLAMP (priv->free_pitch, 0.1f, G_PI / 2.0f - 0.1f);

    /* Keep yaw in range */
    while (priv->free_yaw > G_PI * 2.0f)
        priv->free_yaw -= G_PI * 2.0f;
    while (priv->free_yaw < 0.0f)
        priv->free_yaw += G_PI * 2.0f;
}

void
lrg_vehicle_camera_zoom_free (LrgVehicleCamera *self,
                              gfloat            delta)
{
    LrgVehicleCameraPrivate *priv;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));

    priv = lrg_vehicle_camera_get_instance_private (self);

    priv->free_distance -= delta;
    priv->free_distance = CLAMP (priv->free_distance, 3.0f, 50.0f);
}

void
lrg_vehicle_camera_update (LrgVehicleCamera *self,
                           gfloat            delta)
{
    LrgVehicleCameraClass *klass;

    g_return_if_fail (LRG_IS_VEHICLE_CAMERA (self));
    g_return_if_fail (delta > 0.0f);

    klass = LRG_VEHICLE_CAMERA_GET_CLASS (self);

    if (klass->update_camera != NULL)
        klass->update_camera (self, delta);
}
