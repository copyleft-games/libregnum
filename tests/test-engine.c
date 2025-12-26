/* test-engine.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgEngine.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgEngine *engine;
} EngineFixture;

static void
engine_fixture_set_up (EngineFixture *fixture,
                       gconstpointer  user_data)
{
    /* Get fresh engine - note we can't easily reset the singleton */
    fixture->engine = lrg_engine_get_default ();
    g_assert_nonnull (fixture->engine);
}

static void
engine_fixture_tear_down (EngineFixture *fixture,
                          gconstpointer  user_data)
{
    /* Shutdown if still running */
    if (lrg_engine_is_running (fixture->engine))
    {
        lrg_engine_shutdown (fixture->engine);
    }
}

/* ==========================================================================
 * Test Cases - Singleton
 * ========================================================================== */

static void
test_engine_get_default (void)
{
    LrgEngine *engine1;
    LrgEngine *engine2;

    engine1 = lrg_engine_get_default ();
    g_assert_nonnull (engine1);
    g_assert_true (LRG_IS_ENGINE (engine1));

    engine2 = lrg_engine_get_default ();
    g_assert_nonnull (engine2);
    g_assert_true (engine1 == engine2);
}

/* ==========================================================================
 * Test Cases - State
 * ========================================================================== */

static void
test_engine_initial_state (EngineFixture *fixture,
                           gconstpointer  user_data)
{
    LrgEngineState state;

    state = lrg_engine_get_state (fixture->engine);

    /* Engine should be uninitialized or terminated (if previous test shut it down) */
    g_assert_true (state == LRG_ENGINE_STATE_UNINITIALIZED ||
                   state == LRG_ENGINE_STATE_TERMINATED);
}

static void
test_engine_startup_state (EngineFixture *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;
    LrgEngineState    state;

    /* Startup the engine */
    result = lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);
    g_assert_true (result);

    /* Check state */
    state = lrg_engine_get_state (fixture->engine);
    g_assert_cmpint (state, ==, LRG_ENGINE_STATE_RUNNING);
    g_assert_true (lrg_engine_is_running (fixture->engine));
}

static void
test_engine_shutdown_state (EngineFixture *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    LrgEngineState    state;

    /* Startup first */
    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    /* Shutdown */
    lrg_engine_shutdown (fixture->engine);

    /* Check state */
    state = lrg_engine_get_state (fixture->engine);
    g_assert_cmpint (state, ==, LRG_ENGINE_STATE_TERMINATED);
    g_assert_false (lrg_engine_is_running (fixture->engine));
}

static void
test_engine_double_startup_fails (EngineFixture *fixture,
                                  gconstpointer  user_data)
{
    g_autoptr(GError) error1 = NULL;
    g_autoptr(GError) error2 = NULL;
    gboolean          result1;
    gboolean          result2;

    /* First startup should succeed */
    result1 = lrg_engine_startup (fixture->engine, &error1);
    g_assert_no_error (error1);
    g_assert_true (result1);

    /* Second startup should fail */
    result2 = lrg_engine_startup (fixture->engine, &error2);
    g_assert_false (result2);
    g_assert_error (error2, LRG_ENGINE_ERROR, LRG_ENGINE_ERROR_STATE);
}

/* ==========================================================================
 * Test Cases - Subsystems
 * ========================================================================== */

static void
test_engine_registry_available (EngineFixture *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    LrgRegistry      *registry;

    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    registry = lrg_engine_get_registry (fixture->engine);
    g_assert_nonnull (registry);
    g_assert_true (LRG_IS_REGISTRY (registry));
}

static void
test_engine_data_loader_available (EngineFixture *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    LrgDataLoader    *loader;

    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    loader = lrg_engine_get_data_loader (fixture->engine);
    g_assert_nonnull (loader);
    g_assert_true (LRG_IS_DATA_LOADER (loader));
}

/* ==========================================================================
 * Test Cases - Signals
 * ========================================================================== */

static gboolean startup_signal_received = FALSE;
static gboolean shutdown_signal_received = FALSE;

static void
on_startup (LrgEngine *engine,
            gpointer   user_data)
{
    startup_signal_received = TRUE;
}

static void
on_shutdown (LrgEngine *engine,
             gpointer   user_data)
{
    shutdown_signal_received = TRUE;
}

static void
test_engine_startup_signal (EngineFixture *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    startup_signal_received = FALSE;

    g_signal_connect (fixture->engine, "startup", G_CALLBACK (on_startup), NULL);

    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    g_assert_true (startup_signal_received);
}

static void
test_engine_shutdown_signal (EngineFixture *fixture,
                             gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    shutdown_signal_received = FALSE;

    g_signal_connect (fixture->engine, "shutdown", G_CALLBACK (on_shutdown), NULL);

    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    lrg_engine_shutdown (fixture->engine);

    g_assert_true (shutdown_signal_received);
}

/* ==========================================================================
 * Test Cases - Update
 * ========================================================================== */

static gfloat pre_update_delta = -1.0f;
static gfloat post_update_delta = -1.0f;

static void
on_pre_update (LrgEngine *engine,
               gfloat     delta,
               gpointer   user_data)
{
    pre_update_delta = delta;
}

static void
on_post_update (LrgEngine *engine,
                gfloat     delta,
                gpointer   user_data)
{
    post_update_delta = delta;
}

static void
test_engine_update_signals (EngineFixture *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    gfloat            test_delta = 0.016f; /* ~60fps */

    pre_update_delta = -1.0f;
    post_update_delta = -1.0f;

    g_signal_connect (fixture->engine, "pre-update", G_CALLBACK (on_pre_update), NULL);
    g_signal_connect (fixture->engine, "post-update", G_CALLBACK (on_post_update), NULL);

    lrg_engine_startup (fixture->engine, &error);
    g_assert_no_error (error);

    lrg_engine_update (fixture->engine, test_delta);

    g_assert_cmpfloat (pre_update_delta, ==, test_delta);
    g_assert_cmpfloat (post_update_delta, ==, test_delta);
}

static void
test_engine_update_not_running (EngineFixture *fixture,
                                gconstpointer  user_data)
{
    /* Engine is not started, update should silently do nothing */
    pre_update_delta = -1.0f;

    g_signal_connect (fixture->engine, "pre-update", G_CALLBACK (on_pre_update), NULL);

    lrg_engine_update (fixture->engine, 0.016f);

    /* Signal should not have been emitted */
    g_assert_cmpfloat (pre_update_delta, ==, -1.0f);
}

/* ==========================================================================
 * Test Cases - Version
 * ========================================================================== */

static void
test_version_functions (void)
{
    guint major, minor, micro;

    major = lrg_get_major_version ();
    minor = lrg_get_minor_version ();
    micro = lrg_get_micro_version ();

    /* Version numbers should be reasonable (0-999) */
    g_assert_cmpuint (major, <, 1000);
    g_assert_cmpuint (minor, <, 1000);
    g_assert_cmpuint (micro, <, 1000);

    /* Check version macro matches runtime */
    g_assert_cmpuint (major, ==, LRG_VERSION_MAJOR);
    g_assert_cmpuint (minor, ==, LRG_VERSION_MINOR);
    g_assert_cmpuint (micro, ==, LRG_VERSION_MICRO);
}

static void
test_check_version (void)
{
    /* Current version should pass */
    g_assert_true (lrg_check_version (LRG_VERSION_MAJOR,
                                      LRG_VERSION_MINOR,
                                      LRG_VERSION_MICRO));

    /* Lower version should pass */
    if (LRG_VERSION_MINOR > 0)
    {
        g_assert_true (lrg_check_version (LRG_VERSION_MAJOR,
                                          LRG_VERSION_MINOR - 1,
                                          0));
    }

    /* Higher major version should fail */
    g_assert_false (lrg_check_version (LRG_VERSION_MAJOR + 1, 0, 0));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Singleton tests */
    g_test_add_func ("/engine/singleton", test_engine_get_default);

    /* State tests */
    g_test_add ("/engine/state/initial",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_initial_state,
                engine_fixture_tear_down);

    g_test_add ("/engine/state/startup",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_startup_state,
                engine_fixture_tear_down);

    g_test_add ("/engine/state/shutdown",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_shutdown_state,
                engine_fixture_tear_down);

    g_test_add ("/engine/state/double-startup",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_double_startup_fails,
                engine_fixture_tear_down);

    /* Subsystem tests */
    g_test_add ("/engine/subsystems/registry",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_registry_available,
                engine_fixture_tear_down);

    g_test_add ("/engine/subsystems/data-loader",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_data_loader_available,
                engine_fixture_tear_down);

    /* Signal tests */
    g_test_add ("/engine/signals/startup",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_startup_signal,
                engine_fixture_tear_down);

    g_test_add ("/engine/signals/shutdown",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_shutdown_signal,
                engine_fixture_tear_down);

    /* Update tests */
    g_test_add ("/engine/update/signals",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_update_signals,
                engine_fixture_tear_down);

    g_test_add ("/engine/update/not-running",
                EngineFixture,
                NULL,
                engine_fixture_set_up,
                test_engine_update_not_running,
                engine_fixture_tear_down);

    /* Version tests */
    g_test_add_func ("/engine/version/functions", test_version_functions);
    g_test_add_func ("/engine/version/check", test_check_version);

    return g_test_run ();
}
