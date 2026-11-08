/* lrg-depth-prepass.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDepthPrepass: an offscreen depth (+ view-space normal) target that
 * a caller fills by drawing its opaque geometry once per frame; screen
 * space effects (LrgSsao, volumetric fog) read its textures.
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

#define LRG_TYPE_DEPTH_PREPASS (lrg_depth_prepass_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDepthPrepass, lrg_depth_prepass, LRG, DEPTH_PREPASS, GObject)

/**
 * LrgDepthPrepassDrawFunc:
 * @prepass: the #LrgDepthPrepass being recorded
 * @shader: (nullable): the prepass shader (writes view-space normals and
 *   honours the alpha cutoff); %NULL when it could not be compiled
 * @user_data: (closure): data passed to lrg_depth_prepass_record()
 *
 * Draws the opaque geometry of the frame into the prepass.  The prepass
 * target and the camera are already bound.  Depth is written whatever
 * shader draws; the normal texture is only meaningful for geometry drawn
 * with @shader (lrg_depth_prepass_draw_model() does that for a model).
 */
typedef void (*LrgDepthPrepassDrawFunc) (LrgDepthPrepass *prepass,
                                         GrlShader       *shader,
                                         gpointer         user_data);

/**
 * lrg_depth_prepass_new:
 * @width: target width in pixels (>= 1)
 * @height: target height in pixels (>= 1)
 *
 * Creates a prepass of @width x @height.  GPU objects are made on the
 * first lrg_depth_prepass_begin(), so this works without a window.
 *
 * Returns: (transfer full): a new #LrgDepthPrepass
 */
LRG_AVAILABLE_IN_ALL
LrgDepthPrepass * lrg_depth_prepass_new (gint width,
                                         gint height);

/**
 * lrg_depth_prepass_resize:
 * @self: an #LrgDepthPrepass
 * @width: new width in pixels (>= 1)
 * @height: new height in pixels (>= 1)
 *
 * Changes the target size.  The textures are recreated at the next
 * lrg_depth_prepass_begin() and the prepass is invalid
 * (lrg_depth_prepass_is_valid()) until then.  Same size: no-op.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_resize (LrgDepthPrepass *self,
                               gint             width,
                               gint             height);

/**
 * lrg_depth_prepass_get_width:
 * @self: an #LrgDepthPrepass
 *
 * Returns: the target width in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_depth_prepass_get_width (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_get_height:
 * @self: an #LrgDepthPrepass
 *
 * Returns: the target height in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_depth_prepass_get_height (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_set_normals:
 * @self: an #LrgDepthPrepass
 * @normals: whether the colour texture holds view-space normals
 *
 * With normals on (the default) consumers such as #LrgSsao read the
 * normal texture; turn it off when the geometry is not drawn with the
 * prepass shader (they then rebuild normals from depth).
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_set_normals (LrgDepthPrepass *self,
                                    gboolean         normals);

/**
 * lrg_depth_prepass_get_normals:
 * @self: an #LrgDepthPrepass
 *
 * Returns: whether the normal texture is valid for consumers
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_depth_prepass_get_normals (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_set_alpha_cutoff:
 * @self: an #LrgDepthPrepass
 * @cutoff: 0 .. 1; fragments whose albedo alpha is below it write no
 *   depth (0: no test)
 *
 * The prepass shader's alpha test, for foliage and fences.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_set_alpha_cutoff (LrgDepthPrepass *self,
                                         gfloat           cutoff);

/**
 * lrg_depth_prepass_get_alpha_cutoff:
 * @self: an #LrgDepthPrepass
 *
 * Returns: the alpha cutoff
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_depth_prepass_get_alpha_cutoff (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_get_shader:
 * @self: an #LrgDepthPrepass
 *
 * The prepass shader (GLSL 330, raylib attribute and matrix names): it
 * writes the view-space normal encoded as n * 0.5 + 0.5 and discards
 * fragments below the alpha cutoff.  Compiled on first use.
 *
 * Returns: (transfer none) (nullable): the shader, or %NULL without a
 *   GL context
 */
LRG_AVAILABLE_IN_ALL
GrlShader * lrg_depth_prepass_get_shader (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_begin:
 * @self: an #LrgDepthPrepass
 * @camera: the camera the frame is drawn from
 *
 * Binds the prepass target (creating it if needed), clears depth to 1
 * and the normals to (0, 0, 1), begins @camera and records its
 * projection and view matrices.  Draw opaque geometry, then call
 * lrg_depth_prepass_end().  Must not be called inside another texture
 * mode (raylib render targets do not nest).
 *
 * Returns: %TRUE when the target is bound; %FALSE without a GL context
 *   or when the framebuffer cannot be made (then do not call end)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_depth_prepass_begin (LrgDepthPrepass *self,
                                  LrgCamera       *camera);

/**
 * lrg_depth_prepass_end:
 * @self: an #LrgDepthPrepass
 *
 * Ends the camera and the target begun by lrg_depth_prepass_begin(); the
 * prepass is valid afterwards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_end (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_record:
 * @self: an #LrgDepthPrepass
 * @camera: the camera the frame is drawn from
 * @func: (scope call): draws the opaque geometry
 * @user_data: data for @func
 *
 * lrg_depth_prepass_begin(), @func, lrg_depth_prepass_end().
 *
 * Returns: %TRUE when the prepass was recorded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_depth_prepass_record (LrgDepthPrepass         *self,
                                   LrgCamera               *camera,
                                   LrgDepthPrepassDrawFunc  func,
                                   gpointer                 user_data);

/**
 * lrg_depth_prepass_draw_model:
 * @self: an #LrgDepthPrepass
 * @model: a model
 * @transform: (nullable) (array fixed-size=16): column-major world
 *   matrix applied after the model's own transform, or %NULL for the
 *   identity
 *
 * Draws every mesh of @model with the prepass shader (its materials keep
 * their albedo map and colour, so the alpha cutoff works).  Call between
 * lrg_depth_prepass_begin() and lrg_depth_prepass_end().
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_draw_model (LrgDepthPrepass *self,
                                   GrlModel        *model,
                                   const gfloat    *transform);

/**
 * lrg_depth_prepass_is_valid:
 * @self: an #LrgDepthPrepass
 *
 * Returns: %TRUE once a recording finished at the current size
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_depth_prepass_is_valid (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_get_depth_texture_id:
 * @self: an #LrgDepthPrepass
 *
 * The depth texture (window depth in 0 .. 1, point sampled, clamped).
 *
 * Returns: the GL texture id, 0 before the first lrg_depth_prepass_begin()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_depth_prepass_get_depth_texture_id (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_get_normal_texture_id:
 * @self: an #LrgDepthPrepass
 *
 * The RGBA8 colour texture holding view-space normals (n * 0.5 + 0.5).
 *
 * Returns: the GL texture id, 0 before the first lrg_depth_prepass_begin()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_depth_prepass_get_normal_texture_id (LrgDepthPrepass *self);

/**
 * lrg_depth_prepass_get_projection:
 * @self: an #LrgDepthPrepass
 * @out_matrix: (out caller-allocates) (array fixed-size=16): the
 *   column-major projection matrix of the last recording
 *
 * The identity before the first recording.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_get_projection (LrgDepthPrepass *self,
                                       gfloat          *out_matrix);

/**
 * lrg_depth_prepass_get_view:
 * @self: an #LrgDepthPrepass
 * @out_matrix: (out caller-allocates) (array fixed-size=16): the
 *   column-major view matrix of the last recording
 *
 * The identity before the first recording.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_get_view (LrgDepthPrepass *self,
                                 gfloat          *out_matrix);

/**
 * lrg_depth_prepass_linearize_depth:
 * @self: an #LrgDepthPrepass
 * @depth: a depth texture value (0 .. 1)
 *
 * Converts a depth value to the distance in front of the camera along
 * its view axis, with the projection of the last recording (perspective
 * or orthographic).
 *
 * Returns: the view-space distance (positive in front of the camera)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_depth_prepass_linearize_depth (LrgDepthPrepass *self,
                                          gfloat           depth);

/**
 * lrg_depth_prepass_release:
 * @self: an #LrgDepthPrepass
 *
 * Frees the GPU objects (call before CloseWindow()); the next
 * lrg_depth_prepass_begin() makes new ones.
 */
LRG_AVAILABLE_IN_ALL
void lrg_depth_prepass_release (LrgDepthPrepass *self);

G_END_DECLS
