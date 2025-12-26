/* lrg-camera-sideon.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Side-on (platformer) camera implementation for 2D games.
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

#define LRG_TYPE_CAMERA_SIDEON (lrg_camera_sideon_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCameraSideOn, lrg_camera_sideon, LRG, CAMERA_SIDEON, LrgCamera2D)

/**
 * LrgCameraSideOnClass:
 * @parent_class: Parent class
 *
 * Class for side-on (platformer) cameras. Inherits from #LrgCamera2D and provides:
 *
 * - Separate X/Y axis following with different speeds
 * - Horizontal lookahead based on movement direction
 * - Rectangular deadzone (larger vertically for jump jitter reduction)
 * - Vertical bias to show more ground than sky
 * - World bounds clamping
 * - Screen shake effects
 *
 * Ideal for platformer games like Mario, Celeste, and Hollow Knight.
 */
struct _LrgCameraSideOnClass
{
	LrgCamera2DClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera_sideon_new:
 *
 * Create a new side-on camera with default settings.
 *
 * Returns: (transfer full): a new #LrgCameraSideOn
 */
LRG_AVAILABLE_IN_ALL
LrgCameraSideOn * lrg_camera_sideon_new (void);

/* ==========================================================================
 * Following Configuration
 * ========================================================================== */

/**
 * lrg_camera_sideon_get_follow_speed_x:
 * @self: an #LrgCameraSideOn
 *
 * Get the horizontal follow speed.
 *
 * Returns: the horizontal follow speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_follow_speed_x (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_follow_speed_x:
 * @self: an #LrgCameraSideOn
 * @speed: the horizontal follow speed (must be > 0)
 *
 * Set the horizontal follow speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_follow_speed_x (LrgCameraSideOn *self,
                                           gfloat           speed);

/**
 * lrg_camera_sideon_get_follow_speed_y:
 * @self: an #LrgCameraSideOn
 *
 * Get the vertical follow speed.
 *
 * Returns: the vertical follow speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_follow_speed_y (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_follow_speed_y:
 * @self: an #LrgCameraSideOn
 * @speed: the vertical follow speed (must be > 0)
 *
 * Set the vertical follow speed. Typically slower than horizontal
 * to reduce jitter during jumps.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_follow_speed_y (LrgCameraSideOn *self,
                                           gfloat           speed);

/* ==========================================================================
 * Deadzone Configuration
 * ========================================================================== */

/**
 * lrg_camera_sideon_get_deadzone_width:
 * @self: an #LrgCameraSideOn
 *
 * Get the horizontal deadzone width.
 *
 * Returns: the deadzone width in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_deadzone_width (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_get_deadzone_height:
 * @self: an #LrgCameraSideOn
 *
 * Get the vertical deadzone height.
 *
 * Returns: the deadzone height in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_deadzone_height (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_deadzone:
 * @self: an #LrgCameraSideOn
 * @width: the deadzone width
 * @height: the deadzone height
 *
 * Set the rectangular deadzone dimensions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_deadzone (LrgCameraSideOn *self,
                                     gfloat           width,
                                     gfloat           height);

/* ==========================================================================
 * Lookahead Configuration
 * ========================================================================== */

/**
 * lrg_camera_sideon_get_lookahead_distance:
 * @self: an #LrgCameraSideOn
 *
 * Get the horizontal lookahead distance.
 *
 * Returns: the lookahead distance in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_lookahead_distance (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_lookahead_distance:
 * @self: an #LrgCameraSideOn
 * @distance: the lookahead distance
 *
 * Set how far ahead of the player the camera looks
 * in the direction of movement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_lookahead_distance (LrgCameraSideOn *self,
                                               gfloat           distance);

/**
 * lrg_camera_sideon_get_lookahead_speed:
 * @self: an #LrgCameraSideOn
 *
 * Get the lookahead transition speed.
 *
 * Returns: the lookahead speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_lookahead_speed (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_lookahead_speed:
 * @self: an #LrgCameraSideOn
 * @speed: the lookahead transition speed
 *
 * Set how quickly the lookahead offset transitions when
 * the player changes direction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_lookahead_speed (LrgCameraSideOn *self,
                                            gfloat           speed);

/* ==========================================================================
 * Vertical Bias
 * ========================================================================== */

/**
 * lrg_camera_sideon_get_vertical_bias:
 * @self: an #LrgCameraSideOn
 *
 * Get the vertical bias offset.
 *
 * Returns: the vertical bias (-1.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_sideon_get_vertical_bias (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_vertical_bias:
 * @self: an #LrgCameraSideOn
 * @bias: the vertical bias (-1.0 to 1.0)
 *
 * Set the vertical bias. Positive values show more ground (player
 * appears higher on screen), negative values show more sky.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_vertical_bias (LrgCameraSideOn *self,
                                          gfloat           bias);

/* ==========================================================================
 * Target Following
 * ========================================================================== */

/**
 * lrg_camera_sideon_follow:
 * @self: an #LrgCameraSideOn
 * @target_x: the target X position in world space
 * @target_y: the target Y position in world space
 * @delta_time: the frame delta time in seconds
 *
 * Update the camera to follow a target position. Call this each frame.
 * The camera will smoothly track the target with lookahead and deadzone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_follow (LrgCameraSideOn *self,
                               gfloat           target_x,
                               gfloat           target_y,
                               gfloat           delta_time);

/* ==========================================================================
 * World Bounds
 * ========================================================================== */

/**
 * lrg_camera_sideon_get_bounds_enabled:
 * @self: an #LrgCameraSideOn
 *
 * Check if world bounds clamping is enabled.
 *
 * Returns: %TRUE if bounds are enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_sideon_get_bounds_enabled (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_set_bounds_enabled:
 * @self: an #LrgCameraSideOn
 * @enabled: whether to enable bounds clamping
 *
 * Enable or disable world bounds clamping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_bounds_enabled (LrgCameraSideOn *self,
                                           gboolean         enabled);

/**
 * lrg_camera_sideon_set_bounds:
 * @self: an #LrgCameraSideOn
 * @min_x: minimum X bound
 * @min_y: minimum Y bound
 * @max_x: maximum X bound
 * @max_y: maximum Y bound
 *
 * Set the world bounds for camera clamping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_set_bounds (LrgCameraSideOn *self,
                                   gfloat           min_x,
                                   gfloat           min_y,
                                   gfloat           max_x,
                                   gfloat           max_y);

/**
 * lrg_camera_sideon_get_bounds:
 * @self: an #LrgCameraSideOn
 * @out_min_x: (out) (optional): return location for min X
 * @out_min_y: (out) (optional): return location for min Y
 * @out_max_x: (out) (optional): return location for max X
 * @out_max_y: (out) (optional): return location for max Y
 *
 * Get the current world bounds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_get_bounds (LrgCameraSideOn *self,
                                   gfloat          *out_min_x,
                                   gfloat          *out_min_y,
                                   gfloat          *out_max_x,
                                   gfloat          *out_max_y);

/* ==========================================================================
 * Screen Shake
 * ========================================================================== */

/**
 * lrg_camera_sideon_shake:
 * @self: an #LrgCameraSideOn
 * @intensity: the shake intensity in pixels
 * @duration: the shake duration in seconds
 *
 * Start a screen shake effect.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_shake (LrgCameraSideOn *self,
                              gfloat           intensity,
                              gfloat           duration);

/**
 * lrg_camera_sideon_stop_shake:
 * @self: an #LrgCameraSideOn
 *
 * Immediately stop any active screen shake.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_sideon_stop_shake (LrgCameraSideOn *self);

/**
 * lrg_camera_sideon_is_shaking:
 * @self: an #LrgCameraSideOn
 *
 * Check if the camera is currently shaking.
 *
 * Returns: %TRUE if screen shake is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_camera_sideon_is_shaking (LrgCameraSideOn *self);

G_END_DECLS
