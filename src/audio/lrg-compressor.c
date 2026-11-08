/* lrg-compressor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Feed-forward compressor (see lrg-compressor.h and
 * docs/modules/audio/effects.org).
 *
 * Per frame the detector takes the largest absolute sample over all
 * channels (stereo link, so the image does not wander), converts it to
 * dB and runs the soft-knee gain computer of Giannoulis, Massberg and
 * Reiss ("Digital Dynamic Range Compressor Design", JAES 2012):
 *
 *   x < T - W/2:          y = x
 *   |x - T| <= W/2:       y = x + (1/R - 1) (x - T + W/2)^2 / (2 W)
 *   x > T + W/2:          y = T + (x - T) / R
 *
 * The reduction y - x is smoothed in dB by a one-pole filter with the
 * attack coefficient while the reduction deepens and the release one
 * while it recovers; the gain is then 10^((reduction + makeup) / 20).
 * The mix parameter blends the dry input back in (parallel compression).
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>

#include "lrg-compressor.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

struct _LrgCompressor
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble threshold;   /* dBFS */
	gdouble ratio;
	gdouble knee;        /* dB */
	gdouble attack;      /* seconds */
	gdouble release;     /* seconds */
	gdouble makeup;      /* dB */
	gdouble mix;         /* 0 dry .. 1 wet */

	/* state */
	gdouble env;         /* smoothed reduction, dB (<= 0) */
};

G_DEFINE_FINAL_TYPE (LrgCompressor, lrg_compressor, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgCompressor:threshold:
 *
 * Level in dBFS above which the gain is reduced.
 */
/**
 * LrgCompressor:ratio:
 *
 * Input dB over the threshold per output dB (1: no compression, 30: a
 * limiter in all but name).
 */
/**
 * LrgCompressor:knee:
 *
 * Width in dB of the soft knee around the threshold (0: hard knee).
 */
/**
 * LrgCompressor:attack:
 *
 * Seconds the reduction takes to deepen (one-pole time constant).
 */
/**
 * LrgCompressor:release:
 *
 * Seconds the reduction takes to recover.
 */
/**
 * LrgCompressor:makeup:
 *
 * Gain in dB added after compression.
 */
/**
 * LrgCompressor:mix:
 *
 * Wet share: 1 is fully compressed, lower values blend the dry signal
 * back (parallel or "New York" compression).
 */
static const LrgAudioParam params[] = {
	{ "threshold", "Level (dBFS) above which the gain is reduced",
	  G_STRUCT_OFFSET (LrgCompressor, threshold), -60.0, 0.0, -18.0 },
	{ "ratio", "Input dB over the threshold per output dB",
	  G_STRUCT_OFFSET (LrgCompressor, ratio), 1.0, 30.0, 4.0 },
	{ "knee", "Soft knee width in dB",
	  G_STRUCT_OFFSET (LrgCompressor, knee), 0.0, 24.0, 6.0 },
	{ "attack", "Attack time in seconds",
	  G_STRUCT_OFFSET (LrgCompressor, attack), 0.0001, 0.5, 0.005 },
	{ "release", "Release time in seconds",
	  G_STRUCT_OFFSET (LrgCompressor, release), 0.005, 5.0, 0.12 },
	{ "makeup", "Make-up gain in dB",
	  G_STRUCT_OFFSET (LrgCompressor, makeup), -24.0, 24.0, 0.0 },
	{ "mix", "Wet share (parallel compression below 1)",
	  G_STRUCT_OFFSET (LrgCompressor, mix), 0.0, 1.0, 1.0 },
};

#define N_PARAMS (G_N_ELEMENTS (params))

static GParamSpec *specs[N_PARAMS + 1];

/* the static gain curve: the reduction in dB for an input of @x dB */
static gdouble
gain_computer (LrgCompressor *self,
               gdouble        x)
{
	gdouble t = self->threshold;
	gdouble r = self->ratio;
	gdouble w = self->knee;
	gdouble over = x - t;
	gdouble y;

	if (2.0 * over < -w)
		y = x;
	else if (w > 0.0 && 2.0 * fabs (over) <= w)
	{
		gdouble d = over + w / 2.0;

		y = x + (1.0 / r - 1.0) * d * d / (2.0 * w);
	}
	else
		y = t + over / r;
	return y - x;
}

static void
lrg_compressor_reset (LrgAudioEffect *effect)
{
	LRG_COMPRESSOR (effect)->env = 0.0;
}

static void
lrg_compressor_process (LrgAudioEffect *effect,
                        gfloat         *frames,
                        guint           n_frames,
                        guint           channels,
                        guint           sample_rate)
{
	LrgCompressor *self = LRG_COMPRESSOR (effect);
	gdouble        att = lrg_dsp_one_pole (self->attack, sample_rate);
	gdouble        rel = lrg_dsp_one_pole (self->release, sample_rate);
	gdouble        makeup = self->makeup;
	gdouble        wet = self->mix;
	guint          f, c;

	for (f = 0; f < n_frames; f++)
	{
		gfloat *frame = frames + (gsize) f * channels;
		gdouble peak = 0.0;
		gdouble target;
		gdouble gain;

		/* stereo-linked peak detector */
		for (c = 0; c < channels; c++)
			peak = MAX (peak, fabs ((gdouble) frame[c]));
		target = gain_computer (self, lrg_dsp_lin_to_db (peak));

		/* ballistics in the dB domain */
		if (target < self->env)
			self->env += att * (target - self->env);
		else
			self->env += rel * (target - self->env);
		if (self->env > -1e-9)
			self->env = 0.0;

		gain = lrg_dsp_db_to_lin (self->env + makeup);
		for (c = 0; c < channels; c++)
		{
			gdouble dry = (gdouble) frame[c];

			frame[c] = (gfloat) (dry * (1.0 - wet) + dry * gain * wet);
		}
	}
}

static void
lrg_compressor_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_compressor_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_compressor_class_init (LrgCompressorClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_compressor_process;
	effect_class->reset = lrg_compressor_reset;
	object_class->get_property = lrg_compressor_get_property;
	object_class->set_property = lrg_compressor_set_property;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_compressor_init (LrgCompressor *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
}

LrgCompressor *
lrg_compressor_new (gdouble threshold_db,
                    gdouble ratio)
{
	LrgCompressor *self = g_object_new (LRG_TYPE_COMPRESSOR, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "threshold", threshold_db);
	lrg_audio_params_set_clamped (G_OBJECT (self), "ratio", ratio);
	return self;
}

gdouble
lrg_compressor_get_gain_reduction (LrgCompressor *self)
{
	g_return_val_if_fail (LRG_IS_COMPRESSOR (self), 0.0);
	return self->env;
}
