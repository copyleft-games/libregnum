/* lrg-photo-camera-controller.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoCameraController - Free camera controls for photo mode.
 */

#include "config.h"

#include <math.h>
#include "lrg-photo-camera-controller.h"
#include "../lrg-log.h"

struct _LrgPhotoCameraController
{
    GObject parent_instance;

    LrgCamera3D *camera;

    /* Current camera state */
    gfloat pos_x, pos_y, pos_z;
    gfloat yaw;
    gfloat pitch;
    gfloat roll;
    gfloat fov;

    /* Target state for smoothing */
    gfloat target_pos_x, target_pos_y, target_pos_z;
    gfloat target_yaw;
    gfloat target_pitch;

    /* Initial state for reset */
    gfloat initial_pos_x, initial_pos_y, initial_pos_z;
    gfloat initial_yaw;
    gfloat initial_pitch;
    gfloat initial_fov;

    /* Configuration */
    gfloat move_speed;
    gfloat look_sensitivity;
    gfloat smoothing;
};

enum
{
    PROP_0,
    PROP_MOVE_SPEED,
    PROP_LOOK_SENSITIVITY,
    PROP_SMOOTHING,
    PROP_YAW,
    PROP_PITCH,
    PROP_ROLL,
    PROP_FOV,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgPhotoCameraController, lrg_photo_camera_controller, G_TYPE_OBJECT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat
lerp (gfloat a, gfloat b, gfloat t)
{
    return a + (b - a) * t;
}

static gfloat
clamp (gfloat value, gfloat min_val, gfloat max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

static void
update_camera_from_state (LrgPhotoCameraController *self)
{
    gfloat yaw_rad;
    gfloat pitch_rad;
    gfloat dir_x, dir_y, dir_z;
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) target = NULL;

    /* Convert angles to radians */
    yaw_rad = self->yaw * (G_PI / 180.0f);
    pitch_rad = self->pitch * (G_PI / 180.0f);

    /* Calculate forward direction from yaw and pitch */
    dir_x = cosf (pitch_rad) * sinf (yaw_rad);
    dir_y = sinf (pitch_rad);
    dir_z = cosf (pitch_rad) * cosf (yaw_rad);

    /* Update camera position and target */
    position = grl_vector3_new (self->pos_x, self->pos_y, self->pos_z);
    target = grl_vector3_new (self->pos_x + dir_x,
                              self->pos_y + dir_y,
                              self->pos_z + dir_z);

    lrg_camera3d_set_position (self->camera, position);
    lrg_camera3d_set_target (self->camera, target);
    lrg_camera3d_set_fovy (self->camera, self->fov);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_photo_camera_controller_finalize (GObject *object)
{
    LrgPhotoCameraController *self = LRG_PHOTO_CAMERA_CONTROLLER (object);

    g_clear_object (&self->camera);

    G_OBJECT_CLASS (lrg_photo_camera_controller_parent_class)->finalize (object);
}

static void
lrg_photo_camera_controller_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
    LrgPhotoCameraController *self = LRG_PHOTO_CAMERA_CONTROLLER (object);

    switch (prop_id)
    {
    case PROP_MOVE_SPEED:
        g_value_set_float (value, self->move_speed);
        break;
    case PROP_LOOK_SENSITIVITY:
        g_value_set_float (value, self->look_sensitivity);
        break;
    case PROP_SMOOTHING:
        g_value_set_float (value, self->smoothing);
        break;
    case PROP_YAW:
        g_value_set_float (value, self->yaw);
        break;
    case PROP_PITCH:
        g_value_set_float (value, self->pitch);
        break;
    case PROP_ROLL:
        g_value_set_float (value, self->roll);
        break;
    case PROP_FOV:
        g_value_set_float (value, self->fov);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_camera_controller_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
    LrgPhotoCameraController *self = LRG_PHOTO_CAMERA_CONTROLLER (object);

    switch (prop_id)
    {
    case PROP_MOVE_SPEED:
        self->move_speed = g_value_get_float (value);
        break;
    case PROP_LOOK_SENSITIVITY:
        self->look_sensitivity = g_value_get_float (value);
        break;
    case PROP_SMOOTHING:
        self->smoothing = clamp (g_value_get_float (value), 0.0f, 1.0f);
        break;
    case PROP_YAW:
        self->yaw = g_value_get_float (value);
        self->target_yaw = self->yaw;
        break;
    case PROP_PITCH:
        self->pitch = clamp (g_value_get_float (value), -89.0f, 89.0f);
        self->target_pitch = self->pitch;
        break;
    case PROP_ROLL:
        self->roll = g_value_get_float (value);
        break;
    case PROP_FOV:
        self->fov = clamp (g_value_get_float (value), 1.0f, 179.0f);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_camera_controller_class_init (LrgPhotoCameraControllerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_photo_camera_controller_finalize;
    object_class->get_property = lrg_photo_camera_controller_get_property;
    object_class->set_property = lrg_photo_camera_controller_set_property;

    /**
     * LrgPhotoCameraController:move-speed:
     *
     * Movement speed in units per second.
     *
     * Since: 1.0
     */
    properties[PROP_MOVE_SPEED] =
        g_param_spec_float ("move-speed",
                            "Move Speed",
                            "Movement speed in units per second",
                            0.1f, 1000.0f, 10.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:look-sensitivity:
     *
     * Mouse look sensitivity.
     *
     * Since: 1.0
     */
    properties[PROP_LOOK_SENSITIVITY] =
        g_param_spec_float ("look-sensitivity",
                            "Look Sensitivity",
                            "Mouse look sensitivity",
                            0.01f, 10.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:smoothing:
     *
     * Movement smoothing factor (0 = instant, 1 = very smooth).
     *
     * Since: 1.0
     */
    properties[PROP_SMOOTHING] =
        g_param_spec_float ("smoothing",
                            "Smoothing",
                            "Movement smoothing factor",
                            0.0f, 1.0f, 0.8f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:yaw:
     *
     * Camera yaw (horizontal rotation) in degrees.
     *
     * Since: 1.0
     */
    properties[PROP_YAW] =
        g_param_spec_float ("yaw",
                            "Yaw",
                            "Horizontal rotation in degrees",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:pitch:
     *
     * Camera pitch (vertical rotation) in degrees, clamped to -89 to 89.
     *
     * Since: 1.0
     */
    properties[PROP_PITCH] =
        g_param_spec_float ("pitch",
                            "Pitch",
                            "Vertical rotation in degrees",
                            -89.0f, 89.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:roll:
     *
     * Camera roll (tilt) in degrees.
     *
     * Since: 1.0
     */
    properties[PROP_ROLL] =
        g_param_spec_float ("roll",
                            "Roll",
                            "Camera tilt in degrees",
                            -180.0f, 180.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:fov:
     *
     * Field of view in degrees.
     *
     * Since: 1.0
     */
    properties[PROP_FOV] =
        g_param_spec_float ("fov",
                            "FOV",
                            "Field of view in degrees",
                            1.0f, 179.0f, 45.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_photo_camera_controller_init (LrgPhotoCameraController *self)
{
    /* Create internal camera */
    self->camera = lrg_camera3d_new ();

    /* Default position */
    self->pos_x = 0.0f;
    self->pos_y = 10.0f;
    self->pos_z = 10.0f;
    self->target_pos_x = self->pos_x;
    self->target_pos_y = self->pos_y;
    self->target_pos_z = self->pos_z;

    /* Default orientation */
    self->yaw = 0.0f;
    self->pitch = 0.0f;
    self->roll = 0.0f;
    self->target_yaw = self->yaw;
    self->target_pitch = self->pitch;
    self->fov = 45.0f;

    /* Store initial state */
    self->initial_pos_x = self->pos_x;
    self->initial_pos_y = self->pos_y;
    self->initial_pos_z = self->pos_z;
    self->initial_yaw = self->yaw;
    self->initial_pitch = self->pitch;
    self->initial_fov = self->fov;

    /* Default configuration */
    self->move_speed = 10.0f;
    self->look_sensitivity = 0.5f;
    self->smoothing = 0.8f;

    update_camera_from_state (self);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPhotoCameraController *
lrg_photo_camera_controller_new (void)
{
    return g_object_new (LRG_TYPE_PHOTO_CAMERA_CONTROLLER, NULL);
}

LrgPhotoCameraController *
lrg_photo_camera_controller_new_from_camera (LrgCamera3D *camera)
{
    LrgPhotoCameraController *self;
    g_autoptr(GrlVector3) position = NULL;

    g_return_val_if_fail (LRG_IS_CAMERA3D (camera), NULL);

    self = lrg_photo_camera_controller_new ();

    /* Copy position from source camera */
    position = lrg_camera3d_get_position (camera);
    self->pos_x = position->x;
    self->pos_y = position->y;
    self->pos_z = position->z;
    self->target_pos_x = self->pos_x;
    self->target_pos_y = self->pos_y;
    self->target_pos_z = self->pos_z;

    /* Copy FOV */
    self->fov = lrg_camera3d_get_fovy (camera);

    /* Store initial state */
    self->initial_pos_x = self->pos_x;
    self->initial_pos_y = self->pos_y;
    self->initial_pos_z = self->pos_z;
    self->initial_fov = self->fov;

    update_camera_from_state (self);

    return self;
}

LrgCamera3D *
lrg_photo_camera_controller_get_camera (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), NULL);

    return self->camera;
}

GrlVector3 *
lrg_photo_camera_controller_get_position (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), NULL);

    return grl_vector3_new (self->pos_x, self->pos_y, self->pos_z);
}

void
lrg_photo_camera_controller_set_position (LrgPhotoCameraController *self,
                                          GrlVector3               *position)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));
    g_return_if_fail (position != NULL);

    self->pos_x = position->x;
    self->pos_y = position->y;
    self->pos_z = position->z;
    self->target_pos_x = self->pos_x;
    self->target_pos_y = self->pos_y;
    self->target_pos_z = self->pos_z;

    update_camera_from_state (self);
}

gfloat
lrg_photo_camera_controller_get_yaw (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->yaw;
}

void
lrg_photo_camera_controller_set_yaw (LrgPhotoCameraController *self,
                                     gfloat                    yaw)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->yaw = yaw;
    self->target_yaw = yaw;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
}

gfloat
lrg_photo_camera_controller_get_pitch (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->pitch;
}

void
lrg_photo_camera_controller_set_pitch (LrgPhotoCameraController *self,
                                       gfloat                    pitch)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->pitch = clamp (pitch, -89.0f, 89.0f);
    self->target_pitch = self->pitch;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

gfloat
lrg_photo_camera_controller_get_roll (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->roll;
}

void
lrg_photo_camera_controller_set_roll (LrgPhotoCameraController *self,
                                      gfloat                    roll)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->roll = roll;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROLL]);
}

gfloat
lrg_photo_camera_controller_get_move_speed (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->move_speed;
}

void
lrg_photo_camera_controller_set_move_speed (LrgPhotoCameraController *self,
                                            gfloat                    speed)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));
    g_return_if_fail (speed > 0.0f);

    self->move_speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOVE_SPEED]);
}

gfloat
lrg_photo_camera_controller_get_look_sensitivity (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->look_sensitivity;
}

void
lrg_photo_camera_controller_set_look_sensitivity (LrgPhotoCameraController *self,
                                                  gfloat                    sensitivity)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));
    g_return_if_fail (sensitivity > 0.0f);

    self->look_sensitivity = sensitivity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOK_SENSITIVITY]);
}

gfloat
lrg_photo_camera_controller_get_smoothing (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->smoothing;
}

void
lrg_photo_camera_controller_set_smoothing (LrgPhotoCameraController *self,
                                           gfloat                    smoothing)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->smoothing = clamp (smoothing, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SMOOTHING]);
}

gfloat
lrg_photo_camera_controller_get_fov (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->fov;
}

void
lrg_photo_camera_controller_set_fov (LrgPhotoCameraController *self,
                                     gfloat                    fov)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->fov = clamp (fov, 1.0f, 179.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOV]);
}

void
lrg_photo_camera_controller_move_forward (LrgPhotoCameraController *self,
                                          gfloat                    amount)
{
    gfloat yaw_rad;
    gfloat pitch_rad;
    gfloat move_amount;
    gfloat dir_x, dir_y, dir_z;

    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    yaw_rad = self->yaw * (G_PI / 180.0f);
    pitch_rad = self->pitch * (G_PI / 180.0f);
    move_amount = amount * self->move_speed;

    /* Calculate forward direction */
    dir_x = cosf (pitch_rad) * sinf (yaw_rad);
    dir_y = sinf (pitch_rad);
    dir_z = cosf (pitch_rad) * cosf (yaw_rad);

    self->target_pos_x += dir_x * move_amount;
    self->target_pos_y += dir_y * move_amount;
    self->target_pos_z += dir_z * move_amount;
}

void
lrg_photo_camera_controller_move_right (LrgPhotoCameraController *self,
                                        gfloat                    amount)
{
    gfloat yaw_rad;
    gfloat move_amount;
    gfloat right_x, right_z;

    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    yaw_rad = self->yaw * (G_PI / 180.0f);
    move_amount = amount * self->move_speed;

    /* Right vector is perpendicular to forward on XZ plane */
    right_x = cosf (yaw_rad);
    right_z = -sinf (yaw_rad);

    self->target_pos_x += right_x * move_amount;
    self->target_pos_z += right_z * move_amount;
}

void
lrg_photo_camera_controller_move_up (LrgPhotoCameraController *self,
                                     gfloat                    amount)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->target_pos_y += amount * self->move_speed;
}

void
lrg_photo_camera_controller_rotate (LrgPhotoCameraController *self,
                                    gfloat                    delta_yaw,
                                    gfloat                    delta_pitch)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->target_yaw += delta_yaw * self->look_sensitivity;
    self->target_pitch += delta_pitch * self->look_sensitivity;
    self->target_pitch = clamp (self->target_pitch, -89.0f, 89.0f);
}

void
lrg_photo_camera_controller_reset (LrgPhotoCameraController *self)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->pos_x = self->initial_pos_x;
    self->pos_y = self->initial_pos_y;
    self->pos_z = self->initial_pos_z;
    self->target_pos_x = self->pos_x;
    self->target_pos_y = self->pos_y;
    self->target_pos_z = self->pos_z;

    self->yaw = self->initial_yaw;
    self->pitch = self->initial_pitch;
    self->target_yaw = self->yaw;
    self->target_pitch = self->pitch;
    self->roll = 0.0f;

    self->fov = self->initial_fov;

    update_camera_from_state (self);

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE, "Photo camera reset to initial state");
}

void
lrg_photo_camera_controller_update (LrgPhotoCameraController *self,
                                    gfloat                    delta)
{
    gfloat smooth_factor;

    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    /* Calculate smoothing factor based on delta and smoothing setting */
    smooth_factor = 1.0f - powf (self->smoothing, delta * 60.0f);

    /* Interpolate position */
    self->pos_x = lerp (self->pos_x, self->target_pos_x, smooth_factor);
    self->pos_y = lerp (self->pos_y, self->target_pos_y, smooth_factor);
    self->pos_z = lerp (self->pos_z, self->target_pos_z, smooth_factor);

    /* Interpolate rotation */
    self->yaw = lerp (self->yaw, self->target_yaw, smooth_factor);
    self->pitch = lerp (self->pitch, self->target_pitch, smooth_factor);

    /* Update the underlying camera */
    update_camera_from_state (self);
}
