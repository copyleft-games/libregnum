/* lrg-camera-sideon.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Side-on (platformer) camera implementation for 2D games.
 */

#include "config.h"
#include "lrg-camera-sideon.h"
#include <math.h>

/**
 * SECTION:lrg-camera-sideon
 * @title: LrgCameraSideOn
 * @short_description: Side-on camera for platformer games
 *
 * #LrgCameraSideOn is a specialized 2D camera for platformer games.
 * It inherits from #LrgCamera2D and provides:
 *
 * - Separate X/Y axis following with different speeds
 * - Horizontal lookahead based on movement direction
 * - Rectangular deadzone (larger vertically for jump jitter reduction)
 * - Vertical bias to show more ground than sky
 * - World bounds clamping
 * - Screen shake effects
 *
 * ## Properties
 *
 * - **follow-speed-x**: Horizontal follow speed (default: 8.0)
 * - **follow-speed-y**: Vertical follow speed (default: 4.0, slower to reduce jitter)
 * - **deadzone-width**: Horizontal deadzone width (default: 100.0)
 * - **deadzone-height**: Vertical deadzone height (default: 150.0, larger for jumps)
 * - **lookahead-distance**: How far ahead to look (default: 100.0)
 * - **lookahead-speed**: Lookahead transition speed (default: 3.0)
 * - **vertical-bias**: Vertical offset bias (default: 0.3, shows more ground)
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCameraSideOn) camera = lrg_camera_sideon_new ();
 *
 * // Configure for platformer
 * lrg_camera_sideon_set_follow_speed_x (camera, 10.0f);
 * lrg_camera_sideon_set_follow_speed_y (camera, 5.0f);
 * lrg_camera_sideon_set_deadzone (camera, 80.0f, 120.0f);
 * lrg_camera_sideon_set_lookahead_distance (camera, 150.0f);
 * lrg_camera_sideon_set_vertical_bias (camera, 0.2f);
 *
 * // Set screen offset (center of screen)
 * lrg_camera2d_set_offset_xy (LRG_CAMERA2D (camera), 400.0f, 300.0f);
 *
 * // In game loop
 * lrg_camera_sideon_follow (camera, player_x, player_y, delta_time);
 * ]|
 */

/* Velocity threshold for direction detection */
#define VELOCITY_THRESHOLD (0.5f)

typedef struct
{
	/* Following behavior */
	gfloat follow_speed_x;
	gfloat follow_speed_y;

	/* Deadzone (rectangular) */
	gfloat deadzone_width;
	gfloat deadzone_height;

	/* Lookahead */
	gfloat lookahead_distance;
	gfloat lookahead_speed;
	gfloat current_lookahead;
	gint   last_direction;  /* -1 = left, 0 = none, 1 = right */

	/* Vertical bias */
	gfloat vertical_bias;

	/* Target tracking */
	gfloat actual_target_x;
	gfloat actual_target_y;
	gfloat last_target_x;
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
} LrgCameraSideOnPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCameraSideOn, lrg_camera_sideon, LRG_TYPE_CAMERA2D)

enum
{
	PROP_0,
	PROP_FOLLOW_SPEED_X,
	PROP_FOLLOW_SPEED_Y,
	PROP_DEADZONE_WIDTH,
	PROP_DEADZONE_HEIGHT,
	PROP_LOOKAHEAD_DISTANCE,
	PROP_LOOKAHEAD_SPEED,
	PROP_VERTICAL_BIAS,
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
 * @self: an #LrgCameraSideOn
 * @delta_time: frame delta time
 *
 * Update the smoothed target position with separate axis handling
 * and rectangular deadzone.
 */
static void
update_smooth_follow (LrgCameraSideOn *self,
                      gfloat           delta_time)
{
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);
	gfloat dx, dy;
	gfloat half_dz_w, half_dz_h;
	gfloat lerp_x, lerp_y;
	gfloat excess, move;

	dx = priv->actual_target_x - priv->smoothed_target_x;
	dy = priv->actual_target_y - priv->smoothed_target_y;

	half_dz_w = priv->deadzone_width / 2.0f;
	half_dz_h = priv->deadzone_height / 2.0f;

	/* Horizontal movement (only if outside deadzone) */
	if (fabsf (dx) > half_dz_w)
	{
		excess = fabsf (dx) - half_dz_w;
		lerp_x = 1.0f - expf (-priv->follow_speed_x * delta_time);
		move = excess * lerp_x;
		priv->smoothed_target_x += (dx > 0.0f) ? move : -move;
	}

	/* Vertical movement (only if outside deadzone) */
	if (fabsf (dy) > half_dz_h)
	{
		excess = fabsf (dy) - half_dz_h;
		lerp_y = 1.0f - expf (-priv->follow_speed_y * delta_time);
		move = excess * lerp_y;
		priv->smoothed_target_y += (dy > 0.0f) ? move : -move;
	}

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
 * update_lookahead:
 * @self: an #LrgCameraSideOn
 * @delta_time: frame delta time
 *
 * Update the horizontal lookahead based on movement direction.
 */
static void
update_lookahead (LrgCameraSideOn *self,
                  gfloat           delta_time)
{
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);
	gfloat velocity_x;
	gint new_direction;
	gfloat target_lookahead;
	gfloat lerp_factor;

	/* Detect horizontal velocity */
	velocity_x = priv->actual_target_x - priv->last_target_x;
	priv->last_target_x = priv->actual_target_x;

	/* Determine direction (-1, 0, 1) */
	if (velocity_x > VELOCITY_THRESHOLD)
		new_direction = 1;
	else if (velocity_x < -VELOCITY_THRESHOLD)
		new_direction = -1;
	else
		new_direction = priv->last_direction;  /* Keep last direction when stopped */

	priv->last_direction = new_direction;

	/* Calculate target lookahead */
	target_lookahead = (gfloat)new_direction * priv->lookahead_distance;

	/* Smooth transition to target lookahead */
	lerp_factor = 1.0f - expf (-priv->lookahead_speed * delta_time);
	priv->current_lookahead += (target_lookahead - priv->current_lookahead) * lerp_factor;
}

/*
 * update_shake:
 * @self: an #LrgCameraSideOn
 * @delta_time: frame delta time
 *
 * Update the screen shake effect.
 */
static void
update_shake (LrgCameraSideOn *self,
              gfloat           delta_time)
{
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);
	gfloat decay;

	if (!priv->shake_active)
		return;

	priv->shake_timer -= delta_time;

	if (priv->shake_timer <= 0.0f)
	{
		priv->shake_active = FALSE;
		priv->shake_offset_x = 0.0f;
		priv->shake_offset_y = 0.0f;
		priv->shake_timer = 0.0f;
		return;
	}

	/* Calculate decay */
	decay = priv->shake_timer / priv->shake_duration;

	/* Random offset */
	priv->shake_offset_x = (g_random_double () * 2.0 - 1.0)
	                       * priv->shake_intensity * decay;
	priv->shake_offset_y = (g_random_double () * 2.0 - 1.0)
	                       * priv->shake_intensity * decay;
}

/*
 * sync_to_parent:
 * @self: an #LrgCameraSideOn
 *
 * Sync the calculated target position to the parent LrgCamera2D.
 */
static void
sync_to_parent (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);
	gfloat final_x, final_y;
	gfloat bias_offset;

	/* Apply lookahead to X */
	final_x = priv->smoothed_target_x + priv->current_lookahead;

	/* Apply vertical bias (offset in pixels) */
	/* Positive bias = camera looks more at ground (target appears higher on screen) */
	bias_offset = priv->vertical_bias * 200.0f;
	final_y = priv->smoothed_target_y + bias_offset;

	/* Apply shake offset */
	final_x += priv->shake_offset_x;
	final_y += priv->shake_offset_y;

	/* Update parent target */
	lrg_camera2d_set_target_xy (LRG_CAMERA2D (self), final_x, final_y);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_camera_sideon_begin (LrgCamera *camera)
{
	LrgCameraSideOn *self = LRG_CAMERA_SIDEON (camera);

	/* Sync our calculated position to parent before rendering */
	sync_to_parent (self);

	/* Call parent begin */
	LRG_CAMERA_CLASS (lrg_camera_sideon_parent_class)->begin (camera);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera_sideon_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgCameraSideOn        *self = LRG_CAMERA_SIDEON (object);
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_FOLLOW_SPEED_X:
		g_value_set_float (value, priv->follow_speed_x);
		break;
	case PROP_FOLLOW_SPEED_Y:
		g_value_set_float (value, priv->follow_speed_y);
		break;
	case PROP_DEADZONE_WIDTH:
		g_value_set_float (value, priv->deadzone_width);
		break;
	case PROP_DEADZONE_HEIGHT:
		g_value_set_float (value, priv->deadzone_height);
		break;
	case PROP_LOOKAHEAD_DISTANCE:
		g_value_set_float (value, priv->lookahead_distance);
		break;
	case PROP_LOOKAHEAD_SPEED:
		g_value_set_float (value, priv->lookahead_speed);
		break;
	case PROP_VERTICAL_BIAS:
		g_value_set_float (value, priv->vertical_bias);
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
lrg_camera_sideon_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgCameraSideOn        *self = LRG_CAMERA_SIDEON (object);
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_FOLLOW_SPEED_X:
		priv->follow_speed_x = g_value_get_float (value);
		if (priv->follow_speed_x <= 0.0f)
			priv->follow_speed_x = 8.0f;
		break;
	case PROP_FOLLOW_SPEED_Y:
		priv->follow_speed_y = g_value_get_float (value);
		if (priv->follow_speed_y <= 0.0f)
			priv->follow_speed_y = 4.0f;
		break;
	case PROP_DEADZONE_WIDTH:
		priv->deadzone_width = g_value_get_float (value);
		if (priv->deadzone_width < 0.0f)
			priv->deadzone_width = 0.0f;
		break;
	case PROP_DEADZONE_HEIGHT:
		priv->deadzone_height = g_value_get_float (value);
		if (priv->deadzone_height < 0.0f)
			priv->deadzone_height = 0.0f;
		break;
	case PROP_LOOKAHEAD_DISTANCE:
		priv->lookahead_distance = g_value_get_float (value);
		if (priv->lookahead_distance < 0.0f)
			priv->lookahead_distance = 0.0f;
		break;
	case PROP_LOOKAHEAD_SPEED:
		priv->lookahead_speed = g_value_get_float (value);
		if (priv->lookahead_speed <= 0.0f)
			priv->lookahead_speed = 3.0f;
		break;
	case PROP_VERTICAL_BIAS:
		priv->vertical_bias = CLAMP (g_value_get_float (value), -1.0f, 1.0f);
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
lrg_camera_sideon_class_init (LrgCameraSideOnClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->get_property = lrg_camera_sideon_get_property;
	object_class->set_property = lrg_camera_sideon_set_property;

	camera_class->begin = lrg_camera_sideon_begin;

	/* Properties */
	properties[PROP_FOLLOW_SPEED_X] =
		g_param_spec_float ("follow-speed-x",
		                    "Follow Speed X",
		                    "Horizontal camera follow speed",
		                    0.1f, 100.0f, 8.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_FOLLOW_SPEED_Y] =
		g_param_spec_float ("follow-speed-y",
		                    "Follow Speed Y",
		                    "Vertical camera follow speed",
		                    0.1f, 100.0f, 4.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_DEADZONE_WIDTH] =
		g_param_spec_float ("deadzone-width",
		                    "Deadzone Width",
		                    "Horizontal deadzone width in world units",
		                    0.0f, 500.0f, 100.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_DEADZONE_HEIGHT] =
		g_param_spec_float ("deadzone-height",
		                    "Deadzone Height",
		                    "Vertical deadzone height in world units",
		                    0.0f, 500.0f, 150.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_LOOKAHEAD_DISTANCE] =
		g_param_spec_float ("lookahead-distance",
		                    "Lookahead Distance",
		                    "Horizontal lookahead distance",
		                    0.0f, 500.0f, 100.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_LOOKAHEAD_SPEED] =
		g_param_spec_float ("lookahead-speed",
		                    "Lookahead Speed",
		                    "Lookahead transition speed",
		                    0.1f, 20.0f, 3.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_VERTICAL_BIAS] =
		g_param_spec_float ("vertical-bias",
		                    "Vertical Bias",
		                    "Vertical offset bias (-1 = show ceiling, +1 = show ground)",
		                    -1.0f, 1.0f, 0.3f,
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
lrg_camera_sideon_init (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv = lrg_camera_sideon_get_instance_private (self);

	/* Following defaults */
	priv->follow_speed_x = 8.0f;
	priv->follow_speed_y = 4.0f;

	/* Deadzone defaults */
	priv->deadzone_width = 100.0f;
	priv->deadzone_height = 150.0f;

	/* Lookahead defaults */
	priv->lookahead_distance = 100.0f;
	priv->lookahead_speed = 3.0f;
	priv->current_lookahead = 0.0f;
	priv->last_direction = 0;

	/* Vertical bias default */
	priv->vertical_bias = 0.3f;

	/* Target tracking */
	priv->actual_target_x = 0.0f;
	priv->actual_target_y = 0.0f;
	priv->last_target_x = 0.0f;
	priv->smoothed_target_x = 0.0f;
	priv->smoothed_target_y = 0.0f;

	/* Bounds defaults */
	priv->bounds_enabled = FALSE;
	priv->bounds_min_x = 0.0f;
	priv->bounds_min_y = 0.0f;
	priv->bounds_max_x = 1000.0f;
	priv->bounds_max_y = 1000.0f;

	/* Shake defaults */
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

LrgCameraSideOn *
lrg_camera_sideon_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_SIDEON, NULL);
}

gfloat
lrg_camera_sideon_get_follow_speed_x (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 8.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->follow_speed_x;
}

void
lrg_camera_sideon_set_follow_speed_x (LrgCameraSideOn *self,
                                      gfloat           speed)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));
	g_return_if_fail (speed > 0.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->follow_speed_x = speed;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLLOW_SPEED_X]);
}

gfloat
lrg_camera_sideon_get_follow_speed_y (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 4.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->follow_speed_y;
}

void
lrg_camera_sideon_set_follow_speed_y (LrgCameraSideOn *self,
                                      gfloat           speed)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));
	g_return_if_fail (speed > 0.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->follow_speed_y = speed;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOLLOW_SPEED_Y]);
}

gfloat
lrg_camera_sideon_get_deadzone_width (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 100.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->deadzone_width;
}

gfloat
lrg_camera_sideon_get_deadzone_height (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 150.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->deadzone_height;
}

void
lrg_camera_sideon_set_deadzone (LrgCameraSideOn *self,
                                gfloat           width,
                                gfloat           height)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->deadzone_width = (width >= 0.0f) ? width : 0.0f;
	priv->deadzone_height = (height >= 0.0f) ? height : 0.0f;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEADZONE_WIDTH]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEADZONE_HEIGHT]);
}

gfloat
lrg_camera_sideon_get_lookahead_distance (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 100.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->lookahead_distance;
}

void
lrg_camera_sideon_set_lookahead_distance (LrgCameraSideOn *self,
                                          gfloat           distance)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->lookahead_distance = (distance >= 0.0f) ? distance : 0.0f;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOKAHEAD_DISTANCE]);
}

gfloat
lrg_camera_sideon_get_lookahead_speed (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 3.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->lookahead_speed;
}

void
lrg_camera_sideon_set_lookahead_speed (LrgCameraSideOn *self,
                                       gfloat           speed)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));
	g_return_if_fail (speed > 0.0f);

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->lookahead_speed = speed;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOOKAHEAD_SPEED]);
}

gfloat
lrg_camera_sideon_get_vertical_bias (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), 0.3f);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->vertical_bias;
}

void
lrg_camera_sideon_set_vertical_bias (LrgCameraSideOn *self,
                                     gfloat           bias)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->vertical_bias = CLAMP (bias, -1.0f, 1.0f);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VERTICAL_BIAS]);
}

void
lrg_camera_sideon_follow (LrgCameraSideOn *self,
                          gfloat           target_x,
                          gfloat           target_y,
                          gfloat           delta_time)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));
	g_return_if_fail (delta_time >= 0.0f);

	priv = lrg_camera_sideon_get_instance_private (self);

	/* Update actual target position */
	priv->actual_target_x = target_x;
	priv->actual_target_y = target_y;

	/* Update lookahead */
	update_lookahead (self, delta_time);

	/* Update smooth following */
	update_smooth_follow (self, delta_time);

	/* Update shake effect */
	update_shake (self, delta_time);

	/* Sync to parent so target is immediately available */
	sync_to_parent (self);
}

gboolean
lrg_camera_sideon_get_bounds_enabled (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), FALSE);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->bounds_enabled;
}

void
lrg_camera_sideon_set_bounds_enabled (LrgCameraSideOn *self,
                                      gboolean         enabled)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);
	priv->bounds_enabled = enabled;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNDS_ENABLED]);
}

void
lrg_camera_sideon_set_bounds (LrgCameraSideOn *self,
                              gfloat           min_x,
                              gfloat           min_y,
                              gfloat           max_x,
                              gfloat           max_y)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);

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
lrg_camera_sideon_get_bounds (LrgCameraSideOn *self,
                              gfloat          *out_min_x,
                              gfloat          *out_min_y,
                              gfloat          *out_max_x,
                              gfloat          *out_max_y)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);

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
lrg_camera_sideon_shake (LrgCameraSideOn *self,
                         gfloat           intensity,
                         gfloat           duration)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));
	g_return_if_fail (intensity >= 0.0f);
	g_return_if_fail (duration > 0.0f);

	priv = lrg_camera_sideon_get_instance_private (self);

	priv->shake_active = TRUE;
	priv->shake_intensity = intensity;
	priv->shake_duration = duration;
	priv->shake_timer = duration;
}

void
lrg_camera_sideon_stop_shake (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_SIDEON (self));

	priv = lrg_camera_sideon_get_instance_private (self);

	priv->shake_active = FALSE;
	priv->shake_offset_x = 0.0f;
	priv->shake_offset_y = 0.0f;
	priv->shake_timer = 0.0f;
}

gboolean
lrg_camera_sideon_is_shaking (LrgCameraSideOn *self)
{
	LrgCameraSideOnPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_SIDEON (self), FALSE);

	priv = lrg_camera_sideon_get_instance_private (self);
	return priv->shake_active;
}
