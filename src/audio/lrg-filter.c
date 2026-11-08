/* lrg-filter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cascaded cookbook biquad (see lrg-filter.h).  Coefficients are
 * designed once per block; stages share them.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-filter.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

G_DEFINE_ENUM_TYPE (LrgFilterMode, lrg_filter_mode,
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_LOWPASS, "lowpass"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_HIGHPASS, "highpass"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_BANDPASS, "bandpass"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_NOTCH, "notch"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_PEAK, "peak"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_LOWSHELF, "lowshelf"),
	G_DEFINE_ENUM_VALUE (LRG_FILTER_MODE_HIGHSHELF, "highshelf"))

/* the private biquad kind of each mode, in enum order */
static const LrgDspBiquadKind mode_kind[] = {
	LRG_DSP_BIQUAD_LOWPASS, LRG_DSP_BIQUAD_HIGHPASS, LRG_DSP_BIQUAD_BANDPASS, LRG_DSP_BIQUAD_NOTCH,
	LRG_DSP_BIQUAD_PEAK, LRG_DSP_BIQUAD_LOWSHELF, LRG_DSP_BIQUAD_HIGHSHELF
};

struct _LrgFilter
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble frequency;   /* Hz */
	gdouble q;
	gdouble gain;   /* dB */
	gdouble stages;
	gdouble mix;
	LrgFilterMode mode;

	/* state: up to 4 stages x channels */
	LrgDspBiquadState z[4][LRG_AUDIO_EFFECT_MAX_CHANNELS];
};

G_DEFINE_FINAL_TYPE (LrgFilter, lrg_filter, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgFilter:frequency:
 *
 * Corner or centre frequency in Hz.
 */
/**
 * LrgFilter:q:
 *
 * Quality (resonance).
 */
/**
 * LrgFilter:gain:
 *
 * Gain of the peak and shelf modes in dB.
 */
/**
 * LrgFilter:stages:
 *
 * Cascaded stages, 1 .. 4 (12 dB per octave each).
 */
/**
 * LrgFilter:mix:
 *
 * Wet share.
 */
static const LrgAudioParam params[] = {
	{ "frequency", "Corner or centre frequency in Hz",
	  G_STRUCT_OFFSET (LrgFilter, frequency), 20.0, 20000.0, 1000.0 },
	{ "q", "Quality (resonance)",
	  G_STRUCT_OFFSET (LrgFilter, q), 0.1, 20.0, 0.7071 },
	{ "gain", "Gain of the peak and shelf modes in dB",
	  G_STRUCT_OFFSET (LrgFilter, gain), -24.0, 24.0, 0.0 },
	{ "stages", "Cascaded stages, 1 .. 4 (12 dB per octave each)",
	  G_STRUCT_OFFSET (LrgFilter, stages), 1.0, 4.0, 1.0 },
	{ "mix", "Wet share",
	  G_STRUCT_OFFSET (LrgFilter, mix), 0.0, 1.0, 1.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))

#define PROP_MODE (N_PARAMS + 1)

static GParamSpec *specs[N_PARAMS + 1];

static void
lrg_filter_reset (LrgAudioEffect *effect)
{
	LrgFilter *self = LRG_FILTER (effect);

	memset (self->z, 0, sizeof (self->z));
}

static void
lrg_filter_process (LrgAudioEffect *effect,
                    gfloat         *frames,
                    guint           n_frames,
                    guint           channels,
                    guint           sample_rate)
{
	LrgFilter *self = LRG_FILTER (effect);
	LrgDspBiquadCoef coef;
	guint            stages = (guint) CLAMP (round (self->stages), 1.0, 4.0);
	gdouble          wet = self->mix;
	guint            f, c, s;

	lrg_dsp_biquad_design (&coef, mode_kind[self->mode], self->frequency, self->q, self->gain, sample_rate);
	for (f = 0; f < n_frames; f++)
		for (c = 0; c < channels; c++)
		{
			gfloat *p = frames + (gsize) f * channels + c;
			gdouble dry = (gdouble) *p;
			gdouble y = dry;

			for (s = 0; s < stages; s++)
				y = lrg_dsp_biquad_tick (&coef, &self->z[s][c], y);
			*p = (gfloat) (dry * (1.0 - wet) + y * wet);
		}
}

static void
lrg_filter_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	if (prop_id == PROP_MODE)
	{
		g_value_set_enum (value, (gint) LRG_FILTER (object)->mode);
		return;
	}
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_filter_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	if (prop_id == PROP_MODE)
	{
		LrgFilterMode mode = (LrgFilterMode) g_value_get_enum (value);

		if (mode != LRG_FILTER (object)->mode)
		{
			LRG_FILTER (object)->mode = mode;
			memset (LRG_FILTER (object)->z, 0, sizeof (LRG_FILTER (object)->z));
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_filter_finalize (GObject *object)
{
	G_OBJECT_CLASS (lrg_filter_parent_class)->finalize (object);
}

static void
lrg_filter_class_init (LrgFilterClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_filter_process;
	effect_class->reset = lrg_filter_reset;
	object_class->get_property = lrg_filter_get_property;
	object_class->set_property = lrg_filter_set_property;
	object_class->finalize = lrg_filter_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);

	/**
	 * LrgFilter:mode:
	 *
	 * The response (#LrgFilterMode).
	 */
	g_object_class_install_property (object_class, PROP_MODE,
	                                 g_param_spec_enum ("mode", "mode", "Filter response",
	                                                    LRG_TYPE_FILTER_MODE, LRG_FILTER_MODE_LOWPASS,
	                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
	                                                    | G_PARAM_EXPLICIT_NOTIFY));
}

static void
lrg_filter_init (LrgFilter *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgFilter *
lrg_filter_new (LrgFilterMode mode,
                gdouble frequency,
                gdouble q)
{
	LrgFilter *self = g_object_new (LRG_TYPE_FILTER, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "frequency", frequency);
	lrg_audio_params_set_clamped (G_OBJECT (self), "q", q);
	self->mode = mode;
	return self;
}
