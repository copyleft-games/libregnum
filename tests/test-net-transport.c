/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <glib.h>
#include "net/lrg-net-server.h"
#include "net/lrg-net-client.h"
#include "mmo/lrg-mmo-realm.h"

typedef struct
{
    LrgNetServer *server;
    LrgNetClient *client;
    guint server_messages;
    guint client_messages;
    guint32 peer;
} Fixture;

static void
on_server_message (LrgNetServer  *server,
                   guint32        peer,
                   LrgNetMessage *message,
                   Fixture       *fixture)
{
    g_autoptr(GError) error = NULL;
    fixture->server_messages++;
    fixture->peer = peer;
    g_assert_true (lrg_net_server_send (server, peer, message, &error));
    g_assert_no_error (error);
}

static void
on_client_message (LrgNetClient  *client,
                   LrgNetMessage *message,
                   Fixture       *fixture)
{
    fixture->client_messages++;
}

static void
pump (Fixture *fixture)
{
    guint i;
    for (i = 0; i < 16 && g_main_context_pending (NULL); i++)
        g_main_context_iteration (NULL, FALSE);
    lrg_net_client_poll (fixture->client);
    lrg_net_server_poll (fixture->server);
}

static void
setup (Fixture *fixture,
       gconstpointer unused)
{
    g_autoptr(GError) error = NULL;
    gint64 deadline;
    fixture->server = lrg_net_server_new ("127.0.0.1", 0);
    g_assert_true (lrg_net_server_start (fixture->server, &error));
    fixture->client = lrg_net_client_new ("127.0.0.1", lrg_net_server_get_port (fixture->server));
    g_assert_true (lrg_net_client_connect (fixture->client, &error));
    g_assert_no_error (error);
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (lrg_net_server_get_peer_count (fixture->server) != 1 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (lrg_net_server_get_peer_count (fixture->server), ==, 1);
    g_signal_connect (fixture->server, "message-received", G_CALLBACK (on_server_message), fixture);
    g_signal_connect (fixture->client, "message-received", G_CALLBACK (on_client_message), fixture);
}

static void
teardown (Fixture *fixture,
          gconstpointer unused)
{
    g_object_unref (fixture->client);
    g_object_unref (fixture->server);
}

static void
test_roundtrip (Fixture *fixture,
                gconstpointer unused)
{
    g_autoptr(GBytes) payload = g_bytes_new_static ("command", 7);
    g_autoptr(LrgNetMessage) message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 999, 0, payload);
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoRealm) realm = lrg_mmo_realm_new (1, G_TIME_SPAN_SECOND);
    gint64 deadline;
    guint i;
    for (i = 0; i < 100; i++)
        g_assert_true (lrg_net_client_send (fixture->client, message, &error));
    deadline = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;
    while (fixture->client_messages < 100 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (fixture->client_messages, ==, 100);
    g_assert_cmpuint (fixture->server_messages, ==, 100);
    g_assert_cmpuint (fixture->peer, !=, 999);
    g_assert_true (lrg_mmo_realm_login (realm, fixture->peer, "verified-local-test", 0, &error));
    g_assert_true (lrg_mmo_realm_accept_command (realm, fixture->peer, 1, 0, &error));
    g_assert_false (lrg_mmo_realm_accept_command (realm, 999, 1, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

static void
test_backpressure (Fixture *fixture,
                   gconstpointer unused)
{
    g_autofree gchar *data = g_malloc0 (1024 * 1024);
    g_autoptr(GBytes) payload = g_bytes_new (data, 1024 * 1024);
    g_autoptr(LrgNetMessage) message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 0, 0, payload);
    g_autoptr(GError) error = NULL;
    guint i;
    for (i = 0; i < 3; i++)
        g_assert_true (lrg_net_client_send (fixture->client, message, &error));
    g_assert_false (lrg_net_client_send (fixture->client, message, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
}

static void
test_disconnect (Fixture *fixture,
                 gconstpointer unused)
{
    gint64 deadline;
    lrg_net_client_disconnect (fixture->client);
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (lrg_net_server_get_peer_count (fixture->server) != 0 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (lrg_net_server_get_peer_count (fixture->server), ==, 0);
}

static void
test_fragmentation (Fixture *fixture,
                    gconstpointer unused)
{
    g_autoptr(GSocketClient) connector = g_socket_client_new ();
    g_autoptr(GSocketConnection) connection = NULL;
    g_autoptr(GBytes) payload = g_bytes_new_static ("fragment", 8);
    g_autoptr(LrgNetMessage) message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 0, 0, payload);
    g_autoptr(GBytes) wire = lrg_net_message_serialize (message);
    g_autoptr(GError) error = NULL;
    GOutputStream *output;
    const guint8 *data;
    gsize size;
    guint i;
    gint64 deadline;
    connection = g_socket_client_connect_to_host (connector, "127.0.0.1",
                                                  lrg_net_server_get_port (fixture->server), NULL, &error);
    g_assert_no_error (error);
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    data = g_bytes_get_data (wire, &size);
    for (i = 0; i < size - 1; i++)
    {
        g_assert_true (g_output_stream_write_all (output, data + i, 1, NULL, NULL, &error));
        pump (fixture);
        g_assert_cmpuint (fixture->server_messages, ==, 0);
    }
    g_assert_true (g_output_stream_write_all (output, data + size - 1, 1, NULL, NULL, &error));
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (fixture->server_messages == 0 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (fixture->server_messages, ==, 1);
    g_assert_no_error (error);
    g_io_stream_close (G_IO_STREAM (connection), NULL, NULL);
}

static void
test_invalid_frames (void)
{
    guint8 raw[27] = { 0 };
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GError) error = NULL;
    raw[22] = 255;
    raw[23] = 255;
    raw[24] = 255;
    raw[25] = 255;
    bytes = g_bytes_new (raw, 26);
    g_assert_null (lrg_net_message_deserialize (bytes, &error));
    g_assert_error (error, LRG_NET_ERROR, LRG_NET_ERROR_MESSAGE_INVALID);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);
    memset (raw, 0, sizeof raw);
    bytes = g_bytes_new (raw, sizeof raw);
    g_assert_null (lrg_net_message_deserialize (bytes, &error));
    g_assert_error (error, LRG_NET_ERROR, LRG_NET_ERROR_MESSAGE_INVALID);
    g_clear_error (&error);
    g_clear_pointer (&bytes, g_bytes_unref);
    raw[0] = 255;
    bytes = g_bytes_new (raw, 26);
    g_assert_null (lrg_net_message_deserialize (bytes, &error));
    g_assert_error (error, LRG_NET_ERROR, LRG_NET_ERROR_MESSAGE_INVALID);
}

static void
stop_on_message (LrgNetServer  *server,
                 guint32        peer,
                 LrgNetMessage *message,
                 guint         *count)
{
    (*count)++;
    lrg_net_server_poll (server);
    lrg_net_server_stop (server);
}

static void
stop_on_disconnect (LrgNetServer *server,
                    guint32       peer,
                    const gchar  *reason,
                    gpointer      unused)
{
    lrg_net_server_stop (server);
}

static void
test_reentrant_stop (Fixture *fixture,
                     gconstpointer unused)
{
    g_autoptr(LrgNetMessage) message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_PING, 0, 0, NULL);
    guint count = 0;
    gint64 deadline;
    g_signal_connect (fixture->server, "message-received", G_CALLBACK (stop_on_message), &count);
    g_signal_connect (fixture->server, "peer-disconnected", G_CALLBACK (stop_on_disconnect), NULL);
    g_assert_true (lrg_net_client_send (fixture->client, message, NULL));
    g_assert_true (lrg_net_client_send (fixture->client, message, NULL));
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (lrg_net_server_is_running (fixture->server) && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_false (lrg_net_server_is_running (fixture->server));
    g_assert_cmpuint (count, ==, 1);
    g_assert_cmpuint (lrg_net_server_get_peer_count (fixture->server), ==, 0);
}

static void
test_oversized_frame (Fixture *fixture,
                      gconstpointer unused)
{
    g_autoptr(GSocketClient) connector = g_socket_client_new ();
    g_autoptr(GSocketConnection) connection = NULL;
    g_autoptr(GError) error = NULL;
    guint8 header[26] = { 0 };
    GOutputStream *output;
    gint64 deadline;
    connection = g_socket_client_connect_to_host (connector, "127.0.0.1",
                                                  lrg_net_server_get_port (fixture->server), NULL, &error);
    g_assert_no_error (error);
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (lrg_net_server_get_peer_count (fixture->server) < 2 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (lrg_net_server_get_peer_count (fixture->server), ==, 2);
    header[22] = 255;
    header[23] = 255;
    header[24] = 255;
    header[25] = 255;
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    g_assert_true (g_output_stream_write_all (output, header, sizeof header, NULL, NULL, &error));
    deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    while (lrg_net_server_get_peer_count (fixture->server) > 1 && g_get_monotonic_time () < deadline)
        pump (fixture);
    g_assert_cmpuint (lrg_net_server_get_peer_count (fixture->server), ==, 1);
    g_assert_cmpuint (fixture->server_messages, ==, 0);
    g_assert_no_error (error);
}

static void
on_failed (LrgNetClient *client,
           GError       *error,
           guint        *count)
{
    g_assert_nonnull (error);
    g_assert_nonnull (error->message);
    (*count)++;
}

static void
test_connection_failure (void)
{
    g_autoptr(LrgNetServer) server = lrg_net_server_new ("127.0.0.1", 0);
    g_autoptr(LrgNetClient) client = NULL;
    g_autoptr(GError) error = NULL;
    guint count = 0;
    guint port;
    g_assert_true (lrg_net_server_start (server, &error));
    port = lrg_net_server_get_port (server);
    lrg_net_server_stop (server);
    client = lrg_net_client_new ("127.0.0.1", port);
    g_signal_connect (client, "connection-failed", G_CALLBACK (on_failed), &count);
    g_assert_false (lrg_net_client_connect (client, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_false (lrg_net_client_connect (client, NULL));
    g_assert_cmpuint (count, ==, 2);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add ("/net/transport/roundtrip", Fixture, NULL, setup, test_roundtrip, teardown);
    g_test_add ("/net/transport/backpressure", Fixture, NULL, setup, test_backpressure, teardown);
    g_test_add ("/net/transport/disconnect", Fixture, NULL, setup, test_disconnect, teardown);
    g_test_add ("/net/transport/fragmentation", Fixture, NULL, setup, test_fragmentation, teardown);
    g_test_add_func ("/net/transport/invalid", test_invalid_frames);
    g_test_add_func ("/net/transport/connect-failure", test_connection_failure);
    g_test_add ("/net/transport/reentrant-stop", Fixture, NULL, setup, test_reentrant_stop, teardown);
    g_test_add ("/net/transport/oversized-frame", Fixture, NULL, setup, test_oversized_frame, teardown);
    return g_test_run ();
}
