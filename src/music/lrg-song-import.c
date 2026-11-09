/* lrg-song-import.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Recordings into songs (see lrg-song-import.h and
 * docs/modules/music/import.org).
 *
 * The pipeline:
 *
 *   1. decode (lrg_audio_decode_file()) and resample to the song rate
 *      with a Blackman-windowed sinc;
 *   2. analyse a mono copy at ANA_RATE: every HOP samples a short FFT
 *      gives the onset strength (log spectral flux) overall and in three
 *      bands (kick, snare, hat) and the frame energy; every CHROMA_HOPS
 *      hops a long FFT gives the twelve pitch classes and, when
 *      transcribing, the salience of every melody note;
 *   3. tempo: autocorrelation of the onset envelope with a prior around
 *      120 BPM, then a joint search of tempo and phase whose comb of beats
 *      best matches the onsets over the whole recording; the downbeat is
 *      the beat of the bar where the kick band hits hardest;
 *   4. the aligned WAV: silence before the recording so that a bar starts
 *      at beat 0, played by one clip on the audio track "original";
 *   5. sections: bar features (chroma, loudness, brightness), a novelty
 *      curve from a checkerboard kernel over their similarity, peaks at
 *      least section_bars apart; similar sections share a letter;
 *   6. transcription (optional): drum steps per sixteenth from the band
 *      onsets, the bass and the melody as the notes of greatest harmonic
 *      salience (the bass by its upper harmonics), chords by
 *      matching chroma to the 24 triads; identical bars share a pattern.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <glib/gstdio.h>
#include <errno.h>

#include "lrg-song-import.h"
#include "../audio/lrg-audio-encoder.h"
#include "../audio/lrg-audio-dsp-private.h"
#include "../audio/lrg-mixer-asset.h"
#include "../audio/lrg-wave-data.h"
#include "lrg-instrument.h"

#define ANA_RATE      (24000)
#define HOP           (256)         /* 10.7 ms at ANA_RATE */
#define FFT_SHORT     (1024)
#define FFT_LONG      (8192)        /* 2.9 Hz bins for pitch */
#define CHROMA_HOPS   (4)
#define MEL_LOW       (55)          /* G3 */
#define MEL_HIGH      (84)          /* C6 */
#define N_MEL         (MEL_HIGH - MEL_LOW + 1)
#define SINC_ZEROS    (16)
#define SINC_RES      (512)

/* ==========================================================================
 * Options and report
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgSongImportOptions, lrg_song_import_options, lrg_song_import_options_copy,
                     lrg_song_import_options_free)
G_DEFINE_BOXED_TYPE (LrgSongImportReport, lrg_song_import_report, lrg_song_import_report_copy,
                     lrg_song_import_report_free)

LrgSongImportOptions *
lrg_song_import_options_new (void)
{
	LrgSongImportOptions *o = g_new0 (LrgSongImportOptions, 1);

	o->sample_rate = 48000;
	o->min_bpm = 70.0;
	o->max_bpm = 180.0;
	o->beats_per_bar = 4;
	o->sections = TRUE;
	o->section_bars = 4;
	return o;
}

LrgSongImportOptions *
lrg_song_import_options_copy (const LrgSongImportOptions *options)
{
	LrgSongImportOptions *o;

	g_return_val_if_fail (options != NULL, NULL);
	o = g_memdup2 (options, sizeof *options);
	o->title = g_strdup (options->title);
	o->artist = g_strdup (options->artist);
	o->audio_dir = g_strdup (options->audio_dir);
	return o;
}

void
lrg_song_import_options_free (LrgSongImportOptions *options)
{
	if (options == NULL)
		return;
	g_free (options->title);
	g_free (options->artist);
	g_free (options->audio_dir);
	g_free (options);
}

LrgSongImportReport *
lrg_song_import_report_copy (const LrgSongImportReport *report)
{
	LrgSongImportReport *r;

	g_return_val_if_fail (report != NULL, NULL);
	r = g_memdup2 (report, sizeof *report);
	r->key = g_strdup (report->key);
	r->audio_file = g_strdup (report->audio_file);
	return r;
}

void
lrg_song_import_report_free (LrgSongImportReport *report)
{
	if (report == NULL)
		return;
	g_free (report->key);
	g_free (report->audio_file);
	g_free (report);
}

/* ==========================================================================
 * Resampling: Blackman-windowed sinc
 * ========================================================================== */

/* one side of the kernel, SINC_RES points per zero crossing */
static const gfloat *
sinc_table (void)
{
	static gfloat *table = NULL;
	guint          i;

	if (g_once_init_enter_pointer (&table))
	{
		gfloat *t = g_new (gfloat, SINC_ZEROS * SINC_RES + 2);

		for (i = 0; i <= SINC_ZEROS * SINC_RES + 1; i++)
		{
			gdouble x = (gdouble) i / SINC_RES;
			gdouble s = x == 0.0 ? 1.0 : sin (G_PI * x) / (G_PI * x);
			gdouble w = x >= SINC_ZEROS ? 0.0
			            : 0.42 + 0.5 * cos (G_PI * x / SINC_ZEROS) + 0.08 * cos (2.0 * G_PI * x / SINC_ZEROS);

			t[i] = (gfloat) (s * w);
		}
		g_once_init_leave_pointer (&table, t);
	}
	return table;
}

/*
 * resample:
 * @in: interleaved frames at @rate_in; returns them at @rate_out (and
 * their count in @out_frames).  Downsampling lowers the cut-off to the new
 * Nyquist so nothing folds back.
 */
static gfloat *
resample (const gfloat *in,
          gsize         frames,
          guint         channels,
          guint         rate_in,
          guint         rate_out,
          gsize        *out_frames)
{
	const gfloat *table = sinc_table ();
	gdouble       ratio = (gdouble) rate_out / (gdouble) rate_in;
	gdouble       fc = MIN (1.0, ratio);
	gdouble       half = SINC_ZEROS / fc;
	gsize         n = (gsize) floor ((gdouble) frames * ratio);
	gfloat       *out;
	gsize         i;
	guint         c;

	*out_frames = n;
	if (rate_in == rate_out)
		return g_memdup2 (in, frames * channels * sizeof (gfloat));
	out = g_new0 (gfloat, n * channels + 1);
	for (i = 0; i < n; i++)
	{
		gdouble t = (gdouble) i / ratio;
		glong   first = (glong) ceil (t - half);
		glong   last = (glong) floor (t + half);
		gdouble acc[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		glong   j;

		first = MAX (first, 0);
		last = MIN (last, (glong) frames - 1);
		for (j = first; j <= last; j++)
		{
			gdouble x = fabs ((gdouble) j - t) * fc * SINC_RES;
			gsize   k = (gsize) x;
			gdouble fr = x - (gdouble) k;
			gdouble w;

			if (k >= (gsize) SINC_ZEROS * SINC_RES)
				continue;
			w = (table[k] + (table[k + 1] - table[k]) * fr) * fc;
			for (c = 0; c < channels && c < 8; c++)
				acc[c] += w * in[(gsize) j * channels + c];
		}
		for (c = 0; c < channels && c < 8; c++)
			out[i * channels + c] = (gfloat) acc[c];
	}
	return out;
}

/* ==========================================================================
 * FFT (radix 2, in place)
 * ========================================================================== */

typedef struct
{
	guint   n;
	guint  *rev;
	gfloat *cos_t;
	gfloat *sin_t;
	gfloat *window;
	gfloat *re;
	gfloat *im;
} Fft;

static void
fft_init (Fft  *f,
          guint n)
{
	guint bits = 0, i, j;

	f->n = n;
	while ((1u << bits) < n)
		bits++;
	f->rev = g_new (guint, n);
	f->cos_t = g_new (gfloat, n / 2);
	f->sin_t = g_new (gfloat, n / 2);
	f->window = g_new (gfloat, n);
	f->re = g_new (gfloat, n);
	f->im = g_new (gfloat, n);
	for (i = 0; i < n; i++)
	{
		guint r = 0;

		for (j = 0; j < bits; j++)
			if (i & (1u << j))
				r |= 1u << (bits - 1 - j);
		f->rev[i] = r;
		f->window[i] = (gfloat) (0.5 - 0.5 * cos (2.0 * G_PI * i / n));
	}
	for (i = 0; i < n / 2; i++)
	{
		f->cos_t[i] = (gfloat) cos (2.0 * G_PI * i / n);
		f->sin_t[i] = (gfloat) -sin (2.0 * G_PI * i / n);
	}
}

static void
fft_clear (Fft *f)
{
	g_free (f->rev);
	g_free (f->cos_t);
	g_free (f->sin_t);
	g_free (f->window);
	g_free (f->re);
	g_free (f->im);
}

/* magnitudes (n/2 + 1) of the Hann-windowed frame starting at @x[start]
 * (zeros past the end) */
static void
fft_magnitudes (Fft          *f,
                const gfloat *x,
                gsize         len,
                glong         start,
                gfloat       *mag)
{
	guint n = f->n, i, size, half, step, k;

	for (i = 0; i < n; i++)
	{
		glong  p = start + (glong) i;
		gfloat v = (p >= 0 && (gsize) p < len) ? x[p] : 0.0f;

		f->re[f->rev[i]] = v * f->window[i];
		f->im[f->rev[i]] = 0.0f;
	}
	for (size = 2; size <= n; size *= 2)
	{
		half = size / 2;
		step = n / size;
		for (i = 0; i < n; i += size)
			for (k = 0; k < half; k++)
			{
				gfloat wr = f->cos_t[k * step], wi = f->sin_t[k * step];
				guint  a = i + k, b = i + k + half;
				gfloat tr = f->re[b] * wr - f->im[b] * wi;
				gfloat ti = f->re[b] * wi + f->im[b] * wr;

				f->re[b] = f->re[a] - tr;
				f->im[b] = f->im[a] - ti;
				f->re[a] += tr;
				f->im[a] += ti;
			}
	}
	for (i = 0; i <= n / 2; i++)
		mag[i] = sqrtf (f->re[i] * f->re[i] + f->im[i] * f->im[i]);
}

/* ==========================================================================
 * Analysis
 * ========================================================================== */

typedef struct
{
	gsize   n;            /* hop frames */
	gdouble hop_sec;
	gfloat *onset;        /* overall flux, local mean removed, >= 0 */
	gfloat *kick;         /* band onsets, likewise */
	gfloat *snare;
	gfloat *hat;
	gfloat *rms;          /* frame energy */
	gfloat *bright;       /* high / total energy */
	gsize   nc;           /* chroma frames (every CHROMA_HOPS hops) */
	gfloat *chroma;       /* nc x 12, each frame sums to 1 (or 0) */
	gfloat *bass;         /* nc x 12: pitch classes below 250 Hz (energy) */
	gfloat *sal;          /* nc x N_MEL, or NULL */
	gfloat *mono;         /* the mono signal at ANA_RATE */
	gsize   mono_len;
} Ana;

static void
ana_clear (Ana *a)
{
	g_free (a->onset);
	g_free (a->kick);
	g_free (a->snare);
	g_free (a->hat);
	g_free (a->rms);
	g_free (a->bright);
	g_free (a->chroma);
	g_free (a->bass);
	g_free (a->sal);
	g_free (a->mono);
	memset (a, 0, sizeof *a);
}

/* removes a 0.4 s moving average and clips at 0: onsets, not loudness */
static void
detrend (gfloat *x,
         gsize   n,
         guint   win)
{
	g_autofree gfloat *avg = g_new (gfloat, n + 1);
	gdouble            sum = 0.0;
	gsize              i;

	for (i = 0; i < n; i++)
	{
		sum += x[i];
		if (i >= win)
			sum -= x[i - win];
		avg[i] = (gfloat) (sum / (gdouble) MIN (i + 1, (gsize) win));
	}
	for (i = 0; i < n; i++)
	{
		/* centre the average on the frame */
		gsize   c = MIN (i + win / 2, n - 1);
		gdouble v = x[i] - avg[c];

		x[i] = (gfloat) MAX (v, 0.0);
	}
}

/* the mono mix of interleaved frames, at ANA_RATE */
static gfloat *
to_mono_ana (const gfloat *samples,
             gsize         frames,
             guint         channels,
             guint         rate,
             gsize        *out_len)
{
	g_autofree gfloat *mono = g_new (gfloat, frames + 1);
	gsize              i;
	guint              c;

	for (i = 0; i < frames; i++)
	{
		gdouble s = 0.0;

		for (c = 0; c < channels; c++)
			s += samples[i * channels + c];
		mono[i] = (gfloat) (s / channels);
	}
	return resample (mono, frames, 1, rate, ANA_RATE, out_len);
}

static void
analyse (Ana          *a,
         const gfloat *samples,
         gsize         frames,
         guint         channels,
         guint         rate,
         gboolean      salience)
{
	Fft                f_short, f_long;
	g_autofree gfloat *mag = g_new (gfloat, FFT_SHORT / 2 + 1);
	g_autofree gfloat *prev = g_new0 (gfloat, FFT_SHORT / 2 + 1);
	g_autofree gfloat *prev_lin = g_new0 (gfloat, FFT_SHORT / 2 + 1);
	g_autofree gfloat *lmag = g_new (gfloat, FFT_LONG / 2 + 1);
	gdouble            bin_hz = (gdouble) ANA_RATE / FFT_SHORT;
	gdouble            lbin_hz = (gdouble) ANA_RATE / FFT_LONG;
	guint              b_kick = (guint) (100.0 / bin_hz);
	guint              b_snare_lo = (guint) (1000.0 / bin_hz), b_snare_hi = (guint) (5000.0 / bin_hz);
	guint              b_hat = (guint) (7000.0 / bin_hz);
	gsize              i, k;

	memset (a, 0, sizeof *a);
	a->mono = to_mono_ana (samples, frames, channels, rate, &a->mono_len);
	a->hop_sec = (gdouble) HOP / ANA_RATE;
	a->n = a->mono_len / HOP;
	a->onset = g_new0 (gfloat, a->n + 1);
	a->kick = g_new0 (gfloat, a->n + 1);
	a->snare = g_new0 (gfloat, a->n + 1);
	a->hat = g_new0 (gfloat, a->n + 1);
	a->rms = g_new0 (gfloat, a->n + 1);
	a->bright = g_new0 (gfloat, a->n + 1);
	a->nc = a->n / CHROMA_HOPS;
	a->chroma = g_new0 (gfloat, a->nc * 12 + 1);
	a->bass = g_new0 (gfloat, a->nc * 12 + 1);
	a->sal = salience ? g_new0 (gfloat, a->nc * N_MEL + 1) : NULL;

	/* short frames: onsets per band, energy, brightness */
	fft_init (&f_short, FFT_SHORT);
	for (i = 0; i < a->n; i++)
	{
		gdouble flux = 0, fk = 0, fs = 0, fh = 0, e = 0, eh = 0;

		fft_magnitudes (&f_short, a->mono, a->mono_len, (glong) (i * HOP) - FFT_SHORT / 2, mag);
		for (k = 1; k <= FFT_SHORT / 2; k++)
		{
			gdouble lm = log1p (100.0 * mag[k]);
			gdouble d = lm - prev[k];
			gdouble dl = (gdouble) mag[k] - prev_lin[k];

			prev[k] = (gfloat) lm;
			prev_lin[k] = mag[k];
			/* the kick band in linear magnitude: a kick on top of a bass
			 * note must count for more than the bass note alone, which
			 * the log flux compresses away */
			if (k <= b_kick && dl > 0.0)
				fk += dl;
			e += (gdouble) mag[k] * mag[k];
			if (k >= b_hat)
				eh += (gdouble) mag[k] * mag[k];
			if (d <= 0.0)
				continue;
			flux += d;
			if (k >= b_snare_lo && k <= b_snare_hi)
				fs += d;
			else if (k >= b_hat)
				fh += d;
		}
		a->onset[i] = (gfloat) flux;
		a->kick[i] = (gfloat) fk;
		a->snare[i] = (gfloat) fs;
		a->hat[i] = (gfloat) fh;
		a->rms[i] = (gfloat) sqrt (e / (FFT_SHORT / 2));
		a->bright[i] = e > 1e-12 ? (gfloat) (eh / e) : 0.0f;
	}
	fft_clear (&f_short);
	detrend (a->onset, a->n, 38);
	detrend (a->kick, a->n, 38);
	detrend (a->snare, a->n, 38);
	detrend (a->hat, a->n, 38);

	/* long frames: chroma (65 Hz .. 2 kHz) and melody salience */
	fft_init (&f_long, FFT_LONG);
	for (i = 0; i < a->nc; i++)
	{
		gfloat *ch = a->chroma + i * 12;
		gdouble total = 0.0;
		guint   p;

		fft_magnitudes (&f_long, a->mono, a->mono_len, (glong) (i * CHROMA_HOPS * HOP) - FFT_LONG / 2, lmag);
		/* spectral peaks only, at their interpolated frequency: the bins
		 * around a loud low note are a semitone apart and would smear it
		 * into its neighbours.  Below 80 Hz the kick drum's tuned thump
		 * would pass for a bass note, so the range starts above it. */
		for (k = (gsize) (80.0 / lbin_hz); k <= (gsize) (2000.0 / lbin_hz); k++)
		{
			gdouble y0 = lmag[k - 1], y1 = lmag[k], y2 = lmag[k + 1];
			gdouble den, shift, f, sub = 0.0, v;
			gint    pc;
			guint   h;

			if (!(y1 > y0 && y1 >= y2) || y1 <= 1e-6)
				continue;
			den = y0 - 2.0 * y1 + y2;
			shift = den != 0.0 ? CLAMP (0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
			f = ((gdouble) k + shift) * lbin_hz;
			pc = ((gint) floor (69.0 + 12.0 * log2 (f / 440.0) + 0.5) % 12 + 12) % 12;

			/* energy that is the 2nd .. 4th harmonic of a stronger lower
			 * note belongs to that note: without this, every note also
			 * counts as the fifth above it and keys come out a fifth high */
			for (h = 2; h <= 4; h++)
			{
				gsize kb = (gsize) floor (((gdouble) k + shift) / h + 0.5);

				if (kb >= 1)
					sub = MAX (sub, (gdouble) MAX (lmag[kb], MAX (lmag[kb - 1], lmag[kb + 1])) * (h == 3 ? 0.9 : 0.6));
			}
			v = log1p (10.0 * MAX (y1 - sub, 0.0));
			if (f >= 110.0)
			{
				ch[pc] += (gfloat) v;
				total += v;
			}
			/* the bass's pitch classes, by energy, below 260 Hz */
			if (f <= 260.0)
				a->bass[i * 12 + pc] += (gfloat) (y1 * y1);
		}
		for (p = 0; p < 12 && total > 0.0; p++)
			ch[p] = (gfloat) (ch[p] / total);
		if (a->sal == NULL)
			continue;
		for (p = 0; p < N_MEL; p++)
		{
			gdouble f0 = lrg_dsp_key_to_hz (MEL_LOW + p);
			gdouble s = 0.0, w = 1.0;
			guint   h;

			/* harmonic summation over five partials, +- a quarter tone */
			for (h = 1; h <= 5; h++, w *= 0.8)
			{
				gdouble fh = f0 * h;
				gsize   lo = (gsize) (fh * 0.9857 / lbin_hz);
				gsize   hi = (gsize) ceil (fh * 1.0145 / lbin_hz);
				gdouble best = 0.0;
				gsize   b;

				for (b = lo; b <= hi && b <= FFT_LONG / 2; b++)
					best = MAX (best, (gdouble) lmag[b]);
				s += w * best;
			}
			a->sal[i * N_MEL + p] = (gfloat) s;
		}
	}
	fft_clear (&f_long);
}

/* below -60 dBFS: silence (or the dither of it), no music to analyse */
static gboolean
is_silent (const gfloat *x,
           gsize         n)
{
	gsize i;

	for (i = 0; i < n; i++)
		if (fabsf (x[i]) > 0.001f)
			return FALSE;
	return TRUE;
}

/* @x linearly interpolated at fractional index @pos (0 outside) */
static gdouble
at_frac (const gfloat *x,
         gsize         n,
         gdouble       pos)
{
	gsize   i;
	gdouble f;

	if (pos < 0.0 || pos >= (gdouble) (n - 1))
		return 0.0;
	i = (gsize) pos;
	f = pos - (gdouble) i;
	return x[i] + (x[i + 1] - x[i]) * f;
}

/* ==========================================================================
 * Tempo, phase, downbeat
 * ========================================================================== */

typedef struct
{
	gdouble bpm;
	gdouble phase;        /* hop frames to the first beat */
	gdouble confidence;
	gdouble alternative;  /* half or double, when it fits nearly as well; or 0 */
} Tempo;

/* the comb score of beats every @period frames from @phase */
static gdouble
comb (const gfloat *o,
      gsize         n,
      gdouble       period,
      gdouble       phase)
{
	gdouble s = 0.0, t;

	for (t = phase; t < (gdouble) n - 1; t += period)
		s += at_frac (o, n, t);
	return s;
}

/* the autocorrelation of @x at a fractional lag, normalised by lag 0 */
static gdouble
autocorr (const gfloat *x,
          gsize         n,
          gdouble       lag)
{
	gdouble s = 0.0, s0 = 0.0;
	gsize   i;

	for (i = 0; i < n; i++)
	{
		s0 += (gdouble) x[i] * x[i];
		if ((gdouble) i + lag < (gdouble) (n - 1))
			s += x[i] * at_frac (x, n, (gdouble) i + lag);
	}
	return s0 > 0.0 ? s / s0 : 0.0;
}

/*
 * backbeat_octave:
 * Tempo octave errors (half or double) are the usual failure.  Snares and
 * claps mostly land every second beat (the backbeat), so of @bpm, half and
 * double it, prefer the tempo at which the snare band repeats every two
 * beats, then every four; without a clear snare, keep @bpm.
 */
static gdouble
backbeat_octave (const Ana *a,
                 gdouble    bpm,
                 gdouble    min_bpm,
                 gdouble    max_bpm)
{
	static const gdouble mults[4] = { 1.0, 2.0, 4.0, 8.0 };
	static const gdouble pref[4] = { 0.45, 1.0, 0.75, 0.3 };
	gdouble cands[3], best_score = -1.0, best = bpm;
	guint   c, m;

	cands[0] = bpm;
	cands[1] = bpm / 2.0;
	cands[2] = bpm * 2.0;
	for (c = 0; c < 3; c++)
	{
		gdouble period, peak = 0.0, score;
		guint   at = 0;

		if (cands[c] < min_bpm * 0.999 || cands[c] > max_bpm * 1.001)
			continue;
		period = 60.0 / (cands[c] * a->hop_sec);
		for (m = 0; m < 4; m++)
		{
			gdouble r = autocorr (a->snare, a->n, period * mults[m]);

			if (r > peak)
			{
				peak = r;
				at = m;
			}
		}
		/* a weak or flat snare periodicity decides nothing */
		{
			gdouble low = peak;

			for (m = 0; m < 4; m++)
				low = MIN (low, autocorr (a->snare, a->n, period * mults[m]));
			if (peak < 0.08 || peak - low < 0.1)
				return bpm;
		}
		score = pref[at] * peak;
		if (score > best_score)
		{
			best_score = score;
			best = cands[c];
		}
	}
	return best;
}

static gboolean
find_tempo (const Ana *a,
            gdouble    min_bpm,
            gdouble    max_bpm,
            gdouble    fixed_bpm,
            Tempo     *out)
{
	gdouble  best_bpm = fixed_bpm, best = -1.0, mean, sum_sq = 0, prominence = -1.0;
	gdouble  period, phase, best_phase = 0.0, phase_sum = 0.0;
	gsize    lag_min, lag_max, lag, best_lag = 0, nph = 0, i;
	gdouble *r;

	if (a->n < (gsize) (4.0 / a->hop_sec))
		return FALSE;
	for (i = 0; i < a->n; i++)
		sum_sq += (gdouble) a->onset[i] * a->onset[i];
	if (sum_sq <= 1e-9)
		return FALSE;

	if (fixed_bpm <= 0.0)
	{
		/* autocorrelation with a log-normal prior around 120 BPM */
		lag_min = (gsize) floor (60.0 / (max_bpm * a->hop_sec));
		lag_max = (gsize) ceil (60.0 / (min_bpm * a->hop_sec));
		r = g_new0 (gdouble, 2 * lag_max + 2);
		for (lag = 1; lag <= 2 * lag_max && lag < a->n; lag++)
		{
			gdouble s = 0.0;

			for (i = 0; i + lag < a->n; i++)
				s += (gdouble) a->onset[i] * a->onset[i + lag];
			r[lag] = s / (gdouble) (a->n - lag);
		}
		for (lag = MAX (lag_min, 2); lag <= lag_max; lag++)
		{
			gdouble bpm = 60.0 / (lag * a->hop_sec);
			gdouble prior = exp (-0.5 * pow (log2 (bpm / 120.0) / 0.9, 2.0));
			gdouble s = prior * (r[lag] + 0.5 * r[MIN (2 * lag, 2 * lag_max)]);

			if (s > best)
			{
				best = s;
				best_lag = lag;
			}
		}
		{
			/* parabolic peak */
			gdouble y0 = r[best_lag - 1], y1 = r[best_lag], y2 = r[best_lag + 1];
			gdouble d = (y0 - 2.0 * y1 + y2) != 0.0 ? 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2) : 0.0;

			best_bpm = 60.0 / (((gdouble) best_lag + CLAMP (d, -0.5, 0.5)) * a->hop_sec);
		}
		{
			/* how far the winning period stands above the others */
			gdouble avg = 0.0;
			gsize   cnt = 0;

			for (lag = MAX (lag_min, 2); lag <= lag_max; lag++, cnt++)
				avg += r[lag];
			avg = cnt > 0 ? avg / cnt : 0.0;
			prominence = avg > 0.0 ? r[best_lag] / avg : 0.0;
		}
		g_free (r);
		best_bpm = backbeat_octave (a, best_bpm, min_bpm, max_bpm);
	}

	/* joint tempo and phase: the comb that fits the whole recording */
	{
		gdouble lo = fixed_bpm > 0.0 ? fixed_bpm : best_bpm * 0.98;
		gdouble hi = fixed_bpm > 0.0 ? fixed_bpm : best_bpm * 1.02;
		gdouble step = fixed_bpm > 0.0 ? 1.0 : MAX ((hi - lo) / 200.0, 0.005);
		gdouble bpm;

		best = -1.0;
		for (bpm = lo; bpm <= hi + 1e-9; bpm += step)
		{
			period = 60.0 / (bpm * a->hop_sec);
			for (phase = 0.0; phase < period; phase += 0.5)
			{
				gdouble s = comb (a->onset, a->n, period, phase) / ((gdouble) a->n / period);

				if (s > best)
				{
					best = s;
					best_bpm = bpm;
					best_phase = phase;
				}
			}
		}
	}

	/* confidence: the best phase against the average phase */
	period = 60.0 / (best_bpm * a->hop_sec);
	for (phase = 0.0; phase < period; phase += 0.5, nph++)
		phase_sum += comb (a->onset, a->n, period, phase) / ((gdouble) a->n / period);
	mean = nph > 0 ? phase_sum / nph : 0.0;
	out->bpm = best_bpm;
	out->phase = best_phase;
	out->alternative = 0.0;
	if (fixed_bpm <= 0.0)
	{
		/* the other tempo octave, by its mean onset strength on the beat:
		 * half time keeps the strongest beats (nearly as good: 90 %),
		 * double time adds off-beats, naturally weaker (75 %) */
		static const gdouble factor[2] = { 0.5, 2.0 };
		static const gdouble need[2] = { 0.95, 0.85 };
		gdouble              alt_ratio = 0.0;
		guint                c;

		for (c = 0; c < 2; c++)
		{
			gdouble cand = best_bpm * factor[c];
			gdouble per = 60.0 / (cand * a->hop_sec), ph, sc = 0.0;

			if (cand < min_bpm * 0.999 || cand > max_bpm * 1.001)
				continue;
			for (ph = 0.0; ph < per; ph += 0.5)
				sc = MAX (sc, comb (a->onset, a->n, per, ph) / ((gdouble) a->n / per));
			if (best > 0.0 && sc / best >= need[c] && sc / best / need[c] > alt_ratio)
			{
				alt_ratio = sc / best / need[c];
				out->alternative = cand;
			}
		}
	}
	if (prominence >= 0.0)
		out->confidence = CLAMP ((prominence - 1.6) / 1.4, 0.0, 1.0);
	else
		out->confidence = best > 0.0 ? CLAMP ((best - mean) / best, 0.0, 1.0) : 0.0;
	return TRUE;
}

gboolean
lrg_music_detect_tempo (const gfloat *samples,
                        gsize         n_samples,
                        guint         channels,
                        guint         sample_rate,
                        gdouble       min_bpm,
                        gdouble       max_bpm,
                        gdouble      *out_bpm,
                        gdouble      *out_first_beat,
                        gdouble      *out_confidence)
{
	Ana      a;
	Tempo    t;
	gboolean ok;

	g_return_val_if_fail (samples != NULL || n_samples == 0, FALSE);
	g_return_val_if_fail (channels >= 1 && channels <= 8, FALSE);
	g_return_val_if_fail (sample_rate >= 8000, FALSE);
	g_return_val_if_fail (out_bpm != NULL, FALSE);

	if (is_silent (samples, n_samples))
		return FALSE;
	analyse (&a, samples, n_samples / channels, channels, sample_rate, FALSE);
	ok = find_tempo (&a, min_bpm > 0.0 ? min_bpm : 70.0, max_bpm > 0.0 ? max_bpm : 180.0, 0.0, &t);
	if (ok)
	{
		*out_bpm = t.bpm;
		if (out_first_beat != NULL)
			*out_first_beat = t.phase * a.hop_sec;
		if (out_confidence != NULL)
			*out_confidence = t.confidence;
	}
	ana_clear (&a);
	return ok;
}

/* the mean chroma of the analysis frames [f0, f1) (hop frames) */
static void
chroma_frames (const Ana *a,
               gdouble    f0,
               gdouble    f1,
               gdouble   *out)
{
	glong i;
	guint p;
	glong c = 0;

	memset (out, 0, 12 * sizeof (gdouble));
	for (i = MAX ((glong) floor (f0 / CHROMA_HOPS), 0); i < (glong) ceil (f1 / CHROMA_HOPS) && i < (glong) a->nc; i++, c++)
		for (p = 0; p < 12; p++)
			out[p] += a->chroma[i * 12 + p];
	for (p = 0; p < 12 && c > 0; p++)
		out[p] /= c;
}

/* which beat of the bar is the downbeat: where the harmony changes most
 * (chords change on bar lines) and the kick band hits hardest */
static guint
find_downbeat (const Ana *a,
               const Tempo *t,
               guint      bpb)
{
	gdouble period = 60.0 / (t->bpm * a->hop_sec);
	gdouble harm[32], kick[32], hsum = 0.0, ksum = 0.0;
	guint   j, best = 0;
	gsize   k;

	memset (harm, 0, sizeof harm);
	memset (kick, 0, sizeof kick);
	for (k = 1; t->phase + (k + 1) * period < (gdouble) a->n - 1; k++)
	{
		gdouble pos = t->phase + k * period;
		gdouble before[12], after[12], d = 0.0;
		guint   p;

		chroma_frames (a, pos - period, pos, before);
		chroma_frames (a, pos, pos + period, after);
		for (p = 0; p < 12; p++)
			d += fabs (after[p] - before[p]);
		harm[k % bpb] += d;
		kick[k % bpb] += at_frac (a->kick, a->n, pos);
	}
	for (j = 0; j < bpb; j++)
	{
		hsum += harm[j];
		ksum += kick[j];
	}
	for (j = 0; j < bpb; j++)
	{
		harm[j] = hsum > 0 ? harm[j] / hsum : 0.0;
		kick[j] = ksum > 0 ? kick[j] / ksum : 0.0;
	}
	for (j = 1; j < bpb; j++)
		if (harm[j] + 0.5 * kick[j] > harm[best] + 0.5 * kick[best])
			best = j;
	return best;
}

/* ==========================================================================
 * Key and chords
 * ========================================================================== */

static const gchar *pitch_names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/* Krumhansl-Kessler key profiles */
static const gdouble major_profile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
static const gdouble minor_profile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

static gdouble
correlate (const gdouble *a,
           const gdouble *b)
{
	gdouble ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
	guint   i;

	for (i = 0; i < 12; i++)
	{
		ma += a[i];
		mb += b[i];
	}
	ma /= 12.0;
	mb /= 12.0;
	for (i = 0; i < 12; i++)
	{
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return (saa > 0 && sbb > 0) ? sab / sqrt (saa * sbb) : 0.0;
}

static gchar *
key_of_chroma (const gdouble *total,
               const gdouble *bass)
{
	gdouble best = -2.0, prof[12], bass_top = 0.0;
	gint    best_root = -1;
	gboolean minor = FALSE;
	guint   root, i, m;

	for (i = 0; i < 12; i++)
		bass_top = MAX (bass_top, bass[i]);
	for (m = 0; m < 2; m++)
		for (root = 0; root < 12; root++)
		{
			gdouble c;

			for (i = 0; i < 12; i++)
				prof[(i + root) % 12] = m == 0 ? major_profile[i] : minor_profile[i];
			/* the profile fit, and the bass resting on the tonic */
			c = correlate (total, prof) + (bass_top > 0.0 ? 0.6 * bass[root] / bass_top : 0.0);
			if (c > best)
			{
				best = c;
				best_root = (gint) root;
				minor = m == 1;
			}
		}
	if (best_root < 0)
		return NULL;
	return g_strdup_printf ("%s %s", pitch_names[best_root], minor ? "minor" : "major");
}

/* the key of the whole analysis, or NULL for silence */
static gchar *
key_of_analysis (const Ana *a)
{
	gdouble total[12] = { 0 }, bass[12] = { 0 }, sum = 0.0;
	gsize   i;
	guint   p;

	for (i = 0; i < a->nc; i++)
		for (p = 0; p < 12; p++)
		{
			total[p] += a->chroma[i * 12 + p] * a->rms[MIN (i * CHROMA_HOPS, a->n - 1)];
			bass[p] += a->bass[i * 12 + p];
		}
	for (p = 0; p < 12; p++)
		sum += total[p];
	return sum > 1e-9 ? key_of_chroma (total, bass) : NULL;
}

gchar *
lrg_music_detect_key (const gfloat *samples,
                      gsize         n_samples,
                      guint         channels,
                      guint         sample_rate)
{
	Ana     a;
	gchar  *key;

	g_return_val_if_fail (samples != NULL || n_samples == 0, NULL);
	g_return_val_if_fail (channels >= 1 && channels <= 8, NULL);
	g_return_val_if_fail (sample_rate >= 8000, NULL);

	analyse (&a, samples, n_samples / channels, channels, sample_rate, FALSE);
	key = key_of_analysis (&a);
	ana_clear (&a);
	return key;
}

/* the best triad for a chroma vector: root 0..11, minor; FALSE when flat */
static gboolean
match_chord (const gdouble *ch,
             guint         *out_root,
             gboolean      *out_minor)
{
	gdouble best = 0.0, mean = 0.0, top = 0.0;
	guint   r, m, i;

	for (i = 0; i < 12; i++)
	{
		mean += ch[i] / 12.0;
		top = MAX (top, ch[i]);
	}
	if (top < mean * 1.6 || top <= 0.0)
		return FALSE;
	for (m = 0; m < 2; m++)
		for (r = 0; r < 12; r++)
		{
			/* root and fifth weigh most, the third tells major from minor */
			gdouble s = ch[r] + 0.8 * ch[(r + 7) % 12] + 0.9 * ch[(r + (m ? 3 : 4)) % 12]
			            - 0.5 * ch[(r + (m ? 4 : 3)) % 12] - 0.3 * ch[(r + 1) % 12];

			if (s > best)
			{
				best = s;
				*out_root = r;
				*out_minor = m == 1;
			}
		}
	return best > 0.0;
}

/* ==========================================================================
 * The grid: song beats and recording time
 * ========================================================================== */

typedef struct
{
	gdouble beat_sec;
	gdouble lead_in;        /* seconds of silence before the recording */
	guint   bpb;
	guint   bars;
	gdouble hop_sec;
} Grid;

/* the analysis frame (fractional) at song beat @beat */
static gdouble
frame_at (const Grid *g,
          gdouble     beat)
{
	return (beat * g->beat_sec - g->lead_in) / g->hop_sec;
}

/* the largest value of @x between song beats @b0 and @b1 */
static gdouble
max_between (const gfloat *x,
             gsize         n,
             const Grid   *g,
             gdouble       b0,
             gdouble       b1)
{
	glong   i0 = (glong) floor (frame_at (g, b0));
	glong   i1 = (glong) ceil (frame_at (g, b1));
	gdouble m = 0.0;
	glong   i;

	for (i = MAX (i0, 0); i <= i1 && i < (glong) n; i++)
		m = MAX (m, (gdouble) x[i]);
	return m;
}

/* the mean of @x between song beats */
static gdouble
mean_between (const gfloat *x,
              gsize         n,
              const Grid   *g,
              gdouble       b0,
              gdouble       b1)
{
	glong   i0 = (glong) floor (frame_at (g, b0));
	glong   i1 = (glong) ceil (frame_at (g, b1));
	gdouble s = 0.0;
	glong   i, c = 0;

	for (i = MAX (i0, 0); i < i1 && i < (glong) n; i++, c++)
		s += x[i];
	return c > 0 ? s / c : 0.0;
}

/* the mean chroma between song beats (normalised) */
static void
chroma_between (const Ana  *a,
                const Grid *g,
                gdouble     b0,
                gdouble     b1,
                gdouble    *out)
{
	glong   i0 = (glong) floor (frame_at (g, b0) / CHROMA_HOPS);
	glong   i1 = (glong) ceil (frame_at (g, b1) / CHROMA_HOPS);
	gdouble s = 0.0;
	glong   i;
	guint   p;

	memset (out, 0, 12 * sizeof (gdouble));
	for (i = MAX (i0, 0); i < i1 && i < (glong) a->nc; i++)
		for (p = 0; p < 12; p++)
		{
			gdouble w = a->rms[MIN ((gsize) i * CHROMA_HOPS, a->n - 1)];

			out[p] += a->chroma[i * 12 + p] * w;
		}
	for (p = 0; p < 12; p++)
		s += out[p];
	for (p = 0; p < 12 && s > 0.0; p++)
		out[p] /= s;
}

/* ==========================================================================
 * Sections
 * ========================================================================== */

#define N_FEAT (17)

static void
bar_features (const Ana  *a,
              const Grid *g,
              guint       bar,
              gdouble    *f)
{
	gdouble b0 = bar * g->bpb, b1 = b0 + g->bpb;
	gdouble e = mean_between (a->rms, a->n, g, b0, b1);

	chroma_between (a, g, b0, b1, f);
	f[12] = 0.15 * log1p (200.0 * e);
	f[13] = 2.0 * mean_between (a->bright, a->n, g, b0, b1);
	/* how busy each drum band is: arrangements change with the drums */
	f[14] = 0.08 * log1p (mean_between (a->kick, a->n, g, b0, b1));
	f[15] = 0.08 * log1p (mean_between (a->snare, a->n, g, b0, b1));
	f[16] = 0.08 * log1p (mean_between (a->hat, a->n, g, b0, b1));
}

static gdouble
feat_dist (const gdouble *x,
           const gdouble *y)
{
	gdouble s = 0.0;
	guint   i;

	for (i = 0; i < N_FEAT; i++)
		s += (x[i] - y[i]) * (x[i] - y[i]);
	return sqrt (s);
}

/* section starts (bars), always including 0; returns their count */
static guint
find_sections (const Ana  *a,
               const Grid *g,
               guint       min_bars,
               guint     **out_starts,
               gdouble   **out_feats)
{
	g_autofree gdouble *feat = g_new0 (gdouble, (gsize) g->bars * N_FEAT + N_FEAT);
	g_autofree gdouble *nov = g_new0 (gdouble, g->bars + 1);
	GArray             *starts = g_array_new (FALSE, FALSE, sizeof (guint));
	gdouble             mean = 0.0, sd = 0.0;
	guint               b, k, w = MAX (min_bars, 2u), last = 0, zero = 0;

	for (b = 0; b < g->bars; b++)
		bar_features (a, g, b, feat + b * N_FEAT);

	/* novelty: how different the w bars after b are from the w before */
	for (b = 1; b < g->bars; b++)
	{
		gdouble cross = 0.0, within = 0.0;
		guint   n_c = 0, n_w = 0, i, j;

		for (i = (b >= w ? b - w : 0); i < b; i++)
			for (j = b; j < MIN (b + w, g->bars); j++, n_c++)
				cross += feat_dist (feat + i * N_FEAT, feat + j * N_FEAT);
		for (i = (b >= w ? b - w : 0); i < b; i++)
			for (j = i + 1; j < b; j++, n_w++)
				within += feat_dist (feat + i * N_FEAT, feat + j * N_FEAT);
		for (i = b; i < MIN (b + w, g->bars); i++)
			for (j = i + 1; j < MIN (b + w, g->bars); j++, n_w++)
				within += feat_dist (feat + i * N_FEAT, feat + j * N_FEAT);
		nov[b] = (n_c ? cross / n_c : 0.0) - (n_w ? within / n_w : 0.0);
	}
	for (b = 1; b < g->bars; b++)
		mean += nov[b] / MAX (g->bars - 1, 1u);
	for (b = 1; b < g->bars; b++)
		sd += (nov[b] - mean) * (nov[b] - mean) / MAX (g->bars - 1, 1u);
	sd = sqrt (sd);

	g_array_append_val (starts, zero);
	for (b = min_bars; b + min_bars / 2 < g->bars; b++)
	{
		gboolean peak = TRUE;

		if (nov[b] < mean + 0.25 * sd || nov[b] <= 0.0 || b - last < min_bars)
			continue;
		for (k = 1; k < min_bars / 2 + 1 && peak; k++)
			if ((b >= k && nov[b - k] > nov[b]) || (b + k < g->bars && nov[b + k] > nov[b]))
				peak = FALSE;
		if (!peak)
			continue;
		g_array_append_val (starts, b);
		last = b;
	}
	{
		guint n = starts->len;

		*out_feats = g_steal_pointer (&feat);
		*out_starts = (guint *) g_array_free (starts, FALSE);
		return n;
	}
}

/* names the sections: quiet first and last ones are the intro and outro,
 * sections that sound alike share a letter ("A", "B", "A 2") */
static gchar **
name_sections (const guint   *starts,
               guint          n,
               guint          bars,
               const gdouble *feat)
{
	gchar   **names = g_new0 (gchar *, n + 1);
	gdouble  *mean = g_new0 (gdouble, (gsize) n * N_FEAT + N_FEAT);
	gint     *label = g_new0 (gint, n + 1);
	guint    *seen = g_new0 (guint, n + 1);
	gdouble   loud_sum = 0.0, loud_med;
	guint     s, b, i, letters = 0;

	for (s = 0; s < n; s++)
	{
		guint end = s + 1 < n ? starts[s + 1] : bars;

		for (b = starts[s]; b < end; b++)
			for (i = 0; i < N_FEAT; i++)
				mean[s * N_FEAT + i] += feat[b * N_FEAT + i] / MAX (end - starts[s], 1u);
		loud_sum += mean[s * N_FEAT + 12];
	}
	loud_med = loud_sum / MAX (n, 1u);
	for (s = 0; s < n; s++)
	{
		guint j;

		label[s] = -1;
		for (j = 0; j < s && label[s] < 0; j++)
			if (label[j] >= 0 && feat_dist (mean + s * N_FEAT, mean + j * N_FEAT) < 0.12)
				label[s] = label[j];
		if (label[s] < 0)
			label[s] = (gint) letters++;
	}
	for (s = 0; s < n; s++)
	{
		gboolean quiet = mean[s * N_FEAT + 12] < 0.8 * loud_med;

		if (n > 2 && s == 0 && quiet)
			names[s] = g_strdup ("Intro");
		else if (n > 2 && s == n - 1 && quiet)
			names[s] = g_strdup ("Outro");
		else if (label[s] < 26)
		{
			seen[label[s]]++;
			names[s] = seen[label[s]] > 1 ? g_strdup_printf ("%c %u", 'A' + label[s], seen[label[s]])
			                              : g_strdup_printf ("%c", 'A' + label[s]);
		}
		else
			names[s] = g_strdup_printf ("Part %u", s + 1);
	}
	g_free (mean);
	g_free (label);
	g_free (seen);
	return names;
}

/* ==========================================================================
 * Patterns shared by identical bars
 * ========================================================================== */

typedef struct
{
	LrgSong    *song;
	const char *track;
	const char *prefix;
	GHashTable *ids;      /* content key -> pattern id */
	guint       next;
	gchar     **bar_ids;  /* per bar: pattern id (not owned) or NULL */
	guint       bars;
	guint       bpb;
} Bars;

static void
bars_init (Bars        *b,
           LrgSong     *song,
           const gchar *track,
           const gchar *prefix,
           guint        bars,
           guint        bpb)
{
	b->song = song;
	b->track = track;
	b->prefix = prefix;
	b->ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	b->next = 0;
	b->bar_ids = g_new0 (gchar *, bars + 1);
	b->bars = bars;
	b->bpb = bpb;
}

/* clips for runs of bars playing the same pattern */
static void
bars_place (Bars *b)
{
	guint i = 0;

	while (i < b->bars)
	{
		guint j = i + 1;

		if (b->bar_ids[i] == NULL)
		{
			i++;
			continue;
		}
		while (j < b->bars && g_strcmp0 (b->bar_ids[j], b->bar_ids[i]) == 0)
			j++;
		lrg_song_add_clip (b->song, b->track, b->bar_ids[i], (gdouble) i * b->bpb, (gdouble) (j - i) * b->bpb);
		i = j;
	}
	g_hash_table_unref (b->ids);
	g_free (b->bar_ids);
}

typedef struct
{
	gdouble beat;
	gint    key;
	gdouble len;
	gdouble vel;
} Note;

/* the bar's notes as a pattern (new or shared); FALSE when it had none */
static gboolean
bars_set_notes (Bars   *b,
                guint   bar,
                GArray *notes)
{
	GString *key = g_string_new (NULL);
	gchar   *id;
	guint    i;

	if (notes->len == 0)
	{
		g_string_free (key, TRUE);
		return FALSE;
	}
	for (i = 0; i < notes->len; i++)
	{
		Note *n = &g_array_index (notes, Note, i);

		g_string_append_printf (key, "%.3f:%d:%.3f:%.1f;", n->beat, n->key, n->len, n->vel);
	}
	id = g_hash_table_lookup (b->ids, key->str);
	if (id == NULL)
	{
		id = g_strdup_printf ("%s-%u", b->prefix, ++b->next);
		lrg_song_add_pattern (b->song, id, b->bpb);
		for (i = 0; i < notes->len; i++)
		{
			Note *n = &g_array_index (notes, Note, i);

			lrg_song_pattern_add_note (b->song, id, n->beat, n->key, n->len, n->vel);
		}
		g_hash_table_insert (b->ids, g_strdup (key->str), id);
	}
	b->bar_ids[bar] = id;
	g_string_free (key, TRUE);
	return TRUE;
}

/* the bar's drum lanes as a pattern (new or shared) */
static void
bars_set_lanes (Bars         *b,
                guint         bar,
                const gchar **names,
                gchar       **lanes,
                guint         n_lanes)
{
	GString *key = g_string_new (NULL);
	gchar   *id;
	guint    i;

	for (i = 0; i < n_lanes; i++)
		g_string_append_printf (key, "%s|", lanes[i]);
	id = g_hash_table_lookup (b->ids, key->str);
	if (id == NULL)
	{
		id = g_strdup_printf ("%s-%u", b->prefix, ++b->next);
		lrg_song_add_pattern (b->song, id, b->bpb);
		lrg_song_pattern_set_steps (b->song, id, 4 * b->bpb);
		for (i = 0; i < n_lanes; i++)
			if (strpbrk (lanes[i], "goxX") != NULL)
				lrg_song_pattern_set_lane (b->song, id, names[i], lanes[i]);
		g_hash_table_insert (b->ids, g_strdup (key->str), id);
	}
	b->bar_ids[bar] = id;
	g_string_free (key, TRUE);
}

/* ==========================================================================
 * Transcription
 * ========================================================================== */

static int
cmp_double (gconstpointer a,
            gconstpointer b)
{
	gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

	return x < y ? -1 : x > y ? 1 : 0;
}

/* the @q quantile (0 .. 1) of @n values (sorts a copy) */
static gdouble
quantile (const gdouble *v,
          gsize          n,
          gdouble        q)
{
	g_autofree gdouble *c = NULL;

	if (n == 0)
		return 0.0;
	c = g_memdup2 (v, n * sizeof (gdouble));
	qsort (c, n, sizeof (gdouble), cmp_double);
	return c[MIN ((gsize) (q * (n - 1) + 0.5), n - 1)];
}

/*
 * transcribe_drums:
 * Drum steps per sixteenth from the three band onsets.  Each band's value
 * is normalised by its loud hits (97th percentile).  A kick is a low onset
 * near the loudest of its bar (a bass line also hits the low band, but
 * kick and bass together hit harder than the bass alone); a snare must
 * beat the hat band, a hat must beat the snare band (a snare's noise
 * reaches the hat band and the other way round).
 */
static guint
transcribe_drums (LrgSong    *song,
                  const Ana  *a,
                  const Grid *g)
{
	static const gchar *names[3] = { "kick", "snare", "closed-hat" };
	const gfloat       *bands[3];
	guint               per_bar = g->bpb * 4, steps = g->bars * per_bar, s, l, hits = 0, bar;
	g_autofree gdouble *v = g_new0 (gdouble, (gsize) steps * 3 + 3);
	gdouble             thr[3];
	Bars                bars;

	bands[0] = a->kick;
	bands[1] = a->snare;
	bands[2] = a->hat;
	for (l = 0; l < 3; l++)
	{
		g_autofree gdouble *col = g_new0 (gdouble, steps + 1);
		gdouble             top, mean = 0.0, sd = 0.0;

		for (s = 0; s < steps; s++)
		{
			gdouble beat = s * 0.25;

			col[s] = max_between (bands[l], a->n, g, beat - 0.1, beat + 0.1);
			mean += col[s] / steps;
		}
		for (s = 0; s < steps; s++)
			sd += (col[s] - mean) * (col[s] - mean) / steps;
		top = MAX (quantile (col, steps, 0.97), 1e-9);
		thr[l] = (mean + (l == 2 ? 0.2 : 0.5) * sqrt (sd)) / top;
		for (s = 0; s < steps; s++)
			v[s * 3 + l] = col[s] / top;
	}
	bars_init (&bars, song, "drums", "drums", g->bars, g->bpb);
	for (bar = 0; bar < g->bars; bar++)
	{
		gchar  *lanes[3];
		gdouble bar_max[3] = { 0.0, 0.0, 0.0 };
		guint   any = 0;

		/* the loudest hit of each band in the bar: real hits are near it */
		for (s = 0; s < per_bar; s++)
			for (l = 0; l < 3; l++)
				bar_max[l] = MAX (bar_max[l], v[(bar * per_bar + s) * 3 + l]);
		for (l = 0; l < 3; l++)
			lanes[l] = g_malloc0 (per_bar + 1);
		for (s = 0; s < per_bar; s++)
		{
			const gdouble *x = v + (bar * per_bar + s) * 3;
			gboolean       hit[3];

			hit[0] = x[0] > thr[0] && x[0] >= 0.4 * bar_max[0];
			hit[1] = x[1] > thr[1] && x[1] >= 0.75 * x[2] && x[1] >= 0.5 * bar_max[1];
			hit[2] = x[2] > thr[2] && x[2] >= 1.25 * x[1] && x[2] >= 0.3 * bar_max[2];
			for (l = 0; l < 3; l++)
			{
				gdouble r = x[l];

				lanes[l][s] = !hit[l] ? '.' : r >= 0.9 ? 'X' : r >= 0.55 ? 'x' : r >= 0.3 ? 'o' : 'g';
				if (hit[l])
				{
					any++;
					hits++;
				}
			}
		}
		if (any > 0)
			bars_set_lanes (&bars, bar, names, lanes, 3);
		for (l = 0; l < 3; l++)
			g_free (lanes[l]);
	}
	bars_place (&bars);
	return hits;
}

/* consecutive equal keys on a grid of @step beats become one note per bar */
static guint
notes_from_steps (Bars        *bars,
                  const gint  *keys,
                  const gdouble *vel,
                  guint        n_steps,
                  gdouble      step,
                  guint        min_steps)
{
	guint per_bar = (guint) (bars->bpb / step + 0.5), bar, notes = 0;

	for (bar = 0; bar < bars->bars; bar++)
	{
		GArray *arr = g_array_new (FALSE, FALSE, sizeof (Note));
		guint   s = 0;

		while (s < per_bar)
		{
			guint i = bar * per_bar + s, e = s + 1;

			if (i >= n_steps || keys[i] < 0)
			{
				s++;
				continue;
			}
			while (e < per_bar && bar * per_bar + e < n_steps && keys[bar * per_bar + e] == keys[i])
				e++;
			if (e - s >= min_steps)
			{
				Note nt;

				nt.beat = s * step;
				nt.key = keys[i];
				nt.len = (e - s) * step - MIN (0.05, step * 0.2);
				nt.vel = CLAMP (vel[i], 0.4, 1.0);
				nt.vel = floor (nt.vel * 10.0 + 0.5) / 10.0;
				g_array_append_val (arr, nt);
				notes++;
			}
			s = e;
		}
		bars_set_notes (bars, bar, arr);
		g_array_free (arr, TRUE);
	}
	bars_place (bars);
	return notes;
}

/*
 * transcribe_bass:
 * The bass per eighth note by harmonic salience: for every candidate note
 * from A0 to G3, the spectrum of the eighth (from 20 ms after it starts)
 * at its 2nd to 6th harmonics, and only a little at its fundamental.  A
 * bass note is rich in harmonics; a kick drum's ringing tail is nearly a
 * pure sine at a fixed pitch, which would win a pitch tracker over every
 * off-beat, and this way hardly counts.
 */
static guint
transcribe_bass (LrgSong    *song,
                 const Ana  *a,
                 const Grid *g)
{
	guint               steps = g->bars * g->bpb * 2, s, win = 4096, n_fft = FFT_LONG, kk;
	g_autofree gint    *keys = g_new (gint, steps + 1);
	g_autofree gdouble *vel = g_new0 (gdouble, steps + 1);
	g_autofree gdouble *strength = g_new0 (gdouble, steps + 1);
	g_autofree gint    *best_key = g_new (gint, steps + 1);
	g_autofree gfloat  *mag = g_new (gfloat, n_fft / 2 + 1);
	g_autofree gfloat  *frame = g_new0 (gfloat, n_fft);
	gdouble             bin_hz = (gdouble) ANA_RATE / n_fft, s_top;
	Fft                 f;
	Bars                bars;

	fft_init (&f, n_fft);
	for (s = 0; s < steps; s++)
	{
		gdouble t = (s * 0.5 * g->beat_sec - g->lead_in) + 0.02;
		glong   start = (glong) (t * ANA_RATE);
		gdouble best = 0.0, total = 0.0;
		guint   j;
		gint    k;

		/* the eighth, Hann-windowed over 170 ms and zero-padded */
		for (j = 0; j < win; j++)
		{
			glong  p = start + (glong) j;
			gfloat v = (p >= 0 && (gsize) p < a->mono_len) ? a->mono[p] : 0.0f;

			frame[j] = v * (gfloat) (0.5 - 0.5 * cos (2.0 * G_PI * j / (win - 1)));
		}
		for (kk = 0; kk < n_fft; kk++)
		{
			/* fft_magnitudes windows again: undo by a flat window */
			f.window[kk] = 1.0f;
		}
		fft_magnitudes (&f, frame, n_fft, 0, mag);
		best_key[s] = -1;
		for (k = 21; k <= 55; k++)
		{
			gdouble f0 = lrg_dsp_key_to_hz (k), sal = 0.0;
			guint   h;

			for (h = 1; h <= 6; h++)
			{
				gdouble fh = f0 * h;
				gsize   lo = (gsize) floor (fh * 0.985 / bin_hz), hi = (gsize) ceil (fh * 1.015 / bin_hz), b;
				gdouble m = 0.0;

				for (b = lo; b <= hi && b <= n_fft / 2; b++)
					m = MAX (m, (gdouble) mag[b]);
				sal += (h == 1 ? 0.3 : 1.0) * m;
			}
			total += sal;
			if (sal > best)
			{
				best = sal;
				best_key[s] = k;
			}
		}
		strength[s] = best;
		/* a clear winner, not a flat spectrum */
		if (best < 2.0 * total / 35.0)
			best_key[s] = -1;
	}
	fft_clear (&f);
	s_top = MAX (quantile (strength, steps, 0.9), 1e-9);
	for (s = 0; s < steps; s++)
	{
		keys[s] = strength[s] > 0.25 * s_top ? best_key[s] : -1;
		vel[s] = 0.5 + 0.5 * MIN (strength[s] / s_top, 1.0);
	}
	bars_init (&bars, song, "bass", "bass", g->bars, g->bpb);
	return notes_from_steps (&bars, keys, vel, steps, 0.5, 1);
}

/* the melody: per sixteenth, the note of greatest harmonic salience */
static guint
transcribe_melody (LrgSong    *song,
                   const Ana  *a,
                   const Grid *g)
{
	guint               steps = g->bars * g->bpb * 4, s, p;
	g_autofree gint    *raw = g_new (gint, steps + 1);
	g_autofree gint    *keys = g_new (gint, steps + 1);
	g_autofree gdouble *vel = g_new0 (gdouble, steps + 1);
	g_autofree gdouble *strength = g_new0 (gdouble, steps + 1);
	gdouble             s_top;
	Bars                bars;

	for (s = 0; s < steps; s++)
	{
		gdouble f0 = frame_at (g, s * 0.25) / CHROMA_HOPS, f1 = frame_at (g, (s + 1) * 0.25) / CHROMA_HOPS;
		gdouble v[N_MEL], best = 0.0, mean = 0.0;
		glong   i, cnt = 0;
		gint    arg = -1;

		memset (v, 0, sizeof v);
		for (i = MAX ((glong) floor (f0), 0); i < (glong) ceil (f1) && i < (glong) a->nc; i++, cnt++)
			for (p = 0; p < N_MEL; p++)
				v[p] += a->sal[i * N_MEL + p];
		for (p = 0; p < N_MEL; p++)
		{
			mean += v[p] / N_MEL;
			if (v[p] > best)
			{
				best = v[p];
				arg = (gint) p;
			}
		}
		raw[s] = (cnt > 0 && best > 1.8 * mean && arg >= 0) ? MEL_LOW + arg : -1;
		strength[s] = cnt > 0 ? best / cnt : 0.0;
	}
	s_top = MAX (quantile (strength, steps, 0.9), 1e-9);
	for (s = 0; s < steps; s++)
	{
		/* a median of three steps removes single-step jumps */
		gint a0 = s > 0 ? raw[s - 1] : raw[s], a1 = raw[s], a2 = s + 1 < steps ? raw[s + 1] : raw[s];
		gint m = MAX (MIN (a0, a1), MIN (MAX (a0, a1), a2));

		keys[s] = (strength[s] > 0.25 * s_top && a1 >= 0) ? (m >= 0 ? m : a1) : -1;
		vel[s] = 0.5 + 0.5 * MIN (strength[s] / s_top, 1.0);
	}
	bars_init (&bars, song, "melody", "melody", g->bars, g->bpb);
	return notes_from_steps (&bars, keys, vel, steps, 0.25, 2);
}

/* chords: a triad per half bar from the chroma */
static guint
transcribe_chords (LrgSong    *song,
                   const Ana  *a,
                   const Grid *g)
{
	guint  halves = g->bpb % 2 == 0 ? 2 : 1, bar, h, chords = 0;
	gdouble len = (gdouble) g->bpb / halves;
	Bars   bars;

	bars_init (&bars, song, "chords", "chords", g->bars, g->bpb);
	for (bar = 0; bar < g->bars; bar++)
	{
		GArray  *arr = g_array_new (FALSE, FALSE, sizeof (Note));
		gint     prev_root = -1;
		gboolean prev_minor = FALSE;

		for (h = 0; h < halves; h++)
		{
			gdouble  ch[12];
			guint    root = 0;
			gboolean minor = FALSE;
			gdouble  b0 = bar * g->bpb + h * len;

			chroma_between (a, g, b0, b0 + len, ch);
			if (!match_chord (ch, &root, &minor))
			{
				prev_root = -1;
				continue;
			}
			if ((gint) root == prev_root && minor == prev_minor && arr->len >= 3)
			{
				/* the same chord again: lengthen it */
				guint i;

				for (i = arr->len - 3; i < arr->len; i++)
					g_array_index (arr, Note, i).len += len;
				continue;
			}
			{
				gint  base = 48 + (gint) root;
				gint  third = minor ? 3 : 4;
				Note  nt;
				gint  iv[3];
				guint i;

				iv[0] = 0;
				iv[1] = third;
				iv[2] = 7;
				for (i = 0; i < 3; i++)
				{
					nt.beat = h * len;
					nt.key = base + iv[i];
					nt.len = len - 0.05;
					nt.vel = 0.6;
					g_array_append_val (arr, nt);
				}
				chords++;
			}
			prev_root = (gint) root;
			prev_minor = minor;
		}
		bars_set_notes (&bars, bar, arr);
		g_array_free (arr, TRUE);
	}
	bars_place (&bars);
	return chords;
}

/* ==========================================================================
 * The import
 * ========================================================================== */

static gchar *
file_stem (const gchar *path)
{
	g_autofree gchar *base = g_path_get_basename (path);
	gchar            *dot = strrchr (base, '.');
	gchar            *s;

	if (dot != NULL && dot != base)
		*dot = '\0';
	/* a safe file name: letters, digits, '-', '_' */
	s = g_strdup (base);
	g_strcanon (s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", '-');
	return s;
}

LrgSong *
lrg_song_import_audio (const gchar                 *audio_path,
                       const gchar                 *out_dir,
                       const LrgSongImportOptions  *options,
                       LrgSongImportReport        **out_report,
                       GError                     **error)
{
	g_autoptr(LrgSongImportOptions) defaults = NULL;
	g_autoptr(LrgWaveData)          wave = NULL;
	g_autoptr(LrgSong)              song = NULL;
	g_autoptr(LrgSongImportReport)  report = NULL;
	g_autoptr(LrgAudioEncodeOptions) enc = NULL;
	g_autofree gfloat              *src = NULL;
	g_autofree gfloat              *pcm = NULL;
	g_autofree gfloat              *aligned = NULL;
	g_autofree gchar               *stem = NULL;
	g_autofree gchar               *rel = NULL;
	g_autofree gchar               *abs_wav = NULL;
	g_autofree gchar               *wav_dir = NULL;
	g_autofree gchar               *title = NULL;
	g_autofree guint               *starts = NULL;
	g_autofree gdouble             *feat = NULL;
	gsize                           n_src = 0, frames = 0, lead_frames, i;
	guint                           channels, src_rate, rate, bpb, down;
	Ana                             a;
	Tempo                           t;
	Grid                            g;

	g_return_val_if_fail (audio_path != NULL, NULL);
	g_return_val_if_fail (out_dir != NULL, NULL);

	if (options == NULL)
		options = defaults = lrg_song_import_options_new ();
	rate = options->sample_rate > 0 ? options->sample_rate : 48000;
	bpb = CLAMP (options->beats_per_bar > 0 ? options->beats_per_bar : 4, 1u, 16u);
	report = g_new0 (LrgSongImportReport, 1);

	/* 1. decode, then resample to the song rate */
	wave = lrg_audio_decode_file (audio_path, error);
	if (wave == NULL)
		return NULL;
	channels = MIN (lrg_wave_data_get_channels (wave), 2u);
	src_rate = lrg_wave_data_get_sample_rate (wave);
	src = lrg_wave_data_get_samples (wave, &n_src);
	if (lrg_wave_data_get_channels (wave) > 2)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s: more than two channels", audio_path);
		return NULL;
	}
	if (src == NULL || n_src < (gsize) channels * src_rate)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: shorter than a second", audio_path);
		return NULL;
	}
	pcm = resample (src, n_src / channels, channels, src_rate, rate, &frames);
	g_clear_pointer (&src, g_free);
	report->duration = (gdouble) frames / rate;
	report->source_rate = src_rate;

	/* 2. analyse; 3. tempo, phase and the downbeat */
	analyse (&a, pcm, frames, channels, rate, options->transcribe);
	if (is_silent (pcm, frames * channels)
	    || !find_tempo (&a, options->min_bpm > 0 ? options->min_bpm : 70.0, options->max_bpm > 0 ? options->max_bpm : 180.0,
	                 options->bpm, &t))
	{
		if (options->bpm <= 0.0)
		{
			ana_clear (&a);
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			             "%s: no tempo found (too short or no clear onsets); give the tempo", audio_path);
			return NULL;
		}
		t.bpm = options->bpm;
		t.phase = 0.0;
		t.confidence = 0.0;
	}
	down = find_downbeat (&a, &t, bpb);
	report->bpm = t.bpm;
	report->bpm_detected = options->bpm <= 0.0;
	report->confidence = t.confidence;
	report->alternative_bpm = t.alternative;
	report->first_beat = fmod ((t.phase + down * 60.0 / (t.bpm * a.hop_sec)) * a.hop_sec, bpb * 60.0 / t.bpm);

	/* 4. the grid: silence before the recording so a bar starts at beat 0 */
	g.beat_sec = 60.0 / t.bpm;
	g.bpb = bpb;
	g.hop_sec = a.hop_sec;
	{
		gdouble bar_sec = bpb * g.beat_sec;
		gdouble rem = fmod (report->first_beat, bar_sec);

		g.lead_in = rem > 1e-3 ? bar_sec - rem : 0.0;
		g.bars = (guint) ceil ((g.lead_in + report->duration) / bar_sec - 1e-9);
	}
	report->lead_in = g.lead_in;
	lead_frames = (gsize) floor (g.lead_in * rate + 0.5);

	/* the aligned WAV */
	stem = file_stem (audio_path);
	rel = g_build_filename (options->audio_dir != NULL ? options->audio_dir : "audio", stem, NULL);
	{
		gchar *with_ext = g_strconcat (rel, ".wav", NULL);

		g_free (rel);
		rel = with_ext;
	}
	abs_wav = g_build_filename (out_dir, rel, NULL);
	wav_dir = g_path_get_dirname (abs_wav);
	if (g_mkdir_with_parents (wav_dir, 0755) != 0)
	{
		ana_clear (&a);
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not create %s: %s", wav_dir,
		             g_strerror (errno));
		return NULL;
	}
	aligned = g_new0 (gfloat, (lead_frames + frames) * channels + 1);
	memcpy (aligned + lead_frames * channels, pcm, frames * channels * sizeof (gfloat));
	enc = lrg_audio_encode_options_new ();
	enc->bit_depth = 24;
	if (!lrg_audio_encode_samples (aligned, (lead_frames + frames) * channels, channels, rate,
	                               LRG_AUDIO_FILE_FORMAT_WAV, enc, abs_wav, NULL, error))
	{
		ana_clear (&a);
		return NULL;
	}
	report->audio_file = g_strdup (rel);

	/* the song: the recording on the "original" track */
	title = options->title != NULL ? g_strdup (options->title) : g_path_get_basename (audio_path);
	if (options->title == NULL && strrchr (title, '.') != NULL && strrchr (title, '.') != title)
		*strrchr (title, '.') = '\0';
	song = lrg_song_new ();
	g_object_set (song, "title", title, "bpm", t.bpm, "beats-per-bar", bpb, "sample-rate", rate,
	              "length", (gdouble) g.bars * bpb, "base-path", out_dir, NULL);
	if (options->artist != NULL)
		g_object_set (song, "artist", options->artist, NULL);
	lrg_mixer_asset_add_bus (lrg_song_get_mixer (song), "original", NULL);
	lrg_song_add_track (song, "original", LRG_SONG_TRACK_AUDIO, NULL);
	lrg_song_set_track (song, "original", "bus", "original", 0);
	lrg_song_set_track (song, "original", "name", "Original recording", 0);
	lrg_song_add_clip (song, "original", rel, 0.0, (gdouble) g.bars * bpb);

	/* the key */
	report->key = key_of_analysis (&a);

	/* 5. sections */
	if (options->sections && g.bars >= 2)
	{
		guint   n = find_sections (&a, &g, MAX (options->section_bars, 1u), &starts, &feat);
		gchar **names = name_sections (starts, n, g.bars, feat);

		for (i = 0; i < n; i++)
			lrg_song_add_marker (song, (gdouble) starts[i] * bpb, names[i]);
		report->sections = n;
		g_strfreev (names);
	}

	/* 6. transcription, on muted tracks */
	if (options->transcribe)
	{
		static const struct { const gchar *id, *type, *source, *name; } parts[] = {
			{ "drums", "drums", "808", "Drums (estimate)" },
			{ "bass", "synth", "synthwave-bass", "Bass (estimate)" },
			{ "melody", "synth", "synthwave-lead", "Melody (estimate)" },
			{ "chords", "synth", "synthwave-pad", "Chords (estimate)" },
		};
		guint p;

		lrg_mixer_asset_add_bus (lrg_song_get_mixer (song), "remake", NULL);
		for (p = 0; p < G_N_ELEMENTS (parts); p++)
		{
			lrg_song_set_instrument (song, parts[p].id, parts[p].type, parts[p].source);
			lrg_song_add_track (song, parts[p].id, LRG_SONG_TRACK_INSTRUMENT, parts[p].id);
			lrg_song_set_track (song, parts[p].id, "bus", "remake", 0);
			lrg_song_set_track (song, parts[p].id, "name", parts[p].name, 0);
			lrg_song_set_track (song, parts[p].id, "mute", NULL, 1);
		}
		report->drum_hits = transcribe_drums (song, &a, &g);
		report->bass_notes = transcribe_bass (song, &a, &g);
		report->melody_notes = transcribe_melody (song, &a, &g);
		report->chords = transcribe_chords (song, &a, &g);
	}
	ana_clear (&a);

	if (out_report != NULL)
		*out_report = g_steal_pointer (&report);
	return g_steal_pointer (&song);
}
