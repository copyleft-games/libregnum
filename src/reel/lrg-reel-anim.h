/* lrg-reel-anim.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Reel animation math: range interpolation and spring physics.
 *
 * These are pure, stateless functions of the current frame, designed for
 * frame-by-frame offline rendering (no game loop, no manager).  They build on
 * the engine easing library (#LrgEasingType, lrg_easing_apply()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Spring configuration (boxed value type)
 * ========================================================================== */

#define LRG_TYPE_REEL_SPRING_CONFIG (lrg_reel_spring_config_get_type ())

/**
 * LrgReelSpringConfig:
 * @mass: oscillator mass (inertia); default 1.0, must be > 0.
 * @stiffness: spring constant; default 100.0, must be > 0.
 * @damping: damping (friction) coefficient; default 10.0, must be >= 0.
 * @initial_velocity: initial velocity in value-units per second; default 0.0.
 * @overshoot_clamping: if %TRUE the result never overshoots @to.
 *
 * Physics parameters for lrg_reel_spring().  This is a plain copyable value
 * type (#GBoxed), not a #GObject.
 *
 * Since: 1.0
 */
struct _LrgReelSpringConfig
{
    gdouble  mass;
    gdouble  stiffness;
    gdouble  damping;
    gdouble  initial_velocity;
    gboolean overshoot_clamping;
};

LRG_AVAILABLE_IN_ALL
GType lrg_reel_spring_config_get_type (void) G_GNUC_CONST;

/**
 * lrg_reel_spring_config_new:
 *
 * Allocates a spring config initialised to the sensible defaults
 * (mass 1.0, stiffness 100.0, damping 10.0, velocity 0.0, no clamping).
 *
 * Returns: (transfer full): a new #LrgReelSpringConfig
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSpringConfig *
lrg_reel_spring_config_new (void);

/**
 * lrg_reel_spring_config_copy:
 * @self: a #LrgReelSpringConfig
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSpringConfig *
lrg_reel_spring_config_copy (const LrgReelSpringConfig *self);

/**
 * lrg_reel_spring_config_free:
 * @self: (transfer full): a #LrgReelSpringConfig
 *
 * Frees @self.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_spring_config_free (LrgReelSpringConfig *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgReelSpringConfig, lrg_reel_spring_config_free)

/* ==========================================================================
 * Interpolation
 * ========================================================================== */

/**
 * lrg_reel_interpolate:
 * @input: the input value (typically the current frame).
 * @input_range: (array length=n_input): monotonically increasing input
 *   breakpoints.
 * @n_input: number of input breakpoints (>= 2).
 * @output_range: (array length=n_output): output values, one per input
 *   breakpoint.
 * @n_output: number of output values (must equal @n_input).
 * @easing: easing applied within each segment.
 * @extrapolate_left: behaviour for @input below the first breakpoint.
 * @extrapolate_right: behaviour for @input above the last breakpoint.
 *
 * Maps @input through a piecewise curve defined by @input_range ->
 * @output_range, applying @easing within the active segment.  This is the
 * core value-animation primitive: e.g. fade a value from 0 to 1 over frames
 * 0..30, hold, then back.  Zero-width segments (equal adjacent inputs) are
 * handled without division by zero.
 *
 * Returns: the interpolated output value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_interpolate (gdouble             input,
                      const gdouble      *input_range,
                      gsize               n_input,
                      const gdouble      *output_range,
                      gsize               n_output,
                      LrgEasingType       easing,
                      LrgReelExtrapolate  extrapolate_left,
                      LrgReelExtrapolate  extrapolate_right);

/**
 * lrg_reel_interpolate_clamped:
 * @input: the input value (typically the current frame).
 * @in_min: start of the input range.
 * @in_max: end of the input range.
 * @out_min: output at @in_min.
 * @out_max: output at @in_max.
 * @easing: easing applied across the range.
 *
 * Convenience for the common two-point case with clamping at both ends.
 * Equivalent to lrg_reel_interpolate() over a single segment with
 * %LRG_REEL_EXTRAPOLATE_CLAMP on both sides.
 *
 * Returns: the interpolated output value, clamped to [@out_min, @out_max]
 *   (respecting their order)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_interpolate_clamped (gdouble       input,
                              gdouble       in_min,
                              gdouble       in_max,
                              gdouble       out_min,
                              gdouble       out_max,
                              LrgEasingType easing);

/* ==========================================================================
 * Spring physics
 * ========================================================================== */

/**
 * lrg_reel_spring:
 * @frame: the current frame (frame 0 yields @from).
 * @fps: frames per second (must be > 0); converts @frame to seconds.
 * @config: (nullable): spring parameters, or %NULL for the defaults.
 * @from: the start value.
 * @to: the rest (target) value.
 *
 * Evaluates a damped harmonic oscillator at @frame, animating from @from to
 * @to.  Underdamped configurations overshoot and settle; critically- and
 * over-damped configurations approach monotonically.  Set
 * @config->overshoot_clamping to forbid overshoot past @to.
 *
 * Returns: the spring value at @frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_spring (gint                       frame,
                 gdouble                    fps,
                 const LrgReelSpringConfig *config,
                 gdouble                    from,
                 gdouble                    to);

/**
 * lrg_reel_spring_duration_in_frames:
 * @fps: frames per second (must be > 0).
 * @config: (nullable): spring parameters, or %NULL for the defaults.
 *
 * Estimates how many frames the spring takes to come to rest (within a small
 * relative threshold), independent of @from/@to.  Useful for sizing a
 * sequence to a spring animation.
 *
 * Returns: the settle time in frames (always >= 1)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reel_spring_duration_in_frames (gdouble                    fps,
                                    const LrgReelSpringConfig *config);

/* ==========================================================================
 * Color interpolation (perceptual, OkLab)
 * ========================================================================== */

/**
 * lrg_reel_interpolate_color:
 * @input: the input value (e.g. the current frame).
 * @input_range: (array length=n_input): monotonically increasing breakpoints.
 * @n_input: number of breakpoints (>= 2).
 * @colors: (array length=n_colors): one #GrlColor per breakpoint.
 * @n_colors: number of colors (must equal @n_input).
 * @easing: easing applied within the active segment.
 * @extrapolate_left: behaviour for @input below the range.
 * @extrapolate_right: behaviour for @input above the range.
 *
 * Like lrg_reel_interpolate() but for colors, blending adjacent stops in OkLab
 * for perceptually even transitions.
 *
 * Returns: (transfer full): the interpolated #GrlColor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_reel_interpolate_color (gdouble             input,
                            const gdouble      *input_range,
                            gsize               n_input,
                            const GrlColor     *colors,
                            gsize               n_colors,
                            LrgEasingType       easing,
                            LrgReelExtrapolate  extrapolate_left,
                            LrgReelExtrapolate  extrapolate_right);

/**
 * lrg_reel_interpolate_color_clamped:
 * @input: the input value.
 * @in_min: start of the input range.
 * @in_max: end of the input range.
 * @color_a: color at @in_min.
 * @color_b: color at @in_max.
 * @easing: easing across the range.
 *
 * Two-stop OkLab color interpolation, clamped at both ends.
 *
 * Returns: (transfer full): the interpolated #GrlColor
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor *
lrg_reel_interpolate_color_clamped (gdouble         input,
                                    gdouble         in_min,
                                    gdouble         in_max,
                                    const GrlColor *color_a,
                                    const GrlColor *color_b,
                                    LrgEasingType   easing);

/* ==========================================================================
 * Cubic-bezier easing
 * ========================================================================== */

/**
 * lrg_reel_easing_bezier:
 * @x1: first control point X (0..1).
 * @y1: first control point Y.
 * @x2: second control point X (0..1).
 * @y2: second control point Y.
 * @t: normalized input (0..1).
 *
 * Evaluates a CSS-style cubic-bezier easing curve at @t (the curve passes
 * through (0,0) and (1,1)).  bezier(0,0,1,1,t) == t (linear).
 *
 * Returns: the eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_easing_bezier (gdouble x1,
                        gdouble y1,
                        gdouble x2,
                        gdouble y2,
                        gdouble t);

/* ==========================================================================
 * Deterministic randomness + value noise
 * ========================================================================== */

/**
 * lrg_reel_random:
 * @seed: any 64-bit seed (e.g. frame number combined with a clip index).
 *
 * A deterministic, uniformly-distributed pseudo-random number in [0.0, 1.0)
 * derived purely from @seed (same seed -> same value).  Use it instead of
 * rand() so renders are reproducible.
 *
 * Returns: a value in [0.0, 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_random (guint64 seed);

/**
 * lrg_reel_random_range:
 * @seed: any 64-bit seed.
 * @min: lower bound.
 * @max: upper bound.
 *
 * Returns: a deterministic value in [@min, @max)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_random_range (guint64 seed,
                       gdouble min,
                       gdouble max);

/**
 * lrg_reel_noise_1d:
 * @x: sample coordinate.
 *
 * Smooth, deterministic 1D value noise in [-1.0, 1.0].  Adjacent @x values
 * yield close results (continuous), ideal for organic motion.
 *
 * Returns: a value in [-1.0, 1.0]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_noise_1d (gdouble x);

/**
 * lrg_reel_noise_2d:
 * @x: sample X coordinate.
 * @y: sample Y coordinate.
 *
 * Returns: smooth 2D value noise in [-1.0, 1.0]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_noise_2d (gdouble x,
                   gdouble y);

/**
 * lrg_reel_noise_3d:
 * @x: sample X coordinate.
 * @y: sample Y coordinate.
 * @z: sample Z coordinate.
 *
 * Returns: smooth 3D value noise in [-1.0, 1.0]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_noise_3d (gdouble x,
                   gdouble y,
                   gdouble z);

G_END_DECLS
