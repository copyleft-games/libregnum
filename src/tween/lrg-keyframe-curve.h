/* lrg-keyframe-curve.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pure, clock-free multi-keyframe value sampler for offline/headless rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-easing.h"

G_BEGIN_DECLS

#define LRG_TYPE_KEYFRAME_CURVE (lrg_keyframe_curve_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgKeyframeCurve, lrg_keyframe_curve, LRG, KEYFRAME_CURVE, GObject)

/**
 * lrg_keyframe_curve_new:
 *
 * Creates a new, empty keyframe curve.
 *
 * An #LrgKeyframeCurve is a pure, clock-free sampler that maps a normalized
 * time value @t (typically in [0,1]) to an interpolated output value across
 * one or more keyframes.  It is designed for offline / headless animation
 * baking — the caller drives time by iterating over frames; no
 * #LrgTweenManager or game loop is required.
 *
 * Returns: (transfer full): A new #LrgKeyframeCurve
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve *  lrg_keyframe_curve_new          (void);

/**
 * lrg_keyframe_curve_add_key:
 * @self: an #LrgKeyframeCurve
 * @t: normalized time of this keyframe, typically in [0.0, 1.0]
 * @value: the value at this keyframe
 * @ease_to_next: easing applied when interpolating from this key to the next
 *
 * Adds a keyframe at normalized time @t.
 *
 * Keys are stored sorted by @t in ascending order.  If a key already exists at
 * exactly the same @t the new entry overwrites it (last-write-wins for
 * duplicate times).  The @ease_to_next parameter controls the easing function
 * used in the segment from this key to the following one; it is ignored for
 * the last key in the curve.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_keyframe_curve_add_key      (LrgKeyframeCurve *self,
                                                     gfloat            t,
                                                     gfloat            value,
                                                     LrgEasingType     ease_to_next);

/**
 * lrg_keyframe_curve_sample:
 * @self: an #LrgKeyframeCurve
 * @t: normalized time to evaluate
 *
 * Samples the curve at normalized time @t.
 *
 * Behaviour contracts:
 * - Zero keys: returns 0.0 (and triggers a g_return_val_if_fail warning).
 * - One key:   returns that key's value for all @t.
 * - @t before the first key: returns the first key's value (clamped).
 * - @t after the last key:   returns the last key's value (clamped).
 * - @t outside [0,1] is clamped to [first-key-t, last-key-t].
 * - Between two adjacent keys: local segment @t' is computed and
 *   lrg_easing_interpolate() is applied using the left key's ease_to_next.
 *
 * This function is pure and has no side effects; it is safe to call from any
 * thread as long as no concurrent mutations occur.
 *
 * Returns: the interpolated value at @t
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_keyframe_curve_sample       (LrgKeyframeCurve *self,
                                                     gfloat            t);

/**
 * lrg_keyframe_curve_get_key_count:
 * @self: an #LrgKeyframeCurve
 *
 * Returns the number of keyframes currently stored in the curve.
 *
 * Returns: the number of keyframes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_keyframe_curve_get_key_count (LrgKeyframeCurve *self);

G_END_DECLS
