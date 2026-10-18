/* lrg-midi-synth-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal bridge between LrgMidiSynth (lrg-midi-synth.c) and the
 * vendored, unmodified TinySoundFont / TinyMidiLoader single headers,
 * which are compiled in exactly one translation unit
 * (lrg-midi-synth-vendor.c).  Nothing here is installed or introspected;
 * the vendored types never leak into the public API.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * LrgMidiSynthEventType:
 * The channel messages a song plays (MIDI status high nibble), plus the
 * tempo meta event TinyMidiLoader keeps (only used to report it).
 */
typedef enum
{
    LRG_MIDI_SYNTH_EVENT_NOTE_OFF         = 0x80,
    LRG_MIDI_SYNTH_EVENT_NOTE_ON          = 0x90,
    LRG_MIDI_SYNTH_EVENT_KEY_PRESSURE     = 0xA0,
    LRG_MIDI_SYNTH_EVENT_CONTROL_CHANGE   = 0xB0,
    LRG_MIDI_SYNTH_EVENT_PROGRAM_CHANGE   = 0xC0,
    LRG_MIDI_SYNTH_EVENT_CHANNEL_PRESSURE = 0xD0,
    LRG_MIDI_SYNTH_EVENT_PITCH_BEND       = 0xE0
} LrgMidiSynthEventType;

/*
 * LrgMidiSynthEvent:
 * One MIDI message of a song, at its absolute time in milliseconds
 * (TinyMidiLoader's tempo-mapped time).  @data1 is the key, controller
 * or program; @data2 the velocity or controller value; @bend the 14-bit
 * pitch wheel of a pitch bend.
 */
typedef struct
{
    guint32 time_ms;
    guint8  type;
    guint8  channel;
    guint8  data1;
    guint8  data2;
    guint16 bend;
} LrgMidiSynthEvent;

/* a loaded SoundFont (TinySoundFont "tsf"), the template of every song */
typedef struct _LrgMidiSynthFont LrgMidiSynthFont;

/* one song's private synthesizer instance (a tsf_copy() of the font) */
typedef struct _LrgMidiSynthVoices LrgMidiSynthVoices;

/*
 * lrg_midi_synth_font_load:
 * Loads a SoundFont that lrg_midi_synth_sf2_validate() accepted (the
 * vendored loader trusts its indices) for stereo interleaved float
 * output at @sample_rate.  After loading, every region's sample range is
 * clamped inside the sample pool (generator offsets may push it out),
 * so rendering never reads past the pool.  @sample_count is the pool
 * size the validator measured.  Returns NULL when TinySoundFont rejects
 * the data.
 */
G_GNUC_INTERNAL LrgMidiSynthFont *
lrg_midi_synth_font_load (const guint8 *data,
                          gsize         size,
                          guint32       sample_count,
                          guint         sample_rate);

G_GNUC_INTERNAL void
lrg_midi_synth_font_free (LrgMidiSynthFont *font);

/* the number of presets of a loaded font */
G_GNUC_INTERNAL gint
lrg_midi_synth_font_get_preset_count (LrgMidiSynthFont *font);

/*
 * lrg_midi_synth_voices_new:
 * A fresh synthesizer sharing @font's samples: 16 channels on General
 * MIDI defaults (preset 0, channel 10 on the drum bank), no voices.
 */
G_GNUC_INTERNAL LrgMidiSynthVoices *
lrg_midi_synth_voices_new (LrgMidiSynthFont *font);

G_GNUC_INTERNAL void
lrg_midi_synth_voices_free (LrgMidiSynthVoices *voices);

/* applies one channel message */
G_GNUC_INTERNAL void
lrg_midi_synth_voices_apply (LrgMidiSynthVoices      *voices,
                             const LrgMidiSynthEvent *event);

/* releases every sounding note (they fade with their release envelope) */
G_GNUC_INTERNAL void
lrg_midi_synth_voices_release_all (LrgMidiSynthVoices *voices);

/* voices still sounding (playing or releasing) */
G_GNUC_INTERNAL gint
lrg_midi_synth_voices_get_active (LrgMidiSynthVoices *voices);

/*
 * lrg_midi_synth_voices_render:
 * Writes @n_frames interleaved stereo frames into @frames (overwriting).
 */
G_GNUC_INTERNAL void
lrg_midi_synth_voices_render (LrgMidiSynthVoices *voices,
                              gfloat             *frames,
                              guint               n_frames);

/*
 * lrg_midi_synth_events_load:
 * Converts a Standard MIDI File (already checked by
 * lrg_midi_synth_smf_scan()) with TinyMidiLoader into a time-ordered
 * array of #LrgMidiSynthEvent (meta events dropped).  Returns NULL when
 * the loader rejects the data; an empty array for a song without
 * channel messages.
 */
G_GNUC_INTERNAL GArray *
lrg_midi_synth_events_load (const guint8 *data,
                            gsize         size);

/*
 * lrg_midi_synth_parse_song:
 * Validates a Standard MIDI File and returns its channel events
 * (#LrgMidiSynthEvent, by time, owned by the caller), its length and its
 * loop range ("loopStart" / "loopEnd" markers, else the whole song), all
 * in milliseconds.  A malformed file fails with
 * %LRG_MIDI_SYNTH_ERROR_INVALID and returns nothing.  Shared by
 * #LrgMidiSynth and #LrgMidiSequencer.
 */
G_GNUC_INTERNAL gboolean
lrg_midi_synth_parse_song (GBytes         *smf,
                           GArray        **out_events,
                           gdouble        *out_length_ms,
                           gdouble        *out_loop_start_ms,
                           gdouble        *out_loop_end_ms,
                           GError        **error);

G_END_DECLS
