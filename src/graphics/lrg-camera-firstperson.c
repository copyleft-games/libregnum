/* lrg-camera-firstperson.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * First-person camera implementation for 3D games.
 */

#include "config.h"
#include "lrg-camera-firstperson.h"
#include <math.h>

/**
 * SECTION:lrg-camera-firstperson
 * @title: LrgCameraFirstPerson
 * @short_description: First-person camera for 3D games
 *
 * #LrgCameraFirstPerson is a specialized 3D camera for first-person games.
 * It inherits from #LrgCamera3D and provides:
 *
 * - Pitch/yaw rotation from mouse delta input
 * - Pitch clamping to prevent gimbal lock (-89 to +89 degrees)
 * - Configurable mouse sensitivity
 * - Optional head bob with horizontal sway during movement
 * - Eye height above body position
 * - Direction vectors for movement calculations
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCameraFirstPerson) camera = lrg_camera_firstperson_new ();
 *
 * // Configure sensitivity
 * lrg_camera_firstperson_set_sensitivity_x (camera, 0.15f);
 * lrg_camera_firstperson_set_sensitivity_y (camera, 0.12f);
 *
 * // Configure head bob
 * lrg_camera_firstperson_set_head_bob (camera, 12.0f, 0.04f, 0.02f);
 * lrg_camera_firstperson_set_head_bob_enabled (camera, TRUE);
 *
 * // In game loop
 * lrg_camera_firstperson_rotate (camera, mouse_dx, mouse_dy);
 * lrg_camera_firstperson_set_body_position (camera, player_x, player_y, player_z);
 * lrg_camera_firstperson_update_head_bob (camera, is_walking, delta_time);
 *
 * // Get movement direction
 * g_autoptr(GrlVector3) forward = lrg_camera_firstperson_get_forward (camera);
 * ]|
 */

#define DEG_TO_RAD (G_PI / 180.0f)
#define RAD_TO_DEG (180.0f / G_PI)

typedef struct
{
	/* Look angles (degrees) */
	gfloat pitch;
	gfloat yaw;

	/* Sensitivity */
	gfloat sensitivity_x;
	gfloat sensitivity_y;

	/* Pitch limits */
	gfloat pitch_min;
	gfloat pitch_max;

	/* Body position (feet) */
	gfloat body_x;
	gfloat body_y;
	gfloat body_z;
	gfloat eye_height;

	/* Head bob */
	gboolean head_bob_enabled;
	gfloat head_bob_speed;
	gfloat head_bob_amount;
	gfloat head_sway_amount;
	gfloat head_bob_timer;
	gboolean is_moving;
} LrgCameraFirstPersonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCameraFirstPerson, lrg_camera_firstperson, LRG_TYPE_CAMERA3D)

enum
{
	PROP_0,
	PROP_PITCH,
	PROP_YAW,
	PROP_SENSITIVITY_X,
	PROP_SENSITIVITY_Y,
	PROP_PITCH_MIN,
	PROP_PITCH_MAX,
	PROP_EYE_HEIGHT,
	PROP_HEAD_BOB_ENABLED,
	PROP_HEAD_BOB_SPEED,
	PROP_HEAD_BOB_AMOUNT,
	PROP_HEAD_SWAY_AMOUNT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * wrap_angle:
 * @angle: angle in degrees
 *
 * Wrap angle to 0-360 range.
 */
static gfloat
wrap_angle (gfloat angle)
{
	while (angle >= 360.0f)
		angle -= 360.0f;
	while (angle < 0.0f)
		angle += 360.0f;
	return angle;
}

/*
 * sync_camera_orientation:
 * @self: an #LrgCameraFirstPerson
 *
 * Update the parent camera's position and target based on
 * current pitch, yaw, body position, eye height, and head bob.
 */
static void
sync_camera_orientation (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv = lrg_camera_firstperson_get_instance_private (self);
	gfloat pitch_rad, yaw_rad;
	gfloat cos_pitch, sin_pitch;
	gfloat cos_yaw, sin_yaw;
	gfloat dir_x, dir_y, dir_z;
	gfloat eye_x, eye_y, eye_z;
	gfloat bob_offset, sway_offset;
	gfloat right_x, right_z;

	pitch_rad = priv->pitch * DEG_TO_RAD;
	yaw_rad = priv->yaw * DEG_TO_RAD;

	cos_pitch = cosf (pitch_rad);
	sin_pitch = sinf (pitch_rad);
	cos_yaw = cosf (yaw_rad);
	sin_yaw = sinf (yaw_rad);

	/* Calculate look direction from spherical coordinates */
	dir_x = cos_pitch * sin_yaw;
	dir_y = sin_pitch;
	dir_z = cos_pitch * cos_yaw;

	/* Calculate head bob offset */
	bob_offset = 0.0f;
	sway_offset = 0.0f;
	if (priv->head_bob_enabled && priv->is_moving)
	{
		bob_offset = sinf (priv->head_bob_timer * priv->head_bob_speed)
		             * priv->head_bob_amount;
		sway_offset = cosf (priv->head_bob_timer * priv->head_bob_speed * 0.5f)
		              * priv->head_sway_amount;
	}

	/* Calculate eye position (body position + eye height + bob) */
	/* Sway is applied along the right vector */
	right_x = cos_yaw;
	right_z = -sin_yaw;

	eye_x = priv->body_x + right_x * sway_offset;
	eye_y = priv->body_y + priv->eye_height + bob_offset;
	eye_z = priv->body_z + right_z * sway_offset;

	/* Set camera position (eye position) */
	lrg_camera3d_set_position_xyz (LRG_CAMERA3D (self), eye_x, eye_y, eye_z);

	/* Set target (position + look direction) */
	lrg_camera3d_set_target_xyz (LRG_CAMERA3D (self),
	                             eye_x + dir_x,
	                             eye_y + dir_y,
	                             eye_z + dir_z);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_camera_firstperson_begin (LrgCamera *camera)
{
	LrgCameraFirstPerson *self = LRG_CAMERA_FIRSTPERSON (camera);

	/* Sync orientation before rendering */
	sync_camera_orientation (self);

	/* Call parent begin */
	LRG_CAMERA_CLASS (lrg_camera_firstperson_parent_class)->begin (camera);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera_firstperson_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgCameraFirstPerson        *self = LRG_CAMERA_FIRSTPERSON (object);
	LrgCameraFirstPersonPrivate *priv = lrg_camera_firstperson_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_PITCH:
		g_value_set_float (value, priv->pitch);
		break;
	case PROP_YAW:
		g_value_set_float (value, priv->yaw);
		break;
	case PROP_SENSITIVITY_X:
		g_value_set_float (value, priv->sensitivity_x);
		break;
	case PROP_SENSITIVITY_Y:
		g_value_set_float (value, priv->sensitivity_y);
		break;
	case PROP_PITCH_MIN:
		g_value_set_float (value, priv->pitch_min);
		break;
	case PROP_PITCH_MAX:
		g_value_set_float (value, priv->pitch_max);
		break;
	case PROP_EYE_HEIGHT:
		g_value_set_float (value, priv->eye_height);
		break;
	case PROP_HEAD_BOB_ENABLED:
		g_value_set_boolean (value, priv->head_bob_enabled);
		break;
	case PROP_HEAD_BOB_SPEED:
		g_value_set_float (value, priv->head_bob_speed);
		break;
	case PROP_HEAD_BOB_AMOUNT:
		g_value_set_float (value, priv->head_bob_amount);
		break;
	case PROP_HEAD_SWAY_AMOUNT:
		g_value_set_float (value, priv->head_sway_amount);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_firstperson_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgCameraFirstPerson        *self = LRG_CAMERA_FIRSTPERSON (object);
	LrgCameraFirstPersonPrivate *priv = lrg_camera_firstperson_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_PITCH:
		priv->pitch = CLAMP (g_value_get_float (value), priv->pitch_min, priv->pitch_max);
		break;
	case PROP_YAW:
		priv->yaw = wrap_angle (g_value_get_float (value));
		break;
	case PROP_SENSITIVITY_X:
		priv->sensitivity_x = g_value_get_float (value);
		if (priv->sensitivity_x <= 0.0f)
			priv->sensitivity_x = 0.1f;
		break;
	case PROP_SENSITIVITY_Y:
		priv->sensitivity_y = g_value_get_float (value);
		if (priv->sensitivity_y <= 0.0f)
			priv->sensitivity_y = 0.1f;
		break;
	case PROP_PITCH_MIN:
		priv->pitch_min = g_value_get_float (value);
		break;
	case PROP_PITCH_MAX:
		priv->pitch_max = g_value_get_float (value);
		break;
	case PROP_EYE_HEIGHT:
		priv->eye_height = g_value_get_float (value);
		if (priv->eye_height <= 0.0f)
			priv->eye_height = 1.7f;
		break;
	case PROP_HEAD_BOB_ENABLED:
		priv->head_bob_enabled = g_value_get_boolean (value);
		break;
	case PROP_HEAD_BOB_SPEED:
		priv->head_bob_speed = g_value_get_float (value);
		if (priv->head_bob_speed <= 0.0f)
			priv->head_bob_speed = 10.0f;
		break;
	case PROP_HEAD_BOB_AMOUNT:
		priv->head_bob_amount = g_value_get_float (value);
		if (priv->head_bob_amount < 0.0f)
			priv->head_bob_amount = 0.0f;
		break;
	case PROP_HEAD_SWAY_AMOUNT:
		priv->head_sway_amount = g_value_get_float (value);
		if (priv->head_sway_amount < 0.0f)
			priv->head_sway_amount = 0.0f;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_firstperson_class_init (LrgCameraFirstPersonClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->get_property = lrg_camera_firstperson_get_property;
	object_class->set_property = lrg_camera_firstperson_set_property;

	camera_class->begin = lrg_camera_firstperson_begin;

	/* Properties */
	properties[PROP_PITCH] =
		g_param_spec_float ("pitch",
		                    "Pitch",
		                    "Vertical look angle in degrees",
		                    -90.0f, 90.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_YAW] =
		g_param_spec_float ("yaw",
		                    "Yaw",
		                    "Horizontal look angle in degrees",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_SENSITIVITY_X] =
		g_param_spec_float ("sensitivity-x",
		                    "Sensitivity X",
		                    "Horizontal mouse sensitivity",
		                    0.01f, 1.0f, 0.1f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_SENSITIVITY_Y] =
		g_param_spec_float ("sensitivity-y",
		                    "Sensitivity Y",
		                    "Vertical mouse sensitivity",
		                    0.01f, 1.0f, 0.1f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_PITCH_MIN] =
		g_param_spec_float ("pitch-min",
		                    "Pitch Min",
		                    "Minimum pitch angle (looking up)",
		                    -90.0f, 0.0f, -89.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_PITCH_MAX] =
		g_param_spec_float ("pitch-max",
		                    "Pitch Max",
		                    "Maximum pitch angle (looking down)",
		                    0.0f, 90.0f, 89.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_EYE_HEIGHT] =
		g_param_spec_float ("eye-height",
		                    "Eye Height",
		                    "Eye height above body position",
		                    0.1f, 5.0f, 1.7f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEAD_BOB_ENABLED] =
		g_param_spec_boolean ("head-bob-enabled",
		                      "Head Bob Enabled",
		                      "Whether head bob effect is enabled",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEAD_BOB_SPEED] =
		g_param_spec_float ("head-bob-speed",
		                    "Head Bob Speed",
		                    "Head bob oscillation speed",
		                    1.0f, 30.0f, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEAD_BOB_AMOUNT] =
		g_param_spec_float ("head-bob-amount",
		                    "Head Bob Amount",
		                    "Vertical bob displacement",
		                    0.0f, 0.5f, 0.05f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEAD_SWAY_AMOUNT] =
		g_param_spec_float ("head-sway-amount",
		                    "Head Sway Amount",
		                    "Horizontal sway displacement",
		                    0.0f, 0.5f, 0.02f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera_firstperson_init (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv = lrg_camera_firstperson_get_instance_private (self);

	/* Look angle defaults */
	priv->pitch = 0.0f;
	priv->yaw = 0.0f;

	/* Sensitivity defaults */
	priv->sensitivity_x = 0.1f;
	priv->sensitivity_y = 0.1f;

	/* Pitch limits */
	priv->pitch_min = -89.0f;
	priv->pitch_max = 89.0f;

	/* Body position defaults */
	priv->body_x = 0.0f;
	priv->body_y = 0.0f;
	priv->body_z = 0.0f;
	priv->eye_height = 1.7f;

	/* Head bob defaults */
	priv->head_bob_enabled = FALSE;
	priv->head_bob_speed = 10.0f;
	priv->head_bob_amount = 0.05f;
	priv->head_sway_amount = 0.02f;
	priv->head_bob_timer = 0.0f;
	priv->is_moving = FALSE;

	/* Set up vector (always up for FPS) */
	lrg_camera3d_set_up_xyz (LRG_CAMERA3D (self), 0.0f, 1.0f, 0.0f);

	/* Set perspective projection */
	lrg_camera3d_set_projection (LRG_CAMERA3D (self), LRG_PROJECTION_PERSPECTIVE);
	lrg_camera3d_set_fovy (LRG_CAMERA3D (self), 60.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCameraFirstPerson *
lrg_camera_firstperson_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_FIRSTPERSON, NULL);
}

gfloat
lrg_camera_firstperson_get_pitch (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->pitch;
}

void
lrg_camera_firstperson_set_pitch (LrgCameraFirstPerson *self,
                                  gfloat                pitch)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->pitch = CLAMP (pitch, priv->pitch_min, priv->pitch_max);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

gfloat
lrg_camera_firstperson_get_yaw (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->yaw;
}

void
lrg_camera_firstperson_set_yaw (LrgCameraFirstPerson *self,
                                gfloat                yaw)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->yaw = wrap_angle (yaw);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
}

void
lrg_camera_firstperson_rotate (LrgCameraFirstPerson *self,
                               gfloat                delta_x,
                               gfloat                delta_y)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);

	/* Update yaw (horizontal) */
	priv->yaw += delta_x * priv->sensitivity_x;
	priv->yaw = wrap_angle (priv->yaw);

	/* Update pitch (vertical) - invert Y for natural feel */
	priv->pitch -= delta_y * priv->sensitivity_y;
	priv->pitch = CLAMP (priv->pitch, priv->pitch_min, priv->pitch_max);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
}

gfloat
lrg_camera_firstperson_get_sensitivity_x (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), 0.1f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->sensitivity_x;
}

void
lrg_camera_firstperson_set_sensitivity_x (LrgCameraFirstPerson *self,
                                          gfloat                sensitivity)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));
	g_return_if_fail (sensitivity > 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->sensitivity_x = sensitivity;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SENSITIVITY_X]);
}

gfloat
lrg_camera_firstperson_get_sensitivity_y (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), 0.1f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->sensitivity_y;
}

void
lrg_camera_firstperson_set_sensitivity_y (LrgCameraFirstPerson *self,
                                          gfloat                sensitivity)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));
	g_return_if_fail (sensitivity > 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->sensitivity_y = sensitivity;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SENSITIVITY_Y]);
}

void
lrg_camera_firstperson_set_pitch_limits (LrgCameraFirstPerson *self,
                                         gfloat                min_pitch,
                                         gfloat                max_pitch)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));
	g_return_if_fail (min_pitch < max_pitch);

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->pitch_min = CLAMP (min_pitch, -90.0f, 0.0f);
	priv->pitch_max = CLAMP (max_pitch, 0.0f, 90.0f);

	/* Re-clamp current pitch */
	priv->pitch = CLAMP (priv->pitch, priv->pitch_min, priv->pitch_max);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH_MIN]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH_MAX]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

void
lrg_camera_firstperson_get_pitch_limits (LrgCameraFirstPerson *self,
                                         gfloat               *out_min,
                                         gfloat               *out_max)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);

	if (out_min != NULL)
		*out_min = priv->pitch_min;
	if (out_max != NULL)
		*out_max = priv->pitch_max;
}

void
lrg_camera_firstperson_set_body_position (LrgCameraFirstPerson *self,
                                          gfloat                x,
                                          gfloat                y,
                                          gfloat                z)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->body_x = x;
	priv->body_y = y;
	priv->body_z = z;
}

gfloat
lrg_camera_firstperson_get_eye_height (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), 1.7f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->eye_height;
}

void
lrg_camera_firstperson_set_eye_height (LrgCameraFirstPerson *self,
                                       gfloat                height)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));
	g_return_if_fail (height > 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->eye_height = height;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EYE_HEIGHT]);
}

gboolean
lrg_camera_firstperson_get_head_bob_enabled (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), FALSE);

	priv = lrg_camera_firstperson_get_instance_private (self);
	return priv->head_bob_enabled;
}

void
lrg_camera_firstperson_set_head_bob_enabled (LrgCameraFirstPerson *self,
                                             gboolean              enabled)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->head_bob_enabled = enabled;

	if (!enabled)
		priv->head_bob_timer = 0.0f;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEAD_BOB_ENABLED]);
}

void
lrg_camera_firstperson_set_head_bob (LrgCameraFirstPerson *self,
                                     gfloat                speed,
                                     gfloat                bob_amount,
                                     gfloat                sway_amount)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));
	g_return_if_fail (speed > 0.0f);

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->head_bob_speed = speed;
	priv->head_bob_amount = (bob_amount >= 0.0f) ? bob_amount : 0.0f;
	priv->head_sway_amount = (sway_amount >= 0.0f) ? sway_amount : 0.0f;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEAD_BOB_SPEED]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEAD_BOB_AMOUNT]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEAD_SWAY_AMOUNT]);
}

void
lrg_camera_firstperson_update_head_bob (LrgCameraFirstPerson *self,
                                        gboolean              is_moving,
                                        gfloat                delta_time)
{
	LrgCameraFirstPersonPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self));

	priv = lrg_camera_firstperson_get_instance_private (self);
	priv->is_moving = is_moving;

	if (is_moving && priv->head_bob_enabled)
	{
		priv->head_bob_timer += delta_time;
	}
	else
	{
		/* Decay timer when not moving */
		priv->head_bob_timer *= 0.9f;
		if (priv->head_bob_timer < 0.01f)
			priv->head_bob_timer = 0.0f;
	}
}

GrlVector3 *
lrg_camera_firstperson_get_forward (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;
	gfloat yaw_rad;
	gfloat dir_x, dir_z;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), NULL);

	priv = lrg_camera_firstperson_get_instance_private (self);

	/* Horizontal forward direction (Y = 0) */
	yaw_rad = priv->yaw * DEG_TO_RAD;
	dir_x = sinf (yaw_rad);
	dir_z = cosf (yaw_rad);

	return grl_vector3_new (dir_x, 0.0f, dir_z);
}

GrlVector3 *
lrg_camera_firstperson_get_right (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;
	gfloat yaw_rad;
	gfloat dir_x, dir_z;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), NULL);

	priv = lrg_camera_firstperson_get_instance_private (self);

	/* Right vector is perpendicular to forward */
	yaw_rad = priv->yaw * DEG_TO_RAD;
	dir_x = cosf (yaw_rad);
	dir_z = -sinf (yaw_rad);

	return grl_vector3_new (dir_x, 0.0f, dir_z);
}

GrlVector3 *
lrg_camera_firstperson_get_look_direction (LrgCameraFirstPerson *self)
{
	LrgCameraFirstPersonPrivate *priv;
	gfloat pitch_rad, yaw_rad;
	gfloat cos_pitch, sin_pitch;
	gfloat cos_yaw, sin_yaw;
	gfloat dir_x, dir_y, dir_z;

	g_return_val_if_fail (LRG_IS_CAMERA_FIRSTPERSON (self), NULL);

	priv = lrg_camera_firstperson_get_instance_private (self);

	pitch_rad = priv->pitch * DEG_TO_RAD;
	yaw_rad = priv->yaw * DEG_TO_RAD;

	cos_pitch = cosf (pitch_rad);
	sin_pitch = sinf (pitch_rad);
	cos_yaw = cosf (yaw_rad);
	sin_yaw = sinf (yaw_rad);

	/* Full look direction including pitch */
	dir_x = cos_pitch * sin_yaw;
	dir_y = sin_pitch;
	dir_z = cos_pitch * cos_yaw;

	return grl_vector3_new (dir_x, dir_y, dir_z);
}
