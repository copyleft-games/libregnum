/* lrg-volumetric-fog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVolumetricFog - screen-space ray-marched height fog with sun shafts.
 *
 * A post-scene pass: for every pixel of a (by default half-resolution)
 * target it marches the view ray from the eye to the depth buffer's
 * surface (or to the march distance for the sky) through an exponential
 * height fog, adding the directional light of an #LrgLightSet where its
 * cascaded shadow map says the point is lit (so shafts form behind
 * casters), scattered by a Henyey-Greenstein phase function, plus an
 * isotropic ambient term, and keeps the transmittance.  A second pass
 * upsamples the result with depth-aware (bilateral) weights and
 * composites it over the current framebuffer:
 * colour = colour x transmittance + in-scattered light.
 *
 * Unlike the light set's per-fragment "air" (lrg_light_set_set_air()),
 * the cost does not depend on overdraw and the sky gets fog too; use
 * one or the other.  The CPU mirror lrg_volumetric_fog_sample() is
 * headless; rendering needs GL 3.3.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../graphics/lrg-light-set.h"
#include "../graphics/lrg-camera3d.h"
#include "lrg-environment.h"

G_BEGIN_DECLS

#define LRG_TYPE_VOLUMETRIC_FOG (lrg_volumetric_fog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVolumetricFog, lrg_volumetric_fog, LRG, VOLUMETRIC_FOG, GObject)

/**
 * lrg_volumetric_fog_new:
 *
 * Creates a fog with density 0.02, anisotropy 0.6, 32 steps, a march
 * distance of 100, intensity 1, a white tint, a black ambient, base
 * height 0 with falloff 0.1, and half-resolution marching.
 *
 * Returns: (transfer full): a new #LrgVolumetricFog
 */
LRG_AVAILABLE_IN_ALL
LrgVolumetricFog *
lrg_volumetric_fog_new (void);

/**
 * lrg_volumetric_fog_set_density:
 * @self: an #LrgVolumetricFog
 * @density: extinction per world unit at (and below) the base height
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_density (LrgVolumetricFog *self,
                                gfloat            density);

/**
 * lrg_volumetric_fog_get_density:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the density
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_get_density (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_anisotropy:
 * @self: an #LrgVolumetricFog
 * @g: Henyey-Greenstein g, -0.95 .. 0.95 (> 0: brightest looking toward
 *   the sun)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_anisotropy (LrgVolumetricFog *self,
                                   gfloat            g);

/**
 * lrg_volumetric_fog_get_anisotropy:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the phase function's g
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_get_anisotropy (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_steps:
 * @self: an #LrgVolumetricFog
 * @steps: march steps per pixel, 4 .. 128 (jittered per pixel)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_steps (LrgVolumetricFog *self,
                              guint             steps);

/**
 * lrg_volumetric_fog_get_steps:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the march steps
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_volumetric_fog_get_steps (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_max_distance:
 * @self: an #LrgVolumetricFog
 * @distance: how far along the view ray the fog is marched (> 0); also
 *   the length marched for sky pixels
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_max_distance (LrgVolumetricFog *self,
                                     gfloat            distance);

/**
 * lrg_volumetric_fog_get_max_distance:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the march distance
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_get_max_distance (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_intensity:
 * @self: an #LrgVolumetricFog
 * @intensity: multiplier of the in-scattered light (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_intensity (LrgVolumetricFog *self,
                                  gfloat            intensity);

/**
 * lrg_volumetric_fog_get_intensity:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_get_intensity (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_height:
 * @self: an #LrgVolumetricFog
 * @base_height: world height at and below which the density is full
 * @falloff: thinning per world unit above it: density x
 *   exp (-@falloff x (y - @base_height)) (>= 0; 0 = uniform fog)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_height (LrgVolumetricFog *self,
                               gfloat            base_height,
                               gfloat            falloff);

/**
 * lrg_volumetric_fog_set_tint:
 * @self: an #LrgVolumetricFog
 * @rgb: (array fixed-size=3): linear colour of the medium (multiplies the
 *   scattered light)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_tint (LrgVolumetricFog *self,
                             const gfloat     *rgb);

/**
 * lrg_volumetric_fog_set_ambient:
 * @self: an #LrgVolumetricFog
 * @rgb: (array fixed-size=3): linear light reaching the medium from
 *   every direction (scattered isotropically)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_ambient (LrgVolumetricFog *self,
                                const gfloat     *rgb);

/**
 * lrg_volumetric_fog_set_resolution_scale:
 * @self: an #LrgVolumetricFog
 * @scale: march target size relative to the screen, 0.25 .. 1
 *   (default 0.5: half resolution, bilateral upsample)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_set_resolution_scale (LrgVolumetricFog *self,
                                         gfloat            scale);

/**
 * lrg_volumetric_fog_get_resolution_scale:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the march target scale
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_get_resolution_scale (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_set_from_environment:
 * @self: an #LrgVolumetricFog
 * @environment: an environment
 *
 * Copies the environment's volumetric parameters
 * (lrg_environment_get_volumetric()), its fog height shape, fog colour
 * (as the tint, linearized) and ambient light (linearized, times its
 * intensity and exposure).
 *
 * Returns: whether the environment enables the volumetric fog
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_volumetric_fog_set_from_environment (LrgVolumetricFog *self,
                                         LrgEnvironment   *environment);

/**
 * lrg_volumetric_fog_sample:
 * @self: an #LrgVolumetricFog
 * @from: (array fixed-size=3): the eye
 * @to: (array fixed-size=3): the end of the ray (a surface point)
 * @sun_direction: (array fixed-size=3) (nullable): direction the sun
 *   light travels, %NULL for no sun
 * @sun_rgb: (array fixed-size=3) (nullable): linear sun colour
 * @out_rgb: (out caller-allocates) (array fixed-size=3): in-scattered light
 *
 * The shader's march on the CPU with every point lit (no shadow
 * lookups) and the steps at their middles (no jitter); the march is cut
 * at the maximum distance.
 *
 * Returns: the transmittance from @from to @to
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_volumetric_fog_sample (LrgVolumetricFog *self,
                           const gfloat     *from,
                           const gfloat     *to,
                           const gfloat     *sun_direction,
                           const gfloat     *sun_rgb,
                           gfloat           *out_rgb);

/**
 * lrg_volumetric_fog_render:
 * @self: an #LrgVolumetricFog
 * @depth_texture: GL id of the scene's depth texture (window-space depth
 *   0 .. 1 of @projection, e.g. from #LrgDepthPrepass)
 * @width: width of the target being composited onto, in pixels
 * @height: its height
 * @view: (array fixed-size=16): the scene's view matrix (column-major,
 *   draw space)
 * @projection: (array fixed-size=16): the scene's projection matrix
 * @lights: (nullable): the light set: its directional light and the
 *   cascades of its shadow renderer light the fog; %NULL for ambient only
 * @error: return location for a #GError
 *
 * Marches into the internal target, then composites onto the bound
 * framebuffer with the viewport set to @width x @height (call after the
 * scene, outside BeginMode3D(), before transparent objects or post
 * effects that want the fog).  The internal target follows the size.
 *
 * Returns: %TRUE on success, else %FALSE with an
 *   %LRG_ENVIRONMENT_ERROR_GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_volumetric_fog_render (LrgVolumetricFog  *self,
                           guint              depth_texture,
                           gint               width,
                           gint               height,
                           const gfloat      *view,
                           const gfloat      *projection,
                           LrgLightSet       *lights,
                           GError           **error);

/**
 * lrg_volumetric_fog_render_camera:
 * @self: an #LrgVolumetricFog
 * @depth_texture: GL id of the scene's depth texture
 * @width: target width
 * @height: target height
 * @camera: the camera the scene was drawn with (its render pose, field of
 *   view, the aspect @width / @height and rlgl's cull distances, as
 *   BeginMode3D() builds them)
 * @lights: (nullable): the light set
 * @error: return location for a #GError
 *
 * lrg_volumetric_fog_render() with the matrices of @camera.  The light
 * set's floating origin is subtracted from the eye.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_volumetric_fog_render_camera (LrgVolumetricFog  *self,
                                  guint              depth_texture,
                                  gint               width,
                                  gint               height,
                                  LrgCamera3D       *camera,
                                  LrgLightSet       *lights,
                                  GError           **error);

/**
 * lrg_volumetric_fog_get_scatter_texture_id:
 * @self: an #LrgVolumetricFog
 *
 * Returns: the GL id of the march target (RGBA16F: in-scattered light,
 *   transmittance in alpha), 0 before the first render
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_volumetric_fog_get_scatter_texture_id (LrgVolumetricFog *self);

/**
 * lrg_volumetric_fog_release_gpu:
 * @self: an #LrgVolumetricFog
 *
 * Frees the shaders and the target (call before CloseWindow()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_volumetric_fog_release_gpu (LrgVolumetricFog *self);

G_END_DECLS
