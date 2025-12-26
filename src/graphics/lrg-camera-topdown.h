/* lrg-camera-topdown.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-down camera implementation for 2D games.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-camera2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA_TOPDOWN (lrg_camera_topdown_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCameraTopDown, lrg_camera_topdown, LRG, CAMERA_TOPDOWN, LrgCamera2D)

/**
 * LrgCameraTopDownClass:
 * @parent_class: Parent class
 *
 * Class for top-down cameras. Inherits from #LrgCamera2D and provides:
 *
 * - Smooth target following with configurable speed
 * - Circular deadzone (no movement when target is within radius)
 * - World bounds clamping
 * - Screen shake effects
 *
 * Ideal for top-down games like Zelda, Hotline Miami, and twin-stick shooters.
 */
struct _LrgCameraTopDownClass
{
	LrgCamera2DClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera_topdown_new:
 *
 * Create a new top-down camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCameraTopDown
 */
LRG_AVAILABLE_IN_ALL
LrgCameraTopDown * lrg_camera_topdown_new (void);

/* ==========================================================================
 * Following Configuration
 * ========================================================================== */

/**
 * lrg_camera_topdown_get_follow_speed:
 * @self: an #LrgCameraTopDown
 *
 * Get the camera follow speed (smoothing factor).
 *
 * Returns: the follow speed (higher = faster following)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_topdown_get_follow_speed (LrgCameraTopDown *self);

/**
 * lrg_camera_topdown_set_follow_speed:
 * @self: an #LrgCameraTopDown
 * @speed: the follow speed (must be > 0)
 *
 * Set the camera follow speed. Higher values make the camera
 * follow the target more quickly. Uses exponential smoothing
 * for frame-rate independent movement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_set_follow_speed (LrgCameraTopDown *self,
                                          gfloat            speed);

/**
 * lrg_camera_topdown_get_deadzone_radius:
 * @self: an #LrgCameraTopDown
 *
 * Get the circular deadzone radius.
 *
 * Returns: the deadzone radius in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_topdown_get_deadzone_radius (LrgCameraTopDown *self);

/**
 * lrg_camera_topdown_set_deadzone_radius:
 * @self: an #LrgCameraTopDown
 * @radius: the deadzone radius (0 = no deadzone)
 *
 * Set the circular deadzone radius. The camera won't move if
 * the target is within this distance from the camera center.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_set_deadzone_radius (LrgCameraTopDown *self,
                                             gfloat            radius);

/* ==========================================================================
 * Target Following
 * ========================================================================== */

/**
 * lrg_camera_topdown_follow:
 * @self: an #LrgCameraTopDown
 * @target_x: the target X position in world space
 * @target_y: the target Y position in world space
 * @delta_time: the frame delta time in seconds
 *
 * Update the camera to follow a target position. Call this each frame
 * with the target's current position. The camera will smoothly track
 * the target based on follow_speed and deadzone settings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_follow (LrgCameraTopDown *self,
                                gfloat            target_x,
                                gfloat            target_y,
                                gfloat            delta_time);

/* ==========================================================================
 * World Bounds
 * ========================================================================== */

/**
 * lrg_camera_topdown_get_bounds_enabled:
 * @self: an #LrgCameraTopDown
 *
 * Check if world bounds clamping is enabled.
 *
 * Returns: %TRUE if bounds are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_topdown_get_bounds_enabled (LrgCameraTopDown *self);

/**
 * lrg_camera_topdown_set_bounds_enabled:
 * @self: an #LrgCameraTopDown
 * @enabled: whether to enable bounds clamping
 *
 * Enable or disable world bounds clamping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_set_bounds_enabled (LrgCameraTopDown *self,
                                            gboolean          enabled);

/**
 * lrg_camera_topdown_set_bounds:
 * @self: an #LrgCameraTopDown
 * @min_x: minimum X bound
 * @min_y: minimum Y bound
 * @max_x: maximum X bound
 * @max_y: maximum Y bound
 *
 * Set the world bounds. The camera target will be clamped
 * to stay within these bounds when bounds are enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_set_bounds (LrgCameraTopDown *self,
                                    gfloat            min_x,
                                    gfloat            min_y,
                                    gfloat            max_x,
                                    gfloat            max_y);

/**
 * lrg_camera_topdown_get_bounds:
 * @self: an #LrgCameraTopDown
 * @out_min_x: (out) (optional): return location for min X
 * @out_min_y: (out) (optional): return location for min Y
 * @out_max_x: (out) (optional): return location for max X
 * @out_max_y: (out) (optional): return location for max Y
 *
 * Get the current world bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_get_bounds (LrgCameraTopDown *self,
                                    gfloat           *out_min_x,
                                    gfloat           *out_min_y,
                                    gfloat           *out_max_x,
                                    gfloat           *out_max_y);

/* ==========================================================================
 * Screen Shake
 * ========================================================================== */

/**
 * lrg_camera_topdown_shake:
 * @self: an #LrgCameraTopDown
 * @intensity: the shake intensity in pixels
 * @duration: the shake duration in seconds
 *
 * Start a screen shake effect. The shake intensity decays
 * linearly over the duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_shake (LrgCameraTopDown *self,
                               gfloat            intensity,
                               gfloat            duration);

/**
 * lrg_camera_topdown_stop_shake:
 * @self: an #LrgCameraTopDown
 *
 * Immediately stop any active screen shake.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_stop_shake (LrgCameraTopDown *self);

/**
 * lrg_camera_topdown_is_shaking:
 * @self: an #LrgCameraTopDown
 *
 * Check if the camera is currently shaking.
 *
 * Returns: %TRUE if screen shake is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_topdown_is_shaking (LrgCameraTopDown *self);

/**
 * lrg_camera_topdown_update_shake:
 * @self: an #LrgCameraTopDown
 * @delta_time: the frame delta time in seconds
 *
 * Update the screen shake effect. Called automatically by follow(),
 * but can be called manually if not using follow().
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_topdown_update_shake (LrgCameraTopDown *self,
                                      gfloat            delta_time);

G_END_DECLS
