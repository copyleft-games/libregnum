/* lrg-audio-mixer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Configurable PCM audio graph (see lrg-audio-mixer.h and
 * docs/modules/audio/mixer.org).
 *
 * The graph is a tree of buses (every bus but master has one parent)
 * plus post-fader sends and side-chain ducking rules, which both add
 * edges to the render order.  A render is cut into blocks of
 * LRG_AUDIO_MIXER_BLOCK_FRAMES frames.  Per block:
 *
 *   1. effect-parameter ramps set their properties (block granularity);
 *   2. every voice is read (pitch with linear interpolation, its own
 *      low-pass, gain and balance, both ramped per frame) into its bus;
 *   3. buses are processed in dependency order (Kahn's algorithm over
 *      parent, send and duck edges): built-in low-pass, built-in delay,
 *      effect chain, then per frame the ducking gain, the fader (ramped
 *      per frame) and mute/solo; the result is metered and added to the
 *      parent and the send targets.
 *
 * Master's buffer is the output, clipped to [-1, 1].  Everything runs on
 * the caller's thread; no audio device is touched except by pump().
 */

#include "config.h"

#include "lrg-audio-mixer.h"
#include <gio/gio.h>
#include <string.h>
#include <math.h>

#define MAX_BUSES      (256)
#define MIN_PITCH      (0.125)
#define MAX_PITCH      (8.0)
#define MAX_DUCK_TIME  (10.0)

/* ==========================================================================
 * Data model
 * ========================================================================== */

/*
 * Ramp:
 * A per-frame linear move of a value from @from to @to over @total
 * frames; inactive (the value is @to) once @done reaches @total.
 */
typedef struct
{
	gdouble from;
	gdouble to;
	guint64 total;
	guint64 done;
} Ramp;

typedef struct
{
	guint   target;
	gdouble gain;
	Ramp    ramp;
} Send;

typedef struct
{
	gchar               *name;
	guint                parent;
	gdouble              gain;
	Ramp                 gain_ramp;
	gboolean             muted;
	gboolean             solo;
	GArray              *sends;      /* Send */

	/* built-in effects (lrg_audio_mixer_set_effects ()) */
	gdouble              alpha;
	gdouble              low[2];
	gdouble              feedback, wet;
	gdouble             *delay;
	guint                delay_frames, cursor;

	LrgAudioEffectChain *chain;      /* nullable, created on demand */

	/* meters of the current render (accumulators) and the last one */
	gdouble              acc_peak[2];
	gdouble              acc_sq[2];
	guint64              acc_frames;
	gdouble              peak[2];
	gdouble              rms[2];
	gdouble              rms_all;
	gdouble              duck_gain;  /* product of the duck gains, last frame */
} Bus;

typedef struct
{
	guint   trigger;
	guint   target;
	gdouble threshold;   /* linear */
	gdouble depth;       /* linear */
	gdouble attack;      /* seconds */
	gdouble release;     /* seconds */
	gdouble att_coef;
	gdouble rel_coef;
	gdouble gain;        /* state */
} Duck;

typedef struct
{
	guint           bus;
	LrgAudioEffect *effect;      /* owned ref */
	gchar          *property;
	gdouble         from;
	gdouble         to;
	guint64         total;
	guint64         done;
} ParamRamp;

typedef struct
{
	guint64  id;
	guint    bus;
	gfloat  *samples;
	gsize    count;      /* samples */
	gsize    frames;     /* count / channels */
	gdouble  pos;        /* read position in frames */
	gdouble  pitch;
	gdouble  gain;
	Ramp     gain_ramp;
	gdouble  pan;
	Ramp     pan_ramp;
	gdouble  lp_alpha;   /* 0: off */
	gdouble  lp[2];
	gboolean loop, paused;
} Voice;

struct _LrgAudioMixer
{
	GObject    parent_instance;
	guint      rate, channels;
	guint64    last_id;
	GPtrArray *buses;
	GPtrArray *voices;
	GArray    *ducks;        /* Duck */
	GPtrArray *param_ramps;  /* ParamRamp */

	/* Render scratch kept between refills: the per-bus mix (8 bytes a
	 * sample a bus), the float block an effect chain runs on, and
	 * pump()'s output. Fresh ones past glibc's 128 KiB mmap threshold
	 * cost an mmap, page faults and an munmap on every refill. */
	gdouble   *mix;
	gsize      mix_len;
	gfloat    *fx;
	gsize      fx_len;
	gfloat    *out;
	gsize      out_len;
};
G_DEFINE_TYPE (LrgAudioMixer, lrg_audio_mixer, G_TYPE_OBJECT)

static void
bus_free (gpointer data)
{
	Bus *bus = data;

	g_free (bus->name);
	g_array_unref (bus->sends);
	g_free (bus->delay);
	g_clear_object (&bus->chain);
	g_free (bus);
}

static void
voice_free (gpointer data)
{
	Voice *voice = data;

	g_free (voice->samples);
	g_free (voice);
}

static void
param_ramp_free (gpointer data)
{
	ParamRamp *ramp = data;

	g_clear_object (&ramp->effect);
	g_free (ramp->property);
	g_free (ramp);
}

static void
lrg_audio_mixer_finalize (GObject *object)
{
	LrgAudioMixer *self = LRG_AUDIO_MIXER (object);

	g_ptr_array_unref (self->buses);
	g_ptr_array_unref (self->voices);
	g_array_unref (self->ducks);
	g_ptr_array_unref (self->param_ramps);
	g_free (self->mix);
	g_free (self->fx);
	g_free (self->out);
	G_OBJECT_CLASS (lrg_audio_mixer_parent_class)->finalize (object);
}

static void
lrg_audio_mixer_class_init (LrgAudioMixerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_audio_mixer_finalize;
}

static Bus *
new_bus (const gchar *name,
         guint        parent)
{
	Bus *bus = g_new0 (Bus, 1);

	bus->name = g_strdup (name);
	bus->parent = parent;
	bus->gain = 1.0;
	bus->duck_gain = 1.0;
	bus->sends = g_array_new (FALSE, FALSE, sizeof (Send));
	return bus;
}

static void
lrg_audio_mixer_init (LrgAudioMixer *self)
{
	self->rate = 48000;
	self->channels = 2;
	self->buses = g_ptr_array_new_with_free_func (bus_free);
	self->voices = g_ptr_array_new_with_free_func (voice_free);
	self->ducks = g_array_new (FALSE, FALSE, sizeof (Duck));
	self->param_ramps = g_ptr_array_new_with_free_func (param_ramp_free);
	g_ptr_array_add (self->buses, new_bus ("master", G_MAXUINT));
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gint
find_bus (LrgAudioMixer *self,
          const gchar   *name)
{
	guint i;

	if (name == NULL)
		return -1;
	for (i = 0; i < self->buses->len; i++)
		if (g_strcmp0 (((Bus *) g_ptr_array_index (self->buses, i))->name, name) == 0)
			return (gint) i;
	return -1;
}

static Bus *
bus_at (LrgAudioMixer *self,
        guint          index)
{
	return (Bus *) g_ptr_array_index (self->buses, index);
}

static gboolean
valid_gain (gdouble gain)
{
	return isfinite (gain) && gain >= 0.0 && gain <= 4.0;
}

/* frames of @seconds at the mixer rate (0 for none) */
static guint64
seconds_to_frames (LrgAudioMixer *self,
                   gdouble        seconds)
{
	if (!isfinite (seconds) || seconds <= 0.0)
		return 0;
	return (guint64) llround (seconds * (gdouble) self->rate);
}

/* the value a ramp has reached */
static gdouble
ramp_value (const Ramp *ramp,
            gdouble     settled)
{
	if (ramp->done >= ramp->total)
		return settled;
	return ramp->from + (ramp->to - ramp->from) * ((gdouble) ramp->done / (gdouble) ramp->total);
}

/* starts a ramp from @current to @to over @frames (0: jump) */
static void
ramp_start (Ramp    *ramp,
            gdouble  current,
            gdouble  to,
            guint64  frames)
{
	ramp->from = current;
	ramp->to = to;
	ramp->total = frames;
	ramp->done = 0;
}

/* one frame of a ramped value: returns the value for this frame and
 * advances the ramp; @value is updated to the settled value at the end */
static gdouble
ramp_step (Ramp    *ramp,
           gdouble *value)
{
	gdouble v;

	if (ramp->done >= ramp->total)
		return *value;
	v = ramp->from + (ramp->to - ramp->from) * ((gdouble) ramp->done / (gdouble) ramp->total);
	ramp->done++;
	if (ramp->done >= ramp->total)
		*value = ramp->to;
	return v;
}

/* the one-pole coefficient of a time constant (0 seconds: 1, a jump) */
static gdouble
time_coef (gdouble seconds,
           guint   rate)
{
	if (seconds <= 0.0)
		return 1.0;
	return 1.0 - exp (-1.0 / (seconds * (gdouble) rate));
}

/*
 * reaches:
 * Whether audio (or render order) flows from bus @from to bus @target
 * through parent links, sends and duck rules (trigger -> target).
 */
static gboolean
reaches (LrgAudioMixer *self,
         guint          from,
         guint          target,
         gboolean      *seen)
{
	Bus  *bus;
	guint i;

	if (from == target)
		return TRUE;
	if (seen[from])
		return FALSE;
	seen[from] = TRUE;
	bus = bus_at (self, from);
	if (from != 0 && reaches (self, bus->parent, target, seen))
		return TRUE;
	for (i = 0; i < bus->sends->len; i++)
		if (reaches (self, g_array_index (bus->sends, Send, i).target, target, seen))
			return TRUE;
	for (i = 0; i < self->ducks->len; i++)
	{
		Duck *duck = &g_array_index (self->ducks, Duck, i);

		if (duck->trigger == from && reaches (self, duck->target, target, seen))
			return TRUE;
	}
	return FALSE;
}

static Voice *
find_voice (LrgAudioMixer *self,
            guint64        id)
{
	guint i;

	for (i = 0; i < self->voices->len; i++)
	{
		Voice *voice = g_ptr_array_index (self->voices, i);

		if (voice->id == id)
			return voice;
	}
	return NULL;
}

static Send *
find_send (Bus   *bus,
           guint  target)
{
	guint i;

	for (i = 0; i < bus->sends->len; i++)
	{
		Send *send = &g_array_index (bus->sends, Send, i);

		if (send->target == target)
			return send;
	}
	return NULL;
}

/* ==========================================================================
 * dB helpers
 * ========================================================================== */

gdouble
lrg_audio_db_to_gain (gdouble db)
{
	if (isnan (db) || db <= LRG_AUDIO_SILENCE_DB)
		return 0.0;
	return pow (10.0, db / 20.0);
}

gdouble
lrg_audio_gain_to_db (gdouble gain)
{
	gdouble db;

	if (!(gain > 0.0))
		return LRG_AUDIO_SILENCE_DB;
	db = 20.0 * log10 (gain);
	return MAX (db, LRG_AUDIO_SILENCE_DB);
}

/* ==========================================================================
 * Construction and graph
 * ========================================================================== */

LrgAudioMixer *
lrg_audio_mixer_new (guint sample_rate,
                     guint channels)
{
	LrgAudioMixer *self;

	if (sample_rate < 8000 || sample_rate > 192000 || channels < 1 || channels > 2)
		return NULL;
	self = g_object_new (LRG_TYPE_AUDIO_MIXER, NULL);
	self->rate = sample_rate;
	self->channels = channels;
	return self;
}

guint
lrg_audio_mixer_get_sample_rate (LrgAudioMixer *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	return self->rate;
}

guint
lrg_audio_mixer_get_channels (LrgAudioMixer *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	return self->channels;
}

gboolean
lrg_audio_mixer_add_bus (LrgAudioMixer *self,
                         const gchar   *name,
                         const gchar   *parent)
{
	gint p;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	p = find_bus (self, parent);
	if (name == NULL || *name == '\0' || p < 0 || find_bus (self, name) >= 0
	    || self->buses->len >= MAX_BUSES)
		return FALSE;
	g_ptr_array_add (self->buses, new_bus (name, (guint) p));
	return TRUE;
}

guint
lrg_audio_mixer_get_bus_count (LrgAudioMixer *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	return self->buses->len;
}

const gchar *
lrg_audio_mixer_get_bus_name (LrgAudioMixer *self,
                              guint          index)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	if (index >= self->buses->len)
		return NULL;
	return bus_at (self, index)->name;
}

gboolean
lrg_audio_mixer_has_bus (LrgAudioMixer *self,
                         const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	return find_bus (self, name) >= 0;
}

const gchar *
lrg_audio_mixer_get_bus_parent (LrgAudioMixer *self,
                                const gchar   *name)
{
	gint index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	index = find_bus (self, name);
	if (index <= 0)
		return NULL;
	return bus_at (self, bus_at (self, (guint) index)->parent)->name;
}

gboolean
lrg_audio_mixer_set_bus (LrgAudioMixer *self,
                         const gchar   *name,
                         gdouble        gain,
                         gboolean       muted)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0 || !valid_gain (gain))
		return FALSE;
	bus = bus_at (self, (guint) index);
	bus->gain = gain;
	bus->gain_ramp.total = bus->gain_ramp.done = 0;
	bus->muted = muted;
	return TRUE;
}

gdouble
lrg_audio_mixer_get_bus_gain (LrgAudioMixer *self,
                              const gchar   *name)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), -1.0);
	index = find_bus (self, name);
	if (index < 0)
		return -1.0;
	bus = bus_at (self, (guint) index);
	return ramp_value (&bus->gain_ramp, bus->gain);
}

gboolean
lrg_audio_mixer_get_bus_muted (LrgAudioMixer *self,
                               const gchar   *name)
{
	gint index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	return index >= 0 && bus_at (self, (guint) index)->muted;
}

gboolean
lrg_audio_mixer_set_bus_solo (LrgAudioMixer *self,
                              const gchar   *name,
                              gboolean       solo)
{
	gint index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0)
		return FALSE;
	bus_at (self, (guint) index)->solo = !!solo;
	return TRUE;
}

gboolean
lrg_audio_mixer_get_bus_solo (LrgAudioMixer *self,
                              const gchar   *name)
{
	gint index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	return index >= 0 && bus_at (self, (guint) index)->solo;
}

/* marks @index and every bus its output reaches (parents, send targets) */
static void
mark_downstream (LrgAudioMixer *self,
                 guint          index,
                 gboolean      *audible)
{
	Bus  *bus = bus_at (self, index);
	guint i;

	if (audible[index])
		return;
	audible[index] = TRUE;
	if (index != 0)
		mark_downstream (self, bus->parent, audible);
	for (i = 0; i < bus->sends->len; i++)
		mark_downstream (self, g_array_index (bus->sends, Send, i).target, audible);
}

/*
 * compute_audible:
 * Fills @audible (one per bus) with the solo state: everything when no
 * bus is soloed; otherwise soloed buses, their descendants (children
 * feed them) and everything downstream of them.
 */
static void
compute_audible (LrgAudioMixer *self,
                 gboolean      *audible)
{
	gboolean any = FALSE;
	guint    i;

	for (i = 0; i < self->buses->len; i++)
		any = any || bus_at (self, i)->solo;
	for (i = 0; i < self->buses->len; i++)
		audible[i] = !any;
	if (!any)
		return;
	for (i = 0; i < self->buses->len; i++)
	{
		if (!bus_at (self, i)->solo)
			continue;
		/* downstream of the solo */
		audible[i] = FALSE;
		mark_downstream (self, i, audible);
	}
	/* descendants: a bus whose parent chain meets a soloed bus */
	for (i = 1; i < self->buses->len; i++)
	{
		guint walk = i;
		guint depth;

		for (depth = 0; depth < MAX_BUSES && walk != 0; depth++)
		{
			walk = bus_at (self, walk)->parent;
			if (bus_at (self, walk)->solo)
			{
				audible[i] = TRUE;
				break;
			}
		}
	}
}

gboolean
lrg_audio_mixer_is_bus_audible (LrgAudioMixer *self,
                                const gchar   *name)
{
	gboolean audible[MAX_BUSES];
	gint     index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0)
		return FALSE;
	compute_audible (self, audible);
	return audible[index] && !bus_at (self, (guint) index)->muted;
}

/* validates and finds (or creates at gain 0) the send @src -> @dst */
static Send *
ensure_send (LrgAudioMixer *self,
             gint           src,
             gint           dst)
{
	gboolean seen[MAX_BUSES] = { FALSE };
	Bus     *bus;
	Send    *existing;
	Send     send;

	if (src <= 0 || dst < 0)
		return NULL;
	bus = bus_at (self, (guint) src);
	existing = find_send (bus, (guint) dst);
	if (existing != NULL)
		return existing;
	if (bus->parent == (guint) dst || reaches (self, (guint) dst, (guint) src, seen))
		return NULL;
	memset (&send, 0, sizeof send);
	send.target = (guint) dst;
	g_array_append_val (bus->sends, send);
	return &g_array_index (bus->sends, Send, bus->sends->len - 1);
}

gboolean
lrg_audio_mixer_set_send (LrgAudioMixer *self,
                          const gchar   *source,
                          const gchar   *target,
                          gdouble        gain)
{
	gint  src, dst;
	Bus  *bus;
	Send *send;
	guint i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	src = find_bus (self, source);
	dst = find_bus (self, target);
	if (src <= 0 || dst < 0 || !valid_gain (gain))
		return FALSE;
	bus = bus_at (self, (guint) src);
	if (gain == 0.0)
	{
		/* zero removes the send (and is a no-op when there is none, as
		 * long as the routing would have been valid) */
		for (i = 0; i < bus->sends->len; i++)
			if (g_array_index (bus->sends, Send, i).target == (guint) dst)
			{
				g_array_remove_index (bus->sends, i);
				return TRUE;
			}
		{
			gboolean seen[MAX_BUSES] = { FALSE };

			return bus->parent != (guint) dst && !reaches (self, (guint) dst, (guint) src, seen);
		}
	}
	send = ensure_send (self, src, dst);
	if (send == NULL)
		return FALSE;
	send->gain = gain;
	send->ramp.total = send->ramp.done = 0;
	return TRUE;
}

gdouble
lrg_audio_mixer_get_send (LrgAudioMixer *self,
                          const gchar   *source,
                          const gchar   *target)
{
	gint  src, dst;
	Send *send;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0.0);
	src = find_bus (self, source);
	dst = find_bus (self, target);
	if (src <= 0 || dst < 0)
		return 0.0;
	send = find_send (bus_at (self, (guint) src), (guint) dst);
	return send != NULL ? ramp_value (&send->ramp, send->gain) : 0.0;
}

gchar **
lrg_audio_mixer_list_sends (LrgAudioMixer *self,
                            const gchar   *source)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	gint                 src;
	guint                i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	src = find_bus (self, source);
	if (src > 0)
	{
		Bus *bus = bus_at (self, (guint) src);

		for (i = 0; i < bus->sends->len; i++)
		{
			Send *send = &g_array_index (bus->sends, Send, i);

			if (ramp_value (&send->ramp, send->gain) > 0.0 || send->ramp.done < send->ramp.total)
				g_ptr_array_add (names, g_strdup (bus_at (self, send->target)->name));
		}
	}
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

gboolean
lrg_audio_mixer_set_effects (LrgAudioMixer *self,
                             const gchar   *name,
                             gdouble        cutoff,
                             gdouble        delay_seconds,
                             gdouble        feedback,
                             gdouble        wet)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0 || !isfinite (cutoff) || cutoff < 0 || cutoff >= self->rate * 0.5
	    || !isfinite (delay_seconds) || delay_seconds < 0 || delay_seconds > 2
	    || !isfinite (feedback) || feedback < 0 || feedback >= 1 || !isfinite (wet) || wet < 0
	    || wet > 1)
		return FALSE;
	bus = bus_at (self, (guint) index);
	bus->alpha = cutoff == 0 ? 0 : 1.0 - exp (-2.0 * G_PI * cutoff / self->rate);
	bus->low[0] = bus->low[1] = 0;
	bus->feedback = feedback;
	bus->wet = wet;
	bus->delay_frames = delay_seconds == 0 ? 0 : MAX (1u, (guint) (delay_seconds * self->rate));
	bus->cursor = 0;
	g_free (bus->delay);
	bus->delay = g_new0 (gdouble, bus->delay_frames * self->channels);
	return TRUE;
}

LrgAudioEffectChain *
lrg_audio_mixer_get_bus_chain (LrgAudioMixer *self,
                               const gchar   *name)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	index = find_bus (self, name);
	if (index < 0)
		return NULL;
	bus = bus_at (self, (guint) index);
	if (bus->chain == NULL)
		bus->chain = lrg_audio_effect_chain_new ();
	return bus->chain;
}

/* ==========================================================================
 * Ramps
 * ========================================================================== */

gboolean
lrg_audio_mixer_ramp_bus_gain (LrgAudioMixer *self,
                               const gchar   *name,
                               gdouble        gain,
                               gdouble        seconds)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0 || !valid_gain (gain))
		return FALSE;
	bus = bus_at (self, (guint) index);
	ramp_start (&bus->gain_ramp, ramp_value (&bus->gain_ramp, bus->gain), gain,
	            seconds_to_frames (self, seconds));
	if (bus->gain_ramp.total == 0)
		bus->gain = gain;
	return TRUE;
}

gboolean
lrg_audio_mixer_ramp_send (LrgAudioMixer *self,
                           const gchar   *source,
                           const gchar   *target,
                           gdouble        gain,
                           gdouble        seconds)
{
	Send *send;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	if (!valid_gain (gain))
		return FALSE;
	send = ensure_send (self, find_bus (self, source), find_bus (self, target));
	if (send == NULL)
		return FALSE;
	ramp_start (&send->ramp, ramp_value (&send->ramp, send->gain), gain,
	            seconds_to_frames (self, seconds));
	if (send->ramp.total == 0)
		send->gain = gain;
	return TRUE;
}

/* the numeric value of @property of @effect as a double */
static gboolean
get_numeric_property (GObject     *object,
                      GParamSpec  *pspec,
                      gdouble     *out)
{
	g_auto(GValue) v = G_VALUE_INIT;
	g_auto(GValue) d = G_VALUE_INIT;

	g_value_init (&v, pspec->value_type);
	g_object_get_property (object, pspec->name, &v);
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_transform (&v, &d))
		return FALSE;
	*out = g_value_get_double (&d);
	return TRUE;
}

/* sets the numeric @property from a double (rounded for integers) */
static void
set_numeric_property (GObject    *object,
                      GParamSpec *pspec,
                      gdouble     value)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, pspec->value_type);
	switch (G_TYPE_FUNDAMENTAL (pspec->value_type))
	{
	case G_TYPE_FLOAT:
		g_value_set_float (&v, (gfloat) value);
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (&v, value);
		break;
	case G_TYPE_INT:
		g_value_set_int (&v, (gint) lround (value));
		break;
	case G_TYPE_UINT:
		g_value_set_uint (&v, (guint) MAX (0, lround (value)));
		break;
	default:
		return;
	}
	g_param_value_validate (pspec, &v);
	g_object_set_property (object, pspec->name, &v);
}

static gboolean
is_numeric_pspec (GParamSpec *pspec)
{
	GType fund;

	if (pspec == NULL || (pspec->flags & G_PARAM_WRITABLE) == 0 || (pspec->flags & G_PARAM_READABLE) == 0
	    || (pspec->flags & G_PARAM_CONSTRUCT_ONLY) != 0)
		return FALSE;
	fund = G_TYPE_FUNDAMENTAL (pspec->value_type);
	return fund == G_TYPE_FLOAT || fund == G_TYPE_DOUBLE || fund == G_TYPE_INT || fund == G_TYPE_UINT;
}

gboolean
lrg_audio_mixer_ramp_effect_param (LrgAudioMixer *self,
                                   const gchar   *bus,
                                   guint          index,
                                   const gchar   *property,
                                   gdouble        value,
                                   gdouble        seconds)
{
	gint            b;
	Bus            *busp;
	LrgAudioEffect *effect;
	GParamSpec     *pspec;
	gdouble         current = 0.0;
	ParamRamp      *ramp;
	guint           i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	g_return_val_if_fail (property != NULL, FALSE);
	b = find_bus (self, bus);
	if (b < 0 || !isfinite (value))
		return FALSE;
	busp = bus_at (self, (guint) b);
	if (busp->chain == NULL)
		return FALSE;
	effect = lrg_audio_effect_chain_get_effect (busp->chain, index);
	if (effect == NULL)
		return FALSE;
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (effect), property);
	if (!is_numeric_pspec (pspec) || !get_numeric_property (G_OBJECT (effect), pspec, &current))
		return FALSE;

	/* a running ramp of the same parameter is replaced */
	for (i = 0; i < self->param_ramps->len; i++)
	{
		ParamRamp *old = g_ptr_array_index (self->param_ramps, i);

		if (old->effect == effect && g_strcmp0 (old->property, pspec->name) == 0)
		{
			g_ptr_array_remove_index (self->param_ramps, i);
			break;
		}
	}
	if (seconds_to_frames (self, seconds) == 0)
	{
		set_numeric_property (G_OBJECT (effect), pspec, value);
		return TRUE;
	}
	ramp = g_new0 (ParamRamp, 1);
	ramp->bus = (guint) b;
	ramp->effect = g_object_ref (effect);
	ramp->property = g_strdup (pspec->name);
	ramp->from = current;
	ramp->to = value;
	ramp->total = seconds_to_frames (self, seconds);
	g_ptr_array_add (self->param_ramps, ramp);
	return TRUE;
}

gboolean
lrg_audio_mixer_is_ramping (LrgAudioMixer *self)
{
	guint i, j;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	if (self->param_ramps->len > 0)
		return TRUE;
	for (i = 0; i < self->buses->len; i++)
	{
		Bus *bus = bus_at (self, i);

		if (bus->gain_ramp.done < bus->gain_ramp.total)
			return TRUE;
		for (j = 0; j < bus->sends->len; j++)
		{
			Send *send = &g_array_index (bus->sends, Send, j);

			if (send->ramp.done < send->ramp.total)
				return TRUE;
		}
	}
	return FALSE;
}

/* sets every effect-parameter ramp's value for the block starting now,
 * then advances it by @frames; finished ramps are dropped */
static void
apply_param_ramps (LrgAudioMixer *self,
                   guint          frames)
{
	guint i = 0;

	while (i < self->param_ramps->len)
	{
		ParamRamp  *ramp = g_ptr_array_index (self->param_ramps, i);
		GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (ramp->effect),
		                                                  ramp->property);
		gdouble     t = (gdouble) ramp->done / (gdouble) ramp->total;

		if (pspec != NULL)
			set_numeric_property (G_OBJECT (ramp->effect), pspec,
			                      ramp->from + (ramp->to - ramp->from) * t);
		ramp->done += frames;
		if (ramp->done >= ramp->total)
		{
			/* the end value lands exactly when the ramp ends */
			if (pspec != NULL)
				set_numeric_property (G_OBJECT (ramp->effect), pspec, ramp->to);
			g_ptr_array_remove_index (self->param_ramps, i);
		}
		else
			i++;
	}
}

/* ==========================================================================
 * Ducking
 * ========================================================================== */

gboolean
lrg_audio_mixer_set_duck (LrgAudioMixer *self,
                          const gchar   *trigger,
                          const gchar   *target,
                          gdouble        threshold_db,
                          gdouble        depth_db,
                          gdouble        attack,
                          gdouble        release)
{
	gboolean seen[MAX_BUSES] = { FALSE };
	gint     trg, tgt;
	Duck     duck;
	guint    i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	trg = find_bus (self, trigger);
	tgt = find_bus (self, target);
	if (trg < 0 || tgt < 0 || trg == tgt || isnan (threshold_db) || isnan (depth_db)
	    || depth_db > 0.0 || !isfinite (attack) || attack < 0.0 || attack > MAX_DUCK_TIME
	    || !isfinite (release) || release < 0.0 || release > MAX_DUCK_TIME)
		return FALSE;

	/* an existing rule is edited in place (its state kept) */
	for (i = 0; i < self->ducks->len; i++)
	{
		Duck *d = &g_array_index (self->ducks, Duck, i);

		if (d->trigger == (guint) trg && d->target == (guint) tgt)
		{
			d->threshold = lrg_audio_db_to_gain (threshold_db);
			d->depth = lrg_audio_db_to_gain (depth_db);
			d->attack = attack;
			d->release = release;
			d->att_coef = time_coef (attack, self->rate);
			d->rel_coef = time_coef (release, self->rate);
			return TRUE;
		}
	}

	/* the target must not feed the trigger: the trigger renders first */
	if (reaches (self, (guint) tgt, (guint) trg, seen))
		return FALSE;
	memset (&duck, 0, sizeof duck);
	duck.trigger = (guint) trg;
	duck.target = (guint) tgt;
	duck.threshold = lrg_audio_db_to_gain (threshold_db);
	duck.depth = lrg_audio_db_to_gain (depth_db);
	duck.attack = attack;
	duck.release = release;
	duck.att_coef = time_coef (attack, self->rate);
	duck.rel_coef = time_coef (release, self->rate);
	duck.gain = 1.0;
	g_array_append_val (self->ducks, duck);
	return TRUE;
}

gboolean
lrg_audio_mixer_remove_duck (LrgAudioMixer *self,
                             const gchar   *trigger,
                             const gchar   *target)
{
	gint  trg, tgt;
	guint i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	trg = find_bus (self, trigger);
	tgt = find_bus (self, target);
	for (i = 0; i < self->ducks->len; i++)
	{
		Duck *d = &g_array_index (self->ducks, Duck, i);

		if ((gint) d->trigger == trg && (gint) d->target == tgt)
		{
			g_array_remove_index (self->ducks, i);
			return TRUE;
		}
	}
	return FALSE;
}

void
lrg_audio_mixer_clear_ducks (LrgAudioMixer *self)
{
	guint i;

	g_return_if_fail (LRG_IS_AUDIO_MIXER (self));
	g_array_set_size (self->ducks, 0);
	for (i = 0; i < self->buses->len; i++)
		bus_at (self, i)->duck_gain = 1.0;
}

guint
lrg_audio_mixer_get_duck_count (LrgAudioMixer *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	return self->ducks->len;
}

gdouble
lrg_audio_mixer_get_bus_duck_gain (LrgAudioMixer *self,
                                   const gchar   *name)
{
	gint index;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), -1.0);
	index = find_bus (self, name);
	if (index < 0)
		return -1.0;
	return bus_at (self, (guint) index)->duck_gain;
}

/* ==========================================================================
 * Meters
 * ========================================================================== */

gboolean
lrg_audio_mixer_get_bus_meter (LrgAudioMixer *self,
                               const gchar   *name,
                               gint           channel,
                               gdouble       *out_peak,
                               gdouble       *out_rms)
{
	gint index;
	Bus *bus;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	index = find_bus (self, name);
	if (index < 0 || channel < -1 || channel >= (gint) self->channels)
		return FALSE;
	bus = bus_at (self, (guint) index);
	if (channel < 0)
	{
		if (out_peak != NULL)
			*out_peak = MAX (bus->peak[0], bus->peak[1]);
		if (out_rms != NULL)
			*out_rms = bus->rms_all;
	}
	else
	{
		if (out_peak != NULL)
			*out_peak = bus->peak[channel];
		if (out_rms != NULL)
			*out_rms = bus->rms[channel];
	}
	return TRUE;
}

/* ==========================================================================
 * Voices
 * ========================================================================== */

guint64
lrg_audio_mixer_play (LrgAudioMixer *self,
                      LrgWaveData   *wave,
                      const gchar   *bus,
                      gdouble        gain,
                      gboolean       loop)
{
	g_autoptr(LrgWaveData) converted = NULL;
	Voice                 *voice;
	gint                   index;
	gsize                  i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), 0);
	index = find_bus (self, bus);
	if (index < 0 || !valid_gain (gain) || self->last_id == G_MAXUINT64
	    || !lrg_wave_data_is_valid (wave) || lrg_wave_data_get_frame_count (wave) == 0)
		return 0;
	converted = lrg_wave_data_convert (wave, self->rate, 32, self->channels);
	if (converted == NULL)
		return 0;
	voice = g_new0 (Voice, 1);
	voice->samples = lrg_wave_data_get_samples (converted, &voice->count);
	if (voice->samples == NULL || voice->count == 0 || voice->count % self->channels != 0)
	{
		voice_free (voice);
		return 0;
	}
	for (i = 0; i < voice->count; i++)
		if (!isfinite (voice->samples[i]))
		{
			voice_free (voice);
			return 0;
		}
	voice->frames = voice->count / self->channels;
	voice->id = ++self->last_id;
	voice->bus = (guint) index;
	voice->gain = gain;
	voice->pitch = 1.0;
	voice->loop = loop;
	g_ptr_array_add (self->voices, voice);
	return voice->id;
}

gboolean
lrg_audio_mixer_stop_voice (LrgAudioMixer *self,
                            guint64        id)
{
	guint i;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	for (i = 0; i < self->voices->len; i++)
		if (((Voice *) g_ptr_array_index (self->voices, i))->id == id)
		{
			g_ptr_array_remove_index (self->voices, i);
			return TRUE;
		}
	return FALSE;
}

gboolean
lrg_audio_mixer_pause_voice (LrgAudioMixer *self,
                             guint64        id,
                             gboolean       paused)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	voice = find_voice (self, id);
	if (voice == NULL)
		return FALSE;
	voice->paused = paused;
	return TRUE;
}

guint
lrg_audio_mixer_get_voice_count (LrgAudioMixer *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), 0);
	return self->voices->len;
}

gboolean
lrg_audio_mixer_has_voice (LrgAudioMixer *self,
                           guint64        id)
{
	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	return id != 0 && find_voice (self, id) != NULL;
}

gboolean
lrg_audio_mixer_set_voice_gain (LrgAudioMixer *self,
                                guint64        id,
                                gdouble        gain,
                                gdouble        seconds)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	voice = find_voice (self, id);
	if (voice == NULL || !valid_gain (gain))
		return FALSE;
	ramp_start (&voice->gain_ramp, ramp_value (&voice->gain_ramp, voice->gain), gain,
	            seconds_to_frames (self, seconds));
	if (voice->gain_ramp.total == 0)
		voice->gain = gain;
	return TRUE;
}

gboolean
lrg_audio_mixer_set_voice_pan (LrgAudioMixer *self,
                               guint64        id,
                               gdouble        pan,
                               gdouble        seconds)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	voice = find_voice (self, id);
	if (voice == NULL || !isfinite (pan) || pan < -1.0 || pan > 1.0)
		return FALSE;
	ramp_start (&voice->pan_ramp, ramp_value (&voice->pan_ramp, voice->pan), pan,
	            seconds_to_frames (self, seconds));
	if (voice->pan_ramp.total == 0)
		voice->pan = pan;
	return TRUE;
}

gboolean
lrg_audio_mixer_set_voice_pitch (LrgAudioMixer *self,
                                 guint64        id,
                                 gdouble        pitch)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	voice = find_voice (self, id);
	if (voice == NULL || !isfinite (pitch) || pitch < MIN_PITCH || pitch > MAX_PITCH)
		return FALSE;
	voice->pitch = pitch;
	return TRUE;
}

gboolean
lrg_audio_mixer_set_voice_lowpass (LrgAudioMixer *self,
                                   guint64        id,
                                   gdouble        cutoff)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	voice = find_voice (self, id);
	if (voice == NULL || !isfinite (cutoff) || cutoff < 0.0 || cutoff >= self->rate * 0.5)
		return FALSE;
	if (cutoff == 0.0)
	{
		voice->lp_alpha = 0.0;
		voice->lp[0] = voice->lp[1] = 0.0;
	}
	else
		voice->lp_alpha = 1.0 - exp (-2.0 * G_PI * cutoff / self->rate);
	return TRUE;
}

const gchar *
lrg_audio_mixer_get_voice_bus (LrgAudioMixer *self,
                               guint64        id)
{
	Voice *voice;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	voice = find_voice (self, id);
	return voice != NULL ? bus_at (self, voice->bus)->name : NULL;
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/* Grows (never shrinks) a scratch buffer to at least @len elements. */
static gpointer
scratch_reserve (gpointer  buffer,
                 gsize    *capacity,
                 gsize     len,
                 gsize     element)
{
	if (*capacity >= len)
		return buffer;
	*capacity = len;
	return g_realloc_n (buffer, len, element);
}

/* the sample of channel @c at frame @f of @voice (0 past the end of a
 * voice that does not loop) */
static gdouble
voice_sample (const Voice *voice,
              gsize        f,
              guint        c,
              guint        channels)
{
	if (f >= voice->frames)
	{
		if (!voice->loop)
			return 0.0;
		f %= voice->frames;
	}
	return voice->samples[f * channels + c];
}

/*
 * mix_voice:
 * Adds @frames frames of @voice into @dest.  Returns FALSE once the voice
 * reached its end (it is removed by the caller).
 */
static gboolean
mix_voice (LrgAudioMixer *self,
           Voice         *voice,
           gdouble       *dest,
           guint          frames)
{
	guint ch = self->channels;
	guint f, c;

	/*
	 * Fast path, the common case: unpitched, unfiltered, centred, no ramp
	 * running.  Copies runs of contiguous samples times one gain (the
	 * same arithmetic as the general loop below, so the result is
	 * bit-identical).
	 */
	if (voice->pitch == 1.0 && voice->lp_alpha == 0.0 && voice->pan == 0.0
	    && voice->gain_ramp.done >= voice->gain_ramp.total && voice->pan_ramp.done >= voice->pan_ramp.total
	    && voice->pos == floor (voice->pos))
	{
		gsize   cursor = (gsize) voice->pos;
		gdouble gain = voice->gain;
		guint   done = 0;

		while (done < frames)
		{
			gsize        run = MIN ((gsize) (frames - done), voice->frames - cursor);
			const gfloat *src = voice->samples + cursor * ch;
			gdouble     *out = dest + (gsize) done * ch;
			gsize        k;

			for (k = 0; k < run * ch; k++)
				out[k] += (gdouble) src[k] * gain;
			done += (guint) run;
			cursor += run;
			if (cursor >= voice->frames)
			{
				if (!voice->loop)
				{
					voice->pos = (gdouble) cursor;
					return FALSE;
				}
				cursor = 0;
			}
		}
		voice->pos = (gdouble) cursor;
		return TRUE;
	}

	for (f = 0; f < frames; f++)
	{
		gsize   i0 = (gsize) voice->pos;
		gdouble frac = voice->pos - (gdouble) i0;
		gdouble gain = ramp_step (&voice->gain_ramp, &voice->gain);
		gdouble pan = ramp_step (&voice->pan_ramp, &voice->pan);

		for (c = 0; c < ch; c++)
		{
			gdouble s = voice_sample (voice, i0, c, ch);

			/* linear interpolation; frac is exactly 0 at pitch 1 */
			if (frac > 0.0)
				s += (voice_sample (voice, i0 + 1, c, ch) - s) * frac;
			if (voice->lp_alpha > 0.0)
			{
				voice->lp[c] += voice->lp_alpha * (s - voice->lp[c]);
				s = voice->lp[c];
			}
			if (ch == 2)
			{
				if (c == 0 && pan > 0.0)
					s *= 1.0 - pan;
				else if (c == 1 && pan < 0.0)
					s *= 1.0 + pan;
			}
			dest[f * ch + c] += s * gain;
		}

		voice->pos += voice->pitch;
		if (voice->pos >= (gdouble) voice->frames)
		{
			if (!voice->loop)
				return FALSE;
			voice->pos = fmod (voice->pos, (gdouble) voice->frames);
		}
	}
	return TRUE;
}

/*
 * render_block:
 * Mixes one block of @frames (<= LRG_AUDIO_MIXER_BLOCK_FRAMES) into
 * @output.  @audible is the solo state of every bus.
 */
static void
render_block (LrgAudioMixer  *self,
              guint           frames,
              gfloat         *output,
              const gboolean *audible)
{
	gdouble *buffers;
	guint    indegree[MAX_BUSES] = { 0 };
	gboolean done[MAX_BUSES] = { FALSE };
	gsize    count;
	gsize    k;
	guint    ch = self->channels;
	guint    i, j, step, f, c;

	count = (gsize) frames * ch;
	self->mix = scratch_reserve (self->mix, &self->mix_len, count * self->buses->len, sizeof (gdouble));
	buffers = self->mix;
	memset (buffers, 0, count * self->buses->len * sizeof (gdouble));

	/* 1. voices into their buses */
	for (i = 0; i < self->voices->len;)
	{
		Voice *voice = g_ptr_array_index (self->voices, i);

		if (!voice->paused && !mix_voice (self, voice, buffers + count * voice->bus, frames))
			g_ptr_array_remove_index (self->voices, i);
		else
			i++;
	}

	/* 2. the render order's in-degrees: parents, sends, duck rules */
	for (i = 1; i < self->buses->len; i++)
	{
		Bus *bus = bus_at (self, i);

		indegree[bus->parent]++;
		for (j = 0; j < bus->sends->len; j++)
			indegree[g_array_index (bus->sends, Send, j).target]++;
	}
	for (j = 0; j < self->ducks->len; j++)
		indegree[g_array_index (self->ducks, Duck, j).target]++;

	/* 3. every bus once its inputs are complete */
	for (step = 0; step < self->buses->len; step++)
	{
		Bus     *bus;
		gdouble *samples;
		gboolean silent;

		for (i = 0; i < self->buses->len; i++)
			if (!done[i] && indegree[i] == 0)
				break;
		g_assert (i < self->buses->len);
		done[i] = TRUE;
		bus = bus_at (self, i);
		samples = buffers + count * i;
		silent = bus->muted || !audible[i];

		/* built-in low-pass and delay */
		if (bus->alpha > 0 || bus->delay_frames > 0)
			for (f = 0; f < frames; f++)
			{
				for (c = 0; c < ch; c++)
				{
					gdouble sample = samples[f * ch + c];

					if (bus->alpha > 0)
					{
						bus->low[c] += bus->alpha * (sample - bus->low[c]);
						sample = bus->low[c];
					}
					if (bus->delay_frames > 0)
					{
						guint   pos = bus->cursor * ch + c;
						gdouble delayed = bus->delay[pos];

						bus->delay[pos] = sample + delayed * bus->feedback;
						sample = sample * (1.0 - bus->wet) + delayed * bus->wet;
					}
					samples[f * ch + c] = sample;
				}
				if (bus->delay_frames > 0)
					bus->cursor = (bus->cursor + 1) % bus->delay_frames;
			}

		/* the effect chain, in float */
		if (bus->chain != NULL && lrg_audio_effect_chain_get_count (bus->chain) > 0)
		{
			self->fx = scratch_reserve (self->fx, &self->fx_len, count, sizeof (gfloat));
			for (k = 0; k < count; k++)
				self->fx[k] = (gfloat) samples[k];
			lrg_audio_effect_chain_process (bus->chain, self->fx, frames, ch, self->rate);
			for (k = 0; k < count; k++)
				samples[k] = (gdouble) self->fx[k];
		}

		/* per frame: ducking, fader, mute/solo, meters */
		for (f = 0; f < frames; f++)
		{
			gdouble gain = ramp_step (&bus->gain_ramp, &bus->gain);
			gdouble duck = 1.0;

			for (j = 0; j < self->ducks->len; j++)
			{
				Duck          *d = &g_array_index (self->ducks, Duck, j);
				const gdouble *trig;
				gdouble        level = 0.0;
				gdouble        goal;

				if (d->target != i)
					continue;
				trig = buffers + count * d->trigger + (gsize) f * ch;
				for (c = 0; c < ch; c++)
					level = MAX (level, fabs (trig[c]));
				goal = (level >= d->threshold && level > 0.0) ? d->depth : 1.0;
				d->gain += (goal < d->gain ? d->att_coef : d->rel_coef) * (goal - d->gain);
				duck *= d->gain;
			}
			bus->duck_gain = duck;

			for (c = 0; c < ch; c++)
			{
				gdouble s = silent ? 0.0 : samples[f * ch + c] * gain * duck;
				gdouble a = fabs (s);

				samples[f * ch + c] = s;
				if (a > bus->acc_peak[c])
					bus->acc_peak[c] = a;
				bus->acc_sq[c] += s * s;
			}
		}
		bus->acc_frames += frames;

		/* 4. into the parent and the send targets (send gains ramp per
		 * frame too) */
		if (i != 0)
		{
			gdouble *parent = buffers + count * bus->parent;

			for (k = 0; k < count; k++)
				parent[k] += samples[k];
			indegree[bus->parent]--;
			for (j = 0; j < bus->sends->len; j++)
			{
				Send    *send = &g_array_index (bus->sends, Send, j);
				gdouble *dest = buffers + count * send->target;

				for (f = 0; f < frames; f++)
				{
					gdouble g = ramp_step (&send->ramp, &send->gain);

					for (c = 0; c < ch; c++)
						dest[f * ch + c] += samples[f * ch + c] * g;
				}
				indegree[send->target]--;
			}
		}
		for (j = 0; j < self->ducks->len; j++)
			if (g_array_index (self->ducks, Duck, j).trigger == i)
				indegree[g_array_index (self->ducks, Duck, j).target]--;
	}
	for (k = 0; k < count; k++)
		output[k] = (gfloat) CLAMP (buffers[k], -1.0, 1.0);
}

/*
 * mixer_render_into:
 * Mixes @frames frames into @output (frames * channels samples), block by
 * block, and publishes the meters of the whole call.
 */
static void
mixer_render_into (LrgAudioMixer *self,
                   guint          frames,
                   gfloat        *output)
{
	gboolean audible[MAX_BUSES];
	guint    done = 0;
	guint    i, c;

	compute_audible (self, audible);
	for (i = 0; i < self->buses->len; i++)
	{
		Bus *bus = bus_at (self, i);

		bus->acc_peak[0] = bus->acc_peak[1] = 0.0;
		bus->acc_sq[0] = bus->acc_sq[1] = 0.0;
		bus->acc_frames = 0;
	}
	while (done < frames)
	{
		guint n = MIN (frames - done, (guint) LRG_AUDIO_MIXER_BLOCK_FRAMES);

		apply_param_ramps (self, n);
		render_block (self, n, output + (gsize) done * self->channels, audible);
		done += n;
	}
	for (i = 0; i < self->buses->len; i++)
	{
		Bus    *bus = bus_at (self, i);
		gdouble total = 0.0;

		for (c = 0; c < 2; c++)
		{
			bus->peak[c] = c < self->channels ? bus->acc_peak[c] : 0.0;
			bus->rms[c] = (c < self->channels && bus->acc_frames > 0)
			              ? sqrt (bus->acc_sq[c] / (gdouble) bus->acc_frames) : 0.0;
			if (c < self->channels)
				total += bus->acc_sq[c];
		}
		bus->rms_all = bus->acc_frames > 0
		               ? sqrt (total / ((gdouble) bus->acc_frames * self->channels)) : 0.0;
	}
}

gfloat *
lrg_audio_mixer_render (LrgAudioMixer *self,
                        guint          frames,
                        gsize         *n_samples)
{
	gfloat *output;
	gsize   count;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), NULL);
	g_return_val_if_fail (n_samples != NULL, NULL);
	*n_samples = 0;
	if (frames == 0 || frames > 65536)
		return NULL;
	count = (gsize) frames * self->channels;
	output = g_new (gfloat, count);
	mixer_render_into (self, frames, output);
	*n_samples = count;
	return output;
}

gboolean
lrg_audio_mixer_pump (LrgAudioMixer  *self,
                      GrlAudioStream *stream,
                      guint           frames,
                      GError        **error)
{
	gsize count;

	g_return_val_if_fail (LRG_IS_AUDIO_MIXER (self), FALSE);
	g_return_val_if_fail (GRL_IS_AUDIO_STREAM (stream), FALSE);
	if (frames == 0 || frames > 65536 || !grl_audio_stream_is_valid (stream)
	    || grl_audio_stream_get_sample_rate (stream) != self->rate
	    || grl_audio_stream_get_channels (stream) != self->channels
	    || grl_audio_stream_get_sample_size (stream) != 32)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     "Stream format or frame count does not match mixer");
		return FALSE;
	}
	if (!grl_audio_stream_is_playing (stream) || !grl_audio_stream_is_processed (stream))
		return TRUE;
	count = (gsize) frames * self->channels;
	self->out = scratch_reserve (self->out, &self->out_len, count, sizeof (gfloat));
	mixer_render_into (self, frames, self->out);
	grl_audio_stream_update (stream, self->out, (gint) frames);
	return TRUE;
}
