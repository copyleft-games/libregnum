/* test-scripting-pygobject.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgScriptingPyGObject.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixture
 * ========================================================================== */

typedef struct
{
    LrgScriptingPyGObject *scripting;
    LrgRegistry           *registry;
    LrgEngine             *engine;
} PyGObjectFixture;

static void
pygobject_fixture_set_up (PyGObjectFixture *fixture,
                          gconstpointer     user_data)
{
    (void)user_data;

    fixture->scripting = lrg_scripting_pygobject_new ();
    g_assert_nonnull (fixture->scripting);

    fixture->registry = lrg_registry_new ();
    g_assert_nonnull (fixture->registry);

    fixture->engine = lrg_engine_get_default ();
    g_assert_nonnull (fixture->engine);
}

static void
pygobject_fixture_tear_down (PyGObjectFixture *fixture,
                             gconstpointer     user_data)
{
    (void)user_data;

    g_clear_object (&fixture->scripting);
    g_clear_object (&fixture->registry);
    /* Engine is a singleton, don't unref */
}

/* ==========================================================================
 * Construction Tests
 * ========================================================================== */

static void
test_scripting_pygobject_new (void)
{
    g_autoptr(LrgScriptingPyGObject) scripting = NULL;

    scripting = lrg_scripting_pygobject_new ();

    g_assert_nonnull (scripting);
    g_assert_true (LRG_IS_SCRIPTING_PYGOBJECT (scripting));
    g_assert_true (LRG_IS_SCRIPTING_GI (scripting));
    g_assert_true (LRG_IS_SCRIPTING (scripting));
}

static void
test_scripting_pygobject_type_hierarchy (void)
{
    g_autoptr(LrgScriptingPyGObject) scripting = NULL;

    scripting = lrg_scripting_pygobject_new ();

    /* Verify full type hierarchy */
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING_PYGOBJECT));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING_GI));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, G_TYPE_OBJECT));
}

/* ==========================================================================
 * Script Execution Tests
 * ========================================================================== */

static void
test_scripting_pygobject_load_string_basic (PyGObjectFixture *fixture,
                                            gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "x = 42\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_basic", code, &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_pygobject_load_string_syntax_error (PyGObjectFixture *fixture,
                                                   gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "def broken(\n";  /* Invalid syntax */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_syntax", code, &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX);
}

static void
test_scripting_pygobject_load_string_runtime_error (PyGObjectFixture *fixture,
                                                    gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "undefined_variable + 1\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_runtime", code, &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME);
}

static void
test_scripting_pygobject_load_file_not_found (PyGObjectFixture *fixture,
                                              gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;

    (void)user_data;

    result = lrg_scripting_load_file (LRG_SCRIPTING (fixture->scripting),
                                      "/nonexistent/path/to/script.py",
                                      &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
}

/* ==========================================================================
 * Globals Tests
 * ========================================================================== */

static void
test_scripting_pygobject_globals_set_get_int (PyGObjectFixture *fixture,
                                              gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             set_value = G_VALUE_INIT;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;

    (void)user_data;

    /* Set an integer global (Python integers map to G_TYPE_INT64) */
    g_value_init (&set_value, G_TYPE_INT64);
    g_value_set_int64 (&set_value, 42);

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_int", &set_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Get it back - don't pre-init, let implementation determine type */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_int", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpint (g_value_get_int64 (&get_value), ==, 42);

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_pygobject_globals_set_get_string (PyGObjectFixture *fixture,
                                                 gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             set_value = G_VALUE_INIT;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;

    (void)user_data;

    /* Set a string global */
    g_value_init (&set_value, G_TYPE_STRING);
    g_value_set_string (&set_value, "hello world");

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_string", &set_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Get it back - don't pre-init, let implementation determine type */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_string", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpstr (g_value_get_string (&get_value), ==, "hello world");

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_pygobject_globals_from_script (PyGObjectFixture *fixture,
                                              gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "script_var = 123\n";

    /* Run script that defines a global */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_global", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Get the global set by script - don't pre-init, let implementation do it */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "script_var", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpint (g_value_get_int64 (&get_value), ==, 123);

    g_value_unset (&get_value);
}

static void
test_scripting_pygobject_globals_not_found (PyGObjectFixture *fixture,
                                            gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;

    (void)user_data;

    /* Don't pre-init - but also the implementation shouldn't init if not found */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "nonexistent_global", &get_value, &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Function Calling Tests
 * ========================================================================== */

static void
test_scripting_pygobject_call_function (PyGObjectFixture *fixture,
                                        gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    GValue             return_value = G_VALUE_INIT;
    GValue             args[1] = { G_VALUE_INIT };
    const gchar       *code;

    (void)user_data;

    code = "def add_one(x):\n    return x + 1\n";

    /* Define a function */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "define_func", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Call it with argument (Python integers map to G_TYPE_INT64) */
    g_value_init (&args[0], G_TYPE_INT64);
    g_value_set_int64 (&args[0], 5);

    /* Don't pre-init return_value - let implementation do it */
    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "add_one", &return_value, 1, args, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpint (g_value_get_int64 (&return_value), ==, 6);

    g_value_unset (&args[0]);
    g_value_unset (&return_value);
}

static void
test_scripting_pygobject_call_function_not_found (PyGObjectFixture *fixture,
                                                  gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;

    (void)user_data;

    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "nonexistent_func", NULL, 0, NULL, &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * GI Integration Tests
 * ========================================================================== */

static void
test_scripting_pygobject_gi_available (PyGObjectFixture *fixture,
                                       gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "from gi.repository import GLib\nversion = GLib.MAJOR_VERSION\n";

    /* Test that gi.repository is available */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_gi", code, &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_pygobject_expose_typelib (PyGObjectFixture *fixture,
                                         gconstpointer     user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    g_autoptr(GError) error = NULL;
    gboolean          result;
    const gchar      *code;

    (void)user_data;

    /* Require GLib typelib */
    result = lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* It should now be available in Python without import */
    code = "version = GLib.MAJOR_VERSION\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_exposed", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);
}

/* ==========================================================================
 * Update Hooks Tests (inherited from LrgScriptingGI)
 * ========================================================================== */

static void
test_scripting_pygobject_update_hooks (PyGObjectFixture *fixture,
                                       gconstpointer     user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    g_autoptr(GError) error = NULL;
    gboolean          result;
    GValue            get_value = G_VALUE_INIT;
    const gchar      *code;

    (void)user_data;

    /* Define an update function that increments a counter */
    code =
        "update_count = 0\n"
        "def on_update(delta):\n"
        "    global update_count\n"
        "    update_count = update_count + 1\n";

    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "define_update", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Register the update hook */
    lrg_scripting_gi_register_update_hook (gi_self, "on_update");

    /* Call update multiple times */
    lrg_scripting_gi_update (gi_self, 0.016f);
    lrg_scripting_gi_update (gi_self, 0.016f);
    lrg_scripting_gi_update (gi_self, 0.016f);

    /* Check that counter was incremented - don't pre-init, let implementation do it */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "update_count", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpint (g_value_get_int64 (&get_value), ==, 3);

    g_value_unset (&get_value);
}

/* ==========================================================================
 * Reset Tests
 * ========================================================================== */

static void
test_scripting_pygobject_reset (PyGObjectFixture *fixture,
                                gconstpointer     user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    GValue             get_value1 = G_VALUE_INIT;
    GValue             get_value2 = G_VALUE_INIT;
    const gchar       *code;

    (void)user_data;

    code = "reset_test_var = 999\n";

    /* Define a global */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "pre_reset", code, &error);
    g_assert_true (result);

    /* Verify it exists - don't pre-init, let implementation determine type */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "reset_test_var", &get_value1, &error);
    g_assert_true (result);
    g_value_unset (&get_value1);

    /* Reset the scripting context */
    lrg_scripting_reset (LRG_SCRIPTING (fixture->scripting));

    /* The global should no longer exist */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "reset_test_var", &get_value2, &error);
    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Inherited LrgScriptingGI Features Tests
 * ========================================================================== */

static void
test_scripting_pygobject_search_paths (PyGObjectFixture *fixture,
                                       gconstpointer     user_data)
{
    LrgScriptingGI     *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    const gchar *const *paths;

    (void)user_data;

    /* Add search paths */
    lrg_scripting_gi_add_search_path (gi_self, "/custom/path/one");
    lrg_scripting_gi_add_search_path (gi_self, "/custom/path/two");

    /* Verify they were added */
    paths = lrg_scripting_gi_get_search_paths (gi_self);
    g_assert_nonnull (paths);
    g_assert_cmpstr (paths[0], ==, "/custom/path/one");
    g_assert_cmpstr (paths[1], ==, "/custom/path/two");
    g_assert_null (paths[2]);
}

static void
test_scripting_pygobject_registry_integration (PyGObjectFixture *fixture,
                                               gconstpointer     user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    LrgRegistry    *retrieved;

    (void)user_data;

    /* Set registry */
    lrg_scripting_gi_set_registry (gi_self, fixture->registry);

    /* Verify retrieval */
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_true (retrieved == fixture->registry);
}

static void
test_scripting_pygobject_engine_integration (PyGObjectFixture *fixture,
                                             gconstpointer     user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    LrgEngine      *retrieved;

    (void)user_data;

    /* Set engine */
    lrg_scripting_gi_set_engine (gi_self, fixture->engine);

    /* Verify retrieval */
    retrieved = lrg_scripting_gi_get_engine (gi_self);
    g_assert_true (retrieved == fixture->engine);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction tests */
    g_test_add_func ("/scripting-pygobject/new",
                     test_scripting_pygobject_new);
    g_test_add_func ("/scripting-pygobject/type-hierarchy",
                     test_scripting_pygobject_type_hierarchy);

    /* Script execution tests */
    g_test_add ("/scripting-pygobject/load-string/basic",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_load_string_basic,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/load-string/syntax-error",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_load_string_syntax_error,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/load-string/runtime-error",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_load_string_runtime_error,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/load-file/not-found",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_load_file_not_found,
                pygobject_fixture_tear_down);

    /* Globals tests */
    g_test_add ("/scripting-pygobject/globals/set-get-int",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_globals_set_get_int,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/globals/set-get-string",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_globals_set_get_string,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/globals/from-script",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_globals_from_script,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/globals/not-found",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_globals_not_found,
                pygobject_fixture_tear_down);

    /* Function calling tests */
    g_test_add ("/scripting-pygobject/call-function/basic",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_call_function,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/call-function/not-found",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_call_function_not_found,
                pygobject_fixture_tear_down);

    /* GI integration tests */
    g_test_add ("/scripting-pygobject/gi/available",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_gi_available,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/gi/expose-typelib",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_expose_typelib,
                pygobject_fixture_tear_down);

    /* Update hooks tests */
    g_test_add ("/scripting-pygobject/update-hooks/call",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_update_hooks,
                pygobject_fixture_tear_down);

    /* Reset tests */
    g_test_add ("/scripting-pygobject/reset",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_reset,
                pygobject_fixture_tear_down);

    /* Inherited LrgScriptingGI feature tests */
    g_test_add ("/scripting-pygobject/search-paths",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_search_paths,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/registry-integration",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_registry_integration,
                pygobject_fixture_tear_down);

    g_test_add ("/scripting-pygobject/engine-integration",
                PyGObjectFixture, NULL,
                pygobject_fixture_set_up,
                test_scripting_pygobject_engine_integration,
                pygobject_fixture_tear_down);

    return g_test_run ();
}
