/* lrg-video-player.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-video-texture.h"
#include "lrg-video-subtitles.h"
#include "../lrg-enums.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_VIDEO_PLAYER (lrg_video_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVideoPlayer, lrg_video_player, LRG, VIDEO_PLAYER, GObject)

/**
 * lrg_video_player_new:
 *
 * Creates a new video player.
 *
 * Returns: (transfer full): A new #LrgVideoPlayer
 */
LRG_AVAILABLE_IN_ALL
LrgVideoPlayer *lrg_video_player_new (void);

/**
 * lrg_video_player_open:
 * @player: an #LrgVideoPlayer
 * @path: path to video file
 * @error: (nullable): return location for error
 *
 * Opens a video file for playback.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_open (LrgVideoPlayer  *player,
                                const gchar     *path,
                                GError         **error);

/**
 * lrg_video_player_close:
 * @player: an #LrgVideoPlayer
 *
 * Closes the currently open video.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_close (LrgVideoPlayer *player);

/**
 * lrg_video_player_play:
 * @player: an #LrgVideoPlayer
 *
 * Starts or resumes playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_play (LrgVideoPlayer *player);

/**
 * lrg_video_player_pause:
 * @player: an #LrgVideoPlayer
 *
 * Pauses playback.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_pause (LrgVideoPlayer *player);

/**
 * lrg_video_player_stop:
 * @player: an #LrgVideoPlayer
 *
 * Stops playback and resets to the beginning.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_stop (LrgVideoPlayer *player);

/**
 * lrg_video_player_seek:
 * @player: an #LrgVideoPlayer
 * @position: position in seconds
 *
 * Seeks to the specified position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_seek (LrgVideoPlayer *player,
                            gdouble         position);

/**
 * lrg_video_player_get_state:
 * @player: an #LrgVideoPlayer
 *
 * Gets the current playback state.
 *
 * Returns: The current state
 */
LRG_AVAILABLE_IN_ALL
LrgVideoState lrg_video_player_get_state (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_position:
 * @player: an #LrgVideoPlayer
 *
 * Gets the current playback position.
 *
 * Returns: Position in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_video_player_get_position (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_duration:
 * @player: an #LrgVideoPlayer
 *
 * Gets the video duration.
 *
 * Returns: Duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_video_player_get_duration (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_width:
 * @player: an #LrgVideoPlayer
 *
 * Gets the video width.
 *
 * Returns: Width in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_player_get_width (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_height:
 * @player: an #LrgVideoPlayer
 *
 * Gets the video height.
 *
 * Returns: Height in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_player_get_height (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_frame_rate:
 * @player: an #LrgVideoPlayer
 *
 * Gets the video frame rate.
 *
 * Returns: Frames per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_video_player_get_frame_rate (LrgVideoPlayer *player);

/**
 * lrg_video_player_set_volume:
 * @player: an #LrgVideoPlayer
 * @volume: volume level (0.0 to 1.0)
 *
 * Sets the audio volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_set_volume (LrgVideoPlayer *player,
                                  gfloat          volume);

/**
 * lrg_video_player_get_volume:
 * @player: an #LrgVideoPlayer
 *
 * Gets the audio volume.
 *
 * Returns: Volume level (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_video_player_get_volume (LrgVideoPlayer *player);

/**
 * lrg_video_player_set_muted:
 * @player: an #LrgVideoPlayer
 * @muted: whether audio should be muted
 *
 * Sets the mute state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_set_muted (LrgVideoPlayer *player,
                                 gboolean        muted);

/**
 * lrg_video_player_get_muted:
 * @player: an #LrgVideoPlayer
 *
 * Gets the mute state.
 *
 * Returns: %TRUE if muted
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_get_muted (LrgVideoPlayer *player);

/**
 * lrg_video_player_set_loop:
 * @player: an #LrgVideoPlayer
 * @loop: whether to loop playback
 *
 * Sets the loop mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_set_loop (LrgVideoPlayer *player,
                                gboolean        loop);

/**
 * lrg_video_player_get_loop:
 * @player: an #LrgVideoPlayer
 *
 * Gets the loop mode.
 *
 * Returns: %TRUE if looping
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_get_loop (LrgVideoPlayer *player);

/**
 * lrg_video_player_set_playback_rate:
 * @player: an #LrgVideoPlayer
 * @rate: playback rate (1.0 = normal)
 *
 * Sets the playback rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_set_playback_rate (LrgVideoPlayer *player,
                                         gfloat          rate);

/**
 * lrg_video_player_get_playback_rate:
 * @player: an #LrgVideoPlayer
 *
 * Gets the playback rate.
 *
 * Returns: Playback rate
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_video_player_get_playback_rate (LrgVideoPlayer *player);

/**
 * lrg_video_player_has_audio:
 * @player: an #LrgVideoPlayer
 *
 * Checks whether the open video has an audio stream that libregnum can
 * decode.  Audio is best effort: a file whose audio codec is missing or
 * broken still opens and plays its video, it just reports no audio.
 * Always %FALSE when libregnum was built without FFmpeg.
 *
 * Returns: %TRUE if decoded PCM can be read with
 *   lrg_video_player_read_audio()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_has_audio (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_audio_sample_rate:
 * @player: an #LrgVideoPlayer
 *
 * Gets the sample rate of the PCM the player delivers, which is the audio
 * stream's native rate (no resampling is done; feed it to an audio stream
 * opened at this rate).
 *
 * Returns: the sample rate in Hz, or 0 when there is no audio
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_player_get_audio_sample_rate (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_audio_channels:
 * @player: an #LrgVideoPlayer
 *
 * Gets the channel count of the PCM the player delivers: 1 for a mono
 * stream, 2 for stereo (streams with more channels are folded down to
 * stereo).
 *
 * Returns: 1 or 2, or 0 when there is no audio
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_player_get_audio_channels (LrgVideoPlayer *player);

/**
 * lrg_video_player_set_audio_enabled:
 * @player: an #LrgVideoPlayer
 * @enabled: whether to decode and buffer the audio stream
 *
 * Enables or disables the audio path.  While disabled (the default) the
 * player drops audio packets as it demuxes them, so a player nobody pulls
 * audio from never decodes or buffers PCM.  Disabling discards buffered
 * audio.  The setting survives lrg_video_player_open() and
 * lrg_video_player_close().
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_set_audio_enabled (LrgVideoPlayer *player,
                                         gboolean        enabled);

/**
 * lrg_video_player_get_audio_enabled:
 * @player: an #LrgVideoPlayer
 *
 * Gets whether the audio path is enabled.
 *
 * Returns: %TRUE if audio is decoded for lrg_video_player_read_audio()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_get_audio_enabled (LrgVideoPlayer *player);

/**
 * lrg_video_player_read_audio: (skip)
 * @player: an #LrgVideoPlayer
 * @samples: (out caller-allocates): buffer of at least @n_frames times
 *   lrg_video_player_get_audio_channels() samples
 * @n_frames: maximum number of frames to write
 *
 * Pulls decoded audio as interleaved signed 16-bit PCM at
 * lrg_video_player_get_audio_sample_rate() with
 * lrg_video_player_get_audio_channels() channels, for the host to play
 * through its own audio output.
 *
 * Audio is delivered only while the player is playing (paused, stopped and
 * finished players deliver nothing) and only when audio is enabled.  The
 * player's volume is applied; while muted the frames are consumed but
 * written as silence, so audio stays in sync.  Delivery is kept in step with
 * the playback position: never more than a quarter second or so ahead of
 * lrg_video_player_get_position(), and audio that fell more than 0.2 s
 * behind it (the host pulled too slowly, or the position jumped) is dropped
 * to catch up.  Seeking, stopping, looping and replaying a finished video
 * restart the audio with the video.
 *
 * Call it from the thread that calls lrg_video_player_update() (typically
 * the host's update tick), never from an audio callback thread.
 *
 * Returns: the number of frames written (0 when nothing is due)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_player_read_audio (LrgVideoPlayer *player,
                                   gint16         *samples,
                                   guint           n_frames);

/**
 * lrg_video_player_read_audio_bytes: (rename-to lrg_video_player_read_audio)
 * @player: an #LrgVideoPlayer
 * @n_frames: maximum number of frames to read
 *
 * Binding-friendly variant of lrg_video_player_read_audio(): the same PCM
 * (interleaved native-endian signed 16-bit samples) in a #GBytes whose
 * size is the number of frames read times channels times 2.
 *
 * Returns: (transfer full): the PCM read, possibly empty
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_video_player_read_audio_bytes (LrgVideoPlayer *player,
                                           guint           n_frames);

/**
 * lrg_video_player_get_audio_time:
 * @player: an #LrgVideoPlayer
 *
 * Gets the presentation time of the next frame
 * lrg_video_player_read_audio() would deliver.  It may demux ahead to find
 * out.  Compare it with lrg_video_player_get_position() to measure how far
 * the host's audio runs ahead of (or behind) the picture.
 *
 * Returns: the time in seconds, or -1.0 when unknown (no audio, audio
 *   disabled, nothing left to decode)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_video_player_get_audio_time (LrgVideoPlayer *player);

/**
 * lrg_video_player_update:
 * @player: an #LrgVideoPlayer
 * @delta_time: time since last update in seconds
 *
 * Updates the video player, advancing the playback position
 * and decoding new frames as needed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_update (LrgVideoPlayer *player,
                              gfloat          delta_time);

/**
 * lrg_video_player_get_texture:
 * @player: an #LrgVideoPlayer
 *
 * Gets the video texture for rendering.
 *
 * Returns: (transfer none) (nullable): The video texture
 */
LRG_AVAILABLE_IN_ALL
LrgVideoTexture *lrg_video_player_get_texture (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_subtitles:
 * @player: an #LrgVideoPlayer
 *
 * Gets the subtitle renderer.
 *
 * Returns: (transfer none): The subtitle renderer
 */
LRG_AVAILABLE_IN_ALL
LrgVideoSubtitles *lrg_video_player_get_subtitles (LrgVideoPlayer *player);

/**
 * lrg_video_player_load_subtitles:
 * @player: an #LrgVideoPlayer
 * @path: path to subtitle file
 * @error: (nullable): return location for error
 *
 * Loads subtitles from a file (SRT or VTT format).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_load_subtitles (LrgVideoPlayer  *player,
                                          const gchar     *path,
                                          GError         **error);

/**
 * lrg_video_player_draw:
 * @player: an #LrgVideoPlayer
 * @x: x position
 * @y: y position
 * @width: draw width
 * @height: draw height
 *
 * Draws the current video frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_player_draw (LrgVideoPlayer *player,
                            gint            x,
                            gint            y,
                            gint            width,
                            gint            height);

/**
 * lrg_video_player_is_open:
 * @player: an #LrgVideoPlayer
 *
 * Checks if a video is currently open.
 *
 * Returns: %TRUE if a video is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_player_is_open (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_error:
 * @player: an #LrgVideoPlayer
 *
 * Gets the last error that occurred.
 *
 * Returns: The error type
 */
LRG_AVAILABLE_IN_ALL
LrgVideoError lrg_video_player_get_error (LrgVideoPlayer *player);

/**
 * lrg_video_player_get_error_message:
 * @player: an #LrgVideoPlayer
 *
 * Gets the last error message.
 *
 * Returns: (transfer none) (nullable): The error message
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_video_player_get_error_message (LrgVideoPlayer *player);

G_END_DECLS
