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

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

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
	gfloat            aspect;      /* 0: the target's own */

	/* Render interpolation: eye and target at the start of the latest
	 * fixed step they changed in (see grl-interpolation.h) */
	gfloat                prev_position_x;
	gfloat                prev_position_y;
	gfloat                prev_position_z;
	gfloat                prev_target_x;
	gfloat                prev_target_y;
	gfloat                prev_target_z;
	GrlInterpolationTrack interp;
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
	PROP_ASPECT,
	PROP_INTERPOLATE,
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

/* The first write in a fixed step keeps the eye and target the step
 * began with */
static void
camera3d_capture_previous (LrgCamera3DPrivate *priv)
{
	priv->prev_position_x = priv->position_x;
	priv->prev_position_y = priv->position_y;
	priv->prev_position_z = priv->position_z;
	priv->prev_target_x = priv->target_x;
	priv->prev_target_y = priv->target_y;
	priv->prev_target_z = priv->target_z;
}

/* Stores one interpolated eye/target coordinate */
static void
camera3d_write (LrgCamera3DPrivate *priv,
                gfloat             *current,
                gfloat             *previous,
                gfloat              value)
{
	if (grl_interpolation_track_begin_write (&priv->interp))
		camera3d_capture_previous (priv);

	grl_interpolation_track_write_float (&priv->interp, current, previous, value);
}

/*
 * lrg_camera3d_sync_to_grl:
 *
 * Pushes the camera to graylib. Eye and target are the render
 * (interpolated) ones when the camera moved in the latest fixed step, so
 * a camera following a target from fixed_update moves with the drawn
 * objects; the up vector, field of view and projection are not blended.
 */
static void
lrg_camera3d_sync_to_grl (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv = lrg_camera3d_get_instance_private (self);
	g_autoptr(GrlVector3) up = NULL;
	GrlVector3 position;
	GrlVector3 target;

	lrg_camera3d_get_render_pose (self, &position, &target);
	grl_camera3d_set_position_xyz (priv->grl_camera, position.x, position.y, position.z);
	grl_camera3d_set_target_xyz (priv->grl_camera, target.x, target.y, target.z);

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

	/*
	 * raylib's BeginMode3D takes the aspect from the bound target; with
	 * an override the perspective frustum is loaded again with the same
	 * field of view and cull distances, so a view drawn into a target of
	 * another shape (non-square pixels) is stretched back undistorted.
	 */
	if (priv->aspect > 0.0f && priv->projection == LRG_PROJECTION_PERSPECTIVE)
	{
		gdouble near_plane;
		gdouble far_plane;
		gdouble top;
		gdouble right;

		near_plane = rlGetCullDistanceNear ();
		far_plane = rlGetCullDistanceFar ();
		top = near_plane * tan ((gdouble)priv->fovy * 0.5 * DEG2RAD);
		right = top * (gdouble)priv->aspect;
		rlMatrixMode (RL_PROJECTION);
		rlLoadIdentity ();
		rlFrustum (-right, right, -top, top, near_plane, far_plane);
		rlMatrixMode (RL_MODELVIEW);
	}
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
	case PROP_ASPECT:
		g_value_set_float (value, priv->aspect);
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
		camera3d_write (priv, &priv->position_x, &priv->prev_position_x, g_value_get_float (value));
		break;
	case PROP_POSITION_Y:
		camera3d_write (priv, &priv->position_y, &priv->prev_position_y, g_value_get_float (value));
		break;
	case PROP_POSITION_Z:
		camera3d_write (priv, &priv->position_z, &priv->prev_position_z, g_value_get_float (value));
		break;
	case PROP_TARGET_X:
		camera3d_write (priv, &priv->target_x, &priv->prev_target_x, g_value_get_float (value));
		break;
	case PROP_TARGET_Y:
		camera3d_write (priv, &priv->target_y, &priv->prev_target_y, g_value_get_float (value));
		break;
	case PROP_TARGET_Z:
		camera3d_write (priv, &priv->target_z, &priv->prev_target_z, g_value_get_float (value));
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
	case PROP_ASPECT:
		lrg_camera3d_set_aspect (self, g_value_get_float (value));
		break;
	case PROP_INTERPOLATE:
		lrg_camera3d_set_interpolate (self, g_value_get_boolean (value));
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

	/**
	 * LrgCamera3D:aspect:
	 *
	 * The perspective projection's width over height; 0 takes the bound
	 * target's own (raylib's default).
	 */
	properties[PROP_ASPECT] =
		g_param_spec_float ("aspect",
		                    "Aspect",
		                    "Projection aspect ratio (0: the target's)",
		                    0.0f, 16.0f, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgCamera3D:interpolate:
	 *
	 * Whether the eye and target are interpolated for drawing when the
	 * camera is moved in fixed steps, so it moves smoothly with the
	 * interpolated objects on a display faster than the step rate.
	 */
	properties[PROP_INTERPOLATE] =
		g_param_spec_boolean ("interpolate",
		                      "Interpolate",
		                      "Whether the eye and target are interpolated for drawing",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

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

	/* New cameras start with previous == current */
	camera3d_capture_previous (priv);
	grl_interpolation_track_init (&priv->interp);
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
	camera3d_write (priv, &priv->position_x, &priv->prev_position_x, x);
	camera3d_write (priv, &priv->position_y, &priv->prev_position_y, y);
	camera3d_write (priv, &priv->position_z, &priv->prev_position_z, z);

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
	camera3d_write (priv, &priv->target_x, &priv->prev_target_x, x);
	camera3d_write (priv, &priv->target_y, &priv->prev_target_y, y);
	camera3d_write (priv, &priv->target_z, &priv->prev_target_z, z);

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

gfloat
lrg_camera3d_get_aspect (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), 0.0f);

	priv = lrg_camera3d_get_instance_private (self);
	return priv->aspect;
}

void
lrg_camera3d_set_aspect (LrgCamera3D *self,
                         gfloat       aspect)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	/* anything not a positive finite ratio is "the target's own" */
	if (!isfinite (aspect) || aspect <= 0.0f)
		aspect = 0.0f;
	aspect = MIN (aspect, 16.0f);
	if (priv->aspect == aspect)
		return;
	priv->aspect = aspect;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASPECT]);
}

/* ==========================================================================
 * Quaternion Orientation API
 * ========================================================================== */

/*
 * lrg_camera3d_get_orientation:
 * @self: an #LrgCamera3D
 *
 * Gets the camera orientation as a quaternion.
 *
 * The orientation is computed from the camera's look direction
 * (position to target) and up vector.
 *
 * Returns: (transfer full): A new #GrlQuaternion with the camera orientation
 */
GrlQuaternion *
lrg_camera3d_get_orientation (LrgCamera3D *self)
{
	LrgCamera3DPrivate   *priv;
	g_autoptr(GrlVector3) forward = NULL;
	g_autoptr(GrlVector3) right = NULL;
	g_autoptr(GrlVector3) up = NULL;
	g_autoptr(GrlVector3) up_input = NULL;
	gfloat                forward_x;
	gfloat                forward_y;
	gfloat                forward_z;
	gfloat                len;
	gfloat                trace;
	gfloat                s;
	gfloat                qw;
	gfloat                qx;
	gfloat                qy;
	gfloat                qz;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), NULL);

	priv = lrg_camera3d_get_instance_private (self);

	/*
	 * Calculate forward direction (normalized)
	 */
	forward_x = priv->target_x - priv->position_x;
	forward_y = priv->target_y - priv->position_y;
	forward_z = priv->target_z - priv->position_z;

	len = sqrtf (forward_x * forward_x + forward_y * forward_y + forward_z * forward_z);
	if (len < 0.0001f)
	{
		/* Camera at target - return identity */
		return grl_quaternion_new_identity ();
	}

	forward_x /= len;
	forward_y /= len;
	forward_z /= len;

	forward = grl_vector3_new (forward_x, forward_y, forward_z);
	up_input = grl_vector3_new (priv->up_x, priv->up_y, priv->up_z);

	/*
	 * Calculate right vector: right = forward x up
	 */
	right = grl_vector3_cross (forward, up_input);
	right = grl_vector3_normalize (right);

	/*
	 * Recalculate up: up = right x forward
	 */
	up = grl_vector3_cross (right, forward);

	/*
	 * Build rotation matrix and extract quaternion.
	 * Matrix columns are: right, up, -forward
	 * Using the standard rotation matrix to quaternion conversion.
	 */
	trace = right->x + up->y + (-forward_z);

	if (trace > 0.0f)
	{
		s = sqrtf (trace + 1.0f) * 2.0f;
		qw = 0.25f * s;
		qx = (up->z - (-forward_y)) / s;
		qy = ((-forward_x) - right->z) / s;
		qz = (right->y - up->x) / s;
	}
	else if (right->x > up->y && right->x > (-forward_z))
	{
		s = sqrtf (1.0f + right->x - up->y - (-forward_z)) * 2.0f;
		qw = (up->z - (-forward_y)) / s;
		qx = 0.25f * s;
		qy = (right->y + up->x) / s;
		qz = ((-forward_x) + right->z) / s;
	}
	else if (up->y > (-forward_z))
	{
		s = sqrtf (1.0f + up->y - right->x - (-forward_z)) * 2.0f;
		qw = ((-forward_x) - right->z) / s;
		qx = (right->y + up->x) / s;
		qy = 0.25f * s;
		qz = (up->z + (-forward_y)) / s;
	}
	else
	{
		s = sqrtf (1.0f + (-forward_z) - right->x - up->y) * 2.0f;
		qw = (right->y - up->x) / s;
		qx = ((-forward_x) + right->z) / s;
		qy = (up->z + (-forward_y)) / s;
		qz = 0.25f * s;
	}

	return grl_quaternion_new (qx, qy, qz, qw);
}

/*
 * lrg_camera3d_set_orientation:
 * @self: an #LrgCamera3D
 * @quaternion: The orientation quaternion
 *
 * Sets the camera orientation from a quaternion.
 *
 * This updates the camera's target position and up vector based on
 * the orientation while keeping the camera position unchanged.
 */
void
lrg_camera3d_set_orientation (LrgCamera3D         *self,
                              const GrlQuaternion *quaternion)
{
	LrgCamera3DPrivate    *priv;
	g_autoptr(GrlMatrix)   rot_matrix = NULL;
	g_autoptr(GrlVector3)  forward = NULL;
	g_autoptr(GrlVector3)  up = NULL;
	g_autoptr(GrlVector3)  default_forward = NULL;
	g_autoptr(GrlVector3)  default_up = NULL;

	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (quaternion != NULL);

	priv = lrg_camera3d_get_instance_private (self);

	/*
	 * Default camera looks down -Z with Y up.
	 * We transform these vectors by the quaternion rotation.
	 */
	default_forward = grl_vector3_new (0.0f, 0.0f, -1.0f);
	default_up = grl_vector3_new (0.0f, 1.0f, 0.0f);

	/* Convert quaternion to rotation matrix */
	rot_matrix = grl_quaternion_to_matrix (quaternion);

	/*
	 * Transform forward and up vectors using raylib's Vector3Transform.
	 * We convert between GrlVector3 (boxed type) and raylib's Vector3.
	 */
	{
		Vector3 rl_forward = { default_forward->x, default_forward->y, default_forward->z };
		Vector3 rl_up = { default_up->x, default_up->y, default_up->z };
		Matrix rl_matrix;
		Vector3 result_forward;
		Vector3 result_up;

		/* Copy GrlMatrix to raylib Matrix */
		memcpy (&rl_matrix, rot_matrix, sizeof (Matrix));

		result_forward = Vector3Transform (rl_forward, rl_matrix);
		result_up = Vector3Transform (rl_up, rl_matrix);

		forward = grl_vector3_new (result_forward.x, result_forward.y, result_forward.z);
		up = grl_vector3_new (result_up.x, result_up.y, result_up.z);
	}

	/* Set target = position + forward */
	priv->target_x = priv->position_x + forward->x;
	priv->target_y = priv->position_y + forward->y;
	priv->target_z = priv->position_z + forward->z;

	/* Set up vector */
	priv->up_x = up->x;
	priv->up_y = up->y;
	priv->up_z = up->z;

	/* Notify property changes */
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_Z]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_X]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_Y]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UP_Z]);
}

/*
 * lrg_camera3d_slerp_to:
 * @self: an #LrgCamera3D
 * @target: Target orientation quaternion
 * @amount: Interpolation amount (0.0 to 1.0)
 *
 * Spherically interpolates the camera orientation toward the target.
 */
void
lrg_camera3d_slerp_to (LrgCamera3D         *self,
                       const GrlQuaternion *target,
                       gfloat               amount)
{
	g_autoptr(GrlQuaternion) current = NULL;
	g_autoptr(GrlQuaternion) result = NULL;

	g_return_if_fail (LRG_IS_CAMERA3D (self));
	g_return_if_fail (target != NULL);

	/* Get current orientation */
	current = lrg_camera3d_get_orientation (self);

	/* Perform spherical interpolation */
	result = grl_quaternion_slerp (current, target, amount);

	/* Apply interpolated orientation */
	lrg_camera3d_set_orientation (self, result);
}

void
lrg_camera3d_reset_interpolation (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);
	camera3d_capture_previous (priv);
	grl_interpolation_track_reset (&priv->interp);
}

gboolean
lrg_camera3d_get_interpolate (LrgCamera3D *self)
{
	LrgCamera3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA3D (self), FALSE);

	priv = lrg_camera3d_get_instance_private (self);
	return priv->interp.enabled;
}

void
lrg_camera3d_set_interpolate (LrgCamera3D *self,
                              gboolean     interpolate)
{
	LrgCamera3DPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);

	interpolate = (interpolate != FALSE);
	if (priv->interp.enabled == interpolate)
		return;

	priv->interp.enabled = interpolate;
	lrg_camera3d_reset_interpolation (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTERPOLATE]);
}

void
lrg_camera3d_get_render_pose (LrgCamera3D *self,
                              GrlVector3  *position,
                              GrlVector3  *target)
{
	LrgCamera3DPrivate *priv;
	gfloat              alpha;

	g_return_if_fail (LRG_IS_CAMERA3D (self));

	priv = lrg_camera3d_get_instance_private (self);

	/* Not moved in the latest step (or interpolation off): true pose */
	if (!grl_interpolation_track_get_blend (&priv->interp, &alpha))
	{
		if (position != NULL)
		{
			position->x = priv->position_x;
			position->y = priv->position_y;
			position->z = priv->position_z;
		}
		if (target != NULL)
		{
			target->x = priv->target_x;
			target->y = priv->target_y;
			target->z = priv->target_z;
		}
		return;
	}

	if (position != NULL)
	{
		position->x = grl_interpolation_lerp (priv->prev_position_x, priv->position_x, alpha);
		position->y = grl_interpolation_lerp (priv->prev_position_y, priv->position_y, alpha);
		position->z = grl_interpolation_lerp (priv->prev_position_z, priv->position_z, alpha);
	}
	if (target != NULL)
	{
		target->x = grl_interpolation_lerp (priv->prev_target_x, priv->target_x, alpha);
		target->y = grl_interpolation_lerp (priv->prev_target_y, priv->target_y, alpha);
		target->z = grl_interpolation_lerp (priv->prev_target_z, priv->target_z, alpha);
	}
}
