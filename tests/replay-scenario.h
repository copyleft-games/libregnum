/* replay-scenario.h - Shared deterministic scenario for the replay tests
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A small game that exercises every part of the determinism contract:
 * a physics world (falling boxes on a static floor), gameplay timers on an
 * LrgTimerManager, the simulation random stream, and per-player action
 * input (a "jump" button and a signed "move" axis). Included by
 * test-replay.c and test-rollback.c.
 */

#pragma once

#include <glib.h>
#include <libregnum.h>

#define SCENARIO_MAX_PLAYERS (4)
#define SCENARIO_STEP        (1.0 / 60.0)

/* ==========================================================================
 * Arena: the game-specific state, an LrgSimStateful
 * ========================================================================== */

#define TEST_TYPE_ARENA (test_arena_get_type ())
G_DECLARE_FINAL_TYPE (TestArena, test_arena, TEST, ARENA, GObject)

struct _TestArena
{
    GObject  parent_instance;

    gint64   score;
    gfloat   x[SCENARIO_MAX_PLAYERS];
    guint    timer_fires;
    gint64   bug_at_tick;    /* >= 0: corrupt the state at that tick */
};

static void test_arena_stateful_init (LrgSimStatefulInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (TestArena, test_arena, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SIM_STATEFUL,
                                                      test_arena_stateful_init))

static const gchar *
test_arena_get_state_id (LrgSimStateful *stateful)
{
    return "arena";
}

static void
test_arena_save_state (LrgSimStateful *stateful,
                       LrgStateBuffer *buffer)
{
    TestArena *self = TEST_ARENA (stateful);
    guint i;

    lrg_state_buffer_write_int64 (buffer, self->score);
    lrg_state_buffer_write_uint32 (buffer, self->timer_fires);
    for (i = 0; i < SCENARIO_MAX_PLAYERS; i++)
        lrg_state_buffer_write_float (buffer, self->x[i]);
}

static gboolean
test_arena_load_state (LrgSimStateful  *stateful,
                       LrgStateBuffer  *buffer,
                       GError         **error)
{
    TestArena *self = TEST_ARENA (stateful);
    guint i;

    self->score = lrg_state_buffer_read_int64 (buffer);
    self->timer_fires = lrg_state_buffer_read_uint32 (buffer);
    for (i = 0; i < SCENARIO_MAX_PLAYERS; i++)
        self->x[i] = lrg_state_buffer_read_float (buffer);
    return lrg_state_buffer_check (buffer, error);
}

static void
test_arena_stateful_init (LrgSimStatefulInterface *iface)
{
    iface->get_state_id = test_arena_get_state_id;
    iface->save_state = test_arena_save_state;
    iface->load_state = test_arena_load_state;
}

static void
test_arena_class_init (TestArenaClass *klass)
{
}

static void
test_arena_init (TestArena *self)
{
    self->bug_at_tick = -1;
}

/* ==========================================================================
 * Scenario: simulation + arena + physics + timers
 * ========================================================================== */

typedef struct
{
    LrgSimulation   *simulation;
    TestArena       *arena;
    LrgPhysicsWorld *world;
    LrgTimerManager *timers;
    GPtrArray       *bodies;    /* one dynamic box per player, then crates */
} Scenario;

static const gchar *const scenario_actions[] = { "jump", "move", NULL };

/*
 * scenario_on_timeout:
 *
 * A repeating timer kicks a random crate with a random impulse: timers and
 * the random stream both feed the physics.
 */
static void
scenario_on_timeout (LrgTimerManager *timers,
                     guint64          id,
                     Scenario        *scenario)
{
    LrgRandomStream *random = lrg_simulation_get_random (scenario->simulation);
    LrgRigidBody *body;

    scenario->arena->timer_fires++;
    body = g_ptr_array_index (scenario->bodies,
                              lrg_random_stream_bounded (random, scenario->bodies->len));
    lrg_rigid_body_set_velocity (body,
                                 (gfloat) (lrg_random_stream_next_double (random) * 80.0 - 40.0),
                                 -(gfloat) (lrg_random_stream_next_double (random) * 120.0));
}

/*
 * scenario_on_tick:
 *
 * One tick of the game: player input moves and launches each player's box,
 * jumps score random points, then physics and timers advance one step.
 */
static void
scenario_on_tick (LrgSimulation *simulation,
                  guint64        tick,
                  gdouble        step,
                  Scenario      *scenario)
{
    LrgRandomStream *random = lrg_simulation_get_random (simulation);
    guint n_players = lrg_simulation_get_n_players (simulation);
    guint p;

    for (p = 0; p < n_players; p++)
    {
        LrgRigidBody *body = g_ptr_array_index (scenario->bodies, p);
        gfloat vx;
        gfloat vy;

        scenario->arena->x[p] += lrg_simulation_get_axis (simulation, p, "move") * 3.0f;
        lrg_rigid_body_get_velocity (body, &vx, &vy);
        vx = lrg_simulation_get_axis (simulation, p, "move") * 50.0f;
        if (lrg_simulation_is_pressed (simulation, p, "jump"))
        {
            vy = -200.0f;
            scenario->arena->score += 1 + lrg_random_stream_bounded (random, 10);
        }
        lrg_rigid_body_set_velocity (body, vx, vy);
    }

    if (scenario->arena->bug_at_tick >= 0 && (gint64) tick == scenario->arena->bug_at_tick)
        scenario->arena->score += 1000;

    lrg_physics_world_step (scenario->world, (gfloat) step);
    lrg_timer_manager_update (scenario->timers, step, step);
}

static Scenario *
scenario_new (guint64 seed,
              guint   n_players)
{
    Scenario *scenario;
    LrgRigidBody *floor_body;
    guint i;

    scenario = g_new0 (Scenario, 1);
    scenario->simulation = lrg_simulation_new (seed);
    lrg_simulation_set_n_players (scenario->simulation, n_players);
    lrg_simulation_set_actions (scenario->simulation, scenario_actions);
    lrg_simulation_set_fixed_step (scenario->simulation, SCENARIO_STEP);

    scenario->arena = g_object_new (TEST_TYPE_ARENA, NULL);
    scenario->timers = lrg_timer_manager_new ();
    scenario->world = lrg_physics_world_new ();
    lrg_physics_world_set_gravity (scenario->world, 0.0f, 400.0f);
    lrg_physics_world_set_time_step (scenario->world, (gfloat) SCENARIO_STEP);
    scenario->bodies = g_ptr_array_new_with_free_func (g_object_unref);

    /* A static floor and one box per player plus three crates */
    floor_body = lrg_rigid_body_new (LRG_RIGID_BODY_STATIC);
    lrg_rigid_body_set_box_shape (floor_body, 800.0f, 20.0f);
    lrg_rigid_body_set_position (floor_body, 400.0f, 500.0f);
    lrg_physics_world_add_body (scenario->world, floor_body);
    g_object_unref (floor_body);
    for (i = 0; i < n_players + 3; i++)
    {
        LrgRigidBody *body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

        lrg_rigid_body_set_box_shape (body, 16.0f, 16.0f);
        lrg_rigid_body_set_position (body, 100.0f + 60.0f * i, 100.0f + 7.0f * i);
        lrg_physics_world_add_body (scenario->world, body);
        g_ptr_array_add (scenario->bodies, body);
    }

    /* Two timers: a kick every 0.25 s, a one-shot at 1 s */
    lrg_timer_manager_add (scenario->timers, 0.25, TRUE, FALSE);
    lrg_timer_manager_add (scenario->timers, 1.0, FALSE, FALSE);
    g_signal_connect (scenario->timers, "timeout", G_CALLBACK (scenario_on_timeout), scenario);

    /* Registration order is serialization order */
    lrg_simulation_add_object (scenario->simulation, G_OBJECT (scenario->arena));
    lrg_simulation_add_object (scenario->simulation, G_OBJECT (scenario->world));
    lrg_simulation_add_object (scenario->simulation, G_OBJECT (scenario->timers));
    g_signal_connect (scenario->simulation, "tick", G_CALLBACK (scenario_on_tick), scenario);

    return scenario;
}

static void
scenario_free (Scenario *scenario)
{
    g_signal_handlers_disconnect_by_data (scenario->timers, scenario);
    g_signal_handlers_disconnect_by_data (scenario->simulation, scenario);
    g_clear_object (&scenario->simulation);
    g_clear_object (&scenario->arena);
    g_clear_object (&scenario->world);
    g_clear_object (&scenario->timers);
    g_clear_pointer (&scenario->bodies, g_ptr_array_unref);
    g_free (scenario);
}

/*
 * scripted_input:
 *
 * The input player @player gives at @tick in the scripted runs: jump held
 * for a few ticks now and then, a move axis sweeping between -1 and 1.
 */
G_GNUC_UNUSED static LrgInputSnapshot *
scripted_input (guint  player,
                gint64 tick)
{
    LrgInputSnapshot *input = lrg_input_snapshot_new ();
    gint64 phase = (tick + 13 * (gint64) player) % 40;

    lrg_input_snapshot_set_down (input, 0, ((tick / 7) + player) % 5 == 0);
    lrg_input_snapshot_set_axis (input, 1, (gfloat) (phase - 20) / 20.0f);
    lrg_input_snapshot_set_down (input, 1, phase != 20);
    return input;
}

/*
 * scenario_run:
 *
 * Steps @n_ticks scripted ticks; when @checksums is set, appends the state
 * checksum after every tick.
 */
G_GNUC_UNUSED static void
scenario_run (Scenario *scenario,
              guint     n_ticks,
              GArray   *checksums)
{
    guint n_players = lrg_simulation_get_n_players (scenario->simulation);
    guint t;
    guint p;

    for (t = 0; t < n_ticks; t++)
    {
        LrgInputSnapshot *inputs[SCENARIO_MAX_PLAYERS];
        gint64 tick = (gint64) lrg_simulation_get_tick (scenario->simulation);
        guint64 checksum;

        for (p = 0; p < n_players; p++)
            inputs[p] = scripted_input (p, tick);
        lrg_simulation_step (scenario->simulation, inputs, n_players);
        for (p = 0; p < n_players; p++)
            lrg_input_snapshot_free (inputs[p]);

        if (checksums != NULL)
        {
            checksum = lrg_simulation_checksum (scenario->simulation);
            g_array_append_val (checksums, checksum);
        }
    }
}
