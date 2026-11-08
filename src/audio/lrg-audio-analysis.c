/* lrg-audio-analysis.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Level and loudness measurements (see lrg-audio-analysis.h and
 * docs/modules/audio/analysis.org).
 *
 * Loudness follows ITU-R BS.1770-4:
 *   1. every channel through the K-weighting filter (a high shelf of
 *      about +4 dB above 1.5 kHz, then a 38 Hz high-pass), with the
 *      coefficients derived for any rate the way libebur128 does;
 *   2. mean squares over 100 ms segments, weighted per channel (1 for
 *      front channels, 1.41 for surrounds);
 *   3. 400 ms blocks with 75 % overlap (four segments) give momentary
 *      loudness; the integrated loudness averages the blocks above the
 *      absolute gate (-70 LUFS) and above the relative gate (10 LU under
 *      the absolute-gated mean);
 *   4. 3 s windows every 100 ms give short-term loudness; the loudness
 *      range (EBU Tech 3342) is the spread between the 10th and 95th
 *      percentile of the windows above -70 LUFS and 20 LU under their
 *      mean.
 *   L = -0.691 + 10 log10 (sum).
 *
 * True peak: each channel is upsampled 4x by a 48-tap windowed-sinc
 * polyphase filter (12 taps per phase) and the largest absolute value is
 * kept (BS.1770-4 Annex 2).
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "lrg-audio-analysis.h"

#define TP_PHASES (4)
#define TP_TAPS   (12)    /* per phase */
#define MAX_CH    (8)

G_DEFINE_BOXED_TYPE (LrgAudioAnalysis, lrg_audio_analysis, lrg_audio_analysis_copy, lrg_audio_analysis_free)

LrgAudioAnalysis *
lrg_audio_analysis_copy (const LrgAudioAnalysis *analysis)
{
	g_return_val_if_fail (analysis != NULL, NULL);
	return g_memdup2 (analysis, sizeof *analysis);
}

void
lrg_audio_analysis_free (LrgAudioAnalysis *analysis)
{
	g_free (analysis);
}

static gdouble
to_db (gdouble lin)
{
	return lin > 1e-7 ? 20.0 * log10 (lin) : LRG_AUDIO_LOUDNESS_SILENCE;
}

static gdouble
to_lufs (gdouble z)
{
	return z > 1e-15 ? -0.691 + 10.0 * log10 (z) : LRG_AUDIO_LOUDNESS_SILENCE;
}

/* ==========================================================================
 * K-weighting
 * ========================================================================== */

typedef struct
{
	gdouble b[3], a[3];
} Biquad;

static void
k_weighting (guint   rate,
             Biquad *shelf,
             Biquad *hp)
{
	gdouble f0 = 1681.974450955533;
	gdouble g = 3.999843853973347;
	gdouble q = 0.7071752369554196;
	gdouble k = tan (G_PI * f0 / rate);
	gdouble vh = pow (10.0, g / 20.0);
	gdouble vb = pow (vh, 0.4996667741545416);
	gdouble a0 = 1.0 + k / q + k * k;

	shelf->b[0] = (vh + vb * k / q + k * k) / a0;
	shelf->b[1] = 2.0 * (k * k - vh) / a0;
	shelf->b[2] = (vh - vb * k / q + k * k) / a0;
	shelf->a[0] = 1.0;
	shelf->a[1] = 2.0 * (k * k - 1.0) / a0;
	shelf->a[2] = (1.0 - k / q + k * k) / a0;

	f0 = 38.13547087602444;
	q = 0.5003270373238773;
	k = tan (G_PI * f0 / rate);
	a0 = 1.0 + k / q + k * k;
	hp->b[0] = 1.0;
	hp->b[1] = -2.0;
	hp->b[2] = 1.0;
	hp->a[0] = 1.0;
	hp->a[1] = 2.0 * (k * k - 1.0) / a0;
	hp->a[2] = (1.0 - k / q + k * k) / a0;
}

/* channel weights of BS.1770 for the common layouts */
static gdouble
channel_weight (guint channels,
                guint c)
{
	if (channels == 6)
	{
		if (c == 3)
			return 0.0;          /* the LFE does not count */
		if (c >= 4)
			return 1.41;         /* the surrounds */
	}
	return 1.0;
}

/* ==========================================================================
 * True peak
 * ========================================================================== */

static void
tp_filter (gdouble taps[TP_PHASES][TP_TAPS])
{
	guint   n = TP_PHASES * TP_TAPS;
	guint   i;
	gdouble sum[TP_PHASES] = { 0 };

	for (i = 0; i < n; i++)
	{
		gdouble x = ((gdouble) i - (gdouble) (n - 1) / 2.0) / TP_PHASES;
		gdouble sinc = x == 0.0 ? 1.0 : sin (G_PI * x) / (G_PI * x);
		gdouble w = 0.5 - 0.5 * cos (2.0 * G_PI * ((gdouble) i + 0.5) / n);   /* Hann */

		taps[i % TP_PHASES][i / TP_PHASES] = sinc * w;
	}

	/* every phase at unity DC gain */
	for (i = 0; i < n; i++)
		sum[i % TP_PHASES] += taps[i % TP_PHASES][i / TP_PHASES];
	for (i = 0; i < n; i++)
		taps[i % TP_PHASES][i / TP_PHASES] /= sum[i % TP_PHASES];
}

/* ==========================================================================
 * Analysis
 * ========================================================================== */

static int
cmp_double (const void *a,
            const void *b)
{
	gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

	return x < y ? -1 : x > y ? 1 : 0;
}

LrgAudioAnalysis *
lrg_audio_analyze (const gfloat *samples,
                   gsize         n_samples,
                   guint         channels,
                   guint         sample_rate)
{
	LrgAudioAnalysis *r;
	Biquad            shelf, hp;
	gdouble           taps[TP_PHASES][TP_TAPS];
	gdouble           s1[MAX_CH][2], s2[MAX_CH][2];       /* biquad states */
	gdouble           hist[MAX_CH][TP_TAPS];             /* true-peak history */
	gdouble           sum[MAX_CH], sq[MAX_CH];
	gdouble           peak = 0.0, tpeak = 0.0;
	guint             seg_frames;
	gsize             frames, f, n_segs, i;
	g_autofree gdouble *segs = NULL;
	guint             c, k, ph;

	g_return_val_if_fail (samples != NULL || n_samples == 0, NULL);
	if (channels == 0 || channels > MAX_CH || sample_rate < 8000 || sample_rate > 384000
	    || n_samples % channels != 0)
		return NULL;

	r = g_new0 (LrgAudioAnalysis, 1);
	frames = n_samples / channels;
	r->frames = frames;
	r->channels = channels;
	r->sample_rate = sample_rate;
	r->duration = (gdouble) frames / sample_rate;
	k_weighting (sample_rate, &shelf, &hp);
	tp_filter (taps);
	memset (s1, 0, sizeof s1);
	memset (s2, 0, sizeof s2);
	memset (hist, 0, sizeof hist);
	memset (sum, 0, sizeof sum);
	memset (sq, 0, sizeof sq);
	seg_frames = MAX (sample_rate / 10, 1u);
	n_segs = frames / seg_frames;
	segs = g_new0 (gdouble, n_segs + 1);

	/* one pass: levels, K-weighted segment energies, true peak */
	for (f = 0; f < frames; f++)
	{
		gsize seg = f / seg_frames;

		for (c = 0; c < channels; c++)
		{
			gdouble x = (gdouble) samples[f * channels + c];
			gdouble y, z;
			gdouble w;

			if (!isfinite (x))
				x = 0.0;
			sum[c] += x;
			sq[c] += x * x;
			if (fabs (x) > peak)
				peak = fabs (x);
			if (fabs (x) >= 1.0)
				r->clipped_samples++;

			/* K-weighting: shelf then high-pass (direct form I with
			 * transposed state) */
			y = shelf.b[0] * x + s1[c][0];
			s1[c][0] = shelf.b[1] * x - shelf.a[1] * y + s1[c][1];
			s1[c][1] = shelf.b[2] * x - shelf.a[2] * y;
			z = hp.b[0] * y + s2[c][0];
			s2[c][0] = hp.b[1] * y - hp.a[1] * z + s2[c][1];
			s2[c][1] = hp.b[2] * y - hp.a[2] * z;
			w = channel_weight (channels, c);
			if (seg < n_segs && w > 0.0)
				segs[seg] += w * z * z;

			/* true peak: push into the history, evaluate four phases */
			memmove (&hist[c][1], &hist[c][0], (TP_TAPS - 1) * sizeof (gdouble));
			hist[c][0] = x;
			for (ph = 0; ph < TP_PHASES; ph++)
			{
				gdouble acc = 0.0;

				for (k = 0; k < TP_TAPS; k++)
					acc += taps[ph][k] * hist[c][k];
				if (fabs (acc) > tpeak)
					tpeak = fabs (acc);
			}
		}
	}
	tpeak = MAX (tpeak, peak);

	r->peak_db = to_db (peak);
	r->true_peak_db = to_db (tpeak);
	{
		gdouble all = 0.0;
		gdouble dc = 0.0;

		for (c = 0; c < channels; c++)
		{
			all += sq[c];
			if (frames > 0 && fabs (sum[c] / frames) > fabs (dc))
				dc = sum[c] / frames;
		}
		r->rms_db = frames > 0 ? to_db (sqrt (all / ((gdouble) frames * channels))) : LRG_AUDIO_LOUDNESS_SILENCE;
		r->dc_offset = dc;
	}

	/* the gated loudness */
	r->integrated_lufs = LRG_AUDIO_LOUDNESS_SILENCE;
	r->momentary_max_lufs = LRG_AUDIO_LOUDNESS_SILENCE;
	r->short_term_max_lufs = LRG_AUDIO_LOUDNESS_SILENCE;
	r->loudness_range = 0.0;
	for (i = 0; i < n_segs; i++)
		segs[i] /= (gdouble) seg_frames;
	if (n_segs >= 4)
	{
		gsize    n_blocks = n_segs - 3;
		gdouble *blocks = g_new (gdouble, n_blocks);
		gdouble  abs_sum = 0.0, rel_sum = 0.0, gate;
		gsize    abs_n = 0, rel_n = 0;

		for (i = 0; i < n_blocks; i++)
		{
			blocks[i] = (segs[i] + segs[i + 1] + segs[i + 2] + segs[i + 3]) / 4.0;
			r->momentary_max_lufs = MAX (r->momentary_max_lufs, to_lufs (blocks[i]));
			if (to_lufs (blocks[i]) > -70.0)
			{
				abs_sum += blocks[i];
				abs_n++;
			}
		}
		if (abs_n > 0)
		{
			gate = to_lufs (abs_sum / abs_n) - 10.0;
			for (i = 0; i < n_blocks; i++)
				if (to_lufs (blocks[i]) > -70.0 && to_lufs (blocks[i]) > gate)
				{
					rel_sum += blocks[i];
					rel_n++;
				}
			if (rel_n > 0)
				r->integrated_lufs = to_lufs (rel_sum / rel_n);
		}
		g_free (blocks);
	}
	if (n_segs >= 30)
	{
		gsize    n_st = n_segs - 29;
		gdouble *st = g_new (gdouble, n_st);
		gdouble *kept = g_new (gdouble, n_st);
		gdouble  abs_sum = 0.0;
		gsize    abs_n = 0, kept_n = 0;
		gdouble  window = 0.0;

		for (i = 0; i < 30; i++)
			window += segs[i];
		for (i = 0; i < n_st; i++)
		{
			if (i > 0)
				window += segs[i + 29] - segs[i - 1];
			st[i] = MAX (window / 30.0, 0.0);
			r->short_term_max_lufs = MAX (r->short_term_max_lufs, to_lufs (st[i]));
			if (to_lufs (st[i]) > -70.0)
			{
				abs_sum += st[i];
				abs_n++;
			}
		}
		if (abs_n > 0)
		{
			gdouble gate = to_lufs (abs_sum / abs_n) - 20.0;

			for (i = 0; i < n_st; i++)
				if (to_lufs (st[i]) > -70.0 && to_lufs (st[i]) > gate)
					kept[kept_n++] = to_lufs (st[i]);
			if (kept_n > 1)
			{
				qsort (kept, kept_n, sizeof (gdouble), cmp_double);
				r->loudness_range = kept[(gsize) llround (0.95 * (kept_n - 1))]
				                    - kept[(gsize) llround (0.10 * (kept_n - 1))];
			}
		}
		g_free (st);
		g_free (kept);
	}
	return r;
}

LrgAudioAnalysis *
lrg_audio_analyze_wave (LrgWaveData *wave)
{
	g_autofree gfloat *samples = NULL;
	gsize              count = 0;

	g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), NULL);
	samples = lrg_wave_data_get_samples (wave, &count);
	if (samples == NULL)
		return NULL;
	return lrg_audio_analyze (samples, count, lrg_wave_data_get_channels (wave),
	                          lrg_wave_data_get_sample_rate (wave));
}

gdouble
lrg_audio_analysis_normalize_gain (const LrgAudioAnalysis *analysis,
                                   gboolean                loudness,
                                   gdouble                 target,
                                   gdouble                 true_peak_ceiling)
{
	gdouble gain_db;

	g_return_val_if_fail (analysis != NULL, 1.0);
	if (!loudness)
	{
		if (analysis->peak_db <= LRG_AUDIO_LOUDNESS_SILENCE)
			return 1.0;
		return pow (10.0, (target - analysis->peak_db) / 20.0);
	}
	if (analysis->integrated_lufs <= LRG_AUDIO_LOUDNESS_SILENCE)
	{
		/* too short or too quiet for a loudness: fall back to the peak */
		if (analysis->peak_db <= LRG_AUDIO_LOUDNESS_SILENCE)
			return 1.0;
		return pow (10.0, (true_peak_ceiling - analysis->true_peak_db) / 20.0);
	}
	gain_db = target - analysis->integrated_lufs;
	if (analysis->true_peak_db + gain_db > true_peak_ceiling)
		gain_db = true_peak_ceiling - analysis->true_peak_db;
	return pow (10.0, gain_db / 20.0);
}
