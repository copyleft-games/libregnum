/* lrg-celestial.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sun, moon and star math.  Everything here is a pure function of its
 * arguments so skies stay deterministic headless and across platforms.
 */

#include "config.h"

#include <math.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "weather/lrg-celestial.h"

#define CEL_DEG2RAD (G_PI / 180.0)

gdouble
lrg_celestial_moon_phase (gdouble day,
                          gdouble period,
                          gdouble offset)
{
    gdouble cycle;

    if (!isfinite (day) || !isfinite (period) || !isfinite (offset) || period <= 0.0)
        return 0.0;

    /* fmod keeps the sign of the dividend: fold negatives back up */
    cycle = fmod (day + offset, period) / period;
    if (cycle < 0.0)
        cycle += 1.0;
    if (cycle >= 1.0)
        cycle = 0.0;
    return cycle;
}

gdouble
lrg_celestial_moon_illumination (gdouble phase)
{
    if (!isfinite (phase))
        return 0.0;
    return (1.0 - cos (2.0 * G_PI * phase)) * 0.5;
}

gboolean
lrg_celestial_moon_is_waxing (gdouble phase)
{
    gdouble p;

    if (!isfinite (phase))
        return FALSE;
    p = phase - floor (phase);
    return p > 0.0 && p < 0.5;
}

gboolean
lrg_celestial_moon_disc_span (gdouble  phase,
                              gdouble  v,
                              gdouble *x_left,
                              gdouble *x_right)
{
    gdouble half;
    gdouble terminator;
    gdouble p;

    g_return_val_if_fail (x_left != NULL, FALSE);
    g_return_val_if_fail (x_right != NULL, FALSE);

    *x_left = 0.0;
    *x_right = 0.0;
    if (!isfinite (phase) || !isfinite (v) || v < -1.0 || v > 1.0)
        return FALSE;

    p = phase - floor (phase);
    half = sqrt (MAX (0.0, 1.0 - v * v));
    terminator = half * cos (2.0 * G_PI * p);

    /*
     * Waxing: lit from the right limb (+half) to the terminator, which
     * sweeps from the right limb (new) through the centre (quarter) to
     * the left limb (full).  Waning mirrors it: lit from the left limb
     * to a terminator sweeping back to the left limb.
     */
    if (p <= 0.5)
    {
        *x_left = terminator;
        *x_right = half;
    }
    else
    {
        *x_left = -half;
        *x_right = -terminator;
    }
    return (*x_right - *x_left) > 1e-9;
}

void
lrg_celestial_orbit_direction (gdouble  angle,
                               gdouble  tilt_degrees,
                               gfloat  *out_direction)
{
    gdouble t;

    g_return_if_fail (out_direction != NULL);

    if (!isfinite (angle))
        angle = 0.0;
    if (!isfinite (tilt_degrees))
        tilt_degrees = 0.0;
    t = tilt_degrees * CEL_DEG2RAD;
    out_direction[0] = (gfloat)cos (angle);
    out_direction[1] = (gfloat)(sin (angle) * cos (t));
    out_direction[2] = (gfloat)(sin (angle) * sin (t));
}

gdouble
lrg_celestial_moon_orbit_angle (gdouble sun_angle,
                                gdouble phase)
{
    gdouble a;

    if (!isfinite (sun_angle))
        sun_angle = 0.0;
    if (!isfinite (phase))
        phase = 0.0;

    /* the moon falls behind the sun by 2 pi x phase per cycle */
    a = fmod (sun_angle - 2.0 * G_PI * phase, 2.0 * G_PI);
    if (a < 0.0)
        a += 2.0 * G_PI;
    return a;
}

gfloat
lrg_celestial_night_factor (gfloat sun_height)
{
    gfloat hi = sinf ((gfloat)(3.0 * CEL_DEG2RAD));
    gfloat lo = sinf ((gfloat)(-9.0 * CEL_DEG2RAD));
    gfloat t;

    if (!isfinite (sun_height))
        return 0.0f;
    t = (hi - sun_height) / (hi - lo);
    t = CLAMP (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* hash32:
 * A 32-bit integer mix (lowbias32): the same bits everywhere. */
static guint32
hash32 (guint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* unit01:
 * The k-th uniform number of star i, in [0, 1). */
static gdouble
unit01 (guint32 seed,
        guint   i,
        guint   k)
{
    return (gdouble)hash32 (seed ^ hash32 ((guint32)i * 4u + k + 0x9e3779b9U)) / 4294967296.0;
}

void
lrg_celestial_generate_stars (guint32  seed,
                              guint    count,
                              gfloat  *out_directions,
                              gfloat  *out_brightness)
{
    guint i;

    g_return_if_fail (count == 0 || out_directions != NULL);

    for (i = 0; i < count; i++)
    {
        /* uniform on the sphere: z uniform in [-1, 1], azimuth uniform */
        gdouble y = 2.0 * unit01 (seed, i, 0) - 1.0;
        gdouble az = 2.0 * G_PI * unit01 (seed, i, 1);
        gdouble r = sqrt (MAX (0.0, 1.0 - y * y));

        out_directions[i * 3 + 0] = (gfloat)(r * cos (az));
        out_directions[i * 3 + 1] = (gfloat)y;
        out_directions[i * 3 + 2] = (gfloat)(r * sin (az));
        if (out_brightness != NULL)
        {
            gdouble b = unit01 (seed, i, 2);

            /* few bright stars, many faint ones */
            out_brightness[i] = (gfloat)(0.15 + 0.85 * b * b * b);
        }
    }
}

void
lrg_celestial_rotate_sky (const gfloat *direction,
                          gdouble       angle,
                          gdouble       tilt_degrees,
                          gfloat       *out_direction)
{
    gdouble t;
    gdouble n[3];
    gdouble v[3];
    gdouble c;
    gdouble s;
    gdouble dot;
    gdouble cr[3];
    guint   k;

    g_return_if_fail (direction != NULL);
    g_return_if_fail (out_direction != NULL);

    if (!isfinite (angle))
        angle = 0.0;
    if (!isfinite (tilt_degrees))
        tilt_degrees = 0.0;
    t = tilt_degrees * CEL_DEG2RAD;

    /* the orbit plane holds (1, 0, 0) and (0, cos t, sin t); its
     * normal is their cross product */
    n[0] = 0.0;
    n[1] = -sin (t);
    n[2] = cos (t);
    for (k = 0; k < 3; k++)
        v[k] = direction[k];

    /* Rodrigues: v cos a + (n x v) sin a + n (n . v)(1 - cos a); n x e1
     * is e2, so a positive angle turns the sky the way the sun goes */
    c = cos (angle);
    s = sin (angle);
    dot = n[0] * v[0] + n[1] * v[1] + n[2] * v[2];
    cr[0] = n[1] * v[2] - n[2] * v[1];
    cr[1] = n[2] * v[0] - n[0] * v[2];
    cr[2] = n[0] * v[1] - n[1] * v[0];
    for (k = 0; k < 3; k++)
        out_direction[k] = (gfloat)(v[k] * c + cr[k] * s + n[k] * dot * (1.0 - c));
}

gdouble
lrg_celestial_thunder_delay (gdouble distance)
{
    if (!isfinite (distance) || distance <= 0.0)
        return 0.0;
    return distance / LRG_SPEED_OF_SOUND;
}
