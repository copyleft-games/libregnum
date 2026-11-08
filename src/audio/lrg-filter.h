/* lrg-filter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A general cookbook biquad: low-pass, high-pass, band-pass, notch,
 * peak and shelves, with 1 to 4 cascaded stages for steeper slopes
 * (12 to 48 dB per octave).  Automating its frequency makes the classic
 * filter sweep.
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
 * LrgFilterMode:
 * @LRG_FILTER_MODE_LOWPASS: passes below the frequency
 * @LRG_FILTER_MODE_HIGHPASS: passes above the frequency
 * @LRG_FILTER_MODE_BANDPASS: passes a band around the frequency
 * @LRG_FILTER_MODE_NOTCH: removes a band around the frequency
 * @LRG_FILTER_MODE_PEAK: boosts or cuts a band by the gain
 * @LRG_FILTER_MODE_LOWSHELF: boosts or cuts below the frequency
 * @LRG_FILTER_MODE_HIGHSHELF: boosts or cuts above the frequency
 *
 * The response of an #LrgFilter.
 */
typedef enum
{
	LRG_FILTER_MODE_LOWPASS,
	LRG_FILTER_MODE_HIGHPASS,
	LRG_FILTER_MODE_BANDPASS,
	LRG_FILTER_MODE_NOTCH,
	LRG_FILTER_MODE_PEAK,
	LRG_FILTER_MODE_LOWSHELF,
	LRG_FILTER_MODE_HIGHSHELF
} LrgFilterMode;

LRG_AVAILABLE_IN_ALL
GType lrg_filter_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FILTER_MODE (lrg_filter_mode_get_type ())

#define LRG_TYPE_FILTER (lrg_filter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFilter, lrg_filter, LRG, FILTER, LrgAudioEffect)

/**
 * lrg_filter_new:
 * @mode: the response
 * @frequency: the corner or centre frequency in Hz
 * @q: the quality (0.1 .. 20)
 *
 * Creates the effect; every parameter is also a GObject property (#LrgFilter:frequency, #LrgFilter:q, #LrgFilter:gain, #LrgFilter:stages, #LrgFilter:mix).
 * Out-of-range arguments are clamped.
 *
 * Returns: (transfer full): a new #LrgFilter
 */
LRG_AVAILABLE_IN_ALL
LrgFilter * lrg_filter_new (LrgFilterMode mode,
                            gdouble frequency,
                            gdouble q);

G_END_DECLS
