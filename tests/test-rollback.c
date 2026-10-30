/* test-rollback.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rollback netcode: the loopback network's simulated faults, two- and
 * three-player sessions converging under latency, jitter, loss and
 * duplication (every peer ends with the checksums of an offline run with
 * the true inputs), stalling at the rollback window, desync detection
 * between peers, and a session over real UDP sockets on localhost.
 */

#include "replay-scenario.h"

#define TARGET_FRAMES (360)

/* ==========================================================================
 * Loopback network
 * ========================================================================== */

static void
test_loopback_network (void)
{
    g_autoptr(LrgLoopbackNetwork) network = NULL;
    g_autoptr(LrgLoopbackTransport) a = NULL;
    g_autoptr(LrgLoopbackTransport) b = NULL;
    g_autoptr(GBytes) hello = g_bytes_new_static ("hello", 5);
    g_autoptr(GBytes) received = NULL;
    guint from = 99;
    guint i;
    guint delivered;

    network = lrg_loopback_network_new (1);
    a = lrg_loopback_network_create_endpoint (network, 0);
    b = lrg_loopback_network_create_endpoint (network, 1);

    /* Latency: nothing before 50 ms, then the datagram from player 0 */
    lrg_loopback_network_set_latency (network, 0.05, 0.0);
    lrg_rollback_transport_send (LRG_ROLLBACK_TRANSPORT (a), 1, hello);
    g_assert_null (lrg_rollback_transport_receive (LRG_ROLLBACK_TRANSPORT (b), &from));
    lrg_loopback_network_advance (network, 0.049);
    g_assert_null (lrg_rollback_transport_receive (LRG_ROLLBACK_TRANSPORT (b), &from));
    lrg_loopback_network_advance (network, 0.002);
    received = lrg_rollback_transport_receive (LRG_ROLLBACK_TRANSPORT (b), &from);
    g_assert_nonnull (received);
    g_assert_cmpuint (from, ==, 0);
    g_assert_true (g_bytes_equal (received, hello));
    g_assert_null (lrg_rollback_transport_receive (LRG_ROLLBACK_TRANSPORT (a), &from));

    /* Loss: about the configured fraction is dropped, the rest arrives */
    lrg_loopback_network_set_latency (network, 0.0, 0.0);
    lrg_loopback_network_set_loss (network, 0.25, 0.0);
    for (i = 0; i < 1000; i++)
        lrg_rollback_transport_send (LRG_ROLLBACK_TRANSPORT (a), 1, hello);
    delivered = 0;
    for (;;)
    {
        g_autoptr(GBytes) packet = lrg_rollback_transport_receive (LRG_ROLLBACK_TRANSPORT (b), &from);

        if (packet == NULL)
            break;
        delivered++;
    }
    g_assert_cmpuint (delivered + lrg_loopback_network_get_n_dropped (network), ==, 1000);
    g_assert_cmpuint (delivered, >, 650);
    g_assert_cmpuint (delivered, <, 850);
    g_assert_cmpuint (lrg_loopback_network_get_n_in_flight (network), ==, 0);
}

/* ==========================================================================
 * Peers
 * ========================================================================== */

typedef struct
{
    Scenario           *scenario;
    LrgRollbackSession *session;
    gint64              next_input;   /* frame of the next local input */
    guint               desyncs;
} Peer;

static void
on_peer_desync (LrgRollbackSession *session,
                gint64              frame,
                guint               peer,
                guint64             local,
                guint64             remote,
                Peer               *self)
{
    self->desyncs++;
}

static void
peers_init (Peer                  *peers,
            guint                  n_peers,
            LrgRollbackTransport **transports,
            guint                  input_delay)
{
    guint i;

    for (i = 0; i < n_peers; i++)
    {
        peers[i].scenario = scenario_new (31337, n_peers);
        peers[i].session = lrg_rollback_session_new (peers[i].scenario->simulation,
                                                     transports[i], i);
        lrg_rollback_session_set_input_delay (peers[i].session, input_delay);
        lrg_rollback_session_set_max_rollback (peers[i].session, 12);
        lrg_rollback_session_set_checksum_interval (peers[i].session, 10);
        peers[i].next_input = 0;
        peers[i].desyncs = 0;
        g_signal_connect (peers[i].session, "desync", G_CALLBACK (on_peer_desync), &peers[i]);
    }
}

static void
peers_clear (Peer  *peers,
             guint  n_peers)
{
    guint i;

    for (i = 0; i < n_peers; i++)
    {
        g_clear_object (&peers[i].session);
        scenario_free (peers[i].scenario);
    }
}

/*
 * peers_tick:
 *
 * One frame of every peer: feed the next scripted input (until the
 * target), or just poll once done or stalled.
 */
static void
peers_tick (Peer  *peers,
            guint  n_peers,
            gint64 target)
{
    guint i;

    for (i = 0; i < n_peers; i++)
    {
        g_autoptr(GError) error = NULL;

        if (peers[i].next_input < target)
        {
            g_autoptr(LrgInputSnapshot) input = scripted_input (i, peers[i].next_input);

            if (lrg_rollback_session_advance_frame (peers[i].session, input, &error))
                peers[i].next_input++;
        }
        else
            lrg_rollback_session_poll (peers[i].session, &error);
        g_assert_no_error (error);
    }
}

static gboolean
peers_done (Peer  *peers,
            guint  n_peers,
            gint64 target)
{
    guint i;

    for (i = 0; i < n_peers; i++)
    {
        if (lrg_rollback_session_get_current_frame (peers[i].session) < target ||
            lrg_rollback_session_get_last_verified_frame (peers[i].session) < target)
            return FALSE;
    }
    return TRUE;
}

/*
 * reference_checksums:
 *
 * The offline truth: one simulation stepped with every player's real
 * input (scheduled input-delay frames late), checksummed every 10 frames.
 */
static GArray *
reference_checksums (guint  n_players,
                     guint  input_delay,
                     gint64 target)
{
    GArray *sums = g_array_new (FALSE, TRUE, sizeof (guint64));
    Scenario *scenario = scenario_new (31337, n_players);
    gint64 f;
    guint p;

    g_array_set_size (sums, (guint) target + 1);
    for (f = 0; f < target; f++)
    {
        LrgInputSnapshot *inputs[SCENARIO_MAX_PLAYERS];

        for (p = 0; p < n_players; p++)
            inputs[p] = f >= (gint64) input_delay ? scripted_input (p, f - input_delay)
                                                  : lrg_input_snapshot_new ();
        lrg_simulation_step (scenario->simulation, inputs, n_players);
        for (p = 0; p < n_players; p++)
            lrg_input_snapshot_free (inputs[p]);
        g_array_index (sums, guint64, f + 1) = lrg_simulation_checksum (scenario->simulation);
    }
    scenario_free (scenario);
    return sums;
}

static void
run_converge (guint   n_peers,
              gdouble latency,
              gdouble jitter,
              gdouble loss,
              gdouble duplicate,
              guint64 network_seed)
{
    g_autoptr(LrgLoopbackNetwork) network = NULL;
    g_autoptr(GArray) truth = NULL;
    LrgRollbackTransport *transports[SCENARIO_MAX_PLAYERS];
    Peer peers[SCENARIO_MAX_PLAYERS];
    guint rollbacks = 0;
    guint iterations;
    gint64 f;
    guint i;

    network = lrg_loopback_network_new (network_seed);
    lrg_loopback_network_set_latency (network, latency, jitter);
    lrg_loopback_network_set_loss (network, loss, duplicate);
    for (i = 0; i < n_peers; i++)
        transports[i] = LRG_ROLLBACK_TRANSPORT (lrg_loopback_network_create_endpoint (network, i));
    peers_init (peers, n_peers, transports, 2);

    /* Every peer runs at 60 frames a second of network time */
    for (iterations = 0; iterations < 20 * TARGET_FRAMES; iterations++)
    {
        peers_tick (peers, n_peers, TARGET_FRAMES);
        lrg_loopback_network_advance (network, SCENARIO_STEP);
        if (peers_done (peers, n_peers, TARGET_FRAMES))
            break;
    }
    g_assert_true (peers_done (peers, n_peers, TARGET_FRAMES));

    /* Every confirmed checksum on every peer equals the offline truth */
    truth = reference_checksums (n_peers, 2, TARGET_FRAMES);
    for (f = 10; f <= TARGET_FRAMES; f += 10)
    {
        for (i = 0; i < n_peers; i++)
        {
            guint64 checksum = 0;

            g_assert_true (lrg_rollback_session_lookup_checksum (peers[i].session, f, &checksum));
            g_assert_cmpuint (checksum, ==, g_array_index (truth, guint64, f));
        }
    }

    for (i = 0; i < n_peers; i++)
    {
        g_assert_cmpuint (peers[i].desyncs, ==, 0);
        g_assert_cmpint (lrg_rollback_session_get_desync_frame (peers[i].session), ==, -1);
        g_assert_cmpint (lrg_rollback_session_get_confirmed_frame (peers[i].session), >=,
                         TARGET_FRAMES - 1);
        rollbacks += lrg_rollback_session_get_rollback_count (peers[i].session);
    }

    /* With latency above the input delay, predictions were wrong and fixed */
    if (latency > 2 * SCENARIO_STEP)
        g_assert_cmpuint (rollbacks, >, 0);

    peers_clear (peers, n_peers);
    for (i = 0; i < n_peers; i++)
        g_object_unref (transports[i]);
}

static void
test_converge_two_players_ideal (void)
{
    run_converge (2, 0.0, 0.0, 0.0, 0.0, 1);
}

static void
test_converge_two_players_faulty (void)
{
    /* 80 ms one way, up to 40 ms jitter (reordering), 15 % loss, 5 % dups */
    run_converge (2, 0.08, 0.04, 0.15, 0.05, 2);
}

static void
test_converge_three_players_faulty (void)
{
    run_converge (3, 0.06, 0.05, 0.10, 0.05, 3);
}

/* ==========================================================================
 * Stall and desync
 * ========================================================================== */

static void
test_stall_at_rollback_window (void)
{
    g_autoptr(LrgLoopbackNetwork) network = NULL;
    LrgRollbackTransport *transports[2];
    Peer peers[2];
    g_autoptr(LrgInputSnapshot) input = lrg_input_snapshot_new ();
    g_autoptr(GError) error = NULL;
    guint advanced = 0;
    guint i;

    /* A one-second link: the peer may run max-rollback frames past what it
     * has confirmed, then waits */
    network = lrg_loopback_network_new (4);
    lrg_loopback_network_set_latency (network, 1.0, 0.0);
    for (i = 0; i < 2; i++)
        transports[i] = LRG_ROLLBACK_TRANSPORT (lrg_loopback_network_create_endpoint (network, i));
    peers_init (peers, 2, transports, 2);

    for (i = 0; i < 30; i++)
    {
        if (lrg_rollback_session_advance_frame (peers[0].session, input, &error))
            advanced++;
        g_assert_no_error (error);
        lrg_loopback_network_advance (network, SCENARIO_STEP);
    }
    /* Frames 0-1 are known (input delay), then 12 predicted frames */
    g_assert_cmpuint (advanced, ==, 2 + 12);
    g_assert_cmpint (lrg_rollback_session_get_current_frame (peers[0].session), ==, 14);
    g_assert_cmpint (lrg_rollback_session_get_confirmed_frame (peers[0].session), ==, 1);

    peers_clear (peers, 2);
    for (i = 0; i < 2; i++)
        g_object_unref (transports[i]);
}

static void
test_desync_between_peers (void)
{
    g_autoptr(LrgLoopbackNetwork) network = NULL;
    LrgRollbackTransport *transports[2];
    Peer peers[2];
    guint iterations;
    guint i;

    network = lrg_loopback_network_new (5);
    lrg_loopback_network_set_latency (network, 0.03, 0.01);
    for (i = 0; i < 2; i++)
        transports[i] = LRG_ROLLBACK_TRANSPORT (lrg_loopback_network_create_endpoint (network, i));
    peers_init (peers, 2, transports, 2);

    /* Peer 1's simulation breaks the determinism contract at frame 95 */
    peers[1].scenario->arena->bug_at_tick = 95;

    for (iterations = 0; iterations < 400; iterations++)
    {
        peers_tick (peers, 2, 200);
        lrg_loopback_network_advance (network, SCENARIO_STEP);
    }

    for (i = 0; i < 2; i++)
    {
        g_assert_cmpuint (peers[i].desyncs, >, 0);
        g_assert_cmpint (lrg_rollback_session_get_desync_frame (peers[i].session), ==, 100);
        g_assert_cmpint (lrg_rollback_session_get_last_verified_frame (peers[i].session), ==, 90);
    }

    peers_clear (peers, 2);
    for (i = 0; i < 2; i++)
        g_object_unref (transports[i]);
}

/* ==========================================================================
 * UDP
 * ========================================================================== */

static void
test_converge_udp (void)
{
    g_autoptr(LrgUdpTransport) a = NULL;
    g_autoptr(LrgUdpTransport) b = NULL;
    g_autoptr(GError) error = NULL;
    LrgRollbackTransport *transports[2];
    Peer peers[2];
    guint iterations;
    guint64 sum_a = 0;
    guint64 sum_b = 0;

    a = lrg_udp_transport_new ("127.0.0.1", 0, &error);
    if (a == NULL)
    {
        g_test_skip_printf ("No UDP on localhost: %s", error->message);
        return;
    }
    b = lrg_udp_transport_new ("127.0.0.1", 0, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_udp_transport_get_port (a), !=, 0);
    g_assert_true (lrg_udp_transport_add_peer (a, 1, "127.0.0.1", lrg_udp_transport_get_port (b), &error));
    g_assert_true (lrg_udp_transport_add_peer (b, 0, "127.0.0.1", lrg_udp_transport_get_port (a), &error));
    g_assert_false (lrg_udp_transport_add_peer (b, 0, "not-an-ip", 1, &error));
    g_clear_error (&error);

    transports[0] = LRG_ROLLBACK_TRANSPORT (a);
    transports[1] = LRG_ROLLBACK_TRANSPORT (b);
    peers_init (peers, 2, transports, 2);

    /* Real sockets: give the kernel a moment between rounds */
    for (iterations = 0; iterations < 5000; iterations++)
    {
        peers_tick (peers, 2, 120);
        if (peers_done (peers, 2, 120))
            break;
        g_usleep (200);
    }
    g_assert_true (peers_done (peers, 2, 120));
    g_assert_true (lrg_rollback_session_lookup_checksum (peers[0].session, 120, &sum_a));
    g_assert_true (lrg_rollback_session_lookup_checksum (peers[1].session, 120, &sum_b));
    g_assert_cmpuint (sum_a, ==, sum_b);
    g_assert_cmpuint (peers[0].desyncs + peers[1].desyncs, ==, 0);

    peers_clear (peers, 2);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/rollback/loopback/faults", test_loopback_network);
    g_test_add_func ("/rollback/converge/two-players-ideal", test_converge_two_players_ideal);
    g_test_add_func ("/rollback/converge/two-players-faulty", test_converge_two_players_faulty);
    g_test_add_func ("/rollback/converge/three-players-faulty", test_converge_three_players_faulty);
    g_test_add_func ("/rollback/stall", test_stall_at_rollback_window);
    g_test_add_func ("/rollback/desync", test_desync_between_peers);
    g_test_add_func ("/rollback/udp/localhost", test_converge_udp);

    return g_test_run ();
}
