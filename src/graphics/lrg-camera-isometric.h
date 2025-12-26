/* lrg-camera-isometric.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Isometric camera implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-camera3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAMERA_ISOMETRIC (lrg_camera_isometric_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCameraIsometric, lrg_camera_isometric, LRG, CAMERA_ISOMETRIC, LrgCamera3D)

/**
 * LrgCameraIsometricClass:
 * @parent_class: Parent class
 *
 * Class for isometric cameras. Inherits from #LrgCamera3D and constrains
 * the camera to a fixed isometric angle with orthographic projection.
 *
 * Isometric cameras are ideal for tile-based strategy games, city builders,
 * and classic RPGs. The camera maintains a 45-degree horizontal rotation
 * and approximately 35.264-degree vertical tilt (arctan(1/sqrt(2))).
 */
struct _LrgCameraIsometricClass
{
	LrgCamera3DClass parent_class;

	/*< private >*/
	gpointer _reserved[4];
};

/**
 * lrg_camera_isometric_new:
 *
 * Create a new isometric camera with default settings.
 * The camera is automatically configured with:
 * - Orthographic projection
 * - Standard isometric angle (45 horizontal, 35.264 vertical)
 * - Default tile size 64x32 pixels
 * - Zoom level 1.0
 *
 * Returns: (transfer full): a new #LrgCameraIsometric
 */
LRG_AVAILABLE_IN_ALL
LrgCameraIsometric * lrg_camera_isometric_new (void);

/* ==========================================================================
 * Tile Size
 * ========================================================================== */

/**
 * lrg_camera_isometric_get_tile_width:
 * @self: an #LrgCameraIsometric
 *
 * Get the base tile width in pixels.
 *
 * Returns: the tile width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_isometric_get_tile_width (LrgCameraIsometric *self);

/**
 * lrg_camera_isometric_set_tile_width:
 * @self: an #LrgCameraIsometric
 * @width: the tile width in pixels (must be > 0)
 *
 * Set the base tile width in pixels.
 * Standard isometric tiles are typically 64 pixels wide.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_set_tile_width (LrgCameraIsometric *self,
                                          gfloat              width);

/**
 * lrg_camera_isometric_get_tile_height:
 * @self: an #LrgCameraIsometric
 *
 * Get the base tile height in pixels.
 *
 * Returns: the tile height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_isometric_get_tile_height (LrgCameraIsometric *self);

/**
 * lrg_camera_isometric_set_tile_height:
 * @self: an #LrgCameraIsometric
 * @height: the tile height in pixels (must be > 0)
 *
 * Set the base tile height in pixels.
 * Standard isometric tiles are typically half the width (32 pixels for 2:1 ratio).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_set_tile_height (LrgCameraIsometric *self,
                                           gfloat              height);

/* ==========================================================================
 * Height Scale
 * ========================================================================== */

/**
 * lrg_camera_isometric_get_height_scale:
 * @self: an #LrgCameraIsometric
 *
 * Get the vertical (Y-axis) visual scaling factor.
 *
 * Returns: the height scale
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_isometric_get_height_scale (LrgCameraIsometric *self);

/**
 * lrg_camera_isometric_set_height_scale:
 * @self: an #LrgCameraIsometric
 * @scale: the height scale (must be > 0, default is 0.5)
 *
 * Set the vertical scaling factor for height differences.
 * A value of 0.5 means a 1-unit height difference appears
 * as half a tile height on screen.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_set_height_scale (LrgCameraIsometric *self,
                                            gfloat              scale);

/* ==========================================================================
 * Zoom
 * ========================================================================== */

/**
 * lrg_camera_isometric_get_zoom:
 * @self: an #LrgCameraIsometric
 *
 * Get the current zoom level.
 *
 * Returns: the zoom level (1.0 is default, higher values zoom in)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_camera_isometric_get_zoom (LrgCameraIsometric *self);

/**
 * lrg_camera_isometric_set_zoom:
 * @self: an #LrgCameraIsometric
 * @zoom: the zoom level (must be > 0, 1.0 is default)
 *
 * Set the zoom level. Higher values zoom in (objects appear larger).
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_set_zoom (LrgCameraIsometric *self,
                                    gfloat              zoom);

/* ==========================================================================
 * Focus / Pan
 * ========================================================================== */

/**
 * lrg_camera_isometric_focus_on:
 * @self: an #LrgCameraIsometric
 * @world_x: the X position in world space
 * @world_y: the Y position in world space (height)
 * @world_z: the Z position in world space
 *
 * Focus the camera on a world position. The camera will maintain
 * the isometric angle while centering on this position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_focus_on (LrgCameraIsometric *self,
                                    gfloat              world_x,
                                    gfloat              world_y,
                                    gfloat              world_z);

/* ==========================================================================
 * Tile Coordinate Conversion
 * ========================================================================== */

/**
 * lrg_camera_isometric_world_to_tile:
 * @self: an #LrgCameraIsometric
 * @world_x: the X position in world space
 * @world_y: the Y position in world space (height, currently ignored for tile XY)
 * @world_z: the Z position in world space
 * @out_tile_x: (out): return location for the tile X coordinate
 * @out_tile_y: (out): return location for the tile Y coordinate
 *
 * Convert a world position to tile coordinates.
 * This uses the configured tile dimensions to calculate which tile
 * contains the given world position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_world_to_tile (LrgCameraIsometric *self,
                                         gfloat              world_x,
                                         gfloat              world_y,
                                         gfloat              world_z,
                                         gint               *out_tile_x,
                                         gint               *out_tile_y);

/**
 * lrg_camera_isometric_tile_to_world:
 * @self: an #LrgCameraIsometric
 * @tile_x: the tile X coordinate
 * @tile_y: the tile Y coordinate
 * @out_world_x: (out): return location for the world X position
 * @out_world_z: (out): return location for the world Z position
 *
 * Convert tile coordinates to world position.
 * Returns the center of the specified tile in world space.
 * The Y (height) value should be set separately based on terrain.
 */
LRG_AVAILABLE_IN_ALL
void lrg_camera_isometric_tile_to_world (LrgCameraIsometric *self,
                                         gint                tile_x,
                                         gint                tile_y,
                                         gfloat             *out_world_x,
                                         gfloat             *out_world_z);

G_END_DECLS
