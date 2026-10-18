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
 * lrg_ground_overlay_begin_objects:
 * @self: an #LrgGroundOverlay
 * @origin: (array fixed-size=3): the world position of the draw space's
 *   origin, as lrg_terrain_get_origin() gives it for terrain drawn in the
 *   same space
 *
 * The overlay over things that are not terrain: buildings, models,
 * props, drawn a second time with the returned shader (for instance
 * lrg_static_batch_set_shader()) after the first, lit draw.  Sets the
 * uniforms (the clouds and the tracks placed by @origin), alpha
 * blending and depth writes off; the depth test stays less-or-equal.
 * Surfaces whose normal faces up take the snow, every surface the cloud
 * shadows.  A draw under a matrix of its own (a scene translated to its
 * place, a rotated object) calls lrg_ground_overlay_set_model_matrix()
 * first.  Finish with lrg_ground_overlay_end_objects().
 *
 * Returns: (transfer none) (nullable): the overlay shader, owned by
 *   @self; %NULL when there is nothing to draw, no graphics context, or
 *   the shader failed (warned once); then do not draw and do not call
 *   end
 */
LRG_AVAILABLE_IN_ALL
GrlShader *         lrg_ground_overlay_begin_objects    (LrgGroundOverlay *self,
                                                         const gdouble    *origin);

/**
 * lrg_ground_overlay_set_model_matrix:
 * @self: an #LrgGroundOverlay
 * @matrix: (array fixed-size=16) (nullable): the transform the next
 *   draws sit under (what the rlgl matrix stack holds), 16 floats in
 *   OpenGL's column-major order; %NULL for the identity
 *
 * Tells the overlay shader where the surfaces of the next draws really
 * are: rlgl's matrix stack reaches the vertex transform but not the
 * shader's model matrix, so the world position (the clouds, the snow's
 * patches and tracks) and the normal would be wrong without it.  Does
 * nothing before lrg_ground_overlay_begin_objects().
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_set_model_matrix (LrgGroundOverlay *self,
                                                         const gfloat     *matrix);

/**
 * lrg_ground_overlay_end_objects:
 * @self: an #LrgGroundOverlay
 *
 * Ends lrg_ground_overlay_begin_objects(): flushes and restores raylib's
 * depth writes.  Take the shader off what you set it on first.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_end_objects      (LrgGroundOverlay *self);

/**
 * lrg_ground_overlay_release_gpu:
 * @self: an #LrgGroundOverlay
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ground_overlay_release_gpu      (LrgGroundOverlay *self);

G_END_DECLS
