/* lrg-camera2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D camera implementation.
 */

#include "config.h"
#include "lrg-camera2d.h"

/**
 * SECTION:lrg-camera2d
 * @title: LrgCamera2D
 * @short_description: 2D camera implementation
 *
 * #LrgCamera2D is a 2D camera that wraps #GrlCamera2D.
 * It provides offset, target, rotation, and zoom controls
 * for 2D games.
 *
 * ## Properties
 *
 * - **offset-x**, **offset-y**: Camera offset from target
 * - **target-x**, **target-y**: Point the camera follows
 * - **rotation**: Camera rotation in degrees
 * - **zoom**: Zoom level (1.0 = normal)
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCamera2D) camera = lrg_camera2d_new ();
 *
 * // Center camera on player
 * lrg_camera2d_set_target_xy (camera, player_x, player_y);
 * lrg_camera2d_set_zoom (camera, 2.0f);  // Zoom in
 *
 * lrg_camera_begin (LRG_CAMERA (camera));
 * // Draw game world
 * lrg_camera_end (LRG_CAMERA (camera));
 * ]|
 */

typedef struct
{
	GrlCamera2D *grl_camera;
	gfloat       offset_x;
	gfloat       offset_y;
	gfloat       target_x;
	gfloat       target_y;
	gfloat       rotation;
	gfloat       zoom;

	/* Render interpolation: the view at the start of the latest fixed
	 * step it changed in (see grl-interpolation.h) */
	gfloat                prev_offset_x;
	gfloat                prev_offset_y;
	gfloat                prev_target_x;
	gfloat                prev_target_y;
	gfloat                prev_rotation;
	gfloat                prev_zoom;
	GrlInterpolationTrack interp;
} LrgCamera2DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCamera2D, lrg_camera2d, LRG_TYPE_CAMERA)

enum
{
	PROP_0,
	PROP_OFFSET_X,
	PROP_OFFSET_Y,
	PROP_TARGET_X,
	PROP_TARGET_Y,
	PROP_ROTATION,
	PROP_ZOOM,
	PROP_INTERPOLATE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/* The first write in a fixed step keeps the view the step began with */
static void
camera2d_capture_previous (LrgCamera2DPrivate *priv)
{
	priv->prev_offset_x = priv->offset_x;
	priv->prev_offset_y = priv->offset_y;
	priv->prev_target_x = priv->target_x;
	priv->prev_target_y = priv->target_y;
	priv->prev_rotation = priv->rotation;
	priv->prev_zoom = priv->zoom;
}

/* Stores one interpolated view field */
static void
camera2d_write (LrgCamera2DPrivate *priv,
                gfloat             *current,
                gfloat             *previous,
                gfloat              value)
{
	if (grl_interpolation_track_begin_write (&priv->interp))
		camera2d_capture_previous (priv);

	grl_interpolation_track_write_float (&priv->interp, current, previous, value);
}

/*
 * lrg_camera2d_sync_to_grl:
 *
 * Pushes the view to the graylib camera: the render (interpolated) view
 * when the camera moved in the latest fixed step, else the true one. Used
 * for drawing and for the world/screen conversions, so picking matches
 * what is on screen.
 */
static void
lrg_camera2d_sync_to_grl (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);
	gfloat              alpha;

	if (!grl_interpolation_track_get_blend (&priv->interp, &alpha))
	{
		grl_camera2d_set_offset_xy (priv->grl_camera, priv->offset_x, priv->offset_y);
		grl_camera2d_set_target_xy (priv->grl_camera, priv->target_x, priv->target_y);
		grl_camera2d_set_rotation (priv->grl_camera, priv->rotation);
		grl_camera2d_set_zoom (priv->grl_camera, priv->zoom);
		return;
	}

	grl_camera2d_set_offset_xy (priv->grl_camera,
	                            grl_interpolation_lerp (priv->prev_offset_x, priv->offset_x, alpha),
	                            grl_interpolation_lerp (priv->prev_offset_y, priv->offset_y, alpha));
	grl_camera2d_set_target_xy (priv->grl_camera,
	                            grl_interpolation_lerp (priv->prev_target_x, priv->target_x, alpha),
	                            grl_interpolation_lerp (priv->prev_target_y, priv->target_y, alpha));
	grl_camera2d_set_rotation (priv->grl_camera,
	                           grl_interpolation_lerp_angle (priv->prev_rotation, priv->rotation,
	                                                         alpha, 360.0f));
	grl_camera2d_set_zoom (priv->grl_camera,
	                       grl_interpolation_lerp (priv->prev_zoom, priv->zoom, alpha));
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_camera2d_begin (LrgCamera *camera)
{
	LrgCamera2D        *self = LRG_CAMERA2D (camera);
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	lrg_camera2d_sync_to_grl (self);
	grl_camera2d_begin (priv->grl_camera);
}

static void
lrg_camera2d_end (LrgCamera *camera)
{
	LrgCamera2D        *self = LRG_CAMERA2D (camera);
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	grl_camera2d_end (priv->grl_camera);
}

static void
lrg_camera2d_world_to_screen (LrgCamera        *camera,
                              const GrlVector3 *world,
                              GrlVector2       *out_screen)
{
	LrgCamera2D         *self = LRG_CAMERA2D (camera);
	LrgCamera2DPrivate  *priv = lrg_camera2d_get_instance_private (self);
	g_autoptr(GrlVector2) world2d = NULL;
	g_autoptr(GrlVector2) result = NULL;

	lrg_camera2d_sync_to_grl (self);

	world2d = grl_vector2_new (world->x, world->y);
	result = grl_camera2d_get_world_to_screen (priv->grl_camera, world2d);

	out_screen->x = result->x;
	out_screen->y = result->y;
}

static void
lrg_camera2d_screen_to_world (LrgCamera        *camera,
                              const GrlVector2 *screen,
                              GrlVector3       *out_world)
{
	LrgCamera2D         *self = LRG_CAMERA2D (camera);
	LrgCamera2DPrivate  *priv = lrg_camera2d_get_instance_private (self);
	g_autoptr(GrlVector2) screen_copy = NULL;
	g_autoptr(GrlVector2) result = NULL;

	lrg_camera2d_sync_to_grl (self);

	screen_copy = grl_vector2_new (screen->x, screen->y);
	result = grl_camera2d_get_screen_to_world (priv->grl_camera, screen_copy);

	out_world->x = result->x;
	out_world->y = result->y;
	out_world->z = 0.0f;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera2d_finalize (GObject *object)
{
	LrgCamera2D        *self = LRG_CAMERA2D (object);
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	g_clear_object (&priv->grl_camera);

	G_OBJECT_CLASS (lrg_camera2d_parent_class)->finalize (object);
}

static void
lrg_camera2d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgCamera2D        *self = LRG_CAMERA2D (object);
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_OFFSET_X:
		g_value_set_float (value, priv->offset_x);
		break;
	case PROP_OFFSET_Y:
		g_value_set_float (value, priv->offset_y);
		break;
	case PROP_TARGET_X:
		g_value_set_float (value, priv->target_x);
		break;
	case PROP_TARGET_Y:
		g_value_set_float (value, priv->target_y);
		break;
	case PROP_ROTATION:
		g_value_set_float (value, priv->rotation);
		break;
	case PROP_ZOOM:
		g_value_set_float (value, priv->zoom);
		break;
	case PROP_INTERPOLATE:
		g_value_set_boolean (value, priv->interp.enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera2d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgCamera2D        *self = LRG_CAMERA2D (object);
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_OFFSET_X:
		camera2d_write (priv, &priv->offset_x, &priv->prev_offset_x, g_value_get_float (value));
		break;
	case PROP_OFFSET_Y:
		camera2d_write (priv, &priv->offset_y, &priv->prev_offset_y, g_value_get_float (value));
		break;
	case PROP_TARGET_X:
		camera2d_write (priv, &priv->target_x, &priv->prev_target_x, g_value_get_float (value));
		break;
	case PROP_TARGET_Y:
		camera2d_write (priv, &priv->target_y, &priv->prev_target_y, g_value_get_float (value));
		break;
	case PROP_ROTATION:
		camera2d_write (priv, &priv->rotation, &priv->prev_rotation, g_value_get_float (value));
		break;
	case PROP_ZOOM:
		{
			gfloat zoom = g_value_get_float (value);

			camera2d_write (priv, &priv->zoom, &priv->prev_zoom,
			                zoom <= 0.0f ? 0.01f : zoom);
		}
		break;
	case PROP_INTERPOLATE:
		lrg_camera2d_set_interpolate (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera2d_class_init (LrgCamera2DClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->finalize = lrg_camera2d_finalize;
	object_class->get_property = lrg_camera2d_get_property;
	object_class->set_property = lrg_camera2d_set_property;

	camera_class->begin = lrg_camera2d_begin;
	camera_class->end = lrg_camera2d_end;
	camera_class->world_to_screen = lrg_camera2d_world_to_screen;
	camera_class->screen_to_world = lrg_camera2d_screen_to_world;

	properties[PROP_OFFSET_X] =
		g_param_spec_float ("offset-x",
		                    "Offset X",
		                    "Camera offset X",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_OFFSET_Y] =
		g_param_spec_float ("offset-y",
		                    "Offset Y",
		                    "Camera offset Y",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_TARGET_X] =
		g_param_spec_float ("target-x",
		                    "Target X",
		                    "Camera target X",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_TARGET_Y] =
		g_param_spec_float ("target-y",
		                    "Target Y",
		                    "Camera target Y",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_ROTATION] =
		g_param_spec_float ("rotation",
		                    "Rotation",
		                    "Camera rotation in degrees",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_ZOOM] =
		g_param_spec_float ("zoom",
		                    "Zoom",
		                    "Camera zoom level",
		                    0.01f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCamera2D:interpolate:
	 *
	 * Whether the view is interpolated for drawing when it is moved in
	 * fixed steps (a camera following a target from fixed_update), so it
	 * moves smoothly with the interpolated objects on a display faster
	 * than the step rate.
	 */
	properties[PROP_INTERPOLATE] =
		g_param_spec_boolean ("interpolate",
		                      "Interpolate",
		                      "Whether the view is interpolated for drawing",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera2d_init (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	priv->grl_camera = grl_camera2d_new ();
	priv->offset_x = 0.0f;
	priv->offset_y = 0.0f;
	priv->target_x = 0.0f;
	priv->target_y = 0.0f;
	priv->rotation = 0.0f;
	priv->zoom = 1.0f;

	/* New cameras start with previous == current */
	camera2d_capture_previous (priv);
	grl_interpolation_track_init (&priv->interp);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCamera2D *
lrg_camera2d_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA2D, NULL);
}

GrlVector2 *
lrg_camera2d_get_offset (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA2D (self), NULL);

	priv = lrg_camera2d_get_instance_private (self);
	return grl_vector2_new (priv->offset_x, priv->offset_y);
}

void
lrg_camera2d_set_offset (LrgCamera2D *self,
                         GrlVector2  *offset)
{
	g_return_if_fail (LRG_IS_CAMERA2D (self));
	g_return_if_fail (offset != NULL);

	lrg_camera2d_set_offset_xy (self, offset->x, offset->y);
}

void
lrg_camera2d_set_offset_xy (LrgCamera2D *self,
                            gfloat       x,
                            gfloat       y)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));

	priv = lrg_camera2d_get_instance_private (self);
	camera2d_write (priv, &priv->offset_x, &priv->prev_offset_x, x);
	camera2d_write (priv, &priv->offset_y, &priv->prev_offset_y, y);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET_Y]);
}

GrlVector2 *
lrg_camera2d_get_target (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA2D (self), NULL);

	priv = lrg_camera2d_get_instance_private (self);
	return grl_vector2_new (priv->target_x, priv->target_y);
}

void
lrg_camera2d_set_target (LrgCamera2D *self,
                         GrlVector2  *target)
{
	g_return_if_fail (LRG_IS_CAMERA2D (self));
	g_return_if_fail (target != NULL);

	lrg_camera2d_set_target_xy (self, target->x, target->y);
}

void
lrg_camera2d_set_target_xy (LrgCamera2D *self,
                            gfloat       x,
                            gfloat       y)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));

	priv = lrg_camera2d_get_instance_private (self);
	camera2d_write (priv, &priv->target_x, &priv->prev_target_x, x);
	camera2d_write (priv, &priv->target_y, &priv->prev_target_y, y);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_Y]);
}

gfloat
lrg_camera2d_get_rotation (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA2D (self), 0.0f);

	priv = lrg_camera2d_get_instance_private (self);
	return priv->rotation;
}

void
lrg_camera2d_set_rotation (LrgCamera2D *self,
                           gfloat       rotation)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));

	priv = lrg_camera2d_get_instance_private (self);
	camera2d_write (priv, &priv->rotation, &priv->prev_rotation, rotation);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROTATION]);
}

gfloat
lrg_camera2d_get_zoom (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA2D (self), 1.0f);

	priv = lrg_camera2d_get_instance_private (self);
	return priv->zoom;
}

void
lrg_camera2d_set_zoom (LrgCamera2D *self,
                       gfloat       zoom)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));
	g_return_if_fail (zoom > 0.0f);

	priv = lrg_camera2d_get_instance_private (self);
	camera2d_write (priv, &priv->zoom, &priv->prev_zoom, zoom);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
}

void
lrg_camera2d_reset_interpolation (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));

	priv = lrg_camera2d_get_instance_private (self);
	camera2d_capture_previous (priv);
	grl_interpolation_track_reset (&priv->interp);
}

gboolean
lrg_camera2d_get_interpolate (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA2D (self), FALSE);

	priv = lrg_camera2d_get_instance_private (self);
	return priv->interp.enabled;
}

void
lrg_camera2d_set_interpolate (LrgCamera2D *self,
                              gboolean     interpolate)
{
	LrgCamera2DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA2D (self));

	priv = lrg_camera2d_get_instance_private (self);

	interpolate = (interpolate != FALSE);
	if (priv->interp.enabled == interpolate)
		return;

	priv->interp.enabled = interpolate;
	lrg_camera2d_reset_interpolation (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERPOLATE]);
}
