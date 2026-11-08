/* lrg-gate.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Noise gate (see lrg-gate.h).  A stereo-linked peak detector opens
 * the gate at the threshold; once the level falls below, the gate
 * stays open for the hold time, then closes.  The gain moves to 1 with
 * the attack time constant and to the range with the release one.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-gate.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

struct _LrgGate
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble threshold;   /* dBFS */
	gdouble attack;   /* seconds */
	gdouble hold;   /* seconds */
	gdouble release;   /* seconds */
	gdouble range;   /* dB */
	gdouble gain;
	guint64 hold_left;   /* frames */
};

G_DEFINE_FINAL_TYPE (LrgGate, lrg_gate, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgGate:threshold:
 *
 * Level (dBFS) that opens the gate.
 */
/**
 * LrgGate:attack:
 *
 * Opening time in seconds.
 */
/**
 * LrgGate:hold:
 *
 * Seconds the gate stays open after the level falls.
 */
/**
 * LrgGate:release:
 *
 * Closing time in seconds.
 */
/**
 * LrgGate:range:
 *
 * Attenuation while closed in dB.
 */
static const LrgAudioParam params[] = {
	{ "threshold", "Level (dBFS) that opens the gate",
	  G_STRUCT_OFFSET (LrgGate, threshold), -90.0, 0.0, -40.0 },
	{ "attack", "Opening time in seconds",
	  G_STRUCT_OFFSET (LrgGate, attack), 0.0001, 0.1, 0.0005 },
	{ "hold", "Seconds the gate stays open after the level falls",
	  G_STRUCT_OFFSET (LrgGate, hold), 0.0, 2.0, 0.02 },
	{ "release", "Closing time in seconds",
	  G_STRUCT_OFFSET (LrgGate, release), 0.001, 3.0, 0.06 },
	{ "range", "Attenuation while closed in dB",
	  G_STRUCT_OFFSET (LrgGate, range), -90.0, 0.0, -90.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))


static GParamSpec *specs[N_PARAMS + 1];

static void
lrg_gate_reset (LrgAudioEffect *effect)
{
	LrgGate *self = LRG_GATE (effect);

	self->gain = 0.0;
	self->hold_left = 0;
}

static void
lrg_gate_process (LrgAudioEffect *effect,
                  gfloat         *frames,
                  guint           n_frames,
                  guint           channels,
                  guint           sample_rate)
{
	LrgGate *self = LRG_GATE (effect);
	gdouble thr = lrg_dsp_db_to_lin (self->threshold);
	gdouble floor_gain = lrg_dsp_db_to_lin (self->range);
	gdouble att = lrg_dsp_one_pole (self->attack, sample_rate);
	gdouble rel = lrg_dsp_one_pole (self->release, sample_rate);
	guint64 hold = (guint64) llround (self->hold * (gdouble) sample_rate);
	guint   f, c;

	for (f = 0; f < n_frames; f++)
	{
		gfloat *frame = frames + (gsize) f * channels;
		gdouble peak = 0.0;
		gdouble goal;

		for (c = 0; c < channels; c++)
			peak = MAX (peak, fabs ((gdouble) frame[c]));
		if (peak >= thr)
			self->hold_left = hold + 1;
		else if (self->hold_left > 0)
			self->hold_left--;
		goal = self->hold_left > 0 ? 1.0 : floor_gain;
		self->gain += (goal > self->gain ? att : rel) * (goal - self->gain);
		for (c = 0; c < channels; c++)
			frame[c] = (gfloat) ((gdouble) frame[c] * self->gain);
	}
}

static void
lrg_gate_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_gate_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_gate_finalize (GObject *object)
{
	G_OBJECT_CLASS (lrg_gate_parent_class)->finalize (object);
}

static void
lrg_gate_class_init (LrgGateClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_gate_process;
	effect_class->reset = lrg_gate_reset;
	object_class->get_property = lrg_gate_get_property;
	object_class->set_property = lrg_gate_set_property;
	object_class->finalize = lrg_gate_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_gate_init (LrgGate *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgGate *
lrg_gate_new (gdouble threshold_db)
{
	LrgGate *self = g_object_new (LRG_TYPE_GATE, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "threshold", threshold_db);
	return self;
}
