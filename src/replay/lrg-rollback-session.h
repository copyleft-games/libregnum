/* lrg-rollback-session.h - GGPO-style rollback netcode session
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-simulation.h"
#include "lrg-input-snapshot.h"
#include "lrg-rollback-transport.h"

G_BEGIN_DECLS

/**
 * LRG_ROLLBACK_MAX_FRAMES:
 *
 * The largest #LrgRollbackSession:max-rollback.
 *
 * Since: 0.4
 */
#define LRG_ROLLBACK_MAX_FRAMES (32)

/**
 * LRG_ROLLBACK_MAX_INPUT_DELAY:
 *
 * The largest #LrgRollbackSession:input-delay.
 *
 * Since: 0.4
 */
#define LRG_ROLLBACK_MAX_INPUT_DELAY (16)

#define LRG_TYPE_ROLLBACK_SESSION (lrg_rollback_session_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRollbackSession, lrg_rollback_session, LRG, ROLLBACK_SESSION, GObject)

/**
 * lrg_rollback_session_new:
 * @simulation: the local copy of the shared simulation; every peer creates
 *   one with the same seed, players, action table, objects and state
 * @transport: how peers are reached (by player index)
 * @local_player: the player index this peer controls
 *
 * Creates a peer-to-peer rollback session (GGPO style) for a non-MMO game
 * with up to %LRG_SIMULATION_MAX_PLAYERS players in a full mesh. Each
 * frame the local input is scheduled @input-delay frames ahead and sent
 * to every peer; remote inputs that have not arrived are predicted by
 * repeating the player's last confirmed input. When a real input differs
 * from its prediction the session loads the state saved before that frame
 * and re-simulates up to the present. Confirmed frames (all inputs known)
 * are checksummed every #LrgRollbackSession:checksum-interval frames and
 * the checksums are exchanged to detect desyncs.
 *
 * Session frame N is the Nth tick simulated by the session; the simulation
 * must be at the same tick on every peer when the session is created.
 *
 * Returns: (transfer full): a new #LrgRollbackSession
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
LrgRollbackSession *lrg_rollback_session_new (LrgSimulation        *simulation,
                                              LrgRollbackTransport *transport,
                                              guint                 local_player);

/**
 * lrg_rollback_session_get_simulation:
 * @self: an #LrgRollbackSession
 *
 * Returns: (transfer none): the simulation
 */
LRG_AVAILABLE_IN_ALL
LrgSimulation *lrg_rollback_session_get_simulation (LrgRollbackSession *self);

/**
 * lrg_rollback_session_set_input_delay:
 * @self: an #LrgRollbackSession
 * @frames: 0 to %LRG_ROLLBACK_MAX_INPUT_DELAY
 *
 * Sets how many frames after it is sampled a local input takes effect
 * (default 2). More delay means fewer rollbacks and more input lag. Every
 * peer must use the same delay, and it must be set before the first frame.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rollback_session_set_input_delay (LrgRollbackSession *self,
                                           guint               frames);

/**
 * lrg_rollback_session_get_input_delay:
 * @self: an #LrgRollbackSession
 *
 * Returns: the input delay in frames
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rollback_session_get_input_delay (LrgRollbackSession *self);

/**
 * lrg_rollback_session_set_max_rollback:
 * @self: an #LrgRollbackSession
 * @frames: 1 to %LRG_ROLLBACK_MAX_FRAMES
 *
 * Sets how many frames the session may run ahead of the last confirmed
 * frame (default 8). Past that, lrg_rollback_session_advance_frame() stalls
 * until remote input arrives, which bounds the re-simulation cost.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rollback_session_set_max_rollback (LrgRollbackSession *self,
                                            guint               frames);

/**
 * lrg_rollback_session_get_max_rollback:
 * @self: an #LrgRollbackSession
 *
 * Returns: the rollback window in frames
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rollback_session_get_max_rollback (LrgRollbackSession *self);

/**
 * lrg_rollback_session_set_checksum_interval:
 * @self: an #LrgRollbackSession
 * @frames: frames between exchanged checksums, 0 to turn desync
 *   detection off
 *
 * Sets the desync-detection cadence (default 10). Every peer must use the
 * same interval.
 */
LRG_AVAILABLE_IN_ALL
void lrg_rollback_session_set_checksum_interval (LrgRollbackSession *self,
                                                 guint               frames);

/**
 * lrg_rollback_session_advance_frame:
 * @self: an #LrgRollbackSession
 * @local_input: this peer's input sampled for this frame
 * @error: (nullable): return location for an error
 *
 * Processes received packets (rolling back and re-simulating when a
 * prediction was wrong), then, unless the session is too far ahead of the
 * last confirmed frame, schedules @local_input, simulates one frame and
 * sends the local inputs the peers have not acknowledged.
 *
 * Returns: %TRUE if a frame was simulated; %FALSE without @error when the
 *   session stalled waiting for remote input (call again next frame with
 *   the same input); %FALSE with @error if a saved state could not be
 *   restored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rollback_session_advance_frame (LrgRollbackSession      *self,
                                             const LrgInputSnapshot  *local_input,
                                             GError                 **error);

/**
 * lrg_rollback_session_poll:
 * @self: an #LrgRollbackSession
 * @error: (nullable): return location for an error
 *
 * Processes received packets and applies any rollback they require
 * without simulating a new frame, then re-sends unacknowledged input.
 * Call it while waiting (paused, stalled, or after the last frame) so
 * peers keep converging.
 *
 * Returns: %FALSE with @error if a saved state could not be restored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rollback_session_poll (LrgRollbackSession  *self,
                                    GError             **error);

/**
 * lrg_rollback_session_get_current_frame:
 * @self: an #LrgRollbackSession
 *
 * Returns: the number of frames simulated
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_rollback_session_get_current_frame (LrgRollbackSession *self);

/**
 * lrg_rollback_session_get_confirmed_frame:
 * @self: an #LrgRollbackSession
 *
 * Returns: the last frame for which every player's input is known (state
 *   up to and including it is final), or -1
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_rollback_session_get_confirmed_frame (LrgRollbackSession *self);

/**
 * lrg_rollback_session_get_frames_ahead:
 * @self: an #LrgRollbackSession
 *
 * How far this peer runs ahead of the slowest peer, from the frame numbers
 * peers report. A game that stays ahead by more than a frame or two should
 * skip a frame now and then so that both sides roll back equally.
 *
 * Returns: local frame minus the oldest remote frame heard of
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_rollback_session_get_frames_ahead (LrgRollbackSession *self);

/**
 * lrg_rollback_session_get_rollback_count:
 * @self: an #LrgRollbackSession
 *
 * Returns: the number of rollbacks performed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rollback_session_get_rollback_count (LrgRollbackSession *self);

/**
 * lrg_rollback_session_get_resimulated_frames:
 * @self: an #LrgRollbackSession
 *
 * Returns: the total number of frames simulated again after rollbacks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_rollback_session_get_resimulated_frames (LrgRollbackSession *self);

/**
 * lrg_rollback_session_lookup_checksum:
 * @self: an #LrgRollbackSession
 * @frame: a frame number (a multiple of the checksum interval)
 * @checksum: (out) (optional): return location for the checksum
 *
 * Looks up the local checksum of the confirmed state after @frame frames.
 *
 * Returns: %TRUE if it is known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_rollback_session_lookup_checksum (LrgRollbackSession *self,
                                               gint64              frame,
                                               guint64            *checksum);

/**
 * lrg_rollback_session_get_last_verified_frame:
 * @self: an #LrgRollbackSession
 *
 * Returns: the last frame whose checksum matched a peer's, or -1
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_rollback_session_get_last_verified_frame (LrgRollbackSession *self);

/**
 * lrg_rollback_session_get_desync_frame:
 * @self: an #LrgRollbackSession
 *
 * Returns: the first frame whose checksum differed from a peer's, or -1
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_rollback_session_get_desync_frame (LrgRollbackSession *self);

G_END_DECLS
