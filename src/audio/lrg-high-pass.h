/* lrg-high-pass.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A second-order (12 dB per octave) resonant high-pass filter, the
 * "biquad" of Robert Bristow-Johnson's Audio EQ Cookbook, with a gliding
 * cut-off: thin radio and telephone voices, small speakers, rumble
 * removal.  The counterpart of #LrgLowPass.
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
 * LRG_HIGH_PASS_OPEN:
 *
 * A cut-off (Hz) at or below which the filter is fully open: once the
 * glide reaches it the samples pass untouched.  It is also the lowest
 * cut-off.
 */
#define LRG_HIGH_PASS_OPEN (20.0f)

/**
 * LRG_HIGH_PASS_MAX_CUTOFF:
 *
 * The highest cut-off in Hz.
 */
#define LRG_HIGH_PASS_MAX_CUTOFF (20000.0f)

#define LRG_TYPE_HIGH_PASS (lrg_high_pass_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHighPass, lrg_high_pass, LRG, HIGH_PASS, LrgAudioEffect)

/**
 * lrg_high_pass_new:
 * @cutoff: the cut-off frequency in Hz (%LRG_HIGH_PASS_OPEN or less: open)
 *
 * Returns: (transfer full): a new #LrgHighPass starting at @cutoff
 *   (no glide from an earlier value)
 */
LRG_AVAILABLE_IN_ALL
LrgHighPass * lrg_high_pass_new (gfloat cutoff);

/**
 * lrg_high_pass_set_cutoff:
 * @self: an #LrgHighPass
 * @cutoff: the target cut-off in Hz, clamped to
 *   %LRG_HIGH_PASS_OPEN .. %LRG_HIGH_PASS_MAX_CUTOFF
 *
 * The filter glides to the new cut-off exponentially over about
 * lrg_high_pass_get_glide() seconds, so a change never clicks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_high_pass_set_cutoff (LrgHighPass *self,
                               gfloat       cutoff);

/**
 * lrg_high_pass_get_cutoff:
 * @self: an #LrgHighPass
 *
 * Returns: the target cut-off in Hz
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_high_pass_get_cutoff (LrgHighPass *self);

/**
 * lrg_high_pass_get_current_cutoff:
 * @self: an #LrgHighPass
 *
 * Returns: the cut-off the last processed block ended at (Hz)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_high_pass_get_current_cutoff (LrgHighPass *self);

/**
 * lrg_high_pass_set_resonance:
 * @self: an #LrgHighPass
 * @q: the filter's Q, 0.1 .. 10 (0.7071 is flat, Butterworth)
 */
LRG_AVAILABLE_IN_ALL
void lrg_high_pass_set_resonance (LrgHighPass *self,
                                  gfloat       q);

/**
 * lrg_high_pass_get_resonance:
 * @self: an #LrgHighPass
 *
 * Returns: the Q
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_high_pass_get_resonance (LrgHighPass *self);

/**
 * lrg_high_pass_set_glide:
 * @self: an #LrgHighPass
 * @seconds: time constant of cut-off changes, 0 (instant) .. 5
 */
LRG_AVAILABLE_IN_ALL
void lrg_high_pass_set_glide (LrgHighPass *self,
                              gfloat       seconds);

/**
 * lrg_high_pass_get_glide:
 * @self: an #LrgHighPass
 *
 * Returns: the glide time constant in seconds (default 0.08)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_high_pass_get_glide (LrgHighPass *self);

G_END_DECLS
