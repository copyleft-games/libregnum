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

    /*
     * Mode.  In free mode pos_* is the camera; in orbit mode pos_* is the
     * subject the camera circles at distance, and cam_* is where the
     * camera really is (both modes).
     */
    LrgPhotoCameraMode mode;
    gfloat cam_x, cam_y, cam_z;
    gfloat distance;
    gfloat target_distance;
    gfloat initial_distance;

    /* Limits */
    gfloat anchor_x, anchor_y, anchor_z;
    gfloat max_distance;
    gboolean collision_enabled;
    gfloat collision_radius;
    LrgPhotoCollisionFunc collision_func;
    gpointer collision_data;
    GDestroyNotify collision_notify;
};

/* the orbit distance limits */
#define ORBIT_MIN_DISTANCE (0.5f)
#define ORBIT_MAX_DISTANCE (500.0f)

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
    PROP_MODE,
    PROP_ORBIT_DISTANCE,
    PROP_MAX_DISTANCE,
    PROP_COLLISION_ENABLED,
    PROP_COLLISION_RADIUS,
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

/* the unit view direction of a yaw and pitch in degrees */
static void
direction_from (gfloat  yaw,
                gfloat  pitch,
                gfloat *dir)
{
    gfloat yaw_rad = yaw * (G_PI / 180.0f);
    gfloat pitch_rad = pitch * (G_PI / 180.0f);

    dir[0] = cosf (pitch_rad) * sinf (yaw_rad);
    dir[1] = sinf (pitch_rad);
    dir[2] = cosf (pitch_rad) * cosf (yaw_rad);
}

/* keeps (x, y, z) inside the sphere of max_distance around the anchor */
static gboolean
clamp_to_anchor (LrgPhotoCameraController *self,
                 gfloat                   *x,
                 gfloat                   *y,
                 gfloat                   *z)
{
    gfloat dx = *x - self->anchor_x;
    gfloat dy = *y - self->anchor_y;
    gfloat dz = *z - self->anchor_z;
    gfloat len = sqrtf (dx * dx + dy * dy + dz * dz);
    gfloat scale;

    if (self->max_distance <= 0.0f || len <= self->max_distance)
        return FALSE;
    scale = self->max_distance / len;
    *x = self->anchor_x + dx * scale;
    *y = self->anchor_y + dy * scale;
    *z = self->anchor_z + dz * scale;
    return TRUE;
}

/*
 * Works out where the camera is and applies the limits.  The maximum
 * distance keeps the camera (free mode) or the subject (orbit mode) and
 * the camera in a sphere around the anchor; the collision function, when
 * on, sweeps from the previous camera (free mode) or from the subject
 * (orbit mode, a spring arm) to the new position and may pull it back.
 */
static void
resolve_camera (LrgPhotoCameraController *self,
                const gfloat             *dir)
{
    gfloat want[3];
    gfloat from[3];
    gfloat out[3];

    /* the subject / camera inside the sphere; targets too, so pushing
     * against the edge does not wind up */
    clamp_to_anchor (self, &self->pos_x, &self->pos_y, &self->pos_z);
    clamp_to_anchor (self, &self->target_pos_x, &self->target_pos_y, &self->target_pos_z);

    if (self->mode == LRG_PHOTO_CAMERA_MODE_ORBIT)
    {
        want[0] = self->pos_x - dir[0] * self->distance;
        want[1] = self->pos_y - dir[1] * self->distance;
        want[2] = self->pos_z - dir[2] * self->distance;
        from[0] = self->pos_x;
        from[1] = self->pos_y;
        from[2] = self->pos_z;
    }
    else
    {
        want[0] = self->pos_x;
        want[1] = self->pos_y;
        want[2] = self->pos_z;
        from[0] = self->cam_x;
        from[1] = self->cam_y;
        from[2] = self->cam_z;
    }
    clamp_to_anchor (self, &want[0], &want[1], &want[2]);

    if (self->collision_enabled && self->collision_func != NULL)
    {
        out[0] = want[0];
        out[1] = want[1];
        out[2] = want[2];
        if (self->collision_func (from, want, self->collision_radius, out, self->collision_data))
        {
            want[0] = out[0];
            want[1] = out[1];
            want[2] = out[2];
            if (self->mode == LRG_PHOTO_CAMERA_MODE_FREE)
            {
                /* blocked: the camera stays where it was let to go */
                self->pos_x = self->target_pos_x = want[0];
                self->pos_y = self->target_pos_y = want[1];
                self->pos_z = self->target_pos_z = want[2];
            }
        }
    }
    self->cam_x = want[0];
    self->cam_y = want[1];
    self->cam_z = want[2];
}

static void
update_camera_from_state (LrgPhotoCameraController *self)
{
    gfloat dir[3];
    gfloat right[3];
    gfloat up[3];
    gfloat roll_rad;
    gfloat len;
    g_autoptr(GrlVector3) position = NULL;
    g_autoptr(GrlVector3) target = NULL;
    g_autoptr(GrlVector3) up_vector = NULL;

    direction_from (self->yaw, self->pitch, dir);
    resolve_camera (self, dir);

    /* the camera looks along dir; roll turns the up vector about it */
    right[0] = dir[1] * 0.0f - dir[2] * 1.0f;
    right[1] = dir[2] * 0.0f - dir[0] * 0.0f;
    right[2] = dir[0] * 1.0f - dir[1] * 0.0f;
    len = sqrtf (right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (len < 1e-6f)
    {
        right[0] = 1.0f;
        right[1] = 0.0f;
        right[2] = 0.0f;
    }
    else
    {
        right[0] /= len;
        right[1] /= len;
        right[2] /= len;
    }
    up[0] = right[1] * dir[2] - right[2] * dir[1];
    up[1] = right[2] * dir[0] - right[0] * dir[2];
    up[2] = right[0] * dir[1] - right[1] * dir[0];
    roll_rad = self->roll * (G_PI / 180.0f);
    up[0] = up[0] * cosf (roll_rad) + right[0] * sinf (roll_rad);
    up[1] = up[1] * cosf (roll_rad) + right[1] * sinf (roll_rad);
    up[2] = up[2] * cosf (roll_rad) + right[2] * sinf (roll_rad);

    position = grl_vector3_new (self->cam_x, self->cam_y, self->cam_z);
    target = grl_vector3_new (self->cam_x + dir[0], self->cam_y + dir[1], self->cam_z + dir[2]);
    up_vector = grl_vector3_new (up[0], up[1], up[2]);

    lrg_camera3d_set_position (self->camera, position);
    lrg_camera3d_set_target (self->camera, target);
    lrg_camera3d_set_up (self->camera, up_vector);
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
    if (self->collision_notify != NULL)
        self->collision_notify (self->collision_data);

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
    case PROP_MODE:
        g_value_set_enum (value, self->mode);
        break;
    case PROP_ORBIT_DISTANCE:
        g_value_set_float (value, self->distance);
        break;
    case PROP_MAX_DISTANCE:
        g_value_set_float (value, self->max_distance);
        break;
    case PROP_COLLISION_ENABLED:
        g_value_set_boolean (value, self->collision_enabled);
        break;
    case PROP_COLLISION_RADIUS:
        g_value_set_float (value, self->collision_radius);
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
    case PROP_MODE:
        lrg_photo_camera_controller_set_mode (self, g_value_get_enum (value));
        break;
    case PROP_ORBIT_DISTANCE:
        lrg_photo_camera_controller_set_orbit_distance (self, g_value_get_float (value));
        break;
    case PROP_MAX_DISTANCE:
        lrg_photo_camera_controller_set_max_distance (self, g_value_get_float (value));
        break;
    case PROP_COLLISION_ENABLED:
        lrg_photo_camera_controller_set_collision_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_COLLISION_RADIUS:
        lrg_photo_camera_controller_set_collision_radius (self, g_value_get_float (value));
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

    /**
     * LrgPhotoCameraController:mode:
     *
     * Free camera or orbit around a subject.
     *
     * Since: 1.0
     */
    properties[PROP_MODE] =
        g_param_spec_enum ("mode", "Mode", "Free camera or orbit",
                           LRG_TYPE_PHOTO_CAMERA_MODE, LRG_PHOTO_CAMERA_MODE_FREE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:orbit-distance:
     *
     * Distance from the subject in orbit mode.
     *
     * Since: 1.0
     */
    properties[PROP_ORBIT_DISTANCE] =
        g_param_spec_float ("orbit-distance", "Orbit distance", "Distance from the subject",
                            ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE, 5.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:max-distance:
     *
     * How far the camera may stray from the anchor, 0 for no limit.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_DISTANCE] =
        g_param_spec_float ("max-distance", "Max distance", "How far from the anchor",
                            0.0f, 1000000.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:collision-enabled:
     *
     * Whether the collision function blocks the camera.
     *
     * Since: 1.0
     */
    properties[PROP_COLLISION_ENABLED] =
        g_param_spec_boolean ("collision-enabled", "Collision enabled",
                              "Whether the collision function blocks the camera", FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoCameraController:collision-radius:
     *
     * The camera's radius for collision.
     *
     * Since: 1.0
     */
    properties[PROP_COLLISION_RADIUS] =
        g_param_spec_float ("collision-radius", "Collision radius", "The camera's radius",
                            0.0f, 100.0f, 0.3f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

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

    /* Default mode and limits: free, no limit, collision off */
    self->mode = LRG_PHOTO_CAMERA_MODE_FREE;
    self->distance = 5.0f;
    self->target_distance = 5.0f;
    self->initial_distance = 5.0f;
    self->anchor_x = self->pos_x;
    self->anchor_y = self->pos_y;
    self->anchor_z = self->pos_z;
    self->cam_x = self->pos_x;
    self->cam_y = self->pos_y;
    self->cam_z = self->pos_z;
    self->max_distance = 0.0f;
    self->collision_enabled = FALSE;
    self->collision_radius = 0.3f;

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

    /* Copy the direction the source camera looks along */
    {
        g_autoptr(GrlVector3) aim = lrg_camera3d_get_target (camera);
        gfloat dx = aim->x - position->x;
        gfloat dy = aim->y - position->y;
        gfloat dz = aim->z - position->z;
        gfloat len = sqrtf (dx * dx + dy * dy + dz * dz);

        if (len > 1e-6f)
        {
            self->yaw = atan2f (dx, dz) * (180.0f / G_PI);
            self->pitch = clamp (asinf (dy / len) * (180.0f / G_PI), -89.0f, 89.0f);
            self->target_yaw = self->yaw;
            self->target_pitch = self->pitch;
        }
    }

    /* Copy FOV */
    self->fov = lrg_camera3d_get_fovy (camera);

    /* Store initial state; the anchor of the distance limit is here */
    self->initial_pos_x = self->pos_x;
    self->initial_pos_y = self->pos_y;
    self->initial_pos_z = self->pos_z;
    self->initial_yaw = self->yaw;
    self->initial_pitch = self->pitch;
    self->initial_fov = self->fov;
    self->anchor_x = self->pos_x;
    self->anchor_y = self->pos_y;
    self->anchor_z = self->pos_z;
    self->cam_x = self->pos_x;
    self->cam_y = self->pos_y;
    self->cam_z = self->pos_z;

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

    /* where the camera is, whatever the mode */
    return grl_vector3_new (self->cam_x, self->cam_y, self->cam_z);
}

void
lrg_photo_camera_controller_set_position (LrgPhotoCameraController *self,
                                          GrlVector3               *position)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));
    g_return_if_fail (position != NULL);

    if (self->mode == LRG_PHOTO_CAMERA_MODE_ORBIT)
    {
        gfloat dir[3];

        /* the camera goes there: the subject stays distance ahead of it */
        direction_from (self->yaw, self->pitch, dir);
        self->pos_x = position->x + dir[0] * self->distance;
        self->pos_y = position->y + dir[1] * self->distance;
        self->pos_z = position->z + dir[2] * self->distance;
    }
    else
    {
        self->pos_x = position->x;
        self->pos_y = position->y;
        self->pos_z = position->z;
    }
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

    /* orbit mode: forward is zoom towards the subject */
    if (self->mode == LRG_PHOTO_CAMERA_MODE_ORBIT)
    {
        self->target_distance = clamp (self->target_distance - amount * self->move_speed,
                                       ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE);
        return;
    }

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

    /*
     * Right is forward x up for raylib's right handed, Y up world: at yaw 0
     * the camera looks along +z and right is -x.  (It used to be the
     * mirror image, so strafing right went left.)
     */
    right_x = -cosf (yaw_rad);
    right_z = sinf (yaw_rad);

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

    /* back to the free camera at the start */
    self->mode = LRG_PHOTO_CAMERA_MODE_FREE;
    self->distance = self->initial_distance;
    self->target_distance = self->initial_distance;
    self->cam_x = self->pos_x;
    self->cam_y = self->pos_y;
    self->cam_z = self->pos_z;

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
    self->distance = lerp (self->distance, self->target_distance, smooth_factor);

    /* Update the underlying camera */
    update_camera_from_state (self);
}

/* ==========================================================================
 * Orbit mode and limits
 * ========================================================================== */

LrgPhotoCameraMode
lrg_photo_camera_controller_get_mode (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), LRG_PHOTO_CAMERA_MODE_FREE);

    return self->mode;
}

void
lrg_photo_camera_controller_set_mode (LrgPhotoCameraController *self,
                                      LrgPhotoCameraMode         mode)
{
    gfloat dir[3];

    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    if (self->mode == mode)
        return;

    /* the view stays as it is: the subject sits distance ahead of the
     * camera, or the camera sits where it was */
    direction_from (self->yaw, self->pitch, dir);
    if (mode == LRG_PHOTO_CAMERA_MODE_ORBIT)
    {
        self->pos_x = self->cam_x + dir[0] * self->distance;
        self->pos_y = self->cam_y + dir[1] * self->distance;
        self->pos_z = self->cam_z + dir[2] * self->distance;
    }
    else
    {
        self->pos_x = self->cam_x;
        self->pos_y = self->cam_y;
        self->pos_z = self->cam_z;
    }
    self->target_pos_x = self->pos_x;
    self->target_pos_y = self->pos_y;
    self->target_pos_z = self->pos_z;
    self->target_distance = self->distance;
    self->mode = mode;
    update_camera_from_state (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

gfloat
lrg_photo_camera_controller_get_orbit_distance (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->distance;
}

void
lrg_photo_camera_controller_set_orbit_distance (LrgPhotoCameraController *self,
                                                gfloat                    distance)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->distance = clamp (distance, ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE);
    self->target_distance = self->distance;
    update_camera_from_state (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORBIT_DISTANCE]);
}

void
lrg_photo_camera_controller_focus_on (LrgPhotoCameraController *self,
                                      gfloat                    x,
                                      gfloat                    y,
                                      gfloat                    z)
{
    gfloat dx;
    gfloat dy;
    gfloat dz;
    gfloat len;

    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    /* the camera stays where it is and turns to the subject */
    dx = x - self->cam_x;
    dy = y - self->cam_y;
    dz = z - self->cam_z;
    len = sqrtf (dx * dx + dy * dy + dz * dz);
    if (len < ORBIT_MIN_DISTANCE)
        len = ORBIT_MIN_DISTANCE;
    self->yaw = atan2f (dx, dz) * (180.0f / G_PI);
    self->pitch = clamp (asinf (clamp (dy / len, -1.0f, 1.0f)) * (180.0f / G_PI), -89.0f, 89.0f);
    self->target_yaw = self->yaw;
    self->target_pitch = self->pitch;
    self->distance = clamp (len, ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE);
    self->target_distance = self->distance;
    self->pos_x = self->target_pos_x = x;
    self->pos_y = self->target_pos_y = y;
    self->pos_z = self->target_pos_z = z;
    self->mode = LRG_PHOTO_CAMERA_MODE_ORBIT;
    update_camera_from_state (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

void
lrg_photo_camera_controller_orbit (LrgPhotoCameraController *self,
                                   gfloat                    delta_yaw,
                                   gfloat                    delta_pitch)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    /* the same turn as free look: the camera swings round the subject
     * because its position is the subject minus the view direction */
    lrg_photo_camera_controller_rotate (self, delta_yaw, delta_pitch);
}

void
lrg_photo_camera_controller_zoom (LrgPhotoCameraController *self,
                                  gfloat                    amount)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    if (self->mode == LRG_PHOTO_CAMERA_MODE_ORBIT)
        lrg_photo_camera_controller_move_forward (self, amount);
    else
        lrg_photo_camera_controller_set_fov (self, self->fov - amount * 10.0f);
}

void
lrg_photo_camera_controller_set_anchor (LrgPhotoCameraController *self,
                                        gfloat                    x,
                                        gfloat                    y,
                                        gfloat                    z)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->anchor_x = x;
    self->anchor_y = y;
    self->anchor_z = z;
    update_camera_from_state (self);
}

void
lrg_photo_camera_controller_get_anchor (LrgPhotoCameraController *self,
                                        gfloat                   *x,
                                        gfloat                   *y,
                                        gfloat                   *z)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    if (x != NULL)
        *x = self->anchor_x;
    if (y != NULL)
        *y = self->anchor_y;
    if (z != NULL)
        *z = self->anchor_z;
}

gfloat
lrg_photo_camera_controller_get_max_distance (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->max_distance;
}

void
lrg_photo_camera_controller_set_max_distance (LrgPhotoCameraController *self,
                                              gfloat                    distance)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->max_distance = clamp (distance, 0.0f, 1000000.0f);
    update_camera_from_state (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DISTANCE]);
}

gfloat
lrg_photo_camera_controller_get_distance_to_anchor (LrgPhotoCameraController *self)
{
    gfloat dx;
    gfloat dy;
    gfloat dz;

    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    dx = self->cam_x - self->anchor_x;
    dy = self->cam_y - self->anchor_y;
    dz = self->cam_z - self->anchor_z;
    return sqrtf (dx * dx + dy * dy + dz * dz);
}

gboolean
lrg_photo_camera_controller_get_collision_enabled (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), FALSE);

    return self->collision_enabled;
}

void
lrg_photo_camera_controller_set_collision_enabled (LrgPhotoCameraController *self,
                                                   gboolean                  enabled)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    enabled = !!enabled;
    if (self->collision_enabled == enabled)
        return;
    self->collision_enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_ENABLED]);
}

gfloat
lrg_photo_camera_controller_get_collision_radius (LrgPhotoCameraController *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self), 0.0f);

    return self->collision_radius;
}

void
lrg_photo_camera_controller_set_collision_radius (LrgPhotoCameraController *self,
                                                  gfloat                    radius)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    self->collision_radius = clamp (radius, 0.0f, 100.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_RADIUS]);
}

void
lrg_photo_camera_controller_set_collision_func (LrgPhotoCameraController *self,
                                                LrgPhotoCollisionFunc     func,
                                                gpointer                  user_data,
                                                GDestroyNotify            notify)
{
    g_return_if_fail (LRG_IS_PHOTO_CAMERA_CONTROLLER (self));

    if (self->collision_notify != NULL)
        self->collision_notify (self->collision_data);
    self->collision_func = func;
    self->collision_data = user_data;
    self->collision_notify = notify;
}
