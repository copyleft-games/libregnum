/* lrg-reel-audio-analysis.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Audio analysis helpers for the Reel video-composition module.
 *
 * These are free functions (no GObject type) that operate on an
 * #LrgWaveData object and produce per-frame analysis results suitable
 * for audiograms and music visualizers.
 *
 * Three analyses are provided:
 *
 *  - lrg_reel_audio_spectrum(): Hann-windowed radix-2 FFT magnitude bins.
 *  - lrg_reel_audio_waveform(): Down-sampled peak-amplitude waveform.
 *  - lrg_reel_audio_level():    Single RMS level value.
 *
 * All functions down-mix multi-channel audio to mono by averaging
 * channels before analysis.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_reel_audio_spectrum:
 * @wave: an #LrgWaveData
 * @frame: video frame index (0-based)
 * @fps: frames per second of the video (must be > 0)
 * @n_bins: number of frequency bins to return (must be > 0)
 * @window_size: number of audio samples in the analysis window (must be > 0);
 *   the window is centred on the sample corresponding to @frame.  If not a
 *   power of two it is rounded up to the next power of two for the FFT.
 * @out_count: (out): set to @n_bins on success, or 0 on failure
 *
 * Computes the FFT magnitude spectrum for the audio window centred on
 * @frame.
 *
 * The audio window of @window_size samples is extracted from @wave
 * (mono-mixed, Hann-windowed) and transformed with a radix-2
 * Cooley-Tukey FFT.  The magnitude of each bin k is:
 *
 *   magnitude[k] = sqrt(re[k]^2 + im[k]^2) * (2 / window_size)
 *
 * for k in [0, N/2) where N is the next power-of-two &gt;= @window_size.
 * If @n_bins &lt; N/2 only the first @n_bins bins are returned; if
 * @n_bins &gt; N/2 the extra entries are zero-filled.
 *
 * Samples that fall outside the audio buffer are treated as silence
 * (zero-padded).
 *
 * Returns: (transfer full) (array length=out_count) (nullable):
 *   A newly allocated array of @n_bins magnitude values, or %NULL if
 *   @wave is %NULL or any parameter is invalid.  Free with g_free().
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_reel_audio_spectrum (LrgWaveData *wave,
                                   gint         frame,
                                   gdouble      fps,
                                   guint        n_bins,
                                   guint        window_size,
                                   gsize       *out_count);

/**
 * lrg_reel_audio_waveform:
 * @wave: an #LrgWaveData
 * @frame: video frame index (0-based)
 * @fps: frames per second of the video (must be > 0)
 * @seconds: length of the audio window in seconds (must be > 0)
 * @n_samples: number of output waveform samples to return (must be > 0)
 * @out_count: (out): set to @n_samples on success, or 0 on failure
 *
 * Returns a down-sampled waveform for the audio window of @seconds
 * starting at @frame.
 *
 * The audio is first down-mixed to mono.  The window is divided into
 * @n_samples equal-sized buckets; each output value is the peak
 * absolute amplitude within that bucket, signed by the mean sign of
 * the samples in the bucket.  Values lie in [-1.0, 1.0].  Buckets
 * that fall outside the audio buffer are returned as 0.0.
 *
 * Returns: (transfer full) (array length=out_count) (nullable):
 *   A newly allocated array of @n_samples values in [-1, 1], or %NULL
 *   if @wave is %NULL or any parameter is invalid.  Free with g_free().
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_reel_audio_waveform (LrgWaveData *wave,
                                   gint         frame,
                                   gdouble      fps,
                                   gdouble      seconds,
                                   guint        n_samples,
                                   gsize       *out_count);

/**
 * lrg_reel_audio_level:
 * @wave: an #LrgWaveData
 * @frame: video frame index (0-based)
 * @fps: frames per second of the video (must be > 0)
 * @seconds: length of the audio window in seconds (must be > 0)
 *
 * Computes the overall RMS level for the audio window of @seconds
 * starting at @frame.
 *
 * The audio is down-mixed to mono and the root-mean-square of all
 * samples in the window is returned, clamped to [0.0, 1.0].  If
 * @wave is %NULL, @fps &lt;= 0, @seconds &lt;= 0, or the window falls
 * entirely outside the buffer, 0.0 is returned.
 *
 * Returns: RMS level in [0.0, 1.0]
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_reel_audio_level (LrgWaveData *wave,
                               gint         frame,
                               gdouble      fps,
                               gdouble      seconds);

G_END_DECLS
