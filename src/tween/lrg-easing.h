/* lrg-easing.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Easing function library for animation interpolation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * lrg_easing_apply:
 * @type: The easing function type
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Applies an easing function to a normalized time value.
 * The input @t should be in the range [0.0, 1.0] where 0.0 represents
 * the start and 1.0 represents the end of the animation.
 *
 * Returns: The eased value, typically in range [0.0, 1.0] but may
 *          exceed this range for elastic/back easing types
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_apply            (LrgEasingType   type,
                                             gfloat          t);

/**
 * lrg_easing_linear:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Linear interpolation (no easing).
 *
 * Returns: The input value unchanged
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_linear           (gfloat          t);

/**
 * lrg_easing_ease_in_quad:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quadratic ease-in (accelerating from zero velocity).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_quad     (gfloat          t);

/**
 * lrg_easing_ease_out_quad:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quadratic ease-out (decelerating to zero velocity).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_quad    (gfloat          t);

/**
 * lrg_easing_ease_in_out_quad:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quadratic ease-in-out (acceleration until halfway, then deceleration).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_quad (gfloat          t);

/**
 * lrg_easing_ease_in_cubic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Cubic ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_cubic    (gfloat          t);

/**
 * lrg_easing_ease_out_cubic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Cubic ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_cubic   (gfloat          t);

/**
 * lrg_easing_ease_in_out_cubic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Cubic ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_cubic (gfloat         t);

/**
 * lrg_easing_ease_in_quart:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quartic ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_quart    (gfloat          t);

/**
 * lrg_easing_ease_out_quart:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quartic ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_quart   (gfloat          t);

/**
 * lrg_easing_ease_in_out_quart:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quartic ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_quart (gfloat         t);

/**
 * lrg_easing_ease_in_quint:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quintic ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_quint    (gfloat          t);

/**
 * lrg_easing_ease_out_quint:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quintic ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_quint   (gfloat          t);

/**
 * lrg_easing_ease_in_out_quint:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Quintic ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_quint (gfloat         t);

/**
 * lrg_easing_ease_in_sine:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Sinusoidal ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_sine     (gfloat          t);

/**
 * lrg_easing_ease_out_sine:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Sinusoidal ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_sine    (gfloat          t);

/**
 * lrg_easing_ease_in_out_sine:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Sinusoidal ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_sine (gfloat          t);

/**
 * lrg_easing_ease_in_expo:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Exponential ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_expo     (gfloat          t);

/**
 * lrg_easing_ease_out_expo:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Exponential ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_expo    (gfloat          t);

/**
 * lrg_easing_ease_in_out_expo:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Exponential ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_expo (gfloat          t);

/**
 * lrg_easing_ease_in_circ:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Circular ease-in.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_circ     (gfloat          t);

/**
 * lrg_easing_ease_out_circ:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Circular ease-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_circ    (gfloat          t);

/**
 * lrg_easing_ease_in_out_circ:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Circular ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_circ (gfloat          t);

/**
 * lrg_easing_ease_in_back:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Back ease-in (overshoots then returns).
 *
 * Returns: The eased value (may go below 0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_back     (gfloat          t);

/**
 * lrg_easing_ease_out_back:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Back ease-out (overshoots then returns).
 *
 * Returns: The eased value (may exceed 1)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_back    (gfloat          t);

/**
 * lrg_easing_ease_in_out_back:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Back ease-in-out (overshoots at both ends).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_back (gfloat          t);

/**
 * lrg_easing_ease_in_elastic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Elastic ease-in (exponentially decaying sine wave).
 *
 * Returns: The eased value (may oscillate)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_elastic  (gfloat          t);

/**
 * lrg_easing_ease_out_elastic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Elastic ease-out (exponentially decaying sine wave).
 *
 * Returns: The eased value (may oscillate)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_elastic (gfloat          t);

/**
 * lrg_easing_ease_in_out_elastic:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Elastic ease-in-out.
 *
 * Returns: The eased value (may oscillate)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_elastic (gfloat       t);

/**
 * lrg_easing_ease_in_bounce:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Bounce ease-in (bouncing effect).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_bounce   (gfloat          t);

/**
 * lrg_easing_ease_out_bounce:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Bounce ease-out (bouncing effect).
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_out_bounce  (gfloat          t);

/**
 * lrg_easing_ease_in_out_bounce:
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Bounce ease-in-out.
 *
 * Returns: The eased value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_ease_in_out_bounce (gfloat        t);

/**
 * lrg_easing_interpolate:
 * @type: The easing function type
 * @from: Start value
 * @to: End value
 * @t: Normalized time value (0.0 to 1.0)
 *
 * Interpolates between two values using the specified easing function.
 * Convenience function that combines easing with linear interpolation.
 *
 * Returns: The interpolated value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_easing_interpolate      (LrgEasingType   type,
                                             gfloat          from,
                                             gfloat          to,
                                             gfloat          t);

G_END_DECLS
