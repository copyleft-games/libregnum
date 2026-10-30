/* replay-demo.c - Deterministic simulation, replays and rollback, headless
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A tiny two-player "ball tag" game on the replay module's bare API:
 *
 *   1. record 10 seconds of scripted play into a replay file,
 *   2. load the file back and play it headless, verifying checksums,
 *   3. run the same game as two rollback peers over a loopback network
 *      with 70 ms latency, jitter and 10 % packet loss, and show that both
 *      peers end in the state of the offline recording.
 *
 * Usage: replay-demo [OUTPUT.lrgreplay]
 */

#include <libregnum.h>
#include <stdio.h>

#define N_PLAYERS (2)
#define N_TICKS   (600)
#define DELAY     (2)

/* ==========================================================================
 * The game: two physics balls steered by a "move" axis, "dash" scores a
 * random number of points. All state lives in registered objects.
 * ========================================================================== */

typedef struct
{
    LrgSimulation   *sim;
    LrgPhysicsWorld *world;
    LrgRigidBody    *balls[N_PLAYERS];
    LrgTimerManager *timers;
    gint64           score[N_PLAYERS];  /* lives in the score object below */
} Game;

/* The score is game state: a tiny LrgSimStateful wrapping Game.score */
#define DEMO_TYPE_SCORE (demo_score_get_type ())
G_DECLARE_FINAL_TYPE (DemoScore, demo_score, DEMO, SCORE, GObject)

struct _DemoScore
{
    GObject  parent_instance;
    Game    *game;
};

static void demo_score_stateful_init (LrgSimStatefulInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (DemoScore, demo_score, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SIM_STATEFUL,
                                                      demo_score_stateful_init))

static void
demo_score_save (LrgSimStateful *stateful,
                 LrgStateBuffer *buffer)
{
    DemoScore *self = DEMO_SCORE (stateful);
    guint p;

    for (p = 0; p < N_PLAYERS; p++)
        lrg_state_buffer_write_int64 (buffer, self->game->score[p]);
}

static gboolean
demo_score_load (LrgSimStateful  *stateful,
                 LrgStateBuffer  *buffer,
                 GError         **error)
{
    DemoScore *self = DEMO_SCORE (stateful);
    guint p;

    for (p = 0; p < N_PLAYERS; p++)
        self->game->score[p] = lrg_state_buffer_read_int64 (buffer);
    return lrg_state_buffer_check (buffer, error);
}

static void
demo_score_stateful_init (LrgSimStatefulInterface *iface)
{
    iface->save_state = demo_score_save;
    iface->load_state = demo_score_load;
}

static void
demo_score_class_init (DemoScoreClass *klass)
{
}

static void
demo_score_init (DemoScore *self)
{
}

static const gchar *const actions[] = { "dash", "move", NULL };

/* One tick: input steers, dashes score, physics and timers advance */
static void
game_tick (LrgSimulation *sim,
           guint64        tick,
           gdouble        step,
           Game          *game)
{
    guint p;

    for (p = 0; p < N_PLAYERS; p++)
    {
        gfloat vx;
        gfloat vy;

        lrg_rigid_body_get_velocity (game->balls[p], &vx, &vy);
        vx = 120.0f * lrg_simulation_get_axis (sim, p, "move");
        if (lrg_simulation_is_pressed (sim, p, "dash"))
        {
            vy = -250.0f;
            game->score[p] += 1 + lrg_random_stream_bounded (lrg_simulation_get_random (sim), 6);
        }
        lrg_rigid_body_set_velocity (game->balls[p], vx, vy);
    }
    lrg_physics_world_step (game->world, (gfloat) step);
    lrg_timer_manager_update (game->timers, step, step);
}

/* A bonus point to a random player every second */
static void
game_on_timeout (LrgTimerManager *timers,
                 guint64          id,
                 Game            *game)
{
    game->score[lrg_random_stream_bounded (lrg_simulation_get_random (game->sim), N_PLAYERS)]++;
}

static Game *
game_new (void)
{
    Game *game = g_new0 (Game, 1);
    g_autoptr(DemoScore) score = g_object_new (DEMO_TYPE_SCORE, NULL);
    g_autoptr(LrgRigidBody) floor_body = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    guint p;

    game->sim = lrg_simulation_new (20261029);
    lrg_simulation_set_n_players (game->sim, N_PLAYERS);
    lrg_simulation_set_actions (game->sim, actions);
    lrg_simulation_set_checksum_interval (game->sim, 30);

    game->world = lrg_physics_world_new ();
    lrg_physics_world_set_gravity (game->world, 0.0f, 500.0f);
    lrg_rigid_body_set_box_shape (floor_body, 1000.0f, 20.0f);
    lrg_rigid_body_set_position (floor_body, 500.0f, 400.0f);
    lrg_physics_world_add_body (game->world, floor_body);
    for (p = 0; p < N_PLAYERS; p++)
    {
        game->balls[p] = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);
        lrg_rigid_body_set_circle_shape (game->balls[p], 12.0f);
        lrg_rigid_body_set_position (game->balls[p], 300.0f + 400.0f * p, 200.0f);
        lrg_physics_world_add_body (game->world, game->balls[p]);
    }

    game->timers = lrg_timer_manager_new ();
    lrg_timer_manager_add (game->timers, 1.0, TRUE, FALSE);
    g_signal_connect (game->timers, "timeout", G_CALLBACK (game_on_timeout), game);

    score->game = game;
    lrg_simulation_add_object (game->sim, G_OBJECT (score));
    lrg_simulation_add_object (game->sim, G_OBJECT (game->world));
    lrg_simulation_add_object (game->sim, G_OBJECT (game->timers));
    g_signal_connect (game->sim, "tick", G_CALLBACK (game_tick), game);
    return game;
}

static void
game_free (Game *game)
{
    guint p;

    g_signal_handlers_disconnect_by_data (game->sim, game);
    g_signal_handlers_disconnect_by_data (game->timers, game);
    for (p = 0; p < N_PLAYERS; p++)
        g_clear_object (&game->balls[p]);
    g_clear_object (&game->sim);
    g_clear_object (&game->world);
    g_clear_object (&game->timers);
    g_free (game);
}

/* The scripted "players": a stick sweep and a dash now and then */
static LrgInputSnapshot *
script (guint  player,
        gint64 frame)
{
    LrgInputSnapshot *input = lrg_input_snapshot_new ();

    lrg_input_snapshot_set_down (input, 0, (frame + 17 * player) % 45 < 4);
    lrg_input_snapshot_set_down (input, 1, TRUE);
    lrg_input_snapshot_set_axis (input, 1, (frame / 60 + player) % 2 == 0 ? 0.75f : -0.75f);
    return input;
}

/* ==========================================================================
 * 1-2: record, save, load, play back headless
 * ========================================================================== */

static gboolean
record_and_replay (const gchar  *path,
                   guint64      *final_checksum,
                   GError      **error)
{
    g_autoptr(LrgReplay) recording = NULL;
    g_autoptr(LrgReplay) loaded = NULL;
    Game *game;
    Game *viewer;
    gint64 f;
    guint p;

    /* Record: the input each tick takes effect DELAY frames late, as it
     * will in the rollback session below, so the results compare */
    game = game_new ();
    lrg_simulation_start_recording (game->sim, "demo:ball-tag");
    for (f = 0; f < N_TICKS; f++)
    {
        LrgInputSnapshot *inputs[N_PLAYERS];

        for (p = 0; p < N_PLAYERS; p++)
            inputs[p] = f >= DELAY ? script (p, f - DELAY) : lrg_input_snapshot_new ();
        lrg_simulation_step (game->sim, inputs, N_PLAYERS);
        for (p = 0; p < N_PLAYERS; p++)
            lrg_input_snapshot_free (inputs[p]);
    }
    recording = lrg_simulation_stop_recording (game->sim);
    *final_checksum = lrg_simulation_checksum (game->sim);
    printf ("recorded  %u ticks, score %" G_GINT64_FORMAT ":%" G_GINT64_FORMAT
            ", checksum %016" G_GINT64_MODIFIER "x\n",
            lrg_replay_get_n_ticks (recording), game->score[0], game->score[1], *final_checksum);
    game_free (game);

    lrg_replay_set_metadata (recording, "example", "replay-demo");
    if (!lrg_replay_save_to_file (recording, path, error))
        return FALSE;

    /* Play back from the file on a brand-new game, as fast as possible */
    loaded = lrg_replay_load_from_file (path, error);
    if (loaded == NULL)
        return FALSE;
    viewer = game_new ();
    if (!lrg_replay_play (loaded, viewer->sim, error))
    {
        game_free (viewer);
        return FALSE;
    }
    printf ("replayed  %s: score %" G_GINT64_FORMAT ":%" G_GINT64_FORMAT
            ", checksum %016" G_GINT64_MODIFIER "x (%s)\n", path,
            viewer->score[0], viewer->score[1], lrg_simulation_checksum (viewer->sim),
            lrg_simulation_checksum (viewer->sim) == *final_checksum ? "identical" : "DIFFERENT");
    game_free (viewer);
    return TRUE;
}

/* ==========================================================================
 * 3: two rollback peers over a bad network
 * ========================================================================== */

static gboolean
rollback_match (guint64   expected,
                GError  **error)
{
    g_autoptr(LrgLoopbackNetwork) network = lrg_loopback_network_new (7);
    LrgLoopbackTransport *transports[N_PLAYERS];
    LrgRollbackSession *sessions[N_PLAYERS];
    Game *games[N_PLAYERS];
    gint64 next[N_PLAYERS] = { 0, 0 };
    gboolean ok = TRUE;
    guint frame;
    guint p;

    lrg_loopback_network_set_latency (network, 0.07, 0.03);
    lrg_loopback_network_set_loss (network, 0.10, 0.02);
    for (p = 0; p < N_PLAYERS; p++)
    {
        games[p] = game_new ();
        transports[p] = lrg_loopback_network_create_endpoint (network, p);
        sessions[p] = lrg_rollback_session_new (games[p]->sim, LRG_ROLLBACK_TRANSPORT (transports[p]), p);
        lrg_rollback_session_set_input_delay (sessions[p], DELAY);
        lrg_rollback_session_set_checksum_interval (sessions[p], 30);
    }

    /* Both peers run at 60 frames a second of network time */
    for (frame = 0; frame < 4 * N_TICKS && ok; frame++)
    {
        for (p = 0; p < N_PLAYERS && ok; p++)
        {
            if (next[p] < N_TICKS)
            {
                g_autoptr(LrgInputSnapshot) input = script (p, next[p]);

                if (lrg_rollback_session_advance_frame (sessions[p], input, error))
                    next[p]++;
                else if (error != NULL && *error != NULL)
                    ok = FALSE;
            }
            else
                ok = lrg_rollback_session_poll (sessions[p], error);
        }
        lrg_loopback_network_advance (network, 1.0 / 60.0);
        if (lrg_rollback_session_get_last_verified_frame (sessions[0]) >= N_TICKS &&
            lrg_rollback_session_get_last_verified_frame (sessions[1]) >= N_TICKS)
            break;
    }

    for (p = 0; p < N_PLAYERS && ok; p++)
    {
        guint64 checksum = 0;

        lrg_rollback_session_lookup_checksum (sessions[p], N_TICKS, &checksum);
        printf ("peer %u    %u rollbacks (%u frames re-simulated), score %" G_GINT64_FORMAT
                ":%" G_GINT64_FORMAT ", checksum %016" G_GINT64_MODIFIER "x (%s)\n", p,
                lrg_rollback_session_get_rollback_count (sessions[p]),
                lrg_rollback_session_get_resimulated_frames (sessions[p]),
                games[p]->score[0], games[p]->score[1], checksum,
                checksum == expected ? "matches the recording" : "DIFFERENT");
        if (checksum != expected)
            ok = FALSE;
    }
    printf ("network   %u datagrams dropped\n", lrg_loopback_network_get_n_dropped (network));

    for (p = 0; p < N_PLAYERS; p++)
    {
        g_object_unref (sessions[p]);
        g_object_unref (transports[p]);
        game_free (games[p]);
    }
    if (!ok && error != NULL && *error == NULL)
        g_set_error_literal (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_DESYNC,
                             "Peers did not converge on the recorded state");
    return ok;
}

int
main (int   argc,
      char *argv[])
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;
    guint64 recorded = 0;

    path = g_strdup (argc > 1 ? argv[1] : "replay-demo.lrgreplay");
    if (!record_and_replay (path, &recorded, &error) ||
        !rollback_match (recorded, &error))
    {
        fprintf (stderr, "replay-demo: %s\n", error->message);
        return 1;
    }
    return 0;
}
