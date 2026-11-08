/* lrg-synth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The subtractive / FM synthesizer (see lrg-synth.h and
 * docs/modules/music/instruments.org).
 *
 * Signal path of one voice, per sample:
 *
 *   osc2 (unison) -> phase-modulates osc1 (fm) and is mixed (level, ring)
 *   osc1 (unison, supersaw) + sub (sine, an octave under osc1) + noise
 *   -> drive (tanh) -> state-variable filter (Cytomic's trapezoidal SVF,
 *      low/high/band) -> amp envelope x velocity x volume -> L/R
 *
 * Unison copies are detuned symmetrically over `detune` cents and spread
 * across the stereo field.  Pitch, cut-off and the filter coefficients are
 * updated every CONTROL frames ("control rate"); oscillators and envelopes
 * run every sample.  Saw and pulse use PolyBLEP to suppress aliasing.
 *
 * Voices: in poly mode a note takes a free voice; past the polyphony the
 * quietest released voice is stolen, else the oldest.  A stolen or
 * retriggered voice attacks from the level it is at, so it never clicks.
 * In mono mode one voice follows the most recent held key (last-note
 * priority); with legato a new key while one is held glides without
 * retriggering the envelopes.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-synth.h"
#include "../audio/lrg-audio-dsp-private.h"
#include "../audio/lrg-audio-param-private.h"

#define CONTROL      (16)
#define MAX_UNISON   (8)
#define MAX_VOICES   (LRG_INSTRUMENT_MAX_POLYPHONY)
#define SILENT_LEVEL (1e-5)

G_DEFINE_ENUM_TYPE (LrgSynthWave, lrg_synth_wave,
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_SINE, "sine"),
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_TRIANGLE, "triangle"),
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_SAW, "saw"),
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_PULSE, "pulse"),
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_NOISE, "noise"),
	G_DEFINE_ENUM_VALUE (LRG_SYNTH_WAVE_SUPERSAW, "supersaw"))

typedef enum
{
	ENV_IDLE,
	ENV_ATTACK,
	ENV_DECAY,
	ENV_SUSTAIN,
	ENV_RELEASE
} EnvStage;

typedef struct
{
	EnvStage stage;
	gdouble  level;
} Env;

typedef struct
{
	gboolean active;
	gboolean gate;         /* key held */
	gint     key;
	gdouble  vel_gain;
	guint64  age;          /* note-on order */
	gdouble  cur_key;      /* glides toward key */
	gdouble  ph1[MAX_UNISON];
	gdouble  ph2[MAX_UNISON];
	gdouble  phsub;
	Env      amp;
	Env      flt;
	gdouble  ic1[2];       /* SVF state per channel */
	gdouble  ic2[2];
} Voice;

struct _LrgSynth
{
	LrgInstrument parent_instance;

	/* parameters (properties, see params[]) */
	gdouble osc1_wave, osc1_octave, osc1_semi, osc1_fine, osc1_level, osc1_pw;
	gdouble osc2_wave, osc2_octave, osc2_semi, osc2_fine, osc2_level, osc2_pw;
	gdouble sub_level, noise_level;
	gdouble unison, detune, spread;
	gdouble fm, ring;
	gdouble filter_mode, cutoff, resonance, key_track, env_amount, drive;
	gdouble amp_attack, amp_decay, amp_sustain, amp_release;
	gdouble filter_attack, filter_decay, filter_sustain, filter_release;
	gdouble lfo1_rate, lfo1_shape, lfo1_pitch, lfo1_cutoff, lfo1_amp, lfo1_pw;
	gdouble lfo2_rate, lfo2_shape, lfo2_pitch, lfo2_cutoff, lfo2_amp, lfo2_pw;
	gdouble lfo_retrigger;
	gdouble glide, mono, legato, velocity;

	/* state */
	Voice   voices[MAX_VOICES];
	guint64 next_age;
	gdouble last_key;      /* where a gliding note starts */
	gdouble lfo_phase[2];
	gdouble lfo_hold[2];   /* sample-and-hold values */
	guint64 rng;
	GArray *held;          /* mono mode: held keys, oldest first (gint) */
};

G_DEFINE_FINAL_TYPE (LrgSynth, lrg_synth, LRG_TYPE_INSTRUMENT)

#define F(field) G_STRUCT_OFFSET (LrgSynth, field)

static const LrgAudioParam params[] = {
	{ "osc1-wave", "Oscillator 1: 0 sine, 1 triangle, 2 saw, 3 pulse, 4 noise, 5 supersaw", F (osc1_wave), 0, 5, 2 },
	{ "osc1-octave", "Oscillator 1 octave", F (osc1_octave), -3, 3, 0 },
	{ "osc1-semi", "Oscillator 1 semitones", F (osc1_semi), -12, 12, 0 },
	{ "osc1-fine", "Oscillator 1 fine tune in cents", F (osc1_fine), -100, 100, 0 },
	{ "osc1-level", "Oscillator 1 level", F (osc1_level), 0, 1, 1 },
	{ "osc1-pw", "Oscillator 1 pulse width", F (osc1_pw), 0.05, 0.95, 0.5 },
	{ "osc2-wave", "Oscillator 2: 0 sine, 1 triangle, 2 saw, 3 pulse, 4 noise, 5 supersaw", F (osc2_wave), 0, 5, 2 },
	{ "osc2-octave", "Oscillator 2 octave", F (osc2_octave), -3, 3, 0 },
	{ "osc2-semi", "Oscillator 2 semitones", F (osc2_semi), -12, 12, 0 },
	{ "osc2-fine", "Oscillator 2 fine tune in cents", F (osc2_fine), -100, 100, 7 },
	{ "osc2-level", "Oscillator 2 level", F (osc2_level), 0, 1, 0 },
	{ "osc2-pw", "Oscillator 2 pulse width", F (osc2_pw), 0.05, 0.95, 0.5 },
	{ "sub-level", "Sub oscillator (sine, an octave below oscillator 1)", F (sub_level), 0, 1, 0 },
	{ "noise-level", "White noise level", F (noise_level), 0, 1, 0 },
	{ "unison", "Unison copies of each oscillator (supersaw: at least 7)", F (unison), 1, 8, 1 },
	{ "detune", "Unison detune spread in cents", F (detune), 0, 100, 12 },
	{ "spread", "Unison stereo spread", F (spread), 0, 1, 0.5 },
	{ "fm", "Oscillator 2 phase-modulates oscillator 1 by this index", F (fm), 0, 10, 0 },
	{ "ring", "Ring modulation of oscillator 1 by oscillator 2", F (ring), 0, 1, 0 },
	{ "filter-mode", "Filter: 0 low-pass, 1 high-pass, 2 band-pass", F (filter_mode), 0, 2, 0 },
	{ "cutoff", "Filter cut-off in Hz", F (cutoff), 20, 20000, 18000 },
	{ "resonance", "Filter resonance", F (resonance), 0, 1, 0.1 },
	{ "key-track", "Cut-off follows the key (1: one octave per octave)", F (key_track), 0, 1, 0 },
	{ "env-amount", "Filter envelope depth in octaves", F (env_amount), -8, 8, 0 },
	{ "drive", "Saturation before the filter", F (drive), 0, 1, 0 },
	{ "amp-attack", "Amp envelope attack in seconds", F (amp_attack), 0, 10, 0.005 },
	{ "amp-decay", "Amp envelope decay in seconds", F (amp_decay), 0, 10, 0.2 },
	{ "amp-sustain", "Amp envelope sustain level", F (amp_sustain), 0, 1, 0.8 },
	{ "amp-release", "Amp envelope release in seconds", F (amp_release), 0, 10, 0.2 },
	{ "filter-attack", "Filter envelope attack in seconds", F (filter_attack), 0, 10, 0.005 },
	{ "filter-decay", "Filter envelope decay in seconds", F (filter_decay), 0, 10, 0.3 },
	{ "filter-sustain", "Filter envelope sustain level", F (filter_sustain), 0, 1, 0 },
	{ "filter-release", "Filter envelope release in seconds", F (filter_release), 0, 10, 0.3 },
	{ "lfo1-rate", "LFO 1 rate in Hz", F (lfo1_rate), 0.01, 40, 5 },
	{ "lfo1-shape", "LFO 1: 0 sine, 1 triangle, 2 square, 3 saw, 4 sample-and-hold", F (lfo1_shape), 0, 4, 0 },
	{ "lfo1-pitch", "LFO 1 to pitch in semitones", F (lfo1_pitch), 0, 24, 0 },
	{ "lfo1-cutoff", "LFO 1 to cut-off in octaves", F (lfo1_cutoff), 0, 8, 0 },
	{ "lfo1-amp", "LFO 1 to level (tremolo)", F (lfo1_amp), 0, 1, 0 },
	{ "lfo1-pw", "LFO 1 to pulse width", F (lfo1_pw), 0, 0.45, 0 },
	{ "lfo2-rate", "LFO 2 rate in Hz", F (lfo2_rate), 0.01, 40, 0.3 },
	{ "lfo2-shape", "LFO 2: 0 sine, 1 triangle, 2 square, 3 saw, 4 sample-and-hold", F (lfo2_shape), 0, 4, 1 },
	{ "lfo2-pitch", "LFO 2 to pitch in semitones", F (lfo2_pitch), 0, 24, 0 },
	{ "lfo2-cutoff", "LFO 2 to cut-off in octaves", F (lfo2_cutoff), 0, 8, 0 },
	{ "lfo2-amp", "LFO 2 to level (tremolo)", F (lfo2_amp), 0, 1, 0 },
	{ "lfo2-pw", "LFO 2 to pulse width", F (lfo2_pw), 0, 0.45, 0 },
	{ "lfo-retrigger", "1 restarts the LFOs on a note when nothing sounds", F (lfo_retrigger), 0, 1, 0 },
	{ "glide", "Portamento time in seconds", F (glide), 0, 5, 0 },
	{ "mono", "1 plays one note at a time", F (mono), 0, 1, 0 },
	{ "legato", "Mono: 1 glides between held notes without retriggering", F (legato), 0, 1, 1 },
	{ "velocity", "How much velocity changes the level", F (velocity), 0, 1, 0.7 },
};

#undef F

#define N_PARAMS (G_N_ELEMENTS (params))

/* table properties start after the base class's; ids 1 .. N_PARAMS here */
static GParamSpec *specs[N_PARAMS + 1];

/* ==========================================================================
 * Presets
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	gdouble      value;
} PresetValue;

typedef struct
{
	const gchar       *name;
	const gchar       *description;
	const PresetValue *values;
} Preset;

static const PresetValue p_init[] = { { NULL, 0 } };

static const PresetValue p_bass[] = {
	{ "osc1-wave", 2 }, { "osc2-wave", 3 }, { "osc2-octave", -1 }, { "osc2-level", 0.5 }, { "osc2-fine", 0 },
	{ "sub-level", 0.35 }, { "cutoff", 380 }, { "resonance", 0.3 }, { "env-amount", 3.2 },
	{ "filter-attack", 0.001 }, { "filter-decay", 0.16 }, { "filter-sustain", 0.08 }, { "filter-release", 0.1 },
	{ "amp-attack", 0.002 }, { "amp-decay", 0.3 }, { "amp-sustain", 0.75 }, { "amp-release", 0.07 },
	{ "drive", 0.15 }, { "polyphony", 4 }, { "volume", -3 }, { NULL, 0 }
};

static const PresetValue p_pad[] = {
	{ "osc1-wave", 2 }, { "unison", 5 }, { "detune", 18 }, { "spread", 0.9 },
	{ "osc2-wave", 2 }, { "osc2-octave", 1 }, { "osc2-level", 0.35 }, { "osc2-fine", 5 },
	{ "cutoff", 1800 }, { "resonance", 0.15 }, { "env-amount", 1.2 },
	{ "filter-attack", 1.2 }, { "filter-decay", 2.0 }, { "filter-sustain", 0.55 }, { "filter-release", 1.5 },
	{ "amp-attack", 0.6 }, { "amp-decay", 1.0 }, { "amp-sustain", 0.85 }, { "amp-release", 1.4 },
	{ "lfo2-rate", 0.15 }, { "lfo2-cutoff", 0.6 }, { "polyphony", 12 }, { "volume", -12 }, { NULL, 0 }
};

static const PresetValue p_lead[] = {
	{ "osc1-wave", 5 }, { "unison", 7 }, { "detune", 24 }, { "spread", 0.7 },
	{ "osc2-wave", 3 }, { "osc2-octave", 1 }, { "osc2-level", 0.22 }, { "osc2-fine", 0 },
	{ "cutoff", 4200 }, { "resonance", 0.2 }, { "env-amount", 1.5 },
	{ "filter-attack", 0.005 }, { "filter-decay", 0.45 }, { "filter-sustain", 0.5 },
	{ "amp-attack", 0.01 }, { "amp-decay", 0.25 }, { "amp-sustain", 0.85 }, { "amp-release", 0.3 },
	{ "lfo1-rate", 5.5 }, { "lfo1-pitch", 0.12 }, { "glide", 0.035 }, { "polyphony", 6 }, { "volume", -10 },
	{ NULL, 0 }
};

static const PresetValue p_pluck[] = {
	{ "osc1-wave", 3 }, { "osc1-pw", 0.35 }, { "osc2-wave", 2 }, { "osc2-level", 0.6 }, { "osc2-fine", 8 },
	{ "cutoff", 700 }, { "resonance", 0.35 }, { "env-amount", 4.2 },
	{ "filter-attack", 0.001 }, { "filter-decay", 0.14 }, { "filter-sustain", 0.0 }, { "filter-release", 0.12 },
	{ "amp-attack", 0.001 }, { "amp-decay", 0.28 }, { "amp-sustain", 0.0 }, { "amp-release", 0.15 },
	{ "polyphony", 8 }, { "volume", -8 }, { NULL, 0 }
};

static const PresetValue p_brass[] = {
	{ "osc1-wave", 2 }, { "unison", 3 }, { "detune", 10 }, { "osc2-wave", 2 }, { "osc2-level", 0.8 },
	{ "osc2-fine", -7 }, { "cutoff", 650 }, { "resonance", 0.2 }, { "env-amount", 3.5 },
	{ "filter-attack", 0.03 }, { "filter-decay", 0.35 }, { "filter-sustain", 0.3 }, { "filter-release", 0.2 },
	{ "amp-attack", 0.01 }, { "amp-decay", 0.4 }, { "amp-sustain", 0.6 }, { "amp-release", 0.2 },
	{ "polyphony", 8 }, { "volume", -10 }, { NULL, 0 }
};

static const PresetValue p_sub[] = {
	{ "osc1-wave", 0 }, { "cutoff", 20000 }, { "amp-attack", 0.005 }, { "amp-decay", 0.5 },
	{ "amp-sustain", 0.9 }, { "amp-release", 0.1 }, { "mono", 1 }, { "volume", -2 }, { NULL, 0 }
};

static const PresetValue p_acid[] = {
	{ "osc1-wave", 3 }, { "cutoff", 280 }, { "resonance", 0.78 }, { "env-amount", 4.0 },
	{ "filter-attack", 0.001 }, { "filter-decay", 0.2 }, { "filter-sustain", 0.0 },
	{ "amp-attack", 0.002 }, { "amp-decay", 0.3 }, { "amp-sustain", 0.8 }, { "amp-release", 0.05 },
	{ "drive", 0.4 }, { "glide", 0.06 }, { "mono", 1 }, { "legato", 1 }, { "volume", -6 }, { NULL, 0 }
};

static const PresetValue p_bell[] = {
	{ "osc1-wave", 0 }, { "osc2-wave", 0 }, { "osc2-octave", 1 }, { "osc2-semi", 7 }, { "osc2-fine", 0 },
	{ "fm", 2.5 }, { "cutoff", 20000 }, { "amp-attack", 0.001 }, { "amp-decay", 1.6 },
	{ "amp-sustain", 0.0 }, { "amp-release", 1.2 }, { "polyphony", 10 }, { "volume", -9 }, { NULL, 0 }
};

static const PresetValue p_strings[] = {
	{ "osc1-wave", 2 }, { "unison", 6 }, { "detune", 14 }, { "spread", 1.0 }, { "cutoff", 3000 },
	{ "resonance", 0.1 }, { "amp-attack", 0.35 }, { "amp-decay", 0.5 }, { "amp-sustain", 0.9 },
	{ "amp-release", 0.8 }, { "lfo1-rate", 5.0 }, { "lfo1-pitch", 0.08 }, { "polyphony", 12 },
	{ "volume", -13 }, { NULL, 0 }
};

static const PresetValue p_riser[] = {
	{ "osc1-level", 0 }, { "noise-level", 1 }, { "filter-mode", 2 }, { "cutoff", 400 }, { "resonance", 0.5 },
	{ "env-amount", 5.5 }, { "filter-attack", 4.0 }, { "filter-decay", 0.1 }, { "filter-sustain", 1.0 },
	{ "amp-attack", 2.0 }, { "amp-sustain", 1.0 }, { "amp-release", 0.8 }, { "volume", -14 }, { NULL, 0 }
};

static const PresetValue p_organ[] = {
	{ "osc1-wave", 0 }, { "osc2-wave", 0 }, { "osc2-octave", 1 }, { "osc2-level", 0.5 }, { "osc2-fine", 0 },
	{ "sub-level", 0.5 }, { "amp-attack", 0.005 }, { "amp-sustain", 1.0 }, { "amp-release", 0.05 },
	{ "polyphony", 10 }, { "volume", -12 }, { NULL, 0 }
};

static const Preset presets[] = {
	{ "init", "One saw through an open filter", p_init },
	{ "synthwave-bass", "Saw and pulse an octave down with a plucky filter, 80s octave bass", p_bass },
	{ "synthwave-pad", "Wide detuned saws with a slow filter swell", p_pad },
	{ "synthwave-lead", "Supersaw lead with vibrato and a short glide", p_lead },
	{ "arp-pluck", "Short resonant pulse pluck for arpeggios", p_pluck },
	{ "brass-stab", "Detuned saws with a brassy filter envelope", p_brass },
	{ "sub-bass", "Mono sine sub", p_sub },
	{ "acid-bass", "Resonant squelchy mono bass with slides", p_acid },
	{ "bell", "FM bell", p_bell },
	{ "strings", "Ensemble strings", p_strings },
	{ "noise-riser", "Band-passed noise sweeping up", p_riser },
	{ "organ", "Drawbar-style sine organ", p_organ },
};

/* ==========================================================================
 * Envelopes, oscillators, LFOs
 * ========================================================================== */

static void
env_trigger (Env *env)
{
	env->stage = ENV_ATTACK;   /* from the current level: no click */
}

static void
env_release (Env *env)
{
	if (env->stage != ENV_IDLE)
		env->stage = ENV_RELEASE;
}

/* per-sample steps of an ADSR, computed at control rate */
typedef struct
{
	gdouble attack;    /* level added per sample (> 1: instant) */
	gdouble decay;     /* one-pole coefficient toward sustain */
	gdouble sustain;
	gdouble release;   /* one-pole coefficient toward 0 */
} EnvRates;

static void
env_rates (EnvRates *r,
           gdouble   attack,
           gdouble   decay,
           gdouble   sustain,
           gdouble   release,
           guint     rate)
{
	r->attack = attack > 0.0 ? 1.0 / (attack * rate) : 2.0;
	r->decay = lrg_dsp_one_pole (decay / 3.0, rate);
	r->sustain = sustain;
	r->release = lrg_dsp_one_pole (release / 3.0, rate);
}

/* one sample of an ADSR (linear attack, exponential decay and release) */
static gdouble
env_tick (Env            *env,
          const EnvRates *r)
{
	switch (env->stage)
	{
	case ENV_ATTACK:
		env->level += r->attack;
		if (env->level >= 1.0)
		{
			env->level = 1.0;
			env->stage = ENV_DECAY;
		}
		break;
	case ENV_DECAY:
		env->level += (r->sustain - env->level) * r->decay;
		if (fabs (env->level - r->sustain) < 1e-4)
		{
			env->level = r->sustain;
			env->stage = ENV_SUSTAIN;
		}
		break;
	case ENV_SUSTAIN:
		env->level = r->sustain;
		break;
	case ENV_RELEASE:
		env->level -= env->level * r->release;
		if (env->level < SILENT_LEVEL)
		{
			env->level = 0.0;
			env->stage = ENV_IDLE;
		}
		break;
	case ENV_IDLE:
	default:
		env->level = 0.0;
		break;
	}
	return env->level;
}

static inline gdouble
poly_blep (gdouble t,
           gdouble dt)
{
	if (dt <= 0.0)
		return 0.0;
	if (t < dt)
	{
		t /= dt;
		return t + t - t * t - 1.0;
	}
	if (t > 1.0 - dt)
	{
		t = (t - 1.0) / dt;
		return t * t + t + t + 1.0;
	}
	return 0.0;
}

static inline gdouble
wrap01 (gdouble x)
{
	return x - floor (x);
}

/* one sample of @wave at phase @t (0 .. 1) advancing by @dt per sample */
static inline gdouble
osc_sample (guint    wave,
            gdouble  t,
            gdouble  dt,
            gdouble  pw,
            guint64 *rng)
{
	switch (wave)
	{
	case LRG_SYNTH_WAVE_SINE:
		return sin (2.0 * G_PI * t);
	case LRG_SYNTH_WAVE_TRIANGLE:
		return 1.0 - 4.0 * fabs (t - 0.5);
	case LRG_SYNTH_WAVE_PULSE:
		return (t < pw ? 1.0 : -1.0) + poly_blep (t, dt) - poly_blep (wrap01 (t + 1.0 - pw), dt);
	case LRG_SYNTH_WAVE_NOISE:
		return lrg_dsp_rng_noise (rng);
	case LRG_SYNTH_WAVE_SAW:
	case LRG_SYNTH_WAVE_SUPERSAW:
	default:
		return 2.0 * t - 1.0 - poly_blep (t, dt);
	}
}

/* the LFO value (-1 .. 1) of @shape at phase @t; @hold is the S&H value */
static gdouble
lfo_value (guint   shape,
           gdouble t,
           gdouble hold)
{
	switch (shape)
	{
	case 1: return 1.0 - 4.0 * fabs (t - 0.5);
	case 2: return t < 0.5 ? 1.0 : -1.0;
	case 3: return 2.0 * t - 1.0;
	case 4: return hold;
	default: return sin (2.0 * G_PI * t);
	}
}

/* ==========================================================================
 * Voices
 * ========================================================================== */

static guint
count_active (LrgSynth *self)
{
	guint i, n = 0;

	for (i = 0; i < MAX_VOICES; i++)
		if (self->voices[i].active)
			n++;
	return n;
}

/* a voice for a new note: free, else steal (quietest released, else oldest) */
static Voice *
pick_voice (LrgSynth *self)
{
	guint  poly = lrg_instrument_get_polyphony (LRG_INSTRUMENT (self));
	Voice *best = NULL;
	guint  i;

	if (count_active (self) < poly)
	{
		for (i = 0; i < MAX_VOICES; i++)
			if (!self->voices[i].active)
				return &self->voices[i];
	}
	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice *v = &self->voices[i];

		if (!v->active || v->gate)
			continue;
		if (best == NULL || v->amp.level < best->amp.level)
			best = v;
	}
	if (best != NULL)
		return best;
	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice *v = &self->voices[i];

		if (v->active && (best == NULL || v->age < best->age))
			best = v;
	}
	return best != NULL ? best : &self->voices[0];
}

static void
start_voice (LrgSynth *self,
             Voice    *v,
             gint      key,
             gdouble   velocity,
             gboolean  retrigger)
{
	gboolean was_active = v->active;

	v->active = TRUE;
	v->gate = TRUE;
	v->key = key;
	v->vel_gain = 1.0 - self->velocity * (1.0 - velocity);
	v->age = ++self->next_age;
	if (!was_active)
	{
		guint u;

		/* a fresh voice: glide from the last note, random-ish phases so
		 * unison copies do not start in phase (deterministic per note) */
		v->cur_key = self->glide > 0.0 ? self->last_key : (gdouble) key;
		for (u = 0; u < MAX_UNISON; u++)
		{
			v->ph1[u] = lrg_dsp_rng_unit (&self->rng);
			v->ph2[u] = lrg_dsp_rng_unit (&self->rng);
		}
		v->phsub = 0.0;
		v->amp.level = 0.0;
		v->flt.level = 0.0;
		memset (v->ic1, 0, sizeof v->ic1);
		memset (v->ic2, 0, sizeof v->ic2);
	}
	else if (self->glide <= 0.0)
		v->cur_key = (gdouble) key;
	if (retrigger || !was_active)
	{
		env_trigger (&v->amp);
		env_trigger (&v->flt);
	}
	self->last_key = (gdouble) key;
}

static void
synth_note_on (LrgInstrument *instrument,
               gint           key,
               gdouble        velocity)
{
	LrgSynth *self = LRG_SYNTH (instrument);
	guint     i;

	if (self->lfo_retrigger >= 0.5 && count_active (self) == 0)
	{
		self->lfo_phase[0] = 0.0;
		self->lfo_phase[1] = 0.0;
	}

	if (self->mono >= 0.5)
	{
		Voice   *v = &self->voices[0];
		gboolean legato = self->legato >= 0.5 && v->active && v->gate;

		g_array_append_val (self->held, key);
		start_voice (self, v, key, velocity, !legato);
		return;
	}

	/* the same key again retriggers its voice */
	for (i = 0; i < MAX_VOICES; i++)
		if (self->voices[i].active && self->voices[i].gate && self->voices[i].key == key)
		{
			start_voice (self, &self->voices[i], key, velocity, TRUE);
			return;
		}
	start_voice (self, pick_voice (self), key, velocity, TRUE);
}

static void
synth_note_off (LrgInstrument *instrument,
                gint           key)
{
	LrgSynth *self = LRG_SYNTH (instrument);
	guint     i;

	if (self->mono >= 0.5)
	{
		Voice *v = &self->voices[0];

		for (i = self->held->len; i > 0; i--)
			if (g_array_index (self->held, gint, i - 1) == key)
			{
				g_array_remove_index (self->held, i - 1);
				break;
			}
		if (!v->active || v->key != key)
			return;
		if (self->held->len > 0)
		{
			/* back to the most recent key still held, legato */
			gint back = g_array_index (self->held, gint, self->held->len - 1);

			v->key = back;
			if (self->glide <= 0.0)
				v->cur_key = (gdouble) back;
			self->last_key = (gdouble) back;
			return;
		}
		v->gate = FALSE;
		env_release (&v->amp);
		env_release (&v->flt);
		return;
	}
	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice *v = &self->voices[i];

		if (v->active && v->gate && v->key == key)
		{
			v->gate = FALSE;
			env_release (&v->amp);
			env_release (&v->flt);
		}
	}
}

static void
synth_all_off (LrgInstrument *instrument,
               gboolean       hard)
{
	LrgSynth *self = LRG_SYNTH (instrument);
	guint     i;

	g_array_set_size (self->held, 0);
	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice *v = &self->voices[i];

		if (hard)
			memset (v, 0, sizeof *v);
		else if (v->active)
		{
			v->gate = FALSE;
			env_release (&v->amp);
			env_release (&v->flt);
		}
	}
}

static guint
synth_get_active_voices (LrgInstrument *instrument)
{
	return count_active (LRG_SYNTH (instrument));
}

static void
synth_reset_voices (LrgInstrument *instrument)
{
	LrgSynth *self = LRG_SYNTH (instrument);

	memset (self->voices, 0, sizeof self->voices);
	g_array_set_size (self->held, 0);
	self->lfo_phase[0] = self->lfo_phase[1] = 0.0;
	self->lfo_hold[0] = self->lfo_hold[1] = 0.0;
	self->next_age = 0;
	self->last_key = 60.0;
	self->rng = lrg_dsp_rng_seed (0x5EED);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/* per-control-block values of one voice */
typedef struct
{
	gdouble inc1[MAX_UNISON];
	gdouble inc2[MAX_UNISON];
	gdouble incsub;
	gdouble gl[MAX_UNISON];    /* unison pan gains */
	gdouble gr[MAX_UNISON];
	guint   n1;                /* oscillator 1 copies */
	guint   n2;
	gdouble pw1, pw2;
	gdouble a1, a2, a3, k;     /* SVF coefficients */
	gdouble amp_mod;
	EnvRates amp;
	EnvRates flt;
} Control;

static void
voice_control (LrgSynth *self,
               Voice    *v,
               Control  *c,
               gdouble   lfo1,
               gdouble   lfo2,
               guint     n,
               guint     rate)
{
	gdouble bend = lrg_instrument_get_pitch_bend (LRG_INSTRUMENT (self));
	guint   w1 = (guint) round (self->osc1_wave);
	guint   w2 = (guint) round (self->osc2_wave);
	guint   uni = (guint) CLAMP (round (self->unison), 1.0, (gdouble) MAX_UNISON);
	gdouble key;
	gdouble cutoff;
	gdouble g;
	guint   u;

	/* glide toward the note */
	if (self->glide > 0.0)
		v->cur_key += ((gdouble) v->key - v->cur_key) * (1.0 - exp (-(gdouble) n / (self->glide / 3.0 * rate)));
	else
		v->cur_key = (gdouble) v->key;
	key = v->cur_key + bend + lfo1 * self->lfo1_pitch + lfo2 * self->lfo2_pitch;

	c->n1 = w1 == LRG_SYNTH_WAVE_SUPERSAW ? MAX (uni, 7u) : uni;
	c->n2 = w2 == LRG_SYNTH_WAVE_SUPERSAW ? MAX (uni, 7u) : uni;
	c->n1 = MIN (c->n1, (guint) MAX_UNISON);
	c->n2 = MIN (c->n2, (guint) MAX_UNISON);
	for (u = 0; u < MAX_UNISON; u++)
	{
		gdouble pos1 = c->n1 > 1 ? (gdouble) u / (c->n1 - 1) * 2.0 - 1.0 : 0.0;
		gdouble pos2 = c->n2 > 1 ? (gdouble) u / (c->n2 - 1) * 2.0 - 1.0 : 0.0;
		gdouble k1 = key + self->osc1_octave * 12.0 + self->osc1_semi + self->osc1_fine / 100.0
		             + pos1 * self->detune / 200.0;
		gdouble k2 = key + self->osc2_octave * 12.0 + self->osc2_semi + self->osc2_fine / 100.0
		             + pos2 * self->detune / 200.0;
		gdouble pan = pos1 * self->spread;

		c->inc1[u] = MIN (lrg_dsp_key_to_hz (k1) / rate, 0.49);
		c->inc2[u] = MIN (lrg_dsp_key_to_hz (k2) / rate, 0.49);
		c->gl[u] = sqrt (0.5 * (1.0 - pan));
		c->gr[u] = sqrt (0.5 * (1.0 + pan));
	}
	c->incsub = MIN (lrg_dsp_key_to_hz (key + self->osc1_octave * 12.0 + self->osc1_semi - 12.0) / rate, 0.49);
	c->pw1 = CLAMP (self->osc1_pw + lfo1 * self->lfo1_pw + lfo2 * self->lfo2_pw, 0.05, 0.95);
	c->pw2 = CLAMP (self->osc2_pw + lfo1 * self->lfo1_pw + lfo2 * self->lfo2_pw, 0.05, 0.95);

	/* the cut-off: base, envelope, key tracking, LFOs (octaves) */
	cutoff = self->cutoff * pow (2.0, self->env_amount * v->flt.level + lfo1 * self->lfo1_cutoff
	                                  + lfo2 * self->lfo2_cutoff + self->key_track * (key - 60.0) / 12.0);
	cutoff = CLAMP (cutoff, 20.0, 0.45 * rate);
	g = tan (G_PI * cutoff / rate);
	c->k = 2.0 - 1.96 * self->resonance;
	c->a1 = 1.0 / (1.0 + g * (g + c->k));
	c->a2 = g * c->a1;
	c->a3 = g * c->a2;
	env_rates (&c->amp, self->amp_attack, self->amp_decay, self->amp_sustain, self->amp_release, rate);
	env_rates (&c->flt, self->filter_attack, self->filter_decay, self->filter_sustain, self->filter_release, rate);
	c->amp_mod = (1.0 - self->lfo1_amp * 0.5 * (1.0 - lfo1)) * (1.0 - self->lfo2_amp * 0.5 * (1.0 - lfo2));
}

/* one SVF sample on channel @ch: low, high or band output */
static inline gdouble
svf_tick (Voice         *v,
          const Control *c,
          guint          ch,
          guint          mode,
          gdouble        x)
{
	gdouble v3 = x - v->ic2[ch];
	gdouble v1 = c->a1 * v->ic1[ch] + c->a2 * v3;
	gdouble v2 = v->ic2[ch] + c->a2 * v->ic1[ch] + c->a3 * v3;

	v->ic1[ch] = 2.0 * v1 - v->ic1[ch];
	v->ic2[ch] = 2.0 * v2 - v->ic2[ch];
	if (fabs (v->ic1[ch]) < 1e-20)
		v->ic1[ch] = 0.0;
	if (fabs (v->ic2[ch]) < 1e-20)
		v->ic2[ch] = 0.0;
	if (mode == 1)
		return x - c->k * v1 - v2;
	if (mode == 2)
		return v1;
	return v2;
}

static void
render_voice (LrgSynth      *self,
              Voice         *v,
              const Control *c,
              gfloat        *out,
              guint          frames,
              guint          channels,
              guint          rate,
              gdouble        gain)
{
	guint   w1 = (guint) round (self->osc1_wave);
	guint   w2 = (guint) round (self->osc2_wave);
	guint   mode = (guint) round (self->filter_mode);
	gdouble norm1 = 1.0 / sqrt ((gdouble) c->n1);
	gdouble norm2 = 1.0 / sqrt ((gdouble) c->n2);
	gdouble drive = 1.0 + self->drive * 8.0;
	gboolean osc2_needed = self->osc2_level > 0.0 || self->fm > 0.0 || self->ring > 0.0;
	guint   f, u;

	for (f = 0; f < frames; f++)
	{
		gdouble l = 0.0, r = 0.0;
		gdouble mono_part;
		gdouble amp;
		gdouble fl, fr;

		amp = env_tick (&v->amp, &c->amp);
		env_tick (&v->flt, &c->flt);

		for (u = 0; u < MAX (c->n1, c->n2); u++)
		{
			gdouble o2 = 0.0;
			gdouble o1 = 0.0;

			if (osc2_needed && u < c->n2)
			{
				o2 = osc_sample (w2, v->ph2[u], c->inc2[u], c->pw2, &self->rng) * norm2;
				v->ph2[u] = wrap01 (v->ph2[u] + c->inc2[u]);
			}
			if (u < c->n1)
			{
				gdouble t = self->fm > 0.0 ? wrap01 (v->ph1[u] + self->fm * o2 * 0.25) : v->ph1[u];

				o1 = osc_sample (w1, t, c->inc1[u], c->pw1, &self->rng) * norm1;
				v->ph1[u] = wrap01 (v->ph1[u] + c->inc1[u]);
				if (self->ring > 0.0)
					o1 = o1 * (1.0 - self->ring) + o1 * o2 * self->ring * 2.0;
				o1 *= self->osc1_level;
			}
			o2 *= self->osc2_level;
			l += (o1 + o2) * c->gl[MIN (u, c->n1 - 1)];
			r += (o1 + o2) * c->gr[MIN (u, c->n1 - 1)];
		}

		/* centred parts: sub and noise */
		mono_part = 0.0;
		if (self->sub_level > 0.0)
		{
			mono_part += sin (2.0 * G_PI * v->phsub) * self->sub_level;
			v->phsub = wrap01 (v->phsub + c->incsub);
		}
		if (self->noise_level > 0.0)
			mono_part += lrg_dsp_rng_noise (&self->rng) * self->noise_level * 0.5;
		l += mono_part * G_SQRT2 * 0.5;
		r += mono_part * G_SQRT2 * 0.5;

		if (self->drive > 0.0)
		{
			l = tanh (l * drive) / sqrt (drive);
			r = tanh (r * drive) / sqrt (drive);
		}
		fl = svf_tick (v, c, 0, mode, l);
		fr = svf_tick (v, c, 1, mode, r);
		amp *= v->vel_gain * gain * c->amp_mod;
		if (channels == 2)
		{
			out[f * 2] += (gfloat) (fl * amp);
			out[f * 2 + 1] += (gfloat) (fr * amp);
		}
		else
			out[f] += (gfloat) ((fl + fr) * 0.5 * amp);
		if (v->amp.stage == ENV_IDLE)
		{
			v->active = FALSE;
			return;
		}
	}
}

static void
synth_render_span (LrgInstrument *instrument,
                   gfloat        *out,
                   guint          frames,
                   guint          channels,
                   guint          sample_rate)
{
	LrgSynth *self = LRG_SYNTH (instrument);
	gdouble   gain = lrg_instrument_get_volume_gain (instrument) * 0.5;
	guint     done = 0;
	guint     i;

	while (done < frames)
	{
		guint   n = MIN (frames - done, (guint) CONTROL);
		gdouble lfo[2];
		guint   j;

		/* the LFOs, once per control block */
		for (j = 0; j < 2; j++)
		{
			gdouble rate_hz = j == 0 ? self->lfo1_rate : self->lfo2_rate;
			guint   shape = (guint) round (j == 0 ? self->lfo1_shape : self->lfo2_shape);
			gdouble next = self->lfo_phase[j] + rate_hz * n / sample_rate;

			if (next >= 1.0)
				self->lfo_hold[j] = lrg_dsp_rng_noise (&self->rng);
			self->lfo_phase[j] = wrap01 (next);
			lfo[j] = lfo_value (shape, self->lfo_phase[j], self->lfo_hold[j]);
		}

		for (i = 0; i < MAX_VOICES; i++)
		{
			Voice  *v = &self->voices[i];
			Control c;

			if (!v->active)
				continue;
			voice_control (self, v, &c, lfo[0], lfo[1], n, sample_rate);
			render_voice (self, v, &c, out + (gsize) done * channels, n, channels, sample_rate, gain);
		}
		done += n;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_synth_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_synth_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_synth_finalize (GObject *object)
{
	g_array_unref (LRG_SYNTH (object)->held);
	G_OBJECT_CLASS (lrg_synth_parent_class)->finalize (object);
}

static void
lrg_synth_class_init (LrgSynthClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgInstrumentClass *instrument_class = LRG_INSTRUMENT_CLASS (klass);

	object_class->get_property = lrg_synth_get_property;
	object_class->set_property = lrg_synth_set_property;
	object_class->finalize = lrg_synth_finalize;
	instrument_class->note_on = synth_note_on;
	instrument_class->note_off = synth_note_off;
	instrument_class->all_off = synth_all_off;
	instrument_class->render_span = synth_render_span;
	instrument_class->get_active_voices = synth_get_active_voices;
	instrument_class->reset_voices = synth_reset_voices;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_synth_init (LrgSynth *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
	self->held = g_array_new (FALSE, FALSE, sizeof (gint));
	self->last_key = 60.0;
	self->rng = lrg_dsp_rng_seed (0x5EED);
}

LrgSynth *
lrg_synth_new (void)
{
	return g_object_new (LRG_TYPE_SYNTH, NULL);
}

static const Preset *
find_preset (const gchar *name)
{
	guint i;

	if (name == NULL)
		return NULL;
	for (i = 0; i < G_N_ELEMENTS (presets); i++)
		if (g_str_equal (presets[i].name, name))
			return &presets[i];
	return NULL;
}

gboolean
lrg_synth_load_preset (LrgSynth    *self,
                       const gchar *name)
{
	const Preset *preset;
	guint         i;

	g_return_val_if_fail (LRG_IS_SYNTH (self), FALSE);

	preset = find_preset (name);
	if (preset == NULL)
		return FALSE;
	g_object_freeze_notify (G_OBJECT (self));
	for (i = 0; i < N_PARAMS; i++)
		lrg_audio_params_set_clamped (G_OBJECT (self), params[i].name, params[i].def);
	lrg_audio_params_set_clamped (G_OBJECT (self), "polyphony", 16);
	lrg_audio_params_set_clamped (G_OBJECT (self), "volume", 0);
	for (i = 0; preset->values[i].name != NULL; i++)
		lrg_audio_params_set_clamped (G_OBJECT (self), preset->values[i].name, preset->values[i].value);
	g_object_thaw_notify (G_OBJECT (self));
	return TRUE;
}

LrgSynth *
lrg_synth_new_preset (const gchar *name)
{
	LrgSynth *self;

	if (find_preset (name) == NULL)
		return NULL;
	self = lrg_synth_new ();
	lrg_synth_load_preset (self, name);
	return self;
}

gchar **
lrg_synth_list_presets (void)
{
	gchar **names = g_new0 (gchar *, G_N_ELEMENTS (presets) + 1);
	guint   i;

	for (i = 0; i < G_N_ELEMENTS (presets); i++)
		names[i] = g_strdup (presets[i].name);
	return names;
}

const gchar *
lrg_synth_get_preset_description (const gchar *name)
{
	const Preset *preset = find_preset (name);

	return preset != NULL ? preset->description : NULL;
}
