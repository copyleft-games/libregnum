/* lrg-easing.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Easing function library for animation interpolation.
 */

#include "config.h"

#include "lrg-easing.h"

#include <math.h>

/* Constants for easing calculations */
#define LRG_PI          3.14159265358979323846f
#define LRG_BACK_C1     1.70158f
#define LRG_BACK_C2     (LRG_BACK_C1 * 1.525f)
#define LRG_BACK_C3     (LRG_BACK_C1 + 1.0f)
#define LRG_ELASTIC_C4  ((2.0f * LRG_PI) / 3.0f)
#define LRG_ELASTIC_C5  ((2.0f * LRG_PI) / 4.5f)
#define LRG_BOUNCE_N1   7.5625f
#define LRG_BOUNCE_D1   2.75f

/*
 * lrg_easing_linear:
 *
 * Linear interpolation - no easing applied.
 */
gfloat
lrg_easing_linear (gfloat t)
{
    return t;
}

/*
 * lrg_easing_ease_in_quad:
 *
 * Quadratic ease-in: t^2
 */
gfloat
lrg_easing_ease_in_quad (gfloat t)
{
    return t * t;
}

/*
 * lrg_easing_ease_out_quad:
 *
 * Quadratic ease-out: 1 - (1-t)^2
 */
gfloat
lrg_easing_ease_out_quad (gfloat t)
{
    gfloat inv;

    inv = 1.0f - t;
    return 1.0f - inv * inv;
}

/*
 * lrg_easing_ease_in_out_quad:
 *
 * Quadratic ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_quad (gfloat t)
{
    gfloat inv;

    if (t < 0.5f)
    {
        return 2.0f * t * t;
    }
    else
    {
        inv = -2.0f * t + 2.0f;
        return 1.0f - inv * inv / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_cubic:
 *
 * Cubic ease-in: t^3
 */
gfloat
lrg_easing_ease_in_cubic (gfloat t)
{
    return t * t * t;
}

/*
 * lrg_easing_ease_out_cubic:
 *
 * Cubic ease-out: 1 - (1-t)^3
 */
gfloat
lrg_easing_ease_out_cubic (gfloat t)
{
    gfloat inv;

    inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

/*
 * lrg_easing_ease_in_out_cubic:
 *
 * Cubic ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_cubic (gfloat t)
{
    gfloat inv;

    if (t < 0.5f)
    {
        return 4.0f * t * t * t;
    }
    else
    {
        inv = -2.0f * t + 2.0f;
        return 1.0f - inv * inv * inv / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_quart:
 *
 * Quartic ease-in: t^4
 */
gfloat
lrg_easing_ease_in_quart (gfloat t)
{
    return t * t * t * t;
}

/*
 * lrg_easing_ease_out_quart:
 *
 * Quartic ease-out: 1 - (1-t)^4
 */
gfloat
lrg_easing_ease_out_quart (gfloat t)
{
    gfloat inv;

    inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv;
}

/*
 * lrg_easing_ease_in_out_quart:
 *
 * Quartic ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_quart (gfloat t)
{
    gfloat inv;

    if (t < 0.5f)
    {
        return 8.0f * t * t * t * t;
    }
    else
    {
        inv = -2.0f * t + 2.0f;
        return 1.0f - inv * inv * inv * inv / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_quint:
 *
 * Quintic ease-in: t^5
 */
gfloat
lrg_easing_ease_in_quint (gfloat t)
{
    return t * t * t * t * t;
}

/*
 * lrg_easing_ease_out_quint:
 *
 * Quintic ease-out: 1 - (1-t)^5
 */
gfloat
lrg_easing_ease_out_quint (gfloat t)
{
    gfloat inv;

    inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv * inv;
}

/*
 * lrg_easing_ease_in_out_quint:
 *
 * Quintic ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_quint (gfloat t)
{
    gfloat inv;

    if (t < 0.5f)
    {
        return 16.0f * t * t * t * t * t;
    }
    else
    {
        inv = -2.0f * t + 2.0f;
        return 1.0f - inv * inv * inv * inv * inv / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_sine:
 *
 * Sinusoidal ease-in.
 */
gfloat
lrg_easing_ease_in_sine (gfloat t)
{
    return 1.0f - cosf ((t * LRG_PI) / 2.0f);
}

/*
 * lrg_easing_ease_out_sine:
 *
 * Sinusoidal ease-out.
 */
gfloat
lrg_easing_ease_out_sine (gfloat t)
{
    return sinf ((t * LRG_PI) / 2.0f);
}

/*
 * lrg_easing_ease_in_out_sine:
 *
 * Sinusoidal ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_sine (gfloat t)
{
    return -(cosf (LRG_PI * t) - 1.0f) / 2.0f;
}

/*
 * lrg_easing_ease_in_expo:
 *
 * Exponential ease-in.
 */
gfloat
lrg_easing_ease_in_expo (gfloat t)
{
    if (t <= 0.0f)
        return 0.0f;

    return powf (2.0f, 10.0f * t - 10.0f);
}

/*
 * lrg_easing_ease_out_expo:
 *
 * Exponential ease-out.
 */
gfloat
lrg_easing_ease_out_expo (gfloat t)
{
    if (t >= 1.0f)
        return 1.0f;

    return 1.0f - powf (2.0f, -10.0f * t);
}

/*
 * lrg_easing_ease_in_out_expo:
 *
 * Exponential ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_expo (gfloat t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    if (t < 0.5f)
    {
        return powf (2.0f, 20.0f * t - 10.0f) / 2.0f;
    }
    else
    {
        return (2.0f - powf (2.0f, -20.0f * t + 10.0f)) / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_circ:
 *
 * Circular ease-in.
 */
gfloat
lrg_easing_ease_in_circ (gfloat t)
{
    return 1.0f - sqrtf (1.0f - t * t);
}

/*
 * lrg_easing_ease_out_circ:
 *
 * Circular ease-out.
 */
gfloat
lrg_easing_ease_out_circ (gfloat t)
{
    gfloat adj;

    adj = t - 1.0f;
    return sqrtf (1.0f - adj * adj);
}

/*
 * lrg_easing_ease_in_out_circ:
 *
 * Circular ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_circ (gfloat t)
{
    gfloat adj;

    if (t < 0.5f)
    {
        return (1.0f - sqrtf (1.0f - 4.0f * t * t)) / 2.0f;
    }
    else
    {
        adj = -2.0f * t + 2.0f;
        return (sqrtf (1.0f - adj * adj) + 1.0f) / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_back:
 *
 * Back ease-in (overshoots at start).
 */
gfloat
lrg_easing_ease_in_back (gfloat t)
{
    return LRG_BACK_C3 * t * t * t - LRG_BACK_C1 * t * t;
}

/*
 * lrg_easing_ease_out_back:
 *
 * Back ease-out (overshoots at end).
 */
gfloat
lrg_easing_ease_out_back (gfloat t)
{
    gfloat adj;

    adj = t - 1.0f;
    return 1.0f + LRG_BACK_C3 * adj * adj * adj + LRG_BACK_C1 * adj * adj;
}

/*
 * lrg_easing_ease_in_out_back:
 *
 * Back ease-in-out (overshoots at both ends).
 */
gfloat
lrg_easing_ease_in_out_back (gfloat t)
{
    gfloat adj;

    if (t < 0.5f)
    {
        adj = 2.0f * t;
        return (adj * adj * ((LRG_BACK_C2 + 1.0f) * adj - LRG_BACK_C2)) / 2.0f;
    }
    else
    {
        adj = 2.0f * t - 2.0f;
        return (adj * adj * ((LRG_BACK_C2 + 1.0f) * adj + LRG_BACK_C2) + 2.0f) / 2.0f;
    }
}

/*
 * lrg_easing_ease_in_elastic:
 *
 * Elastic ease-in.
 */
gfloat
lrg_easing_ease_in_elastic (gfloat t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    return -powf (2.0f, 10.0f * t - 10.0f) * sinf ((t * 10.0f - 10.75f) * LRG_ELASTIC_C4);
}

/*
 * lrg_easing_ease_out_elastic:
 *
 * Elastic ease-out.
 */
gfloat
lrg_easing_ease_out_elastic (gfloat t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    return powf (2.0f, -10.0f * t) * sinf ((t * 10.0f - 0.75f) * LRG_ELASTIC_C4) + 1.0f;
}

/*
 * lrg_easing_ease_in_out_elastic:
 *
 * Elastic ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_elastic (gfloat t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    if (t < 0.5f)
    {
        return -(powf (2.0f, 20.0f * t - 10.0f) * sinf ((20.0f * t - 11.125f) * LRG_ELASTIC_C5)) / 2.0f;
    }
    else
    {
        return (powf (2.0f, -20.0f * t + 10.0f) * sinf ((20.0f * t - 11.125f) * LRG_ELASTIC_C5)) / 2.0f + 1.0f;
    }
}

/*
 * lrg_easing_ease_out_bounce:
 *
 * Bounce ease-out (primary bounce implementation).
 */
gfloat
lrg_easing_ease_out_bounce (gfloat t)
{
    if (t < 1.0f / LRG_BOUNCE_D1)
    {
        return LRG_BOUNCE_N1 * t * t;
    }
    else if (t < 2.0f / LRG_BOUNCE_D1)
    {
        gfloat adj = t - 1.5f / LRG_BOUNCE_D1;
        return LRG_BOUNCE_N1 * adj * adj + 0.75f;
    }
    else if (t < 2.5f / LRG_BOUNCE_D1)
    {
        gfloat adj = t - 2.25f / LRG_BOUNCE_D1;
        return LRG_BOUNCE_N1 * adj * adj + 0.9375f;
    }
    else
    {
        gfloat adj = t - 2.625f / LRG_BOUNCE_D1;
        return LRG_BOUNCE_N1 * adj * adj + 0.984375f;
    }
}

/*
 * lrg_easing_ease_in_bounce:
 *
 * Bounce ease-in (derived from ease-out).
 */
gfloat
lrg_easing_ease_in_bounce (gfloat t)
{
    return 1.0f - lrg_easing_ease_out_bounce (1.0f - t);
}

/*
 * lrg_easing_ease_in_out_bounce:
 *
 * Bounce ease-in-out.
 */
gfloat
lrg_easing_ease_in_out_bounce (gfloat t)
{
    if (t < 0.5f)
    {
        return (1.0f - lrg_easing_ease_out_bounce (1.0f - 2.0f * t)) / 2.0f;
    }
    else
    {
        return (1.0f + lrg_easing_ease_out_bounce (2.0f * t - 1.0f)) / 2.0f;
    }
}

/*
 * lrg_easing_apply:
 *
 * Dispatch to the appropriate easing function based on type.
 */
gfloat
lrg_easing_apply (LrgEasingType type,
                  gfloat        t)
{
    switch (type)
    {
    case LRG_EASING_LINEAR:
        return lrg_easing_linear (t);
    case LRG_EASING_EASE_IN_QUAD:
        return lrg_easing_ease_in_quad (t);
    case LRG_EASING_EASE_OUT_QUAD:
        return lrg_easing_ease_out_quad (t);
    case LRG_EASING_EASE_IN_OUT_QUAD:
        return lrg_easing_ease_in_out_quad (t);
    case LRG_EASING_EASE_IN_CUBIC:
        return lrg_easing_ease_in_cubic (t);
    case LRG_EASING_EASE_OUT_CUBIC:
        return lrg_easing_ease_out_cubic (t);
    case LRG_EASING_EASE_IN_OUT_CUBIC:
        return lrg_easing_ease_in_out_cubic (t);
    case LRG_EASING_EASE_IN_QUART:
        return lrg_easing_ease_in_quart (t);
    case LRG_EASING_EASE_OUT_QUART:
        return lrg_easing_ease_out_quart (t);
    case LRG_EASING_EASE_IN_OUT_QUART:
        return lrg_easing_ease_in_out_quart (t);
    case LRG_EASING_EASE_IN_QUINT:
        return lrg_easing_ease_in_quint (t);
    case LRG_EASING_EASE_OUT_QUINT:
        return lrg_easing_ease_out_quint (t);
    case LRG_EASING_EASE_IN_OUT_QUINT:
        return lrg_easing_ease_in_out_quint (t);
    case LRG_EASING_EASE_IN_SINE:
        return lrg_easing_ease_in_sine (t);
    case LRG_EASING_EASE_OUT_SINE:
        return lrg_easing_ease_out_sine (t);
    case LRG_EASING_EASE_IN_OUT_SINE:
        return lrg_easing_ease_in_out_sine (t);
    case LRG_EASING_EASE_IN_EXPO:
        return lrg_easing_ease_in_expo (t);
    case LRG_EASING_EASE_OUT_EXPO:
        return lrg_easing_ease_out_expo (t);
    case LRG_EASING_EASE_IN_OUT_EXPO:
        return lrg_easing_ease_in_out_expo (t);
    case LRG_EASING_EASE_IN_CIRC:
        return lrg_easing_ease_in_circ (t);
    case LRG_EASING_EASE_OUT_CIRC:
        return lrg_easing_ease_out_circ (t);
    case LRG_EASING_EASE_IN_OUT_CIRC:
        return lrg_easing_ease_in_out_circ (t);
    case LRG_EASING_EASE_IN_BACK:
        return lrg_easing_ease_in_back (t);
    case LRG_EASING_EASE_OUT_BACK:
        return lrg_easing_ease_out_back (t);
    case LRG_EASING_EASE_IN_OUT_BACK:
        return lrg_easing_ease_in_out_back (t);
    case LRG_EASING_EASE_IN_ELASTIC:
        return lrg_easing_ease_in_elastic (t);
    case LRG_EASING_EASE_OUT_ELASTIC:
        return lrg_easing_ease_out_elastic (t);
    case LRG_EASING_EASE_IN_OUT_ELASTIC:
        return lrg_easing_ease_in_out_elastic (t);
    case LRG_EASING_EASE_IN_BOUNCE:
        return lrg_easing_ease_in_bounce (t);
    case LRG_EASING_EASE_OUT_BOUNCE:
        return lrg_easing_ease_out_bounce (t);
    case LRG_EASING_EASE_IN_OUT_BOUNCE:
        return lrg_easing_ease_in_out_bounce (t);
    default:
        return t;
    }
}

/*
 * lrg_easing_interpolate:
 *
 * Convenience function to interpolate between two values using easing.
 */
gfloat
lrg_easing_interpolate (LrgEasingType type,
                        gfloat        from,
                        gfloat        to,
                        gfloat        t)
{
    gfloat eased;

    eased = lrg_easing_apply (type, t);
    return from + (to - from) * eased;
}
