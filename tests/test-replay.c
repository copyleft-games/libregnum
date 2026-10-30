/* test-replay.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Determinism layer: state buffers, input snapshots and their injection
 * into the input layer, deterministic simulation, save/load, replay
 * recording and playback (file round trip, desync detection, corrupt
 * files), the golden-file regression check, and the template's
 * record/headless-playback path.
 */

#include "replay-scenario.h"

#include <glib/gstdio.h>
#include <string.h>

#define GOLDEN_NAME "replay-physics-timers-rng.golden"

/* ==========================================================================
 * State buffer
 * ========================================================================== */

static void
test_state_buffer_round_trip (void)
{
    g_autoptr(LrgStateBuffer) buffer = NULL;
    g_autoptr(LrgStateBuffer) reader = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GBytes) block = NULL;
    g_autofree gchar *text = NULL;
    g_autofree gchar *null_text = NULL;
    g_autoptr(GError) error = NULL;
    gfloat negative_zero = -0.0f;
    gfloat read_zero;
    guint8 raw[3] = { 1, 2, 3 };

    buffer = lrg_state_buffer_new ();
    lrg_state_buffer_write_uint8 (buffer, 0xab);
    lrg_state_buffer_write_boolean (buffer, TRUE);
    lrg_state_buffer_write_uint32 (buffer, 0xdeadbeef);
    lrg_state_buffer_write_int32 (buffer, -5);
    lrg_state_buffer_write_uint64 (buffer, G_GUINT64_CONSTANT (0x0123456789abcdef));
    lrg_state_buffer_write_int64 (buffer, G_MININT64);
    lrg_state_buffer_write_float (buffer, negative_zero);
    lrg_state_buffer_write_double (buffer, 1.0 / 3.0);
    lrg_state_buffer_write_string (buffer, "héllo");
    lrg_state_buffer_write_string (buffer, NULL);
    lrg_state_buffer_write_data (buffer, raw, sizeof (raw));

    /* Little-endian layout is part of the format */
    g_assert_cmpuint (lrg_state_buffer_get_data (buffer, NULL)[2], ==, 0xef);

    bytes = lrg_state_buffer_to_bytes (buffer);
    reader = lrg_state_buffer_new_from_bytes (bytes);
    g_assert_cmpuint (lrg_state_buffer_read_uint8 (reader), ==, 0xab);
    g_assert_true (lrg_state_buffer_read_boolean (reader));
    g_assert_cmpuint (lrg_state_buffer_read_uint32 (reader), ==, 0xdeadbeef);
    g_assert_cmpint (lrg_state_buffer_read_int32 (reader), ==, -5);
    g_assert_cmpuint (lrg_state_buffer_read_uint64 (reader), ==, G_GUINT64_CONSTANT (0x0123456789abcdef));
    g_assert_cmpint (lrg_state_buffer_read_int64 (reader), ==, G_MININT64);
    read_zero = lrg_state_buffer_read_float (reader);
    g_assert_cmpmem (&read_zero, sizeof (gfloat), &negative_zero, sizeof (gfloat));
    g_assert_cmpfloat (lrg_state_buffer_read_double (reader), ==, 1.0 / 3.0);
    text = lrg_state_buffer_read_string (reader);
    g_assert_cmpstr (text, ==, "héllo");
    null_text = lrg_state_buffer_read_string (reader);
    g_assert_null (null_text);
    block = lrg_state_buffer_read_data (reader);
    g_assert_cmpmem (g_bytes_get_data (block, NULL), g_bytes_get_size (block), raw, sizeof (raw));
    g_assert_cmpuint (lrg_state_buffer_get_remaining (reader), ==, 0);
    g_assert_true (lrg_state_buffer_check (reader, &error));

    /* Reading past the end yields zero and is reported */
    g_assert_cmpuint (lrg_state_buffer_read_uint64 (reader), ==, 0);
    g_assert_false (lrg_state_buffer_check (reader, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

    /* FNV-1a 64: the empty string hashes to the offset basis, "a" to the
     * published test vector */
    g_assert_cmpuint (lrg_state_hash_data (NULL, 0, 0), ==, G_GUINT64_CONSTANT (0xcbf29ce484222325));
    g_assert_cmpuint (lrg_state_hash_data ((const guint8 *) "a", 1, 0), ==,
                      G_GUINT64_CONSTANT (0xaf63dc4c8601ec8c));
    g_assert_cmpuint (lrg_state_buffer_checksum (buffer), ==, lrg_state_buffer_checksum (reader));
}

/* ==========================================================================
 * Input snapshots and the input layer
 * ========================================================================== */

static void
test_input_snapshot_basics (void)
{
    g_autoptr(LrgInputSnapshot) a = NULL;
    g_autoptr(LrgInputSnapshot) b = NULL;
    g_autoptr(LrgInputSnapshot) c = NULL;
    g_autoptr(LrgStateBuffer) buffer = NULL;

    a = lrg_input_snapshot_new ();
    lrg_input_snapshot_set_down (a, 0, TRUE);
    lrg_input_snapshot_set_down (a, 63, TRUE);
    lrg_input_snapshot_set_axis (a, 0, 1.0f);    /* implied by the held bit */
    lrg_input_snapshot_set_axis (a, 5, -0.25f);  /* explicit */
    g_assert_true (lrg_input_snapshot_get_down (a, 63));
    g_assert_false (lrg_input_snapshot_get_down (a, 5));
    g_assert_cmpfloat (lrg_input_snapshot_get_axis (a, 0), ==, 1.0f);
    g_assert_cmpfloat (lrg_input_snapshot_get_axis (a, 5), ==, -0.25f);
    g_assert_cmpfloat (lrg_input_snapshot_get_axis (a, 7), ==, 0.0f);

    /* Compact encoding: two masks plus one explicit float */
    buffer = lrg_state_buffer_new ();
    lrg_input_snapshot_write (a, buffer);
    g_assert_cmpuint (lrg_state_buffer_get_size (buffer), ==, 8 + 8 + 4);
    b = lrg_input_snapshot_new ();
    g_assert_true (lrg_input_snapshot_read (b, buffer));
    g_assert_true (lrg_input_snapshot_equal (a, b));

    c = lrg_input_snapshot_copy (a);
    lrg_input_snapshot_set_axis (c, 5, -0.5f);
    g_assert_false (lrg_input_snapshot_equal (a, c));
    lrg_input_snapshot_clear (c);
    g_assert_cmpuint (lrg_input_snapshot_get_down_mask (c), ==, 0);
}

static void
test_input_snapshot_inject (void)
{
    g_autoptr(LrgInputMap) map = NULL;
    g_autoptr(LrgInputAction) jump = NULL;
    g_autoptr(LrgInputAction) move = NULL;
    g_autoptr(LrgInputSnapshot) first = NULL;
    g_autoptr(LrgInputSnapshot) second = NULL;
    g_autoptr(LrgInputSnapshot) captured = NULL;

    map = lrg_input_map_new ();
    jump = lrg_input_action_new ("jump");
    move = lrg_input_action_new ("move");
    lrg_input_map_add_action (map, jump);
    lrg_input_map_add_action (map, move);

    first = lrg_input_snapshot_new ();
    lrg_input_snapshot_set_down (first, 0, TRUE);
    lrg_input_snapshot_set_axis (first, 1, -0.5f);

    /* Pressed on the first tick it is held... */
    lrg_input_snapshot_apply (first, NULL, map, scenario_actions);
    g_assert_true (lrg_input_map_is_down (map, "jump"));
    g_assert_true (lrg_input_map_is_pressed (map, "jump"));
    g_assert_false (lrg_input_map_is_released (map, "jump"));
    g_assert_cmpfloat (lrg_input_map_get_axis (map, "move"), ==, -0.5f);
    g_assert_cmpfloat (lrg_input_map_get_value (map, "move"), ==, 0.5f);

    /* ...a repeated snapshot holds without re-pressing (prediction)... */
    lrg_input_snapshot_apply (first, first, map, scenario_actions);
    g_assert_true (lrg_input_map_is_down (map, "jump"));
    g_assert_false (lrg_input_map_is_pressed (map, "jump"));

    /* ...and letting go is a release */
    second = lrg_input_snapshot_new ();
    lrg_input_snapshot_apply (second, first, map, scenario_actions);
    g_assert_false (lrg_input_map_is_down (map, "jump"));
    g_assert_true (lrg_input_map_is_released (map, "jump"));

    /* Capture reads what the actions report (here: the injected state) */
    lrg_input_snapshot_apply (first, NULL, map, scenario_actions);
    captured = lrg_input_snapshot_capture (map, scenario_actions);
    g_assert_true (lrg_input_snapshot_equal (captured, first));

    /* Cleared overrides fall back to the (unbound) live input */
    lrg_input_map_clear_overrides (map);
    g_assert_false (lrg_input_action_has_override (lrg_input_map_get_action (map, "jump")));
    g_assert_false (lrg_input_map_is_down (map, "jump"));
}

/* ==========================================================================
 * Simulation
 * ========================================================================== */

static void
test_simulation_determinism (void)
{
    g_autoptr(GArray) first = g_array_new (FALSE, FALSE, sizeof (guint64));
    g_autoptr(GArray) second = g_array_new (FALSE, FALSE, sizeof (guint64));
    g_autoptr(GArray) other_seed = g_array_new (FALSE, FALSE, sizeof (guint64));
    Scenario *a;
    Scenario *b;
    Scenario *c;

    /* Same seed, same inputs: identical state after every tick */
    a = scenario_new (42, 2);
    b = scenario_new (42, 2);
    scenario_run (a, 600, first);
    scenario_run (b, 600, second);
    g_assert_cmpuint (first->len, ==, 600);
    g_assert_cmpmem (first->data, first->len * sizeof (guint64),
                     second->data, second->len * sizeof (guint64));
    g_assert_cmpuint (lrg_simulation_get_tick (a->simulation), ==, 600);

    /* The scenario actually exercised timers, randomness and input */
    g_assert_cmpuint (a->arena->timer_fires, >, 20);
    g_assert_cmpint (a->arena->score, >, 0);

    /* Another seed diverges */
    c = scenario_new (43, 2);
    scenario_run (c, 600, other_seed);
    g_assert_cmpuint (g_array_index (first, guint64, 599), !=,
                      g_array_index (other_seed, guint64, 599));

    scenario_free (a);
    scenario_free (b);
    scenario_free (c);
}

static void
test_simulation_save_load (void)
{
    g_autoptr(GBytes) saved = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GArray) ahead = g_array_new (FALSE, FALSE, sizeof (guint64));
    g_autoptr(GArray) again = g_array_new (FALSE, FALSE, sizeof (guint64));
    Scenario *scenario;
    guint64 at_save;

    scenario = scenario_new (7, 2);
    scenario_run (scenario, 100, NULL);
    saved = lrg_simulation_save_bytes (scenario->simulation);
    at_save = lrg_simulation_checksum (scenario->simulation);

    /* Run ahead, rewind, run the same ticks again: identical */
    scenario_run (scenario, 50, ahead);
    g_assert_true (lrg_simulation_load_bytes (scenario->simulation, saved, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_simulation_get_tick (scenario->simulation), ==, 100);
    g_assert_cmpuint (lrg_simulation_checksum (scenario->simulation), ==, at_save);
    scenario_run (scenario, 50, again);
    g_assert_cmpmem (ahead->data, ahead->len * sizeof (guint64),
                     again->data, again->len * sizeof (guint64));

    /* A state from a differently built simulation is refused */
    lrg_simulation_remove_object (scenario->simulation, G_OBJECT (scenario->timers));
    g_assert_false (lrg_simulation_load_bytes (scenario->simulation, saved, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

    scenario_free (scenario);
}

static void
on_desync (LrgSimulation *simulation,
           guint64        tick,
           guint64        local,
           guint64        expected,
           guint         *count)
{
    (*count)++;
}

static void
test_simulation_desync_detector (void)
{
    Scenario *scenario;
    guint64 checksum = 0;
    guint desyncs = 0;

    scenario = scenario_new (1, 1);
    lrg_simulation_set_checksum_interval (scenario->simulation, 5);
    g_signal_connect (scenario->simulation, "desync", G_CALLBACK (on_desync), &desyncs);
    scenario_run (scenario, 20, NULL);

    g_assert_true (lrg_simulation_lookup_checksum (scenario->simulation, 10, &checksum));
    g_assert_false (lrg_simulation_lookup_checksum (scenario->simulation, 11, NULL));
    g_assert_true (lrg_simulation_verify_checksum (scenario->simulation, 10, checksum));
    g_assert_true (lrg_simulation_verify_checksum (scenario->simulation, 999, 1));  /* unknown */
    g_assert_cmpint (lrg_simulation_get_first_desync_tick (scenario->simulation), ==, -1);
    g_assert_false (lrg_simulation_verify_checksum (scenario->simulation, 15, checksum));
    g_assert_cmpuint (desyncs, ==, 1);
    g_assert_cmpint (lrg_simulation_get_first_desync_tick (scenario->simulation), ==, 15);

    scenario_free (scenario);
}

/* ==========================================================================
 * Replays
 * ========================================================================== */

/* Records @n_ticks scripted ticks of a fresh scenario */
static LrgReplay *
record_scenario (guint64 seed,
                 guint   n_players,
                 guint   n_ticks,
                 guint   checksum_interval,
                 guint64 *final_checksum)
{
    Scenario *scenario;
    LrgReplay *replay;

    scenario = scenario_new (seed, n_players);
    lrg_simulation_set_checksum_interval (scenario->simulation, checksum_interval);
    lrg_simulation_start_recording (scenario->simulation, "scenario:arena");
    scenario_run (scenario, n_ticks, NULL);
    replay = lrg_simulation_stop_recording (scenario->simulation);
    if (final_checksum != NULL)
        *final_checksum = lrg_simulation_checksum (scenario->simulation);
    scenario_free (scenario);
    return replay;
}

static void
test_replay_round_trip (void)
{
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(LrgReplay) loaded = NULL;
    g_autoptr(LrgReplay) from_file = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    const gchar *data;
    Scenario *scenario;
    guint64 recorded_final = 0;
    guint64 checksum = 0;
    guint t;
    guint p;

    replay = record_scenario (99, 2, 300, 10, &recorded_final);
    g_assert_cmpuint (lrg_replay_get_n_ticks (replay), ==, 300);
    lrg_replay_set_metadata (replay, "game", "test \"arena\"\n");

    /* Encode: a YAML header, then a compact delta-encoded body */
    bytes = lrg_replay_to_bytes (replay);
    data = g_bytes_get_data (bytes, NULL);
    g_assert_true (g_str_has_prefix (data, "%YAML 1.2\n---\nformat: lrg-replay\nversion: 1\n"));
    g_assert_nonnull (g_strstr_len (data, -1, "actions:\n  - \"jump\"\n  - \"move\"\n"));
    /* The scripted stick moves every tick: at most a full snapshot (two
     * masks and one float) plus two flag bytes per player and tick */
    g_assert_cmpuint (g_bytes_get_size (bytes), <, 1024 + 300 * (1 + 2 * (1 + 8 + 8 + 4)) + 30 * 8);

    /* Decode: identical header, inputs and checksums */
    loaded = lrg_replay_load_from_bytes (bytes, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_replay_get_seed (loaded), ==, 99);
    g_assert_cmpfloat (lrg_replay_get_fixed_step (loaded), ==, SCENARIO_STEP);
    g_assert_cmpuint (lrg_replay_get_n_players (loaded), ==, 2);
    g_assert_cmpuint (lrg_replay_get_n_ticks (loaded), ==, 300);
    g_assert_cmpstr (lrg_replay_get_initial_state (loaded), ==, "scenario:arena");
    g_assert_cmpstr (lrg_replay_get_metadata (loaded, "game"), ==, "test \"arena\"\n");
    g_assert_cmpstrv (lrg_replay_get_actions (loaded), scenario_actions);
    for (t = 0; t < 300; t++)
    {
        guint64 a = 0;
        guint64 b = 0;

        for (p = 0; p < 2; p++)
            g_assert_true (lrg_input_snapshot_equal (lrg_replay_get_input (replay, t, p),
                                                     lrg_replay_get_input (loaded, t, p)));
        g_assert_cmpint (lrg_replay_get_checksum (replay, t, &a), ==,
                         lrg_replay_get_checksum (loaded, t, &b));
        g_assert_cmpuint (a, ==, b);
    }
    g_assert_true (lrg_replay_get_checksum (loaded, 9, NULL));
    g_assert_false (lrg_replay_get_checksum (loaded, 10, NULL));

    /* Through a file too */
    dir = g_dir_make_tmp ("lrg-replay-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "round-trip.lrgreplay", NULL);
    g_assert_true (lrg_replay_save_to_file (loaded, path, &error));
    from_file = lrg_replay_load_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_replay_get_n_ticks (from_file), ==, 300);

    /* Playback on a fresh scenario reproduces the recorded run */
    scenario = scenario_new (99, 2);
    g_assert_true (lrg_replay_play (from_file, scenario->simulation, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_simulation_get_tick (scenario->simulation), ==, 300);
    checksum = lrg_simulation_checksum (scenario->simulation);
    g_assert_cmpuint (checksum, ==, recorded_final);
    scenario_free (scenario);

    g_unlink (path);
    g_rmdir (dir);
}

static void
test_replay_detects_desync (void)
{
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(GError) error = NULL;
    Scenario *scenario;

    replay = record_scenario (5, 1, 200, 10, NULL);

    /* A simulation that diverges at tick 123 fails at the next checksum */
    scenario = scenario_new (5, 1);
    scenario->arena->bug_at_tick = 123;
    g_assert_false (lrg_replay_play (replay, scenario->simulation, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_DESYNC);
    g_assert_cmpint (lrg_simulation_get_first_desync_tick (scenario->simulation), ==, 130);
    scenario_free (scenario);
}

static void
test_replay_rejects_bad_input (void)
{
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(LrgReplay) loaded = NULL;
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GBytes) corrupt = NULL;
    g_autoptr(GBytes) garbage = NULL;
    g_autoptr(GError) error = NULL;
    gconstpointer data;
    guint8 *copy;
    gsize size;
    Scenario *scenario;

    replay = record_scenario (11, 1, 60, 0, NULL);
    bytes = lrg_replay_to_bytes (replay);

    /* A flipped body byte fails the body checksum */
    data = g_bytes_get_data (bytes, &size);
    copy = g_memdup2 (data, size);
    copy[size - 3] ^= 0x40;
    corrupt = g_bytes_new_take (copy, size);
    loaded = lrg_replay_load_from_bytes (corrupt, &error);
    g_assert_null (loaded);
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE);
    g_clear_error (&error);

    /* Not a replay at all */
    garbage = g_bytes_new_static ("hello\n...\nworld", 15);
    g_assert_null (lrg_replay_load_from_bytes (garbage, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_PARSE);
    g_clear_error (&error);

    /* Another seed, or a simulation not at the start, is incompatible */
    scenario = scenario_new (12, 1);
    g_assert_false (lrg_replay_play (replay, scenario->simulation, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INCOMPATIBLE);
    g_clear_error (&error);
    scenario_free (scenario);

    scenario = scenario_new (11, 1);
    scenario_run (scenario, 1, NULL);
    g_assert_false (lrg_replay_play (replay, scenario->simulation, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_INITIAL_STATE);
    scenario_free (scenario);
}

/* ==========================================================================
 * Golden file
 * ========================================================================== */

/*
 * The golden regression test: a 2-player, 600-tick scripted session of
 * physics + timers + randomness, recorded, then played back headless and
 * compared every 60 ticks with tests/golden/. A change to any of those
 * systems that alters simulation results fails here; if the change is
 * intended, regenerate with LRG_UPDATE_GOLDEN=1 and review the diff.
 */
static void
test_replay_golden (void)
{
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *golden = NULL;
    Scenario *scenario;

    golden = g_test_build_filename (G_TEST_DIST, "golden", GOLDEN_NAME, NULL);
    replay = record_scenario (2026, 2, 600, 30, NULL);

    scenario = scenario_new (2026, 2);
    g_assert_true (lrg_replay_check_golden (replay, scenario->simulation, golden, 60, &error));
    g_assert_no_error (error);
    scenario_free (scenario);

    /* The same replay against a diverging simulation fails the check (or,
     * with checksums recorded, the replay's own desync detector) */
    if (g_getenv ("LRG_UPDATE_GOLDEN") != NULL)
        return;
    scenario = scenario_new (2026, 2);
    scenario->arena->bug_at_tick = 400;
    g_assert_false (lrg_replay_check_golden (replay, scenario->simulation, golden, 60, &error));
    g_assert_true (g_error_matches (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_DESYNC) ||
                   g_error_matches (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_GOLDEN_MISMATCH));
    scenario_free (scenario);
}

static void
test_replay_golden_mismatch (void)
{
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *contents = NULL;
    Scenario *scenario;

    if (g_getenv ("LRG_UPDATE_GOLDEN") != NULL)
    {
        g_test_skip ("LRG_UPDATE_GOLDEN is set");
        return;
    }

    /* No checksums in the replay, so only the golden file can catch it */
    replay = record_scenario (3, 1, 120, 0, NULL);
    dir = g_dir_make_tmp ("lrg-golden-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "missing.golden", NULL);

    scenario = scenario_new (3, 1);
    g_assert_false (lrg_replay_check_golden (replay, scenario->simulation, path, 30, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_NOT_FOUND);
    g_clear_error (&error);
    scenario_free (scenario);

    /* Write a golden for a run where the bug fires, then check the clean run */
    g_setenv ("LRG_UPDATE_GOLDEN", "1", TRUE);
    scenario = scenario_new (3, 1);
    scenario->arena->bug_at_tick = 70;
    g_assert_true (lrg_replay_check_golden (replay, scenario->simulation, path, 30, &error));
    g_unsetenv ("LRG_UPDATE_GOLDEN");
    scenario_free (scenario);
    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
    g_assert_true (g_str_has_prefix (contents, "%YAML 1.2\n---\nformat: lrg-golden\n"));

    scenario = scenario_new (3, 1);
    g_assert_false (lrg_replay_check_golden (replay, scenario->simulation, path, 30, &error));
    g_assert_error (error, LRG_REPLAY_ERROR, LRG_REPLAY_ERROR_GOLDEN_MISMATCH);
    g_assert_nonnull (strstr (error->message, "tick 90"));
    scenario_free (scenario);

    g_unlink (path);
    g_rmdir (dir);
}

/* ==========================================================================
 * Template: record live input, play back headless
 * ========================================================================== */

/* An empty playing state: the game logic lives in fixed_update */
typedef LrgGameState TestPlayState;
typedef LrgGameStateClass TestPlayStateClass;
GType test_play_state_get_type (void);
G_DEFINE_TYPE (TestPlayState, test_play_state, LRG_TYPE_GAME_STATE)

static void
test_play_state_noop (LrgGameState *state)
{
}

static void
test_play_state_update (LrgGameState *state,
                        gdouble       delta)
{
}

static void
test_play_state_class_init (TestPlayStateClass *klass)
{
    klass->enter = test_play_state_noop;
    klass->exit = test_play_state_noop;
    klass->draw = test_play_state_noop;
    klass->update = test_play_state_update;
}

static void
test_play_state_init (TestPlayState *self)
{
}

#define TEST_TYPE_REPLAY_GAME (test_replay_game_get_type ())
G_DECLARE_FINAL_TYPE (TestReplayGame, test_replay_game, TEST, REPLAY_GAME, LrgGameTemplate)

struct _TestReplayGame
{
    LrgGameTemplate  parent_instance;

    TestArena       *arena;
    guint            jumps;
};

G_DEFINE_FINAL_TYPE (TestReplayGame, test_replay_game, LRG_TYPE_GAME_TEMPLATE)

static void
test_replay_game_setup_default_input (LrgGameTemplate *template,
                                      LrgInputMap     *map)
{
    g_autoptr(LrgInputBinding) space = NULL;
    g_autoptr(LrgInputBinding) stick = NULL;
    g_autoptr(LrgInputAction) jump = lrg_input_action_new ("jump");
    g_autoptr(LrgInputAction) move = lrg_input_action_new ("move");

    space = lrg_input_binding_new_keyboard (GRL_KEY_SPACE, LRG_INPUT_MODIFIER_NONE);
    stick = lrg_input_binding_new_gamepad_axis (0, GRL_GAMEPAD_AXIS_LEFT_X, 0.1f, TRUE);
    lrg_input_action_add_binding (jump, space);
    lrg_input_action_add_binding (move, stick);
    lrg_input_map_add_action (map, jump);
    lrg_input_map_add_action (map, move);
}

static LrgGameState *
test_replay_game_create_initial_state (LrgGameTemplate *template)
{
    return g_object_new (test_play_state_get_type (), NULL);
}

static void
test_replay_game_post_startup (LrgGameTemplate *template)
{
    TestReplayGame *self = TEST_REPLAY_GAME (template);

    /* The game state the simulation checksums and replays start from */
    lrg_simulation_add_object (lrg_game_template_get_simulation (template),
                               G_OBJECT (self->arena));
    lrg_timer_manager_add (lrg_game_template_get_timer_manager (template), 0.5, TRUE, FALSE);
}

static void
test_replay_game_fixed_update (LrgGameTemplate *template,
                               gdouble          delta)
{
    TestReplayGame *self = TEST_REPLAY_GAME (template);
    LrgInputMap *map = lrg_game_template_get_input_map (template);
    LrgRandomStream *random = lrg_simulation_get_random (lrg_game_template_get_simulation (template));

    /* Game code reads input through the normal input map */
    self->arena->x[0] += lrg_input_map_get_axis (map, "move");
    if (lrg_input_map_is_pressed (map, "jump"))
    {
        self->jumps++;
        self->arena->score += 1 + lrg_random_stream_bounded (random, 100);
    }
}

static void
test_replay_game_on_timeout (LrgTimerManager *timers,
                             guint64          id,
                             TestReplayGame  *self)
{
    self->arena->timer_fires++;
}

static void
test_replay_game_finalize (GObject *object)
{
    TestReplayGame *self = TEST_REPLAY_GAME (object);

    g_clear_object (&self->arena);
    G_OBJECT_CLASS (test_replay_game_parent_class)->finalize (object);
}

static void
test_replay_game_class_init (TestReplayGameClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    G_OBJECT_CLASS (klass)->finalize = test_replay_game_finalize;
    template_class->setup_default_input = test_replay_game_setup_default_input;
    template_class->create_initial_state = test_replay_game_create_initial_state;
    template_class->post_startup = test_replay_game_post_startup;
    template_class->fixed_update = test_replay_game_fixed_update;
}

static void
test_replay_game_init (TestReplayGame *self)
{
    self->arena = g_object_new (TEST_TYPE_ARENA, NULL);
    g_object_set (self,
                  "enable-settings", FALSE,
                  "enable-auto-save", FALSE,
                  "pause-on-focus-loss", FALSE,
                  "pause-on-controller-disconnect", FALSE,
                  NULL);
    g_signal_connect (lrg_game_template_get_timer_manager (LRG_GAME_TEMPLATE (self)),
                      "timeout", G_CALLBACK (test_replay_game_on_timeout), self);
}

/* The default input manager, isolated from real devices behind a mock */
typedef struct
{
    LrgInputManager *manager;
    LrgInputMock    *mock;
    GPtrArray       *saved_sources;
} MockInput;

static void
mock_input_begin (MockInput *input)
{
    GPtrArray *sources;

    input->manager = lrg_input_manager_get_default ();
    input->saved_sources = g_ptr_array_new_with_free_func (g_object_unref);
    sources = lrg_input_manager_get_sources (input->manager);
    while (sources->len > 0)
    {
        LrgInput *source = g_ptr_array_index (sources, 0);

        g_ptr_array_add (input->saved_sources, g_object_ref (source));
        lrg_input_manager_remove_source (input->manager, source);
    }
    input->mock = lrg_input_mock_new ();
    lrg_input_manager_set_enabled (input->manager, TRUE);
    lrg_input_manager_add_source (input->manager, LRG_INPUT (input->mock));
    lrg_input_mock_set_gamepad_available (input->mock, 0, TRUE);
}

static void
mock_input_end (MockInput *input)
{
    guint i;

    lrg_input_mock_reset (input->mock);
    lrg_input_manager_poll (input->manager);
    lrg_input_manager_remove_source (input->manager, LRG_INPUT (input->mock));
    g_clear_object (&input->mock);
    for (i = 0; i < input->saved_sources->len; i++)
        lrg_input_manager_add_source (input->manager,
                                     g_ptr_array_index (input->saved_sources, i));
    g_clear_pointer (&input->saved_sources, g_ptr_array_unref);
}

static void
test_template_record_and_replay_headless (void)
{
    g_autoptr(TestReplayGame) recorder = NULL;
    g_autoptr(TestReplayGame) player = NULL;
    g_autoptr(LrgHeadlessHost) host = NULL;
    g_autoptr(LrgReplay) replay = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    LrgEngine *engine;
    MockInput input;
    guint64 recorded_final;
    guint64 played_final = 0;
    gint64 recorded_score;
    guint frame;

    dir = g_dir_make_tmp ("lrg-template-replay-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "session.lrgreplay", NULL);

    engine = lrg_engine_get_default ();
    if (!lrg_engine_is_running (engine))
        g_assert_true (lrg_engine_startup (engine, &error));
    g_assert_no_error (error);
    mock_input_begin (&input);

    /* 1. Record: live (mock) input, frames of uneven length, 3 seconds */
    recorder = g_object_new (TEST_TYPE_REPLAY_GAME, "simulation-seed", G_GUINT64_CONSTANT (77), NULL);
    lrg_game_template_record_replay (LRG_GAME_TEMPLATE (recorder), path, "test:arena");
    host = lrg_headless_host_new (engine, SCENARIO_STEP);
    g_assert_true (lrg_game_template_startup (LRG_GAME_TEMPLATE (recorder), LRG_GAME_HOST (host), &error));
    g_assert_no_error (error);

    for (frame = 0; frame < 240; frame++)
    {
        /* Taps of SPACE every 20 frames, a stick sweep, and a 144 Hz-ish
         * frame cadence so ticks and frames do not line up */
        lrg_input_mock_set_key_state (input.mock, GRL_KEY_SPACE,
                                      frame % 20 < 3 ? LRG_KEY_STATE_DOWN : LRG_KEY_STATE_UP);
        lrg_input_mock_set_gamepad_axis (input.mock, 0, GRL_GAMEPAD_AXIS_LEFT_X,
                                         (gfloat) ((frame % 50) / 50.0));
        lrg_game_template_update (LRG_GAME_TEMPLATE (recorder), frame % 2 == 0 ? 1.0 / 144.0 : 1.0 / 72.0);
    }
    recorded_final = lrg_simulation_checksum (lrg_game_template_get_simulation (LRG_GAME_TEMPLATE (recorder)));
    recorded_score = recorder->arena->score;
    g_assert_cmpuint (recorder->jumps, >, 0);
    g_assert_cmpuint (recorder->arena->timer_fires, >, 0);
    lrg_game_template_shutdown_game (LRG_GAME_TEMPLATE (recorder));
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    replay = lrg_replay_load_from_file (path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_replay_get_seed (replay), ==, 77);
    g_assert_cmpstr (lrg_replay_get_initial_state (replay), ==, "test:arena");
    g_assert_cmpuint (lrg_replay_get_n_ticks (replay), >, 100);

    /* 2. Play back with no live input at all: same result */
    lrg_input_mock_reset (input.mock);
    player = g_object_new (TEST_TYPE_REPLAY_GAME, NULL);
    g_assert_true (lrg_game_template_run_replay_headless (LRG_GAME_TEMPLATE (player), path,
                                                          &played_final, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (played_final, ==, recorded_final);
    g_assert_cmpint (player->arena->score, ==, recorded_score);
    g_assert_cmpuint (player->jumps, ==, recorder->jumps);

    mock_input_end (&input);
    g_unlink (path);
    g_rmdir (dir);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/replay/state-buffer/round-trip", test_state_buffer_round_trip);
    g_test_add_func ("/replay/input-snapshot/basics", test_input_snapshot_basics);
    g_test_add_func ("/replay/input-snapshot/inject", test_input_snapshot_inject);
    g_test_add_func ("/replay/simulation/determinism", test_simulation_determinism);
    g_test_add_func ("/replay/simulation/save-load", test_simulation_save_load);
    g_test_add_func ("/replay/simulation/desync-detector", test_simulation_desync_detector);
    g_test_add_func ("/replay/file/round-trip", test_replay_round_trip);
    g_test_add_func ("/replay/file/detects-desync", test_replay_detects_desync);
    g_test_add_func ("/replay/file/rejects-bad-input", test_replay_rejects_bad_input);
    g_test_add_func ("/replay/golden/physics-timers-rng", test_replay_golden);
    g_test_add_func ("/replay/golden/mismatch", test_replay_golden_mismatch);
    g_test_add_func ("/replay/template/record-and-replay-headless",
                     test_template_record_and_replay_headless);

    return g_test_run ();
}
