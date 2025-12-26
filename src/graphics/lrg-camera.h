/* lrg-camera.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract camera base class.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA (lrg_camera_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCamera, lrg_camera, LRG, CAMERA, GObject)

/**
 * LrgCameraClass:
 * @parent_class: Parent class
 * @begin: Begin rendering with this camera's transform
 * @end: End rendering with this camera's transform
 * @world_to_screen: Convert world coordinates to screen coordinates
 * @screen_to_world: Convert screen coordinates to world coordinates
 *
 * Abstract class for camera implementations.
 *
 * Subclasses must implement all virtual methods. Two implementations
 * are provided: #LrgCamera2D for 2D games and #LrgCamera3D for 3D games.
 */
struct _LrgCameraClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgCameraClass::begin:
	 * @self: an #LrgCamera
	 *
	 * Begin rendering with this camera's transform.
	 * All drawing calls after this will be transformed.
	 */
	void (*begin) (LrgCamera *self);

	/**
	 * LrgCameraClass::end:
	 * @self: an #LrgCamera
	 *
	 * End rendering with this camera's transform.
	 * Returns to screen-space rendering.
	 */
	void (*end) (LrgCamera *self);

	/**
	 * LrgCameraClass::world_to_screen:
	 * @self: an #LrgCamera
	 * @world: world position
	 * @out_screen: (out): screen position output
	 *
	 * Convert world coordinates to screen coordinates.
	 */
	void (*world_to_screen) (LrgCamera        *self,
	                         const GrlVector3 *world,
	                         GrlVector2       *out_screen);

	/**
	 * LrgCameraClass::screen_to_world:
	 * @self: an #LrgCamera
	 * @screen: screen position
	 * @out_world: (out): world position output
	 *
	 * Convert screen coordinates to world coordinates.
	 */
	void (*screen_to_world) (LrgCamera        *self,
	                         const GrlVector2 *screen,
	                         GrlVector3       *out_world);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_camera_begin:
 * @self: an #LrgCamera
 *
 * Begin rendering with this camera's transform.
 *
 * All drawing calls after this will use the camera's
 * view and projection matrices.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_begin (LrgCamera *self);

/**
 * lrg_camera_end:
 * @self: an #LrgCamera
 *
 * End rendering with this camera's transform.
 *
 * Returns to screen-space (2D) rendering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_end (LrgCamera *self);

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
LRG_AVAILABLE_IN_ALL
void lrg_camera_world_to_screen (LrgCamera        *self,
                                 const GrlVector3 *world,
                                 GrlVector2       *out_screen);

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
LRG_AVAILABLE_IN_ALL
void lrg_camera_screen_to_world (LrgCamera        *self,
                                 const GrlVector2 *screen,
                                 GrlVector3       *out_world);

G_END_DECLS
