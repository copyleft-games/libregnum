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

/**
 * LrgNetClient:
 *
 * Network client for connecting to multiplayer servers.
 */
struct _LrgNetClient
{
    GObject             parent_instance;

    gchar              *server_host;
    guint               server_port;
    guint               timeout_ms;
    guint32             local_id;
    gboolean            connected;

    GSocketClient      *socket_client;
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

    g_clear_pointer (&self->server_host, g_free);
    g_clear_object (&self->socket_client);

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
lrg_net_client_init (LrgNetClient *self)
{
    self->timeout_ms = 5000;
    self->connected = FALSE;
    self->local_id = 0;
    self->socket_client = g_socket_client_new ();
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

    if (self->connected)
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
    g_socket_client_set_timeout (self->socket_client, self->timeout_ms / 1000);

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
        g_propagate_prefixed_error (error, local_error,
                                    "Failed to connect to %s:%u: ",
                                    self->server_host, self->server_port);
        g_clear_object (&self->cancellable);
        g_signal_emit (self, signals[SIGNAL_CONNECTION_FAILED], 0, local_error);
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
    g_autoptr(GError) error = NULL;
    gboolean          result;

    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), NULL);

    /*
     * For now, wrap synchronous connect. A full async implementation
     * would use g_socket_client_connect_to_host_async with DexFuture.
     */
    result = lrg_net_client_connect (self, &error);

    if (result)
        return dex_future_new_for_boolean (TRUE);
    else
        return dex_future_new_for_error (g_steal_pointer (&error));
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

    if (!self->connected)
        return;

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
 * Sends a message to the server.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_net_client_send (LrgNetClient   *self,
                     LrgNetMessage  *message,
                     GError        **error)
{
    g_autoptr(GBytes)  data = NULL;
    gsize              size;
    const guint8      *bytes;
    gsize              written;
    g_autoptr(GError)  local_error = NULL;

    g_return_val_if_fail (LRG_IS_NET_CLIENT (self), FALSE);
    g_return_val_if_fail (message != NULL, FALSE);

    if (!self->connected)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_NOT_CONNECTED,
                     "Client is not connected");
        return FALSE;
    }

    data = lrg_net_message_serialize (message);
    bytes = g_bytes_get_data (data, &size);

    if (!g_output_stream_write_all (self->output,
                                    bytes, size,
                                    &written,
                                    self->cancellable,
                                    &local_error))
    {
        g_propagate_prefixed_error (error, local_error,
                                    "Failed to send message: ");
        return FALSE;
    }

    return TRUE;
}

#ifdef LRG_HAS_LIBDEX
/**
 * lrg_net_client_send_async:
 * @self: an #LrgNetClient
 * @message: the message to send
 *
 * Sends a message asynchronously.
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

    /*
     * For now, wrap synchronous send. A full async implementation
     * would use GOutputStream async APIs with DexFuture.
     */
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
    g_return_if_fail (LRG_IS_NET_CLIENT (self));

    /*
     * In a full implementation, this would:
     * 1. Check for incoming data on the connection
     * 2. Parse complete messages from input buffer
     * 3. Emit message-received signals
     * 4. Check for disconnect
     *
     * For now, message reading would require additional async read scheduling.
     */
}
