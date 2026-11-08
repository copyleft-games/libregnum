/* lrg-ssao.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSsao: screen-space ambient occlusion from an LrgDepthPrepass
 * (hemisphere kernel, rotation noise, depth-aware blur), sampled by the
 * lit shader through lrg_ssao() to darken indirect light.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-depth-prepass.h"

G_BEGIN_DECLS

/**
 * LRG_SSAO_MAX_KERNEL:
 *
 * Most hemisphere samples per pixel (lrg_ssao_set_kernel_size()).
 */
#define LRG_SSAO_MAX_KERNEL (64)

/**
 * LRG_SSAO_NOISE_SIZE:
 *
 * Side of the tiled rotation-noise texture in pixels (the blur averages
 * a block of this size).
 */
#define LRG_SSAO_NOISE_SIZE (4)

#define LRG_TYPE_SSAO (lrg_ssao_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSsao, lrg_ssao, LRG, SSAO, GObject)

/**
 * lrg_ssao_new:
 *
 * Creates an SSAO pass with radius 0.5, bias 0.025, intensity 1, power
 * 1.5, 16 samples, half resolution and blur on, seed 1.
 *
 * Returns: (transfer full): a new #LrgSsao
 */
LRG_AVAILABLE_IN_ALL
LrgSsao * lrg_ssao_new (void);

/**
 * lrg_ssao_set_radius:
 * @self: an #LrgSsao
 * @radius: sampling radius in world units (> 0)
 *
 * How far around a point occluders count.  Small radii pick up contact
 * shadows and creases; large ones darken whole corners and rooms.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_radius (LrgSsao *self,
                          gfloat   radius);

/**
 * lrg_ssao_get_radius:
 * @self: an #LrgSsao
 *
 * Returns: the sampling radius in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_ssao_get_radius (LrgSsao *self);

/**
 * lrg_ssao_set_bias:
 * @self: an #LrgSsao
 * @bias: depth bias in world units (>= 0)
 *
 * A sample is occluded only when the stored surface is this much in
 * front of it: removes self-shadowing acne on flat surfaces; too large
 * and thin creases stop darkening.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_bias (LrgSsao *self,
                        gfloat   bias);

/**
 * lrg_ssao_get_bias:
 * @self: an #LrgSsao
 *
 * Returns: the depth bias
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_ssao_get_bias (LrgSsao *self);

/**
 * lrg_ssao_set_intensity:
 * @self: an #LrgSsao
 * @intensity: occlusion scale (>= 0; 0 turns the effect off, above 1
 *   exaggerates it)
 *
 * The result is clamp (1 - intensity * occlusion, 0, 1) ^ power.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_intensity (LrgSsao *self,
                             gfloat   intensity);

/**
 * lrg_ssao_get_intensity:
 * @self: an #LrgSsao
 *
 * Returns: the occlusion scale
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_ssao_get_intensity (LrgSsao *self);

/**
 * lrg_ssao_set_power:
 * @self: an #LrgSsao
 * @power: contrast exponent (> 0): above 1 deepens the darkest creases
 *   while leaving open surfaces bright
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_power (LrgSsao *self,
                         gfloat   power);

/**
 * lrg_ssao_get_power:
 * @self: an #LrgSsao
 *
 * Returns: the contrast exponent
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_ssao_get_power (LrgSsao *self);

/**
 * lrg_ssao_set_kernel_size:
 * @self: an #LrgSsao
 * @samples: hemisphere samples per pixel, 4 .. %LRG_SSAO_MAX_KERNEL
 *
 * More samples cost linearly more and reduce noise; 8 to 16 with the
 * blur on is the usual range.  Rebuilds the kernel.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_kernel_size (LrgSsao *self,
                               guint    samples);

/**
 * lrg_ssao_get_kernel_size:
 * @self: an #LrgSsao
 *
 * Returns: the samples per pixel
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ssao_get_kernel_size (LrgSsao *self);

/**
 * lrg_ssao_set_seed:
 * @self: an #LrgSsao
 * @seed: seed of the kernel and noise generator
 *
 * The kernel and the noise are a pure function of the seed and the
 * kernel size (the same on every machine).  Rebuilds both.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_seed (LrgSsao *self,
                        guint32  seed);

/**
 * lrg_ssao_get_seed:
 * @self: an #LrgSsao
 *
 * Returns: the generator seed
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_ssao_get_seed (LrgSsao *self);

/**
 * lrg_ssao_set_half_resolution:
 * @self: an #LrgSsao
 * @half: compute at half the prepass width and height
 *
 * Half resolution quarters the cost; the lit shader upsamples
 * bilinearly, which softens the occlusion slightly at silhouettes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_half_resolution (LrgSsao  *self,
                                   gboolean  half);

/**
 * lrg_ssao_get_half_resolution:
 * @self: an #LrgSsao
 *
 * Returns: whether the pass runs at half resolution
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ssao_get_half_resolution (LrgSsao *self);

/**
 * lrg_ssao_set_blur:
 * @self: an #LrgSsao
 * @blur: run the depth-aware 4 x 4 blur that removes the noise pattern
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_blur (LrgSsao  *self,
                        gboolean  blur);

/**
 * lrg_ssao_get_blur:
 * @self: an #LrgSsao
 *
 * Returns: whether the blur runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ssao_get_blur (LrgSsao *self);

/**
 * lrg_ssao_get_kernel_sample:
 * @self: an #LrgSsao
 * @index: 0 .. kernel size - 1
 * @out_xyz: (out caller-allocates) (array fixed-size=3): the sample in
 *   tangent space (+Z along the surface normal, length <= 1)
 *
 * One hemisphere sample.  Samples are denser near the centre (the
 * length grows with the index) and always have z > 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_get_kernel_sample (LrgSsao *self,
                                 guint    index,
                                 gfloat  *out_xyz);

/**
 * lrg_ssao_get_noise_sample:
 * @self: an #LrgSsao
 * @index: 0 .. %LRG_SSAO_NOISE_SIZE squared - 1 (row-major)
 * @out_xy: (out caller-allocates) (array fixed-size=2): a unit rotation
 *   vector in the tangent plane
 *
 * One texel of the tiled rotation noise.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_get_noise_sample (LrgSsao *self,
                                guint    index,
                                gfloat  *out_xy);

/**
 * lrg_ssao_render:
 * @self: an #LrgSsao
 * @prepass: a valid prepass of this frame (lrg_depth_prepass_is_valid())
 *
 * Runs the occlusion pass (and the blur) from @prepass's depth, normals
 * (when lrg_depth_prepass_get_normals(), else normals rebuilt from
 * depth) and projection.  Call after the prepass and before the lit
 * pass, outside any texture mode.
 *
 * Returns: %TRUE when the result texture was produced
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ssao_render (LrgSsao         *self,
                          LrgDepthPrepass *prepass);

/**
 * lrg_ssao_has_result:
 * @self: an #LrgSsao
 *
 * Returns: %TRUE once lrg_ssao_render() succeeded (the binder then
 *   activates lrg_ssao() in the lit shader)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ssao_has_result (LrgSsao *self);

/**
 * lrg_ssao_get_texture_id:
 * @self: an #LrgSsao
 *
 * The result: ambient visibility in the red channel (1 = open, 0 =
 * fully occluded), linearly filtered.
 *
 * Returns: the GL texture id, 0 before the first lrg_ssao_render()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_ssao_get_texture_id (LrgSsao *self);

/**
 * lrg_ssao_get_width:
 * @self: an #LrgSsao
 *
 * Returns: the result texture width (0 before the first render)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ssao_get_width (LrgSsao *self);

/**
 * lrg_ssao_get_height:
 * @self: an #LrgSsao
 *
 * Returns: the result texture height (0 before the first render)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ssao_get_height (LrgSsao *self);

/**
 * lrg_ssao_set_viewport:
 * @self: an #LrgSsao
 * @x: left edge of the lit pass's viewport in framebuffer pixels
 * @y: bottom edge (GL convention, from the framebuffer's bottom)
 * @width: viewport width in pixels, or 0 for automatic
 * @height: viewport height in pixels, or 0 for automatic
 *
 * Where the lit pass draws, so lrg_ssao() maps gl_FragCoord onto the
 * result.  Automatic (the default) is (0, 0, prepass width, prepass
 * height): right when the lit pass draws to a target the prepass's size.
 * Set it when the lit pass draws into a sub-rectangle of the window
 * (editor viewports).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_set_viewport (LrgSsao *self,
                            gint     x,
                            gint     y,
                            gint     width,
                            gint     height);

/**
 * lrg_ssao_bind:
 * @self: an #LrgSsao
 * @shader_id: a GL program that contains lrg_ssao_get_glsl()
 * @first_unit: first free texture unit
 *
 * Public form of the lit-shader binder: binds the result texture on
 * @first_unit and turns lrg_ssao() on (off when there is no result yet).
 * Use it for your own shaders; the light set calls the private binder
 * itself.
 *
 * Returns: the next free texture unit
 */
LRG_AVAILABLE_IN_ALL
gint lrg_ssao_bind (LrgSsao *self,
                    guint    shader_id,
                    gint     first_unit);

/**
 * lrg_ssao_bind_inactive:
 * @shader_id: a GL program that contains lrg_ssao_get_glsl()
 *
 * Turns lrg_ssao() off in @shader_id (it then returns 1.0).
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_bind_inactive (guint shader_id);

/**
 * lrg_ssao_get_glsl:
 *
 * The GLSL 330 hook pasted into the lit shader: its uniforms and
 * `float lrg_ssao()`, the ambient visibility at gl_FragCoord (1.0 =
 * unoccluded, and always 1.0 while inactive).
 *
 * Returns: (transfer none): the GLSL source
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_ssao_get_glsl (void);

/**
 * lrg_ssao_release:
 * @self: an #LrgSsao
 *
 * Frees the GPU objects (call before CloseWindow()); the next
 * lrg_ssao_render() makes new ones.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ssao_release (LrgSsao *self);

G_END_DECLS
