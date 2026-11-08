/* lrg-audio-dsp-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Small DSP building blocks shared by the effects, the synthesizers and
 * the drum machine: the Audio EQ Cookbook biquads (Robert Bristow-
 * Johnson) in transposed direct form II, one-pole smoothing coefficients
 * and decibel conversions.  Everything is static inline so each user gets
 * its own copy and nothing here is exported.
 */

#pragma once

#include <glib.h>
#include <math.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Conversions
 * ========================================================================== */

/* decibels to a linear gain; -144 dB and below is silence */
static inline gdouble
lrg_dsp_db_to_lin (gdouble db)
{
	if (!isfinite (db) || db <= -144.0)
		return 0.0;
	return pow (10.0, db / 20.0);
}

/* a linear gain to decibels, never below -144 */
static inline gdouble
lrg_dsp_lin_to_db (gdouble lin)
{
	if (!(lin > 1e-7))
		return -144.0;
	return 20.0 * log10 (lin);
}

/*
 * lrg_dsp_one_pole:
 * The coefficient of a one-pole smoother `y += k (x - y)` whose step
 * response closes the gap to 1/e in @seconds at @rate (1 for an instant
 * response).
 */
static inline gdouble
lrg_dsp_one_pole (gdouble seconds,
                  guint   rate)
{
	if (!(seconds > 0.0) || rate == 0)
		return 1.0;
	return 1.0 - exp (-1.0 / (seconds * (gdouble) rate));
}

/* MIDI key to frequency in Hz (A4 = key 69 = 440 Hz) */
static inline gdouble
lrg_dsp_key_to_hz (gdouble key)
{
	return 440.0 * pow (2.0, (key - 69.0) / 12.0);
}

/* ==========================================================================
 * Biquad (Audio EQ Cookbook)
 * ========================================================================== */

typedef enum
{
	LRG_DSP_BIQUAD_LOWPASS,
	LRG_DSP_BIQUAD_HIGHPASS,
	LRG_DSP_BIQUAD_BANDPASS,     /* constant 0 dB peak gain */
	LRG_DSP_BIQUAD_PEAK,
	LRG_DSP_BIQUAD_LOWSHELF,
	LRG_DSP_BIQUAD_HIGHSHELF,
	LRG_DSP_BIQUAD_NOTCH
} LrgDspBiquadKind;

typedef struct
{
	gdouble b0, b1, b2, a1, a2;    /* normalised by a0 */
} LrgDspBiquadCoef;

typedef struct
{
	gdouble z1, z2;
} LrgDspBiquadState;

/*
 * lrg_dsp_biquad_design:
 * Fills @coef for a filter of @kind at @freq Hz, quality @q and (for the
 * peak and shelf filters) @gain_db, at @rate.  The frequency is kept
 * below 0.45 x the rate, where the bilinear transform still behaves.
 */
static inline void
lrg_dsp_biquad_design (LrgDspBiquadCoef *coef,
                       LrgDspBiquadKind  kind,
                       gdouble           freq,
                       gdouble           q,
                       gdouble           gain_db,
                       guint             rate)
{
	gdouble fc = CLAMP (freq, 10.0, 0.45 * (gdouble) rate);
	gdouble w0 = 2.0 * G_PI * fc / (gdouble) rate;
	gdouble cw = cos (w0);
	gdouble sw = sin (w0);
	gdouble qq = MAX (q, 0.05);
	gdouble alpha = sw / (2.0 * qq);
	gdouble a = pow (10.0, gain_db / 40.0);
	gdouble b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
	gdouble sq;

	switch (kind)
	{
	case LRG_DSP_BIQUAD_LOWPASS:
		b0 = (1.0 - cw) / 2.0; b1 = 1.0 - cw; b2 = b0;
		a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
		break;
	case LRG_DSP_BIQUAD_HIGHPASS:
		b0 = (1.0 + cw) / 2.0; b1 = -(1.0 + cw); b2 = b0;
		a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
		break;
	case LRG_DSP_BIQUAD_BANDPASS:
		b0 = alpha; b1 = 0.0; b2 = -alpha;
		a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
		break;
	case LRG_DSP_BIQUAD_NOTCH:
		b0 = 1.0; b1 = -2.0 * cw; b2 = 1.0;
		a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
		break;
	case LRG_DSP_BIQUAD_PEAK:
		b0 = 1.0 + alpha * a; b1 = -2.0 * cw; b2 = 1.0 - alpha * a;
		a0 = 1.0 + alpha / a; a1 = -2.0 * cw; a2 = 1.0 - alpha / a;
		break;
	case LRG_DSP_BIQUAD_LOWSHELF:
		sq = 2.0 * sqrt (a) * alpha;
		b0 = a * ((a + 1.0) - (a - 1.0) * cw + sq);
		b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cw);
		b2 = a * ((a + 1.0) - (a - 1.0) * cw - sq);
		a0 = (a + 1.0) + (a - 1.0) * cw + sq;
		a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cw);
		a2 = (a + 1.0) + (a - 1.0) * cw - sq;
		break;
	case LRG_DSP_BIQUAD_HIGHSHELF:
		sq = 2.0 * sqrt (a) * alpha;
		b0 = a * ((a + 1.0) + (a - 1.0) * cw + sq);
		b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cw);
		b2 = a * ((a + 1.0) + (a - 1.0) * cw - sq);
		a0 = (a + 1.0) - (a - 1.0) * cw + sq;
		a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cw);
		a2 = (a + 1.0) - (a - 1.0) * cw - sq;
		break;
	default:
		break;
	}
	coef->b0 = b0 / a0;
	coef->b1 = b1 / a0;
	coef->b2 = b2 / a0;
	coef->a1 = a1 / a0;
	coef->a2 = a2 / a0;
}

/* one sample through a biquad (transposed direct form II) */
static inline gdouble
lrg_dsp_biquad_tick (const LrgDspBiquadCoef *c,
                     LrgDspBiquadState      *s,
                     gdouble                 x)
{
	gdouble y = c->b0 * x + s->z1;

	s->z1 = c->b1 * x - c->a1 * y + s->z2;
	s->z2 = c->b2 * x - c->a2 * y;

	/* flush denormals so a decaying tail never slows the CPU */
	if (fabs (s->z1) < 1e-20)
		s->z1 = 0.0;
	if (fabs (s->z2) < 1e-20)
		s->z2 = 0.0;
	return y;
}

/* ==========================================================================
 * Deterministic noise
 * ========================================================================== */

/*
 * lrg_dsp_rng_next:
 * xorshift64* — fast, seedable, identical everywhere.  @state must not be
 * 0 (seed it with lrg_dsp_rng_seed()).
 */
static inline guint64
lrg_dsp_rng_next (guint64 *state)
{
	guint64 x = *state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * G_GUINT64_CONSTANT (2685821657736338717);
}

/* a seed that is never 0, spread by splitmix64 */
static inline guint64
lrg_dsp_rng_seed (guint64 seed)
{
	guint64 z = seed + G_GUINT64_CONSTANT (0x9E3779B97F4A7C15);

	z = (z ^ (z >> 30)) * G_GUINT64_CONSTANT (0xBF58476D1CE4E5B9);
	z = (z ^ (z >> 27)) * G_GUINT64_CONSTANT (0x94D049BB133111EB);
	z ^= z >> 31;
	return z != 0 ? z : G_GUINT64_CONSTANT (0x2545F4914F6CDD1D);
}

/* white noise in [-1, 1) */
static inline gdouble
lrg_dsp_rng_noise (guint64 *state)
{
	return (gdouble) (lrg_dsp_rng_next (state) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

/* uniform in [0, 1) */
static inline gdouble
lrg_dsp_rng_unit (guint64 *state)
{
	return (gdouble) (lrg_dsp_rng_next (state) >> 11) * (1.0 / 9007199254740992.0);
}

G_END_DECLS
