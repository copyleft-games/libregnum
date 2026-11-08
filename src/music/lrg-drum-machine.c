/* lrg-drum-machine.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The synthesized drum machine (see lrg-drum-machine.h and
 * docs/modules/music/instruments.org).
 *
 * Each piece is a small circuit model run per sample from the time since
 * its hit:
 *
 *   kick     sine with a falling pitch envelope, a noise click, saturation
 *   snare    two damped sines (the drum head) plus high-passed noise
 *            (the wires); `snap` moves the balance to the wires
 *   clap     band-passed noise through four quick bursts and a tail
 *   rim      two short inharmonic sines and a click
 *   hats     six square oscillators at the 808's metallic ratios, band-
 *            and high-passed, blended with noise; the closed and pedal
 *            hats choke the open one
 *   toms     sine with a short pitch drop and a little noise
 *   crash    metallic squares and noise, long decay
 *   ride     metallic squares with a bell partial, longer decay
 *   cowbell  two squares band-passed
 *
 * Every piece has two voice slots used alternately, so a fast repeat lets
 * the previous hit ring out instead of cutting it.  Noise is a seeded
 * generator: the same hits always render the same samples.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-drum-machine.h"
#include "../audio/lrg-audio-dsp-private.h"
#include "../audio/lrg-audio-param-private.h"

G_DEFINE_ENUM_TYPE (LrgDrumKit, lrg_drum_kit,
	G_DEFINE_ENUM_VALUE (LRG_DRUM_KIT_808, "808"),
	G_DEFINE_ENUM_VALUE (LRG_DRUM_KIT_909, "909"),
	G_DEFINE_ENUM_VALUE (LRG_DRUM_KIT_LINN, "linn"))

typedef enum
{
	KICK, SNARE, CLAP, RIM, CLOSED_HAT, PEDAL_HAT, OPEN_HAT, LOW_TOM, MID_TOM, HIGH_TOM, CRASH, RIDE, COWBELL,
	N_PIECES
} Piece;

enum
{
	PP_TUNE, PP_DECAY, PP_TONE, PP_SNAP, PP_LEVEL, PP_PAN, N_PP
};

static const gchar *piece_names[N_PIECES] = {
	"kick", "snare", "clap", "rim", "closed-hat", "pedal-hat", "open-hat", "low-tom", "mid-tom", "high-tom",
	"crash", "ride", "cowbell"
};

static const gchar *piece_alias[N_PIECES] = {
	"bd", "sd", "cp", "rs", "ch", "ph", "oh", "lt", "mt", "ht", "cy", "rd", "cb"
};

/* the key each piece plays on (General MIDI) */
static const gint piece_keys[N_PIECES] = { 36, 38, 39, 37, 42, 44, 46, 41, 45, 48, 49, 51, 56 };

static const gchar *pp_names[N_PP] = { "tune", "decay", "tone", "snap", "level", "pan" };
static const gdouble pp_min[N_PP] = { -24.0, 0.1, 0.0, 0.0, -60.0, -1.0 };
static const gdouble pp_max[N_PP] = { 24.0, 4.0, 1.0, 1.0, 12.0, 1.0 };
static const gdouble pp_def[N_PP] = { 0.0, 1.0, 0.5, 0.5, 0.0, 0.0 };
static const gchar *pp_blurbs[N_PP] = {
	"Pitch in semitones", "Length multiplier", "Brightness", "Attack / noise amount", "Level in dB",
	"Stereo position"
};

/* the six metallic oscillators of the 808 hats, Hz */
static const gdouble metal[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

#define SLOTS (2)

typedef struct
{
	gboolean          active;
	guint64           t;          /* samples since the hit */
	gdouble           vel;
	gdouble           phase[6];
	gdouble           choke;      /* 1, falls to 0 when choked */
	gboolean          choked;
	LrgDspBiquadCoef  c1, c2;
	LrgDspBiquadState s1, s2;
	guint             rate;       /* the rate the coefficients were made for */
} Voice;

struct _LrgDrumMachine
{
	LrgInstrument parent_instance;

	gdouble    pp[N_PIECES][N_PP];   /* piece parameters (properties) */
	gdouble    drive;                /* global saturation */
	LrgDrumKit kit;

	Voice      voices[N_PIECES][SLOTS];
	guint      next_slot[N_PIECES];
	guint64    rng;
};

G_DEFINE_FINAL_TYPE (LrgDrumMachine, lrg_drum_machine, LRG_TYPE_INSTRUMENT)

/* the property table is built once in class_init */
#define N_PARAMS (N_PIECES * N_PP + 1)
#define PROP_KIT (N_PARAMS + 1)

static LrgAudioParam params[N_PARAMS];
static GParamSpec   *specs[N_PARAMS + 1];

/* ==========================================================================
 * Kits
 * ========================================================================== */

typedef struct
{
	gdouble tune, decay, tone, snap, level, pan;
} KitPiece;

static const KitPiece kit_808[N_PIECES] = {
	{ 0, 1.3, 0.35, 0.25, 0, 0 },      /* kick */
	{ 0, 1.0, 0.45, 0.55, -3, 0 },     /* snare */
	{ 0, 1.0, 0.5, 0.5, -4, 0.1 },     /* clap */
	{ 0, 1.0, 0.5, 0.5, -8, -0.2 },    /* rim */
	{ 0, 1.0, 0.55, 0.15, -9, 0.25 },  /* closed hat */
	{ 0, 1.0, 0.55, 0.15, -10, 0.25 }, /* pedal hat */
	{ 0, 1.0, 0.55, 0.15, -10, 0.25 }, /* open hat */
	{ 0, 1.2, 0.4, 0.2, -4, -0.35 },   /* low tom */
	{ 0, 1.1, 0.4, 0.2, -4, 0 },       /* mid tom */
	{ 0, 1.0, 0.4, 0.2, -4, 0.35 },    /* high tom */
	{ 0, 1.0, 0.5, 0.4, -12, -0.3 },   /* crash */
	{ 0, 1.0, 0.5, 0.2, -13, 0.3 },    /* ride */
	{ 0, 1.0, 0.5, 0.5, -9, 0.2 },     /* cowbell */
};

static const KitPiece kit_909[N_PIECES] = {
	{ 2, 0.8, 0.6, 0.7, 0, 0 },
	{ 1, 0.9, 0.6, 0.75, -3, 0 },
	{ 0, 1.0, 0.55, 0.6, -4, 0.1 },
	{ 0, 0.8, 0.6, 0.6, -8, -0.2 },
	{ 0, 0.9, 0.7, 0.65, -9, 0.25 },
	{ 0, 0.9, 0.7, 0.65, -10, 0.25 },
	{ 0, 1.1, 0.7, 0.65, -10, 0.25 },
	{ 1, 1.0, 0.5, 0.35, -4, -0.35 },
	{ 1, 0.9, 0.5, 0.35, -4, 0 },
	{ 1, 0.8, 0.5, 0.35, -4, 0.35 },
	{ 0, 1.1, 0.65, 0.7, -12, -0.3 },
	{ 0, 1.2, 0.6, 0.45, -13, 0.3 },
	{ 0, 0.9, 0.55, 0.5, -9, 0.2 },
};

static const KitPiece kit_linn[N_PIECES] = {
	{ 1, 0.7, 0.45, 0.45, 0, 0 },
	{ 2, 0.7, 0.5, 0.6, -3, 0 },
	{ 0, 0.8, 0.5, 0.55, -4, 0.1 },
	{ 1, 0.6, 0.55, 0.55, -8, -0.2 },
	{ 1, 0.7, 0.6, 0.4, -9, 0.25 },
	{ 1, 0.7, 0.6, 0.4, -10, 0.25 },
	{ 1, 0.8, 0.6, 0.4, -10, 0.25 },
	{ -2, 0.6, 0.45, 0.3, -4, -0.35 },
	{ -1, 0.6, 0.45, 0.3, -4, 0 },
	{ 0, 0.6, 0.45, 0.3, -4, 0.35 },
	{ 0, 0.9, 0.55, 0.5, -12, -0.3 },
	{ 0, 0.9, 0.55, 0.3, -13, 0.3 },
	{ 0, 0.7, 0.5, 0.5, -9, 0.2 },
};

static const KitPiece *
kit_table (LrgDrumKit kit)
{
	switch (kit)
	{
	case LRG_DRUM_KIT_909: return kit_909;
	case LRG_DRUM_KIT_LINN: return kit_linn;
	case LRG_DRUM_KIT_808:
	default: return kit_808;
	}
}

/* ==========================================================================
 * Pieces and keys
 * ========================================================================== */

/* the piece a key plays (General MIDI drum map), or -1 */
static gint
piece_for_key (gint key)
{
	switch (key)
	{
	case 35: case 36: return KICK;
	case 37: return RIM;
	case 38: case 40: return SNARE;
	case 39: return CLAP;
	case 42: return CLOSED_HAT;
	case 44: return PEDAL_HAT;
	case 46: return OPEN_HAT;
	case 41: case 43: return LOW_TOM;
	case 45: case 47: return MID_TOM;
	case 48: case 50: return HIGH_TOM;
	case 49: case 52: case 55: case 57: return CRASH;
	case 51: case 53: case 59: return RIDE;
	case 56: return COWBELL;
	default: return -1;
	}
}

gchar **
lrg_drum_machine_list_pieces (void)
{
	gchar **names = g_new0 (gchar *, N_PIECES + 1);
	guint   i;

	for (i = 0; i < N_PIECES; i++)
		names[i] = g_strdup (piece_names[i]);
	return names;
}

gint
lrg_drum_machine_piece_to_key (const gchar *piece)
{
	guint i;

	if (piece == NULL)
		return -1;
	if (g_ascii_strcasecmp (piece, "hh") == 0 || g_ascii_strcasecmp (piece, "hat") == 0)
		return piece_keys[CLOSED_HAT];
	for (i = 0; i < N_PIECES; i++)
		if (g_ascii_strcasecmp (piece, piece_names[i]) == 0 || g_ascii_strcasecmp (piece, piece_alias[i]) == 0)
			return piece_keys[i];
	return -1;
}

const gchar *
lrg_drum_machine_key_to_piece (gint key)
{
	gint p = piece_for_key (key);

	return p >= 0 ? piece_names[p] : NULL;
}

/* ==========================================================================
 * Hits
 * ========================================================================== */

/* the frequency multiplier of a piece's tune */
static gdouble
tune_ratio (LrgDrumMachine *self,
            Piece           p)
{
	return pow (2.0, self->pp[p][PP_TUNE] / 12.0);
}

static void
drum_note_on (LrgInstrument *instrument,
              gint           key,
              gdouble        velocity)
{
	LrgDrumMachine *self = LRG_DRUM_MACHINE (instrument);
	gint            p = piece_for_key (key);
	Voice          *v;
	guint           i;

	if (p < 0)
		return;

	/* the closed and pedal hats choke the open hat */
	if (p == CLOSED_HAT || p == PEDAL_HAT)
		for (i = 0; i < SLOTS; i++)
			if (self->voices[OPEN_HAT][i].active)
				self->voices[OPEN_HAT][i].choked = TRUE;

	v = &self->voices[p][self->next_slot[p]];
	self->next_slot[p] = (self->next_slot[p] + 1) % SLOTS;
	memset (v, 0, sizeof *v);
	v->active = TRUE;
	v->vel = velocity;
	v->choke = 1.0;
	for (i = 0; i < 6; i++)
		v->phase[i] = lrg_dsp_rng_unit (&self->rng);
}

static void
drum_note_off (LrgInstrument *instrument,
               gint           key)
{
	/* drums are one-shots: a release does nothing */
	(void) instrument;
	(void) key;
}

static void
drum_all_off (LrgInstrument *instrument,
              gboolean       hard)
{
	LrgDrumMachine *self = LRG_DRUM_MACHINE (instrument);
	guint           p, i;

	for (p = 0; p < N_PIECES; p++)
		for (i = 0; i < SLOTS; i++)
		{
			if (hard)
				self->voices[p][i].active = FALSE;
			else if (self->voices[p][i].active)
				self->voices[p][i].choked = TRUE;
		}
}

static guint
drum_get_active_voices (LrgInstrument *instrument)
{
	LrgDrumMachine *self = LRG_DRUM_MACHINE (instrument);
	guint           p, i, n = 0;

	for (p = 0; p < N_PIECES; p++)
		for (i = 0; i < SLOTS; i++)
			if (self->voices[p][i].active)
				n++;
	return n;
}

static void
drum_reset_voices (LrgInstrument *instrument)
{
	LrgDrumMachine *self = LRG_DRUM_MACHINE (instrument);

	memset (self->voices, 0, sizeof self->voices);
	memset (self->next_slot, 0, sizeof self->next_slot);
	self->rng = lrg_dsp_rng_seed (0xD12F);
}

/* ==========================================================================
 * Synthesis
 * ========================================================================== */

/* sum of the six metallic squares at @ratio x their base frequencies */
static gdouble
metal_sample (Voice   *v,
              gdouble  ratio,
              guint    rate)
{
	gdouble sum = 0.0;
	guint   i;

	for (i = 0; i < 6; i++)
	{
		v->phase[i] += metal[i] * ratio / rate;
		v->phase[i] -= floor (v->phase[i]);
		sum += v->phase[i] < 0.5 ? 1.0 : -1.0;
	}
	return sum / 6.0;
}

/* designs a voice's two filters once per hit (and if the rate changes) */
static void
setup_filters (LrgDrumMachine *self,
               Piece           p,
               Voice          *v,
               guint           rate)
{
	gdouble tone = self->pp[p][PP_TONE];
	gdouble r = tune_ratio (self, p);

	switch (p)
	{
	case SNARE:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_HIGHPASS, 1200.0 + tone * 3500.0, 0.7071, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_LOWPASS, 7000.0 + tone * 8000.0, 0.7071, 0.0, rate);
		break;
	case CLAP:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_BANDPASS, 1100.0 * r * (0.7 + tone * 0.8), 1.4, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_HIGHPASS, 600.0, 0.7071, 0.0, rate);
		break;
	case CLOSED_HAT:
	case PEDAL_HAT:
	case OPEN_HAT:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_BANDPASS, (7000.0 + tone * 5000.0) * r, 0.9, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_HIGHPASS, 6000.0 * r, 0.7071, 0.0, rate);
		break;
	case CRASH:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_BANDPASS, (5000.0 + tone * 4000.0) * r, 0.6, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_HIGHPASS, 3500.0 * r, 0.7071, 0.0, rate);
		break;
	case RIDE:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_BANDPASS, (4500.0 + tone * 3000.0) * r, 0.8, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_HIGHPASS, 2800.0 * r, 0.7071, 0.0, rate);
		break;
	case COWBELL:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_BANDPASS, 820.0 * r * (0.8 + tone * 0.4), 2.2, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_HIGHPASS, 400.0, 0.7071, 0.0, rate);
		break;
	default:
		lrg_dsp_biquad_design (&v->c1, LRG_DSP_BIQUAD_HIGHPASS, 30.0, 0.7071, 0.0, rate);
		lrg_dsp_biquad_design (&v->c2, LRG_DSP_BIQUAD_LOWPASS, 8000.0, 0.7071, 0.0, rate);
		break;
	}
	v->rate = rate;
}

/*
 * piece_sample:
 * One sample of piece @p's voice @v at its current time.  Returns the
 * sample; sets *@done once the hit has died away.
 */
static gdouble
piece_sample (LrgDrumMachine *self,
              Piece           p,
              Voice          *v,
              guint           rate,
              gboolean       *done)
{
	gdouble t = (gdouble) v->t / rate;
	gdouble d = self->pp[p][PP_DECAY];
	gdouble tone = self->pp[p][PP_TONE];
	gdouble snap = self->pp[p][PP_SNAP];
	gdouble r = tune_ratio (self, p);
	gdouble n;
	gdouble env;
	gdouble y = 0.0;
	gdouble len;

	switch (p)
	{
	case KICK:
		{
			gdouble f0 = (self->kit == LRG_DRUM_KIT_909 ? 52.0 : self->kit == LRG_DRUM_KIT_LINN ? 58.0 : 46.0) * r;
			gdouble sweep = self->kit == LRG_DRUM_KIT_909 ? 0.012 : 0.022;
			gdouble f = f0 * (1.0 + (2.0 + tone * 5.0) * exp (-t / sweep));
			gdouble body_len = (self->kit == LRG_DRUM_KIT_808 ? 0.55 : 0.32) * d;

			v->phase[0] += f / rate;
			v->phase[0] -= floor (v->phase[0]);
			env = exp (-t / body_len);
			y = sin (2.0 * G_PI * v->phase[0]) * env;
			y += snap * lrg_dsp_rng_noise (&self->rng) * exp (-t / 0.0025) * 0.6;
			y = tanh (y * (1.3 + snap)) / tanh (1.3 + snap);
			len = body_len * 9.0;
		}
		break;
	case SNARE:
		{
			gdouble head = 0.62 * sin (2.0 * G_PI * 185.0 * r * t) * exp (-t / (0.07 * d))
			               + 0.38 * sin (2.0 * G_PI * 330.0 * r * t) * exp (-t / (0.045 * d));

			n = lrg_dsp_rng_noise (&self->rng);
			n = lrg_dsp_biquad_tick (&v->c1, &v->s1, n);
			n = lrg_dsp_biquad_tick (&v->c2, &v->s2, n) * exp (-t / (0.17 * d));
			y = head * (1.0 - snap * 0.55) + n * (0.45 + snap * 0.9);
			len = 0.17 * d * 9.0;
		}
		break;
	case CLAP:
		{
			static const gdouble bursts[4] = { 0.0, 0.011, 0.022, 0.034 };
			guint                i;

			env = 0.0;
			for (i = 0; i < 4; i++)
				if (t >= bursts[i])
					env += exp (-(t - bursts[i]) / 0.006) * (i == 3 ? 1.0 : 0.8);
			if (t >= bursts[3])
				env += 0.55 * exp (-(t - bursts[3]) / (0.16 * d));
			n = lrg_dsp_rng_noise (&self->rng);
			n = lrg_dsp_biquad_tick (&v->c1, &v->s1, n);
			n = lrg_dsp_biquad_tick (&v->c2, &v->s2, n);
			y = n * env * (1.6 + snap);
			len = bursts[3] + 0.16 * d * 9.0;
		}
		break;
	case RIM:
		env = exp (-t / (0.018 * d));
		y = (0.6 * sin (2.0 * G_PI * 1700.0 * r * t) + 0.4 * sin (2.0 * G_PI * 820.0 * r * t)) * env;
		y += snap * lrg_dsp_rng_noise (&self->rng) * exp (-t / 0.002) * 0.5;
		len = 0.018 * d * 9.0;
		break;
	case CLOSED_HAT:
	case PEDAL_HAT:
	case OPEN_HAT:
		{
			gdouble base = p == CLOSED_HAT ? 0.045 : p == PEDAL_HAT ? 0.09 : 0.5;

			n = metal_sample (v, r, rate) * (1.0 - snap * 0.7) + lrg_dsp_rng_noise (&self->rng) * snap;
			n = lrg_dsp_biquad_tick (&v->c1, &v->s1, n);
			n = lrg_dsp_biquad_tick (&v->c2, &v->s2, n);
			env = (t < 0.0008 ? t / 0.0008 : 1.0) * exp (-t / (base * d));
			y = n * env * 2.2;
			len = base * d * 9.0;
		}
		break;
	case LOW_TOM:
	case MID_TOM:
	case HIGH_TOM:
		{
			gdouble f0 = (p == LOW_TOM ? 85.0 : p == MID_TOM ? 120.0 : 165.0) * r;
			gdouble body = (self->kit == LRG_DRUM_KIT_LINN ? 0.22 : 0.4) * d;
			gdouble f = f0 * (1.0 + (0.4 + tone * 0.6) * exp (-t / 0.05));

			v->phase[0] += f / rate;
			v->phase[0] -= floor (v->phase[0]);
			env = exp (-t / body);
			y = sin (2.0 * G_PI * v->phase[0]) * env;
			y += snap * lrg_dsp_rng_noise (&self->rng) * exp (-t / 0.008) * 0.4;
			len = body * 9.0;
		}
		break;
	case CRASH:
	case RIDE:
		{
			gdouble body = (p == CRASH ? 0.6 : 0.8) * d;
			gdouble ratio = p == CRASH ? 2.3 * r : 1.6 * r;

			n = metal_sample (v, ratio, rate) * (1.0 - snap * 0.6) + lrg_dsp_rng_noise (&self->rng) * snap * 0.8;
			n = lrg_dsp_biquad_tick (&v->c1, &v->s1, n);
			n = lrg_dsp_biquad_tick (&v->c2, &v->s2, n);
			env = (t < 0.002 ? t / 0.002 : 1.0) * exp (-t / body);
			y = n * env * 2.0;
			if (p == RIDE)
				y += 0.15 * sin (2.0 * G_PI * 3450.0 * r * t) * exp (-t / (0.6 * d));
			len = body * 6.9;   /* -60 dB */
		}
		break;
	case COWBELL:
	default:
		{
			gdouble s1 = fmod (t * 540.0 * r, 1.0) < 0.5 ? 1.0 : -1.0;
			gdouble s2 = fmod (t * 800.0 * r, 1.0) < 0.5 ? 1.0 : -1.0;

			env = 0.6 * exp (-t / (0.02 * d)) + 0.4 * exp (-t / (0.28 * d));
			n = lrg_dsp_biquad_tick (&v->c1, &v->s1, (s1 + s2) * 0.5);
			n = lrg_dsp_biquad_tick (&v->c2, &v->s2, n);
			y = n * env * 2.5;
			len = 0.28 * d * 9.0;
		}
		break;
	}
	*done = t >= len;
	return y;
}

static void
drum_render_span (LrgInstrument *instrument,
                  gfloat        *out,
                  guint          frames,
                  guint          channels,
                  guint          sample_rate)
{
	LrgDrumMachine *self = LRG_DRUM_MACHINE (instrument);
	gdouble         volume = lrg_instrument_get_volume_gain (instrument);
	gdouble         drive = 1.0 + self->drive * 6.0;
	gdouble         choke_step = 1.0 / (0.003 * sample_rate);
	guint           p, i, f;

	for (p = 0; p < N_PIECES; p++)
	{
		gdouble level = lrg_dsp_db_to_lin (self->pp[p][PP_LEVEL]) * volume;
		gdouble pan = self->pp[p][PP_PAN];
		gdouble gl = channels == 2 ? sqrt (0.5 * (1.0 - pan)) * G_SQRT2 : 1.0;
		gdouble gr = channels == 2 ? sqrt (0.5 * (1.0 + pan)) * G_SQRT2 : 1.0;

		for (i = 0; i < SLOTS; i++)
		{
			Voice  *v = &self->voices[p][i];
			gdouble vel;

			if (!v->active)
				continue;
			if (v->rate != sample_rate)
				setup_filters (self, (Piece) p, v, sample_rate);
			vel = (0.3 + 0.7 * v->vel) * level * 0.5;
			for (f = 0; f < frames; f++)
			{
				gboolean done = FALSE;
				gdouble  y = piece_sample (self, (Piece) p, v, sample_rate, &done);

				if (self->drive > 0.0)
					y = tanh (y * drive) / tanh (drive);
				if (v->choked)
				{
					v->choke -= choke_step;
					if (v->choke <= 0.0)
						done = TRUE;
					y *= MAX (v->choke, 0.0);
				}
				y *= vel;
				if (channels == 2)
				{
					out[f * 2] += (gfloat) (y * gl);
					out[f * 2 + 1] += (gfloat) (y * gr);
				}
				else
					out[f] += (gfloat) y;
				v->t++;
				if (done)
				{
					v->active = FALSE;
					break;
				}
			}
		}
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_drum_machine_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	if (prop_id == PROP_KIT)
	{
		g_value_set_enum (value, (gint) LRG_DRUM_MACHINE (object)->kit);
		return;
	}
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_drum_machine_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	if (prop_id == PROP_KIT)
	{
		lrg_drum_machine_set_kit (LRG_DRUM_MACHINE (object), (LrgDrumKit) g_value_get_enum (value));
		return;
	}
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_drum_machine_class_init (LrgDrumMachineClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgInstrumentClass *instrument_class = LRG_INSTRUMENT_CLASS (klass);
	guint               p, j;

	object_class->get_property = lrg_drum_machine_get_property;
	object_class->set_property = lrg_drum_machine_set_property;
	instrument_class->note_on = drum_note_on;
	instrument_class->note_off = drum_note_off;
	instrument_class->all_off = drum_all_off;
	instrument_class->render_span = drum_render_span;
	instrument_class->get_active_voices = drum_get_active_voices;
	instrument_class->reset_voices = drum_reset_voices;

	/* "<piece>-<param>" for every piece, then "drive"; the names live as
	 * long as the class (forever), as static strings must */
	for (p = 0; p < N_PIECES; p++)
		for (j = 0; j < N_PP; j++)
		{
			LrgAudioParam    *ap = &params[p * N_PP + j];
			g_autofree gchar *name = g_strdup_printf ("%s-%s", piece_names[p], pp_names[j]);

			ap->name = g_intern_string (name);
			ap->blurb = pp_blurbs[j];
			ap->offset = G_STRUCT_OFFSET (LrgDrumMachine, pp) + (p * N_PP + j) * sizeof (gdouble);
			ap->min = pp_min[j];
			ap->max = pp_max[j];
			ap->def = pp_def[j];
		}
	params[N_PARAMS - 1].name = "drive";
	params[N_PARAMS - 1].blurb = "Saturation of the whole kit";
	params[N_PARAMS - 1].offset = G_STRUCT_OFFSET (LrgDrumMachine, drive);
	params[N_PARAMS - 1].min = 0.0;
	params[N_PARAMS - 1].max = 1.0;
	params[N_PARAMS - 1].def = 0.0;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);

	/**
	 * LrgDrumMachine:kit:
	 *
	 * The kit (#LrgDrumKit); setting it resets every piece parameter.
	 */
	g_object_class_install_property (object_class, PROP_KIT,
	                                 g_param_spec_enum ("kit", "kit", "Drum kit", LRG_TYPE_DRUM_KIT,
	                                                    LRG_DRUM_KIT_808,
	                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
lrg_drum_machine_init (LrgDrumMachine *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
	self->rng = lrg_dsp_rng_seed (0xD12F);
	g_object_set (self, "polyphony", (guint) (N_PIECES * SLOTS), NULL);
	lrg_drum_machine_set_kit (self, LRG_DRUM_KIT_808);
}

LrgDrumMachine *
lrg_drum_machine_new (LrgDrumKit kit)
{
	LrgDrumMachine *self = g_object_new (LRG_TYPE_DRUM_MACHINE, NULL);

	lrg_drum_machine_set_kit (self, kit);
	return self;
}

void
lrg_drum_machine_set_kit (LrgDrumMachine *self,
                          LrgDrumKit      kit)
{
	const KitPiece *table;
	guint           p;

	g_return_if_fail (LRG_IS_DRUM_MACHINE (self));

	if ((guint) kit > (guint) LRG_DRUM_KIT_LINN)
		kit = LRG_DRUM_KIT_808;
	table = kit_table (kit);
	self->kit = kit;
	for (p = 0; p < N_PIECES; p++)
	{
		self->pp[p][PP_TUNE] = table[p].tune;
		self->pp[p][PP_DECAY] = table[p].decay;
		self->pp[p][PP_TONE] = table[p].tone;
		self->pp[p][PP_SNAP] = table[p].snap;
		self->pp[p][PP_LEVEL] = table[p].level;
		self->pp[p][PP_PAN] = table[p].pan;
	}
	g_object_notify (G_OBJECT (self), "kit");
}

LrgDrumKit
lrg_drum_machine_get_kit (LrgDrumMachine *self)
{
	g_return_val_if_fail (LRG_IS_DRUM_MACHINE (self), LRG_DRUM_KIT_808);
	return self->kit;
}
