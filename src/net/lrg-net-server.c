/* lrg-net-server.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "net/lrg-net-server.h"

/*
 * Internal peer connection data.
 */
typedef struct
{
    LrgNetPeer       *peer;
    GSocketConnection *connection;
    GInputStream      *input;
    GOutputStream     *output;
    GCancellable      *cancellable;
} PeerConnection;

/**
 * LrgNetServer:
 *
 * Network server for hosting multiplayer games.
 */
struct _LrgNetServer
{
    GObject           parent_instance;

    gchar            *host;
    guint             port;
    guint             max_peers;
    gboolean          running;

    GSocketService   *service;
    GHashTable       *peers;          /* guint32 -> PeerConnection* */
    guint32           next_peer_id;
    GQueue           *pending_messages; /* LrgNetMessage* received */
};

G_DEFINE_TYPE (LrgNetServer, lrg_net_server, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_HOST,
    PROP_PORT,
    PROP_MAX_PEERS,
    PROP_IS_RUNNING,
    PROP_PEER_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STARTED,
    SIGNAL_STOPPED,
    SIGNAL_PEER_CONNECTED,
    SIGNAL_PEER_DISCONNECTED,
    SIGNAL_MESSAGE_RECEIVED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static void
peer_connection_free (PeerConnection *pc)
{
    if (pc == NULL)
        return;

    if (pc->cancellable != NULL)
    {
        g_cancellable_cancel (pc->cancellable);
        g_object_unref (pc->cancellable);
    }

    g_clear_object (&pc->input);
    g_clear_object (&pc->output);
    g_clear_object (&pc->connection);
    g_clear_object (&pc->peer);
    g_free (pc);
}

static gboolean
on_incoming_connection (GSocketService    *service,
                        GSocketConnection *connection,
                        GObject           *source_object,
                        gpointer           user_data)
{
    LrgNetServer    *self = LRG_NET_SERVER (user_data);
    PeerConnection  *pc;
    GSocketAddress  *remote_addr;
    GInetAddress    *inet_addr;
    gchar           *address_str;
    guint            port;
    guint32          peer_id;

    /* Check max peers */
    if (self->max_peers > 0 && g_hash_table_size (self->peers) >= self->max_peers)
    {
        /* Reject connection - at capacity */
        return FALSE;
    }

    /* Get remote address info */
    remote_addr = g_socket_connection_get_remote_address (connection, NULL);
    if (remote_addr == NULL)
    {
        return FALSE;
    }

    if (G_IS_INET_SOCKET_ADDRESS (remote_addr))
    {
        GInetSocketAddress *inet_sock_addr = G_INET_SOCKET_ADDRESS (remote_addr);
        inet_addr = g_inet_socket_address_get_address (inet_sock_addr);
        address_str = g_inet_address_to_string (inet_addr);
        port = g_inet_socket_address_get_port (inet_sock_addr);
    }
    else
    {
        address_str = g_strdup ("unknown");
        port = 0;
    }
    g_object_unref (remote_addr);

    /* Assign peer ID */
    peer_id = self->next_peer_id++;

    /* Create peer connection */
    pc = g_new0 (PeerConnection, 1);
    pc->peer = lrg_net_peer_new (peer_id, address_str, port);
    pc->connection = g_object_ref (connection);
    pc->input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    pc->output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    pc->cancellable = g_cancellable_new ();

    /* Keep references */
    g_object_ref (pc->input);
    g_object_ref (pc->output);

    g_free (address_str);

    /* Set connected state */
    lrg_net_peer_set_state (pc->peer, LRG_NET_PEER_STATE_CONNECTED);

    /* Add to peers table */
    g_hash_table_insert (self->peers, GUINT_TO_POINTER (peer_id), pc);

    /* Notify */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PEER_COUNT]);
    g_signal_emit (self, signals[SIGNAL_PEER_CONNECTED], 0, pc->peer);

    return TRUE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_net_server_finalize (GObject *object)
{
    LrgNetServer *self = LRG_NET_SERVER (object);

    lrg_net_server_stop (self);

    g_clear_pointer (&self->host, g_free);
    g_clear_pointer (&self->peers, g_hash_table_unref);
    g_queue_free_full (self->pending_messages, (GDestroyNotify) lrg_net_message_free);

    G_OBJECT_CLASS (lrg_net_server_parent_class)->finalize (object);
}

static void
lrg_net_server_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgNetServer *self = LRG_NET_SERVER (object);

    switch (prop_id)
    {
    case PROP_HOST:
        g_value_set_string (value, self->host);
        break;
    case PROP_PORT:
        g_value_set_uint (value, self->port);
        break;
    case PROP_MAX_PEERS:
        g_value_set_uint (value, self->max_peers);
        break;
    case PROP_IS_RUNNING:
        g_value_set_boolean (value, self->running);
        break;
    case PROP_PEER_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->peers));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_server_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgNetServer *self = LRG_NET_SERVER (object);

    switch (prop_id)
    {
    case PROP_HOST:
        g_free (self->host);
        self->host = g_value_dup_string (value);
        break;
    case PROP_PORT:
        self->port = g_value_get_uint (value);
        break;
    case PROP_MAX_PEERS:
        self->max_peers = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_server_class_init (LrgNetServerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_net_server_finalize;
    object_class->get_property = lrg_net_server_get_property;
    object_class->set_property = lrg_net_server_set_property;

    /**
     * LrgNetServer:host:
     *
     * The bind address.
     */
    properties[PROP_HOST] =
        g_param_spec_string ("host",
                             "Host",
                             "Bind address",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetServer:port:
     *
     * The listen port.
     */
    properties[PROP_PORT] =
        g_param_spec_uint ("port",
                           "Port",
                           "Listen port",
                           0, 65535, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetServer:max-peers:
     *
     * Maximum number of peers (0 = unlimited).
     */
    properties[PROP_MAX_PEERS] =
        g_param_spec_uint ("max-peers",
                           "Max Peers",
                           "Maximum connected peers (0 = unlimited)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetServer:is-running:
     *
     * Whether the server is running.
     */
    properties[PROP_IS_RUNNING] =
        g_param_spec_boolean ("is-running",
                              "Is Running",
                              "Whether the server is running",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetServer:peer-count:
     *
     * Number of connected peers.
     */
    properties[PROP_PEER_COUNT] =
        g_param_spec_uint ("peer-count",
                           "Peer Count",
                           "Number of connected peers",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgNetServer::started:
     * @self: the #LrgNetServer
     *
     * Emitted when the server starts.
     */
    signals[SIGNAL_STARTED] =
        g_signal_new ("started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgNetServer::stopped:
     * @self: the #LrgNetServer
     *
     * Emitted when the server stops.
     */
    signals[SIGNAL_STOPPED] =
        g_signal_new ("stopped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgNetServer::peer-connected:
     * @self: the #LrgNetServer
     * @peer: the connected peer
     *
     * Emitted when a peer connects.
     */
    signals[SIGNAL_PEER_CONNECTED] =
        g_signal_new ("peer-connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_NET_PEER);

    /**
     * LrgNetServer::peer-disconnected:
     * @self: the #LrgNetServer
     * @peer_id: the peer ID
     * @reason: (nullable): disconnect reason
     *
     * Emitted when a peer disconnects.
     */
    signals[SIGNAL_PEER_DISCONNECTED] =
        g_signal_new ("peer-disconnected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

    /**
     * LrgNetServer::message-received:
     * @self: the #LrgNetServer
     * @peer_id: the sender peer ID
     * @message: the message
     *
     * Emitted when a message is received.
     */
    signals[SIGNAL_MESSAGE_RECEIVED] =
        g_signal_new ("message-received",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2, G_TYPE_UINT, LRG_TYPE_NET_MESSAGE);
}

static void
lrg_net_server_init (LrgNetServer *self)
{
    self->peers = g_hash_table_new_full (g_direct_hash,
                                         g_direct_equal,
                                         NULL,
                                         (GDestroyNotify) peer_connection_free);
    self->pending_messages = g_queue_new ();
    self->next_peer_id = 1;
    self->running = FALSE;
}

/* ==========================================================================
 * Public API
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
LrgNetServer *
lrg_net_server_new (const gchar *host,
                    guint        port)
{
    return g_object_new (LRG_TYPE_NET_SERVER,
                         "host", host,
                         "port", port,
                         NULL);
}

/**
 * lrg_net_server_start:
 * @self: an #LrgNetServer
 * @error: (nullable): return location for error
 *
 * Starts the server listening for connections.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_net_server_start (LrgNetServer  *self,
                      GError       **error)
{
    g_autoptr(GError) local_error = NULL;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), FALSE);

    if (self->running)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_ALREADY_CONNECTED,
                     "Server is already running");
        return FALSE;
    }

    /* Create socket service */
    self->service = g_socket_service_new ();

    /* Add listener */
    if (!g_socket_listener_add_inet_port (G_SOCKET_LISTENER (self->service),
                                          self->port,
                                          NULL,
                                          &local_error))
    {
        g_propagate_prefixed_error (error, local_error,
                                    "Failed to bind to port %u: ", self->port);
        g_clear_object (&self->service);
        return FALSE;
    }

    /* Connect incoming handler */
    g_signal_connect (self->service, "incoming",
                      G_CALLBACK (on_incoming_connection), self);

    /* Start accepting */
    g_socket_service_start (self->service);

    self->running = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_RUNNING]);
    g_signal_emit (self, signals[SIGNAL_STARTED], 0);

    return TRUE;
}

/**
 * lrg_net_server_stop:
 * @self: an #LrgNetServer
 *
 * Stops the server and disconnects all peers.
 */
void
lrg_net_server_stop (LrgNetServer *self)
{
    g_return_if_fail (LRG_IS_NET_SERVER (self));

    if (!self->running)
        return;

    /* Disconnect all peers */
    lrg_net_server_disconnect_all (self);

    /* Stop service */
    if (self->service != NULL)
    {
        g_socket_service_stop (self->service);
        g_socket_listener_close (G_SOCKET_LISTENER (self->service));
        g_clear_object (&self->service);
    }

    self->running = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_RUNNING]);
    g_signal_emit (self, signals[SIGNAL_STOPPED], 0);
}

/**
 * lrg_net_server_is_running:
 * @self: an #LrgNetServer
 *
 * Checks if the server is running.
 *
 * Returns: %TRUE if running
 */
gboolean
lrg_net_server_is_running (LrgNetServer *self)
{
    g_return_val_if_fail (LRG_IS_NET_SERVER (self), FALSE);
    return self->running;
}

/**
 * lrg_net_server_get_host:
 * @self: an #LrgNetServer
 *
 * Gets the bind address.
 *
 * Returns: (transfer none) (nullable): The host address
 */
const gchar *
lrg_net_server_get_host (LrgNetServer *self)
{
    g_return_val_if_fail (LRG_IS_NET_SERVER (self), NULL);
    return self->host;
}

/**
 * lrg_net_server_get_port:
 * @self: an #LrgNetServer
 *
 * Gets the listen port.
 *
 * Returns: The port number
 */
guint
lrg_net_server_get_port (LrgNetServer *self)
{
    g_return_val_if_fail (LRG_IS_NET_SERVER (self), 0);
    return self->port;
}

/**
 * lrg_net_server_get_max_peers:
 * @self: an #LrgNetServer
 *
 * Gets the maximum number of peers.
 *
 * Returns: Max peers (0 = unlimited)
 */
guint
lrg_net_server_get_max_peers (LrgNetServer *self)
{
    g_return_val_if_fail (LRG_IS_NET_SERVER (self), 0);
    return self->max_peers;
}

/**
 * lrg_net_server_set_max_peers:
 * @self: an #LrgNetServer
 * @max_peers: maximum peers (0 = unlimited)
 *
 * Sets the maximum number of peers.
 */
void
lrg_net_server_set_max_peers (LrgNetServer *self,
                              guint         max_peers)
{
    g_return_if_fail (LRG_IS_NET_SERVER (self));

    if (self->max_peers == max_peers)
        return;

    self->max_peers = max_peers;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_PEERS]);
}

/**
 * lrg_net_server_get_peer:
 * @self: an #LrgNetServer
 * @peer_id: the peer identifier
 *
 * Gets a peer by ID.
 *
 * Returns: (transfer none) (nullable): The peer, or %NULL
 */
LrgNetPeer *
lrg_net_server_get_peer (LrgNetServer *self,
                         guint32       peer_id)
{
    PeerConnection *pc;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), NULL);

    pc = g_hash_table_lookup (self->peers, GUINT_TO_POINTER (peer_id));
    return pc ? pc->peer : NULL;
}

/**
 * lrg_net_server_get_peers:
 * @self: an #LrgNetServer
 *
 * Gets a list of all connected peers.
 *
 * Returns: (transfer container) (element-type LrgNetPeer): List of peers
 */
GList *
lrg_net_server_get_peers (LrgNetServer *self)
{
    GList          *list = NULL;
    GHashTableIter  iter;
    gpointer        value;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), NULL);

    g_hash_table_iter_init (&iter, self->peers);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        PeerConnection *pc = value;
        list = g_list_prepend (list, pc->peer);
    }

    return list;
}

/**
 * lrg_net_server_get_peer_count:
 * @self: an #LrgNetServer
 *
 * Gets the number of connected peers.
 *
 * Returns: The peer count
 */
guint
lrg_net_server_get_peer_count (LrgNetServer *self)
{
    g_return_val_if_fail (LRG_IS_NET_SERVER (self), 0);
    return g_hash_table_size (self->peers);
}

/**
 * lrg_net_server_disconnect_peer:
 * @self: an #LrgNetServer
 * @peer_id: the peer to disconnect
 *
 * Disconnects a specific peer.
 */
void
lrg_net_server_disconnect_peer (LrgNetServer *self,
                                guint32       peer_id)
{
    PeerConnection *pc;

    g_return_if_fail (LRG_IS_NET_SERVER (self));

    pc = g_hash_table_lookup (self->peers, GUINT_TO_POINTER (peer_id));
    if (pc == NULL)
        return;

    lrg_net_peer_set_state (pc->peer, LRG_NET_PEER_STATE_DISCONNECTED);
    g_hash_table_remove (self->peers, GUINT_TO_POINTER (peer_id));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PEER_COUNT]);
    g_signal_emit (self, signals[SIGNAL_PEER_DISCONNECTED], 0, peer_id, NULL);
}

/**
 * lrg_net_server_disconnect_all:
 * @self: an #LrgNetServer
 *
 * Disconnects all peers.
 */
void
lrg_net_server_disconnect_all (LrgNetServer *self)
{
    GHashTableIter  iter;
    gpointer        key;
    GList          *peer_ids = NULL;
    GList          *l;

    g_return_if_fail (LRG_IS_NET_SERVER (self));

    /* Collect peer IDs first to avoid modifying during iteration */
    g_hash_table_iter_init (&iter, self->peers);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        peer_ids = g_list_prepend (peer_ids, key);
    }

    /* Disconnect each */
    for (l = peer_ids; l != NULL; l = l->next)
    {
        guint32 peer_id = GPOINTER_TO_UINT (l->data);
        lrg_net_server_disconnect_peer (self, peer_id);
    }

    g_list_free (peer_ids);
}

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
gboolean
lrg_net_server_send (LrgNetServer   *self,
                     guint32         peer_id,
                     LrgNetMessage  *message,
                     GError        **error)
{
    PeerConnection    *pc;
    g_autoptr(GBytes)  data = NULL;
    gsize              size;
    const guint8      *bytes;
    gsize              written;
    g_autoptr(GError)  local_error = NULL;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), FALSE);
    g_return_val_if_fail (message != NULL, FALSE);

    pc = g_hash_table_lookup (self->peers, GUINT_TO_POINTER (peer_id));
    if (pc == NULL)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_NOT_CONNECTED,
                     "Peer %u not found", peer_id);
        return FALSE;
    }

    data = lrg_net_message_serialize (message);
    bytes = g_bytes_get_data (data, &size);

    if (!g_output_stream_write_all (pc->output,
                                    bytes, size,
                                    &written,
                                    NULL,
                                    &local_error))
    {
        g_propagate_prefixed_error (error, local_error,
                                    "Failed to send to peer %u: ", peer_id);
        return FALSE;
    }

    lrg_net_peer_touch (pc->peer);

    return TRUE;
}

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
gboolean
lrg_net_server_broadcast (LrgNetServer   *self,
                          LrgNetMessage  *message,
                          GError        **error)
{
    GHashTableIter  iter;
    gpointer        key;
    gpointer        value;
    gboolean        success = TRUE;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), FALSE);
    g_return_val_if_fail (message != NULL, FALSE);

    g_hash_table_iter_init (&iter, self->peers);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        guint32 peer_id = GPOINTER_TO_UINT (key);
        g_autoptr(GError) local_error = NULL;

        if (!lrg_net_server_send (self, peer_id, message, &local_error))
        {
            /* Log but continue to other peers */
            g_warning ("Failed to broadcast to peer %u: %s",
                       peer_id, local_error->message);
            success = FALSE;
        }
    }

    return success;
}

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
DexFuture *
lrg_net_server_send_async (LrgNetServer  *self,
                           guint32        peer_id,
                           LrgNetMessage *message)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), NULL);
    g_return_val_if_fail (message != NULL, NULL);

    /*
     * For now, wrap synchronous send. A full async implementation
     * would use GOutputStream async APIs with DexFuture.
     */
    result = lrg_net_server_send (self, peer_id, message, &error);

    if (result)
        return dex_future_new_for_boolean (TRUE);
    else
        return dex_future_new_for_error (g_steal_pointer (&error));
}

/**
 * lrg_net_server_poll:
 * @self: an #LrgNetServer
 *
 * Processes pending network events.
 */
void
lrg_net_server_poll (LrgNetServer *self)
{
    g_return_if_fail (LRG_IS_NET_SERVER (self));

    /*
     * In a full implementation, this would:
     * 1. Check for incoming data on all peer connections
     * 2. Parse complete messages from input buffers
     * 3. Emit message-received signals
     * 4. Check for disconnected peers
     *
     * For now, the GSocketService handles connections asynchronously.
     * Message reading would require additional async read scheduling.
     */
}
