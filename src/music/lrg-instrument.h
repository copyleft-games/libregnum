/* lrg-instrument.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The base of every playable instrument (#LrgSynth, #LrgDrumMachine,
 * #LrgSampler, #LrgSoundFontInstrument, #LrgFoleyInstrument).  An
 * instrument is an #LrgAudioSource: a mixer plays it as a voice and it
 * renders whatever notes it was given.  Notes are scheduled on the
 * instrument's own frame clock, so a note lands on the exact sample it
 * was asked for whatever the render block size.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../audio/lrg-audio-source.h"

G_BEGIN_DECLS

/**
 * LRG_INSTRUMENT_MAX_POLYPHONY:
 *
 * The most voices an instrument sounds at once.
 */
#define LRG_INSTRUMENT_MAX_POLYPHONY (128)

/**
 * LrgInstrumentEventType:
 * @LRG_INSTRUMENT_EVENT_NOTE_ON: start @key at @value velocity (0 .. 1)
 * @LRG_INSTRUMENT_EVENT_NOTE_OFF: release @key
 * @LRG_INSTRUMENT_EVENT_ALL_OFF: release every note (@value non-zero:
 *   cut them at once)
 * @LRG_INSTRUMENT_EVENT_PITCH_BEND: bend every note by @value semitones
 *
 * What a scheduled instrument event does.
 */
typedef enum
{
	LRG_INSTRUMENT_EVENT_NOTE_ON,
	LRG_INSTRUMENT_EVENT_NOTE_OFF,
	LRG_INSTRUMENT_EVENT_ALL_OFF,
	LRG_INSTRUMENT_EVENT_PITCH_BEND
} LrgInstrumentEventType;

LRG_AVAILABLE_IN_ALL
GType lrg_instrument_event_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_INSTRUMENT_EVENT_TYPE (lrg_instrument_event_type_get_type ())

#define LRG_TYPE_INSTRUMENT (lrg_instrument_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgInstrument, lrg_instrument, LRG, INSTRUMENT, GObject)

/**
 * LrgInstrumentClass:
 * @parent_class: the parent class
 * @note_on: starts @key (0 .. 127) at @velocity (0 .. 1)
 * @note_off: releases @key
 * @all_off: releases every note, or with @hard silences them at once
 * @render_span: adds @frames frames to @out (interleaved, @channels 1 or
 *   2, at @sample_rate); called between events, so every note change
 *   falls on a span boundary
 * @get_active_voices: the number of voices still sounding
 * @reset_voices: forgets every voice and all DSP state
 *
 * The class of an instrument.  The base class owns the clock and the
 * event queue and calls these at the right frames; subclasses only make
 * sound.
 */
struct _LrgInstrumentClass
{
	GObjectClass parent_class;

	/*< public >*/
	void  (*note_on)           (LrgInstrument *self,
	                            gint           key,
	                            gdouble        velocity);
	void  (*note_off)          (LrgInstrument *self,
	                            gint           key);
	void  (*all_off)           (LrgInstrument *self,
	                            gboolean       hard);
	void  (*render_span)       (LrgInstrument *self,
	                            gfloat        *out,
	                            guint          frames,
	                            guint          channels,
	                            guint          sample_rate);
	guint (*get_active_voices) (LrgInstrument *self);
	void  (*reset_voices)      (LrgInstrument *self);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_instrument_schedule:
 * @self: an #LrgInstrument
 * @frame: when, on the instrument's clock (frames rendered since the last
 *   reset); a frame already rendered means the start of the next render
 * @type: what happens
 * @key: the MIDI key (0 .. 127) for note events, ignored otherwise
 * @value: velocity (note on), hard flag (all off) or semitones (bend)
 *
 * Queues an event.  Events at the same frame run in the order they were
 * scheduled.  Not thread-safe: schedule on the thread that renders.
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_schedule (LrgInstrument          *self,
                              guint64                 frame,
                              LrgInstrumentEventType  type,
                              gint                    key,
                              gdouble                 value);

/**
 * lrg_instrument_note_on:
 * @self: an #LrgInstrument
 * @key: the MIDI key (0 .. 127, 60 is middle C)
 * @velocity: 0 .. 1
 *
 * Starts a note at the start of the next render.
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_note_on (LrgInstrument *self,
                             gint           key,
                             gdouble        velocity);

/**
 * lrg_instrument_note_off:
 * @self: an #LrgInstrument
 * @key: the MIDI key
 *
 * Releases a note at the start of the next render.
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_note_off (LrgInstrument *self,
                              gint           key);

/**
 * lrg_instrument_all_notes_off:
 * @self: an #LrgInstrument
 * @hard: %TRUE cuts every voice at once, %FALSE lets them release
 *
 * Drops every queued event, then releases (or cuts) every note now.
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_all_notes_off (LrgInstrument *self,
                                   gboolean       hard);

/**
 * lrg_instrument_get_pitch_bend:
 * @self: an #LrgInstrument
 *
 * Returns: the current bend in semitones (subclasses read it while
 *   rendering)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_instrument_get_pitch_bend (LrgInstrument *self);

/**
 * lrg_instrument_get_position:
 * @self: an #LrgInstrument
 *
 * Returns: the frames rendered since the last reset (the clock
 *   lrg_instrument_schedule() uses)
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_instrument_get_position (LrgInstrument *self);

/**
 * lrg_instrument_get_pending:
 * @self: an #LrgInstrument
 *
 * Returns: the number of queued events
 */
LRG_AVAILABLE_IN_ALL
guint lrg_instrument_get_pending (LrgInstrument *self);

/**
 * lrg_instrument_get_active_voices:
 * @self: an #LrgInstrument
 *
 * Returns: the voices sounding now
 */
LRG_AVAILABLE_IN_ALL
guint lrg_instrument_get_active_voices (LrgInstrument *self);

/**
 * lrg_instrument_get_polyphony:
 * @self: an #LrgInstrument
 *
 * Returns: the most voices the instrument sounds at once (property
 *   #LrgInstrument:polyphony); the oldest note is stolen past it
 */
LRG_AVAILABLE_IN_ALL
guint lrg_instrument_get_polyphony (LrgInstrument *self);

/**
 * lrg_instrument_get_volume_gain:
 * @self: an #LrgInstrument
 *
 * Returns: #LrgInstrument:volume as a linear gain, for subclasses
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_instrument_get_volume_gain (LrgInstrument *self);

/**
 * lrg_instrument_render:
 * @self: an #LrgInstrument
 * @out: (array) (inout): @frames x @channels samples to add to
 * @frames: frames to render
 * @channels: 1 or 2
 * @sample_rate: the rate in Hz
 *
 * Renders the next @frames frames, running every event that falls in
 * them at its frame.  The same as lrg_audio_source_render().
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_render (LrgInstrument *self,
                            gfloat        *out,
                            guint          frames,
                            guint          channels,
                            guint          sample_rate);

/**
 * lrg_instrument_reset:
 * @self: an #LrgInstrument
 *
 * Silences every voice, drops the queue and the bend and restarts the
 * clock at frame 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_instrument_reset (LrgInstrument *self);

/**
 * lrg_instrument_set_param:
 * @self: an #LrgInstrument
 * @name: a numeric, boolean or enum property
 * @value: the value, clamped to the property's range
 *
 * Sets an instrument parameter by name (presets, automation, the .rsong
 * format, the IPC).  Unlike g_object_set() an out-of-range value is
 * clamped instead of refused.
 *
 * Returns: %TRUE for a known writable numeric property and a finite value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_instrument_set_param (LrgInstrument *self,
                                   const gchar   *name,
                                   gdouble        value);

/**
 * lrg_instrument_get_param:
 * @self: an #LrgInstrument
 * @name: a numeric, boolean or enum property
 *
 * Returns: the value, or NaN when @name is not a numeric property
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_instrument_get_param (LrgInstrument *self,
                                  const gchar   *name);

/**
 * lrg_music_key_from_name:
 * @name: a note name such as "C4", "F#2", "Bb-1", or a MIDI number "60"
 *
 * Parses a note name.  Octaves follow scientific pitch: C4 is middle C
 * (60), A4 is 69.  Sharps are `#` or `s`, flats `b`.
 *
 * Returns: the MIDI key 0 .. 127, or -1 when @name is not a note
 */
LRG_AVAILABLE_IN_ALL
gint lrg_music_key_from_name (const gchar *name);

/**
 * lrg_music_key_to_name:
 * @key: a MIDI key 0 .. 127
 *
 * Returns: (transfer full) (nullable): the name with sharps ("C#4"), or
 *   %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_music_key_to_name (gint key);

/**
 * lrg_music_key_to_hz:
 * @key: a MIDI key (fractional keys are detuned notes)
 *
 * Returns: the frequency in Hz (A4 = 69 = 440 Hz)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_music_key_to_hz (gdouble key);

G_END_DECLS
