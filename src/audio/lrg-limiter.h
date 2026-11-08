/* lrg-limiter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A look-ahead brick-wall peak limiter for the end of a master chain:
 * the output never goes above the ceiling, and the gain moves early
 * enough (the audio is delayed by the look-ahead) that it never clicks.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-audio-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIMITER (lrg_limiter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLimiter, lrg_limiter, LRG, LIMITER, LrgAudioEffect)

/**
 * lrg_limiter_new:
 * @ceiling_db: the highest output level in dBFS (-24 .. 0)
 *
 * Creates the effect; every parameter is also a GObject property (#LrgLimiter:ceiling, #LrgLimiter:release, #LrgLimiter:lookahead).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgLimiter
 */
LRG_AVAILABLE_IN_ALL
LrgLimiter * lrg_limiter_new (gdouble ceiling_db);

/**
 * lrg_limiter_get_gain_reduction:
 * @self: an #LrgLimiter
 *
 * Returns: the deepest gain reduction of the last processed block in dB
 *   (0 or negative)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_limiter_get_gain_reduction (LrgLimiter *self);

/**
 * lrg_limiter_get_latency:
 * @self: an #LrgLimiter
 * @sample_rate: a rate in Hz
 *
 * Returns: the delay the limiter adds at @sample_rate, in frames
 */
LRG_AVAILABLE_IN_ALL
guint lrg_limiter_get_latency (LrgLimiter *self,
                               guint       sample_rate);

G_END_DECLS
