/* lrg-celestial.h - sun, moon and star math for skies
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pure functions (no GPU, no state): moon phases from a calendar day,
 * the lit part of a moon disc, orbit directions across the sky, a
 * deterministic star field and the delay of thunder over a distance.
 * LrgSkyBodies draws what these compute.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_SPEED_OF_SOUND:
 *
 * Speed of sound in air at about 20 degrees Celsius, metres per second:
 * thunder reaches a listener this fast (lrg_celestial_thunder_delay()).
 */
#define LRG_SPEED_OF_SOUND (343.0)

/**
 * lrg_celestial_moon_phase:
 * @day: the calendar day, fractional (any epoch; days may be negative)
 * @period: days from one new moon to the next (> 0)
 * @offset: days added to @day before the cycle is taken (where in its
 *   cycle the moon was at day 0)
 *
 * The phase of a moon: 0 new, 0.25 first quarter (waxing half), 0.5
 * full, 0.75 last quarter (waning half), back towards 1 = new.
 *
 * Returns: the phase in [0, 1); 0 for a non-positive or non-finite
 *   @period or @day
 */
LRG_AVAILABLE_IN_ALL
gdouble     lrg_celestial_moon_phase            (gdouble     day,
                                                 gdouble     period,
                                                 gdouble     offset);

/**
 * lrg_celestial_moon_illumination:
 * @phase: a phase from lrg_celestial_moon_phase()
 *
 * The lit fraction of the visible disc: (1 - cos (2 pi phase)) / 2.
 *
 * Returns: 0 (new) .. 1 (full)
 */
LRG_AVAILABLE_IN_ALL
gdouble     lrg_celestial_moon_illumination     (gdouble     phase);

/**
 * lrg_celestial_moon_is_waxing:
 * @phase: a phase
 *
 * Returns: %TRUE while the lit part grows (phase in (0, 0.5))
 */
LRG_AVAILABLE_IN_ALL
gboolean    lrg_celestial_moon_is_waxing        (gdouble     phase);

/**
 * lrg_celestial_moon_disc_span:
 * @phase: a phase
 * @v: the row of the unit disc, -1 (bottom) .. 1 (top)
 * @x_left: (out): left end of the lit span on that row (unit disc)
 * @x_right: (out): right end
 *
 * The lit part of row @v of a unit moon disc lit from the right while
 * waxing and from the left while waning: between the limb
 * (+-sqrt (1 - v^2)) and the terminator ellipse whose half width is
 * sqrt (1 - v^2) x cos (2 pi phase).
 *
 * Returns: %TRUE when the row has a lit span (not at new moon, nor
 *   outside the disc)
 */
LRG_AVAILABLE_IN_ALL
gboolean    lrg_celestial_moon_disc_span        (gdouble     phase,
                                                 gdouble     v,
                                                 gdouble    *x_left,
                                                 gdouble    *x_right);

/**
 * lrg_celestial_orbit_direction:
 * @angle: radians along the orbit: 0 rising due east (+X), pi/2 at the
 *   top of the arc, pi setting due west, 3 pi/2 at the bottom
 * @tilt_degrees: how far the arc leans from the zenith towards +Z
 * @out_direction: (out caller-allocates) (array fixed-size=3): unit
 *   direction from the viewer towards the body
 *
 * A body on a circular daily arc: (cos a, sin a cos t, sin a sin t).
 * The sun of hour h is at angle (h - 6) / 24 x 2 pi.
 */
LRG_AVAILABLE_IN_ALL
void        lrg_celestial_orbit_direction       (gdouble     angle,
                                                 gdouble     tilt_degrees,
                                                 gfloat     *out_direction);

/**
 * lrg_celestial_moon_orbit_angle:
 * @sun_angle: the sun's orbit angle (lrg_celestial_orbit_direction())
 * @phase: the moon's phase
 *
 * A moon trails the sun by its phase: new moons travel with the sun,
 * full moons opposite it (rising at sunset).
 *
 * Returns: the moon's orbit angle, radians in [0, 2 pi)
 */
LRG_AVAILABLE_IN_ALL
gdouble     lrg_celestial_moon_orbit_angle      (gdouble     sun_angle,
                                                 gdouble     phase);

/**
 * lrg_celestial_night_factor:
 * @sun_height: the y component of the unit direction towards the sun
 *
 * How dark the sky is for stars: 0 with the sun 3 degrees above the
 * horizon or higher, 1 from 9 degrees below it, smooth between.
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat      lrg_celestial_night_factor          (gfloat      sun_height);

/**
 * lrg_celestial_generate_stars:
 * @seed: the star field's seed
 * @count: stars to generate
 * @out_directions: (out caller-allocates) (array): 3 x @count floats,
 *   unit directions spread evenly over the whole sphere
 * @out_brightness: (out caller-allocates) (array) (nullable): @count
 *   floats, 0.15 .. 1, most stars faint
 *
 * The same seed and count always give the same stars, on every
 * platform (an integer hash, no global random state).
 */
LRG_AVAILABLE_IN_ALL
void        lrg_celestial_generate_stars        (guint32     seed,
                                                 guint       count,
                                                 gfloat     *out_directions,
                                                 gfloat     *out_brightness);

/**
 * lrg_celestial_rotate_sky:
 * @direction: (array fixed-size=3): a direction fixed to the star sphere
 * @angle: radians the sky has turned
 * @tilt_degrees: the orbit tilt (lrg_celestial_orbit_direction()); the
 *   sky turns about the normal of that orbit, so stars wheel with the
 *   sun and moons
 * @out_direction: (out caller-allocates) (array fixed-size=3): the
 *   direction now
 */
LRG_AVAILABLE_IN_ALL
void        lrg_celestial_rotate_sky            (const gfloat *direction,
                                                 gdouble       angle,
                                                 gdouble       tilt_degrees,
                                                 gfloat       *out_direction);

/**
 * lrg_celestial_thunder_delay:
 * @distance: metres from the strike to the listener
 *
 * Returns: seconds until the thunder is heard: @distance /
 *   %LRG_SPEED_OF_SOUND; 0 for negative or non-finite distances
 */
LRG_AVAILABLE_IN_ALL
gdouble     lrg_celestial_thunder_delay         (gdouble     distance);

G_END_DECLS
