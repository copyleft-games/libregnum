/* lrg-net-server.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Network server for hosting multiplayer games.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#ifdef LRG_HAS_LIBDEX
#include <libdex.h>
#endif
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-net-message.h"
#include "lrg-net-peer.h"

G_BEGIN_DECLS

#define LRG_TYPE_NET_SERVER (lrg_net_server_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNetServer, lrg_net_server, LRG, NET_SERVER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_net_server_new:
 * @host: (nullable): bind address (NULL for all interfaces)
 * @port: listen port
 *
 * Creates a new network server.
 *
 * Returns: (transfer full): A new #LrgNetServer
 */
LRG_AVAILABLE_IN_ALL
LrgNetServer * lrg_net_server_new (const gchar *host,
                                   guint        port);

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

/**
 * lrg_net_server_start:
 * @self: an #LrgNetServer
 * @error: (nullable): return location for error
 *
 * Starts the server listening for connections.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_server_start (LrgNetServer  *self,
                               GError       **error);

/**
 * lrg_net_server_stop:
 * @self: an #LrgNetServer
 *
 * Stops the server and disconnects all peers.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_server_stop (LrgNetServer *self);

/**
 * lrg_net_server_is_running:
 * @self: an #LrgNetServer
 *
 * Checks if the server is running.
 *
 * Returns: %TRUE if running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_server_is_running (LrgNetServer *self);

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/**
 * lrg_net_server_get_host:
 * @self: an #LrgNetServer
 *
 * Gets the bind address.
 *
 * Returns: (transfer none) (nullable): The host address
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_net_server_get_host (LrgNetServer *self);

/**
 * lrg_net_server_get_port:
 * @self: an #LrgNetServer
 *
 * Gets the listen port.
 *
 * Returns: The port number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_server_get_port (LrgNetServer *self);

/**
 * lrg_net_server_get_max_peers:
 * @self: an #LrgNetServer
 *
 * Gets the maximum number of peers allowed.
 *
 * Returns: Max peers (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_server_get_max_peers (LrgNetServer *self);

/**
 * lrg_net_server_set_max_peers:
 * @self: an #LrgNetServer
 * @max_peers: maximum peers (0 = unlimited)
 *
 * Sets the maximum number of peers allowed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_server_set_max_peers (LrgNetServer *self,
                                   guint         max_peers);

/* ==========================================================================
 * Peer Management
 * ========================================================================== */

/**
 * lrg_net_server_get_peer:
 * @self: an #LrgNetServer
 * @peer_id: the peer identifier
 *
 * Gets a peer by ID.
 *
 * Returns: (transfer none) (nullable): The peer, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNetPeer * lrg_net_server_get_peer (LrgNetServer *self,
                                      guint32       peer_id);

/**
 * lrg_net_server_get_peers:
 * @self: an #LrgNetServer
 *
 * Gets a list of all connected peers.
 *
 * Returns: (transfer container) (element-type LrgNetPeer): List of peers
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_net_server_get_peers (LrgNetServer *self);

/**
 * lrg_net_server_get_peer_count:
 * @self: an #LrgNetServer
 *
 * Gets the number of connected peers.
 *
 * Returns: The peer count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_server_get_peer_count (LrgNetServer *self);

/**
 * lrg_net_server_disconnect_peer:
 * @self: an #LrgNetServer
 * @peer_id: the peer to disconnect
 *
 * Disconnects a specific peer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_server_disconnect_peer (LrgNetServer *self,
                                     guint32       peer_id);

/**
 * lrg_net_server_disconnect_all:
 * @self: an #LrgNetServer
 *
 * Disconnects all peers.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_server_disconnect_all (LrgNetServer *self);

/* ==========================================================================
 * Messaging
 * ========================================================================== */

/**
 * lrg_net_server_send:
 * @self: an #LrgNetServer
 * @peer_id: the recipient peer ID
 * @message: the message to send
 * @error: (nullable): return location for error
 *
 * Sends a message to a specific peer.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_server_send (LrgNetServer   *self,
                              guint32         peer_id,
                              LrgNetMessage  *message,
                              GError        **error);

/**
 * lrg_net_server_broadcast:
 * @self: an #LrgNetServer
 * @message: the message to broadcast
 * @error: (nullable): return location for error
 *
 * Sends a message to all connected peers.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_server_broadcast (LrgNetServer   *self,
                                   LrgNetMessage  *message,
                                   GError        **error);

#ifdef LRG_HAS_LIBDEX
/**
 * lrg_net_server_send_async:
 * @self: an #LrgNetServer
 * @peer_id: the recipient peer ID
 * @message: the message to send
 *
 * Sends a message asynchronously.
 *
 * Returns: (transfer full): A #DexFuture resolving to %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_net_server_send_async (LrgNetServer  *self,
                                       guint32        peer_id,
                                       LrgNetMessage *message);
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_net_server_poll:
 * @self: an #LrgNetServer
 *
 * Processes pending network events.
 *
 * This should be called from the game loop to handle
 * incoming connections and messages.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_server_poll (LrgNetServer *self);

/* ==========================================================================
 * Signals
 * ========================================================================== */

/**
 * LrgNetServer::started:
 * @self: the #LrgNetServer
 *
 * Emitted when the server starts listening.
 */

/**
 * LrgNetServer::stopped:
 * @self: the #LrgNetServer
 *
 * Emitted when the server stops.
 */

/**
 * LrgNetServer::peer-connected:
 * @self: the #LrgNetServer
 * @peer: the connected peer
 *
 * Emitted when a new peer connects.
 */

/**
 * LrgNetServer::peer-disconnected:
 * @self: the #LrgNetServer
 * @peer_id: the disconnected peer's ID
 * @reason: (nullable): disconnect reason
 *
 * Emitted when a peer disconnects.
 */

/**
 * LrgNetServer::message-received:
 * @self: the #LrgNetServer
 * @peer_id: the sender's peer ID
 * @message: the received message
 *
 * Emitted when a message is received from a peer.
 */

G_END_DECLS
