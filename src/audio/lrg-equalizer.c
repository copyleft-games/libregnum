/* lrg-equalizer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Six-band equalizer (see lrg-equalizer.h and
 * docs/modules/audio/effects.org).  The coefficients are designed once
 * per block from the current parameters, so automation and ramps take
 * effect at block granularity.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-equalizer.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

struct _LrgEqualizer
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble low_cut;   /* Hz, 0 off */
	gdouble low_freq;   /* Hz */
	gdouble low_gain;   /* dB */
	gdouble mid1_freq;   /* Hz */
	gdouble mid1_gain;   /* dB */
	gdouble mid1_q;
	gdouble mid2_freq;   /* Hz */
	gdouble mid2_gain;   /* dB */
	gdouble mid2_q;
	gdouble high_freq;   /* Hz */
	gdouble high_gain;   /* dB */
	gdouble high_cut;   /* Hz, 0 off */
	/* state: 6 bands x channels */
	LrgDspBiquadState z[6][LRG_AUDIO_EFFECT_MAX_CHANNELS];
};

G_DEFINE_FINAL_TYPE (LrgEqualizer, lrg_equalizer, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgEqualizer:low-cut:
 *
 * High-pass frequency in Hz (0: off).
 */
/**
 * LrgEqualizer:low-freq:
 *
 * Low shelf corner in Hz.
 */
/**
 * LrgEqualizer:low-gain:
 *
 * Low shelf gain in dB.
 */
/**
 * LrgEqualizer:mid1-freq:
 *
 * First peak centre in Hz.
 */
/**
 * LrgEqualizer:mid1-gain:
 *
 * First peak gain in dB.
 */
/**
 * LrgEqualizer:mid1-q:
 *
 * First peak quality.
 */
/**
 * LrgEqualizer:mid2-freq:
 *
 * Second peak centre in Hz.
 */
/**
 * LrgEqualizer:mid2-gain:
 *
 * Second peak gain in dB.
 */
/**
 * LrgEqualizer:mid2-q:
 *
 * Second peak quality.
 */
/**
 * LrgEqualizer:high-freq:
 *
 * High shelf corner in Hz.
 */
/**
 * LrgEqualizer:high-gain:
 *
 * High shelf gain in dB.
 */
/**
 * LrgEqualizer:high-cut:
 *
 * Low-pass frequency in Hz (0: off).
 */
static const LrgAudioParam params[] = {
	{ "low-cut", "High-pass frequency in Hz (0: off)",
	  G_STRUCT_OFFSET (LrgEqualizer, low_cut), 0.0, 1000.0, 0.0 },
	{ "low-freq", "Low shelf corner in Hz",
	  G_STRUCT_OFFSET (LrgEqualizer, low_freq), 20.0, 2000.0, 120.0 },
	{ "low-gain", "Low shelf gain in dB",
	  G_STRUCT_OFFSET (LrgEqualizer, low_gain), -24.0, 24.0, 0.0 },
	{ "mid1-freq", "First peak centre in Hz",
	  G_STRUCT_OFFSET (LrgEqualizer, mid1_freq), 40.0, 12000.0, 600.0 },
	{ "mid1-gain", "First peak gain in dB",
	  G_STRUCT_OFFSET (LrgEqualizer, mid1_gain), -24.0, 24.0, 0.0 },
	{ "mid1-q", "First peak quality",
	  G_STRUCT_OFFSET (LrgEqualizer, mid1_q), 0.1, 10.0, 1.0 },
	{ "mid2-freq", "Second peak centre in Hz",
	  G_STRUCT_OFFSET (LrgEqualizer, mid2_freq), 200.0, 18000.0, 3500.0 },
	{ "mid2-gain", "Second peak gain in dB",
	  G_STRUCT_OFFSET (LrgEqualizer, mid2_gain), -24.0, 24.0, 0.0 },
	{ "mid2-q", "Second peak quality",
	  G_STRUCT_OFFSET (LrgEqualizer, mid2_q), 0.1, 10.0, 1.0 },
	{ "high-freq", "High shelf corner in Hz",
	  G_STRUCT_OFFSET (LrgEqualizer, high_freq), 1000.0, 20000.0, 9000.0 },
	{ "high-gain", "High shelf gain in dB",
	  G_STRUCT_OFFSET (LrgEqualizer, high_gain), -24.0, 24.0, 0.0 },
	{ "high-cut", "Low-pass frequency in Hz (0: off)",
	  G_STRUCT_OFFSET (LrgEqualizer, high_cut), 0.0, 20000.0, 0.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))


static GParamSpec *specs[N_PARAMS + 1];

static void
lrg_equalizer_reset (LrgAudioEffect *effect)
{
	LrgEqualizer *self = LRG_EQUALIZER (effect);

	memset (self->z, 0, sizeof (self->z));
}

static void
lrg_equalizer_process (LrgAudioEffect *effect,
                       gfloat         *frames,
                       guint           n_frames,
                       guint           channels,
                       guint           sample_rate)
{
	LrgEqualizer *self = LRG_EQUALIZER (effect);
	LrgDspBiquadCoef coef[6];
	gboolean         on[6];
	guint            f, c, b;

	/* design the active bands */
	on[0] = self->low_cut > 0.0;
	on[1] = self->low_gain != 0.0;
	on[2] = self->mid1_gain != 0.0;
	on[3] = self->mid2_gain != 0.0;
	on[4] = self->high_gain != 0.0;
	on[5] = self->high_cut > 0.0;
	if (on[0])
		lrg_dsp_biquad_design (&coef[0], LRG_DSP_BIQUAD_HIGHPASS, self->low_cut, 0.7071, 0.0, sample_rate);
	if (on[1])
		lrg_dsp_biquad_design (&coef[1], LRG_DSP_BIQUAD_LOWSHELF, self->low_freq, 0.7071, self->low_gain,
		                       sample_rate);
	if (on[2])
		lrg_dsp_biquad_design (&coef[2], LRG_DSP_BIQUAD_PEAK, self->mid1_freq, self->mid1_q, self->mid1_gain,
		                       sample_rate);
	if (on[3])
		lrg_dsp_biquad_design (&coef[3], LRG_DSP_BIQUAD_PEAK, self->mid2_freq, self->mid2_q, self->mid2_gain,
		                       sample_rate);
	if (on[4])
		lrg_dsp_biquad_design (&coef[4], LRG_DSP_BIQUAD_HIGHSHELF, self->high_freq, 0.7071, self->high_gain,
		                       sample_rate);
	if (on[5])
		lrg_dsp_biquad_design (&coef[5], LRG_DSP_BIQUAD_LOWPASS, self->high_cut, 0.7071, 0.0, sample_rate);

	for (b = 0; b < 6; b++)
	{
		/* a band switched off forgets its history */
		if (!on[b])
		{
			memset (self->z[b], 0, sizeof (self->z[b]));
			continue;
		}
		for (f = 0; f < n_frames; f++)
			for (c = 0; c < channels; c++)
			{
				gfloat *s = frames + (gsize) f * channels + c;

				*s = (gfloat) lrg_dsp_biquad_tick (&coef[b], &self->z[b][c], (gdouble) *s);
			}
	}
}

static void
lrg_equalizer_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_equalizer_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_equalizer_finalize (GObject *object)
{
	G_OBJECT_CLASS (lrg_equalizer_parent_class)->finalize (object);
}

static void
lrg_equalizer_class_init (LrgEqualizerClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_equalizer_process;
	effect_class->reset = lrg_equalizer_reset;
	object_class->get_property = lrg_equalizer_get_property;
	object_class->set_property = lrg_equalizer_set_property;
	object_class->finalize = lrg_equalizer_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_equalizer_init (LrgEqualizer *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgEqualizer *
lrg_equalizer_new (void)
{
	LrgEqualizer *self = g_object_new (LRG_TYPE_EQUALIZER, NULL);

	return self;
}
