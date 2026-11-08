/* lrg-distortion.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Waveshaper (see lrg-distortion.h and docs/modules/audio/effects.org).
 *
 * Per sample: x * drive, the mode's curve, a biquad low-pass at the
 * tone frequency, the output gain, then the dry/wet blend.  The bit
 * crusher holds each sample for `downsample` frames and rounds it to
 * 2^(bits-1) levels per polarity.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-distortion.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

G_DEFINE_ENUM_TYPE (LrgDistortionMode, lrg_distortion_mode,
	G_DEFINE_ENUM_VALUE (LRG_DISTORTION_MODE_TANH, "tanh"),
	G_DEFINE_ENUM_VALUE (LRG_DISTORTION_MODE_SOFT_CLIP, "soft-clip"),
	G_DEFINE_ENUM_VALUE (LRG_DISTORTION_MODE_HARD_CLIP, "hard-clip"),
	G_DEFINE_ENUM_VALUE (LRG_DISTORTION_MODE_FOLD, "fold"),
	G_DEFINE_ENUM_VALUE (LRG_DISTORTION_MODE_BITCRUSH, "bitcrush"))

struct _LrgDistortion
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble drive;   /* dB */
	gdouble tone;   /* Hz */
	gdouble output;   /* dB */
	gdouble mix;
	gdouble bits;
	gdouble downsample;
	LrgDistortionMode mode;

	/* state */
	LrgDspBiquadState tone_state[LRG_AUDIO_EFFECT_MAX_CHANNELS];
	gdouble           held[LRG_AUDIO_EFFECT_MAX_CHANNELS];
	guint             hold_count;
};

G_DEFINE_FINAL_TYPE (LrgDistortion, lrg_distortion, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgDistortion:drive:
 *
 * Input gain into the shaper in dB.
 */
/**
 * LrgDistortion:tone:
 *
 * Low-pass after the shaper in Hz.
 */
/**
 * LrgDistortion:output:
 *
 * Output gain in dB.
 */
/**
 * LrgDistortion:mix:
 *
 * Wet share.
 */
/**
 * LrgDistortion:bits:
 *
 * Bit depth of the bit crusher.
 */
/**
 * LrgDistortion:downsample:
 *
 * Sample-and-hold factor of the bit crusher.
 */
static const LrgAudioParam params[] = {
	{ "drive", "Input gain into the shaper in dB",
	  G_STRUCT_OFFSET (LrgDistortion, drive), 0.0, 48.0, 12.0 },
	{ "tone", "Low-pass after the shaper in Hz",
	  G_STRUCT_OFFSET (LrgDistortion, tone), 500.0, 20000.0, 12000.0 },
	{ "output", "Output gain in dB",
	  G_STRUCT_OFFSET (LrgDistortion, output), -24.0, 12.0, -6.0 },
	{ "mix", "Wet share",
	  G_STRUCT_OFFSET (LrgDistortion, mix), 0.0, 1.0, 1.0 },
	{ "bits", "Bit depth of the bit crusher",
	  G_STRUCT_OFFSET (LrgDistortion, bits), 1.0, 16.0, 8.0 },
	{ "downsample", "Sample-and-hold factor of the bit crusher",
	  G_STRUCT_OFFSET (LrgDistortion, downsample), 1.0, 32.0, 1.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))

#define PROP_MODE (N_PARAMS + 1)

static GParamSpec *specs[N_PARAMS + 1];

static gdouble
shape (LrgDistortionMode mode,
       gdouble           x)
{
	switch (mode)
	{
	case LRG_DISTORTION_MODE_SOFT_CLIP:
		if (x >= 1.0)
			return 2.0 / 3.0 * 1.5;
		if (x <= -1.0)
			return -2.0 / 3.0 * 1.5;
		return (x - x * x * x / 3.0) * 1.5;
	case LRG_DISTORTION_MODE_HARD_CLIP:
		return CLAMP (x, -1.0, 1.0);
	case LRG_DISTORTION_MODE_FOLD:
		/* a triangle fold: identity inside [-1, 1], mirrored outside */
		return (2.0 / G_PI) * asin (sin (x * G_PI / 2.0));
	case LRG_DISTORTION_MODE_BITCRUSH:
	case LRG_DISTORTION_MODE_TANH:
	default:
		return tanh (x);
	}
}

static void
lrg_distortion_reset (LrgAudioEffect *effect)
{
	LrgDistortion *self = LRG_DISTORTION (effect);

	memset (self->tone_state, 0, sizeof (self->tone_state));
	memset (self->held, 0, sizeof (self->held));
	self->hold_count = 0;
}

static void
lrg_distortion_process (LrgAudioEffect *effect,
                        gfloat         *frames,
                        guint           n_frames,
                        guint           channels,
                        guint           sample_rate)
{
	LrgDistortion *self = LRG_DISTORTION (effect);
	LrgDspBiquadCoef tone;
	gdouble          drive = lrg_dsp_db_to_lin (self->drive);
	gdouble          out_gain = lrg_dsp_db_to_lin (self->output);
	gdouble          wet = self->mix;
	gdouble          levels = pow (2.0, round (self->bits) - 1.0);
	guint            hold = (guint) MAX (1.0, round (self->downsample));
	guint            f, c;

	lrg_dsp_biquad_design (&tone, LRG_DSP_BIQUAD_LOWPASS, self->tone, 0.7071, 0.0, sample_rate);
	for (f = 0; f < n_frames; f++)
	{
		gfloat  *frame = frames + (gsize) f * channels;
		gboolean take = TRUE;

		if (self->mode == LRG_DISTORTION_MODE_BITCRUSH)
		{
			take = self->hold_count == 0;
			self->hold_count = (self->hold_count + 1) % hold;
		}
		for (c = 0; c < channels; c++)
		{
			gdouble dry = (gdouble) frame[c];
			gdouble y;

			if (self->mode == LRG_DISTORTION_MODE_BITCRUSH)
			{
				if (take)
					self->held[c] = round (CLAMP (dry * drive, -1.0, 1.0) * levels) / levels;
				y = self->held[c];
			}
			else
				y = shape (self->mode, dry * drive);
			if (self->tone < 19999.0)
				y = lrg_dsp_biquad_tick (&tone, &self->tone_state[c], y);
			y *= out_gain;
			frame[c] = (gfloat) (dry * (1.0 - wet) + y * wet);
		}
	}
}

static void
lrg_distortion_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	if (prop_id == PROP_MODE)
	{
		g_value_set_enum (value, (gint) LRG_DISTORTION (object)->mode);
		return;
	}
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_distortion_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	if (prop_id == PROP_MODE)
	{
		LrgDistortionMode mode = (LrgDistortionMode) g_value_get_enum (value);

		if (mode != LRG_DISTORTION (object)->mode)
		{
			LRG_DISTORTION (object)->mode = mode;
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_distortion_finalize (GObject *object)
{
	G_OBJECT_CLASS (lrg_distortion_parent_class)->finalize (object);
}

static void
lrg_distortion_class_init (LrgDistortionClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_distortion_process;
	effect_class->reset = lrg_distortion_reset;
	object_class->get_property = lrg_distortion_get_property;
	object_class->set_property = lrg_distortion_set_property;
	object_class->finalize = lrg_distortion_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);

	/**
	 * LrgDistortion:mode:
	 *
	 * The shaping curve (#LrgDistortionMode).
	 */
	g_object_class_install_property (object_class, PROP_MODE,
	                                 g_param_spec_enum ("mode", "mode", "Shaping curve",
	                                                    LRG_TYPE_DISTORTION_MODE, LRG_DISTORTION_MODE_TANH,
	                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
	                                                    | G_PARAM_EXPLICIT_NOTIFY));
}

static void
lrg_distortion_init (LrgDistortion *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgDistortion *
lrg_distortion_new (LrgDistortionMode mode,
                    gdouble drive_db)
{
	LrgDistortion *self = g_object_new (LRG_TYPE_DISTORTION, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "drive", drive_db);
	self->mode = mode;
	return self;
}
