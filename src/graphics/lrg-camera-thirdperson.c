/* lrg-camera-thirdperson.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Third-person camera implementation for 3D games.
 * Provides spherical orbit around a target with smooth following
 * and optional collision avoidance.
 */

#include "lrg-camera-thirdperson.h"
#include "../lrg-log.h"
#include <math.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_GRAPHICS

/* PI constant for angle calculations */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Default values */
#define DEFAULT_DISTANCE         5.0f
#define DEFAULT_MIN_DISTANCE     1.0f
#define DEFAULT_MAX_DISTANCE     20.0f
#define DEFAULT_HEIGHT_OFFSET    1.5f
#define DEFAULT_SHOULDER_OFFSET  0.0f
#define DEFAULT_PITCH            15.0f
#define DEFAULT_YAW              0.0f
#define DEFAULT_PITCH_MIN        -30.0f
#define DEFAULT_PITCH_MAX        60.0f
#define DEFAULT_SENSITIVITY_X    0.15f
#define DEFAULT_SENSITIVITY_Y    0.15f
#define DEFAULT_ORBIT_SMOOTHING  8.0f
#define DEFAULT_FOLLOW_SMOOTHING 10.0f
#define DEFAULT_COLLISION_RADIUS 0.3f
#define DEFAULT_COLLISION_LAYERS 0xFFFFFFFF

/*
 * Private data structure for LrgCameraThirdPerson.
 * Contains orbit state, smoothing state, offsets, and collision settings.
 */
typedef struct _LrgCameraThirdPersonPrivate
{
	/* Orbit distance */
	gfloat distance;
	gfloat actual_distance;
	gfloat min_distance;
	gfloat max_distance;

	/* Orbit angles (in degrees) */
	gfloat target_pitch;
	gfloat target_yaw;
	gfloat current_pitch;
	gfloat current_yaw;

	/* Pitch limits */
	gfloat pitch_min;
	gfloat pitch_max;

	/* Sensitivity */
	gfloat sensitivity_x;
	gfloat sensitivity_y;

	/* Offsets */
	gfloat height_offset;
	gfloat shoulder_offset;

	/* Smoothing */
	gfloat orbit_smoothing;
	gfloat follow_smoothing;

	/* Target position */
	gfloat target_x;
	gfloat target_y;
	gfloat target_z;

	/* Smoothed target position */
	gfloat smoothed_target_x;
	gfloat smoothed_target_y;
	gfloat smoothed_target_z;

	/* Collision avoidance */
	gboolean collision_enabled;
	gfloat collision_radius;
	guint32 collision_layers;
	LrgCameraCollisionCallback collision_callback;
	gpointer collision_user_data;
	GDestroyNotify collision_destroy;

	/* Track if initialized */
	gboolean initialized;
} LrgCameraThirdPersonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCameraThirdPerson, lrg_camera_thirdperson, LRG_TYPE_CAMERA3D)

/* Property IDs */
enum
{
	PROP_0,
	PROP_DISTANCE,
	PROP_ACTUAL_DISTANCE,
	PROP_MIN_DISTANCE,
	PROP_MAX_DISTANCE,
	PROP_PITCH,
	PROP_YAW,
	PROP_PITCH_MIN,
	PROP_PITCH_MAX,
	PROP_SENSITIVITY_X,
	PROP_SENSITIVITY_Y,
	PROP_HEIGHT_OFFSET,
	PROP_SHOULDER_OFFSET,
	PROP_ORBIT_SMOOTHING,
	PROP_FOLLOW_SMOOTHING,
	PROP_COLLISION_ENABLED,
	PROP_COLLISION_RADIUS,
	PROP_COLLISION_LAYERS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Forward declarations */
static void lrg_camera_thirdperson_begin (LrgCamera *camera);
static void update_camera_position (LrgCameraThirdPerson *self);

/*
 * Wrap angle to 0-360 range.
 */
static gfloat
wrap_angle (gfloat angle)
{
	while (angle < 0.0f)
		angle += 360.0f;
	while (angle >= 360.0f)
		angle -= 360.0f;
	return angle;
}

/*
 * Clamp value between min and max.
 */
static gfloat
clamp_float (gfloat value,
             gfloat min,
             gfloat max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

/*
 * Calculate shortest angular distance between two angles.
 * Handles wrap-around properly.
 */
static gfloat
angular_distance (gfloat from,
                  gfloat to)
{
	gfloat diff;

	diff = to - from;

	/* Handle wrap-around */
	while (diff > 180.0f)
		diff -= 360.0f;
	while (diff < -180.0f)
		diff += 360.0f;

	return diff;
}

/*
 * GObject dispose - clean up references.
 */
static void
lrg_camera_thirdperson_dispose (GObject *object)
{
	LrgCameraThirdPerson *self = LRG_CAMERA_THIRDPERSON (object);
	LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Clean up collision callback */
	if (priv->collision_destroy && priv->collision_user_data)
	{
		priv->collision_destroy (priv->collision_user_data);
		priv->collision_user_data = NULL;
	}
	priv->collision_callback = NULL;
	priv->collision_destroy = NULL;

	G_OBJECT_CLASS (lrg_camera_thirdperson_parent_class)->dispose (object);
}

/*
 * GObject get_property implementation.
 */
static void
lrg_camera_thirdperson_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgCameraThirdPerson *self = LRG_CAMERA_THIRDPERSON (object);
	LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_DISTANCE:
		g_value_set_float (value, priv->distance);
		break;
	case PROP_ACTUAL_DISTANCE:
		g_value_set_float (value, priv->actual_distance);
		break;
	case PROP_MIN_DISTANCE:
		g_value_set_float (value, priv->min_distance);
		break;
	case PROP_MAX_DISTANCE:
		g_value_set_float (value, priv->max_distance);
		break;
	case PROP_PITCH:
		g_value_set_float (value, priv->target_pitch);
		break;
	case PROP_YAW:
		g_value_set_float (value, priv->target_yaw);
		break;
	case PROP_PITCH_MIN:
		g_value_set_float (value, priv->pitch_min);
		break;
	case PROP_PITCH_MAX:
		g_value_set_float (value, priv->pitch_max);
		break;
	case PROP_SENSITIVITY_X:
		g_value_set_float (value, priv->sensitivity_x);
		break;
	case PROP_SENSITIVITY_Y:
		g_value_set_float (value, priv->sensitivity_y);
		break;
	case PROP_HEIGHT_OFFSET:
		g_value_set_float (value, priv->height_offset);
		break;
	case PROP_SHOULDER_OFFSET:
		g_value_set_float (value, priv->shoulder_offset);
		break;
	case PROP_ORBIT_SMOOTHING:
		g_value_set_float (value, priv->orbit_smoothing);
		break;
	case PROP_FOLLOW_SMOOTHING:
		g_value_set_float (value, priv->follow_smoothing);
		break;
	case PROP_COLLISION_ENABLED:
		g_value_set_boolean (value, priv->collision_enabled);
		break;
	case PROP_COLLISION_RADIUS:
		g_value_set_float (value, priv->collision_radius);
		break;
	case PROP_COLLISION_LAYERS:
		g_value_set_uint (value, priv->collision_layers);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/*
 * GObject set_property implementation.
 */
static void
lrg_camera_thirdperson_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgCameraThirdPerson *self = LRG_CAMERA_THIRDPERSON (object);

	switch (prop_id)
	{
	case PROP_DISTANCE:
		lrg_camera_thirdperson_set_distance (self, g_value_get_float (value));
		break;
	case PROP_MIN_DISTANCE:
		{
			LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);
			lrg_camera_thirdperson_set_distance_limits (self, g_value_get_float (value), priv->max_distance);
		}
		break;
	case PROP_MAX_DISTANCE:
		{
			LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);
			lrg_camera_thirdperson_set_distance_limits (self, priv->min_distance, g_value_get_float (value));
		}
		break;
	case PROP_PITCH:
		lrg_camera_thirdperson_set_pitch (self, g_value_get_float (value));
		break;
	case PROP_YAW:
		lrg_camera_thirdperson_set_yaw (self, g_value_get_float (value));
		break;
	case PROP_PITCH_MIN:
		{
			LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);
			lrg_camera_thirdperson_set_pitch_limits (self, g_value_get_float (value), priv->pitch_max);
		}
		break;
	case PROP_PITCH_MAX:
		{
			LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);
			lrg_camera_thirdperson_set_pitch_limits (self, priv->pitch_min, g_value_get_float (value));
		}
		break;
	case PROP_SENSITIVITY_X:
		lrg_camera_thirdperson_set_sensitivity_x (self, g_value_get_float (value));
		break;
	case PROP_SENSITIVITY_Y:
		lrg_camera_thirdperson_set_sensitivity_y (self, g_value_get_float (value));
		break;
	case PROP_HEIGHT_OFFSET:
		lrg_camera_thirdperson_set_height_offset (self, g_value_get_float (value));
		break;
	case PROP_SHOULDER_OFFSET:
		lrg_camera_thirdperson_set_shoulder_offset (self, g_value_get_float (value));
		break;
	case PROP_ORBIT_SMOOTHING:
		lrg_camera_thirdperson_set_orbit_smoothing (self, g_value_get_float (value));
		break;
	case PROP_FOLLOW_SMOOTHING:
		lrg_camera_thirdperson_set_follow_smoothing (self, g_value_get_float (value));
		break;
	case PROP_COLLISION_ENABLED:
		lrg_camera_thirdperson_set_collision_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_COLLISION_RADIUS:
		lrg_camera_thirdperson_set_collision_radius (self, g_value_get_float (value));
		break;
	case PROP_COLLISION_LAYERS:
		lrg_camera_thirdperson_set_collision_layers (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/*
 * Class initialization - set up properties and virtual methods.
 */
static void
lrg_camera_thirdperson_class_init (LrgCameraThirdPersonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->dispose = lrg_camera_thirdperson_dispose;
	object_class->get_property = lrg_camera_thirdperson_get_property;
	object_class->set_property = lrg_camera_thirdperson_set_property;

	/* Override begin() to sync our state to parent Camera3D */
	camera_class->begin = lrg_camera_thirdperson_begin;

	/* Distance properties */
	properties[PROP_DISTANCE] =
		g_param_spec_float ("distance",
		                    "Distance",
		                    "Desired orbit distance from target",
		                    0.1f, G_MAXFLOAT,
		                    DEFAULT_DISTANCE,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_ACTUAL_DISTANCE] =
		g_param_spec_float ("actual-distance",
		                    "Actual Distance",
		                    "Actual distance (may be reduced by collision)",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_DISTANCE,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	properties[PROP_MIN_DISTANCE] =
		g_param_spec_float ("min-distance",
		                    "Min Distance",
		                    "Minimum orbit distance",
		                    0.1f, G_MAXFLOAT,
		                    DEFAULT_MIN_DISTANCE,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_MAX_DISTANCE] =
		g_param_spec_float ("max-distance",
		                    "Max Distance",
		                    "Maximum orbit distance",
		                    0.1f, G_MAXFLOAT,
		                    DEFAULT_MAX_DISTANCE,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Angle properties */
	properties[PROP_PITCH] =
		g_param_spec_float ("pitch",
		                    "Pitch",
		                    "Vertical orbit angle in degrees",
		                    -90.0f, 90.0f,
		                    DEFAULT_PITCH,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_YAW] =
		g_param_spec_float ("yaw",
		                    "Yaw",
		                    "Horizontal orbit angle in degrees (0-360)",
		                    0.0f, 360.0f,
		                    DEFAULT_YAW,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_PITCH_MIN] =
		g_param_spec_float ("pitch-min",
		                    "Pitch Min",
		                    "Minimum pitch angle",
		                    -90.0f, 90.0f,
		                    DEFAULT_PITCH_MIN,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_PITCH_MAX] =
		g_param_spec_float ("pitch-max",
		                    "Pitch Max",
		                    "Maximum pitch angle",
		                    -90.0f, 90.0f,
		                    DEFAULT_PITCH_MAX,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Sensitivity properties */
	properties[PROP_SENSITIVITY_X] =
		g_param_spec_float ("sensitivity-x",
		                    "Sensitivity X",
		                    "Horizontal orbit sensitivity",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_SENSITIVITY_X,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_SENSITIVITY_Y] =
		g_param_spec_float ("sensitivity-y",
		                    "Sensitivity Y",
		                    "Vertical orbit sensitivity",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_SENSITIVITY_Y,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Offset properties */
	properties[PROP_HEIGHT_OFFSET] =
		g_param_spec_float ("height-offset",
		                    "Height Offset",
		                    "Height offset above target",
		                    -G_MAXFLOAT, G_MAXFLOAT,
		                    DEFAULT_HEIGHT_OFFSET,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_SHOULDER_OFFSET] =
		g_param_spec_float ("shoulder-offset",
		                    "Shoulder Offset",
		                    "Left/right offset (positive = right)",
		                    -G_MAXFLOAT, G_MAXFLOAT,
		                    DEFAULT_SHOULDER_OFFSET,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Smoothing properties */
	properties[PROP_ORBIT_SMOOTHING] =
		g_param_spec_float ("orbit-smoothing",
		                    "Orbit Smoothing",
		                    "Orbit rotation smoothing speed",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_ORBIT_SMOOTHING,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_FOLLOW_SMOOTHING] =
		g_param_spec_float ("follow-smoothing",
		                    "Follow Smoothing",
		                    "Target follow smoothing speed",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_FOLLOW_SMOOTHING,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Collision properties */
	properties[PROP_COLLISION_ENABLED] =
		g_param_spec_boolean ("collision-enabled",
		                      "Collision Enabled",
		                      "Enable collision avoidance",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_COLLISION_RADIUS] =
		g_param_spec_float ("collision-radius",
		                    "Collision Radius",
		                    "Collision sphere radius",
		                    0.0f, G_MAXFLOAT,
		                    DEFAULT_COLLISION_RADIUS,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_COLLISION_LAYERS] =
		g_param_spec_uint ("collision-layers",
		                   "Collision Layers",
		                   "Collision layer mask",
		                   0, G_MAXUINT32,
		                   DEFAULT_COLLISION_LAYERS,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

/*
 * Instance initialization - set default values.
 */
static void
lrg_camera_thirdperson_init (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Distance */
	priv->distance = DEFAULT_DISTANCE;
	priv->actual_distance = DEFAULT_DISTANCE;
	priv->min_distance = DEFAULT_MIN_DISTANCE;
	priv->max_distance = DEFAULT_MAX_DISTANCE;

	/* Angles */
	priv->target_pitch = DEFAULT_PITCH;
	priv->target_yaw = DEFAULT_YAW;
	priv->current_pitch = DEFAULT_PITCH;
	priv->current_yaw = DEFAULT_YAW;

	/* Pitch limits */
	priv->pitch_min = DEFAULT_PITCH_MIN;
	priv->pitch_max = DEFAULT_PITCH_MAX;

	/* Sensitivity */
	priv->sensitivity_x = DEFAULT_SENSITIVITY_X;
	priv->sensitivity_y = DEFAULT_SENSITIVITY_Y;

	/* Offsets */
	priv->height_offset = DEFAULT_HEIGHT_OFFSET;
	priv->shoulder_offset = DEFAULT_SHOULDER_OFFSET;

	/* Smoothing */
	priv->orbit_smoothing = DEFAULT_ORBIT_SMOOTHING;
	priv->follow_smoothing = DEFAULT_FOLLOW_SMOOTHING;

	/* Target */
	priv->target_x = 0.0f;
	priv->target_y = 0.0f;
	priv->target_z = 0.0f;
	priv->smoothed_target_x = 0.0f;
	priv->smoothed_target_y = 0.0f;
	priv->smoothed_target_z = 0.0f;

	/* Collision - enabled by default as per user request */
	priv->collision_enabled = TRUE;
	priv->collision_radius = DEFAULT_COLLISION_RADIUS;
	priv->collision_layers = DEFAULT_COLLISION_LAYERS;
	priv->collision_callback = NULL;
	priv->collision_user_data = NULL;
	priv->collision_destroy = NULL;

	priv->initialized = FALSE;
}

/*
 * Update camera position based on current orbit state.
 * Calculates position on a sphere around the smoothed target,
 * applies shoulder offset, and handles collision avoidance.
 */
static void
update_camera_position (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv = lrg_camera_thirdperson_get_instance_private (self);
	gfloat pitch_rad;
	gfloat yaw_rad;
	gfloat cos_pitch;
	gfloat sin_pitch;
	gfloat cos_yaw;
	gfloat sin_yaw;
	gfloat offset_x;
	gfloat offset_y;
	gfloat offset_z;
	gfloat focus_x;
	gfloat focus_y;
	gfloat focus_z;
	gfloat cam_x;
	gfloat cam_y;
	gfloat cam_z;
	gfloat right_x;
	gfloat right_z;
	gfloat use_distance;

	/* Convert angles to radians */
	pitch_rad = priv->current_pitch * (gfloat)M_PI / 180.0f;
	yaw_rad = priv->current_yaw * (gfloat)M_PI / 180.0f;

	cos_pitch = cosf (pitch_rad);
	sin_pitch = sinf (pitch_rad);
	cos_yaw = cosf (yaw_rad);
	sin_yaw = sinf (yaw_rad);

	/*
	 * Calculate orbit offset from target.
	 * In spherical coordinates:
	 * - yaw rotates around Y axis (horizontal)
	 * - pitch rotates around horizontal axis (vertical)
	 *
	 * Camera is behind the target, so we use negative Z in local space.
	 */
	offset_x = sin_yaw * cos_pitch * priv->distance;
	offset_y = sin_pitch * priv->distance;
	offset_z = cos_yaw * cos_pitch * priv->distance;

	/* Focus point is the smoothed target with height offset */
	focus_x = priv->smoothed_target_x;
	focus_y = priv->smoothed_target_y + priv->height_offset;
	focus_z = priv->smoothed_target_z;

	/* Initial camera position before shoulder offset */
	cam_x = focus_x + offset_x;
	cam_y = focus_y + offset_y;
	cam_z = focus_z + offset_z;

	/*
	 * Apply shoulder offset.
	 * Right vector is perpendicular to the forward direction (horizontal only).
	 */
	right_x = cos_yaw;
	right_z = -sin_yaw;

	cam_x += right_x * priv->shoulder_offset;
	cam_z += right_z * priv->shoulder_offset;

	/* Start with desired distance */
	use_distance = priv->distance;

	/*
	 * Collision avoidance:
	 * Cast a sphere from focus point to camera position.
	 * If we hit something, pull the camera closer.
	 */
	if (priv->collision_enabled && priv->collision_callback != NULL)
	{
		gfloat hit_distance = 0.0f;
		gboolean hit;

		hit = priv->collision_callback (self,
		                                focus_x, focus_y, focus_z,
		                                cam_x, cam_y, cam_z,
		                                priv->collision_radius,
		                                priv->collision_layers,
		                                &hit_distance,
		                                priv->collision_user_data);

		if (hit && hit_distance < use_distance)
		{
			/* Pull camera closer, but respect minimum distance */
			use_distance = MAX (hit_distance - priv->collision_radius, priv->min_distance);

			/* Recalculate camera position with reduced distance */
			offset_x = sin_yaw * cos_pitch * use_distance;
			offset_y = sin_pitch * use_distance;
			offset_z = cos_yaw * cos_pitch * use_distance;

			cam_x = focus_x + offset_x + right_x * priv->shoulder_offset;
			cam_y = focus_y + offset_y;
			cam_z = focus_z + offset_z + right_z * priv->shoulder_offset;
		}
	}

	priv->actual_distance = use_distance;

	/* Update parent Camera3D position and target */
	lrg_camera3d_set_position_xyz (LRG_CAMERA3D (self), cam_x, cam_y, cam_z);
	lrg_camera3d_set_target_xyz (LRG_CAMERA3D (self), focus_x, focus_y, focus_z);
}

/*
 * Override begin() to sync our state to parent Camera3D before rendering.
 */
static void
lrg_camera_thirdperson_begin (LrgCamera *camera)
{
	LrgCameraThirdPerson *self = LRG_CAMERA_THIRDPERSON (camera);

	/* Update camera position from our orbit state */
	update_camera_position (self);

	/* Chain up to parent's begin() which calls BeginMode3D */
	LRG_CAMERA_CLASS (lrg_camera_thirdperson_parent_class)->begin (camera);
}

/* ==========================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * lrg_camera_thirdperson_new:
 *
 * Create a new third-person camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCameraThirdPerson
 */
LrgCameraThirdPerson *
lrg_camera_thirdperson_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_THIRDPERSON, NULL);
}

/* Distance API */

gfloat
lrg_camera_thirdperson_get_distance (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_DISTANCE);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->distance;
}

void
lrg_camera_thirdperson_set_distance (LrgCameraThirdPerson *self,
                                     gfloat                distance)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (distance > 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Clamp to limits */
	distance = clamp_float (distance, priv->min_distance, priv->max_distance);

	if (priv->distance != distance)
	{
		priv->distance = distance;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISTANCE]);
	}
}

gfloat
lrg_camera_thirdperson_get_actual_distance (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_DISTANCE);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->actual_distance;
}

void
lrg_camera_thirdperson_set_distance_limits (LrgCameraThirdPerson *self,
                                            gfloat                min_distance,
                                            gfloat                max_distance)
{
	LrgCameraThirdPersonPrivate *priv;
	gboolean changed = FALSE;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (min_distance > 0.0f);
	g_return_if_fail (max_distance >= min_distance);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->min_distance != min_distance)
	{
		priv->min_distance = min_distance;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_DISTANCE]);
		changed = TRUE;
	}

	if (priv->max_distance != max_distance)
	{
		priv->max_distance = max_distance;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DISTANCE]);
		changed = TRUE;
	}

	/* Re-clamp distance if limits changed */
	if (changed)
	{
		lrg_camera_thirdperson_set_distance (self, priv->distance);
	}
}

void
lrg_camera_thirdperson_get_distance_limits (LrgCameraThirdPerson *self,
                                            gfloat               *out_min,
                                            gfloat               *out_max)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (out_min != NULL)
		*out_min = priv->min_distance;
	if (out_max != NULL)
		*out_max = priv->max_distance;
}

/* Angle API */

gfloat
lrg_camera_thirdperson_get_pitch (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_PITCH);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->target_pitch;
}

void
lrg_camera_thirdperson_set_pitch (LrgCameraThirdPerson *self,
                                  gfloat                pitch)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Clamp to limits */
	pitch = clamp_float (pitch, priv->pitch_min, priv->pitch_max);

	if (priv->target_pitch != pitch)
	{
		priv->target_pitch = pitch;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
	}
}

gfloat
lrg_camera_thirdperson_get_yaw (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_YAW);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->target_yaw;
}

void
lrg_camera_thirdperson_set_yaw (LrgCameraThirdPerson *self,
                                gfloat                yaw)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Wrap to 0-360 */
	yaw = wrap_angle (yaw);

	if (priv->target_yaw != yaw)
	{
		priv->target_yaw = yaw;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
	}
}

void
lrg_camera_thirdperson_orbit (LrgCameraThirdPerson *self,
                              gfloat                delta_x,
                              gfloat                delta_y)
{
	LrgCameraThirdPersonPrivate *priv;
	gfloat new_yaw;
	gfloat new_pitch;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Apply sensitivity to input */
	new_yaw = priv->target_yaw + delta_x * priv->sensitivity_x;
	new_pitch = priv->target_pitch + delta_y * priv->sensitivity_y;

	/* Wrap yaw */
	new_yaw = wrap_angle (new_yaw);

	/* Clamp pitch */
	new_pitch = clamp_float (new_pitch, priv->pitch_min, priv->pitch_max);

	/* Update target angles */
	if (priv->target_yaw != new_yaw)
	{
		priv->target_yaw = new_yaw;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
	}

	if (priv->target_pitch != new_pitch)
	{
		priv->target_pitch = new_pitch;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
	}
}

/* Pitch limits API */

void
lrg_camera_thirdperson_set_pitch_limits (LrgCameraThirdPerson *self,
                                         gfloat                min_pitch,
                                         gfloat                max_pitch)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (min_pitch >= -90.0f && min_pitch <= 90.0f);
	g_return_if_fail (max_pitch >= -90.0f && max_pitch <= 90.0f);
	g_return_if_fail (max_pitch >= min_pitch);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->pitch_min != min_pitch)
	{
		priv->pitch_min = min_pitch;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH_MIN]);
	}

	if (priv->pitch_max != max_pitch)
	{
		priv->pitch_max = max_pitch;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH_MAX]);
	}

	/* Re-clamp current pitch to new limits */
	lrg_camera_thirdperson_set_pitch (self, priv->target_pitch);
}

void
lrg_camera_thirdperson_get_pitch_limits (LrgCameraThirdPerson *self,
                                         gfloat               *out_min,
                                         gfloat               *out_max)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (out_min != NULL)
		*out_min = priv->pitch_min;
	if (out_max != NULL)
		*out_max = priv->pitch_max;
}

/* Sensitivity API */

gfloat
lrg_camera_thirdperson_get_sensitivity_x (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_SENSITIVITY_X);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->sensitivity_x;
}

void
lrg_camera_thirdperson_set_sensitivity_x (LrgCameraThirdPerson *self,
                                          gfloat                sensitivity)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (sensitivity >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->sensitivity_x != sensitivity)
	{
		priv->sensitivity_x = sensitivity;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SENSITIVITY_X]);
	}
}

gfloat
lrg_camera_thirdperson_get_sensitivity_y (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_SENSITIVITY_Y);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->sensitivity_y;
}

void
lrg_camera_thirdperson_set_sensitivity_y (LrgCameraThirdPerson *self,
                                          gfloat                sensitivity)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (sensitivity >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->sensitivity_y != sensitivity)
	{
		priv->sensitivity_y = sensitivity;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SENSITIVITY_Y]);
	}
}

/* Offset API */

gfloat
lrg_camera_thirdperson_get_height_offset (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_HEIGHT_OFFSET);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->height_offset;
}

void
lrg_camera_thirdperson_set_height_offset (LrgCameraThirdPerson *self,
                                          gfloat                offset)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->height_offset != offset)
	{
		priv->height_offset = offset;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT_OFFSET]);
	}
}

gfloat
lrg_camera_thirdperson_get_shoulder_offset (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_SHOULDER_OFFSET);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->shoulder_offset;
}

void
lrg_camera_thirdperson_set_shoulder_offset (LrgCameraThirdPerson *self,
                                            gfloat                offset)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->shoulder_offset != offset)
	{
		priv->shoulder_offset = offset;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOULDER_OFFSET]);
	}
}

/* Smoothing API */

gfloat
lrg_camera_thirdperson_get_orbit_smoothing (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_ORBIT_SMOOTHING);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->orbit_smoothing;
}

void
lrg_camera_thirdperson_set_orbit_smoothing (LrgCameraThirdPerson *self,
                                            gfloat                speed)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (speed >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->orbit_smoothing != speed)
	{
		priv->orbit_smoothing = speed;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORBIT_SMOOTHING]);
	}
}

gfloat
lrg_camera_thirdperson_get_follow_smoothing (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_FOLLOW_SMOOTHING);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->follow_smoothing;
}

void
lrg_camera_thirdperson_set_follow_smoothing (LrgCameraThirdPerson *self,
                                             gfloat                speed)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (speed >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->follow_smoothing != speed)
	{
		priv->follow_smoothing = speed;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLLOW_SMOOTHING]);
	}
}

/* Target following API */

void
lrg_camera_thirdperson_follow (LrgCameraThirdPerson *self,
                               gfloat                target_x,
                               gfloat                target_y,
                               gfloat                target_z,
                               gfloat                delta_time)
{
	LrgCameraThirdPersonPrivate *priv;
	gfloat lerp_factor;
	gfloat orbit_lerp;
	gfloat yaw_diff;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (delta_time >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Update actual target */
	priv->target_x = target_x;
	priv->target_y = target_y;
	priv->target_z = target_z;

	/* First update: snap to target */
	if (!priv->initialized)
	{
		priv->smoothed_target_x = target_x;
		priv->smoothed_target_y = target_y;
		priv->smoothed_target_z = target_z;
		priv->current_pitch = priv->target_pitch;
		priv->current_yaw = priv->target_yaw;
		priv->initialized = TRUE;
		return;
	}

	/*
	 * Smooth target following using exponential decay.
	 * lerp_factor = 1 - exp(-speed * delta) for frame-rate independence.
	 */
	if (priv->follow_smoothing > 0.0f)
	{
		lerp_factor = 1.0f - expf (-priv->follow_smoothing * delta_time);

		priv->smoothed_target_x += (target_x - priv->smoothed_target_x) * lerp_factor;
		priv->smoothed_target_y += (target_y - priv->smoothed_target_y) * lerp_factor;
		priv->smoothed_target_z += (target_z - priv->smoothed_target_z) * lerp_factor;
	}
	else
	{
		/* No smoothing - instant follow */
		priv->smoothed_target_x = target_x;
		priv->smoothed_target_y = target_y;
		priv->smoothed_target_z = target_z;
	}

	/*
	 * Smooth orbit angles.
	 * Yaw requires special handling for wrap-around (e.g., 359 -> 1).
	 */
	if (priv->orbit_smoothing > 0.0f)
	{
		orbit_lerp = 1.0f - expf (-priv->orbit_smoothing * delta_time);

		/* Pitch: simple lerp */
		priv->current_pitch += (priv->target_pitch - priv->current_pitch) * orbit_lerp;

		/* Yaw: handle wrap-around */
		yaw_diff = angular_distance (priv->current_yaw, priv->target_yaw);
		priv->current_yaw += yaw_diff * orbit_lerp;
		priv->current_yaw = wrap_angle (priv->current_yaw);
	}
	else
	{
		/* No smoothing - instant orbit */
		priv->current_pitch = priv->target_pitch;
		priv->current_yaw = priv->target_yaw;
	}
}

void
lrg_camera_thirdperson_snap_to_target (LrgCameraThirdPerson *self,
                                       gfloat                target_x,
                                       gfloat                target_y,
                                       gfloat                target_z)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Snap everything instantly */
	priv->target_x = target_x;
	priv->target_y = target_y;
	priv->target_z = target_z;
	priv->smoothed_target_x = target_x;
	priv->smoothed_target_y = target_y;
	priv->smoothed_target_z = target_z;
	priv->current_pitch = priv->target_pitch;
	priv->current_yaw = priv->target_yaw;
	priv->initialized = TRUE;

	/* Immediately update camera position */
	update_camera_position (self);
}

/* Collision avoidance API */

gboolean
lrg_camera_thirdperson_get_collision_enabled (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), TRUE);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->collision_enabled;
}

void
lrg_camera_thirdperson_set_collision_enabled (LrgCameraThirdPerson *self,
                                              gboolean              enabled)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->collision_enabled != enabled)
	{
		priv->collision_enabled = enabled;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_ENABLED]);
	}
}

gfloat
lrg_camera_thirdperson_get_collision_radius (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_COLLISION_RADIUS);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->collision_radius;
}

void
lrg_camera_thirdperson_set_collision_radius (LrgCameraThirdPerson *self,
                                             gfloat                radius)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));
	g_return_if_fail (radius >= 0.0f);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->collision_radius != radius)
	{
		priv->collision_radius = radius;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_RADIUS]);
	}
}

guint32
lrg_camera_thirdperson_get_collision_layers (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), DEFAULT_COLLISION_LAYERS);

	priv = lrg_camera_thirdperson_get_instance_private (self);
	return priv->collision_layers;
}

void
lrg_camera_thirdperson_set_collision_layers (LrgCameraThirdPerson *self,
                                             guint32               layers)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	if (priv->collision_layers != layers)
	{
		priv->collision_layers = layers;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLISION_LAYERS]);
	}
}

void
lrg_camera_thirdperson_set_collision_callback (LrgCameraThirdPerson        *self,
                                               LrgCameraCollisionCallback   callback,
                                               gpointer                     user_data,
                                               GDestroyNotify               destroy)
{
	LrgCameraThirdPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_THIRDPERSON (self));

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/* Clean up old callback */
	if (priv->collision_destroy && priv->collision_user_data)
	{
		priv->collision_destroy (priv->collision_user_data);
	}

	/* Set new callback */
	priv->collision_callback = callback;
	priv->collision_user_data = user_data;
	priv->collision_destroy = destroy;
}

/* Direction vectors API */

GrlVector3 *
lrg_camera_thirdperson_get_forward (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;
	gfloat yaw_rad;
	gfloat dir_x;
	gfloat dir_z;
	gfloat length;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), NULL);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/*
	 * Forward direction is from camera toward target.
	 * We use the current yaw to get horizontal forward (Y=0).
	 * Since camera is behind target, forward is in the direction of yaw.
	 */
	yaw_rad = priv->current_yaw * (gfloat)M_PI / 180.0f;

	/* Forward direction (toward target) */
	dir_x = -sinf (yaw_rad);
	dir_z = -cosf (yaw_rad);

	/* Normalize (should already be unit length, but be safe) */
	length = sqrtf (dir_x * dir_x + dir_z * dir_z);
	if (length > 0.0001f)
	{
		dir_x /= length;
		dir_z /= length;
	}

	return grl_vector3_new (dir_x, 0.0f, dir_z);
}

GrlVector3 *
lrg_camera_thirdperson_get_right (LrgCameraThirdPerson *self)
{
	LrgCameraThirdPersonPrivate *priv;
	gfloat yaw_rad;
	gfloat dir_x;
	gfloat dir_z;

	g_return_val_if_fail (LRG_IS_CAMERA_THIRDPERSON (self), NULL);

	priv = lrg_camera_thirdperson_get_instance_private (self);

	/*
	 * Right direction is perpendicular to forward.
	 * Cross product of forward with up (0,1,0) gives right.
	 */
	yaw_rad = priv->current_yaw * (gfloat)M_PI / 180.0f;

	/* Right vector (perpendicular to forward) */
	dir_x = cosf (yaw_rad);
	dir_z = -sinf (yaw_rad);

	return grl_vector3_new (dir_x, 0.0f, dir_z);
}
