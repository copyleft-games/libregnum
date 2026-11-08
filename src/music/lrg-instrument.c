/* lrg-instrument.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The instrument base (see lrg-instrument.h and
 * docs/modules/music/instruments.org).
 *
 * The queue is a GArray kept sorted by frame (stable for equal frames: an
 * event goes after every event at its frame).  A render of [now, now + n)
 * walks the queue: the span up to the next event is rendered by the
 * subclass, the event is applied, and so on; events at or before `now`
 * apply at the start of the block.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-instrument.h"
#include "../audio/lrg-audio-dsp-private.h"
#include "../audio/lrg-audio-param-private.h"

G_DEFINE_ENUM_TYPE (LrgInstrumentEventType, lrg_instrument_event_type,
	G_DEFINE_ENUM_VALUE (LRG_INSTRUMENT_EVENT_NOTE_ON, "note-on"),
	G_DEFINE_ENUM_VALUE (LRG_INSTRUMENT_EVENT_NOTE_OFF, "note-off"),
	G_DEFINE_ENUM_VALUE (LRG_INSTRUMENT_EVENT_ALL_OFF, "all-off"),
	G_DEFINE_ENUM_VALUE (LRG_INSTRUMENT_EVENT_PITCH_BEND, "pitch-bend"))

typedef struct
{
	guint64                frame;
	LrgInstrumentEventType type;
	gint                   key;
	gdouble                value;
} Event;

typedef struct
{
	GArray  *queue;      /* Event, sorted by frame */
	guint64  now;        /* frames rendered */
	gdouble  bend;       /* semitones */
	guint    polyphony;
	gdouble  volume;     /* dB */
} LrgInstrumentPrivate;

static void lrg_instrument_source_init (LrgAudioSourceInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (LrgInstrument, lrg_instrument, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (LrgInstrument)
                                  G_IMPLEMENT_INTERFACE (LRG_TYPE_AUDIO_SOURCE, lrg_instrument_source_init))

enum
{
	PROP_0,
	PROP_POLYPHONY,
	PROP_VOLUME,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

/* ==========================================================================
 * The queue
 * ========================================================================== */

/* the index after the last event at or before @frame (binary search) */
static guint
upper_bound (GArray  *queue,
             guint64  frame)
{
	guint lo = 0;
	guint hi = queue->len;

	while (lo < hi)
	{
		guint mid = lo + (hi - lo) / 2;

		if (g_array_index (queue, Event, mid).frame <= frame)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static void
apply_event (LrgInstrument *self,
             const Event   *ev)
{
	LrgInstrumentPrivate *priv = lrg_instrument_get_instance_private (self);
	LrgInstrumentClass   *klass = LRG_INSTRUMENT_GET_CLASS (self);

	switch (ev->type)
	{
	case LRG_INSTRUMENT_EVENT_NOTE_ON:
		if (klass->note_on != NULL && ev->key >= 0 && ev->key <= 127)
		{
			/* velocity 0 is a note off, as in MIDI */
			if (ev->value <= 0.0)
			{
				if (klass->note_off != NULL)
					klass->note_off (self, ev->key);
			}
			else
				klass->note_on (self, ev->key, MIN (ev->value, 1.0));
		}
		break;
	case LRG_INSTRUMENT_EVENT_NOTE_OFF:
		if (klass->note_off != NULL && ev->key >= 0 && ev->key <= 127)
			klass->note_off (self, ev->key);
		break;
	case LRG_INSTRUMENT_EVENT_ALL_OFF:
		if (klass->all_off != NULL)
			klass->all_off (self, ev->value != 0.0);
		break;
	case LRG_INSTRUMENT_EVENT_PITCH_BEND:
	default:
		priv->bend = CLAMP (ev->value, -48.0, 48.0);
		break;
	}
}

void
lrg_instrument_schedule (LrgInstrument          *self,
                         guint64                 frame,
                         LrgInstrumentEventType  type,
                         gint                    key,
                         gdouble                 value)
{
	LrgInstrumentPrivate *priv;
	Event                 ev;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));

	priv = lrg_instrument_get_instance_private (self);
	if (!isfinite (value))
		return;
	ev.frame = MAX (frame, priv->now);
	ev.type = type;
	ev.key = key;
	ev.value = value;
	g_array_insert_val (priv->queue, upper_bound (priv->queue, ev.frame), ev);
}

void
lrg_instrument_note_on (LrgInstrument *self,
                        gint           key,
                        gdouble        velocity)
{
	LrgInstrumentPrivate *priv;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));
	priv = lrg_instrument_get_instance_private (self);
	lrg_instrument_schedule (self, priv->now, LRG_INSTRUMENT_EVENT_NOTE_ON, key, velocity);
}

void
lrg_instrument_note_off (LrgInstrument *self,
                         gint           key)
{
	LrgInstrumentPrivate *priv;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));
	priv = lrg_instrument_get_instance_private (self);
	lrg_instrument_schedule (self, priv->now, LRG_INSTRUMENT_EVENT_NOTE_OFF, key, 0.0);
}

void
lrg_instrument_all_notes_off (LrgInstrument *self,
                              gboolean       hard)
{
	LrgInstrumentPrivate *priv;
	LrgInstrumentClass   *klass;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));
	priv = lrg_instrument_get_instance_private (self);
	klass = LRG_INSTRUMENT_GET_CLASS (self);
	g_array_set_size (priv->queue, 0);
	if (klass->all_off != NULL)
		klass->all_off (self, hard);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

void
lrg_instrument_render (LrgInstrument *self,
                       gfloat        *out,
                       guint          frames,
                       guint          channels,
                       guint          sample_rate)
{
	LrgInstrumentPrivate *priv;
	LrgInstrumentClass   *klass;
	guint64               end;
	guint                 done = 0;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));
	g_return_if_fail (out != NULL || frames == 0);

	priv = lrg_instrument_get_instance_private (self);
	klass = LRG_INSTRUMENT_GET_CLASS (self);
	end = priv->now + frames;
	while (done < frames)
	{
		guint64 here = priv->now + done;
		guint   span;

		/* every event due by now */
		while (priv->queue->len > 0 && g_array_index (priv->queue, Event, 0).frame <= here)
		{
			Event ev = g_array_index (priv->queue, Event, 0);

			g_array_remove_index (priv->queue, 0);
			apply_event (self, &ev);
		}

		/* up to the next event or the end of the block */
		if (priv->queue->len > 0 && g_array_index (priv->queue, Event, 0).frame < end)
			span = (guint) (g_array_index (priv->queue, Event, 0).frame - here);
		else
			span = frames - done;
		if (span > 0 && klass->render_span != NULL)
			klass->render_span (self, out + (gsize) done * channels, span, channels, sample_rate);
		done += span;
	}
	priv->now = end;
}

static gboolean
source_render (LrgAudioSource *source,
               gfloat         *out,
               guint           frames,
               guint           channels,
               guint           sample_rate)
{
	lrg_instrument_render (LRG_INSTRUMENT (source), out, frames, channels, sample_rate);
	return TRUE;
}

static void
source_reset (LrgAudioSource *source)
{
	lrg_instrument_reset (LRG_INSTRUMENT (source));
}

static void
lrg_instrument_source_init (LrgAudioSourceInterface *iface)
{
	iface->render = source_render;
	iface->reset = source_reset;
}

void
lrg_instrument_reset (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;
	LrgInstrumentClass   *klass;

	g_return_if_fail (LRG_IS_INSTRUMENT (self));
	priv = lrg_instrument_get_instance_private (self);
	klass = LRG_INSTRUMENT_GET_CLASS (self);
	g_array_set_size (priv->queue, 0);
	priv->now = 0;
	priv->bend = 0.0;
	if (klass->reset_voices != NULL)
		klass->reset_voices (self);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

gdouble
lrg_instrument_get_pitch_bend (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 0.0);
	priv = lrg_instrument_get_instance_private (self);
	return priv->bend;
}

guint64
lrg_instrument_get_position (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 0);
	priv = lrg_instrument_get_instance_private (self);
	return priv->now;
}

guint
lrg_instrument_get_pending (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 0);
	priv = lrg_instrument_get_instance_private (self);
	return priv->queue->len;
}

guint
lrg_instrument_get_active_voices (LrgInstrument *self)
{
	LrgInstrumentClass *klass;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 0);
	klass = LRG_INSTRUMENT_GET_CLASS (self);
	return klass->get_active_voices != NULL ? klass->get_active_voices (self) : 0;
}

guint
lrg_instrument_get_polyphony (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 1);
	priv = lrg_instrument_get_instance_private (self);
	return priv->polyphony;
}

gdouble
lrg_instrument_get_volume_gain (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), 1.0);
	priv = lrg_instrument_get_instance_private (self);
	return lrg_dsp_db_to_lin (priv->volume);
}

gboolean
lrg_instrument_set_param (LrgInstrument *self,
                          const gchar   *name,
                          gdouble        value)
{
	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), FALSE);
	if (name == NULL)
		return FALSE;
	return lrg_audio_params_set_clamped (G_OBJECT (self), name, value);
}

gdouble
lrg_instrument_get_param (LrgInstrument *self,
                          const gchar   *name)
{
	GParamSpec *pspec;
	GValue      v = G_VALUE_INIT;
	gdouble     result = NAN;

	g_return_val_if_fail (LRG_IS_INSTRUMENT (self), NAN);
	if (name == NULL)
		return NAN;
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self), name);
	if (pspec == NULL || !(pspec->flags & G_PARAM_READABLE))
		return NAN;
	g_value_init (&v, pspec->value_type);
	g_object_get_property (G_OBJECT (self), name, &v);
	if (G_VALUE_HOLDS_DOUBLE (&v))
		result = g_value_get_double (&v);
	else if (G_VALUE_HOLDS_FLOAT (&v))
		result = g_value_get_float (&v);
	else if (G_VALUE_HOLDS_INT (&v))
		result = g_value_get_int (&v);
	else if (G_VALUE_HOLDS_UINT (&v))
		result = g_value_get_uint (&v);
	else if (G_VALUE_HOLDS_BOOLEAN (&v))
		result = g_value_get_boolean (&v) ? 1.0 : 0.0;
	else if (G_VALUE_HOLDS_ENUM (&v))
		result = g_value_get_enum (&v);
	g_value_unset (&v);
	return result;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_instrument_finalize (GObject *object)
{
	LrgInstrumentPrivate *priv = lrg_instrument_get_instance_private (LRG_INSTRUMENT (object));

	g_array_unref (priv->queue);
	G_OBJECT_CLASS (lrg_instrument_parent_class)->finalize (object);
}

static void
lrg_instrument_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgInstrumentPrivate *priv = lrg_instrument_get_instance_private (LRG_INSTRUMENT (object));

	switch (prop_id)
	{
	case PROP_POLYPHONY: g_value_set_uint (value, priv->polyphony); break;
	case PROP_VOLUME: g_value_set_double (value, priv->volume); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_instrument_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgInstrumentPrivate *priv = lrg_instrument_get_instance_private (LRG_INSTRUMENT (object));

	switch (prop_id)
	{
	case PROP_POLYPHONY: priv->polyphony = g_value_get_uint (value); break;
	case PROP_VOLUME: priv->volume = g_value_get_double (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_instrument_class_init (LrgInstrumentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_instrument_finalize;
	object_class->get_property = lrg_instrument_get_property;
	object_class->set_property = lrg_instrument_set_property;

	/**
	 * LrgInstrument:polyphony:
	 *
	 * The most voices sounding at once; past it the oldest note is
	 * stolen (released voices first).
	 */
	props[PROP_POLYPHONY] = g_param_spec_uint ("polyphony", "polyphony", "Most voices at once", 1,
	                                           LRG_INSTRUMENT_MAX_POLYPHONY, 16,
	                                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgInstrument:volume:
	 *
	 * Output level of the instrument in dB.
	 */
	props[PROP_VOLUME] = g_param_spec_double ("volume", "volume", "Output level in dB", -60.0, 12.0, 0.0,
	                                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
lrg_instrument_init (LrgInstrument *self)
{
	LrgInstrumentPrivate *priv = lrg_instrument_get_instance_private (self);

	priv->queue = g_array_new (FALSE, FALSE, sizeof (Event));
	priv->polyphony = 16;
}

/* ==========================================================================
 * Note names
 * ========================================================================== */

gint
lrg_music_key_from_name (const gchar *name)
{
	static const gint letters[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
	const gchar      *p = name;
	gchar            *end = NULL;
	gint64            number;
	gint              key;
	gint64            octave;

	if (name == NULL)
		return -1;
	while (g_ascii_isspace (*p))
		p++;

	/* a plain MIDI number */
	if (g_ascii_isdigit (*p))
	{
		number = g_ascii_strtoll (p, &end, 10);
		while (end != NULL && g_ascii_isspace (*end))
			end++;
		if (end == NULL || *end != '\0' || number < 0 || number > 127)
			return -1;
		return (gint) number;
	}

	/* letter, accidentals, octave */
	{
		gchar c = g_ascii_toupper (*p);

		if (c < 'A' || c > 'G')
			return -1;
		key = letters[c - 'A'];
	}
	p++;
	for (;; p++)
	{
		if (*p == '#' || *p == 's')
			key++;
		else if (*p == 'b')
			key--;
		else
			break;
	}
	if (*p == '\0')
		return -1;
	octave = g_ascii_strtoll (p, &end, 10);
	if (end == p)
		return -1;
	while (g_ascii_isspace (*end))
		end++;
	if (*end != '\0')
		return -1;
	number = (octave + 1) * 12 + key;
	if (number < 0 || number > 127)
		return -1;
	return (gint) number;
}

gchar *
lrg_music_key_to_name (gint key)
{
	static const gchar *names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

	if (key < 0 || key > 127)
		return NULL;
	return g_strdup_printf ("%s%d", names[key % 12], key / 12 - 1);
}

gdouble
lrg_music_key_to_hz (gdouble key)
{
	return lrg_dsp_key_to_hz (key);
}
