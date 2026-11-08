/* lrg-foley-instrument.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sound effects as notes (see lrg-foley-instrument.h).  The takes are
 * rendered once, when the patch or the take count changes, into an inner
 * one-shot #LrgSampler (one zone per take over every key, so the sampler's
 * round robin alternates them); every note event and render is forwarded
 * to that sampler's class functions.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>

#include "lrg-foley-instrument.h"
#include "lrg-sampler.h"

struct _LrgFoleyInstrument
{
	LrgInstrument  parent_instance;

	LrgFoleyPatch *patch;
	LrgSampler    *sampler;
	guint          takes;
	gint           root_key;
};

G_DEFINE_FINAL_TYPE (LrgFoleyInstrument, lrg_foley_instrument, LRG_TYPE_INSTRUMENT)

enum
{
	PROP_0,
	PROP_TAKES,
	PROP_ROOT_KEY,
	PROP_PITCH_TRACK,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

/* the inner sampler's class, where the forwarded calls go */
#define INNER(self) LRG_INSTRUMENT_GET_CLASS ((self)->sampler)

static gboolean
rebuild (LrgFoleyInstrument  *self,
         GError             **error)
{
	guint i;

	lrg_sampler_clear_zones (self->sampler);
	if (self->patch == NULL)
		return TRUE;
	for (i = 0; i < self->takes; i++)
	{
		g_autoptr(LrgFoleyPatch) take = lrg_foley_patch_copy (self->patch);
		g_autoptr(LrgWaveData)   wave = NULL;
		guint                    variation = 0;

		/* take 0 is the patch itself, the others its variations */
		g_object_get (take, "variation", &variation, NULL);
		g_object_set (take, "variation", variation + i, NULL);
		wave = lrg_foley_patch_render_wave (take, error);
		if (wave == NULL)
			return FALSE;
		lrg_sampler_add_zone (self->sampler, wave, self->root_key, 0, 127);
	}
	return TRUE;
}

static void
fi_note_on (LrgInstrument *instrument,
            gint           key,
            gdouble        velocity)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (instrument);

	INNER (self)->note_on (LRG_INSTRUMENT (self->sampler), key, velocity);
}

static void
fi_note_off (LrgInstrument *instrument,
             gint           key)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (instrument);

	INNER (self)->note_off (LRG_INSTRUMENT (self->sampler), key);
}

static void
fi_all_off (LrgInstrument *instrument,
            gboolean       hard)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (instrument);

	INNER (self)->all_off (LRG_INSTRUMENT (self->sampler), hard);
}

static void
fi_render_span (LrgInstrument *instrument,
                gfloat        *out,
                guint          frames,
                guint          channels,
                guint          sample_rate)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (instrument);
	gdouble             volume = 0.0;

	/* the outer volume rides on the inner one */
	g_object_get (instrument, "volume", &volume, NULL);
	g_object_set (self->sampler, "volume", volume, NULL);
	INNER (self)->render_span (LRG_INSTRUMENT (self->sampler), out, frames, channels, sample_rate);
}

static guint
fi_get_active_voices (LrgInstrument *instrument)
{
	return lrg_instrument_get_active_voices (LRG_INSTRUMENT (LRG_FOLEY_INSTRUMENT (instrument)->sampler));
}

static void
fi_reset_voices (LrgInstrument *instrument)
{
	lrg_instrument_reset (LRG_INSTRUMENT (LRG_FOLEY_INSTRUMENT (instrument)->sampler));
}

static void
lrg_foley_instrument_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (object);

	switch (prop_id)
	{
	case PROP_TAKES: g_value_set_uint (value, self->takes); break;
	case PROP_ROOT_KEY: g_value_set_int (value, self->root_key); break;
	case PROP_PITCH_TRACK:
		g_value_set_double (value, lrg_instrument_get_param (LRG_INSTRUMENT (self->sampler), "pitch-track"));
		break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_foley_instrument_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (object);

	switch (prop_id)
	{
	case PROP_TAKES:
		self->takes = g_value_get_uint (value);
		rebuild (self, NULL);
		break;
	case PROP_ROOT_KEY:
		self->root_key = g_value_get_int (value);
		rebuild (self, NULL);
		break;
	case PROP_PITCH_TRACK:
		lrg_instrument_set_param (LRG_INSTRUMENT (self->sampler), "pitch-track", g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_foley_instrument_finalize (GObject *object)
{
	LrgFoleyInstrument *self = LRG_FOLEY_INSTRUMENT (object);

	g_clear_object (&self->patch);
	g_clear_object (&self->sampler);
	G_OBJECT_CLASS (lrg_foley_instrument_parent_class)->finalize (object);
}

static void
lrg_foley_instrument_class_init (LrgFoleyInstrumentClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgInstrumentClass *instrument_class = LRG_INSTRUMENT_CLASS (klass);

	object_class->get_property = lrg_foley_instrument_get_property;
	object_class->set_property = lrg_foley_instrument_set_property;
	object_class->finalize = lrg_foley_instrument_finalize;
	instrument_class->note_on = fi_note_on;
	instrument_class->note_off = fi_note_off;
	instrument_class->all_off = fi_all_off;
	instrument_class->render_span = fi_render_span;
	instrument_class->get_active_voices = fi_get_active_voices;
	instrument_class->reset_voices = fi_reset_voices;

	/**
	 * LrgFoleyInstrument:takes:
	 *
	 * Takes of the patch (its variations 0 .. takes - 1) that alternate.
	 */
	props[PROP_TAKES] = g_param_spec_uint ("takes", "takes", "Takes that alternate", 1, 16, 4,
	                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFoleyInstrument:root-key:
	 *
	 * The key that plays the patch at its own pitch.
	 */
	props[PROP_ROOT_KEY] = g_param_spec_int ("root-key", "root-key", "Key of the unpitched sound", 0, 127, 60,
	                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgFoleyInstrument:pitch-track:
	 *
	 * Semitones per key (0: every key plays the patch unpitched).
	 */
	props[PROP_PITCH_TRACK] = g_param_spec_double ("pitch-track", "pitch-track", "Semitones per key", 0.0, 1.0,
	                                               1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
lrg_foley_instrument_init (LrgFoleyInstrument *self)
{
	self->sampler = lrg_sampler_new ();
	lrg_instrument_set_param (LRG_INSTRUMENT (self->sampler), "one-shot", 1.0);
	lrg_instrument_set_param (LRG_INSTRUMENT (self->sampler), "sustain", 1.0);
	lrg_instrument_set_param (LRG_INSTRUMENT (self->sampler), "release", 0.05);
	g_object_set (self->sampler, "polyphony", 16u, NULL);
	self->takes = 4;
	self->root_key = 60;
}

LrgFoleyInstrument *
lrg_foley_instrument_new (LrgFoleyPatch *patch)
{
	LrgFoleyInstrument *self = g_object_new (LRG_TYPE_FOLEY_INSTRUMENT, NULL);

	if (patch != NULL)
		lrg_foley_instrument_set_patch (self, patch, NULL);
	return self;
}

gboolean
lrg_foley_instrument_set_patch (LrgFoleyInstrument  *self,
                                LrgFoleyPatch       *patch,
                                GError             **error)
{
	g_return_val_if_fail (LRG_IS_FOLEY_INSTRUMENT (self), FALSE);
	g_return_val_if_fail (patch == NULL || LRG_IS_FOLEY_PATCH (patch), FALSE);

	g_clear_object (&self->patch);
	if (patch != NULL)
		self->patch = lrg_foley_patch_copy (patch);
	return rebuild (self, error);
}

LrgFoleyPatch *
lrg_foley_instrument_get_patch (LrgFoleyInstrument *self)
{
	g_return_val_if_fail (LRG_IS_FOLEY_INSTRUMENT (self), NULL);
	return self->patch;
}
