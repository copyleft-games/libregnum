/* lrg-reel-audio-analysis.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-audio-analysis.h"
#include "../audio/lrg-wave-data.h"
#include <math.h>

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

/*
 * next_pow2:
 * @n: a positive integer
 *
 * Returns the smallest power of two that is >= @n.
 */
static guint
next_pow2 (guint n)
{
    guint p;

    if (n == 0)
        return 1;

    p = 1;
    while (p < n)
        p <<= 1;

    return p;
}

/*
 * bit_reverse_index:
 * @x: index to reverse
 * @log2n: log2 of the FFT size (number of bits)
 *
 * Reverses the bottom @log2n bits of @x.
 */
static guint
bit_reverse_index (guint x,
                   guint log2n)
{
    guint result;
    guint i;

    result = 0;
    for (i = 0; i < log2n; i++)
    {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }

    return result;
}

/*
 * fft_inplace:
 * @re: real parts (length N, N must be a power of two)
 * @im: imaginary parts (length N)
 * @N: FFT size (power of two)
 *
 * In-place iterative radix-2 Cooley-Tukey DIT FFT.
 *
 * After return, re[k] and im[k] hold the real and imaginary parts of
 * the k-th DFT bin.  The twiddle factors are computed on the fly to
 * avoid allocating a separate table.
 *
 * Mental correctness check: a pure cosine of frequency k0 cycles per
 * N samples, stored in @re with @im all zeros, produces a peak at bin
 * k0 (and its mirror at N-k0) with magnitude N/2.  After scaling by
 * 2/window_size (applied in the caller), the returned magnitude is 1.0,
 * matching the unit-amplitude cosine.
 */
static void
fft_inplace (gdouble *re,
             gdouble *im,
             guint    N)
{
    guint log2n;
    guint i;
    guint len;

    /* --- bit-reversal permutation ---------------------------------------- */
    log2n = 0;
    {
        guint tmp;
        tmp = N;
        while (tmp > 1) { tmp >>= 1; log2n++; }
    }

    for (i = 0; i < N; i++)
    {
        guint j;
        j = bit_reverse_index (i, log2n);
        if (j > i)
        {
            gdouble tr, ti;
            tr = re[i]; re[i] = re[j]; re[j] = tr;
            ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    /* --- Cooley-Tukey butterfly passes ------------------------------------ */
    for (len = 2; len <= N; len <<= 1)
    {
        guint half_len;
        gdouble ang;
        gdouble wr_step, wi_step;
        guint i2;

        half_len = len >> 1;
        ang      = -2.0 * G_PI / (gdouble) len;
        wr_step  = cos (ang);
        wi_step  = sin (ang);

        for (i2 = 0; i2 < N; i2 += len)
        {
            gdouble wr, wi;
            guint j2;

            wr = 1.0;
            wi = 0.0;

            for (j2 = 0; j2 < half_len; j2++)
            {
                guint u_idx, v_idx;
                gdouble ur, ui, vr, vi, tmp_wr;

                u_idx = i2 + j2;
                v_idx = i2 + j2 + half_len;

                ur = re[u_idx];
                ui = im[u_idx];
                vr = wr * re[v_idx] - wi * im[v_idx];
                vi = wr * im[v_idx] + wi * re[v_idx];

                re[u_idx] = ur + vr;
                im[u_idx] = ui + vi;
                re[v_idx] = ur - vr;
                im[v_idx] = ui - vi;

                /* Advance twiddle factor via multiplication */
                tmp_wr = wr * wr_step - wi * wi_step;
                wi     = wr * wi_step + wi * wr_step;
                wr     = tmp_wr;
            }
        }
    }
}

/*
 * build_mono_window:
 * @samples: interleaved float samples from lrg_wave_data_get_samples()
 * @total_samples: total number of interleaved samples
 * @channels: number of channels
 * @start_frame: first frame index to read (may be < 0, clamped to 0)
 * @window_frames: number of audio frames to place in the window
 * @out_buf: (array length=window_frames): caller-allocated output buffer
 *
 * Fills @out_buf with @window_frames mono (averaged) samples beginning
 * at @start_frame.  Frames outside [0, total_frames) are written as 0.
 */
static void
build_mono_window (const gfloat *samples,
                   gsize         total_samples,
                   guint         channels,
                   gsize         start_frame,
                   guint         window_frames,
                   gdouble      *out_buf)
{
    guint n;
    gsize total_frames;

    total_frames = (channels > 0) ? (total_samples / channels) : 0;

    for (n = 0; n < window_frames; n++)
    {
        gsize frame_idx;
        gdouble mono;
        guint c;

        frame_idx = start_frame + (gsize) n;

        if (frame_idx >= total_frames)
        {
            out_buf[n] = 0.0;
            continue;
        }

        mono = 0.0;
        for (c = 0; c < channels; c++)
            mono += (gdouble) samples[frame_idx * channels + c];

        out_buf[n] = mono / (gdouble) channels;
    }
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_reel_audio_spectrum:
 * @wave: an #LrgWaveData
 * @frame: video frame index (0-based)
 * @fps: frames per second of the video (must be > 0)
 * @n_bins: number of frequency bins to return (must be > 0)
 * @window_size: number of audio samples in the analysis window (must be > 0)
 * @out_count: (out): set to @n_bins on success, or 0 on failure
 *
 * Computes the FFT magnitude spectrum for the audio window centred on
 * @frame.
 *
 * Returns: (transfer full) (array length=out_count) (nullable):
 *   A newly allocated array of @n_bins magnitude values, or %NULL on
 *   invalid input.  Free with g_free().
 */
gfloat *
lrg_reel_audio_spectrum (LrgWaveData *wave,
                          gint         frame,
                          gdouble      fps,
                          guint        n_bins,
                          guint        window_size,
                          gsize       *out_count)
{
    gfloat  *samples;
    gsize    total_samples;
    guint    channels;
    guint    sample_rate;
    guint    N;
    gdouble *re;
    gdouble *im;
    gfloat  *result;
    gsize    center_sample;
    gsize    start_frame;
    guint    half_window;
    guint    n;
    guint    k;

    /* --- Validate inputs -------------------------------------------------- */
    if (wave == NULL || n_bins == 0 || window_size == 0 || fps <= 0.0)
    {
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    /* --- Fetch raw samples (transfer full) -------------------------------- */
    samples = lrg_wave_data_get_samples (wave, &total_samples);
    if (samples == NULL || total_samples == 0)
    {
        g_free (samples);
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    channels    = lrg_wave_data_get_channels (wave);
    sample_rate = lrg_wave_data_get_sample_rate (wave);

    if (channels == 0 || sample_rate == 0)
    {
        g_free (samples);
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    /* --- Determine window start sample ------------------------------------ */
    center_sample = (gsize) round ((gdouble) frame / fps * (gdouble) sample_rate);
    half_window   = window_size / 2;

    if (center_sample >= (gsize) half_window)
        start_frame = center_sample - (gsize) half_window;
    else
        start_frame = 0;

    /* --- Allocate FFT buffers --------------------------------------------- */
    N  = next_pow2 (window_size);
    re = g_new0 (gdouble, N);
    im = g_new0 (gdouble, N);

    /* --- Fill real part: Hann-windowed mono samples ----------------------- */
    {
        gdouble *mono_window;
        mono_window = g_new0 (gdouble, window_size);

        build_mono_window (samples, total_samples, channels,
                           start_frame, window_size, mono_window);

        for (n = 0; n < window_size; n++)
        {
            gdouble w;
            w     = 0.5 * (1.0 - cos (2.0 * G_PI * (gdouble) n
                                       / (gdouble) (window_size - 1)));
            re[n] = mono_window[n] * w;
        }
        /* Indices [window_size, N) remain zero (zero-padding). */

        g_free (mono_window);
    }

    g_free (samples);

    /* --- FFT -------------------------------------------------------------- */
    fft_inplace (re, im, N);

    /* --- Compute magnitudes and pack result ------------------------------- */
    result = g_new0 (gfloat, n_bins);

    {
        guint available_bins;
        gdouble scale;

        available_bins = N / 2;
        scale          = 2.0 / (gdouble) window_size;

        for (k = 0; k < n_bins; k++)
        {
            if (k < available_bins)
            {
                gdouble mag;
                mag       = sqrt (re[k] * re[k] + im[k] * im[k]) * scale;
                result[k] = (gfloat) mag;
            }
            /* else: already zero from g_new0 */
        }
    }

    g_free (re);
    g_free (im);

    if (out_count != NULL)
        *out_count = (gsize) n_bins;

    return result;
}

/**
 * lrg_reel_audio_waveform:
 * @wave: an #LrgWaveData
 * @frame: video frame index (0-based)
 * @fps: frames per second of the video (must be > 0)
 * @seconds: length of the audio window in seconds (must be > 0)
 * @n_samples: number of output waveform samples to return (must be > 0)
 * @out_count: (out): set to @n_samples on success, or 0 on failure
 *
 * Returns a down-sampled peak-amplitude waveform for the audio window.
 *
 * Returns: (transfer full) (array length=out_count) (nullable):
 *   A newly allocated array of @n_samples values in [-1, 1], or %NULL on
 *   invalid input.  Free with g_free().
 */
gfloat *
lrg_reel_audio_waveform (LrgWaveData *wave,
                          gint         frame,
                          gdouble      fps,
                          gdouble      seconds,
                          guint        n_samples,
                          gsize       *out_count)
{
    gfloat  *samples;
    gsize    total_samples;
    guint    channels;
    guint    sample_rate;
    gsize    start_frame;
    gsize    window_frames;
    gsize    total_frames;
    gfloat  *result;
    guint    i;

    /* --- Validate inputs -------------------------------------------------- */
    if (wave == NULL || n_samples == 0 || fps <= 0.0 || seconds <= 0.0)
    {
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    /* --- Fetch raw samples (transfer full) -------------------------------- */
    samples = lrg_wave_data_get_samples (wave, &total_samples);
    if (samples == NULL || total_samples == 0)
    {
        g_free (samples);
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    channels    = lrg_wave_data_get_channels (wave);
    sample_rate = lrg_wave_data_get_sample_rate (wave);

    if (channels == 0 || sample_rate == 0)
    {
        g_free (samples);
        if (out_count != NULL)
            *out_count = 0;
        return NULL;
    }

    total_frames  = total_samples / channels;
    start_frame   = (gsize) round ((gdouble) frame / fps * (gdouble) sample_rate);
    window_frames = (gsize) round (seconds * (gdouble) sample_rate);

    if (window_frames == 0)
        window_frames = 1;

    /* --- Allocate output -------------------------------------------------- */
    result = g_new0 (gfloat, n_samples);

    /* --- Down-sample into buckets ----------------------------------------- */
    for (i = 0; i < n_samples; i++)
    {
        gsize bucket_start;
        gsize bucket_end;
        gsize f;
        gdouble peak_abs;
        gdouble mean_sign_sum;
        gsize   count_in_range;

        bucket_start = start_frame
                       + (gsize) ((gdouble) i       * (gdouble) window_frames / (gdouble) n_samples);
        bucket_end   = start_frame
                       + (gsize) ((gdouble) (i + 1) * (gdouble) window_frames / (gdouble) n_samples);

        if (bucket_start >= bucket_end)
            bucket_end = bucket_start + 1;

        peak_abs      = 0.0;
        mean_sign_sum = 0.0;
        count_in_range = 0;

        for (f = bucket_start; f < bucket_end; f++)
        {
            guint c;
            gdouble mono;

            if (f >= total_frames)
                break;

            mono = 0.0;
            for (c = 0; c < channels; c++)
                mono += (gdouble) samples[f * channels + c];
            mono /= (gdouble) channels;

            if (fabs (mono) > peak_abs)
                peak_abs = fabs (mono);

            mean_sign_sum += mono;
            count_in_range++;
        }

        if (count_in_range > 0)
        {
            gdouble sign_val;
            sign_val = (mean_sign_sum >= 0.0) ? 1.0 : -1.0;
            if (peak_abs > 1.0)
                peak_abs = 1.0;
            result[i] = (gfloat) (peak_abs * sign_val);
        }
        /* else: out of range, result[i] stays 0.0 (from g_new0) */
    }

    g_free (samples);

    if (out_count != NULL)
        *out_count = (gsize) n_samples;

    return result;
}

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
 * Returns: RMS level in [0.0, 1.0], or 0.0 on invalid input.
 */
gdouble
lrg_reel_audio_level (LrgWaveData *wave,
                       gint         frame,
                       gdouble      fps,
                       gdouble      seconds)
{
    gfloat  *samples;
    gsize    total_samples;
    guint    channels;
    guint    sample_rate;
    gsize    start_frame;
    gsize    window_frames;
    gsize    total_frames;
    gdouble  sum_sq;
    gsize    count;
    gsize    f;
    gdouble  rms;

    /* --- Validate inputs -------------------------------------------------- */
    if (wave == NULL || fps <= 0.0 || seconds <= 0.0)
        return 0.0;

    /* --- Fetch raw samples (transfer full) -------------------------------- */
    samples = lrg_wave_data_get_samples (wave, &total_samples);
    if (samples == NULL || total_samples == 0)
    {
        g_free (samples);
        return 0.0;
    }

    channels    = lrg_wave_data_get_channels (wave);
    sample_rate = lrg_wave_data_get_sample_rate (wave);

    if (channels == 0 || sample_rate == 0)
    {
        g_free (samples);
        return 0.0;
    }

    total_frames  = total_samples / channels;
    start_frame   = (gsize) round ((gdouble) frame / fps * (gdouble) sample_rate);
    window_frames = (gsize) round (seconds * (gdouble) sample_rate);

    if (window_frames == 0)
        window_frames = 1;

    /* --- Accumulate RMS --------------------------------------------------- */
    sum_sq = 0.0;
    count  = 0;

    for (f = start_frame; f < start_frame + window_frames; f++)
    {
        guint c;
        gdouble mono;

        if (f >= total_frames)
            break;

        mono = 0.0;
        for (c = 0; c < channels; c++)
            mono += (gdouble) samples[f * channels + c];
        mono /= (gdouble) channels;

        sum_sq += mono * mono;
        count++;
    }

    g_free (samples);

    if (count == 0)
        return 0.0;

    rms = sqrt (sum_sq / (gdouble) count);

    if (rms > 1.0)
        rms = 1.0;

    return rms;
}
