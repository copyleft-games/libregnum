/* test-scripting-native.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the native (.so) and Crispy (compiled C) scripting backends and
 * the shared LrgNativeFunction call ABI, plus the manager's path lookup.
 *
 * The same fixture source (fixtures/test-native-script.c) is loaded twice:
 * prebuilt by the Makefile as a shared object, and compiled at load time by
 * Crispy.  Both must behave identically.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#ifndef TEST_NATIVE_MODULE_PATH
#error "TEST_NATIVE_MODULE_PATH must name the prebuilt fixture module"
#endif
#ifndef TEST_NATIVE_SOURCE_PATH
#error "TEST_NATIVE_SOURCE_PATH must name the fixture source"
#endif
#ifndef TEST_LIBREGNUM_SRC_DIR
#error "TEST_LIBREGNUM_SRC_DIR must name libregnum's src directory"
#endif

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Host function registered as "double_it": returns 2 * first number */
static gboolean
host_double_it (LrgScripting  *scripting,
                guint          n_args,
                const GValue  *args,
                GValue        *return_value,
                gpointer       user_data,
                GError       **error)
{
    GValue d = G_VALUE_INIT;
    gint  *calls = user_data;

    (void)scripting;

    if (n_args != 1)
    {
        g_set_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE,
                     "double_it takes one number");
        return FALSE;
    }

    (*calls)++;
    g_value_init (&d, G_TYPE_DOUBLE);
    g_value_transform (&args[0], &d);
    g_value_init (return_value, G_TYPE_DOUBLE);
    g_value_set_double (return_value, 2.0 * g_value_get_double (&d));
    g_value_unset (&d);
    return TRUE;
}

/* Reads a double result, accepting any transformable numeric type */
static gdouble
value_as_double (const GValue *value)
{
    GValue  d = G_VALUE_INIT;
    gdouble out;

    g_value_init (&d, G_TYPE_DOUBLE);
    g_assert_true (g_value_transform (value, &d));
    out = g_value_get_double (&d);
    g_value_unset (&d);
    return out;
}

/*
 * Runs the shared behaviour checks against a context that already loaded
 * the fixture.  Used for both the native and the Crispy backend.
 */
static void
check_fixture_behaviour (LrgScripting *ctx)
{
    g_autoptr(GError) error = NULL;
    GValue args[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };
    GValue result = G_VALUE_INIT;
    gint   host_calls = 0;

    /* has_function sees exports and nothing else */
    g_assert_true (lrg_scripting_has_function (ctx, "fixture_add"));
    g_assert_true (lrg_scripting_has_function (ctx, LRG_SCRIPT_HOOK_START));
    g_assert_false (lrg_scripting_has_function (ctx, "no_such_export"));

    /* Mixed numeric arguments cross the ABI */
    g_value_init (&args[0], G_TYPE_INT);
    g_value_set_int (&args[0], 40);
    g_value_init (&args[1], G_TYPE_DOUBLE);
    g_value_set_double (&args[1], 1.5);
    g_value_init (&args[2], G_TYPE_INT64);
    g_value_set_int64 (&args[2], 1);
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_add", &result, 3, args, &error));
    g_assert_no_error (error);
    g_assert_cmpfloat_with_epsilon (value_as_double (&result), 42.5, 1e-9);
    g_value_unset (&result);
    g_value_unset (&args[0]);
    g_value_unset (&args[1]);
    g_value_unset (&args[2]);

    /* Strings round-trip and ownership stays with the GValue */
    g_value_init (&args[0], G_TYPE_STRING);
    g_value_set_string (&args[0], "world");
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_greet", &result, 1, args, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (g_value_get_string (&result), ==, "hello world");
    g_value_unset (&result);

    /* A NULL return location is fine: the value is discarded */
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_greet", NULL, 1, args, &error));
    g_assert_no_error (error);
    g_value_unset (&args[0]);

    /* Script errors propagate unchanged */
    g_assert_false (lrg_scripting_call_function (ctx, "fixture_fail", &result, 0, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_false (G_IS_VALUE (&result));
    g_clear_error (&error);

    /* Argument validation inside the script */
    g_assert_false (lrg_scripting_call_function (ctx, "fixture_greet", &result, 0, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    /* Missing functions are NOT_FOUND */
    g_assert_false (lrg_scripting_call_function (ctx, "no_such_export", &result, 0, NULL, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* A value-less success leaves the return location untouched */
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_nothing", &result, 0, NULL, &error));
    g_assert_false (G_IS_VALUE (&result));

    /* Host callbacks: unregistered, then registered */
    g_value_init (&args[0], G_TYPE_INT);
    g_value_set_int (&args[0], 21);
    g_assert_false (lrg_scripting_call_function (ctx, "fixture_call_back", &result, 1, args, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_true (lrg_scripting_register_function (ctx, "double_it", host_double_it,
                                                    &host_calls, &error));
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_call_back", &result, 1, args, &error));
    g_assert_no_error (error);
    g_assert_cmpfloat_with_epsilon (value_as_double (&result), 42.0, 1e-9);
    g_assert_cmpint (host_calls, ==, 1);
    g_value_unset (&result);
    g_value_unset (&args[0]);

    /* Host errors reach the caller through the script */
    g_assert_false (lrg_scripting_call_function (ctx, "fixture_call_back", &result, 0, NULL, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE);
    g_clear_error (&error);

    /* Globals are a host-side table the script can read */
    g_assert_false (lrg_scripting_call_function (ctx, "fixture_read_global", &result, 0, NULL, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_value_init (&args[0], G_TYPE_INT64);
    g_value_set_int64 (&args[0], 41);
    g_assert_true (lrg_scripting_set_global (ctx, "counter", &args[0], &error));
    g_value_unset (&args[0]);
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_read_global", &result, 0, NULL, &error));
    g_assert_cmpint (g_value_get_int64 (&result), ==, 42);
    g_value_unset (&result);
    g_assert_true (lrg_scripting_get_global (ctx, "counter", &result, &error));
    g_assert_cmpint (g_value_get_int64 (&result), ==, 41);
    g_value_unset (&result);

    /* LRG_DEFINE_SCRIPT emits ABI-conforming lifecycle hooks */
    g_value_init (&args[0], G_TYPE_DOUBLE);
    g_value_set_double (&args[0], 0.25);
    g_assert_true (lrg_scripting_call_function (ctx, LRG_SCRIPT_HOOK_START, NULL, 0, NULL, &error));
    g_assert_true (lrg_scripting_call_function (ctx, LRG_SCRIPT_HOOK_UPDATE, NULL, 1, args, &error));
    g_assert_true (lrg_scripting_call_function (ctx, LRG_SCRIPT_HOOK_UPDATE, NULL, 1, args, &error));
    g_assert_true (lrg_scripting_call_function (ctx, LRG_SCRIPT_HOOK_DETACH, NULL, 0, NULL, &error));
    g_assert_no_error (error);
    g_value_unset (&args[0]);
    g_assert_true (lrg_scripting_call_function (ctx, "fixture_lifecycle_count", &result, 0, NULL, &error));
    g_assert_cmpfloat_with_epsilon (value_as_double (&result), 110.5, 1e-9);
    g_value_unset (&result);

    /* reset drops units, host functions and globals */
    lrg_scripting_reset (ctx);
    g_assert_false (lrg_scripting_has_function (ctx, "fixture_add"));
    g_assert_false (lrg_scripting_get_global (ctx, "counter", &result, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_false (lrg_scripting_native_call_host (ctx, "double_it", 0, NULL, NULL, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Native backend
 * ========================================================================== */

static void
test_native_fixture (void)
{
    g_autoptr(LrgScriptingNative) ctx = NULL;
    g_autoptr(GError) error = NULL;

    ctx = lrg_scripting_native_new ();
    g_assert_true (lrg_scripting_load_file (LRG_SCRIPTING (ctx), TEST_NATIVE_MODULE_PATH, &error));
    g_assert_no_error (error);

    check_fixture_behaviour (LRG_SCRIPTING (ctx));
}

static void
test_native_load_errors (void)
{
    g_autoptr(LrgScriptingNative) ctx = NULL;
    g_autoptr(GError) error = NULL;

    ctx = lrg_scripting_native_new ();

    /* Missing files and non-objects are LOAD errors */
    g_assert_false (lrg_scripting_load_file (LRG_SCRIPTING (ctx), "/nonexistent/x.so", &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
    g_clear_error (&error);
    g_assert_false (lrg_scripting_load_file (LRG_SCRIPTING (ctx), TEST_NATIVE_SOURCE_PATH, &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
    g_clear_error (&error);

    /* Prebuilt objects have no source form */
    g_assert_false (lrg_scripting_load_string (LRG_SCRIPTING (ctx), "inline", "int x;", &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
    g_clear_error (&error);

    /* Setting an uninitialized value is rejected, not a crash */
    {
        GValue unset = G_VALUE_INIT;

        g_assert_false (lrg_scripting_set_global (LRG_SCRIPTING (ctx), "x", &unset, &error));
        g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_TYPE);
    }
}

static void
test_native_search_paths (void)
{
    g_autoptr(LrgScriptingNative) ctx = NULL;
    const gchar * const *paths;

    ctx = lrg_scripting_native_new ();
    paths = lrg_scripting_native_get_search_paths (ctx);
    g_assert_null (paths[0]);

    lrg_scripting_add_search_path (LRG_SCRIPTING (ctx), "/a");
    lrg_scripting_add_search_path (LRG_SCRIPTING (ctx), "/b c");
    paths = lrg_scripting_native_get_search_paths (ctx);
    g_assert_cmpstr (paths[0], ==, "/a");
    g_assert_cmpstr (paths[1], ==, "/b c");
    g_assert_null (paths[2]);
}

/* ==========================================================================
 * Manager
 * ========================================================================== */

static void
test_manager_language_for_path (void)
{
    LrgScriptingManager *manager;
    g_autoptr(LrgScripting) native = NULL;
    g_autofree gchar *module_name = NULL;

    manager = lrg_scripting_manager_get_default ();

    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "a/plugin.lua"), ==, LRG_SCRIPT_LANGUAGE_LUA);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "PLUGIN.PY"), ==, LRG_SCRIPT_LANGUAGE_PYTHON);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "x.js"), ==, LRG_SCRIPT_LANGUAGE_GJS);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "x.c"), ==, LRG_SCRIPT_LANGUAGE_CRISPY);
    module_name = g_strconcat ("lib/addon.", G_MODULE_SUFFIX, NULL);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, module_name), ==, LRG_SCRIPT_LANGUAGE_NATIVE);

    /* Unknown, extension-less, dotted directories and trailing dots */
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "x.txt"), ==, LRG_SCRIPT_LANGUAGE_NONE);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "Makefile"), ==, LRG_SCRIPT_LANGUAGE_NONE);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "dir.lua/file"), ==, LRG_SCRIPT_LANGUAGE_NONE);
    g_assert_cmpint (lrg_scripting_manager_language_for_path (manager, "file."), ==, LRG_SCRIPT_LANGUAGE_NONE);

    /* Native is always available and creates the right type */
    g_assert_true (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_NATIVE));
    native = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_NATIVE);
    g_assert_true (LRG_IS_SCRIPTING_NATIVE (native));
    g_assert_cmpstr (lrg_scripting_manager_get_extension (manager, LRG_SCRIPT_LANGUAGE_NATIVE), ==, G_MODULE_SUFFIX);
}

/* ==========================================================================
 * Crispy backend
 * ========================================================================== */

#ifdef LRG_HAS_CRISPY

/* Each Crispy test compiles into a throwaway cache, never ~/.cache */
typedef struct
{
    LrgScriptingCrispy *ctx;
    gchar              *cache_dir;
} CrispyFixture;

static void
crispy_fixture_set_up (CrispyFixture *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    (void)user_data;

    fixture->cache_dir = g_dir_make_tmp ("lrg-crispy-test-XXXXXX", &error);
    g_assert_no_error (error);
    fixture->ctx = lrg_scripting_crispy_new ();
    lrg_scripting_crispy_set_cache_dir (fixture->ctx, fixture->cache_dir);
    /* The fixture includes a libregnum header for LRG_DEFINE_SCRIPT */
    lrg_scripting_add_search_path (LRG_SCRIPTING (fixture->ctx), TEST_LIBREGNUM_SRC_DIR);
}

/* Removes the cache directory and its compiled objects */
static void
remove_tree (const gchar *path)
{
    g_autoptr(GDir) dir = NULL;
    const gchar *name;

    dir = g_dir_open (path, 0, NULL);
    if (dir != NULL)
    {
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            g_autofree gchar *child = g_build_filename (path, name, NULL);

            if (g_file_test (child, G_FILE_TEST_IS_DIR))
                remove_tree (child);
            else
                g_unlink (child);
        }
    }
    g_rmdir (path);
}

static void
crispy_fixture_tear_down (CrispyFixture *fixture,
                          gconstpointer  user_data)
{
    (void)user_data;

    g_clear_object (&fixture->ctx);
    remove_tree (fixture->cache_dir);
    g_free (fixture->cache_dir);
}

static void
test_crispy_fixture (CrispyFixture *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    (void)user_data;

    g_assert_true (lrg_scripting_load_file (LRG_SCRIPTING (fixture->ctx),
                                            TEST_NATIVE_SOURCE_PATH, &error));
    g_assert_no_error (error);

    check_fixture_behaviour (LRG_SCRIPTING (fixture->ctx));
}

static void
test_crispy_load_string_main (CrispyFixture *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    GValue result = G_VALUE_INIT;

    (void)user_data;

    /* An exported main() runs at load and must return 0 */
    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (fixture->ctx), "ok",
        "#include <glib-object.h>\n"
        "#include <gmodule.h>\n"
        "static gint ran;\n"
        "G_MODULE_EXPORT int main (int argc, char **argv) { (void)argc; (void)argv; ran = 7; return 0; }\n"
        "G_MODULE_EXPORT gboolean ran_value (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
        "{ (void)s; (void)n; (void)a; (void)e; g_value_init (r, G_TYPE_INT); g_value_set_int (r, ran); return TRUE; }\n",
        &error));
    g_assert_no_error (error);
    g_assert_true (lrg_scripting_call_function (LRG_SCRIPTING (fixture->ctx), "ran_value",
                                                &result, 0, NULL, &error));
    g_assert_cmpint (g_value_get_int (&result), ==, 7);
    g_value_unset (&result);

    /* A failing main() is a RUNTIME error and its unit is dropped */
    g_assert_false (lrg_scripting_load_string (LRG_SCRIPTING (fixture->ctx), "bad",
        "#include <gmodule.h>\n"
        "G_MODULE_EXPORT int main (int argc, char **argv) { (void)argc; (void)argv; return 3; }\n"
        "G_MODULE_EXPORT int only_in_bad (void) { return 1; }\n",
        &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME);
    g_clear_error (&error);
    g_assert_false (lrg_scripting_has_function (LRG_SCRIPTING (fixture->ctx), "only_in_bad"));

    /* Earlier units stay loaded next to later ones */
    g_assert_true (lrg_scripting_has_function (LRG_SCRIPTING (fixture->ctx), "ran_value"));
}

static void
test_crispy_compile_error (CrispyFixture *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    (void)user_data;

    g_assert_false (lrg_scripting_load_string (LRG_SCRIPTING (fixture->ctx), "broken",
                                               "this is not C;\n", &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX);
    g_assert_nonnull (strstr (error->message, "broken"));
    g_clear_error (&error);

    g_assert_false (lrg_scripting_load_file (LRG_SCRIPTING (fixture->ctx),
                                             "/nonexistent/plugin.c", &error));
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_LOAD);
}

static void
test_crispy_search_path_and_cflags (CrispyFixture *fixture,
                                    gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *include_dir = NULL;
    g_autofree gchar *header = NULL;
    GValue result = G_VALUE_INIT;

    (void)user_data;

    /* A header in a directory with a space, reached via add_search_path */
    include_dir = g_build_filename (fixture->cache_dir, "inc dir", NULL);
    g_assert_cmpint (g_mkdir_with_parents (include_dir, 0700), ==, 0);
    header = g_build_filename (include_dir, "answer.h", NULL);
    g_assert_true (g_file_set_contents (header, "#define ANSWER_BASE 40\n", -1, &error));
    lrg_scripting_add_search_path (LRG_SCRIPTING (fixture->ctx), include_dir);
    lrg_scripting_crispy_add_cflags (fixture->ctx, "-DANSWER_EXTRA=2");

    g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (fixture->ctx), "answer",
        "#include <glib-object.h>\n"
        "#include <gmodule.h>\n"
        "#include \"answer.h\"\n"
        "G_MODULE_EXPORT gboolean answer (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
        "{ (void)s; (void)n; (void)a; (void)e; g_value_init (r, G_TYPE_INT); g_value_set_int (r, ANSWER_BASE + ANSWER_EXTRA); return TRUE; }\n",
        &error));
    g_assert_no_error (error);
    g_assert_true (lrg_scripting_call_function (LRG_SCRIPTING (fixture->ctx), "answer",
                                                &result, 0, NULL, &error));
    g_assert_cmpint (g_value_get_int (&result), ==, 42);
    g_value_unset (&result);

    g_unlink (header);
    g_rmdir (include_dir);
}

static void
test_crispy_manager (void)
{
    LrgScriptingManager *manager;
    g_autoptr(LrgScripting) ctx = NULL;

    manager = lrg_scripting_manager_get_default ();
    g_assert_true (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_CRISPY));
    ctx = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_CRISPY);
    g_assert_true (LRG_IS_SCRIPTING_CRISPY (ctx));
    /* Crispy contexts are native contexts */
    g_assert_true (LRG_IS_SCRIPTING_NATIVE (ctx));
}

#endif /* LRG_HAS_CRISPY */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/scripting-native/fixture", test_native_fixture);
    g_test_add_func ("/scripting-native/load-errors", test_native_load_errors);
    g_test_add_func ("/scripting-native/search-paths", test_native_search_paths);
    g_test_add_func ("/scripting-manager/language-for-path", test_manager_language_for_path);

#ifdef LRG_HAS_CRISPY
    g_test_add ("/scripting-crispy/fixture", CrispyFixture, NULL,
                crispy_fixture_set_up, test_crispy_fixture, crispy_fixture_tear_down);
    g_test_add ("/scripting-crispy/load-string-main", CrispyFixture, NULL,
                crispy_fixture_set_up, test_crispy_load_string_main, crispy_fixture_tear_down);
    g_test_add ("/scripting-crispy/compile-error", CrispyFixture, NULL,
                crispy_fixture_set_up, test_crispy_compile_error, crispy_fixture_tear_down);
    g_test_add ("/scripting-crispy/search-path-and-cflags", CrispyFixture, NULL,
                crispy_fixture_set_up, test_crispy_search_path_and_cflags, crispy_fixture_tear_down);
    g_test_add_func ("/scripting-crispy/manager", test_crispy_manager);
#endif

    return g_test_run ();
}
