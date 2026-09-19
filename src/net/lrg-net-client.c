/* lrg-net-client.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "net/lrg-net-client.h"
#include "lrg-net-buffer-private.h"

/**
 * LrgNetClient:
 *
 * Network client for connecting to multiplayer servers.
 */
struct _LrgNetClient
{
    GObject             parent_instance;
    LrgNetBuffer        buffer;

    gchar              *server_host;
    guint               server_port;
    guint               timeout_ms;
    guint32             local_id;
    gboolean            connected;
    gboolean            connecting;
    gboolean            polling;

    GSocketClient      *socket_client;
    GTlsDatabase       *tls_database;
    GSocketConnection  *connection;
    GInputStream       *input;
    GOutputStream      *output;
    GCancellable       *cancellable;
};

G_DEFINE_TYPE (LrgNetClient, lrg_net_client, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SERVER_HOST,
    PROP_SERVER_PORT,
    PROP_LOCAL_ID,
    PROP_TIMEOUT,
    PROP_IS_CONNECTED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_CONNECTED,
    SIGNAL_DISCONNECTED,
    SIGNAL_MESSAGE_RECEIVED,
    SIGNAL_CONNECTION_FAILED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_net_client_finalize (GObject *object)
{
    LrgNetClient *self = LRG_NET_CLIENT (object);

    lrg_net_client_disconnect (self);

    _lrg_net_buffer_clear (&self->buffer);
    g_clear_pointer (&self->server_host, g_free);
    g_clear_object (&self->socket_client);
    g_clear_object (&self->tls_database);

    G_OBJECT_CLASS (lrg_net_client_parent_class)->finalize (object);
}

static void
lrg_net_client_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgNetClient *self = LRG_NET_CLIENT (object);

    switch (prop_id)
    {
    case PROP_SERVER_HOST:
        g_value_set_string (value, self->server_host);
        break;
    case PROP_SERVER_PORT:
        g_value_set_uint (value, self->server_port);
        break;
    case PROP_LOCAL_ID:
        g_value_set_uint (value, self->local_id);
        break;
    case PROP_TIMEOUT:
        g_value_set_uint (value, self->timeout_ms);
        break;
    case PROP_IS_CONNECTED:
        g_value_set_boolean (value, self->connected);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_client_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgNetClient *self = LRG_NET_CLIENT (object);

    switch (prop_id)
    {
    case PROP_SERVER_HOST:
        g_free (self->server_host);
        self->server_host = g_value_dup_string (value);
        break;
    case PROP_SERVER_PORT:
        self->server_port = g_value_get_uint (value);
        break;
    case PROP_TIMEOUT:
        self->timeout_ms = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_client_class_init (LrgNetClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_net_client_finalize;
    object_class->get_property = lrg_net_client_get_property;
    object_class->set_property = lrg_net_client_set_property;

    /**
     * LrgNetClient:server-host:
     *
     * The server hostname or IP address.
     */
    properties[PROP_SERVER_HOST] =
        g_param_spec_string ("server-host",
                             "Server Host",
                             "Server hostname or IP address",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetClient:server-port:
     *
     * The server port.
     */
    properties[PROP_SERVER_PORT] =
        g_param_spec_uint ("server-port",
                           "Server Port",
                           "Server port number",
                           0, 65535, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetClient:local-id:
     *
     * The local peer ID assigned by the server.
     */
    properties[PROP_LOCAL_ID] =
        g_param_spec_uint ("local-id",
                           "Local ID",
                           "Local peer ID assigned by server",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetClient:timeout:
     *
     * Connection timeout in milliseconds.
     */
    properties[PROP_TIMEOUT] =
        g_param_spec_uint ("timeout",
                           "Timeout",
                           "Connection timeout in milliseconds",
                           0, G_MAXUINT, 5000,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetClient:is-connected:
     *
     * Whether the client is connected.
     */
    properties[PROP_IS_CONNECTED] =
        g_param_spec_boolean ("is-connected",
                              "Is Connected",
                              "Whether the client is connected",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgNetClient::connected:
     * @self: the #LrgNetClient
     *
     * Emitted when the client connects.
     */
    signals[SIGNAL_CONNECTED] =
        g_signal_new ("connected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgNetClient::disconnected:
     * @self: the #LrgNetClient
     * @reason: (nullable): disconnect reason
     *
     * Emitted when the client disconnects.
     */
    signals[SIGNAL_DISCONNECTED] =
        g_signal_new ("disconnected",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LrgNetClient::message-received:
     * @self: the #LrgNetClient
     * @message: the received message
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
                      G_TYPE_NONE, 1, LRG_TYPE_NET_MESSAGE);

    /**
     * LrgNetClient::connection-failed:
     * @self: the #LrgNetClient
     * @error: the error
     *
     * Emitted when connection fails.
     */
    signals[SIGNAL_CONNECTION_FAILED] =
        g_signal_new ("connection-failed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_ERROR);
}

static void
on_socket_event (GSocketClient *client, GSocketClientEvent event,
                 GSocketConnectable *connectable, GIOStream *stream, LrgNetClient *self)
{
    if (event == G_SOCKET_CLIENT_TLS_HANDSHAKING && self->tls_database != NULL)
        g_tls_connection_set_database (G_TLS_CONNECTION (stream), self->tls_database);
}

static void
lrg_net_client_init (LrgNetClient *self)
{
    _lrg_net_buffer_init (&self->buffer);
    self->timeout_ms = 5000;
    self->connected = FALSE;
    self->local_id = 0;
    self->socket_client = g_socket_client_new ();
    g_signal_connect (self->socket_client, "event", G_CALLBACK (on_socket_event), self);
}

/* ==========================================================================
 * Public API
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
LrgNetClient *
lrg_net_client_new (const gchar *host,
                    guint        port)
{
    return g_object_new (LRG_TYPE_NET_CLIENT,
                         "server-host", host,
                         "server-port", port,
                         NULL);
}

/**
 * lrg_net_client_connect:
 * @self: an #LrgNetClient
 * @error: (nullable): return location for error
 *
 * Connects to the server.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_net_client_connect (LrgNetClient  *self,
                        GError       **error)
{
    g_autoptr(GError) local_error = NULL;

    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), FALSE);

    if (self->connected || self->connecting)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_ALREADY_CONNECTED,
                     "Client is already connected");
        return FALSE;
    }

    if (self->server_host == NULL || self->server_host[0] == '\0')
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_CONNECTION_FAILED,
                     "No server host specified");
        return FALSE;
    }

    /* Set timeout */
    g_socket_client_set_timeout (self->socket_client, MAX (1u, self->timeout_ms / 1000 + (self->timeout_ms % 1000 != 0)));

    /* Create cancellable */
    self->cancellable = g_cancellable_new ();

    /* Connect */
    self->connection = g_socket_client_connect_to_host (self->socket_client,
                                                         self->server_host,
                                                         self->server_port,
                                                         self->cancellable,
                                                         &local_error);

    if (self->connection == NULL)
    {
        g_clear_object (&self->cancellable);
        g_prefix_error (&local_error, "Failed to connect to %s:%u: ",
                        self->server_host, self->server_port);
        g_signal_emit (self, signals[SIGNAL_CONNECTION_FAILED], 0, local_error);
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }

    /* Get streams */
    self->input = g_io_stream_get_input_stream (G_IO_STREAM (self->connection));
    self->output = g_io_stream_get_output_stream (G_IO_STREAM (self->connection));
    g_object_ref (self->input);
    g_object_ref (self->output);

    self->connected = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_CONNECTED]);
    g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);

    return TRUE;
}

#ifdef LRG_HAS_LIBDEX
typedef struct
{
    LrgNetClient *client;
    DexPromise *promise;
    GCancellable *cancellable;
} ConnectRequest;

static void
connect_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    ConnectRequest *request = data;
    LrgNetClient *self = request->client;
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocketConnection) connection = NULL;

    connection = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (source), result, &error);
    if (self->cancellable != request->cancellable || g_cancellable_is_cancelled (request->cancellable))
    {
        g_clear_error (&error);
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Connection attempt cancelled");
    }
    if (error != NULL)
    {
        if (self->cancellable == request->cancellable)
        {
            self->connecting = FALSE;
            g_clear_object (&self->cancellable);
            g_signal_emit (self, signals[SIGNAL_CONNECTION_FAILED], 0, error);
        }
        dex_promise_reject (request->promise, g_steal_pointer (&error));
    }
    else
    {
        self->connection = g_steal_pointer (&connection);
        self->input = g_object_ref (g_io_stream_get_input_stream (G_IO_STREAM (self->connection)));
        self->output = g_object_ref (g_io_stream_get_output_stream (G_IO_STREAM (self->connection)));
        self->connecting = FALSE;
        self->connected = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_CONNECTED]);
        g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);
        dex_promise_resolve_boolean (request->promise, TRUE);
    }
    g_object_unref (request->cancellable);
    dex_unref (request->promise);
    g_object_unref (request->client);
    g_free (request);
}

/**
 * lrg_net_client_connect_async:
 * @self: an #LrgNetClient
 *
 * Connects to the server asynchronously.
 *
 * Returns: (transfer full): A #DexFuture resolving to %TRUE on success
 */
DexFuture *
lrg_net_client_connect_async (LrgNetClient *self)
{
    ConnectRequest *request;
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), NULL);
    if (self->connected || self->connecting)
        return dex_future_new_for_error (g_error_new_literal (LRG_NET_ERROR,
                   LRG_NET_ERROR_ALREADY_CONNECTED, "Connection already active"));
    if (self->server_host == NULL || *self->server_host == '\0')
        return dex_future_new_for_error (g_error_new_literal (LRG_NET_ERROR,
                   LRG_NET_ERROR_CONNECTION_FAILED, "No server host specified"));
    request = g_new0 (ConnectRequest, 1);
    request->client = g_object_ref (self);
    request->promise = dex_promise_new_cancellable ();
    request->cancellable = g_object_ref (dex_promise_get_cancellable (request->promise));
    g_set_object (&self->cancellable, request->cancellable);
    self->connecting = TRUE;
    g_socket_client_set_timeout (self->socket_client,
                                  MAX (1u, self->timeout_ms / 1000 + (self->timeout_ms % 1000 != 0)));
    g_socket_client_connect_to_host_async (self->socket_client, self->server_host, self->server_port,
                                          request->cancellable, connect_ready, request);
    return DEX_FUTURE (dex_ref (request->promise));
}
#endif /* LRG_HAS_LIBDEX */

/**
 * lrg_net_client_disconnect:
 * @self: an #LrgNetClient
 *
 * Disconnects from the server.
 */
void
lrg_net_client_disconnect (LrgNetClient *self)
{
    g_return_if_fail (LRG_IS_NET_CLIENT (self));

    if (!self->connected && !self->connecting)
        return;
    self->connecting = FALSE;

    /* Cancel any pending operations */
    if (self->cancellable != NULL)
    {
        g_cancellable_cancel (self->cancellable);
        g_clear_object (&self->cancellable);
    }

    /* Close connection */
    g_clear_object (&self->input);
    g_clear_object (&self->output);

    if (self->connection != NULL)
    {
        g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
        g_clear_object (&self->connection);
    }

    self->connected = FALSE;
    g_byte_array_set_size (self->buffer.input, 0);
    g_byte_array_set_size (self->buffer.output, 0);
    self->local_id = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_CONNECTED]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCAL_ID]);
    g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0, NULL);
}

/**
 * lrg_net_client_is_connected:
 * @self: an #LrgNetClient
 *
 * Checks if connected to the server.
 *
 * Returns: %TRUE if connected
 */
gboolean
lrg_net_client_is_connected (LrgNetClient *self)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), FALSE);
    return self->connected;
}

/**
 * lrg_net_client_get_server_host:
 * @self: an #LrgNetClient
 *
 * Gets the server hostname.
 *
 * Returns: (transfer none): The server host
 */
const gchar *
lrg_net_client_get_server_host (LrgNetClient *self)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), NULL);
    return self->server_host;
}

/**
 * lrg_net_client_get_server_port:
 * @self: an #LrgNetClient
 *
 * Gets the server port.
 *
 * Returns: The server port
 */
guint
lrg_net_client_get_server_port (LrgNetClient *self)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), 0);
    return self->server_port;
}

/**
 * lrg_net_client_get_local_id:
 * @self: an #LrgNetClient
 *
 * Gets the local peer ID assigned by the server.
 *
 * Returns: The local peer ID, or 0 if not connected
 */
guint32
lrg_net_client_get_local_id (LrgNetClient *self)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), 0);
    return self->local_id;
}

/**
 * lrg_net_client_get_timeout:
 * @self: an #LrgNetClient
 *
 * Gets the connection timeout in milliseconds.
 *
 * Returns: The timeout in ms
 */
guint
lrg_net_client_get_timeout (LrgNetClient *self)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), 0);
    return self->timeout_ms;
}

/**
 * lrg_net_client_set_timeout:
 * @self: an #LrgNetClient
 * @timeout_ms: timeout in milliseconds
 *
 * Sets the connection timeout.
 */
void
lrg_net_client_set_timeout (LrgNetClient *self,
                            guint         timeout_ms)
{
    g_return_if_fail (LRG_IS_NET_CLIENT (self));

    if (self->timeout_ms == timeout_ms)
        return;

    self->timeout_ms = timeout_ms;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMEOUT]);
}

/**
 * lrg_net_client_send:
 * @self: an #LrgNetClient
 * @message: the message to send
 * @error: (nullable): return location for error
 *
 * Queues a message for the server. Call poll() to flush writes.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_net_client_send (LrgNetClient   *self,
                     LrgNetMessage  *message,
                     GError        **error)
{
    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), FALSE);
    g_return_val_if_fail (message != NULL, FALSE);

    if (!self->connected)
    {
        g_set_error_literal (error, LRG_NET_ERROR, LRG_NET_ERROR_NOT_CONNECTED,
                             "Client is not connected");
        return FALSE;
    }
    return _lrg_net_buffer_send (&self->buffer, message, error);
}

#ifdef LRG_HAS_LIBDEX
/**
 * lrg_net_client_send_async:
 * @self: an #LrgNetClient
 * @message: the message to send
 *
 * Resolves when a message is queued, not when delivered. Call poll() to flush.
 *
 * Returns: (transfer full): A #DexFuture resolving to %TRUE on success
 */
DexFuture *
lrg_net_client_send_async (LrgNetClient  *self,
                           LrgNetMessage *message)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), NULL);
    g_return_val_if_fail (message != NULL, NULL);

    /* Resolve when queued; poll() drives bounded nonblocking writes. */
    result = lrg_net_client_send (self, message, &error);

    if (result)
        return dex_future_new_for_boolean (TRUE);
    else
        return dex_future_new_for_error (g_steal_pointer (&error));
}
#endif /* LRG_HAS_LIBDEX */

/**
 * lrg_net_client_poll:
 * @self: an #LrgNetClient
 *
 * Processes pending network events.
 */
void
lrg_net_client_poll (LrgNetClient *self)
{
    guint i;
    g_autoptr(GSocketConnection) connection = NULL;
    g_autoptr(LrgNetClient) keep_alive = NULL;

    g_return_if_fail (LRG_IS_NET_CLIENT (self));
    if (!self->connected || self->polling)
        return;
    keep_alive = g_object_ref (self);
    self->polling = TRUE;
    connection = g_object_ref (self->connection);
    for (i = 0; i < 64 && self->connection == connection; i++)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(LrgNetMessage) message = NULL;

        if (i == 0 && !_lrg_net_buffer_flush (&self->buffer, self->output, &error))
        {
            lrg_net_client_disconnect (self);
            break;
        }
        message = _lrg_net_buffer_receive (&self->buffer, self->input, &error);
        if (error != NULL)
        {
            lrg_net_client_disconnect (self);
            break;
        }
        if (message == NULL)
            break;
        g_signal_emit (self, signals[SIGNAL_MESSAGE_RECEIVED], 0, message);
    }
    self->polling = FALSE;
}

void
lrg_net_client_set_tls (LrgNetClient *self,
                        gboolean      enabled)
{
    g_return_if_fail (LRG_IS_NET_CLIENT (self));
    g_return_if_fail (!self->connected && !self->connecting);
    g_socket_client_set_tls (self->socket_client, enabled);
}

void
lrg_net_client_set_tls_database (LrgNetClient *self, GTlsDatabase *database)
{
    g_return_if_fail (LRG_IS_NET_CLIENT (self));
    g_return_if_fail (!self->connected && !self->connecting);
    g_return_if_fail (database == NULL || G_IS_TLS_DATABASE (database));
    g_set_object (&self->tls_database, database);
}
