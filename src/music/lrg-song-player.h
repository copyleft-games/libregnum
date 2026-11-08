/* lrg-song-player.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Realtime playback of an #LrgSong: a render thread fills a short ring
 * buffer that the audio device reads, so a song plays in a game or the
 * editor while the main thread keeps running.  Transport (play, pause,
 * seek, loop), live edits (a changed song swaps in at the same beat),
 * live notes, and adaptive game music: jump to a section on the next beat
 * or bar, fade tracks in and out as layers, fire a stinger pattern.
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
 * LRG_SONG_PLAYER_RING_FRAMES:
 *
 * The frames the render thread keeps ahead of the device (about 85 ms at
 * 48 kHz): the latency of live edits and notes.
 */
#define LRG_SONG_PLAYER_RING_FRAMES (4096)

/**
 * LrgSongQuantize:
 * @LRG_SONG_QUANTIZE_NOW: at once
 * @LRG_SONG_QUANTIZE_BEAT: on the next beat
 * @LRG_SONG_QUANTIZE_BAR: on the next bar line
 *
 * When an adaptive-music change takes effect.
 */
typedef enum
{
	LRG_SONG_QUANTIZE_NOW,
	LRG_SONG_QUANTIZE_BEAT,
	LRG_SONG_QUANTIZE_BAR
} LrgSongQuantize;

LRG_AVAILABLE_IN_ALL
GType lrg_song_quantize_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SONG_QUANTIZE (lrg_song_quantize_get_type ())

#define LRG_TYPE_SONG_PLAYER (lrg_song_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSongPlayer, lrg_song_player, LRG, SONG_PLAYER, GObject)

/**
 * lrg_song_player_new:
 * @song: the song (a copy is compiled; later edits need
 *   lrg_song_player_set_song())
 * @error: (nullable): return location for an error
 *
 * A stopped player at beat 0 with the song's loop range.  It renders
 * headless (lrg_song_player_render()) until lrg_song_player_start_output()
 * opens a device stream.
 *
 * Returns: (transfer full) (nullable): the player, or %NULL when the song
 *   does not compile (a missing instrument, file or bus)
 */
LRG_AVAILABLE_IN_ALL
LrgSongPlayer * lrg_song_player_new (LrgSong  *song,
                                     GError  **error);

/**
 * lrg_song_player_set_song:
 * @self: an #LrgSongPlayer
 * @song: the edited song
 * @error: (nullable): return location for an error
 *
 * Compiles @song (on the calling thread) and swaps it in at the beat the
 * player is at: playback goes on, notes held across the swap sound again
 * from it.
 *
 * Returns: %TRUE when the song compiled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_set_song (LrgSongPlayer  *self,
                                   LrgSong        *song,
                                   GError        **error);

/**
 * lrg_song_player_play:
 * @self: an #LrgSongPlayer
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_play (LrgSongPlayer *self);

/**
 * lrg_song_player_pause:
 * @self: an #LrgSongPlayer
 *
 * Stops at the current beat; held notes are released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_pause (LrgSongPlayer *self);

/**
 * lrg_song_player_stop:
 * @self: an #LrgSongPlayer
 *
 * Pauses and goes back to the loop start (or beat 0).
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_stop (LrgSongPlayer *self);

/**
 * lrg_song_player_is_playing:
 * @self: an #LrgSongPlayer
 *
 * Returns: whether it is playing (it stops by itself at the song's end
 *   when not looping)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_is_playing (LrgSongPlayer *self);

/**
 * lrg_song_player_seek:
 * @self: an #LrgSongPlayer
 * @beat: where to go
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_seek (LrgSongPlayer *self,
                           gdouble        beat);

/**
 * lrg_song_player_get_beat:
 * @self: an #LrgSongPlayer
 *
 * Returns: the beat being heard (with an output: what the device plays
 *   now, the ring's latency behind the render thread)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_song_player_get_beat (LrgSongPlayer *self);

/**
 * lrg_song_player_set_loop:
 * @self: an #LrgSongPlayer
 * @enabled: whether to loop
 * @start: the first beat of the loop
 * @end: the beat after it (above @start)
 *
 * Returns: %TRUE for a valid range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_set_loop (LrgSongPlayer *self,
                                   gboolean       enabled,
                                   gdouble        start,
                                   gdouble        end);

/**
 * lrg_song_player_start_output:
 * @self: an #LrgSongPlayer
 * @error: (nullable): return location for an error
 *
 * Opens a stream on the audio device (which the application opened, for
 * example through #LrgAudioManager) and starts the render thread.
 *
 * Returns: %TRUE when the output runs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_start_output (LrgSongPlayer  *self,
                                       GError        **error);

/**
 * lrg_song_player_stop_output:
 * @self: an #LrgSongPlayer
 *
 * Stops the render thread and closes the stream; the player can render
 * headless again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_stop_output (LrgSongPlayer *self);

/**
 * lrg_song_player_has_output:
 * @self: an #LrgSongPlayer
 *
 * Returns: whether a device stream is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_has_output (LrgSongPlayer *self);

/**
 * lrg_song_player_update:
 * @self: an #LrgSongPlayer
 *
 * Call once a frame on the main thread while an output runs: refills the
 * stream when the device has no callback support.
 */
LRG_AVAILABLE_IN_ALL
void lrg_song_player_update (LrgSongPlayer *self);

/**
 * lrg_song_player_render:
 * @self: an #LrgSongPlayer
 * @out: (array) (out caller-allocates): @frames x 2 interleaved samples
 * @frames: frames to render
 *
 * Renders the next @frames frames on the calling thread, exactly as the
 * render thread would (tests, previews, offline captures of adaptive
 * playback).  Not while an output runs.
 *
 * Returns: %TRUE when it rendered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_render (LrgSongPlayer *self,
                                 gfloat        *out,
                                 guint          frames);

/**
 * lrg_song_player_queue_section:
 * @self: an #LrgSongPlayer
 * @marker: a marker of the song ("Chorus")
 * @quantize: when to jump
 *
 * Jumps to the marker on the next beat or bar (game state changed: the
 * fight starts, play the chorus).
 *
 * Returns: %TRUE when the marker exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_queue_section (LrgSongPlayer   *self,
                                        const gchar     *marker,
                                        LrgSongQuantize  quantize);

/**
 * lrg_song_player_set_layer:
 * @self: an #LrgSongPlayer
 * @track: a track id
 * @gain_db: an extra level on the track (-144 silences)
 * @seconds: fade time
 *
 * Vertical layering: fades a track in or out on top of its own volume and
 * automation.
 *
 * Returns: %TRUE for a known, audible track
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_set_layer (LrgSongPlayer *self,
                                    const gchar   *track,
                                    gdouble        gain_db,
                                    gdouble        seconds);

/**
 * lrg_song_player_play_stinger:
 * @self: an #LrgSongPlayer
 * @track: an instrument track whose instrument plays it
 * @pattern: a pattern of the song
 * @quantize: when it starts
 *
 * Plays a pattern once over the music (a victory phrase, a hit accent).
 *
 * Returns: %TRUE for a known instrument track and pattern
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_play_stinger (LrgSongPlayer   *self,
                                       const gchar     *track,
                                       const gchar     *pattern,
                                       LrgSongQuantize  quantize);

/**
 * lrg_song_player_note_on:
 * @self: an #LrgSongPlayer
 * @track: an instrument track
 * @key: a MIDI key
 * @velocity: 0 .. 1
 *
 * Plays a note live on the track's instrument (auditioning, a keyboard).
 *
 * Returns: %TRUE for a known instrument track
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_note_on (LrgSongPlayer *self,
                                  const gchar   *track,
                                  gint           key,
                                  gdouble        velocity);

/**
 * lrg_song_player_note_off:
 * @self: an #LrgSongPlayer
 * @track: an instrument track
 * @key: a MIDI key
 *
 * Returns: %TRUE for a known instrument track
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_note_off (LrgSongPlayer *self,
                                   const gchar   *track,
                                   gint           key);

/**
 * lrg_song_player_get_meter:
 * @self: an #LrgSongPlayer
 * @bus: a bus name ("master")
 * @out_peak: (out) (optional): the peak of the last rendered block
 * @out_rms: (out) (optional): its RMS
 *
 * Returns: %TRUE for a known bus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_song_player_get_meter (LrgSongPlayer *self,
                                    const gchar   *bus,
                                    gdouble       *out_peak,
                                    gdouble       *out_rms);

/**
 * lrg_song_player_get_underruns:
 * @self: an #LrgSongPlayer
 *
 * Returns: how many times the device found the ring empty (and played
 *   silence) since the output started
 */
LRG_AVAILABLE_IN_ALL
guint lrg_song_player_get_underruns (LrgSongPlayer *self);

G_END_DECLS
