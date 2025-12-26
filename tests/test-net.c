/* test-net.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for networking module.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Cases - LrgNetMessage
 * ========================================================================== */

static void
test_net_message_new (void)
{
    g_autoptr(LrgNetMessage) msg = NULL;

    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 1, 2, NULL);

    g_assert_nonnull (msg);
    g_assert_cmpint (lrg_net_message_get_message_type (msg), ==, LRG_NET_MESSAGE_TYPE_DATA);
    g_assert_cmpuint (lrg_net_message_get_sender_id (msg), ==, 1);
    g_assert_cmpuint (lrg_net_message_get_receiver_id (msg), ==, 2);
    g_assert_null (lrg_net_message_get_payload (msg));
}

static void
test_net_message_new_with_payload (void)
{
    g_autoptr(LrgNetMessage) msg = NULL;
    g_autoptr(GBytes) payload = NULL;
    const gchar *data = "Hello, World!";
    GBytes *retrieved;
    gsize size;
    const gchar *retrieved_data;

    payload = g_bytes_new (data, strlen (data));
    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 1, 0, payload);

    g_assert_nonnull (msg);
    retrieved = lrg_net_message_get_payload (msg);
    g_assert_nonnull (retrieved);

    retrieved_data = g_bytes_get_data (retrieved, &size);
    g_assert_cmpuint (size, ==, strlen (data));
    g_assert_cmpmem (retrieved_data, size, data, strlen (data));
}

static void
test_net_message_copy (void)
{
    g_autoptr(LrgNetMessage) msg = NULL;
    g_autoptr(LrgNetMessage) copy = NULL;
    g_autoptr(GBytes) payload = NULL;
    const gchar *data = "Test data";

    payload = g_bytes_new (data, strlen (data));
    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_PING, 5, 10, payload);
    lrg_net_message_set_reliable (msg, TRUE);
    lrg_net_message_set_sequence (msg, 42);

    copy = lrg_net_message_copy (msg);

    g_assert_nonnull (copy);
    g_assert_cmpint (lrg_net_message_get_message_type (copy), ==, LRG_NET_MESSAGE_TYPE_PING);
    g_assert_cmpuint (lrg_net_message_get_sender_id (copy), ==, 5);
    g_assert_cmpuint (lrg_net_message_get_receiver_id (copy), ==, 10);
    g_assert_true (lrg_net_message_is_reliable (copy));
    g_assert_cmpuint (lrg_net_message_get_sequence (copy), ==, 42);
}

static void
test_net_message_serialize_deserialize (void)
{
    g_autoptr(LrgNetMessage) msg = NULL;
    g_autoptr(LrgNetMessage) restored = NULL;
    g_autoptr(GBytes) payload = NULL;
    g_autoptr(GBytes) serialized = NULL;
    g_autoptr(GError) error = NULL;
    GBytes *restored_payload = NULL;
    const gchar *data = "Serialization test";
    const gchar *restored_data = NULL;
    gsize size;

    payload = g_bytes_new (data, strlen (data));
    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 100, 200, payload);
    lrg_net_message_set_reliable (msg, TRUE);
    lrg_net_message_set_sequence (msg, 999);

    serialized = lrg_net_message_serialize (msg);
    g_assert_nonnull (serialized);

    restored = lrg_net_message_deserialize (serialized, &error);
    g_assert_no_error (error);
    g_assert_nonnull (restored);

    g_assert_cmpint (lrg_net_message_get_message_type (restored), ==, LRG_NET_MESSAGE_TYPE_DATA);
    g_assert_cmpuint (lrg_net_message_get_sender_id (restored), ==, 100);
    g_assert_cmpuint (lrg_net_message_get_receiver_id (restored), ==, 200);
    g_assert_true (lrg_net_message_is_reliable (restored));
    g_assert_cmpuint (lrg_net_message_get_sequence (restored), ==, 999);

    /* Check payload */
    restored_payload = lrg_net_message_get_payload (restored);
    g_assert_nonnull (restored_payload);
    restored_data = g_bytes_get_data (restored_payload, &size);
    g_assert_cmpuint (size, ==, strlen (data));
    g_assert_cmpmem (restored_data, size, data, strlen (data));
}

static void
test_net_message_reliable (void)
{
    g_autoptr(LrgNetMessage) msg = NULL;

    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 1, 2, NULL);

    g_assert_false (lrg_net_message_is_reliable (msg));

    lrg_net_message_set_reliable (msg, TRUE);
    g_assert_true (lrg_net_message_is_reliable (msg));

    lrg_net_message_set_reliable (msg, FALSE);
    g_assert_false (lrg_net_message_is_reliable (msg));
}

static void
test_net_message_broadcast (void)
{
    g_autoptr(LrgNetMessage) broadcast_msg = NULL;
    g_autoptr(LrgNetMessage) direct_msg = NULL;

    broadcast_msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 1, 0, NULL);
    direct_msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 1, 2, NULL);

    g_assert_true (lrg_net_message_is_broadcast (broadcast_msg));
    g_assert_false (lrg_net_message_is_broadcast (direct_msg));
}

/* ==========================================================================
 * Test Cases - LrgNetPeer
 * ========================================================================== */

static void
test_net_peer_new (void)
{
    g_autoptr(LrgNetPeer) peer = NULL;

    peer = lrg_net_peer_new (42, "127.0.0.1", 12345);

    g_assert_nonnull (peer);
    g_assert_true (LRG_IS_NET_PEER (peer));
    g_assert_cmpuint (lrg_net_peer_get_peer_id (peer), ==, 42);
    g_assert_cmpstr (lrg_net_peer_get_address (peer), ==, "127.0.0.1");
    g_assert_cmpuint (lrg_net_peer_get_port (peer), ==, 12345);
}

static void
test_net_peer_state (void)
{
    g_autoptr(LrgNetPeer) peer = NULL;

    peer = lrg_net_peer_new (1, "localhost", 8080);

    g_assert_cmpint (lrg_net_peer_get_state (peer), ==, LRG_NET_PEER_STATE_DISCONNECTED);
    g_assert_false (lrg_net_peer_is_connected (peer));

    lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_CONNECTING);
    g_assert_cmpint (lrg_net_peer_get_state (peer), ==, LRG_NET_PEER_STATE_CONNECTING);
    g_assert_false (lrg_net_peer_is_connected (peer));

    lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_CONNECTED);
    g_assert_cmpint (lrg_net_peer_get_state (peer), ==, LRG_NET_PEER_STATE_CONNECTED);
    g_assert_true (lrg_net_peer_is_connected (peer));
}

static void
test_net_peer_rtt (void)
{
    g_autoptr(LrgNetPeer) peer = NULL;

    peer = lrg_net_peer_new (1, "localhost", 8080);

    g_assert_cmpuint (lrg_net_peer_get_rtt (peer), ==, 0);

    lrg_net_peer_update_rtt (peer, 50);
    g_assert_cmpuint (lrg_net_peer_get_rtt (peer), ==, 50);

    lrg_net_peer_update_rtt (peer, 100);
    g_assert_cmpuint (lrg_net_peer_get_rtt (peer), ==, 100);
}

static void
test_net_peer_touch (void)
{
    g_autoptr(LrgNetPeer) peer = NULL;
    gint64 initial_time;
    gint64 after_time;

    peer = lrg_net_peer_new (1, "localhost", 8080);

    initial_time = lrg_net_peer_get_last_activity (peer);
    g_assert_cmpint (initial_time, >, 0);

    /* Small delay */
    g_usleep (1000);

    lrg_net_peer_touch (peer);
    after_time = lrg_net_peer_get_last_activity (peer);

    g_assert_cmpint (after_time, >=, initial_time);
}

/* ==========================================================================
 * Test Cases - LrgNetServer
 * ========================================================================== */

static void
test_net_server_new (void)
{
    g_autoptr(LrgNetServer) server = NULL;

    server = lrg_net_server_new ("localhost", 9999);

    g_assert_nonnull (server);
    g_assert_true (LRG_IS_NET_SERVER (server));
    g_assert_cmpstr (lrg_net_server_get_host (server), ==, "localhost");
    g_assert_cmpuint (lrg_net_server_get_port (server), ==, 9999);
}

static void
test_net_server_properties (void)
{
    g_autoptr(LrgNetServer) server = NULL;

    server = lrg_net_server_new (NULL, 8888);

    g_assert_cmpuint (lrg_net_server_get_max_peers (server), ==, 0);
    g_assert_false (lrg_net_server_is_running (server));
    g_assert_cmpuint (lrg_net_server_get_peer_count (server), ==, 0);

    lrg_net_server_set_max_peers (server, 32);
    g_assert_cmpuint (lrg_net_server_get_max_peers (server), ==, 32);
}

static void
test_net_server_not_running (void)
{
    g_autoptr(LrgNetServer) server = NULL;
    LrgNetPeer *peer;
    GList *peers;

    server = lrg_net_server_new ("localhost", 7777);

    g_assert_false (lrg_net_server_is_running (server));

    peer = lrg_net_server_get_peer (server, 1);
    g_assert_null (peer);

    peers = lrg_net_server_get_peers (server);
    g_assert_null (peers);
}

/* ==========================================================================
 * Test Cases - LrgNetClient
 * ========================================================================== */

static void
test_net_client_new (void)
{
    g_autoptr(LrgNetClient) client = NULL;

    client = lrg_net_client_new ("example.com", 5555);

    g_assert_nonnull (client);
    g_assert_true (LRG_IS_NET_CLIENT (client));
    g_assert_cmpstr (lrg_net_client_get_server_host (client), ==, "example.com");
    g_assert_cmpuint (lrg_net_client_get_server_port (client), ==, 5555);
}

static void
test_net_client_properties (void)
{
    g_autoptr(LrgNetClient) client = NULL;

    client = lrg_net_client_new ("localhost", 4444);

    g_assert_false (lrg_net_client_is_connected (client));
    g_assert_cmpuint (lrg_net_client_get_local_id (client), ==, 0);
    g_assert_cmpuint (lrg_net_client_get_timeout (client), ==, 5000);

    lrg_net_client_set_timeout (client, 10000);
    g_assert_cmpuint (lrg_net_client_get_timeout (client), ==, 10000);
}

static void
test_net_client_not_connected (void)
{
    g_autoptr(LrgNetClient) client = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgNetMessage) msg = NULL;
    gboolean result;

    client = lrg_net_client_new ("localhost", 3333);
    msg = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 0, 0, NULL);

    g_assert_false (lrg_net_client_is_connected (client));

    /* Try to send when not connected */
    result = lrg_net_client_send (client, msg, &error);
    g_assert_false (result);
    g_assert_error (error, LRG_NET_ERROR, LRG_NET_ERROR_NOT_CONNECTED);
}

static void
test_net_client_no_host (void)
{
    g_autoptr(LrgNetClient) client = NULL;
    g_autoptr(GError) error = NULL;
    gboolean result;

    client = lrg_net_client_new (NULL, 2222);

    result = lrg_net_client_connect (client, &error);
    g_assert_false (result);
    g_assert_error (error, LRG_NET_ERROR, LRG_NET_ERROR_CONNECTION_FAILED);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Message tests */
    g_test_add_func ("/net/message/new", test_net_message_new);
    g_test_add_func ("/net/message/new-with-payload", test_net_message_new_with_payload);
    g_test_add_func ("/net/message/copy", test_net_message_copy);
    g_test_add_func ("/net/message/serialize-deserialize", test_net_message_serialize_deserialize);
    g_test_add_func ("/net/message/reliable", test_net_message_reliable);
    g_test_add_func ("/net/message/broadcast", test_net_message_broadcast);

    /* Peer tests */
    g_test_add_func ("/net/peer/new", test_net_peer_new);
    g_test_add_func ("/net/peer/state", test_net_peer_state);
    g_test_add_func ("/net/peer/rtt", test_net_peer_rtt);
    g_test_add_func ("/net/peer/touch", test_net_peer_touch);

    /* Server tests */
    g_test_add_func ("/net/server/new", test_net_server_new);
    g_test_add_func ("/net/server/properties", test_net_server_properties);
    g_test_add_func ("/net/server/not-running", test_net_server_not_running);

    /* Client tests */
    g_test_add_func ("/net/client/new", test_net_client_new);
    g_test_add_func ("/net/client/properties", test_net_client_properties);
    g_test_add_func ("/net/client/not-connected", test_net_client_not_connected);
    g_test_add_func ("/net/client/no-host", test_net_client_no_host);

    return g_test_run ();
}
