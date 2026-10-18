/* lrg-midi-sequencer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Plays Standard MIDI Files into an #LrgMidiOutput in real time: the
 * counterpart of #LrgMidiSynth for external synthesizers.  The same
 * parser reads the songs (tempo map, "loopStart" / "loopEnd" markers),
 * the game moves the clock with lrg_midi_sequencer_advance() every frame,
 * and the events due go out; a look-ahead hands upcoming events to the
 * output with their delay, so an output with its own queue (ALSA) keeps
 * the timing exact however long a frame takes.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-midi-output.h"

G_BEGIN_DECLS

/**
 * LRG_MIDI_SEQUENCER_MAX_LOOKAHEAD:
 *
 * The longest look-ahead in seconds.
 */
#define LRG_MIDI_SEQUENCER_MAX_LOOKAHEAD (1.0)

#define LRG_TYPE_MIDI_SEQUENCER (lrg_midi_sequencer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMidiSequencer, lrg_midi_sequencer, LRG, MIDI_SEQUENCER, GObject)

/**
 * lrg_midi_sequencer_new:
 * @output: (transfer none): where the events go
 *
 * Returns: (transfer full): a sequencer without a song, look-ahead 0
 */
LRG_AVAILABLE_IN_ALL
LrgMidiSequencer * lrg_midi_sequencer_new (LrgMidiOutput *output);

/**
 * lrg_midi_sequencer_get_output:
 * @self: an #LrgMidiSequencer
 *
 * Returns: (transfer none): the output
 */
LRG_AVAILABLE_IN_ALL
LrgMidiOutput * lrg_midi_sequencer_get_output (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_play:
 * @self: an #LrgMidiSequencer
 * @smf: a Standard MIDI File
 * @loop: loop between the markers (or the whole song)
 * @error: return location for a #GError
 *
 * Silences the current song (lrg_midi_output_all_notes_off()) and starts
 * @smf at its beginning.  A malformed file fails with
 * %LRG_MIDI_SYNTH_ERROR_INVALID and the current song plays on.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_sequencer_play (LrgMidiSequencer  *self,
                                  GBytes            *smf,
                                  gboolean           loop,
                                  GError           **error);

/**
 * lrg_midi_sequencer_stop:
 * @self: an #LrgMidiSequencer
 *
 * Drops the song and silences the output.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_sequencer_stop (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_advance:
 * @self: an #LrgMidiSequencer
 * @seconds: time since the last call (>= 0)
 *
 * Moves the song clock and sends every event due up to the clock plus
 * the look-ahead.  Events that came due during @seconds go out at once;
 * the ones within the look-ahead go with their delay.  A loop wraps
 * (sounding notes are released first); a song without a loop finishes
 * after its last event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_sequencer_advance (LrgMidiSequencer *self,
                                 gdouble           seconds);

/**
 * lrg_midi_sequencer_is_playing:
 * @self: an #LrgMidiSequencer
 *
 * Returns: %TRUE while a song plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_sequencer_is_playing (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_is_finished:
 * @self: an #LrgMidiSequencer
 *
 * Returns: %TRUE when a song without a loop reached its end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_sequencer_is_finished (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_get_position:
 * @self: an #LrgMidiSequencer
 *
 * Returns: the song clock in seconds (0 without a song)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_midi_sequencer_get_position (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_get_length:
 * @self: an #LrgMidiSequencer
 *
 * Returns: the song's length in seconds (0 without a song)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_midi_sequencer_get_length (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_get_loop_points:
 * @self: an #LrgMidiSequencer
 * @loop_start: (out) (optional): where the loop starts (seconds)
 * @loop_end: (out) (optional): where it ends
 *
 * Returns: %TRUE when the song plays with a loop (both 0 otherwise)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_midi_sequencer_get_loop_points (LrgMidiSequencer *self,
                                             gdouble          *loop_start,
                                             gdouble          *loop_end);

/**
 * lrg_midi_sequencer_set_volume:
 * @self: an #LrgMidiSequencer
 * @volume: 0 .. 1
 *
 * Scales every channel's volume controller (CC 7) the song sends, and
 * resends the scaled value on the channels the song uses.
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_sequencer_set_volume (LrgMidiSequencer *self,
                                    gfloat            volume);

/**
 * lrg_midi_sequencer_get_volume:
 * @self: an #LrgMidiSequencer
 *
 * Returns: the volume
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_midi_sequencer_get_volume (LrgMidiSequencer *self);

/**
 * lrg_midi_sequencer_set_lookahead:
 * @self: an #LrgMidiSequencer
 * @seconds: 0 .. %LRG_MIDI_SEQUENCER_MAX_LOOKAHEAD
 */
LRG_AVAILABLE_IN_ALL
void lrg_midi_sequencer_set_lookahead (LrgMidiSequencer *self,
                                       gdouble           seconds);

/**
 * lrg_midi_sequencer_get_lookahead:
 * @self: an #LrgMidiSequencer
 *
 * Returns: the look-ahead in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_midi_sequencer_get_lookahead (LrgMidiSequencer *self);

G_END_DECLS
