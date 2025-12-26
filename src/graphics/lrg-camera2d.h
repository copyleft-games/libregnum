/* lrg-camera2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 2D camera implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-camera.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA2D (lrg_camera2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCamera2D, lrg_camera2d, LRG, CAMERA2D, LrgCamera)

/**
 * LrgCamera2DClass:
 * @parent_class: Parent class
 *
 * Class for 2D cameras. Can be subclassed for custom behavior
 * like smooth follow cameras or shake effects.
 */
struct _LrgCamera2DClass
{
	LrgCameraClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera2d_new:
 *
 * Create a new 2D camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCamera2D
 */
LRG_AVAILABLE_IN_ALL
LrgCamera2D * lrg_camera2d_new (void);

/* ==========================================================================
 * Offset
 * ========================================================================== */

/**
 * lrg_camera2d_get_offset:
 * @self: an #LrgCamera2D
 *
 * Get the camera offset (displacement from target).
 *
 * Returns: (transfer full): the offset as a #GrlVector2
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_camera2d_get_offset (LrgCamera2D *self);

/**
 * lrg_camera2d_set_offset:
 * @self: an #LrgCamera2D
 * @offset: the new offset
 *
 * Set the camera offset (displacement from target).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_offset (LrgCamera2D *self,
                              GrlVector2  *offset);

/**
 * lrg_camera2d_set_offset_xy:
 * @self: an #LrgCamera2D
 * @x: the x offset
 * @y: the y offset
 *
 * Set the camera offset using separate x and y values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_offset_xy (LrgCamera2D *self,
                                 gfloat       x,
                                 gfloat       y);

/* ==========================================================================
 * Target
 * ========================================================================== */

/**
 * lrg_camera2d_get_target:
 * @self: an #LrgCamera2D
 *
 * Get the camera target (point the camera follows).
 *
 * Returns: (transfer full): the target as a #GrlVector2
 */
LRG_AVAILABLE_IN_ALL
GrlVector2 * lrg_camera2d_get_target (LrgCamera2D *self);

/**
 * lrg_camera2d_set_target:
 * @self: an #LrgCamera2D
 * @target: the new target
 *
 * Set the camera target (point the camera follows).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_target (LrgCamera2D *self,
                              GrlVector2  *target);

/**
 * lrg_camera2d_set_target_xy:
 * @self: an #LrgCamera2D
 * @x: the x target
 * @y: the y target
 *
 * Set the camera target using separate x and y values.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_target_xy (LrgCamera2D *self,
                                 gfloat       x,
                                 gfloat       y);

/* ==========================================================================
 * Rotation & Zoom
 * ========================================================================== */

/**
 * lrg_camera2d_get_rotation:
 * @self: an #LrgCamera2D
 *
 * Get the camera rotation in degrees.
 *
 * Returns: the rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera2d_get_rotation (LrgCamera2D *self);

/**
 * lrg_camera2d_set_rotation:
 * @self: an #LrgCamera2D
 * @rotation: the rotation in degrees
 *
 * Set the camera rotation in degrees.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_rotation (LrgCamera2D *self,
                                gfloat       rotation);

/**
 * lrg_camera2d_get_zoom:
 * @self: an #LrgCamera2D
 *
 * Get the camera zoom level.
 *
 * Returns: the zoom level (1.0 = normal, >1.0 = zoomed in)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera2d_get_zoom (LrgCamera2D *self);

/**
 * lrg_camera2d_set_zoom:
 * @self: an #LrgCamera2D
 * @zoom: the zoom level (must be > 0)
 *
 * Set the camera zoom level.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera2d_set_zoom (LrgCamera2D *self,
                            gfloat       zoom);

G_END_DECLS
