/* lrg-limiter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Look-ahead limiter (see lrg-limiter.h and docs/modules/audio/effects.org).
 *
 * Per frame the gain a frame needs to stay under the ceiling
 * (ceiling / peak, or 1) enters a window of L + 1 frames, L being the
 * look-ahead.  The minimum over that window is held, then averaged by
 * a moving box of L frames, so the gain has fully reached the needed
 * value by the time the delayed loud frame comes out L frames later.
 * Recovery follows a one-pole release.  A final clamp at the ceiling
 * catches the rounding of the float arithmetic.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <string.h>
#include <math.h>

#include "lrg-limiter.h"
#include "lrg-audio-dsp-private.h"
#include "lrg-audio-param-private.h"

struct _LrgLimiter
{
	LrgAudioEffect parent_instance;

	/* parameters (properties, see params[]) */
	gdouble ceiling;   /* dBFS */
	gdouble release;   /* seconds */
	gdouble lookahead;   /* seconds */

	/* state, (re)allocated when the layout or look-ahead changes */
	guint    len;          /* look-ahead in frames (L) */
	guint    alloc_rate;
	guint    alloc_channels;
	gdouble  alloc_lookahead;
	gdouble *delay;        /* L x channels, interleaved */
	gdouble *need;         /* L + 1 required gains */
	gdouble *box;          /* L held minimums */
	gdouble  box_sum;
	guint    pos;          /* write position in delay and box */
	guint    need_pos;
	gdouble  gain;         /* current gain */
	gdouble  reduction;    /* last block's deepest reduction, dB */
};

G_DEFINE_FINAL_TYPE (LrgLimiter, lrg_limiter, LRG_TYPE_AUDIO_EFFECT)

/**
 * LrgLimiter:ceiling:
 *
 * Highest output level in dBFS.
 */
/**
 * LrgLimiter:release:
 *
 * Release time in seconds.
 */
/**
 * LrgLimiter:lookahead:
 *
 * Look-ahead (and added latency) in seconds.
 */
static const LrgAudioParam params[] = {
	{ "ceiling", "Highest output level in dBFS",
	  G_STRUCT_OFFSET (LrgLimiter, ceiling), -24.0, 0.0, -1.0 },
	{ "release", "Release time in seconds",
	  G_STRUCT_OFFSET (LrgLimiter, release), 0.001, 2.0, 0.08 },
	{ "lookahead", "Look-ahead (and added latency) in seconds",
	  G_STRUCT_OFFSET (LrgLimiter, lookahead), 0.0005, 0.01, 0.0015 },
};

#define N_PARAMS (G_N_ELEMENTS (params))


static GParamSpec *specs[N_PARAMS + 1];

/* (re)allocates the delay line and windows for @rate, @channels */
static void
ensure_buffers (LrgLimiter *self,
                guint       rate,
                guint       channels)
{
	guint i;

	if (self->delay != NULL && self->alloc_rate == rate && self->alloc_channels == channels
	    && self->alloc_lookahead == self->lookahead)
		return;
	g_free (self->delay);
	g_free (self->need);
	g_free (self->box);
	self->len = MAX (1u, (guint) llround (self->lookahead * (gdouble) rate));
	self->delay = g_new0 (gdouble, (gsize) self->len * channels);
	self->need = g_new (gdouble, self->len + 1);
	self->box = g_new (gdouble, self->len);
	for (i = 0; i <= self->len; i++)
		self->need[i] = 1.0;
	for (i = 0; i < self->len; i++)
		self->box[i] = 1.0;
	self->box_sum = (gdouble) self->len;
	self->pos = 0;
	self->need_pos = 0;
	self->gain = 1.0;
	self->alloc_rate = rate;
	self->alloc_channels = channels;
	self->alloc_lookahead = self->lookahead;
}

static void
lrg_limiter_reset (LrgAudioEffect *effect)
{
	LrgLimiter *self = LRG_LIMITER (effect);

	g_clear_pointer (&self->delay, g_free);
	g_clear_pointer (&self->need, g_free);
	g_clear_pointer (&self->box, g_free);
	self->gain = 1.0;
	self->reduction = 0.0;
}

static void
lrg_limiter_process (LrgAudioEffect *effect,
                     gfloat         *frames,
                     guint           n_frames,
                     guint           channels,
                     guint           sample_rate)
{
	LrgLimiter *self = LRG_LIMITER (effect);
	gdouble ceil_lin = lrg_dsp_db_to_lin (self->ceiling);
	gdouble rel = lrg_dsp_one_pole (self->release, sample_rate);
	gdouble deepest = 1.0;
	guint   f, c, i;

	ensure_buffers (self, sample_rate, channels);
	for (f = 0; f < n_frames; f++)
	{
		gfloat *frame = frames + (gsize) f * channels;
		gdouble peak = 0.0;
		gdouble held = 1.0;
		gdouble smooth;

		/* 1. the gain this frame needs, into the window; hold its minimum */
		for (c = 0; c < channels; c++)
			peak = MAX (peak, fabs ((gdouble) frame[c]));
		self->need[self->need_pos] = peak > ceil_lin ? ceil_lin / peak : 1.0;
		self->need_pos = (self->need_pos + 1) % (self->len + 1);
		for (i = 0; i <= self->len; i++)
			held = MIN (held, self->need[i]);

		/* 2. a moving average of the held minimum: a ramp over L frames */
		self->box_sum += held - self->box[self->pos];
		self->box[self->pos] = held;
		smooth = self->box_sum / (gdouble) self->len;
		if (smooth < self->gain)
			self->gain = smooth;
		else
			self->gain += rel * (smooth - self->gain);
		deepest = MIN (deepest, self->gain);

		/* 3. the delayed frame out, the current one in */
		for (c = 0; c < channels; c++)
		{
			gdouble *slot = self->delay + (gsize) self->pos * channels + c;
			gdouble  out = *slot * self->gain;

			*slot = (gdouble) frame[c];
			frame[c] = (gfloat) CLAMP (out, -ceil_lin, ceil_lin);
		}
		self->pos = (self->pos + 1) % self->len;
	}

	/* the running sum drifts by rounding; rebuild it once per block */
	self->box_sum = 0.0;
	for (i = 0; i < self->len; i++)
		self->box_sum += self->box[i];
	self->reduction = lrg_dsp_lin_to_db (deepest);
}

static void
lrg_limiter_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_limiter_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_limiter_finalize (GObject *object)
{
	LrgLimiter *self = LRG_LIMITER (object);

	g_free (self->delay);
	g_free (self->need);
	g_free (self->box);
	G_OBJECT_CLASS (lrg_limiter_parent_class)->finalize (object);
}

static void
lrg_limiter_class_init (LrgLimiterClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	LrgAudioEffectClass *effect_class = LRG_AUDIO_EFFECT_CLASS (klass);

	effect_class->process = lrg_limiter_process;
	effect_class->reset = lrg_limiter_reset;
	object_class->get_property = lrg_limiter_get_property;
	object_class->set_property = lrg_limiter_set_property;
	object_class->finalize = lrg_limiter_finalize;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_limiter_init (LrgLimiter *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
	self->gain = 1.0;
}

LrgLimiter *
lrg_limiter_new (gdouble ceiling_db)
{
	LrgLimiter *self = g_object_new (LRG_TYPE_LIMITER, NULL);

	lrg_audio_params_set_clamped (G_OBJECT (self), "ceiling", ceiling_db);
	return self;
}

gdouble
lrg_limiter_get_gain_reduction (LrgLimiter *self)
{
	g_return_val_if_fail (LRG_IS_LIMITER (self), 0.0);
	return self->reduction;
}

guint
lrg_limiter_get_latency (LrgLimiter *self,
                         guint       sample_rate)
{
	g_return_val_if_fail (LRG_IS_LIMITER (self), 0);
	return MAX (1u, (guint) llround (self->lookahead * (gdouble) sample_rate));
}
