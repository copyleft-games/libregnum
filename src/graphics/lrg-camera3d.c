/* lrg-camera3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D camera implementation.
 */

#include "config.h"
#include "lrg-camera3d.h"

/**
 * SECTION:lrg-camera3d
 * @title: LrgCamera3D
 * @short_description: 3D camera implementation
 *
 * #LrgCamera3D is a 3D camera that wraps #GrlCamera3D.
 * It provides position, target, up vector, field of view,
 * and projection controls for 3D games.
 *
 * ## Properties
 *
 * - **position-x**, **position-y**, **position-z**: Camera position in world space
 * - **target-x**, **target-y**, **target-z**: Point the camera looks at
 * - **up-x**, **up-y**, **up-z**: Camera up vector
 * - **fovy**: Field of view in degrees (Y-axis)
 * - **projection**: Perspective or orthographic projection
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();
 *
 * // Position camera behind and above player
 * lrg_camera3d_set_position_xyz (camera, 0.0f, 20.0f, 15.0f);
 * lrg_camera3d_set_target_xyz (camera, 0.0f, 0.0f, 0.0f);
 * lrg_camera3d_set_fovy (camera, 60.0f);
 *
 * lrg_camera_begin (LRG_CAMERA (camera));
 * // Draw 3D game world
 * lrg_camera_end (LRG_CAMERA (camera));
 * ]|
 */

typedef struct
{
	GrlCamera3D      *grl_camera;
	gfloat            position_x;
	gfloat            position_y;
	gfloat            position_z;
	gfloat            target_x;
	gfloat            target_y;
	gfloat            target_z;
	gfloat            up_x;
	gfloat            up_y;
	gfloat            up_z;
	gfloat            fovy;
	LrgProjectionType projection;
} LrgCamera3DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCamera3D, lrg_camera3d, LRG_TYPE_CAMERA)

enum
{
	PROP_0,
	PROP_POSITION_X,
	PROP_POSITION_Y,
	PROP_POSITION_Z,
	PROP_TARGET_X,
	PROP_TARGET_Y,
	PROP_TARGET_Z,
	PROP_UP_X,
	PROP_UP_Y,
	PROP_UP_Z,
	PROP_FOVY,
	PROP_PROJECTION,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static GrlCameraProjection
lrg_projection_to_grl (LrgProjectionType projection)
{
	switch (projection)
	{
	case LRG_PROJECTION_PERSPECTIVE:
		return GRL_CAMERA_PERSPECTIVE;
	case LRG_PROJECTION_ORTHOGRAPHIC:
		return GRL_CAMERA_ORTHOGRAPHIC;
	default:
		return GRL_CAMERA_PERSPECTIVE;
	}
}

static LrgProjectionType G_GNUC_UNUSED
grl_projection_to_lrg (GrlCameraProjection projection)
{
	switch (projection)
	{
	case GRL_CAMERA_PERSPECTIVE:
		return LRG_PROJECTION_PERSPECTIVE;
	case GRL_CAMERA_ORTHOGRAPHIC:
		return LRG_PROJECTION_ORTHOGRAPHIC;
	default:
		return LRG_PROJECTION_PERSPECTIVE;
	}
}

static void
lrg_camera3d_sync_to_grl (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);
	g_autoptr(GrlVector3) up = NULL;

	grl_camera3d_set_position_xyz (priv->grl_camera,
	                               priv->position_x,
	                               priv->position_y,
	                               priv->position_z);
	grl_camera3d_set_target_xyz (priv->grl_camera,
	                             priv->target_x,
	                             priv->target_y,
	                             priv->target_z);

	up = grl_vector3_new (priv->up_x, priv->up_y, priv->up_z);
	grl_camera3d_set_up (priv->grl_camera, up);

	grl_camera3d_set_fovy (priv->grl_camera, priv->fovy);
	grl_camera3d_set_projection (priv->grl_camera, lrg_projection_to_grl (priv->projection));
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_camera3d_begin (LrgCamera *camera)
{
	LrgCamera3D        *self = LRG_CAMERA3D (camera);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	lrg_camera3d_sync_to_grl (self);
	grl_camera3d_begin (priv->grl_camera);
}

static void
lrg_camera3d_end (LrgCamera *camera)
{
	LrgCamera3D        *self = LRG_CAMERA3D (camera);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	grl_camera3d_end (priv->grl_camera);
}

static void
lrg_camera3d_world_to_screen (LrgCamera        *camera,
                              const GrlVector3 *world,
                              GrlVector2       *out_screen)
{
	LrgCamera3D        *self = LRG_CAMERA3D (camera);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	/*
	 * Note: GrlCamera3D doesn't have a direct world_to_screen API.
	 * We would need to use the raylib GetWorldToScreen function.
	 * For now, we provide a placeholder that uses the camera position
	 * to do a simple projection. A proper implementation would use
	 * matrix math or the underlying raylib function.
	 */
	(void)priv;
	(void)world;

	/* Simple placeholder - return screen center */
	out_screen->x = 0.0f;
	out_screen->y = 0.0f;
}

static void
lrg_camera3d_screen_to_world (LrgCamera        *camera,
                              const GrlVector2 *screen,
                              GrlVector3       *out_world)
{
	LrgCamera3D        *self = LRG_CAMERA3D (camera);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	/*
	 * Note: GrlCamera3D doesn't have a direct screen_to_world API.
	 * This would require ray casting. For now, we provide a placeholder.
	 */
	(void)priv;
	(void)screen;

	/* Simple placeholder - return world origin */
	out_world->x = 0.0f;
	out_world->y = 0.0f;
	out_world->z = 0.0f;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera3d_finalize (GObject *object)
{
	LrgCamera3D        *self = LRG_CAMERA3D (object);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	g_clear_object (&priv->grl_camera);

	G_OBJECT_CLASS (lrg_camera3d_parent_class)->finalize (object);
}

static void
lrg_camera3d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgCamera3D        *self = LRG_CAMERA3D (object);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_POSITION_X:
		g_value_set_float (value, priv->position_x);
		break;
	case PROP_POSITION_Y:
		g_value_set_float (value, priv->position_y);
		break;
	case PROP_POSITION_Z:
		g_value_set_float (value, priv->position_z);
		break;
	case PROP_TARGET_X:
		g_value_set_float (value, priv->target_x);
		break;
	case PROP_TARGET_Y:
		g_value_set_float (value, priv->target_y);
		break;
	case PROP_TARGET_Z:
		g_value_set_float (value, priv->target_z);
		break;
	case PROP_UP_X:
		g_value_set_float (value, priv->up_x);
		break;
	case PROP_UP_Y:
		g_value_set_float (value, priv->up_y);
		break;
	case PROP_UP_Z:
		g_value_set_float (value, priv->up_z);
		break;
	case PROP_FOVY:
		g_value_set_float (value, priv->fovy);
		break;
	case PROP_PROJECTION:
		g_value_set_enum (value, priv->projection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera3d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgCamera3D        *self = LRG_CAMERA3D (object);
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_POSITION_X:
		priv->position_x = g_value_get_float (value);
		break;
	case PROP_POSITION_Y:
		priv->position_y = g_value_get_float (value);
		break;
	case PROP_POSITION_Z:
		priv->position_z = g_value_get_float (value);
		break;
	case PROP_TARGET_X:
		priv->target_x = g_value_get_float (value);
		break;
	case PROP_TARGET_Y:
		priv->target_y = g_value_get_float (value);
		break;
	case PROP_TARGET_Z:
		priv->target_z = g_value_get_float (value);
		break;
	case PROP_UP_X:
		priv->up_x = g_value_get_float (value);
		break;
	case PROP_UP_Y:
		priv->up_y = g_value_get_float (value);
		break;
	case PROP_UP_Z:
		priv->up_z = g_value_get_float (value);
		break;
	case PROP_FOVY:
		priv->fovy = g_value_get_float (value);
		if (priv->fovy <= 0.0f)
			priv->fovy = 1.0f;
		break;
	case PROP_PROJECTION:
		priv->projection = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera3d_class_init (LrgCamera3DClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->finalize = lrg_camera3d_finalize;
	object_class->get_property = lrg_camera3d_get_property;
	object_class->set_property = lrg_camera3d_set_property;

	camera_class->begin = lrg_camera3d_begin;
	camera_class->end = lrg_camera3d_end;
	camera_class->world_to_screen = lrg_camera3d_world_to_screen;
	camera_class->screen_to_world = lrg_camera3d_screen_to_world;

	/* Position properties */
	properties[PROP_POSITION_X] =
		g_param_spec_float ("position-x",
		                    "Position X",
		                    "Camera X position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_POSITION_Y] =
		g_param_spec_float ("position-y",
		                    "Position Y",
		                    "Camera Y position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_POSITION_Z] =
		g_param_spec_float ("position-z",
		                    "Position Z",
		                    "Camera Z position",
		                    -G_MAXFLOAT, G_MAXFLOAT, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Target properties */
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

	properties[PROP_TARGET_Z] =
		g_param_spec_float ("target-z",
		                    "Target Z",
		                    "Camera target Z",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* Up vector properties */
	properties[PROP_UP_X] =
		g_param_spec_float ("up-x",
		                    "Up X",
		                    "Camera up vector X",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_UP_Y] =
		g_param_spec_float ("up-y",
		                    "Up Y",
		                    "Camera up vector Y",
		                    -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_UP_Z] =
		g_param_spec_float ("up-z",
		                    "Up Z",
		                    "Camera up vector Z",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/* FOV and projection */
	properties[PROP_FOVY] =
		g_param_spec_float ("fovy",
		                    "Field of View Y",
		                    "Camera field of view in degrees",
		                    1.0f, 180.0f, 45.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_PROJECTION] =
		g_param_spec_enum ("projection",
		                   "Projection",
		                   "Camera projection type",
		                   LRG_TYPE_PROJECTION_TYPE,
		                   LRG_PROJECTION_PERSPECTIVE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera3d_init (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);

	priv->grl_camera = grl_camera3d_new ();
	priv->position_x = 0.0f;
	priv->position_y = 10.0f;
	priv->position_z = 10.0f;
	priv->target_x = 0.0f;
	priv->target_y = 0.0f;
	priv->target_z = 0.0f;
	priv->up_x = 0.0f;
	priv->up_y = 1.0f;
	priv->up_z = 0.0f;
	priv->fovy = 45.0f;
	priv->projection = LRG_PROJECTION_PERSPECTIVE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCamera3D *
lrg_camera3d_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA3D, NULL);
}

GrlVector3 *
lrg_camera3d_get_position (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), NULL);

	priv = lrg_camera3d_get_instance_private (self);
	return grl_vector3_new (priv->position_x, priv->position_y, priv->position_z);
}

void
lrg_camera3d_set_position (LrgCamera3D *self,
                           GrlVector3  *position)
{
	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (position != NULL);

	lrg_camera3d_set_position_xyz (self, position->x, position->y, position->z);
}

void
lrg_camera3d_set_position_xyz (LrgCamera3D *self,
                               gfloat       x,
                               gfloat       y,
                               gfloat       z)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	priv->position_x = x;
	priv->position_y = y;
	priv->position_z = z;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION_Z]);
}

GrlVector3 *
lrg_camera3d_get_target (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), NULL);

	priv = lrg_camera3d_get_instance_private (self);
	return grl_vector3_new (priv->target_x, priv->target_y, priv->target_z);
}

void
lrg_camera3d_set_target (LrgCamera3D *self,
                         GrlVector3  *target)
{
	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (target != NULL);

	lrg_camera3d_set_target_xyz (self, target->x, target->y, target->z);
}

void
lrg_camera3d_set_target_xyz (LrgCamera3D *self,
                             gfloat       x,
                             gfloat       y,
                             gfloat       z)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	priv->target_x = x;
	priv->target_y = y;
	priv->target_z = z;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_Z]);
}

GrlVector3 *
lrg_camera3d_get_up (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), NULL);

	priv = lrg_camera3d_get_instance_private (self);
	return grl_vector3_new (priv->up_x, priv->up_y, priv->up_z);
}

void
lrg_camera3d_set_up (LrgCamera3D *self,
                     GrlVector3  *up)
{
	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (up != NULL);

	lrg_camera3d_set_up_xyz (self, up->x, up->y, up->z);
}

void
lrg_camera3d_set_up_xyz (LrgCamera3D *self,
                         gfloat       x,
                         gfloat       y,
                         gfloat       z)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	priv->up_x = x;
	priv->up_y = y;
	priv->up_z = z;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_Z]);
}

gfloat
lrg_camera3d_get_fovy (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), 45.0f);

	priv = lrg_camera3d_get_instance_private (self);
	return priv->fovy;
}

void
lrg_camera3d_set_fovy (LrgCamera3D *self,
                       gfloat       fovy)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (fovy > 0.0f);

	priv = lrg_camera3d_get_instance_private (self);
	priv->fovy = fovy;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOVY]);
}

LrgProjectionType
lrg_camera3d_get_projection (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), LRG_PROJECTION_PERSPECTIVE);

	priv = lrg_camera3d_get_instance_private (self);
	return priv->projection;
}

void
lrg_camera3d_set_projection (LrgCamera3D       *self,
                             LrgProjectionType  projection)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	priv->projection = projection;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROJECTION]);
}
