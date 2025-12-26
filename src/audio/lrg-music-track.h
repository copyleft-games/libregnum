/* lrg-music-track.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Represents a music track with optional loop points.
 *
 * LrgMusicTrack wraps a GrlMusic object and adds game-specific
 * features like custom loop points, crossfading support, and
 * metadata storage.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_MUSIC_TRACK (lrg_music_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMusicTrack, lrg_music_track, LRG, MUSIC_TRACK, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_music_track_new:
 * @music: the underlying GrlMusic
 *
 * Creates a new music track wrapping the given GrlMusic.
 *
 * Returns: (transfer full): A new #LrgMusicTrack
 */
LRG_AVAILABLE_IN_ALL
LrgMusicTrack * lrg_music_track_new (GrlMusic *music);

/**
 * lrg_music_track_new_from_file:
 * @path: path to the music file
 * @error: (optional): return location for a #GError
 *
 * Loads a music track from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgMusicTrack, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgMusicTrack * lrg_music_track_new_from_file (const gchar  *path,
                                                GError      **error);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_music_track_get_music:
 * @self: a #LrgMusicTrack
 *
 * Gets the underlying GrlMusic.
 *
 * Returns: (transfer none): the GrlMusic
 */
LRG_AVAILABLE_IN_ALL
GrlMusic * lrg_music_track_get_music (LrgMusicTrack *self);

/**
 * lrg_music_track_get_name:
 * @self: a #LrgMusicTrack
 *
 * Gets the track name.
 *
 * Returns: (transfer none) (nullable): the track name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_music_track_get_name (LrgMusicTrack *self);

/**
 * lrg_music_track_set_name:
 * @self: a #LrgMusicTrack
 * @name: (nullable): the track name
 *
 * Sets the track name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_name (LrgMusicTrack *self,
                                const gchar   *name);

/* ==========================================================================
 * Loop Points
 * ========================================================================== */

/**
 * lrg_music_track_set_loop_points:
 * @self: a #LrgMusicTrack
 * @start: loop start in seconds (negative means beginning)
 * @end: loop end in seconds (negative means end of track)
 *
 * Sets custom loop points for the music.
 *
 * When the music reaches the end point, it will seek back to
 * the start point instead of looping to the beginning.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_loop_points (LrgMusicTrack *self,
                                       gfloat         start,
                                       gfloat         end);

/**
 * lrg_music_track_get_loop_start:
 * @self: a #LrgMusicTrack
 *
 * Gets the loop start point.
 *
 * Returns: the loop start in seconds (-1 means beginning)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_loop_start (LrgMusicTrack *self);

/**
 * lrg_music_track_get_loop_end:
 * @self: a #LrgMusicTrack
 *
 * Gets the loop end point.
 *
 * Returns: the loop end in seconds (-1 means end of track)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_loop_end (LrgMusicTrack *self);

/**
 * lrg_music_track_clear_loop_points:
 * @self: a #LrgMusicTrack
 *
 * Clears custom loop points, reverting to default looping.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_clear_loop_points (LrgMusicTrack *self);

/**
 * lrg_music_track_has_loop_points:
 * @self: a #LrgMusicTrack
 *
 * Checks if custom loop points are set.
 *
 * Returns: %TRUE if custom loop points are set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_music_track_has_loop_points (LrgMusicTrack *self);

/* ==========================================================================
 * Playback Control
 * ========================================================================== */

/**
 * lrg_music_track_play:
 * @self: a #LrgMusicTrack
 *
 * Starts playing the music track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_play (LrgMusicTrack *self);

/**
 * lrg_music_track_stop:
 * @self: a #LrgMusicTrack
 *
 * Stops the music track and resets to the beginning.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_stop (LrgMusicTrack *self);

/**
 * lrg_music_track_pause:
 * @self: a #LrgMusicTrack
 *
 * Pauses the music track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_pause (LrgMusicTrack *self);

/**
 * lrg_music_track_resume:
 * @self: a #LrgMusicTrack
 *
 * Resumes a paused music track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_resume (LrgMusicTrack *self);

/**
 * lrg_music_track_update:
 * @self: a #LrgMusicTrack
 *
 * Updates the music stream and handles loop point checking.
 *
 * This must be called every frame while music is playing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_update (LrgMusicTrack *self);

/**
 * lrg_music_track_is_playing:
 * @self: a #LrgMusicTrack
 *
 * Checks if the music track is currently playing.
 *
 * Returns: %TRUE if playing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_music_track_is_playing (LrgMusicTrack *self);

/* ==========================================================================
 * Looping
 * ========================================================================== */

/**
 * lrg_music_track_set_looping:
 * @self: a #LrgMusicTrack
 * @looping: whether to loop
 *
 * Sets whether the music should loop.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_looping (LrgMusicTrack *self,
                                   gboolean       looping);

/**
 * lrg_music_track_get_looping:
 * @self: a #LrgMusicTrack
 *
 * Gets whether the music loops.
 *
 * Returns: %TRUE if looping
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_music_track_get_looping (LrgMusicTrack *self);

/* ==========================================================================
 * Position and Duration
 * ========================================================================== */

/**
 * lrg_music_track_seek:
 * @self: a #LrgMusicTrack
 * @position: position in seconds
 *
 * Seeks to a position in the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_seek (LrgMusicTrack *self,
                            gfloat         position);

/**
 * lrg_music_track_get_position:
 * @self: a #LrgMusicTrack
 *
 * Gets the current playback position.
 *
 * Returns: position in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_position (LrgMusicTrack *self);

/**
 * lrg_music_track_get_duration:
 * @self: a #LrgMusicTrack
 *
 * Gets the total track duration.
 *
 * Returns: duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_duration (LrgMusicTrack *self);

/* ==========================================================================
 * Volume and Effects
 * ========================================================================== */

/**
 * lrg_music_track_set_volume:
 * @self: a #LrgMusicTrack
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the track volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_volume (LrgMusicTrack *self,
                                  gfloat         volume);

/**
 * lrg_music_track_get_volume:
 * @self: a #LrgMusicTrack
 *
 * Gets the track volume.
 *
 * Returns: volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_volume (LrgMusicTrack *self);

/**
 * lrg_music_track_set_pitch:
 * @self: a #LrgMusicTrack
 * @pitch: pitch multiplier (1.0 = normal)
 *
 * Sets the track pitch.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_pitch (LrgMusicTrack *self,
                                 gfloat         pitch);

/**
 * lrg_music_track_get_pitch:
 * @self: a #LrgMusicTrack
 *
 * Gets the track pitch.
 *
 * Returns: pitch multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_pitch (LrgMusicTrack *self);

/* ==========================================================================
 * Crossfade Support
 * ========================================================================== */

/**
 * lrg_music_track_set_fade_in:
 * @self: a #LrgMusicTrack
 * @duration: fade-in duration in seconds
 *
 * Sets the fade-in duration when starting the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_fade_in (LrgMusicTrack *self,
                                   gfloat         duration);

/**
 * lrg_music_track_get_fade_in:
 * @self: a #LrgMusicTrack
 *
 * Gets the fade-in duration.
 *
 * Returns: fade-in duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_fade_in (LrgMusicTrack *self);

/**
 * lrg_music_track_set_fade_out:
 * @self: a #LrgMusicTrack
 * @duration: fade-out duration in seconds
 *
 * Sets the fade-out duration when stopping the track.
 */
LRG_AVAILABLE_IN_ALL
void lrg_music_track_set_fade_out (LrgMusicTrack *self,
                                    gfloat         duration);

/**
 * lrg_music_track_get_fade_out:
 * @self: a #LrgMusicTrack
 *
 * Gets the fade-out duration.
 *
 * Returns: fade-out duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_music_track_get_fade_out (LrgMusicTrack *self);

G_END_DECLS
