/* lrg-reel-anim.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-anim.h"
#include "../tween/lrg-easing.h"
#include <math.h>

/* ==========================================================================
 * Spring configuration (boxed)
 * ========================================================================== */

LrgReelSpringConfig *
lrg_reel_spring_config_new (void)
{
    LrgReelSpringConfig *self;

    self = g_new0 (LrgReelSpringConfig, 1);
    self->mass = 1.0;
    self->stiffness = 100.0;
    self->damping = 10.0;
    self->initial_velocity = 0.0;
    self->overshoot_clamping = FALSE;

    return self;
}

LrgReelSpringConfig *
lrg_reel_spring_config_copy (const LrgReelSpringConfig *self)
{
    LrgReelSpringConfig *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgReelSpringConfig, 1);
    *copy = *self;

    return copy;
}

void
lrg_reel_spring_config_free (LrgReelSpringConfig *self)
{
    g_return_if_fail (self != NULL);

    g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgReelSpringConfig, lrg_reel_spring_config,
                     lrg_reel_spring_config_copy, lrg_reel_spring_config_free)

/* ==========================================================================
 * Interpolation
 * ========================================================================== */

/* Interpolate within a single segment [a0,a1] -> [b0,b1] with easing. */
static gdouble
reel_interp_segment (gdouble       input,
                     gdouble       a0,
                     gdouble       a1,
                     gdouble       b0,
                     gdouble       b1,
                     LrgEasingType easing)
{
    gdouble span;
    gdouble t;
    gdouble eased;

    span = a1 - a0;

    /* Zero-width segment: avoid division by zero, snap to the right end. */
    if (span == 0.0)
        return b1;

    t = (input - a0) / span;
    eased = (gdouble) lrg_easing_apply (easing, (gfloat) t);

    return b0 + (b1 - b0) * eased;
}

/* Apply an extrapolation policy outside [a0,a1] -> [b0,b1] at the given end. */
static gdouble
reel_extrapolate (gdouble             input,
                  gdouble             a0,
                  gdouble             a1,
                  gdouble             b0,
                  gdouble             b1,
                  gdouble             full_in_lo,
                  gdouble             full_in_hi,
                  gdouble             clamp_value,
                  LrgEasingType       easing,
                  LrgReelExtrapolate  mode)
{
    gdouble span;
    gdouble range;
    gdouble wrapped;

    switch (mode)
    {
    case LRG_REEL_EXTRAPOLATE_CLAMP:
        return clamp_value;

    case LRG_REEL_EXTRAPOLATE_IDENTITY:
        return input;

    case LRG_REEL_EXTRAPOLATE_WRAP:
        range = full_in_hi - full_in_lo;
        if (range == 0.0)
            return clamp_value;
        /* Floor-mod into [full_in_lo, full_in_hi). */
        wrapped = input - full_in_lo;
        wrapped = wrapped - range * floor (wrapped / range);
        wrapped += full_in_lo;
        return reel_interp_segment (wrapped, a0, a1, b0, b1, easing);

    case LRG_REEL_EXTRAPOLATE_EXTEND:
    default:
        /* Linear extension of the edge segment (no easing curvature). */
        span = a1 - a0;
        if (span == 0.0)
            return b1;
        return b0 + (b1 - b0) * ((input - a0) / span);
    }
}

gdouble
lrg_reel_interpolate (gdouble             input,
                      const gdouble      *input_range,
                      gsize               n_input,
                      const gdouble      *output_range,
                      gsize               n_output,
                      LrgEasingType       easing,
                      LrgReelExtrapolate  extrapolate_left,
                      LrgReelExtrapolate  extrapolate_right)
{
    gsize i;
    gdouble lo;
    gdouble hi;

    g_return_val_if_fail (input_range != NULL, 0.0);
    g_return_val_if_fail (output_range != NULL, 0.0);
    g_return_val_if_fail (n_input >= 2, 0.0);
    g_return_val_if_fail (n_input == n_output, 0.0);

    lo = input_range[0];
    hi = input_range[n_input - 1];

    /* Below the range: extrapolate using the first segment. */
    if (input < lo)
        return reel_extrapolate (input,
                                 input_range[0], input_range[1],
                                 output_range[0], output_range[1],
                                 lo, hi,
                                 output_range[0],
                                 easing, extrapolate_left);

    /* Above the range: extrapolate using the last segment. */
    if (input > hi)
        return reel_extrapolate (input,
                                 input_range[n_input - 2], input_range[n_input - 1],
                                 output_range[n_input - 2], output_range[n_input - 1],
                                 lo, hi,
                                 output_range[n_input - 1],
                                 easing, extrapolate_right);

    /* In range: find the active segment [i, i+1]. */
    for (i = 0; i < n_input - 1; i++)
    {
        if (input <= input_range[i + 1])
            return reel_interp_segment (input,
                                        input_range[i], input_range[i + 1],
                                        output_range[i], output_range[i + 1],
                                        easing);
    }

    /* Exactly at the upper bound (or numerical edge). */
    return output_range[n_output - 1];
}

gdouble
lrg_reel_interpolate_clamped (gdouble       input,
                              gdouble       in_min,
                              gdouble       in_max,
                              gdouble       out_min,
                              gdouble       out_max,
                              LrgEasingType easing)
{
    gdouble in_range[2];
    gdouble out_range[2];

    in_range[0] = in_min;
    in_range[1] = in_max;
    out_range[0] = out_min;
    out_range[1] = out_max;

    return lrg_reel_interpolate (input, in_range, 2, out_range, 2, easing,
                                 LRG_REEL_EXTRAPOLATE_CLAMP,
                                 LRG_REEL_EXTRAPOLATE_CLAMP);
}

/* ==========================================================================
 * Spring physics
 * ========================================================================== */

/*
 * Damped harmonic oscillator displacement x(t) from equilibrium, given the
 * initial displacement x0 and initial velocity v0.  The equation of motion is
 * m*x'' + c*x' + k*x = 0.  We solve the underdamped, critically-damped, and
 * overdamped cases in closed form so any frame can be evaluated directly with
 * no time-stepping.
 */
static gdouble
reel_spring_displacement (const LrgReelSpringConfig *cfg,
                          gdouble                    t,
                          gdouble                    x0,
                          gdouble                    v0)
{
    gdouble m;
    gdouble k;
    gdouble c;
    gdouble omega0;
    gdouble zeta;

    m = (cfg->mass > 0.0) ? cfg->mass : 1.0;
    k = (cfg->stiffness > 0.0) ? cfg->stiffness : 100.0;
    c = (cfg->damping >= 0.0) ? cfg->damping : 10.0;

    omega0 = sqrt (k / m);
    zeta = c / (2.0 * sqrt (k * m));

    if (zeta < 1.0)
    {
        /* Underdamped: decaying oscillation. */
        gdouble omega1;
        gdouble envelope;

        omega1 = omega0 * sqrt (1.0 - zeta * zeta);
        envelope = exp (-zeta * omega0 * t);

        return envelope * (x0 * cos (omega1 * t)
                           + ((v0 + zeta * omega0 * x0) / omega1) * sin (omega1 * t));
    }
    else if (zeta > 1.0)
    {
        /* Overdamped: sum of two decaying exponentials. */
        gdouble disc;
        gdouble r1;
        gdouble r2;
        gdouble c1;
        gdouble c2;

        disc = omega0 * sqrt (zeta * zeta - 1.0);
        r1 = -zeta * omega0 + disc;
        r2 = -zeta * omega0 - disc;
        c1 = (v0 - r2 * x0) / (r1 - r2);
        c2 = x0 - c1;

        return c1 * exp (r1 * t) + c2 * exp (r2 * t);
    }
    else
    {
        /* Critically damped. */
        return exp (-omega0 * t) * (x0 + (v0 + omega0 * x0) * t);
    }
}

gdouble
lrg_reel_spring (gint                       frame,
                 gdouble                    fps,
                 const LrgReelSpringConfig *config,
                 gdouble                    from,
                 gdouble                    to)
{
    LrgReelSpringConfig defaults;
    const LrgReelSpringConfig *cfg;
    gdouble t;
    gdouble x0;
    gdouble x;
    gdouble value;

    if (fps <= 0.0)
        fps = 60.0;

    if (config != NULL)
    {
        cfg = config;
    }
    else
    {
        defaults.mass = 1.0;
        defaults.stiffness = 100.0;
        defaults.damping = 10.0;
        defaults.initial_velocity = 0.0;
        defaults.overshoot_clamping = FALSE;
        cfg = &defaults;
    }

    if (frame <= 0)
        return from;

    t = (gdouble) frame / fps;
    /* Displacement from equilibrium: x(0) = from - to, settles to 0. */
    x0 = from - to;
    x = reel_spring_displacement (cfg, t, x0, cfg->initial_velocity);
    value = to + x;

    if (cfg->overshoot_clamping)
    {
        if (to >= from)
            value = MIN (value, to);
        else
            value = MAX (value, to);
    }

    return value;
}

guint
lrg_reel_spring_duration_in_frames (gdouble                    fps,
                                    const LrgReelSpringConfig *config)
{
    LrgReelSpringConfig defaults;
    const LrgReelSpringConfig *cfg;
    const gdouble rest_threshold = 0.005;
    guint max_frames;
    guint last_active;
    guint f;

    if (fps <= 0.0)
        fps = 60.0;

    if (config != NULL)
    {
        cfg = config;
    }
    else
    {
        defaults.mass = 1.0;
        defaults.stiffness = 100.0;
        defaults.damping = 10.0;
        defaults.initial_velocity = 0.0;
        defaults.overshoot_clamping = FALSE;
        cfg = &defaults;
    }

    /* Probe a normalised 0 -> 1 spring; cap the search at 20 seconds. */
    max_frames = (guint) (fps * 20.0);
    if (max_frames < 1)
        max_frames = 1;

    last_active = 0;
    for (f = 1; f <= max_frames; f++)
    {
        gdouble t;
        gdouble x;

        t = (gdouble) f / fps;
        /* x0 = from - to = 0 - 1 = -1, so |displacement| is the residual. */
        x = reel_spring_displacement (cfg, t, -1.0, cfg->initial_velocity);
        if (fabs (x) > rest_threshold)
            last_active = f;
    }

    return last_active + 1;
}

/* ==========================================================================
 * Color interpolation (OkLab)
 * ========================================================================== */

GrlColor *
lrg_reel_interpolate_color (gdouble             input,
                            const gdouble      *input_range,
                            gsize               n_input,
                            const GrlColor     *colors,
                            gsize               n_colors,
                            LrgEasingType       easing,
                            LrgReelExtrapolate  extrapolate_left,
                            LrgReelExtrapolate  extrapolate_right)
{
    gsize   i;
    gdouble lo;
    gdouble hi;

    g_return_val_if_fail (input_range != NULL, NULL);
    g_return_val_if_fail (colors != NULL, NULL);
    g_return_val_if_fail (n_input >= 2, NULL);
    g_return_val_if_fail (n_input == n_colors, NULL);

    lo = input_range[0];
    hi = input_range[n_input - 1];

    /* Outside the range: WRAP folds back in; everything else clamps to the
     * nearest stop (a colour cannot be meaningfully extended). */
    if (input < lo)
    {
        if (extrapolate_left == LRG_REEL_EXTRAPOLATE_WRAP)
        {
            gdouble range = hi - lo;
            if (range > 0.0)
                input = input - range * floor ((input - lo) / range) + lo - lo;
        }
        else
        {
            return grl_color_copy (&colors[0]);
        }
    }
    else if (input > hi)
    {
        if (extrapolate_right == LRG_REEL_EXTRAPOLATE_WRAP)
        {
            gdouble range = hi - lo;
            if (range > 0.0)
                input = input - range * floor ((input - lo) / range) + lo - lo;
        }
        else
        {
            return grl_color_copy (&colors[n_colors - 1]);
        }
    }

    /* Clamp into range after a possible wrap. */
    input = CLAMP (input, lo, hi);

    for (i = 0; i < n_input - 1; i++)
    {
        if (input <= input_range[i + 1])
        {
            gdouble span = input_range[i + 1] - input_range[i];
            gdouble local;
            gdouble eased;

            if (span == 0.0)
                return grl_color_copy (&colors[i + 1]);

            local = (input - input_range[i]) / span;
            eased = (gdouble) lrg_easing_apply (easing, (gfloat) local);
            return grl_color_lerp_oklab (&colors[i], &colors[i + 1], (gfloat) eased);
        }
    }

    return grl_color_copy (&colors[n_colors - 1]);
}

GrlColor *
lrg_reel_interpolate_color_clamped (gdouble         input,
                                    gdouble         in_min,
                                    gdouble         in_max,
                                    const GrlColor *color_a,
                                    const GrlColor *color_b,
                                    LrgEasingType   easing)
{
    gdouble  in_range[2];
    GrlColor colors[2];

    g_return_val_if_fail (color_a != NULL, NULL);
    g_return_val_if_fail (color_b != NULL, NULL);

    in_range[0] = in_min;
    in_range[1] = in_max;
    colors[0] = *color_a;
    colors[1] = *color_b;

    return lrg_reel_interpolate_color (input, in_range, 2, colors, 2, easing,
                                       LRG_REEL_EXTRAPOLATE_CLAMP,
                                       LRG_REEL_EXTRAPOLATE_CLAMP);
}

/* ==========================================================================
 * Cubic-bezier easing
 * ========================================================================== */

/* Cubic bezier coordinate with anchors at 0 and 1: B(s) for controls (0,a1,a2,1). */
static gdouble
reel_bezier_axis (gdouble a1,
                  gdouble a2,
                  gdouble s)
{
    gdouble mt = 1.0 - s;

    return 3.0 * mt * mt * s * a1 + 3.0 * mt * s * s * a2 + s * s * s;
}

static gdouble
reel_bezier_axis_deriv (gdouble a1,
                        gdouble a2,
                        gdouble s)
{
    gdouble mt = 1.0 - s;

    return 3.0 * mt * mt * a1 + 6.0 * mt * s * (a2 - a1) + 3.0 * s * s * (1.0 - a2);
}

gdouble
lrg_reel_easing_bezier (gdouble x1,
                        gdouble y1,
                        gdouble x2,
                        gdouble y2,
                        gdouble t)
{
    gdouble s;
    gint    i;

    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    /* Solve reel_bezier_axis(x1,x2,s) == t for s via Newton, bisection fallback. */
    s = t;
    for (i = 0; i < 12; i++)
    {
        gdouble x = reel_bezier_axis (x1, x2, s) - t;
        gdouble dx;

        if (fabs (x) < 1e-7)
            break;
        dx = reel_bezier_axis_deriv (x1, x2, s);
        if (fabs (dx) < 1e-9)
            break;
        s -= x / dx;
        s = CLAMP (s, 0.0, 1.0);
    }

    return reel_bezier_axis (y1, y2, s);
}

/* ==========================================================================
 * Deterministic randomness + value noise
 * ========================================================================== */

gdouble
lrg_reel_random (guint64 seed)
{
    guint64 z = seed + 0x9E3779B97F4A7C15ULL;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);

    /* Top 53 bits -> [0,1). */
    return (gdouble) (z >> 11) * (1.0 / 9007199254740992.0);
}

gdouble
lrg_reel_random_range (guint64 seed,
                       gdouble min,
                       gdouble max)
{
    return min + lrg_reel_random (seed) * (max - min);
}

static gdouble
reel_smoothstep (gdouble t)
{
    return t * t * (3.0 - 2.0 * t);
}

/* Hash an integer lattice point to [-1,1]. */
static gdouble
reel_noise_hash1 (gint64 xi)
{
    return lrg_reel_random ((guint64) xi) * 2.0 - 1.0;
}

static gdouble
reel_noise_hash2 (gint64 xi,
                  gint64 yi)
{
    guint64 key = ((guint64) (guint32) (gint32) xi) |
                  (((guint64) (guint32) (gint32) yi) << 32);

    return lrg_reel_random (key) * 2.0 - 1.0;
}

static gdouble
reel_noise_hash3 (gint64 xi,
                  gint64 yi,
                  gint64 zi)
{
    guint64 key = ((guint64) (guint32) (gint32) xi) * 73856093ULL ^
                  ((guint64) (guint32) (gint32) yi) * 19349663ULL ^
                  ((guint64) (guint32) (gint32) zi) * 83492791ULL;

    return lrg_reel_random (key) * 2.0 - 1.0;
}

gdouble
lrg_reel_noise_1d (gdouble x)
{
    gint64  xi = (gint64) floor (x);
    gdouble xf = x - (gdouble) xi;
    gdouble a = reel_noise_hash1 (xi);
    gdouble b = reel_noise_hash1 (xi + 1);
    gdouble u = reel_smoothstep (xf);

    return a + u * (b - a);
}

gdouble
lrg_reel_noise_2d (gdouble x,
                   gdouble y)
{
    gint64  xi = (gint64) floor (x);
    gint64  yi = (gint64) floor (y);
    gdouble xf = x - (gdouble) xi;
    gdouble yf = y - (gdouble) yi;
    gdouble u = reel_smoothstep (xf);
    gdouble v = reel_smoothstep (yf);
    gdouble n00 = reel_noise_hash2 (xi, yi);
    gdouble n10 = reel_noise_hash2 (xi + 1, yi);
    gdouble n01 = reel_noise_hash2 (xi, yi + 1);
    gdouble n11 = reel_noise_hash2 (xi + 1, yi + 1);
    gdouble nx0 = n00 + u * (n10 - n00);
    gdouble nx1 = n01 + u * (n11 - n01);

    return nx0 + v * (nx1 - nx0);
}

gdouble
lrg_reel_noise_3d (gdouble x,
                   gdouble y,
                   gdouble z)
{
    gint64  xi = (gint64) floor (x);
    gint64  yi = (gint64) floor (y);
    gint64  zi = (gint64) floor (z);
    gdouble xf = x - (gdouble) xi;
    gdouble yf = y - (gdouble) yi;
    gdouble zf = z - (gdouble) zi;
    gdouble u = reel_smoothstep (xf);
    gdouble v = reel_smoothstep (yf);
    gdouble w = reel_smoothstep (zf);
    gdouble c000 = reel_noise_hash3 (xi, yi, zi);
    gdouble c100 = reel_noise_hash3 (xi + 1, yi, zi);
    gdouble c010 = reel_noise_hash3 (xi, yi + 1, zi);
    gdouble c110 = reel_noise_hash3 (xi + 1, yi + 1, zi);
    gdouble c001 = reel_noise_hash3 (xi, yi, zi + 1);
    gdouble c101 = reel_noise_hash3 (xi + 1, yi, zi + 1);
    gdouble c011 = reel_noise_hash3 (xi, yi + 1, zi + 1);
    gdouble c111 = reel_noise_hash3 (xi + 1, yi + 1, zi + 1);
    gdouble x00 = c000 + u * (c100 - c000);
    gdouble x10 = c010 + u * (c110 - c010);
    gdouble x01 = c001 + u * (c101 - c001);
    gdouble x11 = c011 + u * (c111 - c011);
    gdouble y0 = x00 + v * (x10 - x00);
    gdouble y1 = x01 + v * (x11 - x01);

    return y0 + w * (y1 - y0);
}
