/* lrg-sky-bodies.h - the sun disc, phased moons and stars of a sky
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Camera-centred like #LrgSkyDome and drawn right after it (or after a
 * painted band, over it): a deterministic star field that turns with
 * the sky and fades in at night, up to %LRG_SKY_BODIES_MAX_MOONS moon
 * discs lit by phase (lrg_celestial_moon_disc_span()) from the side
 * facing the sun, and an optional sun disc with a halo.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKY_BODIES (lrg_sky_bodies_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkyBodies, lrg_sky_bodies, LRG, SKY_BODIES, GObject)

/**
 * LRG_SKY_BODIES_MAX_MOONS:
 *
 * Moons a sky can show.
 */
#define LRG_SKY_BODIES_MAX_MOONS (4)

/**
 * LRG_SKY_BODIES_MAX_STARS:
 *
 * Largest star field.
 */
#define LRG_SKY_BODIES_MAX_STARS (8192)

/**
 * lrg_sky_bodies_new:
 *
 * No stars, no moons, no sun disc, radius 500.
 *
 * Returns: (transfer full): a new #LrgSkyBodies
 */
LRG_AVAILABLE_IN_ALL
LrgSkyBodies *      lrg_sky_bodies_new                  (void);

/**
 * lrg_sky_bodies_set_radius:
 * @self: an #LrgSkyBodies
 * @radius: the distance the bodies draw at (inside the far plane; the
 *   sky dome's radius is right), > 0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_radius           (LrgSkyBodies     *self,
                                                         gfloat            radius);

/**
 * lrg_sky_bodies_set_sun:
 * @self: an #LrgSkyBodies
 * @towards_sun: the direction to the sun (normalized; zero ignored);
 *   moons are lit from it even when the disc is hidden
 * @size_degrees: the disc's angular radius, 0 hides the disc
 * @color: (nullable): the disc colour (alpha scales disc and halo),
 *   %NULL keeps the colour
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_sun              (LrgSkyBodies     *self,
                                                         const GrlVector3 *towards_sun,
                                                         gfloat            size_degrees,
                                                         const GrlColor   *color);

/**
 * lrg_sky_bodies_set_moon:
 * @self: an #LrgSkyBodies
 * @index: the moon, 0 .. %LRG_SKY_BODIES_MAX_MOONS - 1
 * @towards_moon: the direction to the moon (normalized; zero ignored)
 * @size_degrees: angular radius (> 0)
 * @color: the lit colour (alpha scales the whole moon)
 * @phase: lrg_celestial_moon_phase() (0 new, 0.5 full)
 *
 * Shows a moon (or updates it).  Out-of-range indices are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_moon             (LrgSkyBodies     *self,
                                                         guint             index,
                                                         const GrlVector3 *towards_moon,
                                                         gfloat            size_degrees,
                                                         const GrlColor   *color,
                                                         gdouble           phase);

/**
 * lrg_sky_bodies_hide_moon:
 * @self: an #LrgSkyBodies
 * @index: the moon
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_hide_moon            (LrgSkyBodies     *self,
                                                         guint             index);

/**
 * lrg_sky_bodies_get_moon:
 * @self: an #LrgSkyBodies
 * @index: the moon
 * @out_phase: (out) (optional): its phase
 * @out_direction: (out caller-allocates) (optional): its direction
 *
 * Returns: %TRUE when that moon is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sky_bodies_get_moon             (LrgSkyBodies     *self,
                                                         guint             index,
                                                         gdouble          *out_phase,
                                                         GrlVector3       *out_direction);

/**
 * lrg_sky_bodies_set_moon_dark_color:
 * @self: an #LrgSkyBodies
 * @color: the unlit side of the moons (it hides the stars behind them;
 *   alpha 0 lets them through)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_moon_dark_color  (LrgSkyBodies     *self,
                                                         const GrlColor   *color);

/**
 * lrg_sky_bodies_set_stars:
 * @self: an #LrgSkyBodies
 * @count: stars, 0 .. %LRG_SKY_BODIES_MAX_STARS (clamped)
 * @seed: lrg_celestial_generate_stars()'s seed
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_stars            (LrgSkyBodies     *self,
                                                         guint             count,
                                                         guint32           seed);

/**
 * lrg_sky_bodies_set_star_rotation:
 * @self: an #LrgSkyBodies
 * @angle: radians the star sphere has turned (lrg_celestial_rotate_sky())
 * @tilt_degrees: the orbit tilt it turns with
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_star_rotation    (LrgSkyBodies     *self,
                                                         gdouble           angle,
                                                         gdouble           tilt_degrees);

/**
 * lrg_sky_bodies_set_star_visibility:
 * @self: an #LrgSkyBodies
 * @visibility: 0 (hidden, day or overcast) .. 1 (clear night)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_set_star_visibility  (LrgSkyBodies     *self,
                                                         gfloat            visibility);

/**
 * lrg_sky_bodies_get_star_visibility:
 * @self: an #LrgSkyBodies
 *
 * Returns: the visibility
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_sky_bodies_get_star_visibility  (LrgSkyBodies     *self);

/**
 * lrg_sky_bodies_get_star_count:
 * @self: an #LrgSkyBodies
 *
 * Returns: the stars of the field
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sky_bodies_get_star_count       (LrgSkyBodies     *self);

/**
 * lrg_sky_bodies_count_visible_stars:
 * @self: an #LrgSkyBodies
 *
 * Returns: the stars a draw would show now: above the horizon, with the
 *   visibility above 0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sky_bodies_count_visible_stars  (LrgSkyBodies     *self);

/**
 * lrg_sky_bodies_get_star:
 * @self: an #LrgSkyBodies
 * @index: a star
 * @out_direction: (out caller-allocates): its direction now (rotated)
 * @out_brightness: (out) (optional): 0.15 .. 1
 *
 * Returns: %TRUE when @index is a star
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sky_bodies_get_star             (LrgSkyBodies     *self,
                                                         guint             index,
                                                         GrlVector3       *out_direction,
                                                         gfloat           *out_brightness);

/**
 * lrg_sky_bodies_draw:
 * @self: an #LrgSkyBodies
 * @camera_position: the eye
 *
 * Stars (additive), then the sun disc, then the moons, with depth off
 * like the sky.  Bodies below the horizon are skipped.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sky_bodies_draw                 (LrgSkyBodies     *self,
                                                         const GrlVector3 *camera_position);

G_END_DECLS
