/* lrg-cloud-layer.h - a drifting layer of volumetric-looking clouds
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A cloud deck at an altitude, drawn as stacked translucent slices
 * whose density is fractal noise drifting with the wind, lit from the
 * sun (thin edges bright, the far side of thick clouds shaded).  The
 * same density is sampled on the CPU for cloud shadows
 * (lrg_cloud_layer_sample_shadow()) and by #LrgGroundOverlay, which
 * darkens the terrain under the clouds.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CLOUD_LAYER (lrg_cloud_layer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCloudLayer, lrg_cloud_layer, LRG, CLOUD_LAYER, GObject)

/**
 * LRG_CLOUD_LAYER_MAX_SLICES:
 *
 * Most slices lrg_cloud_layer_set_slices() accepts.
 */
#define LRG_CLOUD_LAYER_MAX_SLICES (8)

/**
 * lrg_cloud_layer_new:
 * @seed: where in the noise the clouds start (different seeds, different
 *   skies)
 *
 * A clear layer (coverage 0) 1200 m up, 300 m thick, three slices,
 * 1800 m noise cells, no wind, 50% shadows.
 *
 * Returns: (transfer full): a new #LrgCloudLayer
 */
LRG_AVAILABLE_IN_ALL
LrgCloudLayer *     lrg_cloud_layer_new                 (guint32        seed);

/**
 * lrg_cloud_layer_set_coverage:
 * @self: an #LrgCloudLayer
 * @coverage: 0 (clear) .. 1 (overcast); clamped
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_coverage        (LrgCloudLayer *self,
                                                         gfloat         coverage);

/**
 * lrg_cloud_layer_get_coverage:
 * @self: an #LrgCloudLayer
 *
 * Returns: the coverage
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_get_coverage        (LrgCloudLayer *self);

/**
 * lrg_cloud_layer_set_altitude:
 * @self: an #LrgCloudLayer
 * @altitude: height of the cloud base above the ground plane, metres
 *   (> 0)
 * @thickness: height of the deck, metres (>= 0)
 *
 * Invalid values are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_altitude        (LrgCloudLayer *self,
                                                         gfloat         altitude,
                                                         gfloat         thickness);

/**
 * lrg_cloud_layer_get_altitude:
 * @self: an #LrgCloudLayer
 *
 * Returns: the cloud base altitude, metres
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_get_altitude        (LrgCloudLayer *self);

/**
 * lrg_cloud_layer_set_scale:
 * @self: an #LrgCloudLayer
 * @metres: size of one noise cell (> 0; larger, bigger clouds)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_scale           (LrgCloudLayer *self,
                                                         gfloat         metres);

/**
 * lrg_cloud_layer_get_scale:
 * @self: an #LrgCloudLayer
 *
 * Returns: the size of one noise cell, metres
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_get_scale           (LrgCloudLayer *self);

/**
 * lrg_cloud_layer_set_slices:
 * @self: an #LrgCloudLayer
 * @slices: slices drawn through the deck, 1 .. %LRG_CLOUD_LAYER_MAX_SLICES
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_slices          (LrgCloudLayer *self,
                                                         guint          slices);

/**
 * lrg_cloud_layer_set_wind:
 * @self: an #LrgCloudLayer
 * @vx: drift along +X, metres per second
 * @vz: drift along +Z
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_wind            (LrgCloudLayer *self,
                                                         gfloat         vx,
                                                         gfloat         vz);

/**
 * lrg_cloud_layer_get_wind:
 * @self: an #LrgCloudLayer
 * @vx: (out) (optional): drift along +X
 * @vz: (out) (optional): drift along +Z
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_get_wind            (LrgCloudLayer *self,
                                                         gfloat        *vx,
                                                         gfloat        *vz);

/**
 * lrg_cloud_layer_advance:
 * @self: an #LrgCloudLayer
 * @seconds: time that passed (game seconds; a time-lapse passes many)
 *
 * Moves the clouds with the wind.  Negative and non-finite values are
 * ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_advance             (LrgCloudLayer *self,
                                                         gdouble        seconds);

/**
 * lrg_cloud_layer_get_offset:
 * @self: an #LrgCloudLayer
 * @x: (out) (optional): metres the clouds have drifted along +X
 * @z: (out) (optional): along +Z
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_get_offset          (LrgCloudLayer *self,
                                                         gdouble       *x,
                                                         gdouble       *z);

/**
 * lrg_cloud_layer_set_offset:
 * @self: an #LrgCloudLayer
 * @x: the drift along +X (a saved lrg_cloud_layer_get_offset())
 * @z: the drift along +Z
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_offset          (LrgCloudLayer *self,
                                                         gdouble        x,
                                                         gdouble        z);

/**
 * lrg_cloud_layer_sample_density:
 * @self: an #LrgCloudLayer
 * @x: absolute world x, metres
 * @z: absolute world z
 *
 * The cloud above (x, z) now: the noise thresholded by the coverage.
 * Exactly what the shaders compute (to float precision).
 *
 * Returns: 0 (clear) .. 1 (solid cloud)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_sample_density      (LrgCloudLayer *self,
                                                         gdouble        x,
                                                         gdouble        z);

/**
 * lrg_cloud_layer_set_shadow_strength:
 * @self: an #LrgCloudLayer
 * @strength: how much a solid cloud darkens sunlit ground, 0 .. 1
 *   (default 0.5)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_shadow_strength (LrgCloudLayer *self,
                                                         gfloat         strength);

/**
 * lrg_cloud_layer_get_shadow_strength:
 * @self: an #LrgCloudLayer
 *
 * Returns: the shadow strength
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_get_shadow_strength (LrgCloudLayer *self);

/**
 * lrg_cloud_layer_sample_shadow:
 * @self: an #LrgCloudLayer
 * @x: absolute world x of a point near the ground, metres
 * @y: its height above the ground plane
 * @z: absolute world z
 * @towards_sun: (array fixed-size=3): unit direction to the sun
 *
 * The cloud shadow on that point: the density where the ray to the sun
 * crosses the cloud base, times the shadow strength, faded out as the
 * sun nears the horizon (no shadows at night).  A shadow-mapping pass
 * can project the same function; #LrgGroundOverlay draws it on terrain.
 *
 * Returns: 0 (full sun) .. strength
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_cloud_layer_sample_shadow       (LrgCloudLayer *self,
                                                         gdouble        x,
                                                         gdouble        y,
                                                         gdouble        z,
                                                         const gfloat  *towards_sun);

/**
 * lrg_cloud_layer_bake_shadow_image:
 * @self: an #LrgCloudLayer
 * @centre_x: absolute world x of the image centre
 * @centre_z: absolute world z of the image centre
 * @extent: metres covered by the image side
 * @size: pixels per side (8 .. 1024)
 * @towards_sun: (array fixed-size=3): unit direction to the sun
 *
 * The cloud shadow over a square of ground as a light map (white is
 * full sun, darker is shadow), for a renderer that projects cloud
 * shadows itself (a shadow-map pass multiplying its sun term).  Row 0
 * is the -Z edge, column 0 the -X edge.  Headless.
 *
 * Returns: (transfer full) (nullable): an RGBA image, %NULL for an
 *   invalid size or extent
 */
LRG_AVAILABLE_IN_ALL
GrlImage *          lrg_cloud_layer_bake_shadow_image   (LrgCloudLayer *self,
                                                         gdouble        centre_x,
                                                         gdouble        centre_z,
                                                         gdouble        extent,
                                                         guint          size,
                                                         const gfloat  *towards_sun);

/**
 * lrg_cloud_layer_set_colors:
 * @self: an #LrgCloudLayer
 * @lit: the sunlit cloud colour (alpha scales the whole deck)
 * @shade: the colour of the shaded cloud underside
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_colors          (LrgCloudLayer  *self,
                                                         const GrlColor *lit,
                                                         const GrlColor *shade);

/**
 * lrg_cloud_layer_set_world_origin:
 * @self: an #LrgCloudLayer
 * @x: absolute world x of draw-space 0 (a floating origin)
 * @y: absolute world y of draw-space 0
 * @z: absolute world z of draw-space 0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_set_world_origin    (LrgCloudLayer *self,
                                                         gdouble        x,
                                                         gdouble        y,
                                                         gdouble        z);

/**
 * lrg_cloud_layer_draw:
 * @self: an #LrgCloudLayer
 * @eye: the camera position in draw space
 * @sky_radius: the radius the sky draws at; the deck (12 km around the
 *   eye) is scaled about the eye to fit inside it, so it is never
 *   clipped by the far plane and still looks the right size
 * @towards_sun: (array fixed-size=3): unit direction to the sun
 *
 * Draws the slices with depth off, after the sky dome and before the
 * world (as a part of the sky).  Nothing at coverage 0 or without a
 * graphics context; a shader that fails to compile is warned about
 * once and the layer stays undrawn.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_draw                (LrgCloudLayer    *self,
                                                         const GrlVector3 *eye,
                                                         gfloat            sky_radius,
                                                         const gfloat     *towards_sun);

/**
 * lrg_cloud_layer_release_gpu:
 * @self: an #LrgCloudLayer
 *
 * Drops the shader (call while the context is alive); the next draw
 * compiles it again.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_cloud_layer_release_gpu         (LrgCloudLayer *self);

G_END_DECLS
