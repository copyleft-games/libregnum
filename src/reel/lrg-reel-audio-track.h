/* lrg-reel-audio-track.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelAudioTrack - audio timeline and offline mixer for a Reel composition.
 *
 * An audio track holds a list of timed audio clips, each referencing an
 * #LrgWaveData source, a start frame, a volume, and optional trim boundaries.
 * lrg_reel_audio_track_mix() renders all clips into a single #LrgWaveData at
 * the requested output format by accumulating resampled, volume-scaled samples
 * into a float accumulation buffer and clamping to [-1, 1].
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_AUDIO_TRACK (lrg_reel_audio_track_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelAudioTrack, lrg_reel_audio_track, LRG, REEL_AUDIO_TRACK, GObject)

/**
 * lrg_reel_audio_track_new:
 * @fps: the frame rate of the owning composition (frames per second, > 0).
 *
 * Creates a new, empty audio track.
 *
 * Returns: (transfer full): a new #LrgReelAudioTrack
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelAudioTrack *
lrg_reel_audio_track_new (gdouble fps);

/**
 * lrg_reel_audio_track_add:
 * @self: a #LrgReelAudioTrack
 * @wave: (transfer none): the wave data to place on the track
 * @from_frame: composition frame at which the clip starts
 * @volume: linear volume scalar (1.0 = unity gain)
 * @trim_start_sec: seconds into @wave at which playback begins (>= 0)
 * @trim_end_sec: seconds into @wave at which playback ends; pass <= 0 to use
 *     the full duration from @trim_start_sec to the end of the wave
 *
 * Adds an audio clip to the track.  @wave is referenced internally and will be
 * released when the track is finalized.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_audio_track_add (LrgReelAudioTrack *self,
                           LrgWaveData       *wave,
                           gint               from_frame,
                           gdouble            volume,
                           gdouble            trim_start_sec,
                           gdouble            trim_end_sec);

/**
 * lrg_reel_audio_track_add_from_file:
 * @self: a #LrgReelAudioTrack
 * @path: (type filename): path to an audio file (wav/ogg/mp3/flac).
 * @from_frame: composition frame at which the clip starts.
 * @volume: linear volume scalar.
 * @trim_start_sec: seconds into the file at which playback begins.
 * @trim_end_sec: seconds at which playback ends (<= 0 for the full file).
 * @error: (nullable): return location for a #GError.
 *
 * Loads an audio file and adds it to the track.
 *
 * Returns: %TRUE on success, %FALSE if the file could not be loaded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_audio_track_add_from_file (LrgReelAudioTrack *self,
                                    const gchar       *path,
                                    gint               from_frame,
                                    gdouble            volume,
                                    gdouble            trim_start_sec,
                                    gdouble            trim_end_sec,
                                    GError           **error);

/**
 * lrg_reel_audio_track_mix:
 * @self: a #LrgReelAudioTrack
 * @sample_rate: output sample rate in Hz (must be > 0)
 * @channels: number of output channels (must be > 0)
 * @total_frames: total composition length in video frames (must be > 0)
 * @error: (nullable): return location for a #GError
 *
 * Renders all clips on the track into a single #LrgWaveData at the requested
 * format.  Each clip's wave is converted to @sample_rate / @channels as needed,
 * scaled by its volume, and accumulated at the correct temporal position.  The
 * final buffer is clamped to [-1.0, 1.0].
 *
 * On invalid arguments @error is set using the %G_IO_ERROR domain and %NULL is
 * returned.
 *
 * Returns: (transfer full) (nullable): a new #LrgWaveData, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData *
lrg_reel_audio_track_mix (LrgReelAudioTrack *self,
                           guint              sample_rate,
                           guint              channels,
                           guint              total_frames,
                           GError           **error);

/**
 * lrg_reel_audio_track_get_n_clips:
 * @self: a #LrgReelAudioTrack
 *
 * Returns: the number of clips currently on the track
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reel_audio_track_get_n_clips (LrgReelAudioTrack *self);

/**
 * lrg_reel_audio_track_get_fps:
 * @self: a #LrgReelAudioTrack
 *
 * Returns: the frame rate of the owning composition in frames per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_audio_track_get_fps (LrgReelAudioTrack *self);

G_END_DECLS
