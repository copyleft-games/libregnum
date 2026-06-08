/* lrg-reel-player.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelPlayer - interactive preview window for a #LrgReel composition.
 *
 * Opens a GPU window that plays back the reel in real time.  Everything else
 * in the Reel module is headless; this is the only type that requires a
 * display.  Instantiation is always safe; the window is only opened when
 * lrg_reel_player_run() is called.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_PLAYER (lrg_reel_player_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelPlayer, lrg_reel_player, LRG, REEL_PLAYER, GObject)

/**
 * lrg_reel_player_new:
 * @reel: (transfer none): the #LrgReel to preview.
 *
 * Creates a new player bound to @reel.  No window is opened at this point;
 * call lrg_reel_player_run() to open the window and start playback.
 *
 * The player holds its own reference to @reel.
 *
 * Returns: (transfer full): a new #LrgReelPlayer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelPlayer *
lrg_reel_player_new (LrgReel *reel);

/**
 * lrg_reel_player_run:
 * @self: an #LrgReelPlayer
 *
 * Opens a window sized to the reel's resolution, starts playback from the
 * current frame, and blocks until the window is closed.
 *
 * Space toggles play/pause; Left/Right arrow keys step one frame backward or
 * forward (with key-repeat).  The reel is letterbox-fitted into the window
 * with black bars if the window size does not match the reel's aspect ratio.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_run (LrgReelPlayer *self);

/**
 * lrg_reel_player_play:
 * @self: an #LrgReelPlayer
 *
 * Starts or resumes playback.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_play (LrgReelPlayer *self);

/**
 * lrg_reel_player_pause:
 * @self: an #LrgReelPlayer
 *
 * Pauses playback.  The current frame is retained.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_pause (LrgReelPlayer *self);

/**
 * lrg_reel_player_toggle:
 * @self: an #LrgReelPlayer
 *
 * Toggles between playing and paused states.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_toggle (LrgReelPlayer *self);

/**
 * lrg_reel_player_seek:
 * @self: an #LrgReelPlayer
 * @frame: the target frame number.
 *
 * Seeks to @frame, clamped to the range [0, duration - 1].
 * The frame accumulator is reset to zero on seek.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_seek (LrgReelPlayer *self,
                      gint           frame);

/**
 * lrg_reel_player_set_loop:
 * @self: an #LrgReelPlayer
 * @loop: %TRUE to loop, %FALSE to stop at the last frame.
 *
 * Sets whether the player loops back to frame 0 when the last frame is
 * reached.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_player_set_loop (LrgReelPlayer *self,
                           gboolean       loop);

/**
 * lrg_reel_player_get_loop:
 * @self: an #LrgReelPlayer
 *
 * Returns: %TRUE if looping is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_player_get_loop (LrgReelPlayer *self);

/**
 * lrg_reel_player_get_current_frame:
 * @self: an #LrgReelPlayer
 *
 * Returns: the current playback frame (0-based).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_player_get_current_frame (LrgReelPlayer *self);

/**
 * lrg_reel_player_get_playing:
 * @self: an #LrgReelPlayer
 *
 * Returns: %TRUE if the player is currently playing (not paused or stopped).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_player_get_playing (LrgReelPlayer *self);

G_END_DECLS
