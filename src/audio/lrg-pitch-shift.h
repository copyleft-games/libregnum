/* lrg-pitch-shift.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A pitch shifter that keeps the duration: deeper or higher voices
 * (monsters, the undead, helium), in real time on a voice or a bus.
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

/**
 * LRG_PITCH_SHIFT_MIN_RATIO:
 *
 * The lowest pitch ratio (an octave down).
 */
#define LRG_PITCH_SHIFT_MIN_RATIO (0.5f)

/**
 * LRG_PITCH_SHIFT_MAX_RATIO:
 *
 * The highest pitch ratio (an octave up).
 */
#define LRG_PITCH_SHIFT_MAX_RATIO (2.0f)

/**
 * LRG_PITCH_SHIFT_MIN_WINDOW:
 *
 * The shortest grain window in seconds.
 */
#define LRG_PITCH_SHIFT_MIN_WINDOW (0.005f)

/**
 * LRG_PITCH_SHIFT_MAX_WINDOW:
 *
 * The longest grain window in seconds.
 */
#define LRG_PITCH_SHIFT_MAX_WINDOW (0.2f)

#define LRG_TYPE_PITCH_SHIFT (lrg_pitch_shift_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPitchShift, lrg_pitch_shift, LRG, PITCH_SHIFT, LrgAudioEffect)

/**
 * lrg_pitch_shift_new:
 * @ratio: the pitch ratio (1 unchanged, 0.5 an octave down, 2 an octave
 *   up), clamped to %LRG_PITCH_SHIFT_MIN_RATIO .. %LRG_PITCH_SHIFT_MAX_RATIO
 *
 * Returns: (transfer full): a new #LrgPitchShift with a 40 ms window
 */
LRG_AVAILABLE_IN_ALL
LrgPitchShift * lrg_pitch_shift_new (gfloat ratio);

/**
 * lrg_pitch_shift_set_ratio:
 * @self: an #LrgPitchShift
 * @ratio: the pitch ratio, clamped to %LRG_PITCH_SHIFT_MIN_RATIO ..
 *   %LRG_PITCH_SHIFT_MAX_RATIO; exactly 1 passes the samples untouched
 */
LRG_AVAILABLE_IN_ALL
void lrg_pitch_shift_set_ratio (LrgPitchShift *self,
                                gfloat         ratio);

/**
 * lrg_pitch_shift_get_ratio:
 * @self: an #LrgPitchShift
 *
 * Returns: the pitch ratio
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_pitch_shift_get_ratio (LrgPitchShift *self);

/**
 * lrg_pitch_shift_set_window:
 * @self: an #LrgPitchShift
 * @window: the grain window in seconds, clamped to
 *   %LRG_PITCH_SHIFT_MIN_WINDOW .. %LRG_PITCH_SHIFT_MAX_WINDOW
 *
 * Longer windows smear transients less into a flutter but delay the
 * output more (about half a window).  A new window forgets the line.
 */
LRG_AVAILABLE_IN_ALL
void lrg_pitch_shift_set_window (LrgPitchShift *self,
                                 gfloat         window);

/**
 * lrg_pitch_shift_get_window:
 * @self: an #LrgPitchShift
 *
 * Returns: the grain window in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_pitch_shift_get_window (LrgPitchShift *self);

G_END_DECLS
