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
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static void
lrg_camera2d_sync_to_grl (LrgCamera2D *self)
{
	LrgCamera2DPrivate *priv = lrg_camera2d_get_instance_private (self);

	grl_camera2d_set_offset_xy (priv->grl_camera, priv->offset_x, priv->offset_y);
	grl_camera2d_set_target_xy (priv->grl_camera, priv->target_x, priv->target_y);
	grl_camera2d_set_rotation (priv->grl_camera, priv->rotation);
	grl_camera2d_set_zoom (priv->grl_camera, priv->zoom);
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
		priv->offset_x = g_value_get_float (value);
		break;
	case PROP_OFFSET_Y:
		priv->offset_y = g_value_get_float (value);
		break;
	case PROP_TARGET_X:
		priv->target_x = g_value_get_float (value);
		break;
	case PROP_TARGET_Y:
		priv->target_y = g_value_get_float (value);
		break;
	case PROP_ROTATION:
		priv->rotation = g_value_get_float (value);
		break;
	case PROP_ZOOM:
		priv->zoom = g_value_get_float (value);
		if (priv->zoom <= 0.0f)
			priv->zoom = 0.01f;
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
	priv->offset_x = x;
	priv->offset_y = y;

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
	priv->target_x = x;
	priv->target_y = y;

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
	priv->rotation = rotation;

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
	priv->zoom = zoom;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
}
