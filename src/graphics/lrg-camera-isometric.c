/* lrg-camera-isometric.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Isometric camera implementation.
 */

#include "config.h"
#include "lrg-camera-isometric.h"
#include <math.h>

/**
 * SECTION:lrg-camera-isometric
 * @title: LrgCameraIsometric
 * @short_description: Isometric camera implementation
 *
 * #LrgCameraIsometric is a constrained 3D camera optimized for isometric
 * tile-based games. It inherits from #LrgCamera3D and enforces:
 *
 * - Orthographic projection (no perspective distortion)
 * - Fixed isometric viewing angle (45 horizontal, ~35.264 vertical)
 * - Tile-based coordinate helpers
 *
 * ## Isometric Math
 *
 * Standard isometric projection uses:
 * - 45-degree horizontal rotation (camera X offset = camera Z offset)
 * - 35.264-degree vertical tilt (arctan(1/sqrt(2)))
 * - This gives the classic 2:1 isometric ratio
 *
 * ## Properties
 *
 * - **tile-width**: Base tile width in pixels (default: 64)
 * - **tile-height**: Base tile height in pixels (default: 32)
 * - **height-scale**: Vertical scaling factor (default: 0.5)
 * - **zoom**: Zoom level (default: 1.0)
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgCameraIsometric) camera = lrg_camera_isometric_new ();
 *
 * // Configure for 64x32 tiles
 * lrg_camera_isometric_set_tile_width (camera, 64.0f);
 * lrg_camera_isometric_set_tile_height (camera, 32.0f);
 * lrg_camera_isometric_set_zoom (camera, 2.0f);
 *
 * // Focus on player position
 * lrg_camera_isometric_focus_on (camera, player_x, player_y, player_z);
 *
 * // Use with renderer
 * lrg_renderer_set_camera (renderer, LRG_CAMERA (camera));
 * ]|
 */

/*
 * Isometric angle constants:
 * - ISO_HORIZONTAL_ANGLE: 45 degrees (pi/4 radians)
 * - ISO_VERTICAL_ANGLE: arctan(1/sqrt(2)) = 35.264 degrees
 *
 * For a camera at distance D from target:
 * - X offset = D * cos(45) = D * 0.7071
 * - Z offset = D * cos(45) = D * 0.7071
 * - Y offset = D * sin(35.264) = D * 0.5774
 */
#define ISO_COS_45       (0.70710678f)  /* cos(45 deg) = sin(45 deg) */
#define ISO_TAN_VERTICAL (0.57735027f)  /* tan(35.264 deg) = 1/sqrt(3) */
#define DEFAULT_DISTANCE (20.0f)

typedef struct
{
	gfloat tile_width;
	gfloat tile_height;
	gfloat height_scale;
	gfloat zoom;
	gfloat base_distance;
} LrgCameraIsometricPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgCameraIsometric, lrg_camera_isometric, LRG_TYPE_CAMERA3D)

enum
{
	PROP_0,
	PROP_TILE_WIDTH,
	PROP_TILE_HEIGHT,
	PROP_HEIGHT_SCALE,
	PROP_ZOOM,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * sync_isometric_position:
 * @self: an #LrgCameraIsometric
 *
 * Recalculate the camera position to maintain the isometric angle
 * relative to the current target. Called before rendering to ensure
 * the camera is always at the correct isometric angle.
 */
static void
sync_isometric_position (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv = lrg_camera_isometric_get_instance_private (self);
	g_autoptr(GrlVector3) target = NULL;
	gfloat distance;
	gfloat offset;
	gfloat height;

	target = lrg_camera3d_get_target (LRG_CAMERA3D (self));
	if (target == NULL)
		return;

	/* Calculate camera position based on zoom and base distance */
	distance = priv->base_distance / priv->zoom;
	offset = distance * ISO_COS_45;
	height = distance * ISO_TAN_VERTICAL;

	/* Position camera at isometric angle from target */
	lrg_camera3d_set_position_xyz (LRG_CAMERA3D (self),
	                               target->x + offset,
	                               target->y + height,
	                               target->z + offset);
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_camera_isometric_begin (LrgCamera *camera)
{
	LrgCameraIsometric *self = LRG_CAMERA_ISOMETRIC (camera);

	/* Ensure camera position is at correct isometric angle before rendering */
	sync_isometric_position (self);

	/* Call parent begin to activate the camera */
	LRG_CAMERA_CLASS (lrg_camera_isometric_parent_class)->begin (camera);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_camera_isometric_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgCameraIsometric        *self = LRG_CAMERA_ISOMETRIC (object);
	LrgCameraIsometricPrivate *priv = lrg_camera_isometric_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_TILE_WIDTH:
		g_value_set_float (value, priv->tile_width);
		break;
	case PROP_TILE_HEIGHT:
		g_value_set_float (value, priv->tile_height);
		break;
	case PROP_HEIGHT_SCALE:
		g_value_set_float (value, priv->height_scale);
		break;
	case PROP_ZOOM:
		g_value_set_float (value, priv->zoom);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_isometric_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgCameraIsometric        *self = LRG_CAMERA_ISOMETRIC (object);
	LrgCameraIsometricPrivate *priv = lrg_camera_isometric_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_TILE_WIDTH:
		priv->tile_width = g_value_get_float (value);
		if (priv->tile_width <= 0.0f)
			priv->tile_width = 64.0f;
		break;
	case PROP_TILE_HEIGHT:
		priv->tile_height = g_value_get_float (value);
		if (priv->tile_height <= 0.0f)
			priv->tile_height = 32.0f;
		break;
	case PROP_HEIGHT_SCALE:
		priv->height_scale = g_value_get_float (value);
		if (priv->height_scale <= 0.0f)
			priv->height_scale = 0.5f;
		break;
	case PROP_ZOOM:
		priv->zoom = g_value_get_float (value);
		if (priv->zoom <= 0.0f)
			priv->zoom = 1.0f;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_isometric_class_init (LrgCameraIsometricClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgCameraClass *camera_class = LRG_CAMERA_CLASS (klass);

	object_class->get_property = lrg_camera_isometric_get_property;
	object_class->set_property = lrg_camera_isometric_set_property;

	/* Override begin to enforce isometric constraints */
	camera_class->begin = lrg_camera_isometric_begin;

	/* Properties */
	properties[PROP_TILE_WIDTH] =
		g_param_spec_float ("tile-width",
		                    "Tile Width",
		                    "Base tile width in pixels",
		                    1.0f, G_MAXFLOAT, 64.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_TILE_HEIGHT] =
		g_param_spec_float ("tile-height",
		                    "Tile Height",
		                    "Base tile height in pixels",
		                    1.0f, G_MAXFLOAT, 32.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_HEIGHT_SCALE] =
		g_param_spec_float ("height-scale",
		                    "Height Scale",
		                    "Vertical scaling factor for height differences",
		                    0.01f, 10.0f, 0.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	properties[PROP_ZOOM] =
		g_param_spec_float ("zoom",
		                    "Zoom",
		                    "Camera zoom level (1.0 is default)",
		                    0.01f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera_isometric_init (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv = lrg_camera_isometric_get_instance_private (self);

	/* Initialize isometric-specific properties */
	priv->tile_width = 64.0f;
	priv->tile_height = 32.0f;
	priv->height_scale = 0.5f;
	priv->zoom = 1.0f;
	priv->base_distance = DEFAULT_DISTANCE;

	/* Force orthographic projection - isometric requires it */
	lrg_camera3d_set_projection (LRG_CAMERA3D (self), LRG_PROJECTION_ORTHOGRAPHIC);

	/* Set initial target at origin */
	lrg_camera3d_set_target_xyz (LRG_CAMERA3D (self), 0.0f, 0.0f, 0.0f);

	/* Set up vector (always straight up for isometric) */
	lrg_camera3d_set_up_xyz (LRG_CAMERA3D (self), 0.0f, 1.0f, 0.0f);

	/* Set initial camera position at isometric angle */
	sync_isometric_position (self);

	/* Set FOV for orthographic projection (controls view size) */
	lrg_camera3d_set_fovy (LRG_CAMERA3D (self), 20.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCameraIsometric *
lrg_camera_isometric_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_ISOMETRIC, NULL);
}

gfloat
lrg_camera_isometric_get_tile_width (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_ISOMETRIC (self), 64.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	return priv->tile_width;
}

void
lrg_camera_isometric_set_tile_width (LrgCameraIsometric *self,
                                     gfloat              width)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (width > 0.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	priv->tile_width = width;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_WIDTH]);
}

gfloat
lrg_camera_isometric_get_tile_height (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_ISOMETRIC (self), 32.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	return priv->tile_height;
}

void
lrg_camera_isometric_set_tile_height (LrgCameraIsometric *self,
                                      gfloat              height)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (height > 0.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	priv->tile_height = height;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_HEIGHT]);
}

gfloat
lrg_camera_isometric_get_height_scale (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_ISOMETRIC (self), 0.5f);

	priv = lrg_camera_isometric_get_instance_private (self);
	return priv->height_scale;
}

void
lrg_camera_isometric_set_height_scale (LrgCameraIsometric *self,
                                       gfloat              scale)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (scale > 0.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	priv->height_scale = scale;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT_SCALE]);
}

gfloat
lrg_camera_isometric_get_zoom (LrgCameraIsometric *self)
{
	LrgCameraIsometricPrivate *priv;

	g_return_val_if_fail (LRG_IS_CAMERA_ISOMETRIC (self), 1.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	return priv->zoom;
}

void
lrg_camera_isometric_set_zoom (LrgCameraIsometric *self,
                               gfloat              zoom)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (zoom > 0.0f);

	priv = lrg_camera_isometric_get_instance_private (self);
	priv->zoom = zoom;

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
}

void
lrg_camera_isometric_focus_on (LrgCameraIsometric *self,
                               gfloat              world_x,
                               gfloat              world_y,
                               gfloat              world_z)
{
	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));

	/* Set target - the camera position will be updated in begin() */
	lrg_camera3d_set_target_xyz (LRG_CAMERA3D (self), world_x, world_y, world_z);
}

void
lrg_camera_isometric_world_to_tile (LrgCameraIsometric *self,
                                    gfloat              world_x,
                                    gfloat              world_y G_GNUC_UNUSED,
                                    gfloat              world_z,
                                    gint               *out_tile_x,
                                    gint               *out_tile_y)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (out_tile_x != NULL);
	g_return_if_fail (out_tile_y != NULL);

	priv = lrg_camera_isometric_get_instance_private (self);

	/*
	 * Convert world XZ to tile coordinates.
	 * In a standard isometric setup:
	 * - Tile X increases along the world X axis
	 * - Tile Y increases along the world Z axis
	 *
	 * The tile size determines the world unit size.
	 * We use tile_width / 2 as the base unit since isometric tiles
	 * overlap by half.
	 */
	*out_tile_x = (gint)floorf (world_x / (priv->tile_width / 2.0f));
	*out_tile_y = (gint)floorf (world_z / (priv->tile_height));
}

void
lrg_camera_isometric_tile_to_world (LrgCameraIsometric *self,
                                    gint                tile_x,
                                    gint                tile_y,
                                    gfloat             *out_world_x,
                                    gfloat             *out_world_z)
{
	LrgCameraIsometricPrivate *priv;

	g_return_if_fail (LRG_IS_CAMERA_ISOMETRIC (self));
	g_return_if_fail (out_world_x != NULL);
	g_return_if_fail (out_world_z != NULL);

	priv = lrg_camera_isometric_get_instance_private (self);

	/*
	 * Convert tile coordinates to world XZ position.
	 * Returns the center of the tile.
	 */
	*out_world_x = (gfloat)tile_x * (priv->tile_width / 2.0f) + (priv->tile_width / 4.0f);
	*out_world_z = (gfloat)tile_y * priv->tile_height + (priv->tile_height / 2.0f);
}
