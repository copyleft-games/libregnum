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
