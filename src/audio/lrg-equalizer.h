/* lrg-equalizer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A six-band equalizer: low cut, low shelf, two parametric peaks, high
 * shelf and high cut, each a cookbook biquad.  A band at 0 dB (or a cut
 * at 0 Hz) is skipped, so a flat equalizer passes the samples untouched.
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

#define LRG_TYPE_EQUALIZER (lrg_equalizer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEqualizer, lrg_equalizer, LRG, EQUALIZER, LrgAudioEffect)

/**
 * lrg_equalizer_new:
 *
 * Creates the effect; every parameter is also a GObject property (#LrgEqualizer:low-cut, #LrgEqualizer:low-freq, #LrgEqualizer:low-gain, #LrgEqualizer:mid1-freq, #LrgEqualizer:mid1-gain, #LrgEqualizer:mid1-q, #LrgEqualizer:mid2-freq, #LrgEqualizer:mid2-gain, #LrgEqualizer:mid2-q, #LrgEqualizer:high-freq, #LrgEqualizer:high-gain, #LrgEqualizer:high-cut).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgEqualizer
 */
LRG_AVAILABLE_IN_ALL
LrgEqualizer * lrg_equalizer_new (void);

G_END_DECLS
