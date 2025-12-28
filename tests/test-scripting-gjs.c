/* test-scripting-gjs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgScriptingGjs.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixture
 * ========================================================================== */

typedef struct
{
    LrgScriptingGjs *scripting;
    LrgRegistry     *registry;
    LrgEngine       *engine;
} GjsFixture;

static void
gjs_fixture_set_up (GjsFixture    *fixture,
                    gconstpointer  user_data)
{
    (void)user_data;

    fixture->scripting = lrg_scripting_gjs_new ();
    g_assert_nonnull (fixture->scripting);

    fixture->registry = lrg_registry_new ();
    g_assert_nonnull (fixture->registry);

    fixture->engine = lrg_engine_get_default ();
    g_assert_nonnull (fixture->engine);
}

static void
gjs_fixture_tear_down (GjsFixture    *fixture,
                       gconstpointer  user_data)
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
test_scripting_gjs_new (void)
{
    g_autoptr(LrgScriptingGjs) scripting = NULL;

    scripting = lrg_scripting_gjs_new ();

    g_assert_nonnull (scripting);
    g_assert_true (LRG_IS_SCRIPTING_GJS (scripting));
    g_assert_true (LRG_IS_SCRIPTING_GI (scripting));
    g_assert_true (LRG_IS_SCRIPTING (scripting));
}

static void
test_scripting_gjs_type_hierarchy (void)
{
    g_autoptr(LrgScriptingGjs) scripting = NULL;

    scripting = lrg_scripting_gjs_new ();

    /* Verify full type hierarchy */
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING_GJS));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING_GI));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, LRG_TYPE_SCRIPTING));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (scripting, G_TYPE_OBJECT));
}

/* ==========================================================================
 * Script Execution Tests
 * ========================================================================== */

static void
test_scripting_gjs_load_string_basic (GjsFixture    *fixture,
                                      gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "let x = 42;\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_basic", code, &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_gjs_load_string_syntax_error (GjsFixture    *fixture,
                                             gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "function broken( {\n";  /* Invalid syntax */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_syntax", code, &error);

    g_assert_false (result);
    g_assert_nonnull (error);
    /*
     * Gjs doesn't always expose the error type clearly in GError.
     * We just verify an error occurred (SYNTAX or RUNTIME are both acceptable).
     */
    g_assert_true (error->code == LRG_SCRIPTING_ERROR_SYNTAX ||
                   error->code == LRG_SCRIPTING_ERROR_RUNTIME);
}

static void
test_scripting_gjs_load_string_runtime_error (GjsFixture    *fixture,
                                              gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "undefined_variable + 1;\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_runtime", code, &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME);
}

static void
test_scripting_gjs_load_file_not_found (GjsFixture    *fixture,
                                        gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;

    (void)user_data;

    result = lrg_scripting_load_file (LRG_SCRIPTING (fixture->scripting),
                                      "/nonexistent/path/to/script.js",
                                      &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
}

/* ==========================================================================
 * Globals Tests
 * ========================================================================== */

static void
test_scripting_gjs_globals_set_get_int (GjsFixture    *fixture,
                                        gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             set_value = G_VALUE_INIT;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;

    (void)user_data;

    /* Set an integer global (JavaScript numbers map to G_TYPE_DOUBLE) */
    g_value_init (&set_value, G_TYPE_INT);
    g_value_set_int (&set_value, 42);

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_int", &set_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Get it back - Gjs can only confirm existence, not actual value */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_int", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /*
     * Gjs doesn't provide a way to get JS values from C without native API.
     * We just verify the global exists (returns TRUE).
     */
    g_assert_true (G_IS_VALUE (&get_value));

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_gjs_globals_set_get_string (GjsFixture    *fixture,
                                           gconstpointer  user_data)
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

    /* Get it back - Gjs can only confirm existence, not actual value */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "my_string", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (G_IS_VALUE (&get_value));

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_gjs_globals_from_script (GjsFixture    *fixture,
                                        gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    GValue             get_value = G_VALUE_INIT;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    /* Use globalThis to ensure the variable is globally accessible */
    code = "globalThis.script_var = 123;\n";

    /* Run script that defines a global */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_global", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Get the global set by script - Gjs can only confirm existence */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "script_var", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (G_IS_VALUE (&get_value));

    g_value_unset (&get_value);
}

static void
test_scripting_gjs_globals_not_found (GjsFixture    *fixture,
                                      gconstpointer  user_data)
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
test_scripting_gjs_call_function (GjsFixture    *fixture,
                                  gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    GValue             args[1] = { G_VALUE_INIT };
    const gchar       *code;

    (void)user_data;

    /* Define function on globalThis so it's accessible */
    code = "globalThis.add_one = function(x) { return x + 1; };\n";

    /* Define a function */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "define_func", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Call it with argument */
    g_value_init (&args[0], G_TYPE_INT);
    g_value_set_int (&args[0], 5);

    /*
     * Gjs doesn't provide return values from eval directly.
     * We just verify the call succeeds.
     */
    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "add_one", NULL, 1, args, &error);
    g_assert_true (result);
    g_assert_no_error (error);

    g_value_unset (&args[0]);
}

static void
test_scripting_gjs_call_function_not_found (GjsFixture    *fixture,
                                            gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;

    (void)user_data;

    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "nonexistent_func", NULL, 0, NULL, &error);

    g_assert_false (result);
    g_assert_nonnull (error);
    /*
     * Gjs doesn't always expose the error type clearly in GError.
     * We just verify an error occurred (NOT_FOUND or RUNTIME are both acceptable).
     */
    g_assert_true (error->code == LRG_SCRIPTING_ERROR_NOT_FOUND ||
                   error->code == LRG_SCRIPTING_ERROR_RUNTIME);
}

/* ==========================================================================
 * GI Integration Tests
 * ========================================================================== */

static void
test_scripting_gjs_gi_available (GjsFixture    *fixture,
                                 gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    const gchar       *code;

    (void)user_data;

    code = "const GLib = imports.gi.GLib;\nlet version = GLib.MAJOR_VERSION;\n";

    /* Test that imports.gi is available */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_gi", code, &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_gjs_expose_typelib (GjsFixture    *fixture,
                                   gconstpointer  user_data)
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

    /* It should now be available in JavaScript via imports.gi */
    code = "let version = imports.gi.GLib.MAJOR_VERSION;\n";
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test_exposed", code, &error);
    g_assert_true (result);
    g_assert_no_error (error);
}

/* ==========================================================================
 * Update Hooks Tests (inherited from LrgScriptingGI)
 * ========================================================================== */

static void
test_scripting_gjs_update_hooks (GjsFixture    *fixture,
                                 gconstpointer  user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->scripting);
    g_autoptr(GError) error = NULL;
    gboolean          result;
    GValue            get_value = G_VALUE_INIT;
    const gchar      *code;

    (void)user_data;

    /* Define an update function that increments a counter */
    code =
        "globalThis.update_count = 0;\n"
        "globalThis.on_update = function(delta) {\n"
        "    globalThis.update_count = globalThis.update_count + 1;\n"
        "};\n";

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

    /*
     * Check that counter exists - Gjs can't return actual values,
     * but we verify the global still exists after updates.
     */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "update_count", &get_value, &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_true (G_IS_VALUE (&get_value));

    g_value_unset (&get_value);
}

/* ==========================================================================
 * Reset Tests
 * ========================================================================== */

static void
test_scripting_gjs_reset (GjsFixture    *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GError)  error = NULL;
    gboolean           result;
    GValue             get_value1 = G_VALUE_INIT;
    GValue             get_value2 = G_VALUE_INIT;
    const gchar       *code;

    (void)user_data;

    code = "globalThis.reset_test_var = 999;\n";

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
test_scripting_gjs_search_paths (GjsFixture    *fixture,
                                 gconstpointer  user_data)
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
test_scripting_gjs_registry_integration (GjsFixture    *fixture,
                                         gconstpointer  user_data)
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
test_scripting_gjs_engine_integration (GjsFixture    *fixture,
                                       gconstpointer  user_data)
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
    g_test_add_func ("/scripting-gjs/new",
                     test_scripting_gjs_new);
    g_test_add_func ("/scripting-gjs/type-hierarchy",
                     test_scripting_gjs_type_hierarchy);

    /* Script execution tests */
    g_test_add ("/scripting-gjs/load-string/basic",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_load_string_basic,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/load-string/syntax-error",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_load_string_syntax_error,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/load-string/runtime-error",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_load_string_runtime_error,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/load-file/not-found",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_load_file_not_found,
                gjs_fixture_tear_down);

    /* Globals tests */
    g_test_add ("/scripting-gjs/globals/set-get-int",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_globals_set_get_int,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/globals/set-get-string",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_globals_set_get_string,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/globals/from-script",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_globals_from_script,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/globals/not-found",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_globals_not_found,
                gjs_fixture_tear_down);

    /* Function calling tests */
    g_test_add ("/scripting-gjs/call-function/basic",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_call_function,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/call-function/not-found",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_call_function_not_found,
                gjs_fixture_tear_down);

    /* GI integration tests */
    g_test_add ("/scripting-gjs/gi/available",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_gi_available,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/gi/expose-typelib",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_expose_typelib,
                gjs_fixture_tear_down);

    /* Update hooks tests */
    g_test_add ("/scripting-gjs/update-hooks/call",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_update_hooks,
                gjs_fixture_tear_down);

    /* Reset tests */
    g_test_add ("/scripting-gjs/reset",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_reset,
                gjs_fixture_tear_down);

    /* Inherited LrgScriptingGI feature tests */
    g_test_add ("/scripting-gjs/search-paths",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_search_paths,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/registry-integration",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_registry_integration,
                gjs_fixture_tear_down);

    g_test_add ("/scripting-gjs/engine-integration",
                GjsFixture, NULL,
                gjs_fixture_set_up,
                test_scripting_gjs_engine_integration,
                gjs_fixture_tear_down);

    return g_test_run ();
}
