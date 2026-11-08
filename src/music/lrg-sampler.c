/* lrg-sampler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The multi-zone sampler (see lrg-sampler.h and
 * docs/modules/music/instruments.org).
 *
 * Zones keep their samples as float at the recording's own rate and
 * channel count.  A voice reads its zone at a fractional position that
 * advances by 2^((key - root) / 12 x pitch-track + tune/1200) x
 * (zone rate / output rate) per output frame, interpolating with a
 * 4-point cubic Hermite (Catmull-Rom).  A mono zone feeds both outputs.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-sampler.h"
#include "../audio/lrg-audio-dsp-private.h"
#include "../audio/lrg-audio-param-private.h"

#define MAX_VOICES (LRG_INSTRUMENT_MAX_POLYPHONY)

typedef struct
{
	gfloat  *pcm;          /* interleaved float */
	guint    channels;     /* 1 or 2 */
	guint    rate;
	gsize    frames;
	gint     root;
	gint     low_key, high_key;
	gdouble  low_vel, high_vel;
	gint64   loop_start, loop_end;   /* -1: no loop */
	gdouble  gain;         /* linear */
	gdouble  tune;         /* cents */
} Zone;

typedef enum
{
	STAGE_IDLE,
	STAGE_ATTACK,
	STAGE_DECAY,
	STAGE_SUSTAIN,
	STAGE_RELEASE
} Stage;

typedef struct
{
	gboolean active;
	gboolean gate;
	gint     key;
	guint    zone;
	gdouble  pos;
	gdouble  vel_gain;
	guint64  age;
	Stage    stage;
	gdouble  level;
} Voice;

struct _LrgSampler
{
	LrgInstrument parent_instance;

	/* parameters (properties, see params[]) */
	gdouble attack, decay, sustain, release;
	gdouble one_shot, velocity, pitch_track;

	/* state */
	GArray  *zones;        /* Zone */
	Voice    voices[MAX_VOICES];
	guint64  next_age;
	guint    round_robin;
};

G_DEFINE_FINAL_TYPE (LrgSampler, lrg_sampler, LRG_TYPE_INSTRUMENT)

#define F(field) G_STRUCT_OFFSET (LrgSampler, field)

static const LrgAudioParam params[] = {
	{ "attack", "Attack in seconds", F (attack), 0, 10, 0.001 },
	{ "decay", "Decay in seconds", F (decay), 0, 10, 0.1 },
	{ "sustain", "Sustain level", F (sustain), 0, 1, 1 },
	{ "release", "Release in seconds", F (release), 0, 10, 0.15 },
	{ "one-shot", "1 plays each sample to its end", F (one_shot), 0, 1, 0 },
	{ "velocity", "How much velocity changes the level", F (velocity), 0, 1, 0.7 },
	{ "pitch-track", "Semitones per key (0: every key at the root pitch)", F (pitch_track), 0, 1, 1 },
};

#undef F

#define N_PARAMS (G_N_ELEMENTS (params))

static GParamSpec *specs[N_PARAMS + 1];

static void
zone_clear (gpointer data)
{
	g_free (((Zone *) data)->pcm);
}

/* ==========================================================================
 * Notes
 * ========================================================================== */

/* the zone for @key at @velocity: round robin over every match */
static gint
find_zone (LrgSampler *self,
           gint        key,
           gdouble     velocity)
{
	guint matches[64];
	guint n = 0;
	guint i;

	for (i = 0; i < self->zones->len && n < G_N_ELEMENTS (matches); i++)
	{
		const Zone *z = &g_array_index (self->zones, Zone, i);

		if (key >= z->low_key && key <= z->high_key && velocity >= z->low_vel && velocity <= z->high_vel)
			matches[n++] = i;
	}
	if (n == 0)
		return -1;
	return (gint) matches[(self->round_robin++) % n];
}

static Voice *
pick_voice (LrgSampler *self)
{
	guint  poly = lrg_instrument_get_polyphony (LRG_INSTRUMENT (self));
	guint  active = 0;
	Voice *best = NULL;
	guint  i;

	for (i = 0; i < MAX_VOICES; i++)
		if (self->voices[i].active)
			active++;
	if (active < poly)
		for (i = 0; i < MAX_VOICES; i++)
			if (!self->voices[i].active)
				return &self->voices[i];
	for (i = 0; i < MAX_VOICES; i++)
		if (self->voices[i].active && (best == NULL || self->voices[i].age < best->age))
			best = &self->voices[i];
	return best != NULL ? best : &self->voices[0];
}

static void
sampler_note_on (LrgInstrument *instrument,
                 gint           key,
                 gdouble        velocity)
{
	LrgSampler *self = LRG_SAMPLER (instrument);
	gint        zone = find_zone (self, key, velocity);
	Voice      *v;

	if (zone < 0)
		return;
	v = pick_voice (self);
	memset (v, 0, sizeof *v);
	v->active = TRUE;
	v->gate = TRUE;
	v->key = key;
	v->zone = (guint) zone;
	v->vel_gain = 1.0 - self->velocity * (1.0 - velocity);
	v->age = ++self->next_age;
	v->stage = STAGE_ATTACK;
}

static void
sampler_note_off (LrgInstrument *instrument,
                  gint           key)
{
	LrgSampler *self = LRG_SAMPLER (instrument);
	guint       i;

	if (self->one_shot >= 0.5)
		return;
	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice *v = &self->voices[i];

		if (v->active && v->gate && v->key == key)
		{
			v->gate = FALSE;
			v->stage = STAGE_RELEASE;
		}
	}
}

static void
sampler_all_off (LrgInstrument *instrument,
                 gboolean       hard)
{
	LrgSampler *self = LRG_SAMPLER (instrument);
	guint       i;

	for (i = 0; i < MAX_VOICES; i++)
	{
		if (hard)
			self->voices[i].active = FALSE;
		else if (self->voices[i].active)
		{
			self->voices[i].gate = FALSE;
			self->voices[i].stage = STAGE_RELEASE;
		}
	}
}

static guint
sampler_get_active_voices (LrgInstrument *instrument)
{
	LrgSampler *self = LRG_SAMPLER (instrument);
	guint       i, n = 0;

	for (i = 0; i < MAX_VOICES; i++)
		if (self->voices[i].active)
			n++;
	return n;
}

static void
sampler_reset_voices (LrgInstrument *instrument)
{
	LrgSampler *self = LRG_SAMPLER (instrument);

	memset (self->voices, 0, sizeof self->voices);
	self->round_robin = 0;
	self->next_age = 0;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/* the sample of channel @c at integer frame @i (0 outside the zone) */
static inline gdouble
zone_at (const Zone *z,
         gint64      i,
         guint       c)
{
	if (i < 0 || (gsize) i >= z->frames)
		return 0.0;
	return (gdouble) z->pcm[(gsize) i * z->channels + MIN (c, z->channels - 1)];
}

/* Catmull-Rom between frames floor(pos) and floor(pos) + 1 */
static inline gdouble
hermite (const Zone *z,
         gdouble     pos,
         guint       c)
{
	gint64  i = (gint64) floor (pos);
	gdouble t = pos - (gdouble) i;
	gdouble y0 = zone_at (z, i - 1, c);
	gdouble y1 = zone_at (z, i, c);
	gdouble y2 = zone_at (z, i + 1, c);
	gdouble y3 = zone_at (z, i + 2, c);
	gdouble a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
	gdouble b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
	gdouble cc = -0.5 * y0 + 0.5 * y2;

	return ((a * t + b) * t + cc) * t + y1;
}

static void
sampler_render_span (LrgInstrument *instrument,
                     gfloat        *out,
                     guint          frames,
                     guint          channels,
                     guint          sample_rate)
{
	LrgSampler *self = LRG_SAMPLER (instrument);
	gdouble     volume = lrg_instrument_get_volume_gain (instrument);
	gdouble     bend = lrg_instrument_get_pitch_bend (instrument);
	gdouble     att = self->attack > 0.0 ? 1.0 / (self->attack * sample_rate) : 2.0;
	gdouble     dec = lrg_dsp_one_pole (self->decay / 3.0, sample_rate);
	gdouble     rel = lrg_dsp_one_pole (self->release / 3.0, sample_rate);
	guint       i, f;

	for (i = 0; i < MAX_VOICES; i++)
	{
		Voice      *v = &self->voices[i];
		const Zone *z;
		gdouble     inc;

		if (!v->active)
			continue;
		if (v->zone >= self->zones->len)
		{
			v->active = FALSE;
			continue;
		}
		z = &g_array_index (self->zones, Zone, v->zone);
		inc = pow (2.0, ((gdouble) (v->key - z->root) * self->pitch_track + bend) / 12.0 + z->tune / 1200.0)
		      * (gdouble) z->rate / (gdouble) sample_rate;
		for (f = 0; f < frames; f++)
		{
			gdouble l, r, g;

			/* the envelope */
			switch (v->stage)
			{
			case STAGE_ATTACK:
				v->level += att;
				if (v->level >= 1.0)
				{
					v->level = 1.0;
					v->stage = STAGE_DECAY;
				}
				break;
			case STAGE_DECAY:
				v->level += (self->sustain - v->level) * dec;
				break;
			case STAGE_RELEASE:
				v->level -= v->level * rel;
				if (v->level < 1e-5)
					v->stage = STAGE_IDLE;
				break;
			default:
				break;
			}
			if (v->stage == STAGE_IDLE || v->pos >= (gdouble) z->frames)
			{
				v->active = FALSE;
				break;
			}

			l = hermite (z, v->pos, 0);
			r = z->channels == 2 ? hermite (z, v->pos, 1) : l;
			g = v->level * v->vel_gain * z->gain * volume;
			if (channels == 2)
			{
				out[f * 2] += (gfloat) (l * g);
				out[f * 2 + 1] += (gfloat) (r * g);
			}
			else
				out[f] += (gfloat) ((l + r) * 0.5 * g);

			/* advance, looping while the key is held */
			v->pos += inc;
			if (v->gate && z->loop_start >= 0 && v->pos >= (gdouble) z->loop_end)
				v->pos -= (gdouble) (z->loop_end - z->loop_start);
		}
	}
}

/* ==========================================================================
 * Zones
 * ========================================================================== */

gint
lrg_sampler_add_zone (LrgSampler  *self,
                      LrgWaveData *wave,
                      gint         root_key,
                      gint         low_key,
                      gint         high_key)
{
	g_autoptr(LrgWaveData) converted = NULL;
	Zone                   z;
	gsize                  count = 0;
	guint                  channels;

	g_return_val_if_fail (LRG_IS_SAMPLER (self), -1);
	g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), -1);

	if (!lrg_wave_data_is_valid (wave) || lrg_wave_data_get_frame_count (wave) == 0 || root_key < 0
	    || root_key > 127 || low_key < 0 || high_key > 127 || low_key > high_key)
		return -1;
	channels = MIN (lrg_wave_data_get_channels (wave), 2u);
	converted = lrg_wave_data_convert (wave, lrg_wave_data_get_sample_rate (wave), 32, channels);
	if (converted == NULL)
		return -1;
	memset (&z, 0, sizeof z);
	z.pcm = lrg_wave_data_get_samples (converted, &count);
	if (z.pcm == NULL || count == 0)
	{
		g_free (z.pcm);
		return -1;
	}
	z.channels = channels;
	z.rate = lrg_wave_data_get_sample_rate (wave);
	z.frames = count / channels;
	z.root = root_key;
	z.low_key = low_key;
	z.high_key = high_key;
	z.low_vel = 0.0;
	z.high_vel = 1.0;
	z.loop_start = -1;
	z.loop_end = -1;
	z.gain = 1.0;
	g_array_append_val (self->zones, z);
	return (gint) self->zones->len - 1;
}

gboolean
lrg_sampler_set_zone_velocity (LrgSampler *self,
                               guint       zone,
                               gdouble     low,
                               gdouble     high)
{
	Zone *z;

	g_return_val_if_fail (LRG_IS_SAMPLER (self), FALSE);
	if (zone >= self->zones->len || !isfinite (low) || !isfinite (high) || low > high)
		return FALSE;
	z = &g_array_index (self->zones, Zone, zone);
	z->low_vel = CLAMP (low, 0.0, 1.0);
	z->high_vel = CLAMP (high, 0.0, 1.0);
	return TRUE;
}

gboolean
lrg_sampler_set_zone_loop (LrgSampler *self,
                           guint       zone,
                           gint64      start,
                           gint64      end)
{
	Zone *z;

	g_return_val_if_fail (LRG_IS_SAMPLER (self), FALSE);
	if (zone >= self->zones->len)
		return FALSE;
	z = &g_array_index (self->zones, Zone, zone);
	if (start < 0)
	{
		z->loop_start = z->loop_end = -1;
		return TRUE;
	}
	if (end <= start + 1 || (gsize) end > z->frames)
		return FALSE;
	z->loop_start = start;
	z->loop_end = end;
	return TRUE;
}

gboolean
lrg_sampler_set_zone_gain (LrgSampler *self,
                           guint       zone,
                           gdouble     gain_db,
                           gdouble     tune_cents)
{
	Zone *z;

	g_return_val_if_fail (LRG_IS_SAMPLER (self), FALSE);
	if (zone >= self->zones->len || !isfinite (gain_db) || !isfinite (tune_cents))
		return FALSE;
	z = &g_array_index (self->zones, Zone, zone);
	z->gain = lrg_dsp_db_to_lin (CLAMP (gain_db, -144.0, 24.0));
	z->tune = CLAMP (tune_cents, -1200.0, 1200.0);
	return TRUE;
}

guint
lrg_sampler_get_n_zones (LrgSampler *self)
{
	g_return_val_if_fail (LRG_IS_SAMPLER (self), 0);
	return self->zones->len;
}

void
lrg_sampler_clear_zones (LrgSampler *self)
{
	g_return_if_fail (LRG_IS_SAMPLER (self));
	memset (self->voices, 0, sizeof self->voices);
	g_array_set_size (self->zones, 0);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_sampler_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	if (!lrg_audio_params_get (object, params, N_PARAMS, prop_id, value))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_sampler_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	if (!lrg_audio_params_set (object, params, N_PARAMS, prop_id, value, pspec))
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
lrg_sampler_finalize (GObject *object)
{
	g_array_unref (LRG_SAMPLER (object)->zones);
	G_OBJECT_CLASS (lrg_sampler_parent_class)->finalize (object);
}

static void
lrg_sampler_class_init (LrgSamplerClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgInstrumentClass *instrument_class = LRG_INSTRUMENT_CLASS (klass);

	object_class->get_property = lrg_sampler_get_property;
	object_class->set_property = lrg_sampler_set_property;
	object_class->finalize = lrg_sampler_finalize;
	instrument_class->note_on = sampler_note_on;
	instrument_class->note_off = sampler_note_off;
	instrument_class->all_off = sampler_all_off;
	instrument_class->render_span = sampler_render_span;
	instrument_class->get_active_voices = sampler_get_active_voices;
	instrument_class->reset_voices = sampler_reset_voices;
	lrg_audio_params_install (object_class, params, N_PARAMS, specs);
}

static void
lrg_sampler_init (LrgSampler *self)
{
	lrg_audio_params_defaults (self, params, N_PARAMS);
	self->zones = g_array_new (FALSE, FALSE, sizeof (Zone));
	g_array_set_clear_func (self->zones, zone_clear);
}

LrgSampler *
lrg_sampler_new (void)
{
	return g_object_new (LRG_TYPE_SAMPLER, NULL);
}

LrgSampler *
lrg_sampler_new_from_wave (LrgWaveData *wave,
                           gint         root_key)
{
	g_autoptr(LrgSampler) self = lrg_sampler_new ();

	if (lrg_sampler_add_zone (self, wave, root_key, 0, 127) < 0)
		return NULL;
	return g_steal_pointer (&self);
}
