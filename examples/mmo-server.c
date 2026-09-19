/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <glib-unix.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>

typedef struct
{
    gchar *token;
    gint64 connected;
    gboolean busy;
} Client;

typedef struct
{
    GMainLoop *loop;
    LrgNetServer *server;
    LrgMmoRealm *realm;
    LrgMmoGate *gate;
    GHashTable *clients;
    gchar *database;
    guint jobs;
    guint64 requests;
    guint64 rejected;
    gboolean stopping;
} Host;

typedef struct
{
    Host *host;
    guint32 peer;
    guint opcode;
    guint64 sequence;
    gchar *token;
    GBytes *payload;
} Request;

static void
client_free (gpointer data)
{
    Client *client = data;
    g_free (client->token);
    g_free (client);
}

static void
request_free (gpointer data)
{
    Request *request = data;
    g_free (request->token);
    g_bytes_unref (request->payload);
    g_free (request);
}

static GVariant *
read_arguments (GBytes *bytes, const gchar *type, GError **error)
{
    g_autoptr(GVariant) value = NULL;
    if (g_bytes_get_size (bytes) > 8192)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Request too large");
        return NULL;
    }
    value = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE (type), bytes, FALSE));
    if (!g_variant_is_normal_form (value))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid request arguments");
        return NULL;
    }
#if G_BYTE_ORDER == G_BIG_ENDIAN
    return g_variant_byteswap (value);
#else
    return g_steal_pointer (&value);
#endif
}

static void
execute_request (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
    Request *request = task_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = NULL;
    g_autoptr(LrgMmoAuth) auth = NULL;
    g_autoptr(LrgMmoMarket) market = NULL;
    g_autoptr(LrgMmoSocial) social = NULL;
    g_autofree gchar *account = NULL;
    g_autoptr(GVariant) arguments = NULL;
    g_autoptr(GVariant) messages = NULL;
    gchar *result = NULL;
    gboolean success = FALSE;

    store = lrg_mmo_store_new (request->host->database, &error);
    if (store == NULL)
        goto out;
    auth = lrg_mmo_auth_new (store);
    account = lrg_mmo_auth_verify (auth, request->token, g_get_real_time () / G_TIME_SPAN_SECOND, &error);
    if (account == NULL)
        goto out;
    market = lrg_mmo_market_new (store);
    social = lrg_mmo_social_new (store);
    switch (request->opcode)
    {
    case 1:
        result = g_strdup (account);
        success = TRUE;
        break;
    case 2:
        success = g_bytes_get_size (request->payload) == 0;
        result = g_strdup ("pong");
        break;
    case 3:
        if (g_bytes_get_size (request->payload) != 0)
            break;
        messages = lrg_mmo_social_read_inbox (social, account, &error);
        if (messages != NULL)
        {
            result = g_variant_print (messages, TRUE);
            success = TRUE;
        }
        break;
    case 4:
        arguments = read_arguments (request->payload, "(sss)", &error);
        if (arguments != NULL)
        {
            const gchar *recipient, *text, *operation;
            g_variant_get (arguments, "(&s&s&s)", &recipient, &text, &operation);
            success = lrg_mmo_social_send_message (social, account, recipient, text, operation, &error);
        }
        break;
    case 5:
        arguments = read_arguments (request->payload, "(ss)", &error);
        if (arguments != NULL)
        {
            const gchar *listing, *operation;
            g_variant_get (arguments, "(&s&s)", &listing, &operation);
            success = lrg_mmo_market_buy (market, account, listing, operation, &error);
        }
        break;
    case 6:
        arguments = read_arguments (request->payload, "s", &error);
        if (arguments != NULL)
        {
            gint64 balance = lrg_mmo_market_get_balance (market, account,
                                                         g_variant_get_string (arguments, NULL), &error);
            if (balance >= 0)
            {
                result = g_strdup_printf ("%" G_GINT64_FORMAT, balance);
                success = TRUE;
            }
        }
        break;
    default:
        break;
    }
out:
    if (success)
        g_task_return_pointer (task, result != NULL ? result : g_strdup ("ok"), g_free);
    else
    {
        g_free (result);
        if (error == NULL)
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid request");
        g_task_return_error (task, g_steal_pointer (&error));
    }
}

static void
send_result (Host *host, Request *request, gboolean success, const gchar *text)
{
    g_autoptr(GVariant) result = g_variant_ref_sink (g_variant_new ("(bs)", success, text));
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GBytes) frame = NULL;
    g_autoptr(LrgNetMessage) message = NULL;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    g_autoptr(GVariant) swapped = g_variant_byteswap (result);
    bytes = g_variant_get_data_as_bytes (swapped);
#else
    bytes = g_variant_get_data_as_bytes (result);
#endif
    frame = lrg_mmo_protocol_encode (request->opcode | 0x8000, request->sequence, bytes, FALSE, NULL);
    if (frame == NULL)
    {
        lrg_net_server_disconnect_peer (host->server, request->peer);
        return;
    }
    message = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 0, request->peer, frame);
    if (!lrg_net_server_send (host->server, request->peer, message, NULL))
        lrg_net_server_disconnect_peer (host->server, request->peer);
}

static void
request_done (GObject *source, GAsyncResult *result, gpointer data)
{
    GTask *task = G_TASK (result);
    Request *request = g_task_get_task_data (task);
    Host *host = request->host;
    Client *client = g_hash_table_lookup (host->clients, GUINT_TO_POINTER (request->peer));
    g_autoptr(GError) error = NULL;
    g_autofree gchar *response = g_task_propagate_pointer (task, &error);
    host->jobs--;
    if (client == NULL || host->stopping)
        return;
    client->busy = FALSE;
    if (request->opcode == 1 && error == NULL)
    {
        if (lrg_mmo_realm_login (host->realm, request->peer, response, g_get_monotonic_time (), &error))
        {
            client->token = g_strdup (request->token);
            /* Handshake sequence is part of the same replay-protected command stream. */
            lrg_mmo_realm_accept_command (host->realm, request->peer, request->sequence,
                                          g_get_monotonic_time (), NULL);
        }
    }
    if (error != NULL)
        host->rejected++;
    send_result (host, request, error == NULL, error == NULL ? response : "Request rejected");
}

static void
peer_connected (LrgNetServer *server, LrgNetPeer *peer, Host *host)
{
    Client *client;
    guint32 id = lrg_net_peer_get_peer_id (peer);
    if (!lrg_mmo_gate_admit (host->gate, lrg_net_peer_get_address (peer), g_get_monotonic_time (), NULL))
    {
        host->rejected++;
        lrg_net_server_disconnect_peer (server, id);
        return;
    }
    client = g_new0 (Client, 1);
    client->connected = g_get_monotonic_time ();
    g_hash_table_insert (host->clients, GUINT_TO_POINTER (id), client);
}

static void
peer_disconnected (LrgNetServer *server, guint32 peer, const gchar *reason, Host *host)
{
    lrg_mmo_realm_logout (host->realm, peer);
    g_hash_table_remove (host->clients, GUINT_TO_POINTER (peer));
}

static void
message_received (LrgNetServer *server, guint32 peer, LrgNetMessage *message, Host *host)
{
    Client *client = g_hash_table_lookup (host->clients, GUINT_TO_POINTER (peer));
    g_autoptr(GBytes) payload = NULL;
    g_autoptr(GTask) task = NULL;
    GBytes *frame = lrg_net_message_get_payload (message);
    guint opcode;
    guint64 sequence;
    Request *request;
    if (client == NULL || client->busy || host->jobs >= 128 || frame == NULL || host->stopping)
        goto reject;
    payload = lrg_mmo_protocol_decode (frame, &opcode, &sequence, NULL);
    if (payload == NULL || opcode < 1 || opcode > 6 ||
        (client->token == NULL && opcode != 1) || (client->token != NULL && opcode == 1))
        goto reject;
    if (opcode == 1)
    {
        if (g_bytes_get_size (payload) != 64 ||
            !lrg_mmo_gate_admit (host->gate, lrg_net_peer_get_address (lrg_net_server_get_peer (server, peer)),
                                 g_get_monotonic_time (), NULL))
            goto reject;
    }
    else if (!lrg_mmo_realm_accept_command (host->realm, peer, sequence, g_get_monotonic_time (), NULL))
        goto reject;
    client->busy = TRUE;
    request = g_new0 (Request, 1);
    request->host = host;
    request->peer = peer;
    request->opcode = opcode;
    request->sequence = sequence;
    request->payload = g_steal_pointer (&payload);
    request->token = opcode == 1 ? g_strndup (g_bytes_get_data (request->payload, NULL), 64)
                                : g_strdup (client->token);
    task = g_task_new (server, NULL, request_done, NULL);
    g_task_set_task_data (task, request, request_free);
    host->jobs++;
    host->requests++;
    g_task_run_in_thread (task, execute_request);
    return;
reject:
    host->rejected++;
    lrg_net_server_disconnect_peer (server, peer);
}

static void
session_ended (LrgMmoRealm *realm, guint peer, Host *host)
{
    lrg_net_server_disconnect_peer (host->server, peer);
}

static gboolean
tick (gpointer data)
{
    Host *host = data;
    GHashTableIter iter;
    gpointer key, value;
    g_autoptr(GArray) expired = g_array_new (FALSE, FALSE, sizeof (guint32));
    gint64 now = g_get_monotonic_time ();
    guint i;
    if (host->stopping)
    {
        if (host->jobs == 0)
        {
            g_main_loop_quit (host->loop);
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
    lrg_net_server_poll (host->server);
    lrg_mmo_realm_expire (host->realm, now);
    g_hash_table_iter_init (&iter, host->clients);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Client *client = value;
        if (client->token == NULL && now - client->connected >= 10 * G_TIME_SPAN_SECOND)
        {
            guint32 peer = GPOINTER_TO_UINT (key);
            g_array_append_val (expired, peer);
        }
    }
    for (i = 0; i < expired->len; i++)
        lrg_net_server_disconnect_peer (host->server, g_array_index (expired, guint32, i));
    return G_SOURCE_CONTINUE;
}

static gboolean
stop (gpointer data)
{
    Host *host = data;
    host->stopping = TRUE;
    lrg_net_server_stop (host->server);
    return G_SOURCE_CONTINUE;
}

static void
health (SoupServer *server, SoupServerMessage *message, const gchar *path, GHashTable *query, gpointer data)
{
    Host *host = data;
    g_autofree gchar *body = NULL;
    if (!g_str_equal (soup_server_message_get_method (message), "GET"))
    {
        soup_server_message_set_status (message, SOUP_STATUS_METHOD_NOT_ALLOWED, NULL);
        return;
    }
    if (g_str_equal (path, "/healthz"))
        body = g_strdup (host->stopping ? "stopping\n" : "ready\n");
    else if (g_str_equal (path, "/metrics"))
        body = g_strdup_printf ("libregnum_sessions %u\nlibregnum_requests_total %" G_GUINT64_FORMAT
                                "\nlibregnum_rejected_total %" G_GUINT64_FORMAT "\nlibregnum_jobs %u\n",
                                lrg_mmo_realm_get_session_count (host->realm), host->requests,
                                host->rejected, host->jobs);
    else
    {
        soup_server_message_set_status (message, SOUP_STATUS_NOT_FOUND, NULL);
        return;
    }
    soup_server_message_set_status (message, host->stopping ? SOUP_STATUS_SERVICE_UNAVAILABLE : SOUP_STATUS_OK, NULL);
    soup_server_message_set_response (message, "text/plain", SOUP_MEMORY_COPY, body, strlen (body));
}

int
main (int argc, char **argv)
{
    g_autoptr(GOptionContext) options = g_option_context_new ("- TLS MMO service reference host (AGPL-3.0-or-later)");
    g_autoptr(GError) error = NULL;
    g_autoptr(GTlsCertificate) certificate = NULL;
    g_autoptr(LrgMmoStore) store = NULL;
    g_autoptr(LrgMmoAuth) auth = NULL;
    g_autoptr(SoupServer) metrics = NULL;
    g_autofree gchar *database = NULL;
    g_autofree gchar *cert = NULL;
    g_autofree gchar *key = NULL;
    g_autofree gchar *account = NULL;
    g_autofree gchar *login = NULL;
    g_autofree gchar *password_file = NULL;
    g_autofree gchar *password = NULL;
    g_autofree gchar *backup = NULL;
    g_autofree gchar *token = NULL;
    gsize password_size;
    gboolean version = FALSE;
    gint port = 7777;
    gint metrics_port = 9090;
    guint interrupt_source, terminate_source;
    Host host = { 0 };
    GOptionEntry entries[] = {
        { "version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version and license", NULL },
        { "database", 0, 0, G_OPTION_ARG_FILENAME, &database, "Private SQLite database path", "PATH" },
        { "certificate", 0, 0, G_OPTION_ARG_FILENAME, &cert, "TLS certificate chain", "PATH" },
        { "key", 0, 0, G_OPTION_ARG_FILENAME, &key, "TLS private key", "PATH" },
        { "port", 0, 0, G_OPTION_ARG_INT, &port, "TLS listen port (0 selects an ephemeral port)", "PORT" },
        { "metrics-port", 0, 0, G_OPTION_ARG_INT, &metrics_port, "Loopback HTTP health/metrics port", "PORT" },
        { "register", 0, 0, G_OPTION_ARG_STRING, &account, "Register an account and exit", "ACCOUNT" },
        { "login", 0, 0, G_OPTION_ARG_STRING, &login, "Issue an account token to stdout and exit", "ACCOUNT" },
        { "password-file", 0, 0, G_OPTION_ARG_FILENAME, &password_file, "Private password file for register/login", "PATH" },
        { "backup", 0, 0, G_OPTION_ARG_FILENAME, &backup, "Write a consistent backup and exit", "PATH" },
        { NULL }
    };
    g_option_context_add_main_entries (options, entries, NULL);
    g_option_context_set_summary (options, "Example: mmo-server --database world.db --certificate chain.pem --key key.pem\n"
                                 "Admin: mmo-server --database world.db --register alice --password-file private.txt");
    if (!g_option_context_parse (options, &argc, &argv, &error))
        goto failure;
    if (version)
    {
        g_print ("Libregnum MMO host %s\nAGPL-3.0-or-later\n", LRG_VERSION_STRING);
        return 0;
    }
    if (database == NULL || port < 0 || port > 65535 || metrics_port < 0 || metrics_port > 65535 ||
        (account != NULL) + (login != NULL) + (backup != NULL) > 1)
    {
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Specify a database and valid exclusive operating mode");
        goto failure;
    }
    store = lrg_mmo_store_new (database, &error);
    if (store == NULL)
        goto failure;
    if (backup != NULL)
        return lrg_mmo_store_backup (store, backup, &error) ? 0 : (g_printerr ("Backup failed: %s\n", error->message), 1);
    if (account != NULL || login != NULL)
    {
        if (password_file == NULL || !g_file_get_contents (password_file, &password, &password_size, &error))
        {
            if (error == NULL)
                g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A private password file is required");
            goto failure;
        }
        if (password_size > 1025 || memchr (password, '\0', password_size) != NULL)
        {
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid password file");
            goto failure;
        }
        if (password_size > 0 && password[password_size - 1] == '\n')
            password[password_size - 1] = '\0';
        auth = lrg_mmo_auth_new (store);
        if (account != NULL)
        {
            if (!lrg_mmo_auth_register (auth, account, password, &error))
                goto failure;
        }
        else
        {
            token = lrg_mmo_auth_login (auth, login, password, g_get_real_time () / G_TIME_SPAN_SECOND, &error);
            if (token == NULL)
                goto failure;
            g_print ("%s\n", token);
        }
        return 0;
    }
    if (cert == NULL || key == NULL)
    {
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "TLS certificate and key are required");
        goto failure;
    }
    certificate = g_tls_certificate_new_from_files (cert, key, &error);
    if (certificate == NULL)
        goto failure;
    host.loop = g_main_loop_new (NULL, FALSE);
    host.database = database;
    host.clients = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, client_free);
    host.server = lrg_net_server_new ("0.0.0.0", port);
    host.realm = lrg_mmo_realm_new (128, 60 * G_TIME_SPAN_SECOND);
    host.gate = lrg_mmo_gate_new (4096, 20, 2);
    lrg_net_server_set_tls_certificate (host.server, certificate);
    lrg_net_server_set_max_peers (host.server, 128);
    g_signal_connect (host.server, "peer-connected", G_CALLBACK (peer_connected), &host);
    g_signal_connect (host.server, "peer-disconnected", G_CALLBACK (peer_disconnected), &host);
    g_signal_connect (host.server, "message-received", G_CALLBACK (message_received), &host);
    g_signal_connect (host.realm, "session-ended", G_CALLBACK (session_ended), &host);
    metrics = soup_server_new (NULL, NULL);
    soup_server_add_handler (metrics, NULL, health, &host, NULL);
    if (!lrg_net_server_start (host.server, &error) ||
        !soup_server_listen_local (metrics, metrics_port, SOUP_SERVER_LISTEN_IPV4_ONLY, &error))
        goto cleanup;
    interrupt_source = g_unix_signal_add (SIGINT, stop, &host);
    terminate_source = g_unix_signal_add (SIGTERM, stop, &host);
    g_timeout_add (10, tick, &host);
    {
        GSList *uris = soup_server_get_uris (metrics);
        g_print ("READY tls-port=%u metrics-port=%d\n", lrg_net_server_get_port (host.server),
                 g_uri_get_port (uris->data));
        g_slist_free_full (uris, (GDestroyNotify) g_uri_unref);
    }
    fflush (stdout);
    g_main_loop_run (host.loop);
    g_source_remove (interrupt_source);
    g_source_remove (terminate_source);
cleanup:
    soup_server_disconnect (metrics);
    lrg_net_server_stop (host.server);
    g_signal_handlers_disconnect_by_data (host.server, &host);
    g_signal_handlers_disconnect_by_data (host.realm, &host);
    g_object_unref (host.server);
    g_object_unref (host.realm);
    g_object_unref (host.gate);
    g_hash_table_unref (host.clients);
    g_main_loop_unref (host.loop);
    if (error == NULL)
        return 0;
failure:
    g_printerr ("MMO host: %s\n", error->message);
    return 1;
}
