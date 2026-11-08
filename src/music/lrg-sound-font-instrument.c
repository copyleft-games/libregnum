/* lrg-sound-font-instrument.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One SoundFont preset as an instrument (see lrg-sound-font-instrument.h).
 *
 * The bank bytes are kept: TinySoundFont loads a bank for one output
 * rate, so the first render at another rate reloads it.  Notes go to MIDI
 * channel 1 (or channel 10, the percussion bank, with `drums`), the
 * program change is sent whenever the program or bank changes, and the
 * synthesizer's output (which overwrites its buffer) is rendered into
 * scratch and added.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-sound-font-instrument.h"
#include "../audio/lrg-midi-synth.h"
#include "../audio/lrg-midi-synth-private.h"

struct _LrgSoundFontInstrument
{
	LrgInstrument       parent_instance;

	GBytes             *bytes;
	LrgMidiSynthFont   *font;
	LrgMidiSynthVoices *voices;
	guint               rate;        /* the rate font and voices were made for */
	guint               program;
	gboolean            drums;
	gboolean            program_dirty;
	gfloat             *scratch;
	gsize               scratch_len;
	gdouble             last_bend;
};

G_DEFINE_FINAL_TYPE (LrgSoundFontInstrument, lrg_sound_font_instrument, LRG_TYPE_INSTRUMENT)

enum
{
	PROP_0,
	PROP_PROGRAM,
	PROP_DRUMS,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

static guint8
channel_of (LrgSoundFontInstrument *self)
{
	return self->drums ? 9 : 0;
}

static void
send (LrgSoundFontInstrument *self,
      guint8                  type,
      guint8                  data1,
      guint8                  data2,
      guint16                 bend)
{
	LrgMidiSynthEvent ev;

	if (self->voices == NULL)
		return;
	memset (&ev, 0, sizeof ev);
	ev.type = type;
	ev.channel = channel_of (self);
	ev.data1 = data1;
	ev.data2 = data2;
	ev.bend = bend;
	lrg_midi_synth_voices_apply (self->voices, &ev);
}

/* (re)loads the bank for @rate */
static gboolean
ensure_rate (LrgSoundFontInstrument *self,
             guint                   rate)
{
	LrgMidiSynthFont *font;
	const guint8     *data;
	gsize             size = 0;

	if (self->voices != NULL && self->rate == rate)
		return TRUE;
	data = g_bytes_get_data (self->bytes, &size);
	font = lrg_midi_synth_font_load_checked (data, size, rate, NULL);
	if (font == NULL)
		return FALSE;
	g_clear_pointer (&self->voices, lrg_midi_synth_voices_free);
	g_clear_pointer (&self->font, lrg_midi_synth_font_free);
	self->font = font;
	self->voices = lrg_midi_synth_voices_new (font);
	self->rate = rate;
	self->program_dirty = TRUE;
	return self->voices != NULL;
}

static void
sf_note_on (LrgInstrument *instrument,
            gint           key,
            gdouble        velocity)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (instrument);

	if (self->program_dirty)
	{
		send (self, LRG_MIDI_SYNTH_EVENT_PROGRAM_CHANGE, (guint8) self->program, 0, 0);
		self->program_dirty = FALSE;
	}
	send (self, LRG_MIDI_SYNTH_EVENT_NOTE_ON, (guint8) key,
	      (guint8) CLAMP (lround (velocity * 127.0), 1, 127), 0);
}

static void
sf_note_off (LrgInstrument *instrument,
             gint           key)
{
	send (LRG_SOUND_FONT_INSTRUMENT (instrument), LRG_MIDI_SYNTH_EVENT_NOTE_OFF, (guint8) key, 0, 0);
}

static void
sf_all_off (LrgInstrument *instrument,
            gboolean       hard)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (instrument);

	if (self->voices == NULL)
		return;
	if (hard)
	{
		/* a fresh synthesizer is the only hard stop TinySoundFont offers */
		g_clear_pointer (&self->voices, lrg_midi_synth_voices_free);
		self->voices = lrg_midi_synth_voices_new (self->font);
		self->program_dirty = TRUE;
	}
	else
		lrg_midi_synth_voices_release_all (self->voices);
}

static guint
sf_get_active_voices (LrgInstrument *instrument)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (instrument);

	return self->voices != NULL ? (guint) MAX (lrg_midi_synth_voices_get_active (self->voices), 0) : 0;
}

static void
sf_reset_voices (LrgInstrument *instrument)
{
	sf_all_off (instrument, TRUE);
}

static void
sf_render_span (LrgInstrument *instrument,
                gfloat        *out,
                guint          frames,
                guint          channels,
                guint          sample_rate)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (instrument);
	gdouble                 volume = lrg_instrument_get_volume_gain (instrument);
	gdouble                 bend = lrg_instrument_get_pitch_bend (instrument);
	guint                   f;

	if (!ensure_rate (self, sample_rate))
		return;
	if (bend != self->last_bend)
	{
		/* +-2 semitones is the General MIDI wheel range */
		gint wheel = (gint) CLAMP (8192.0 + bend / 2.0 * 8192.0, 0.0, 16383.0);

		send (self, LRG_MIDI_SYNTH_EVENT_PITCH_BEND, 0, 0, (guint16) wheel);
		self->last_bend = bend;
	}
	if (self->scratch_len < (gsize) frames * 2)
	{
		self->scratch_len = (gsize) frames * 2;
		self->scratch = g_realloc_n (self->scratch, self->scratch_len, sizeof (gfloat));
	}
	lrg_midi_synth_voices_render (self->voices, self->scratch, frames);
	for (f = 0; f < frames; f++)
	{
		if (channels == 2)
		{
			out[f * 2] += (gfloat) (self->scratch[f * 2] * volume);
			out[f * 2 + 1] += (gfloat) (self->scratch[f * 2 + 1] * volume);
		}
		else
			out[f] += (gfloat) ((self->scratch[f * 2] + self->scratch[f * 2 + 1]) * 0.5 * volume);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_sound_font_instrument_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (object);

	switch (prop_id)
	{
	case PROP_PROGRAM: g_value_set_uint (value, self->program); break;
	case PROP_DRUMS: g_value_set_boolean (value, self->drums); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sound_font_instrument_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (object);

	switch (prop_id)
	{
	case PROP_PROGRAM:
		self->program = g_value_get_uint (value);
		self->program_dirty = TRUE;
		break;
	case PROP_DRUMS:
		self->drums = g_value_get_boolean (value);
		self->program_dirty = TRUE;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sound_font_instrument_finalize (GObject *object)
{
	LrgSoundFontInstrument *self = LRG_SOUND_FONT_INSTRUMENT (object);

	g_clear_pointer (&self->voices, lrg_midi_synth_voices_free);
	g_clear_pointer (&self->font, lrg_midi_synth_font_free);
	g_clear_pointer (&self->bytes, g_bytes_unref);
	g_free (self->scratch);
	G_OBJECT_CLASS (lrg_sound_font_instrument_parent_class)->finalize (object);
}

static void
lrg_sound_font_instrument_class_init (LrgSoundFontInstrumentClass *klass)
{
	GObjectClass       *object_class = G_OBJECT_CLASS (klass);
	LrgInstrumentClass *instrument_class = LRG_INSTRUMENT_CLASS (klass);

	object_class->get_property = lrg_sound_font_instrument_get_property;
	object_class->set_property = lrg_sound_font_instrument_set_property;
	object_class->finalize = lrg_sound_font_instrument_finalize;
	instrument_class->note_on = sf_note_on;
	instrument_class->note_off = sf_note_off;
	instrument_class->all_off = sf_all_off;
	instrument_class->render_span = sf_render_span;
	instrument_class->get_active_voices = sf_get_active_voices;
	instrument_class->reset_voices = sf_reset_voices;

	/**
	 * LrgSoundFontInstrument:program:
	 *
	 * The preset (General MIDI program number, 0 .. 127).
	 */
	props[PROP_PROGRAM] = g_param_spec_uint ("program", "program", "Preset (program number)", 0, 127, 0,
	                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgSoundFontInstrument:drums:
	 *
	 * Whether notes play the percussion bank (MIDI channel 10).
	 */
	props[PROP_DRUMS] = g_param_spec_boolean ("drums", "drums", "Play the percussion bank", FALSE,
	                                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
lrg_sound_font_instrument_init (LrgSoundFontInstrument *self)
{
	self->program_dirty = TRUE;
}

LrgSoundFontInstrument *
lrg_sound_font_instrument_new_from_bytes (GBytes  *sf2,
                                          GError **error)
{
	g_autoptr(LrgSoundFontInstrument) self = NULL;
	const guint8                     *data;
	gsize                             size = 0;

	g_return_val_if_fail (sf2 != NULL, NULL);

	data = g_bytes_get_data (sf2, &size);
	self = g_object_new (LRG_TYPE_SOUND_FONT_INSTRUMENT, NULL);
	self->font = lrg_midi_synth_font_load_checked (data, size, 48000, error);
	if (self->font == NULL)
		return NULL;
	self->bytes = g_bytes_ref (sf2);
	self->voices = lrg_midi_synth_voices_new (self->font);
	self->rate = 48000;
	return g_steal_pointer (&self);
}

LrgSoundFontInstrument *
lrg_sound_font_instrument_new_from_file (const gchar  *path,
                                         GError      **error)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autofree gchar *text = NULL;
	gsize             length = 0;

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, error))
		return NULL;
	bytes = g_bytes_new_take (g_steal_pointer (&text), length);
	return lrg_sound_font_instrument_new_from_bytes (bytes, error);
}

guint
lrg_sound_font_instrument_get_preset_count (LrgSoundFontInstrument *self)
{
	g_return_val_if_fail (LRG_IS_SOUND_FONT_INSTRUMENT (self), 0);
	return self->font != NULL ? (guint) MAX (lrg_midi_synth_font_get_preset_count (self->font), 0) : 0;
}
