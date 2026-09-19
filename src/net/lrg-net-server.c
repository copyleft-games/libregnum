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
#include "lrg-net-buffer-private.h"

/*
 * Internal peer connection data.
 */
typedef struct
{
    LrgNetBuffer     buffer;
    LrgNetPeer       *peer;
    GIOStream         *connection;
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
    gboolean          polling;

    GSocketService   *service;
    GTlsCertificate  *certificate;
    GCancellable     *handshakes;
    guint             pending_tls;
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

    _lrg_net_buffer_clear (&pc->buffer);
    g_io_stream_close (G_IO_STREAM (pc->connection), NULL, NULL);
    g_clear_object (&pc->input);
    g_clear_object (&pc->output);
    g_clear_object (&pc->connection);
    g_clear_object (&pc->peer);
    g_free (pc);
}

static gboolean
admit_connection (LrgNetServer      *self,
                  GSocketConnection *connection,
                  GIOStream         *stream)
{
    PeerConnection  *pc;
    GSocketAddress  *remote_addr;
    GInetAddress    *inet_addr;
    gchar           *address_str;
    guint            port;
    guint32          peer_id;
    g_autoptr(LrgNetPeer) peer = NULL;
    g_autoptr(LrgNetServer) keep_alive = g_object_ref (self);

    if (!self->running)
        return FALSE;

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
    do
    {
        peer_id = self->next_peer_id++;
    } while (peer_id == 0 || g_hash_table_contains (self->peers, GUINT_TO_POINTER (peer_id)));

    /* Create peer connection */
    pc = g_new0 (PeerConnection, 1);
    _lrg_net_buffer_init (&pc->buffer);
    pc->peer = lrg_net_peer_new (peer_id, address_str, port);
    pc->connection = g_object_ref (stream);
    pc->input = g_io_stream_get_input_stream (stream);
    pc->output = g_io_stream_get_output_stream (stream);
    pc->cancellable = g_cancellable_new ();

    /* Keep references */
    g_object_ref (pc->input);
    g_object_ref (pc->output);

    g_free (address_str);

    /* Set connected state */
    lrg_net_peer_set_state (pc->peer, LRG_NET_PEER_STATE_CONNECTED);

    /* Add to peers table */
    g_hash_table_insert (self->peers, GUINT_TO_POINTER (peer_id), pc);

    /* Keep the peer alive if a notify handler disconnects it. */
    peer = g_object_ref (pc->peer);
    /* Notify */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PEER_COUNT]);
    if (g_hash_table_contains (self->peers, GUINT_TO_POINTER (peer_id)))
        g_signal_emit (self, signals[SIGNAL_PEER_CONNECTED], 0, peer);

    return TRUE;
}

typedef struct
{
    GWeakRef server;
    GSocketConnection *connection;
    GIOStream *stream;
    GCancellable *cancel;
    GCancellable *generation;
    GSource *timeout;
    gulong cancelled_id;
} TlsHandshake;

static gboolean
tls_timeout (gpointer data)
{
    g_cancellable_cancel (data);
    return G_SOURCE_REMOVE;
}

static void
cancel_handshake (GCancellable *source,
                  gpointer      data)
{
    g_cancellable_cancel (data);
}

static void
tls_ready (GObject      *source,
           GAsyncResult *result,
           gpointer      data)
{
    TlsHandshake *pending = data;
    g_autoptr(LrgNetServer) self = g_weak_ref_get (&pending->server);
    g_autoptr(GError) error = NULL;
    gboolean ready = g_tls_connection_handshake_finish (G_TLS_CONNECTION (source), result, &error);

    if (self != NULL && self->handshakes == pending->generation)
    {
        self->pending_tls--;
        if (ready && !g_cancellable_is_cancelled (pending->cancel) && self->running)
            admit_connection (self, pending->connection, pending->stream);
    }
    g_source_destroy (pending->timeout);
    g_source_unref (pending->timeout);
    g_cancellable_disconnect (pending->generation, pending->cancelled_id);
    g_object_unref (pending->generation);
    g_object_unref (pending->cancel);
    g_object_unref (pending->connection);
    g_object_unref (pending->stream);
    g_weak_ref_clear (&pending->server);
    g_free (pending);
}

static gboolean
on_incoming_connection (GSocketService    *service,
                        GSocketConnection *connection,
                        GObject           *source_object,
                        gpointer           user_data)
{
    LrgNetServer *self = user_data;
    TlsHandshake *pending;
    g_autoptr(GIOStream) stream = NULL;
    g_autoptr(GError) error = NULL;

    if (!self->running || (self->max_peers > 0 &&
        g_hash_table_size (self->peers) + self->pending_tls >= self->max_peers))
        return FALSE;
    if (self->certificate == NULL)
        return admit_connection (self, connection, G_IO_STREAM (connection));
    /* Bound handshakes even if the compatibility peer limit is unlimited. */
    if (self->pending_tls >= 128)
        return FALSE;
    stream = g_tls_server_connection_new (G_IO_STREAM (connection), self->certificate, &error);
    if (stream == NULL)
        return FALSE;
    pending = g_new0 (TlsHandshake, 1);
    g_weak_ref_init (&pending->server, self);
    pending->connection = g_object_ref (connection);
    pending->stream = g_steal_pointer (&stream);
    pending->cancel = g_cancellable_new ();
    pending->generation = g_object_ref (self->handshakes);
    pending->cancelled_id = g_cancellable_connect (pending->generation,
                                                  G_CALLBACK (cancel_handshake),
                                                  g_object_ref (pending->cancel), g_object_unref);
    pending->timeout = g_timeout_source_new_seconds (10);
    g_source_set_callback (pending->timeout, tls_timeout, g_object_ref (pending->cancel), g_object_unref);
    g_source_attach (pending->timeout, g_main_context_get_thread_default ());
    self->pending_tls++;
    g_tls_connection_handshake_async (G_TLS_CONNECTION (pending->stream), G_PRIORITY_DEFAULT,
                                      pending->cancel, tls_ready, pending);
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

    g_clear_object (&self->certificate);
    g_clear_object (&self->handshakes);
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

    g_clear_object (&self->handshakes);
    self->handshakes = g_cancellable_new ();
    self->pending_tls = 0;

    /* Create socket service */
    self->service = g_socket_service_new ();

    /* Honor the bind address and allow an ephemeral port for local hosts. */
    {
        g_autoptr(GInetAddress) address = NULL;
        g_autoptr(GSocketAddress) bind_address = NULL;
        g_autoptr(GSocketAddress) effective = NULL;

        address = self->host != NULL ? g_inet_address_new_from_string (self->host)
                                    : g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
        if (address == NULL)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Bind host must be a numeric IP address");
            g_clear_object (&self->service);
            return FALSE;
        }
        bind_address = g_inet_socket_address_new (address, self->port);
        if (!g_socket_listener_add_address (G_SOCKET_LISTENER (self->service),
                                            bind_address, G_SOCKET_TYPE_STREAM,
                                            G_SOCKET_PROTOCOL_TCP, NULL, &effective,
                                            &local_error))
        {
            g_propagate_error (error, g_steal_pointer (&local_error));
            g_clear_object (&self->service);
            return FALSE;
        }
        self->port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (effective));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORT]);
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

    g_cancellable_cancel (self->handshakes);

    /* Mark stopped before callbacks can re-enter stop(). */
    self->running = FALSE;

    /* Stop service */
    if (self->service != NULL)
    {
        g_signal_handlers_disconnect_by_data (self->service, self);
        g_socket_service_stop (self->service);
        g_socket_listener_close (G_SOCKET_LISTENER (self->service));
        g_clear_object (&self->service);
    }

    lrg_net_server_disconnect_all (self);
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
 * Queues a message for a specific peer. Call poll() to flush writes.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_net_server_send (LrgNetServer   *self,
                     guint32         peer_id,
                     LrgNetMessage  *message,
                     GError        **error)
{
    PeerConnection *pc;

    g_return_val_if_fail (LRG_IS_NET_SERVER (self), FALSE);
    g_return_val_if_fail (message != NULL, FALSE);

    pc = g_hash_table_lookup (self->peers, GUINT_TO_POINTER (peer_id));
    if (pc == NULL)
    {
        g_set_error (error, LRG_NET_ERROR, LRG_NET_ERROR_NOT_CONNECTED,
                     "Peer %u not found", peer_id);
        return FALSE;
    }
    return _lrg_net_buffer_send (&pc->buffer, message, error);
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
            /* Queue pressure is an expected recoverable failure. */
            if (success)
                g_propagate_error (error, g_steal_pointer (&local_error));
            success = FALSE;
        }
    }

    return success;
}

#ifdef LRG_HAS_LIBDEX
/**
 * lrg_net_server_send_async:
 * @self: an #LrgNetServer
 * @peer_id: the recipient peer ID
 * @message: the message to send
 *
 * Resolves when a message is queued, not when delivered. Call poll() to flush.
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

    /* Resolve when queued; poll() drives bounded nonblocking writes. */
    result = lrg_net_server_send (self, peer_id, message, &error);

    if (result)
        return dex_future_new_for_boolean (TRUE);
    else
        return dex_future_new_for_error (g_steal_pointer (&error));
}
#endif /* LRG_HAS_LIBDEX */

/**
 * lrg_net_server_poll:
 * @self: an #LrgNetServer
 *
 * Processes pending network events.
 */
void
lrg_net_server_poll (LrgNetServer *self)
{
    GList *ids;
    GList *item;
    g_autoptr(LrgNetServer) keep_alive = NULL;

    g_return_if_fail (LRG_IS_NET_SERVER (self));
    if (self->polling)
        return;
    keep_alive = g_object_ref (self);
    self->polling = TRUE;

    /* Accept callbacks run on the service's main context, driven by the host. */
    ids = g_hash_table_get_keys (self->peers);
    for (item = ids; item != NULL; item = item->next)
    {
        guint i;
        PeerConnection *pc;

        for (i = 0; i < 64; i++)
        {
            g_autoptr(GError) error = NULL;
            g_autoptr(LrgNetMessage) message = NULL;

            pc = g_hash_table_lookup (self->peers, item->data);
            if (pc == NULL)
                break;
            if (i == 0 && !_lrg_net_buffer_flush (&pc->buffer, pc->output, &error))
            {
                lrg_net_server_disconnect_peer (self, GPOINTER_TO_UINT (item->data));
                break;
            }
            message = _lrg_net_buffer_receive (&pc->buffer, pc->input, &error);
            if (error != NULL)
            {
                lrg_net_server_disconnect_peer (self, GPOINTER_TO_UINT (item->data));
                break;
            }
            if (message == NULL)
                break;
            lrg_net_peer_touch (pc->peer);
            /* The transport peer ID is authoritative, never the wire sender ID. */
            g_signal_emit (self, signals[SIGNAL_MESSAGE_RECEIVED], 0,
                           GPOINTER_TO_UINT (item->data), message);
        }
    }
    g_list_free (ids);
    self->polling = FALSE;
}

void
lrg_net_server_set_tls_certificate (LrgNetServer    *self,
                                    GTlsCertificate *certificate)
{
    g_return_if_fail (LRG_IS_NET_SERVER (self));
    g_return_if_fail (!self->running);
    g_return_if_fail (certificate == NULL || G_IS_TLS_CERTIFICATE (certificate));
    g_set_object (&self->certificate, certificate);
}
