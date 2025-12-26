/* lrg-camera.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract camera base class.
 */

#include "config.h"
#include "lrg-camera.h"

/**
 * SECTION:lrg-camera
 * @title: LrgCamera
 * @short_description: Abstract camera base class
 *
 * #LrgCamera is an abstract base class for camera implementations.
 * It defines the interface that all cameras must implement.
 *
 * Two implementations are provided:
 * - #LrgCamera2D for 2D games with offset, target, zoom, and rotation
 * - #LrgCamera3D for 3D games with position, target, up vector, and projection
 *
 * ## Using Cameras
 *
 * Cameras are used with the renderer's begin_layer/end_layer methods,
 * or directly with begin/end:
 *
 * |[<!-- language="C" -->
 * lrg_camera_begin (camera);
 * // All drawing here uses camera transform
 * grl_draw_sphere (position, 1.0f, color);
 * lrg_camera_end (camera);
 * ]|
 *
 * ## Custom Cameras
 *
 * You can create custom camera behaviors by subclassing #LrgCamera2D
 * or #LrgCamera3D. For example, a follow camera or orbit camera.
 */

G_DEFINE_ABSTRACT_TYPE (LrgCamera, lrg_camera, G_TYPE_OBJECT)

static void
lrg_camera_class_init (LrgCameraClass *klass)
{
	/* Abstract class - no implementation needed */
}

static void
lrg_camera_init (LrgCamera *self)
{
	/* Nothing to initialize in base class */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_camera_begin:
 * @self: an #LrgCamera
 *
 * Begin rendering with this camera's transform.
 *
 * All drawing calls after this will use the camera's
 * view and projection matrices.
 */
void
lrg_camera_begin (LrgCamera *self)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->begin != NULL);

	klass->begin (self);
}

/**
 * lrg_camera_end:
 * @self: an #LrgCamera
 *
 * End rendering with this camera's transform.
 *
 * Returns to screen-space (2D) rendering.
 */
void
lrg_camera_end (LrgCamera *self)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->end != NULL);

	klass->end (self);
}

/**
 * lrg_camera_world_to_screen:
 * @self: an #LrgCamera
 * @world: world position (uses x, y for 2D cameras)
 * @out_screen: (out): screen position output
 *
 * Convert world coordinates to screen coordinates.
 *
 * For 2D cameras, only the x and y components of @world are used.
 */
void
lrg_camera_world_to_screen (LrgCamera        *self,
                            const GrlVector3 *world,
                            GrlVector2       *out_screen)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));
	g_return_if_fail (world != NULL);
	g_return_if_fail (out_screen != NULL);

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->world_to_screen != NULL);

	klass->world_to_screen (self, world, out_screen);
}

/**
 * lrg_camera_screen_to_world:
 * @self: an #LrgCamera
 * @screen: screen position
 * @out_world: (out): world position output
 *
 * Convert screen coordinates to world coordinates.
 *
 * For 2D cameras, the z component of @out_world will be 0.
 */
void
lrg_camera_screen_to_world (LrgCamera        *self,
                            const GrlVector2 *screen,
                            GrlVector3       *out_world)
{
	LrgCameraClass *klass;

	g_return_if_fail (LRG_IS_CAMERA (self));
	g_return_if_fail (screen != NULL);
	g_return_if_fail (out_world != NULL);

	klass = LRG_CAMERA_GET_CLASS (self);
	g_return_if_fail (klass->screen_to_world != NULL);

	klass->screen_to_world (self, screen, out_world);
}
