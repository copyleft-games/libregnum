/* lrg-camera-topdown.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-down camera implementation for 2D games.
 */

#include "config.h"
#include "lrg-camera-topdown.h"
#include <math.h>

/**
 * SECTION:lrg-camera-topdown
 * @title: LrgCameraTopDown
 * @short_description: Top-down camera for 2D games
 *
 * #LrgCameraTopDown is a specialized 2D camera for top-down games.
 * It inherits from #LrgCamera2D and provides:
 *
 * - Smooth target following with exponential decay (frame-rate independent)
 * - Circular deadzone to prevent jitter when target is near center
 * - World bounds clamping to keep camera within level
 * - Screen shake effects for impact feedback
 *
 * ## Properties
 *
 * - **follow-speed**: How quickly the camera follows the target (default: 5.0)
 * - **deadzone-radius**: Circular deadzone radius (default: 20.0)
 * - **bounds-enabled**: Whether to clamp to world bounds (default: FALSE)
 * - **bounds-min-x/y**: Minimum world bounds (default: 0.0)
 * - **bounds-max-x/y**: Maximum world bounds (default: 1000.0)
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCameraTopDown) camera = lrg_camera_topdown_new ();
 *
 * // Configure following
 * lrg_camera_topdown_set_follow_speed (camera, 8.0f);
 * lrg_camera_topdown_set_deadzone_radius (camera, 30.0f);
 *
 * // Set world bounds
 * lrg_camera_topdown_set_bounds (camera, 0, 0, 3200, 2400);
 * lrg_camera_topdown_set_bounds_enabled (camera, TRUE);
 *
 * // Set screen offset (center of screen)
 * lrg_camera2d_set_offset_xy (LRG_CAMERA2D (camera), 400.0f, 300.0f);
 *
 * // In game loop
 * lrg_camera_topdown_follow (camera, player_x, player_y, delta_time);
 *
 * // Trigger shake on damage
 * if (player_took_damage)
 *     lrg_camera_topdown_shake (camera, 10.0f, 0.3f);
 * ]|
 */

typedef struct
{
	/* Following behavior */
	gfloat follow_speed;
	gfloat deadzone_radius;

	/* Target tracking */
	gfloat actual_target_x;
	gfloat actual_target_y;
	gfloat smoothed_target_x;
	gfloat smoothed_target_y;

	/* World bounds */
	gboolean bounds_enabled;
	gfloat bounds_min_x;
	gfloat bounds_min_y;
	gfloat bounds_max_x;
	gfloat bounds_max_y;

	/* Screen shake */
	gboolean shake_active;
	gfloat shake_intensity;
	gfloat shake_duration;
	gfloat shake_timer;
	gfloat shake_offset_x;
	gfloat shake_offset_y;
} LrgCameraTopDownPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCameraTopDown, lrg_camera_topdown, LRG_TYPE_CAMERA2D)

enum
{
	PROP_0,
	PROP_FOLLOW_SPEED,
	PROP_DEADZONE_RADIUS,
	PROP_BOUNDS_ENABLED,
	PROP_BOUNDS_MIN_X,
	PROP_BOUNDS_MIN_Y,
	PROP_BOUNDS_MAX_X,
	PROP_BOUNDS_MAX_Y,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * update_smooth_follow:
 * @self: an #LrgCameraTopDown
 * @delta_time: frame delta time
 *
 * Update the smoothed target position based on the actual target,
 * applying deadzone and exponential smoothing.
 */
static void
update_smooth_follow (LrgCameraTopDown *self,
                      gfloat            delta_time)
{
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);
	gfloat dx, dy, distance;
	gfloat lerp_factor;
	gfloat move_distance;
	gfloat norm_dx, norm_dy;

	/* Calculate distance to target */
	dx = priv->actual_target_x - priv->smoothed_target_x;
	dy = priv->actual_target_y - priv->smoothed_target_y;
	distance = sqrtf (dx * dx + dy * dy);

	/* Skip if within deadzone */
	if (distance <= priv->deadzone_radius)
		return;

	/* Exponential smoothing for frame-rate independence */
	lerp_factor = 1.0f - expf (-priv->follow_speed * delta_time);

	/* Move only the distance beyond the deadzone */
	move_distance = (distance - priv->deadzone_radius) * lerp_factor;

	/* Normalize direction */
	norm_dx = dx / distance;
	norm_dy = dy / distance;

	/* Update smoothed position */
	priv->smoothed_target_x += norm_dx * move_distance;
	priv->smoothed_target_y += norm_dy * move_distance;

	/* Apply world bounds clamping */
	if (priv->bounds_enabled)
	{
		priv->smoothed_target_x = CLAMP (priv->smoothed_target_x,
		                                 priv->bounds_min_x,
		                                 priv->bounds_max_x);
		priv->smoothed_target_y = CLAMP (priv->smoothed_target_y,
		                                 priv->bounds_min_y,
		                                 priv->bounds_max_y);
	}
}

/*
 * update_shake:
 * @self: an #LrgCameraTopDown
 * @delta_time: frame delta time
 *
 * Update the screen shake effect, calculating new random offsets
 * with decay over time.
 */
static void
update_shake (LrgCameraTopDown *self,
              gfloat            delta_time)
{
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);
	gfloat decay;

	if (!priv->shake_active)
		return;

	priv->shake_timer -= delta_time;

	if (priv->shake_timer <= 0.0f)
	{
		/* Shake finished */
		priv->shake_active = FALSE;
		priv->shake_offset_x = 0.0f;
		priv->shake_offset_y = 0.0f;
		priv->shake_timer = 0.0f;
		return;
	}

	/* Calculate decay based on remaining time */
	decay = priv->shake_timer / priv->shake_duration;

	/* Random offset within intensity (scaled by decay) */
	priv->shake_offset_x = (g_random_double () * 2.0 - 1.0)
	                       * priv->shake_intensity * decay;
	priv->shake_offset_y = (g_random_double () * 2.0 - 1.0)
	                       * priv->shake_intensity * decay;
}

/*
 * sync_to_parent:
 * @self: an #LrgCameraTopDown
 *
 * Sync the smoothed target position (with shake offset) to the parent
 * LrgCamera2D target property.
 */
static void
sync_to_parent (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);
	gfloat final_x, final_y;

	/* Apply shake offset to smoothed target */
	final_x = priv->smoothed_target_x + priv->shake_offset_x;
	final_y = priv->smoothed_target_y + priv->shake_offset_y;

	/* Update parent target */
	lrg_camera2d_set_target_xy (LRG_CAMERA2D (self), final_x, final_y);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_camera_topdown_begin (LrgCamera *camera)
{
	LrgCameraTopDown *self = LRG_CAMERA_TOPDOWN (camera);

	/* Sync our smoothed position to parent before rendering */
	sync_to_parent (self);

	/* Call parent begin to activate the camera */
	LRG_CAMERA_CLASS (lrg_camera_topdown_parent_class)->begin (camera);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera_topdown_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgCameraTopDown        *self = LRG_CAMERA_TOPDOWN (object);
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_FOLLOW_SPEED:
		g_value_set_float (value, priv->follow_speed);
		break;
	case PROP_DEADZONE_RADIUS:
		g_value_set_float (value, priv->deadzone_radius);
		break;
	case PROP_BOUNDS_ENABLED:
		g_value_set_boolean (value, priv->bounds_enabled);
		break;
	case PROP_BOUNDS_MIN_X:
		g_value_set_float (value, priv->bounds_min_x);
		break;
	case PROP_BOUNDS_MIN_Y:
		g_value_set_float (value, priv->bounds_min_y);
		break;
	case PROP_BOUNDS_MAX_X:
		g_value_set_float (value, priv->bounds_max_x);
		break;
	case PROP_BOUNDS_MAX_Y:
		g_value_set_float (value, priv->bounds_max_y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_topdown_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgCameraTopDown        *self = LRG_CAMERA_TOPDOWN (object);
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_FOLLOW_SPEED:
		priv->follow_speed = g_value_get_float (value);
		if (priv->follow_speed <= 0.0f)
			priv->follow_speed = 5.0f;
		break;
	case PROP_DEADZONE_RADIUS:
		priv->deadzone_radius = g_value_get_float (value);
		if (priv->deadzone_radius < 0.0f)
			priv->deadzone_radius = 0.0f;
		break;
	case PROP_BOUNDS_ENABLED:
		priv->bounds_enabled = g_value_get_boolean (value);
		break;
	case PROP_BOUNDS_MIN_X:
		priv->bounds_min_x = g_value_get_float (value);
		break;
	case PROP_BOUNDS_MIN_Y:
		priv->bounds_min_y = g_value_get_float (value);
		break;
	case PROP_BOUNDS_MAX_X:
		priv->bounds_max_x = g_value_get_float (value);
		break;
	case PROP_BOUNDS_MAX_Y:
		priv->bounds_max_y = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_topdown_class_init (LrgCameraTopDownClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->get_property = lrg_camera_topdown_get_property;
	object_class->set_property = lrg_camera_topdown_set_property;

	/* Override begin to sync our smoothed target */
	camera_class->begin = lrg_camera_topdown_begin;

	/* Properties */
	properties[PROP_FOLLOW_SPEED] =
		g_param_spec_float ("follow-speed",
		                    "Follow Speed",
		                    "Camera follow speed (higher = faster)",
		                    0.1f, 100.0f, 5.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_DEADZONE_RADIUS] =
		g_param_spec_float ("deadzone-radius",
		                    "Deadzone Radius",
		                    "Circular deadzone radius in world units",
		                    0.0f, 500.0f, 20.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_BOUNDS_ENABLED] =
		g_param_spec_boolean ("bounds-enabled",
		                      "Bounds Enabled",
		                      "Whether to clamp camera to world bounds",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_BOUNDS_MIN_X] =
		g_param_spec_float ("bounds-min-x",
		                    "Bounds Min X",
		                    "Minimum X world bound",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_BOUNDS_MIN_Y] =
		g_param_spec_float ("bounds-min-y",
		                    "Bounds Min Y",
		                    "Minimum Y world bound",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_BOUNDS_MAX_X] =
		g_param_spec_float ("bounds-max-x",
		                    "Bounds Max X",
		                    "Maximum X world bound",
		                    -G_MAXFLOAT, G_MAXFLOAT, 1000.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_BOUNDS_MAX_Y] =
		g_param_spec_float ("bounds-max-y",
		                    "Bounds Max Y",
		                    "Maximum Y world bound",
		                    -G_MAXFLOAT, G_MAXFLOAT, 1000.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera_topdown_init (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv = lrg_camera_topdown_get_instance_private (self);

	/* Following defaults */
	priv->follow_speed = 5.0f;
	priv->deadzone_radius = 20.0f;

	/* Target tracking (start at origin) */
	priv->actual_target_x = 0.0f;
	priv->actual_target_y = 0.0f;
	priv->smoothed_target_x = 0.0f;
	priv->smoothed_target_y = 0.0f;

	/* Bounds defaults (disabled) */
	priv->bounds_enabled = FALSE;
	priv->bounds_min_x = 0.0f;
	priv->bounds_min_y = 0.0f;
	priv->bounds_max_x = 1000.0f;
	priv->bounds_max_y = 1000.0f;

	/* Shake defaults (inactive) */
	priv->shake_active = FALSE;
	priv->shake_intensity = 0.0f;
	priv->shake_duration = 0.0f;
	priv->shake_timer = 0.0f;
	priv->shake_offset_x = 0.0f;
	priv->shake_offset_y = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCameraTopDown *
lrg_camera_topdown_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_TOPDOWN, NULL);
}

gfloat
lrg_camera_topdown_get_follow_speed (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_TOPDOWN (self), 5.0f);

	priv = lrg_camera_topdown_get_instance_private (self);
	return priv->follow_speed;
}

void
lrg_camera_topdown_set_follow_speed (LrgCameraTopDown *self,
                                     gfloat            speed)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));
	g_return_if_fail (speed > 0.0f);

	priv = lrg_camera_topdown_get_instance_private (self);
	priv->follow_speed = speed;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLLOW_SPEED]);
}

gfloat
lrg_camera_topdown_get_deadzone_radius (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_TOPDOWN (self), 20.0f);

	priv = lrg_camera_topdown_get_instance_private (self);
	return priv->deadzone_radius;
}

void
lrg_camera_topdown_set_deadzone_radius (LrgCameraTopDown *self,
                                        gfloat            radius)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));

	priv = lrg_camera_topdown_get_instance_private (self);
	priv->deadzone_radius = (radius >= 0.0f) ? radius : 0.0f;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEADZONE_RADIUS]);
}

void
lrg_camera_topdown_follow (LrgCameraTopDown *self,
                           gfloat            target_x,
                           gfloat            target_y,
                           gfloat            delta_time)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));
	g_return_if_fail (delta_time >= 0.0f);

	priv = lrg_camera_topdown_get_instance_private (self);

	/* Update actual target position */
	priv->actual_target_x = target_x;
	priv->actual_target_y = target_y;

	/* Update smooth following */
	update_smooth_follow (self, delta_time);

	/* Update shake effect */
	update_shake (self, delta_time);

	/* Sync to parent so target is immediately available */
	sync_to_parent (self);
}

gboolean
lrg_camera_topdown_get_bounds_enabled (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_TOPDOWN (self), FALSE);

	priv = lrg_camera_topdown_get_instance_private (self);
	return priv->bounds_enabled;
}

void
lrg_camera_topdown_set_bounds_enabled (LrgCameraTopDown *self,
                                       gboolean          enabled)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));

	priv = lrg_camera_topdown_get_instance_private (self);
	priv->bounds_enabled = enabled;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_ENABLED]);
}

void
lrg_camera_topdown_set_bounds (LrgCameraTopDown *self,
                               gfloat            min_x,
                               gfloat            min_y,
                               gfloat            max_x,
                               gfloat            max_y)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));

	priv = lrg_camera_topdown_get_instance_private (self);

	priv->bounds_min_x = min_x;
	priv->bounds_min_y = min_y;
	priv->bounds_max_x = max_x;
	priv->bounds_max_y = max_y;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_MIN_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_MIN_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_MAX_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_MAX_Y]);
}

void
lrg_camera_topdown_get_bounds (LrgCameraTopDown *self,
                               gfloat           *out_min_x,
                               gfloat           *out_min_y,
                               gfloat           *out_max_x,
                               gfloat           *out_max_y)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));

	priv = lrg_camera_topdown_get_instance_private (self);

	if (out_min_x != NULL)
		*out_min_x = priv->bounds_min_x;
	if (out_min_y != NULL)
		*out_min_y = priv->bounds_min_y;
	if (out_max_x != NULL)
		*out_max_x = priv->bounds_max_x;
	if (out_max_y != NULL)
		*out_max_y = priv->bounds_max_y;
}

void
lrg_camera_topdown_shake (LrgCameraTopDown *self,
                          gfloat            intensity,
                          gfloat            duration)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));
	g_return_if_fail (intensity >= 0.0f);
	g_return_if_fail (duration > 0.0f);

	priv = lrg_camera_topdown_get_instance_private (self);

	priv->shake_active = TRUE;
	priv->shake_intensity = intensity;
	priv->shake_duration = duration;
	priv->shake_timer = duration;
}

void
lrg_camera_topdown_stop_shake (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));

	priv = lrg_camera_topdown_get_instance_private (self);

	priv->shake_active = FALSE;
	priv->shake_offset_x = 0.0f;
	priv->shake_offset_y = 0.0f;
	priv->shake_timer = 0.0f;
}

gboolean
lrg_camera_topdown_is_shaking (LrgCameraTopDown *self)
{
	LrgCameraTopDownPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_TOPDOWN (self), FALSE);

	priv = lrg_camera_topdown_get_instance_private (self);
	return priv->shake_active;
}

void
lrg_camera_topdown_update_shake (LrgCameraTopDown *self,
                                 gfloat            delta_time)
{
	g_return_if_fail (LRG_IS_CAMERA_TOPDOWN (self));
	g_return_if_fail (delta_time >= 0.0f);

	update_shake (self, delta_time);
}
