/* lrg-net-peer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Represents a connected peer in the network.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_NET_PEER (lrg_net_peer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNetPeer, lrg_net_peer, LRG, NET_PEER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_net_peer_new:
 * @peer_id: unique peer identifier
 * @address: network address (IP or hostname)
 * @port: port number
 *
 * Creates a new network peer.
 *
 * Returns: (transfer full): A new #LrgNetPeer
 */
LRG_AVAILABLE_IN_ALL
LrgNetPeer * lrg_net_peer_new (guint32      peer_id,
                               const gchar *address,
                               guint        port);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_net_peer_get_peer_id:
 * @self: an #LrgNetPeer
 *
 * Gets the unique peer identifier.
 *
 * Returns: The peer ID
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_net_peer_get_peer_id (LrgNetPeer *self);

/**
 * lrg_net_peer_get_address:
 * @self: an #LrgNetPeer
 *
 * Gets the network address.
 *
 * Returns: (transfer none): The address string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_net_peer_get_address (LrgNetPeer *self);

/**
 * lrg_net_peer_get_port:
 * @self: an #LrgNetPeer
 *
 * Gets the port number.
 *
 * Returns: The port number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_peer_get_port (LrgNetPeer *self);

/**
 * lrg_net_peer_get_state:
 * @self: an #LrgNetPeer
 *
 * Gets the current connection state.
 *
 * Returns: The peer state
 */
LRG_AVAILABLE_IN_ALL
LrgNetPeerState lrg_net_peer_get_state (LrgNetPeer *self);

/**
 * lrg_net_peer_get_rtt:
 * @self: an #LrgNetPeer
 *
 * Gets the round-trip time in milliseconds.
 *
 * Returns: The RTT in ms, or 0 if unknown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_peer_get_rtt (LrgNetPeer *self);

/**
 * lrg_net_peer_get_last_activity:
 * @self: an #LrgNetPeer
 *
 * Gets the timestamp of last activity (microseconds since epoch).
 *
 * Returns: The last activity timestamp
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_net_peer_get_last_activity (LrgNetPeer *self);

/* ==========================================================================
 * State Methods
 * ========================================================================== */

/**
 * lrg_net_peer_is_connected:
 * @self: an #LrgNetPeer
 *
 * Checks if the peer is currently connected.
 *
 * Returns: %TRUE if connected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_peer_is_connected (LrgNetPeer *self);

/**
 * lrg_net_peer_set_state:
 * @self: an #LrgNetPeer
 * @state: the new state
 *
 * Sets the connection state.
 *
 * This is intended for internal use by LrgNetServer/LrgNetClient.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_peer_set_state (LrgNetPeer      *self,
                             LrgNetPeerState  state);

/**
 * lrg_net_peer_update_rtt:
 * @self: an #LrgNetPeer
 * @rtt_ms: round-trip time in milliseconds
 *
 * Updates the RTT measurement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_peer_update_rtt (LrgNetPeer *self,
                              guint       rtt_ms);

/**
 * lrg_net_peer_touch:
 * @self: an #LrgNetPeer
 *
 * Updates the last activity timestamp to now.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_peer_touch (LrgNetPeer *self);

/* ==========================================================================
 * Signals
 * ========================================================================== */

/**
 * LrgNetPeer::state-changed:
 * @self: the #LrgNetPeer
 * @old_state: the previous state
 * @new_state: the new state
 *
 * Emitted when the peer's connection state changes.
 */

G_END_DECLS
