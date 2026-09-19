/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <glib.h>
#include "net/lrg-net-server.h"
#include "net/lrg-net-client.h"

typedef struct
{
    LrgNetClient *client;
    GError *error;
    gboolean success;
    gint done;
} Connect;

static gpointer
connect_thread (gpointer data)
{
    Connect *attempt = data;
    attempt->success = lrg_net_client_connect (attempt->client, &attempt->error);
    g_atomic_int_set (&attempt->done, 1);
    return NULL;
}

static void
count_message (LrgNetServer *server, guint32 peer, LrgNetMessage *message, guint *count)
{
    (*count)++;
    g_assert_true (lrg_net_server_send (server, peer, message, NULL));
}

static void
client_message (LrgNetClient *client, LrgNetMessage *message, guint *count)
{
    (*count)++;
}

static void
run_tls (gboolean trusted)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *cert_path = g_test_build_filename (G_TEST_DIST, "fixtures", "mmo-test-cert.pem", NULL);
    g_autofree gchar *key_path = g_test_build_filename (G_TEST_DIST, "fixtures", "mmo-test-key.pem", NULL);
    g_autoptr(GTlsCertificate) cert = g_tls_certificate_new_from_files (cert_path, key_path, &error);
    g_autoptr(GTlsDatabase) database = NULL;
    g_autoptr(LrgNetServer) server = lrg_net_server_new ("127.0.0.1", 0);
    g_autoptr(LrgNetClient) client = NULL;
    g_autoptr(LrgNetMessage) message = NULL;
    Connect attempt = { 0 };
    GThread *thread;
    gint64 deadline;
    guint received = 0;
    guint echoed = 0;
    g_assert_no_error (error);
    lrg_net_server_set_tls_certificate (server, cert);
    lrg_net_server_set_max_peers (server, 4);
    g_assert_true (lrg_net_server_start (server, &error));
    client = lrg_net_client_new ("127.0.0.1", lrg_net_server_get_port (server));
    lrg_net_client_set_tls (client, TRUE);
    if (trusted)
    {
        database = g_tls_file_database_new (cert_path, &error);
        g_assert_no_error (error);
        lrg_net_client_set_tls_database (client, database);
    }
    attempt.client = client;
    thread = g_thread_new ("tls-test", connect_thread, &attempt);
    deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (!g_atomic_int_get (&attempt.done) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_true (g_atomic_int_get (&attempt.done));
    g_thread_join (thread);
    if (trusted)
    {
        g_assert_no_error (attempt.error);
        g_assert_true (attempt.success);
        message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_PING, 0, 0, NULL);
        g_signal_connect (server, "message-received", G_CALLBACK (count_message), &received);
        g_signal_connect (client, "message-received", G_CALLBACK (client_message), &echoed);
        g_assert_true (lrg_net_client_send (client, message, &error));
        deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
        while (echoed == 0 && g_get_monotonic_time () < deadline)
        {
            g_main_context_iteration (NULL, FALSE);
            lrg_net_client_poll (client);
            lrg_net_server_poll (server);
            g_usleep (1000);
        }
        g_assert_cmpuint (received, ==, 1);
        g_assert_cmpuint (echoed, ==, 1);
    }
    else
    {
        g_assert_false (attempt.success);
        g_assert_nonnull (attempt.error);
        g_clear_error (&attempt.error);
    }
    lrg_net_client_disconnect (client);
    lrg_net_server_stop (server);
    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
}

static void
test_trusted (void)
{
    run_tls (TRUE);
}
static void
test_untrusted (void)
{
    run_tls (FALSE);
}
static void
test_async_context (void)
{
    g_autoptr(GMainContext) context = g_main_context_new ();
    g_autoptr(LrgNetServer) server = NULL;
    g_autoptr(LrgNetClient) client = NULL;
    g_autoptr(GError) error = NULL;
    DexFuture *future;
    gint64 deadline;

    g_main_context_push_thread_default (context);
    server = lrg_net_server_new ("127.0.0.1", 0);
    g_assert_true (lrg_net_server_start (server, &error));
    client = lrg_net_client_new ("127.0.0.1", lrg_net_server_get_port (server));
    future = lrg_net_client_connect_async (client);
    g_assert_true (dex_future_is_pending (future));
    deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (dex_future_is_pending (future) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (context, FALSE);
        g_usleep (1000);
    }
    g_assert_nonnull (dex_future_get_value (future, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_net_client_is_connected (client));
    dex_unref (future);
    lrg_net_client_disconnect (client);
    future = lrg_net_client_connect_async (client);
    lrg_net_client_disconnect (client);
    deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    while (dex_future_is_pending (future) && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (context, FALSE);
        g_usleep (1000);
    }
    g_assert_null (dex_future_get_value (future, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_false (lrg_net_client_is_connected (client));
    dex_unref (future);
    lrg_net_server_stop (server);
    while (g_main_context_pending (context))
        g_main_context_iteration (context, FALSE);
    g_main_context_pop_thread_default (context);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/net/tls/trusted-roundtrip", test_trusted);
    g_test_add_func ("/net/tls/untrusted-rejected", test_untrusted);
    g_test_add_func ("/net/async/private-context-cancellation", test_async_context);
    return g_test_run ();
}
