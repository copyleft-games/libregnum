/* lrg-midi-file.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standard MIDI Files and songs: import a .mid as an #LrgSong (one
 * pattern, instrument and track per MIDI channel; the tempo map, time
 * signature and markers), and export a song's notes as a type-1 .mid for
 * other music software.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-song.h"

G_BEGIN_DECLS

/**
 * lrg_song_new_from_midi:
 * @smf: a Standard MIDI File (format 0 or 1, ticks per quarter note)
 * @error: (nullable): return location for an error
 *
 * Builds a song from a MIDI file.  Every channel with notes becomes a
 * pattern holding all its notes, an instrument (channel 10: an 808 drum
 * machine; others: the synthesizer preset closest to the channel's
 * General MIDI program) and a track playing the pattern once from beat 0.
 * The first tempo is #LrgSong:bpm and later ones are tempo changes; the
 * first time signature and every marker are kept.
 *
 * Returns: (transfer full) (nullable): the song, or %NULL with
 *   %LRG_SONG_ERROR_PARSE for a file that is not a valid SMF
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_new_from_midi (GBytes  *smf,
                                  GError **error);

/**
 * lrg_song_new_from_midi_file:
 * @path: a .mid file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the song
 */
LRG_AVAILABLE_IN_ALL
LrgSong * lrg_song_new_from_midi_file (const gchar  *path,
                                       GError      **error);

/**
 * lrg_song_to_midi:
 * @self: an #LrgSong
 * @error: (nullable): return location for an error
 *
 * Writes the notes of every instrument track as a type-1 Standard MIDI
 * File at 480 ticks per quarter note: a tempo track (tempo map, ramps in
 * steps of a sixteenth, time signature, markers, title) and one track per
 * song track, each on its own channel (drum machines on channel 10) with
 * a General MIDI program near its synthesizer preset.  Audio tracks,
 * automation and the mixer are not part of a MIDI file.
 *
 * Returns: (transfer full) (nullable): the file's bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes * lrg_song_to_midi (LrgSong  *self,
                           GError  **error);

/**
 * lrg_song_save_midi:
 * @self: an #LrgSong
 * @path: the .mid file to write
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_save_midi (LrgSong      *self,
                             const gchar  *path,
                             GError      **error);

G_END_DECLS
