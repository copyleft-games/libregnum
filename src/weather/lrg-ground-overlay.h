/* lrg-ground-overlay.h - lying snow, tracks and cloud shadows on terrain
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A second, blended pass over terrain that is already drawn: it whitens
 * the flatter ground by an #LrgSnowCover's level (less where tracks
 * were walked, which also show as darker dents) and darkens it under an
 * #LrgCloudLayer's clouds (the same noise the clouds draw with, seen
 * along the sun).  It leaves the terrain's own shader and textures
 * alone, so it works over any lighting.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../graphics/lrg-terrain.h"
#include "lrg-cloud-layer.h"
#include "lrg-snow-cover.h"

G_BEGIN_DECLS

#define LRG_TYPE_GROUND_OVERLAY (lrg_ground_overlay_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGroundOverlay, lrg_ground_overlay, LRG, GROUND_OVERLAY, GObject)

/**
 * LRG_GROUND_OVERLAY_MAX_TRACKS:
 *
 * Tracks the shader draws (the newest of the snow cover's).
 */
#define LRG_GROUND_OVERLAY_MAX_TRACKS (64)

/**
 * lrg_ground_overlay_new:
 *
 * Returns: (transfer full): an overlay with no snow and no clouds
 */
LRG_AVAILABLE_IN_ALL
LrgGroundOverlay *  lrg_ground_overlay_new              (void);

/**
 * lrg_ground_overlay_set_snow_cover:
 * @self: an #LrgGroundOverlay
 * @snow: (nullable): the snow to draw, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_set_snow_cover   (LrgGroundOverlay *self,
                                                         LrgSnowCover     *snow);

/**
 * lrg_ground_overlay_set_cloud_layer:
 * @self: an #LrgGroundOverlay
 * @clouds: (nullable): the clouds whose shadows to draw, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_set_cloud_layer  (LrgGroundOverlay *self,
                                                         LrgCloudLayer    *clouds);

/**
 * lrg_ground_overlay_set_sun_direction:
 * @self: an #LrgGroundOverlay
 * @towards_sun: (array fixed-size=3): unit direction to the sun
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_set_sun_direction (LrgGroundOverlay *self,
                                                          const gfloat     *towards_sun);

/**
 * lrg_ground_overlay_set_snow_color:
 * @self: an #LrgGroundOverlay
 * @color: the colour of lying snow (lit by the scene's light level
 *   already, alpha scales the snow)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_set_snow_color   (LrgGroundOverlay *self,
                                                         const GrlColor   *color);

/**
 * lrg_ground_overlay_is_visible:
 * @self: an #LrgGroundOverlay
 *
 * Returns: %TRUE when there is snow or a cloud shadow to draw
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_ground_overlay_is_visible       (LrgGroundOverlay *self);

/**
 * lrg_ground_overlay_begin:
 * @self: an #LrgGroundOverlay
 * @terrain: the terrain about to be drawn again
 *
 * Puts the overlay shader on @terrain (its floating origin places the
 * tracks and the clouds), with alpha blending, depth writes off and a
 * depth test of less-or-equal, so the second draw lands exactly on the
 * first.  Draw the chunks (lrg_terrain_draw_chunk()), then call
 * lrg_ground_overlay_end().
 *
 * Returns: %FALSE when there is nothing to draw, no graphics context,
 *   or the shader failed (warned once); then do not draw and do not
 *   call end
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_ground_overlay_begin            (LrgGroundOverlay *self,
                                                         LrgTerrain       *terrain);

/**
 * lrg_ground_overlay_end:
 * @self: an #LrgGroundOverlay
 * @terrain: the terrain given to lrg_ground_overlay_begin()
 *
 * Takes the overlay shader off @terrain again (leaving it without a
 * shader, raylib's default, as before a lit draw sets one) and restores
 * raylib's depth and blend state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_end              (LrgGroundOverlay *self,
                                                         LrgTerrain       *terrain);

/**
 * lrg_ground_overlay_release_gpu:
 * @self: an #LrgGroundOverlay
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_release_gpu      (LrgGroundOverlay *self);

G_END_DECLS
