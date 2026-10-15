/* lrg-sky-dome.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkyDome - a camera-centred sky: a zenith/horizon/ground colour
 * gradient, an optional textured horizon band (one texture wrapped all
 * the way round, or two halves), a sun disc with a soft halo and a
 * time-of-day helper interpolating configurable colour keyframes. The
 * geometry and colours are computed headlessly; only draw() needs GL.
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

#define LRG_TYPE_SKY_DOME (lrg_sky_dome_get_type ())

/**
 * LRG_SKY_DOME_DEFAULT_RADIUS:
 *
 * Default dome radius in world units: inside raylib's default far
 * plane (1000) with room to spare.
 */
#define LRG_SKY_DOME_DEFAULT_RADIUS (100.0f)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkyDome, lrg_sky_dome, LRG, SKY_DOME, GObject)

/**
 * lrg_sky_dome_new:
 *
 * Creates a sky with 32 segments, 8 rings per hemisphere, radius
 * %LRG_SKY_DOME_DEFAULT_RADIUS, a default set of four time-of-day
 * keyframes (midnight, dawn at 6, noon, dusk at 18) and the time set to
 * noon.
 *
 * Returns: (transfer full): a new #LrgSkyDome
 */
LRG_AVAILABLE_IN_ALL
LrgSkyDome *
lrg_sky_dome_new (void);

/**
 * lrg_sky_dome_set_resolution:
 * @self: an #LrgSkyDome
 * @segments: vertices around the horizon, 3 .. 256
 * @rings: rings per hemisphere (horizon to pole), 1 .. 64
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_resolution (LrgSkyDome *self,
                             guint       segments,
                             guint       rings);

/**
 * lrg_sky_dome_set_radius:
 * @self: an #LrgSkyDome
 * @radius: dome radius (> 0); keep it inside the camera's far plane
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_radius (LrgSkyDome *self,
                         gfloat      radius);

/**
 * lrg_sky_dome_get_radius:
 * @self: an #LrgSkyDome
 *
 * Returns: the dome radius
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sky_dome_get_radius (LrgSkyDome *self);

/* ---------------------------------------------------------------------- */
/* Colours                                                                */
/* ---------------------------------------------------------------------- */

/**
 * lrg_sky_dome_set_zenith_color:
 * @self: an #LrgSkyDome
 * @color: colour straight up
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_zenith_color (LrgSkyDome     *self,
                               const GrlColor *color);

/**
 * lrg_sky_dome_set_horizon_color:
 * @self: an #LrgSkyDome
 * @color: colour at the horizon (also a good fog colour)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_horizon_color (LrgSkyDome     *self,
                                const GrlColor *color);

/**
 * lrg_sky_dome_set_ground_color:
 * @self: an #LrgSkyDome
 * @color: (nullable): colour straight down, %NULL to follow the horizon
 *   colour (the default)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_ground_color (LrgSkyDome     *self,
                               const GrlColor *color);

/**
 * lrg_sky_dome_get_zenith_color:
 * @self: an #LrgSkyDome
 * @color: (out caller-allocates): the current zenith colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_zenith_color (LrgSkyDome *self,
                               GrlColor   *color);

/**
 * lrg_sky_dome_get_horizon_color:
 * @self: an #LrgSkyDome
 * @color: (out caller-allocates): the current horizon colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_horizon_color (LrgSkyDome *self,
                                GrlColor   *color);

/**
 * lrg_sky_dome_get_ground_color:
 * @self: an #LrgSkyDome
 * @color: (out caller-allocates): the current ground colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_ground_color (LrgSkyDome *self,
                               GrlColor   *color);

/**
 * lrg_sky_dome_get_color_at:
 * @self: an #LrgSkyDome
 * @direction: a view direction (need not be normalized, not zero)
 * @color: (out caller-allocates): the gradient colour in that direction
 *
 * The gradient as drawn at the dome vertices: above the horizon
 * horizon → zenith by the sine of the elevation, below it horizon →
 * ground likewise. Headless.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_color_at (LrgSkyDome       *self,
                           const GrlVector3 *direction,
                           GrlColor         *color);

/* ---------------------------------------------------------------------- */
/* Horizon band                                                           */
/* ---------------------------------------------------------------------- */

/**
 * lrg_sky_dome_set_band_texture:
 * @self: an #LrgSkyDome
 * @texture: (nullable): one texture wrapped once around the horizon,
 *   %NULL to remove the band
 *
 * Replaces any band textures. The sky keeps a reference.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_band_texture (LrgSkyDome *self,
                               GrlTexture *texture);

/**
 * lrg_sky_dome_set_band_halves:
 * @self: an #LrgSkyDome
 * @first: first half, covering azimuth [heading, heading + 180)
 * @second: second half, covering azimuth [heading + 180, heading + 360)
 *
 * Wraps two textures round the horizon, each over half of it (e.g. a
 * panorama stored as east and west images). The sky keeps references.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_band_halves (LrgSkyDome *self,
                              GrlTexture *first,
                              GrlTexture *second);

/**
 * lrg_sky_dome_set_band_range:
 * @self: an #LrgSkyDome
 * @bottom_degrees: elevation of the texture's bottom edge (> -89)
 * @top_degrees: elevation of its top edge (< 89, > @bottom_degrees)
 *
 * The band is a vertical cylinder at the dome radius (a cylindrical
 * panorama keeps straight vertical lines); its edges sit at these
 * elevations seen from the centre. Default -5 .. 40.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_band_range (LrgSkyDome *self,
                             gfloat      bottom_degrees,
                             gfloat      top_degrees);

/**
 * lrg_sky_dome_set_band_heading:
 * @self: an #LrgSkyDome
 * @degrees: azimuth at which the band texture starts (u = 0)
 *
 * Azimuth is measured clockwise seen from above, from -Z (0) towards +X
 * (90). Texture u increases with azimuth, so the image reads left to
 * right as the viewer turns right.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_band_heading (LrgSkyDome *self,
                               gfloat      degrees);

/**
 * lrg_sky_dome_set_band_tint:
 * @self: an #LrgSkyDome
 * @tint: (nullable): colour multiplied into the band, %NULL for white
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_band_tint (LrgSkyDome     *self,
                            const GrlColor *tint);

/**
 * lrg_sky_dome_get_band_uv:
 * @self: an #LrgSkyDome
 * @azimuth_degrees: a direction round the horizon
 * @texture_index: (out) (optional): 0 for the single texture or first
 *   half, 1 for the second half
 * @u: (out) (optional): horizontal texture coordinate in [0, 1)
 *
 * Which band texture, and where in it, faces the given azimuth.
 * Headless.
 *
 * Returns: %TRUE when a band is set
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sky_dome_get_band_uv (LrgSkyDome *self,
                          gfloat      azimuth_degrees,
                          guint      *texture_index,
                          gfloat     *u);

/* ---------------------------------------------------------------------- */
/* Sun                                                                    */
/* ---------------------------------------------------------------------- */

/**
 * lrg_sky_dome_set_sun_direction:
 * @self: an #LrgSkyDome
 * @direction: direction from the viewer towards the sun (normalized
 *   internally; zero is ignored)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_sun_direction (LrgSkyDome       *self,
                                const GrlVector3 *direction);

/**
 * lrg_sky_dome_get_sun_direction:
 * @self: an #LrgSkyDome
 * @direction: (out caller-allocates): unit direction towards the sun
 *
 * Feed the negated vector to a directional light.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_sun_direction (LrgSkyDome *self,
                                GrlVector3 *direction);

/**
 * lrg_sky_dome_set_sun_color:
 * @self: an #LrgSkyDome
 * @color: disc colour; its alpha scales the disc and halo
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_sun_color (LrgSkyDome     *self,
                            const GrlColor *color);

/**
 * lrg_sky_dome_get_sun_color:
 * @self: an #LrgSkyDome
 * @color: (out caller-allocates): the current sun colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_get_sun_color (LrgSkyDome *self,
                            GrlColor   *color);

/**
 * lrg_sky_dome_set_sun_size:
 * @self: an #LrgSkyDome
 * @degrees: angular radius of the disc (0 hides the sun); the halo is
 *   four times larger. Default 2.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_sun_size (LrgSkyDome *self,
                           gfloat      degrees);

/**
 * lrg_sky_dome_set_sun_tilt:
 * @self: an #LrgSkyDome
 * @degrees: how far the sun's daily arc leans from the zenith towards +Z
 *   (default 30)
 *
 * Used by lrg_sky_dome_set_time_of_day().
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_sun_tilt (LrgSkyDome *self,
                           gfloat      degrees);

/* ---------------------------------------------------------------------- */
/* Time of day                                                            */
/* ---------------------------------------------------------------------- */

/**
 * lrg_sky_dome_clear_keyframes:
 * @self: an #LrgSkyDome
 *
 * Removes every time-of-day keyframe (including the defaults).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_clear_keyframes (LrgSkyDome *self);

/**
 * lrg_sky_dome_add_keyframe:
 * @self: an #LrgSkyDome
 * @hours: time of day in [0, 24)
 * @zenith: zenith colour at that time
 * @horizon: horizon colour at that time
 * @sun: sun colour at that time (alpha 0 hides it)
 *
 * Adds a keyframe, replacing one at the same time.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_add_keyframe (LrgSkyDome     *self,
                           gfloat          hours,
                           const GrlColor *zenith,
                           const GrlColor *horizon,
                           const GrlColor *sun);

/**
 * lrg_sky_dome_get_keyframe_count:
 * @self: an #LrgSkyDome
 *
 * Returns: the number of keyframes
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_sky_dome_get_keyframe_count (LrgSkyDome *self);

/**
 * lrg_sky_dome_set_time_of_day:
 * @self: an #LrgSkyDome
 * @hours: time of day; wrapped into [0, 24)
 *
 * Sets the zenith, horizon and sun colours by linear interpolation
 * between the surrounding keyframes (cyclically across midnight; one
 * keyframe is constant, none leaves the colours alone) and moves the
 * sun along its daily arc: rising at +X at 6, highest at 12 (tilted by
 * the sun tilt towards +Z), setting at -X at 18 and below the horizon
 * at night.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_set_time_of_day (LrgSkyDome *self,
                              gfloat      hours);

/**
 * lrg_sky_dome_get_time_of_day:
 * @self: an #LrgSkyDome
 *
 * Returns: the last time passed to lrg_sky_dome_set_time_of_day()
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_sky_dome_get_time_of_day (LrgSkyDome *self);

/* ---------------------------------------------------------------------- */
/* Geometry and drawing                                                   */
/* ---------------------------------------------------------------------- */

/**
 * lrg_sky_dome_get_positions:
 * @self: an #LrgSkyDome
 * @n_floats: (out): number of floats (3 per vertex)
 *
 * Gradient sphere vertices relative to the camera: rings from the nadir
 * to the zenith (one ring exactly on the horizon), each with segments + 1
 * vertices (the seam is duplicated). Headless.
 *
 * Returns: (transfer none) (array length=n_floats): positions
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_sky_dome_get_positions (LrgSkyDome *self,
                            guint      *n_floats);

/**
 * lrg_sky_dome_get_colors:
 * @self: an #LrgSkyDome
 * @n_bytes: (out): number of bytes (RGBA per vertex)
 *
 * Returns: (transfer none) (array length=n_bytes): vertex colours
 */
LRG_AVAILABLE_IN_ALL
const guint8 *
lrg_sky_dome_get_colors (LrgSkyDome *self,
                         guint      *n_bytes);

/**
 * lrg_sky_dome_get_indices:
 * @self: an #LrgSkyDome
 * @n_indices: (out): number of indices
 *
 * Triangles are counter-clockwise seen from the centre (front faces
 * point inwards).
 *
 * Returns: (transfer none) (array length=n_indices): triangle indices
 */
LRG_AVAILABLE_IN_ALL
const guint16 *
lrg_sky_dome_get_indices (LrgSkyDome *self,
                          guint      *n_indices);

/**
 * lrg_sky_dome_draw:
 * @self: an #LrgSkyDome
 * @camera_position: the eye position of the active 3D camera
 *
 * Draws the gradient, then the sun, then the band, centred on the
 * camera with depth test and depth writes off, so call it first inside
 * BeginMode3D(), before the world. Restores depth test, depth writes and
 * back-face culling (raylib's defaults) afterwards.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sky_dome_draw (LrgSkyDome       *self,
                   const GrlVector3 *camera_position);

G_END_DECLS
