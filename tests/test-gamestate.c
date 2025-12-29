/* test-gamestate.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgGameState and LrgGameStateManager.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Game State for Testing
 *
 * A simple game state implementation for testing state transitions.
 * ========================================================================== */

#define TEST_TYPE_GAME_STATE (test_game_state_get_type ())
G_DECLARE_FINAL_TYPE (TestGameState, test_game_state, TEST, GAME_STATE, LrgGameState)

struct _TestGameState
{
    LrgGameState parent_instance;

    gboolean entered;
    gboolean exited;
    gboolean paused;
    gboolean resumed;
    gint     update_count;
    gint     draw_count;
};

G_DEFINE_TYPE (TestGameState, test_game_state, LRG_TYPE_GAME_STATE)

static void
test_game_state_enter (LrgGameState *state)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->entered = TRUE;
}

static void
test_game_state_exit (LrgGameState *state)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->exited = TRUE;
}

static void
test_game_state_pause (LrgGameState *state)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->paused = TRUE;
}

static void
test_game_state_resume (LrgGameState *state)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->resumed = TRUE;
}

static void
test_game_state_update (LrgGameState *state,
                        gdouble       delta)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->update_count++;
}

static void
test_game_state_draw (LrgGameState *state)
{
    TestGameState *self = TEST_GAME_STATE (state);
    self->draw_count++;
}

static void
test_game_state_class_init (TestGameStateClass *klass)
{
    LrgGameStateClass *state_class = LRG_GAME_STATE_CLASS (klass);

    state_class->enter  = test_game_state_enter;
    state_class->exit   = test_game_state_exit;
    state_class->pause  = test_game_state_pause;
    state_class->resume = test_game_state_resume;
    state_class->update = test_game_state_update;
    state_class->draw   = test_game_state_draw;
}

static void
test_game_state_init (TestGameState *self)
{
    self->entered = FALSE;
    self->exited = FALSE;
    self->paused = FALSE;
    self->resumed = FALSE;
    self->update_count = 0;
    self->draw_count = 0;
}

static TestGameState *
test_game_state_new (const gchar *name)
{
    TestGameState *state;

    state = g_object_new (TEST_TYPE_GAME_STATE, NULL);
    lrg_game_state_set_name (LRG_GAME_STATE (state), name);

    return state;
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgGameStateManager *manager;
} GameStateFixture;

static void
gamestate_fixture_set_up (GameStateFixture *fixture,
                          gconstpointer     user_data)
{
    fixture->manager = lrg_game_state_manager_new ();
    g_assert_nonnull (fixture->manager);
}

static void
gamestate_fixture_tear_down (GameStateFixture *fixture,
                             gconstpointer     user_data)
{
    g_clear_object (&fixture->manager);
}

/* ==========================================================================
 * Test Cases - LrgGameStateManager Construction
 * ========================================================================== */

static void
test_manager_new (void)
{
    g_autoptr(LrgGameStateManager) manager = NULL;

    manager = lrg_game_state_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_GAME_STATE_MANAGER (manager));
    g_assert_true (lrg_game_state_manager_is_empty (manager));
    g_assert_cmpuint (lrg_game_state_manager_get_state_count (manager), ==, 0);
}

/* ==========================================================================
 * Test Cases - Push/Pop Operations
 * ========================================================================== */

static void
test_manager_push (GameStateFixture *fixture,
                   gconstpointer     user_data)
{
    TestGameState *state;

    state = test_game_state_new ("TestState");

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state));

    g_assert_false (lrg_game_state_manager_is_empty (fixture->manager));
    g_assert_cmpuint (lrg_game_state_manager_get_state_count (fixture->manager), ==, 1);
    g_assert_true (state->entered);
    g_assert_false (state->exited);

    /* Current should be the pushed state */
    g_assert_true (lrg_game_state_manager_get_current (fixture->manager) ==
                   LRG_GAME_STATE (state));
}

static void
test_manager_push_multiple (GameStateFixture *fixture,
                            gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));
    g_assert_true (state1->entered);
    g_assert_false (state1->paused);

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state2));

    /* State1 should be paused, state2 should be entered */
    g_assert_true (state1->paused);
    g_assert_true (state2->entered);
    g_assert_cmpuint (lrg_game_state_manager_get_state_count (fixture->manager), ==, 2);

    /* Current should be state2 */
    g_assert_true (lrg_game_state_manager_get_current (fixture->manager) ==
                   LRG_GAME_STATE (state2));
}

static void
test_manager_pop (GameStateFixture *fixture,
                  gconstpointer     user_data)
{
    TestGameState *state;

    state = test_game_state_new ("TestState");

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state));
    g_assert_true (state->entered);

    /* Need to hold a ref since pop will unref */
    g_object_ref (state);

    lrg_game_state_manager_pop (fixture->manager);

    g_assert_true (state->exited);
    g_assert_true (lrg_game_state_manager_is_empty (fixture->manager));
    g_assert_null (lrg_game_state_manager_get_current (fixture->manager));

    g_object_unref (state);
}

static void
test_manager_pop_resumes_previous (GameStateFixture *fixture,
                                   gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state2));

    g_assert_true (state1->paused);
    g_assert_false (state1->resumed);

    /* Hold ref to state2 */
    g_object_ref (state2);

    lrg_game_state_manager_pop (fixture->manager);

    /* State1 should be resumed, state2 should be exited */
    g_assert_true (state1->resumed);
    g_assert_true (state2->exited);

    /* Current should be state1 */
    g_assert_true (lrg_game_state_manager_get_current (fixture->manager) ==
                   LRG_GAME_STATE (state1));

    g_object_unref (state2);
}

static void
test_manager_pop_empty (GameStateFixture *fixture,
                        gconstpointer     user_data)
{
    /* Pop on empty manager should do nothing (not crash) */
    lrg_game_state_manager_pop (fixture->manager);

    g_assert_true (lrg_game_state_manager_is_empty (fixture->manager));
}

/* ==========================================================================
 * Test Cases - Replace Operation
 * ========================================================================== */

static void
test_manager_replace (GameStateFixture *fixture,
                      gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));

    /* Hold ref to state1 */
    g_object_ref (state1);

    lrg_game_state_manager_replace (fixture->manager, LRG_GAME_STATE (state2));

    /* State1 should be exited (not paused), state2 should be entered */
    g_assert_true (state1->exited);
    g_assert_false (state1->paused);
    g_assert_true (state2->entered);

    /* Count should still be 1 */
    g_assert_cmpuint (lrg_game_state_manager_get_state_count (fixture->manager), ==, 1);

    /* Current should be state2 */
    g_assert_true (lrg_game_state_manager_get_current (fixture->manager) ==
                   LRG_GAME_STATE (state2));

    g_object_unref (state1);
}

static void
test_manager_replace_empty (GameStateFixture *fixture,
                            gconstpointer     user_data)
{
    TestGameState *state;

    state = test_game_state_new ("TestState");

    /* Replace on empty manager should work like push */
    lrg_game_state_manager_replace (fixture->manager, LRG_GAME_STATE (state));

    g_assert_cmpuint (lrg_game_state_manager_get_state_count (fixture->manager), ==, 1);
    g_assert_true (state->entered);
}

/* ==========================================================================
 * Test Cases - Clear Operation
 * ========================================================================== */

static void
test_manager_clear (GameStateFixture *fixture,
                    gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;
    TestGameState *state3;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");
    state3 = test_game_state_new ("State3");

    /* Hold refs */
    g_object_ref (state1);
    g_object_ref (state2);
    g_object_ref (state3);

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state2));
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state3));

    g_assert_cmpuint (lrg_game_state_manager_get_state_count (fixture->manager), ==, 3);

    lrg_game_state_manager_clear (fixture->manager);

    /* All states should be exited */
    g_assert_true (state1->exited);
    g_assert_true (state2->exited);
    g_assert_true (state3->exited);

    g_assert_true (lrg_game_state_manager_is_empty (fixture->manager));

    g_object_unref (state1);
    g_object_unref (state2);
    g_object_unref (state3);
}

/* ==========================================================================
 * Test Cases - Update/Draw
 * ========================================================================== */

static void
test_manager_update (GameStateFixture *fixture,
                     gconstpointer     user_data)
{
    TestGameState *state;

    state = test_game_state_new ("TestState");
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state));

    g_assert_cmpint (state->update_count, ==, 0);

    lrg_game_state_manager_update (fixture->manager, 0.016);
    g_assert_cmpint (state->update_count, ==, 1);

    lrg_game_state_manager_update (fixture->manager, 0.016);
    g_assert_cmpint (state->update_count, ==, 2);
}

static void
test_manager_draw (GameStateFixture *fixture,
                   gconstpointer     user_data)
{
    TestGameState *state;

    state = test_game_state_new ("TestState");
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state));

    g_assert_cmpint (state->draw_count, ==, 0);

    lrg_game_state_manager_draw (fixture->manager);
    g_assert_cmpint (state->draw_count, ==, 1);

    lrg_game_state_manager_draw (fixture->manager);
    g_assert_cmpint (state->draw_count, ==, 2);
}

static void
test_manager_update_empty (GameStateFixture *fixture,
                           gconstpointer     user_data)
{
    /* Update on empty manager should do nothing (not crash) */
    lrg_game_state_manager_update (fixture->manager, 0.016);
    lrg_game_state_manager_draw (fixture->manager);
}

/* ==========================================================================
 * Test Cases - LrgGameState Properties
 * ========================================================================== */

static void
test_state_name (void)
{
    g_autoptr(TestGameState) state = NULL;
    const gchar             *name;

    state = test_game_state_new ("MyState");

    name = lrg_game_state_get_name (LRG_GAME_STATE (state));
    g_assert_cmpstr (name, ==, "MyState");

    lrg_game_state_set_name (LRG_GAME_STATE (state), "RenamedState");
    name = lrg_game_state_get_name (LRG_GAME_STATE (state));
    g_assert_cmpstr (name, ==, "RenamedState");
}

static void
test_state_transparent (void)
{
    g_autoptr(TestGameState) state = NULL;

    state = test_game_state_new ("TestState");

    /* Default should be FALSE */
    g_assert_false (lrg_game_state_is_transparent (LRG_GAME_STATE (state)));

    lrg_game_state_set_transparent (LRG_GAME_STATE (state), TRUE);
    g_assert_true (lrg_game_state_is_transparent (LRG_GAME_STATE (state)));

    lrg_game_state_set_transparent (LRG_GAME_STATE (state), FALSE);
    g_assert_false (lrg_game_state_is_transparent (LRG_GAME_STATE (state)));
}

static void
test_state_blocking (void)
{
    g_autoptr(TestGameState) state = NULL;

    state = test_game_state_new ("TestState");

    /* Default should be TRUE */
    g_assert_true (lrg_game_state_is_blocking (LRG_GAME_STATE (state)));

    lrg_game_state_set_blocking (LRG_GAME_STATE (state), FALSE);
    g_assert_false (lrg_game_state_is_blocking (LRG_GAME_STATE (state)));

    lrg_game_state_set_blocking (LRG_GAME_STATE (state), TRUE);
    g_assert_true (lrg_game_state_is_blocking (LRG_GAME_STATE (state)));
}

/* ==========================================================================
 * Test Cases - Transparent State Rendering
 * ========================================================================== */

static void
test_manager_draw_transparent_states (GameStateFixture *fixture,
                                      gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");

    /* Make state2 transparent so state1 should also be drawn */
    lrg_game_state_set_transparent (LRG_GAME_STATE (state2), TRUE);

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state2));

    lrg_game_state_manager_draw (fixture->manager);

    /* Both states should be drawn when top state is transparent */
    g_assert_cmpint (state1->draw_count, ==, 1);
    g_assert_cmpint (state2->draw_count, ==, 1);
}

/* ==========================================================================
 * Test Cases - Non-blocking State Updates
 * ========================================================================== */

static void
test_manager_update_non_blocking_states (GameStateFixture *fixture,
                                         gconstpointer     user_data)
{
    TestGameState *state1;
    TestGameState *state2;

    state1 = test_game_state_new ("State1");
    state2 = test_game_state_new ("State2");

    /* Make state2 non-blocking so state1 should also be updated */
    lrg_game_state_set_blocking (LRG_GAME_STATE (state2), FALSE);

    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state1));
    lrg_game_state_manager_push (fixture->manager, LRG_GAME_STATE (state2));

    lrg_game_state_manager_update (fixture->manager, 0.016);

    /* Both states should be updated when top state is non-blocking */
    g_assert_cmpint (state1->update_count, ==, 1);
    g_assert_cmpint (state2->update_count, ==, 1);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Manager Construction */
    g_test_add_func ("/gamestate/manager/new", test_manager_new);

    /* Push/Pop Operations */
    g_test_add ("/gamestate/manager/push",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_push,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/push-multiple",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_push_multiple,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/pop",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_pop,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/pop-resumes-previous",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_pop_resumes_previous,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/pop-empty",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_pop_empty,
                gamestate_fixture_tear_down);

    /* Replace Operation */
    g_test_add ("/gamestate/manager/replace",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_replace,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/replace-empty",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_replace_empty,
                gamestate_fixture_tear_down);

    /* Clear Operation */
    g_test_add ("/gamestate/manager/clear",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_clear,
                gamestate_fixture_tear_down);

    /* Update/Draw */
    g_test_add ("/gamestate/manager/update",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_update,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/draw",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_draw,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/update-empty",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_update_empty,
                gamestate_fixture_tear_down);

    /* State Properties */
    g_test_add_func ("/gamestate/state/name", test_state_name);
    g_test_add_func ("/gamestate/state/transparent", test_state_transparent);
    g_test_add_func ("/gamestate/state/blocking", test_state_blocking);

    /* Transparent/Non-blocking */
    g_test_add ("/gamestate/manager/draw-transparent",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_draw_transparent_states,
                gamestate_fixture_tear_down);

    g_test_add ("/gamestate/manager/update-non-blocking",
                GameStateFixture, NULL,
                gamestate_fixture_set_up,
                test_manager_update_non_blocking_states,
                gamestate_fixture_tear_down);

    return g_test_run ();
}
