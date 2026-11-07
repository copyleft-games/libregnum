/* test-net-stats.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNetStats: counters per peer and message type, the per-second
 * history (driven by a manual clock), RTT (explicit samples and
 * ping/pong pairs), loss, and the hooks in LrgNetServer / LrgNetClient /
 * the MMO protocol codec over a loopback connection.
 */

#include <glib.h>
#include <libregnum.h>

#define SEC (G_USEC_PER_SEC)

/* ==========================================================================
 * Core counting (private instances, manual clock)
 * ========================================================================== */

static void
test_counters (void)
{
	g_autoptr(LrgNetStats) stats = lrg_net_stats_new ();
	g_auto(GStrv)          peers = NULL;
	g_auto(GStrv)          types = NULL;
	LrgNetCounters         c;

	g_assert_true (lrg_net_stats_get_enabled (stats));
	g_assert_cmpuint (lrg_net_stats_get_window (stats), ==, LRG_NET_STATS_DEFAULT_WINDOW);
	lrg_net_stats_set_time (stats, 100 * SEC);

	lrg_net_stats_record_sent (stats, "a:1", "data", 100);
	lrg_net_stats_record_sent (stats, "a:1", "data", 50);
	lrg_net_stats_record_received (stats, "a:1", "ping", 26);
	lrg_net_stats_record_sent (stats, "b:2", "data", 10);
	lrg_net_stats_record_received (stats, NULL, NULL, 7);

	/* Everything */
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.packets_out, ==, 3);
	g_assert_cmpuint (c.bytes_out, ==, 160);
	g_assert_cmpuint (c.packets_in, ==, 2);
	g_assert_cmpuint (c.bytes_in, ==, 33);

	/* Per peer, per type, per type on a peer */
	g_assert_true (lrg_net_stats_get_totals (stats, "a:1", NULL, &c));
	g_assert_cmpuint (c.bytes_out, ==, 150);
	g_assert_cmpuint (c.bytes_in, ==, 26);
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, "data", &c));
	g_assert_cmpuint (c.packets_out, ==, 3);
	g_assert_true (lrg_net_stats_get_totals (stats, "a:1", "ping", &c));
	g_assert_cmpuint (c.packets_in, ==, 1);
	g_assert_false (lrg_net_stats_get_totals (stats, "b:2", "ping", &c));
	g_assert_cmpuint (c.packets_in, ==, 0);
	g_assert_false (lrg_net_stats_get_totals (stats, "nobody", NULL, &c));
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, "unknown", &c));
	g_assert_cmpuint (c.bytes_in, ==, 7);

	peers = lrg_net_stats_get_peers (stats);
	g_assert_cmpuint (g_strv_length (peers), ==, 2);
	g_assert_cmpstr (peers[0], ==, "a:1");
	g_assert_cmpstr (peers[1], ==, "b:2");
	types = lrg_net_stats_get_message_types (stats, NULL);
	g_assert_cmpuint (g_strv_length (types), ==, 3);
	g_assert_cmpstr (types[0], ==, "data");
	g_assert_cmpstr (types[1], ==, "ping");
	g_assert_cmpstr (types[2], ==, "unknown");
	g_clear_pointer (&types, g_strfreev);
	types = lrg_net_stats_get_message_types (stats, "b:2");
	g_assert_cmpuint (g_strv_length (types), ==, 1);
	g_assert_true (lrg_net_stats_get_peer_connected (stats, "a:1"));

	/* Disabled: nothing counts */
	lrg_net_stats_set_enabled (stats, FALSE);
	lrg_net_stats_record_sent (stats, "a:1", "data", 1000);
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.bytes_out, ==, 160);
	lrg_net_stats_set_enabled (stats, TRUE);

	/* Reset */
	lrg_net_stats_reset (stats);
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.packets_out + c.packets_in, ==, 0);
	g_clear_pointer (&peers, g_strfreev);
	peers = lrg_net_stats_get_peers (stats);
	g_assert_cmpuint (g_strv_length (peers), ==, 0);
}

static void
test_history (void)
{
	g_autoptr(LrgNetStats) stats = lrg_net_stats_new ();
	g_autofree gdouble    *values = NULL;
	LrgNetCounters         c;
	guint                  n = 0;
	guint                  i;

	lrg_net_stats_set_window (stats, 10);
	g_assert_cmpuint (lrg_net_stats_get_window (stats), ==, 10);

	/* Second s sends s bytes, for s = 1 .. 15 (the first five roll off) */
	for (i = 1; i <= 15; i++)
	{
		lrg_net_stats_set_time (stats, (gint64) i * SEC + SEC / 2);
		lrg_net_stats_record_sent (stats, "p:1", "data", i);
	}
	values = lrg_net_stats_get_history (stats, NULL, NULL, LRG_NET_STATS_METRIC_BYTES_OUT, &n);
	g_assert_cmpuint (n, ==, 10);
	for (i = 0; i < 10; i++)
		g_assert_cmpfloat (values[i], ==, (gdouble) (6 + i));
	g_clear_pointer (&values, g_free);

	/* Per peer and per type tracks hold the same history */
	values = lrg_net_stats_get_history (stats, "p:1", NULL, LRG_NET_STATS_METRIC_PACKETS_OUT, &n);
	g_assert_cmpfloat (values[9], ==, 1.0);
	g_clear_pointer (&values, g_free);
	values = lrg_net_stats_get_history (stats, NULL, "data", LRG_NET_STATS_METRIC_BYTES_OUT, &n);
	g_assert_cmpfloat (values[9], ==, 15.0);
	g_clear_pointer (&values, g_free);

	/* The last 3 seconds: 13 + 14 + 15 */
	g_assert_true (lrg_net_stats_get_window_counters (stats, NULL, NULL, 3, &c));
	g_assert_cmpuint (c.bytes_out, ==, 42);
	g_assert_cmpuint (c.packets_out, ==, 3);

	/* A gap of silence: 4 s later only 15 is still inside a 5 s window */
	lrg_net_stats_set_time (stats, 19 * SEC);
	g_assert_true (lrg_net_stats_get_window_counters (stats, "p:1", NULL, 5, &c));
	g_assert_cmpuint (c.bytes_out, ==, 15);
	values = lrg_net_stats_get_history (stats, NULL, NULL, LRG_NET_STATS_METRIC_BYTES_OUT, &n);
	g_assert_cmpfloat (values[9], ==, 0.0);
	g_assert_cmpfloat (values[5], ==, 15.0);
	g_clear_pointer (&values, g_free);

	/* Long after: stale buckets read as empty */
	lrg_net_stats_set_time (stats, 1000 * SEC);
	g_assert_true (lrg_net_stats_get_window_counters (stats, NULL, NULL, 10, &c));
	g_assert_cmpuint (c.bytes_out, ==, 0);
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.bytes_out, ==, 120);

	/* Changing the window drops history, keeps totals */
	lrg_net_stats_set_time (stats, 1001 * SEC);
	lrg_net_stats_record_sent (stats, "p:1", "data", 5);
	lrg_net_stats_set_window (stats, 4);
	values = lrg_net_stats_get_history (stats, NULL, NULL, LRG_NET_STATS_METRIC_BYTES_OUT, &n);
	g_assert_cmpuint (n, ==, 4);
	g_assert_cmpfloat (values[3], ==, 0.0);
	g_clear_pointer (&values, g_free);
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.bytes_out, ==, 125);
	lrg_net_stats_set_window (stats, 0);
	g_assert_cmpuint (lrg_net_stats_get_window (stats), ==, 1);
	lrg_net_stats_set_window (stats, 1000000);
	g_assert_cmpuint (lrg_net_stats_get_window (stats), ==, LRG_NET_STATS_MAX_WINDOW);
}

static void
test_rtt_and_loss (void)
{
	g_autoptr(LrgNetStats) stats = lrg_net_stats_new ();
	g_autofree gdouble    *values = NULL;
	gdouble                last, avg, min, max;
	guint                  n;

	g_assert_false (lrg_net_stats_get_rtt (stats, "p:1", &last, &avg, &min, &max));
	g_assert_cmpfloat (last, ==, 0.0);

	/* Explicit samples: avg is smoothed with alpha 1/8 */
	lrg_net_stats_set_time (stats, 50 * SEC);
	lrg_net_stats_record_rtt (stats, "p:1", 40.0);
	lrg_net_stats_record_rtt (stats, "p:1", 80.0);
	g_assert_true (lrg_net_stats_get_rtt (stats, "p:1", &last, &avg, &min, &max));
	g_assert_cmpfloat (last, ==, 80.0);
	g_assert_cmpfloat_with_epsilon (avg, 45.0, 1e-9);
	g_assert_cmpfloat (min, ==, 40.0);
	g_assert_cmpfloat (max, ==, 80.0);
	values = lrg_net_stats_get_history (stats, "p:1", NULL, LRG_NET_STATS_METRIC_RTT, &n);
	g_assert_cmpfloat (values[n - 1], ==, 60.0);
	g_clear_pointer (&values, g_free);

	/* Ping / pong pairs, matched by sequence, unknown pongs ignored */
	lrg_net_stats_record_ping (stats, "q:2", 7);
	lrg_net_stats_set_time (stats, 50 * SEC + 25000);
	lrg_net_stats_record_pong (stats, "q:2", 8);
	g_assert_false (lrg_net_stats_get_rtt (stats, "q:2", NULL, NULL, NULL, NULL));
	lrg_net_stats_record_pong (stats, "q:2", 7);
	g_assert_true (lrg_net_stats_get_rtt (stats, "q:2", &last, NULL, NULL, NULL));
	g_assert_cmpfloat_with_epsilon (last, 25.0, 1e-9);
	lrg_net_stats_record_pong (stats, "q:2", 7);   /* already matched */
	g_assert_true (lrg_net_stats_get_rtt (stats, "q:2", NULL, &avg, NULL, NULL));
	g_assert_cmpfloat_with_epsilon (avg, 25.0, 1e-9);

	/* Loss: lost / (lost + received); late arrivals give one back */
	lrg_net_stats_record_received (stats, "q:2", "dtls", 100);
	lrg_net_stats_record_received (stats, "q:2", "dtls", 100);
	lrg_net_stats_record_received (stats, "q:2", "dtls", 100);
	lrg_net_stats_record_lost (stats, "q:2", 2);
	g_assert_cmpfloat_with_epsilon (lrg_net_stats_get_loss (stats, "q:2"), 2.0 / 5.0, 1e-9);
	lrg_net_stats_record_lost (stats, "q:2", -1);
	g_assert_cmpfloat_with_epsilon (lrg_net_stats_get_loss (stats, "q:2"), 1.0 / 4.0, 1e-9);
	lrg_net_stats_record_lost (stats, "q:2", -5);
	g_assert_cmpfloat (lrg_net_stats_get_loss (stats, "q:2"), ==, 0.0);
	g_assert_cmpfloat (lrg_net_stats_get_loss (stats, "nobody"), ==, 0.0);

	/* Connection state */
	lrg_net_stats_set_peer_connected (stats, "q:2", FALSE);
	g_assert_false (lrg_net_stats_get_peer_connected (stats, "q:2"));
	lrg_net_stats_set_peer_connected (stats, "q:2", TRUE);
	g_assert_true (lrg_net_stats_get_peer_connected (stats, "q:2"));
}

static void
test_boxed (void)
{
	LrgNetCounters                c = { 1, 2, 3, 4, 5 };
	g_autoptr(LrgNetCounters)     copy = lrg_net_counters_copy (&c);
	g_autoptr(GEnumClass)         klass = g_type_class_ref (LRG_TYPE_NET_STATS_METRIC);

	g_assert_cmpuint (copy->lost, ==, 5);
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_NET_COUNTERS));
	g_assert_cmpstr (g_enum_get_value (klass, LRG_NET_STATS_METRIC_RTT)->value_nick, ==, "rtt");
}

/* ==========================================================================
 * Hooks: loopback server and client feeding the default instance
 * ========================================================================== */

typedef struct
{
	LrgNetServer *server;
	LrgNetClient *client;
	guint         server_messages;
	guint         client_messages;
} Loop;

/* The server answers a ping with a pong carrying its sequence and
 * echoes data. */
static void
on_server_message (LrgNetServer  *server,
                   guint32        peer,
                   LrgNetMessage *message,
                   Loop          *loop)
{
	loop->server_messages++;
	if (lrg_net_message_get_message_type (message) == LRG_NET_MESSAGE_TYPE_PING)
	{
		g_autoptr(LrgNetMessage) pong = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_PONG, 0, peer, NULL);

		lrg_net_message_set_sequence (pong, lrg_net_message_get_sequence (message));
		g_assert_true (lrg_net_server_send (server, peer, pong, NULL));
	}
	else
		g_assert_true (lrg_net_server_send (server, peer, message, NULL));
}

static void
on_client_message (LrgNetClient  *client,
                   LrgNetMessage *message,
                   Loop          *loop)
{
	loop->client_messages++;
}

static void
pump (Loop *loop)
{
	guint i;

	for (i = 0; i < 16 && g_main_context_pending (NULL); i++)
		g_main_context_iteration (NULL, FALSE);
	lrg_net_client_poll (loop->client);
	lrg_net_server_poll (loop->server);
}

static void
test_loopback (void)
{
	LrgNetStats             *stats = lrg_net_stats_get_default ();
	g_autoptr(GBytes)        payload = g_bytes_new_static ("0123456789", 10);
	g_autoptr(LrgNetMessage) data = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_DATA, 0, 0, payload);
	g_autoptr(LrgNetMessage) ping = lrg_net_message_new (LRG_NET_MESSAGE_TYPE_PING, 0, 0, NULL);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *server_key = NULL;
	g_auto(GStrv)            peers = NULL;
	gchar                   *client_key = NULL;
	LrgNetCounters           c;
	Loop                     loop = { NULL, NULL, 0, 0 };
	gint64                   deadline;
	guint                    i;

	lrg_net_stats_reset (stats);
	lrg_net_stats_set_enabled (stats, TRUE);

	loop.server = lrg_net_server_new ("127.0.0.1", 0);
	g_assert_true (lrg_net_server_start (loop.server, &error));
	g_assert_no_error (error);
	loop.client = lrg_net_client_new ("127.0.0.1", lrg_net_server_get_port (loop.server));
	g_assert_true (lrg_net_client_connect (loop.client, &error));
	g_assert_no_error (error);
	deadline = g_get_monotonic_time () + 2 * SEC;
	while (lrg_net_server_get_peer_count (loop.server) != 1 && g_get_monotonic_time () < deadline)
		pump (&loop);
	g_assert_cmpuint (lrg_net_server_get_peer_count (loop.server), ==, 1);
	g_signal_connect (loop.server, "message-received", G_CALLBACK (on_server_message), &loop);
	g_signal_connect (loop.client, "message-received", G_CALLBACK (on_client_message), &loop);

	/* 10 data messages and one ping, all echoed */
	for (i = 0; i < 10; i++)
		g_assert_true (lrg_net_client_send (loop.client, data, NULL));
	lrg_net_message_set_sequence (ping, 42);
	g_assert_true (lrg_net_client_send (loop.client, ping, NULL));
	deadline = g_get_monotonic_time () + 2 * SEC;
	while (loop.client_messages < 11 && g_get_monotonic_time () < deadline)
		pump (&loop);
	g_assert_cmpuint (loop.client_messages, ==, 11);

	/* Two peers: the client's view of the server, the server's of the client */
	server_key = g_strdup_printf ("127.0.0.1:%u", lrg_net_server_get_port (loop.server));
	peers = lrg_net_stats_get_peers (stats);
	g_assert_cmpuint (g_strv_length (peers), ==, 2);
	client_key = g_strcmp0 (peers[0], server_key) == 0 ? peers[1] : peers[0];
	g_assert_true (g_strv_contains ((const gchar * const *) peers, server_key));

	/* Client side: 10 data + 1 ping out, 10 data + 1 pong in (36 / 26 bytes) */
	g_assert_true (lrg_net_stats_get_totals (stats, server_key, NULL, &c));
	g_assert_cmpuint (c.packets_out, ==, 11);
	g_assert_cmpuint (c.packets_in, ==, 11);
	g_assert_cmpuint (c.bytes_out, ==, 10 * 36 + 26);
	g_assert_cmpuint (c.bytes_in, ==, 10 * 36 + 26);
	g_assert_true (lrg_net_stats_get_totals (stats, server_key, "pong", &c));
	g_assert_cmpuint (c.packets_in, ==, 1);

	/* Server side mirrors it */
	g_assert_true (lrg_net_stats_get_totals (stats, client_key, "data", &c));
	g_assert_cmpuint (c.packets_in, ==, 10);
	g_assert_cmpuint (c.packets_out, ==, 10);
	g_assert_true (lrg_net_stats_get_totals (stats, client_key, "ping", &c));
	g_assert_cmpuint (c.packets_in, ==, 1);

	/* Global per type: both directions of both ends */
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, "data", &c));
	g_assert_cmpuint (c.packets_out, ==, 20);
	g_assert_cmpuint (c.packets_in, ==, 20);

	/* The ping's pong gave the client an RTT sample */
	g_assert_true (lrg_net_stats_get_rtt (stats, server_key, NULL, NULL, NULL, NULL));
	g_assert_true (lrg_net_stats_get_window_counters (stats, NULL, NULL, 10, &c));
	g_assert_cmpuint (c.packets_in, ==, 22);

	/* lrg_net_peer_update_rtt() feeds the server's peer */
	{
		g_autoptr(GList) list = lrg_net_server_get_peers (loop.server);
		gdouble          last = 0.0;

		g_assert_nonnull (list);
		lrg_net_peer_update_rtt (LRG_NET_PEER (list->data), 12);
		g_assert_true (lrg_net_stats_get_rtt (stats, client_key, &last, NULL, NULL, NULL));
		g_assert_cmpfloat (last, ==, 12.0);
	}

	/* Disconnects are reported for both ends */
	g_assert_true (lrg_net_stats_get_peer_connected (stats, server_key));
	lrg_net_client_disconnect (loop.client);
	g_assert_false (lrg_net_stats_get_peer_connected (stats, server_key));
	g_clear_object (&loop.client);
	g_clear_object (&loop.server);
	g_assert_false (lrg_net_stats_get_peer_connected (stats, client_key));

	/* Disabled default: the hooks count nothing */
	lrg_net_stats_reset (stats);
	lrg_net_stats_set_enabled (stats, FALSE);
	{
		g_autoptr(LrgNetServer) server = lrg_net_server_new ("127.0.0.1", 0);
		g_autoptr(LrgNetClient) client = NULL;

		g_assert_true (lrg_net_server_start (server, NULL));
		client = lrg_net_client_new ("127.0.0.1", lrg_net_server_get_port (server));
		g_assert_true (lrg_net_client_connect (client, NULL));
		g_assert_true (lrg_net_client_send (client, data, NULL));
		lrg_net_client_poll (client);
	}
	g_assert_true (lrg_net_stats_get_totals (stats, NULL, NULL, &c));
	g_assert_cmpuint (c.packets_out, ==, 0);
	g_clear_pointer (&peers, g_strfreev);
	peers = lrg_net_stats_get_peers (stats);
	g_assert_cmpuint (g_strv_length (peers), ==, 0);
}

static void
test_mmo_codec (void)
{
	LrgNetStats       *stats = lrg_net_stats_get_default ();
	g_autoptr(GBytes)  payload = g_bytes_new_static ("hello", 5);
	g_autoptr(GBytes)  frame = NULL;
	g_autoptr(GBytes)  decoded = NULL;
	g_autoptr(GError)  error = NULL;
	LrgNetCounters     c;
	guint              opcode = 0;
	guint64            sequence = 0;

	lrg_net_stats_reset (stats);
	lrg_net_stats_set_enabled (stats, TRUE);

	frame = lrg_mmo_protocol_encode (12, 1, payload, FALSE, &error);
	g_assert_no_error (error);
	decoded = lrg_mmo_protocol_decode (frame, &opcode, &sequence, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (opcode, ==, 12);

	g_assert_true (lrg_net_stats_get_totals (stats, NULL, "mmo:12", &c));
	g_assert_cmpuint (c.packets_out, ==, 1);
	g_assert_cmpuint (c.packets_in, ==, 1);
	g_assert_cmpuint (c.bytes_out, ==, g_bytes_get_size (frame));
	g_assert_cmpuint (c.bytes_in, ==, g_bytes_get_size (frame));

	lrg_net_stats_set_enabled (stats, FALSE);
	lrg_net_stats_reset (stats);
}

int
main (int   argc,
      char *argv[])
{
	g_setenv ("LRG_NET_STATS", "0", TRUE);
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/net/stats/counters", test_counters);
	g_test_add_func ("/net/stats/history", test_history);
	g_test_add_func ("/net/stats/rtt-loss", test_rtt_and_loss);
	g_test_add_func ("/net/stats/boxed", test_boxed);
	g_test_add_func ("/net/stats/loopback", test_loopback);
	g_test_add_func ("/net/stats/mmo-codec", test_mmo_codec);
	return g_test_run ();
}
