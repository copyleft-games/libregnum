/* lrg-net-client.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Network client for connecting to multiplayer servers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include <libdex.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-net-message.h"

G_BEGIN_DECLS

#define LRG_TYPE_NET_CLIENT (lrg_net_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNetClient, lrg_net_client, LRG, NET_CLIENT, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_net_client_new:
 * @host: server hostname or IP address
 * @port: server port
 *
 * Creates a new network client.
 *
 * Returns: (transfer full): A new #LrgNetClient
 */
LRG_AVAILABLE_IN_ALL
LrgNetClient * lrg_net_client_new (const gchar *host,
                                   guint        port);

/* ==========================================================================
 * Connection
 * ========================================================================== */

/**
 * lrg_net_client_connect:
 * @self: an #LrgNetClient
 * @error: (nullable): return location for error
 *
 * Connects to the server.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_client_connect (LrgNetClient  *self,
                                 GError       **error);

/**
 * lrg_net_client_connect_async:
 * @self: an #LrgNetClient
 *
 * Connects to the server asynchronously.
 *
 * Returns: (transfer full): A #DexFuture resolving to %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_net_client_connect_async (LrgNetClient *self);

/**
 * lrg_net_client_disconnect:
 * @self: an #LrgNetClient
 *
 * Disconnects from the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_client_disconnect (LrgNetClient *self);

/**
 * lrg_net_client_is_connected:
 * @self: an #LrgNetClient
 *
 * Checks if connected to the server.
 *
 * Returns: %TRUE if connected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_client_is_connected (LrgNetClient *self);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_net_client_get_server_host:
 * @self: an #LrgNetClient
 *
 * Gets the server hostname.
 *
 * Returns: (transfer none): The server host
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_net_client_get_server_host (LrgNetClient *self);

/**
 * lrg_net_client_get_server_port:
 * @self: an #LrgNetClient
 *
 * Gets the server port.
 *
 * Returns: The server port
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_client_get_server_port (LrgNetClient *self);

/**
 * lrg_net_client_get_local_id:
 * @self: an #LrgNetClient
 *
 * Gets the local peer ID assigned by the server.
 *
 * Returns: The local peer ID, or 0 if not connected
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_net_client_get_local_id (LrgNetClient *self);

/**
 * lrg_net_client_get_timeout:
 * @self: an #LrgNetClient
 *
 * Gets the connection timeout in milliseconds.
 *
 * Returns: The timeout in ms
 */
LRG_AVAILABLE_IN_ALL
guint lrg_net_client_get_timeout (LrgNetClient *self);

/**
 * lrg_net_client_set_timeout:
 * @self: an #LrgNetClient
 * @timeout_ms: timeout in milliseconds
 *
 * Sets the connection timeout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_client_set_timeout (LrgNetClient *self,
                                 guint         timeout_ms);

/* ==========================================================================
 * Messaging
 * ========================================================================== */

/**
 * lrg_net_client_send:
 * @self: an #LrgNetClient
 * @message: the message to send
 * @error: (nullable): return location for error
 *
 * Sends a message to the server.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_net_client_send (LrgNetClient   *self,
                              LrgNetMessage  *message,
                              GError        **error);

/**
 * lrg_net_client_send_async:
 * @self: an #LrgNetClient
 * @message: the message to send
 *
 * Sends a message asynchronously.
 *
 * Returns: (transfer full): A #DexFuture resolving to %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_net_client_send_async (LrgNetClient  *self,
                                       LrgNetMessage *message);

/* ==========================================================================
 * Update
 * ========================================================================== */

/**
 * lrg_net_client_poll:
 * @self: an #LrgNetClient
 *
 * Processes pending network events.
 *
 * This should be called from the game loop to handle
 * incoming messages from the server.
 */
LRG_AVAILABLE_IN_ALL
void lrg_net_client_poll (LrgNetClient *self);

/* ==========================================================================
 * Signals
 * ========================================================================== */

/**
 * LrgNetClient::connected:
 * @self: the #LrgNetClient
 *
 * Emitted when the client connects to the server.
 */

/**
 * LrgNetClient::disconnected:
 * @self: the #LrgNetClient
 * @reason: (nullable): disconnect reason
 *
 * Emitted when the client disconnects.
 */

/**
 * LrgNetClient::message-received:
 * @self: the #LrgNetClient
 * @message: the received message
 *
 * Emitted when a message is received from the server.
 */

/**
 * LrgNetClient::connection-failed:
 * @self: the #LrgNetClient
 * @error: the connection error
 *
 * Emitted when connection attempt fails.
 */

G_END_DECLS
