/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "mmo/lrg-mmo-datagram.h"

static GSocket *
udp_socket (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GInetAddress) ip = g_inet_address_new_from_string ("127.0.0.1");
    g_autoptr(GSocketAddress) address = g_inet_socket_address_new (ip, 0);
    GSocket *socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
    g_assert_no_error (error);
    g_assert_true (g_socket_bind (socket, address, FALSE, &error));
    g_assert_no_error (error);
    return socket;
}

static GBytes *
receive (LrgMmoDatagram *channel, guint64 *sequence, GError **error)
{
    GBytes *bytes;
    gint64 deadline = g_get_monotonic_time () + 2000000;
    do
    {
        bytes = lrg_mmo_datagram_receive (channel, sequence, error);
        if (!g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            return bytes;
        g_clear_error (error);
        g_usleep (1000);
    } while (g_get_monotonic_time () < deadline);
    g_error ("Datagram receive timed out");
    return NULL;
}

static void
exercise (gconstpointer data)
{
    gboolean trusted = GPOINTER_TO_INT (data);
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocket) a = udp_socket ();
    g_autoptr(GSocket) b = udp_socket ();
    g_autoptr(GSocketAddress) aa = g_socket_get_local_address (a, &error);
    g_autoptr(GSocketAddress) ba = g_socket_get_local_address (b, &error);
    g_autofree gchar *cert_path = g_test_build_filename (G_TEST_DIST, "fixtures", "mmo-test-cert.pem", NULL);
    g_autofree gchar *key_path = g_test_build_filename (G_TEST_DIST, "fixtures", "mmo-test-key.pem", NULL);
    g_autoptr(LrgMmoDatagram) client = NULL;
    g_autoptr(LrgMmoDatagram) server = NULL;
    g_autoptr(GBytes) payload = g_bytes_new_static ("hello", 5);
    g_autoptr(GBytes) result = NULL;
    g_autoptr(GBytes) oversized = g_bytes_new_take (g_malloc0 (1101), 1101);
    gboolean client_ready = FALSE, server_ready = FALSE;
    gint64 deadline;
    guint64 sequence;
    g_assert_no_error (error);
    g_assert_true (g_socket_connect (a, ba, NULL, &error));
    g_assert_true (g_socket_connect (b, aa, NULL, &error));
    client = lrg_mmo_datagram_new (a, FALSE, trusted ? "localhost" : "wrong.example",
                                  cert_path, key_path, cert_path, &error);
    server = lrg_mmo_datagram_new (b, TRUE, NULL, cert_path, key_path, cert_path, &error);
    g_assert_no_error (error);
    deadline = g_get_monotonic_time () + 10000000;
    while ((!client_ready || !server_ready) && g_get_monotonic_time () < deadline)
    {
        client_ready = lrg_mmo_datagram_handshake (client, &error);
        if (!client_ready && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        {
            g_assert_false (trusted);
            g_assert_nonnull (error);
            return;
        }
        g_clear_error (&error);
        server_ready = lrg_mmo_datagram_handshake (server, &error);
        if (!server_ready)
            g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
        g_clear_error (&error);
        g_usleep (1000);
    }
    g_assert_true (trusted);
    g_assert_true (client_ready && server_ready);
    g_assert_true (lrg_mmo_datagram_send (client, 3, payload, &error));
    result = receive (server, &sequence, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (sequence, ==, 3);
    g_assert_true (g_bytes_equal (result, payload));
    g_clear_pointer (&result, g_bytes_unref);
    /* Sequence 1 was lost. Reordering within the window is valid. */
    g_assert_true (lrg_mmo_datagram_send (client, 2, payload, &error));
    result = receive (server, &sequence, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (sequence, ==, 2);
    g_clear_pointer (&result, g_bytes_unref);
    g_assert_true (lrg_mmo_datagram_send (client, 2, payload, &error));
    result = receive (server, &sequence, &error);
    g_assert_null (result);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_true (lrg_mmo_datagram_send (client, 100, payload, &error));
    result = receive (server, &sequence, &error);
    g_assert_no_error (error);
    g_clear_pointer (&result, g_bytes_unref);
    g_assert_true (lrg_mmo_datagram_send (client, 1, payload, &error));
    result = receive (server, &sequence, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_datagram_send (client, 0, payload, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_false (lrg_mmo_datagram_send (client, 101, oversized, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_clear_pointer (&oversized, g_bytes_unref);
    oversized = g_bytes_new_take (g_malloc0 (1100), 1100);
    g_assert_true (lrg_mmo_datagram_send (client, 101, oversized, &error));
    result = receive (server, &sequence, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_bytes_get_size (result), ==, 1100);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_data_func ("/mmo/datagram/mutual-tls-loss-reorder-replay", GINT_TO_POINTER (TRUE), exercise);
    g_test_add_data_func ("/mmo/datagram/wrong-identity", GINT_TO_POINTER (FALSE), exercise);
    return g_test_run ();
}
