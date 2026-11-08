/* lrg-stereo-widener.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mid/side widener (see lrg-stereo-widener.h).  M = (L + R) / 2,
 * S = (L - R) / 2; S is scaled by the width and, with a bass-mono
 * corner, high-passed; then L = M + S, R = M - S.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-stereo-widener.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

struct _LrgStereoWidener
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble width;
	gdouble bass_mono;   /* Hz, 0 off */
	LrgDspBiquadState side_hp;
};

G_DEFINE_FINAL_TYPE (LrgStereoWidener, lrg_stereo_widener, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgStereoWidener:width:
 *
 * Side gain: 0 mono, 1 unchanged, 2 twice as wide.
 */
/**
 * LrgStereoWidener:bass-mono:
 *
 * Below this frequency the image is mono (0: off).
 */
static const LrgAudioParam params[] = {
	{ "width", "Side gain: 0 mono, 1 unchanged, 2 twice as wide",
	  G_STRUCT_OFFSET (LrgStereoWidener, width), 0.0, 2.0, 1.0 },
	{ "bass-mono", "Below this frequency the image is mono (0: off)",
	  G_STRUCT_OFFSET (LrgStereoWidener, bass_mono), 0.0, 500.0, 0.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))


static GParamSpec *specs[N_PARAMS + 1];

static void
lrg_stereo_widener_reset (LrgAudioEffect *effect)
{
	LrgStereoWidener *self = LRG_STEREO_WIDENER (effect);

	memset (&self->side_hp, 0, sizeof (self->side_hp));
}

static void
lrg_stereo_widener_process (LrgAudioEffect *effect,
                            gfloat         *frames,
                            guint           n_frames,
                            guint           channels,
                            guint           sample_rate)
{
	LrgStereoWidener *self = LRG_STEREO_WIDENER (effect);
	LrgDspBiquadCoef hp;
	guint            f;

	if (channels != 2)
		return;
	if (self->bass_mono > 0.0)
		lrg_dsp_biquad_design (&hp, LRG_DSP_BIQUAD_HIGHPASS, self->bass_mono, 0.7071, 0.0, sample_rate);
	for (f = 0; f < n_frames; f++)
	{
		gfloat *frame = frames + (gsize) f * 2;
		gdouble mid = ((gdouble) frame[0] + (gdouble) frame[1]) * 0.5;
		gdouble side = ((gdouble) frame[0] - (gdouble) frame[1]) * 0.5 * self->width;

		if (self->bass_mono > 0.0)
			side = lrg_dsp_biquad_tick (&hp, &self->side_hp, side);
		frame[0] = (gfloat) (mid + side);
		frame[1] = (gfloat) (mid - side);
	}
}

static void
lrg_stereo_widener_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_stereo_widener_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_stereo_widener_finalize (GObject *object)
{
	G_OBJECT_CLASS (lrg_stereo_widener_parent_class)->finalize (object);
}

static void
lrg_stereo_widener_class_init (LrgStereoWidenerClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_stereo_widener_process;
	effect_class->reset = lrg_stereo_widener_reset;
	object_class->get_property = lrg_stereo_widener_get_property;
	object_class->set_property = lrg_stereo_widener_set_property;
	object_class->finalize = lrg_stereo_widener_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_stereo_widener_init (LrgStereoWidener *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgStereoWidener *
lrg_stereo_widener_new (gdouble width)
{
	LrgStereoWidener *self = g_object_new (LRG_TYPE_STEREO_WIDENER, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "width", width);
	return self;
}
