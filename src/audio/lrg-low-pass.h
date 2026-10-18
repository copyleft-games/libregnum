/* lrg-low-pass.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A second-order (12 dB per octave) resonant low-pass filter, the
 * "biquad" of Robert Bristow-Johnson's Audio EQ Cookbook, with a gliding
 * cut-off: underwater muffling, sounds heard through walls.
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
 * LRG_LOW_PASS_OPEN:
 *
 * A cut-off (Hz) at or above which the filter is fully open: once the
 * glide reaches it the samples pass untouched.
 */
#define LRG_LOW_PASS_OPEN (20000.0f)

/**
 * LRG_LOW_PASS_MIN_CUTOFF:
 *
 * The lowest cut-off in Hz.
 */
#define LRG_LOW_PASS_MIN_CUTOFF (20.0f)

#define LRG_TYPE_LOW_PASS (lrg_low_pass_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLowPass, lrg_low_pass, LRG, LOW_PASS, LrgAudioEffect)

/**
 * lrg_low_pass_new:
 * @cutoff: the cut-off frequency in Hz (%LRG_LOW_PASS_OPEN or more: open)
 *
 * Returns: (transfer full): a new #LrgLowPass starting at @cutoff
 *   (no glide from an earlier value)
 */
LRG_AVAILABLE_IN_ALL
LrgLowPass * lrg_low_pass_new (gfloat cutoff);

/**
 * lrg_low_pass_set_cutoff:
 * @self: an #LrgLowPass
 * @cutoff: the target cut-off in Hz, clamped to
 *   %LRG_LOW_PASS_MIN_CUTOFF .. %LRG_LOW_PASS_OPEN
 *
 * The filter glides to the new cut-off exponentially over about
 * lrg_low_pass_get_glide() seconds, so diving or opening a door never
 * clicks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_low_pass_set_cutoff (LrgLowPass *self,
                              gfloat      cutoff);

/**
 * lrg_low_pass_get_cutoff:
 * @self: an #LrgLowPass
 *
 * Returns: the target cut-off in Hz
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_low_pass_get_cutoff (LrgLowPass *self);

/**
 * lrg_low_pass_get_current_cutoff:
 * @self: an #LrgLowPass
 *
 * Returns: the cut-off the last processed block ended at (Hz)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_low_pass_get_current_cutoff (LrgLowPass *self);

/**
 * lrg_low_pass_set_resonance:
 * @self: an #LrgLowPass
 * @q: the filter's Q, 0.1 .. 10 (0.7071 is flat, Butterworth)
 */
LRG_AVAILABLE_IN_ALL
void lrg_low_pass_set_resonance (LrgLowPass *self,
                                 gfloat      q);

/**
 * lrg_low_pass_get_resonance:
 * @self: an #LrgLowPass
 *
 * Returns: the Q
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_low_pass_get_resonance (LrgLowPass *self);

/**
 * lrg_low_pass_set_glide:
 * @self: an #LrgLowPass
 * @seconds: time constant of cut-off changes, 0 (instant) .. 5
 */
LRG_AVAILABLE_IN_ALL
void lrg_low_pass_set_glide (LrgLowPass *self,
                             gfloat      seconds);

/**
 * lrg_low_pass_get_glide:
 * @self: an #LrgLowPass
 *
 * Returns: the glide time constant in seconds (default 0.08)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_low_pass_get_glide (LrgLowPass *self);

G_END_DECLS
