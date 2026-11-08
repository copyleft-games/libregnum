/* lrg-synth.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A polyphonic subtractive and FM synthesizer: two anti-aliased
 * oscillators with unison and a supersaw, a sub oscillator and noise, a
 * resonant state-variable filter with its own envelope and key tracking,
 * two LFOs, glide and a mono/legato mode.  Every parameter is a double
 * GObject property; lrg_synth_load_preset() sets a whole sound
 * ("synthwave-bass", "synthwave-pad", ...).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-instrument.h"

G_BEGIN_DECLS

/**
 * LrgSynthWave:
 * @LRG_SYNTH_WAVE_SINE: a sine
 * @LRG_SYNTH_WAVE_TRIANGLE: a triangle
 * @LRG_SYNTH_WAVE_SAW: a band-limited saw
 * @LRG_SYNTH_WAVE_PULSE: a band-limited pulse (width `oscN-pw`)
 * @LRG_SYNTH_WAVE_NOISE: white noise
 * @LRG_SYNTH_WAVE_SUPERSAW: seven or more detuned saws
 *
 * The oscillator waveforms; the `osc1-wave` and `osc2-wave` parameters
 * hold these values.
 */
typedef enum
{
	LRG_SYNTH_WAVE_SINE,
	LRG_SYNTH_WAVE_TRIANGLE,
	LRG_SYNTH_WAVE_SAW,
	LRG_SYNTH_WAVE_PULSE,
	LRG_SYNTH_WAVE_NOISE,
	LRG_SYNTH_WAVE_SUPERSAW
} LrgSynthWave;

LRG_AVAILABLE_IN_ALL
GType lrg_synth_wave_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SYNTH_WAVE (lrg_synth_wave_get_type ())

#define LRG_TYPE_SYNTH (lrg_synth_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSynth, lrg_synth, LRG, SYNTH, LrgInstrument)

/**
 * lrg_synth_new:
 *
 * A synthesizer with the "init" sound: one saw through an open filter.
 *
 * Returns: (transfer full): a new #LrgSynth
 */
LRG_AVAILABLE_IN_ALL
LrgSynth * lrg_synth_new (void);

/**
 * lrg_synth_new_preset:
 * @name: a preset name (lrg_synth_list_presets())
 *
 * Returns: (transfer full) (nullable): a synthesizer with the preset, or
 *   %NULL for an unknown name
 */
LRG_AVAILABLE_IN_ALL
LrgSynth * lrg_synth_new_preset (const gchar *name);

/**
 * lrg_synth_load_preset:
 * @self: an #LrgSynth
 * @name: a preset name
 *
 * Resets every parameter to its default, then applies the preset.
 *
 * Returns: %TRUE for a known preset
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_synth_load_preset (LrgSynth    *self,
                                const gchar *name);

/**
 * lrg_synth_list_presets:
 *
 * Returns: (transfer full) (array zero-terminated=1): the preset names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_synth_list_presets (void);

/**
 * lrg_synth_get_preset_description:
 * @name: a preset name
 *
 * Returns: (transfer none) (nullable): a one-line description
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_synth_get_preset_description (const gchar *name);

G_END_DECLS
