/* lrg-audio-analysis.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Level and loudness measurements of rendered audio: sample peak, true
 * peak (4x oversampled), RMS, DC offset, and loudness after ITU-R
 * BS.1770-4 / EBU R128 (integrated, momentary and short-term maximum,
 * loudness range).  Used to normalise mixdowns and to let tools (and AI
 * agents) judge a mix by numbers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-wave-data.h"

G_BEGIN_DECLS

/**
 * LRG_AUDIO_LOUDNESS_SILENCE:
 *
 * The loudness (LUFS) and level (dBFS) reported for silence or a clip
 * too short to measure.
 */
#define LRG_AUDIO_LOUDNESS_SILENCE (-144.0)

/**
 * LrgAudioAnalysis:
 * @frames: frames measured
 * @channels: channels measured
 * @sample_rate: their rate
 * @duration: seconds
 * @peak_db: the largest absolute sample, dBFS
 * @true_peak_db: the largest inter-sample peak (4x oversampled), dBTP
 * @rms_db: the root mean square over every channel, dBFS
 * @dc_offset: the mean sample value (largest of the channels, signed)
 * @integrated_lufs: gated integrated loudness (BS.1770-4), LUFS
 * @momentary_max_lufs: the loudest 400 ms window, LUFS
 * @short_term_max_lufs: the loudest 3 s window, LUFS
 * @loudness_range: EBU R128 loudness range (LRA), LU
 * @clipped_samples: samples at or beyond full scale
 *
 * The measurements of lrg_audio_analyze().  Loudness needs at least
 * 400 ms (the integrated value) or 3 s (the short-term maximum and the
 * range); shorter clips report %LRG_AUDIO_LOUDNESS_SILENCE there.
 */
typedef struct
{
	guint64 frames;
	guint   channels;
	guint   sample_rate;
	gdouble duration;
	gdouble peak_db;
	gdouble true_peak_db;
	gdouble rms_db;
	gdouble dc_offset;
	gdouble integrated_lufs;
	gdouble momentary_max_lufs;
	gdouble short_term_max_lufs;
	gdouble loudness_range;
	guint64 clipped_samples;

	/*< private >*/
	gpointer _reserved[4];
} LrgAudioAnalysis;

LRG_AVAILABLE_IN_ALL
GType lrg_audio_analysis_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUDIO_ANALYSIS (lrg_audio_analysis_get_type ())

/**
 * lrg_audio_analyze:
 * @samples: (array length=n_samples): interleaved float samples
 * @n_samples: the number of samples (frames x @channels)
 * @channels: 1 .. 8; for loudness every channel counts with weight 1,
 *   except 6-channel (5.1) input, where the fourth (LFE) is left out and
 *   the fifth and sixth (surrounds) weigh 1.41, as BS.1770 asks
 * @sample_rate: 8000 .. 384000
 *
 * Measures the buffer.
 *
 * Returns: (transfer full) (nullable): the measurements, or %NULL for a
 *   bad layout
 */
LRG_AVAILABLE_IN_ALL
LrgAudioAnalysis * lrg_audio_analyze (const gfloat *samples,
                                      gsize         n_samples,
                                      guint         channels,
                                      guint         sample_rate);

/**
 * lrg_audio_analyze_wave:
 * @wave: a recording
 *
 * Returns: (transfer full) (nullable): the measurements of @wave
 */
LRG_AVAILABLE_IN_ALL
LrgAudioAnalysis * lrg_audio_analyze_wave (LrgWaveData *wave);

/**
 * lrg_audio_analysis_copy:
 * @analysis: measurements
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgAudioAnalysis * lrg_audio_analysis_copy (const LrgAudioAnalysis *analysis);

/**
 * lrg_audio_analysis_free:
 * @analysis: measurements
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_analysis_free (LrgAudioAnalysis *analysis);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAudioAnalysis, lrg_audio_analysis_free)

/**
 * lrg_audio_analysis_normalize_gain:
 * @analysis: measurements
 * @loudness: %TRUE to reach @target as integrated loudness (LUFS),
 *   %FALSE as sample peak (dBFS)
 * @target: the target
 * @true_peak_ceiling: for loudness: the highest true peak (dBTP) allowed;
 *   the gain is lowered to respect it
 *
 * Returns: the linear gain that normalises the measured audio (1 for
 *   silence)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_audio_analysis_normalize_gain (const LrgAudioAnalysis *analysis,
                                           gboolean                loudness,
                                           gdouble                 target,
                                           gdouble                 true_peak_ceiling);

G_END_DECLS
