/* test-scripting.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgScripting and LrgScriptingLua.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Object for Testing
 *
 * A simple GObject for testing scripting interactions.
 * ========================================================================== */

#define TEST_TYPE_OBJECT (test_object_get_type ())
G_DECLARE_FINAL_TYPE (TestObject, test_object, TEST, OBJECT, GObject)

struct _TestObject
{
    GObject parent_instance;

    gchar  *name;
    gint    value;
};

enum
{
    PROP_TEST_0,
    PROP_TEST_NAME,
    PROP_TEST_VALUE,
    N_TEST_PROPS
};

static GParamSpec *test_properties[N_TEST_PROPS];

G_DEFINE_TYPE (TestObject, test_object, G_TYPE_OBJECT)

static void
test_object_finalize (GObject *object)
{
    TestObject *self = TEST_OBJECT (object);

    g_free (self->name);

    G_OBJECT_CLASS (test_object_parent_class)->finalize (object);
}

static void
test_object_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    TestObject *self = TEST_OBJECT (object);

    switch (prop_id)
    {
    case PROP_TEST_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_TEST_VALUE:
        g_value_set_int (value, self->value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_object_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    TestObject *self = TEST_OBJECT (object);

    switch (prop_id)
    {
    case PROP_TEST_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_TEST_VALUE:
        self->value = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_object_class_init (TestObjectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = test_object_finalize;
    object_class->get_property = test_object_get_property;
    object_class->set_property = test_object_set_property;

    test_properties[PROP_TEST_NAME] =
        g_param_spec_string ("name", "Name", "Object name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    test_properties[PROP_TEST_VALUE] =
        g_param_spec_int ("value", "Value", "Object value",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_TEST_PROPS, test_properties);
}

static void
test_object_init (TestObject *self)
{
    self->name = NULL;
    self->value = 0;
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgScriptingLua *scripting;
    LrgRegistry     *registry;
} ScriptingFixture;

static void
scripting_fixture_set_up (ScriptingFixture *fixture,
                          gconstpointer     user_data)
{
    fixture->scripting = lrg_scripting_lua_new ();
    g_assert_nonnull (fixture->scripting);

    fixture->registry = lrg_registry_new ();
    g_assert_nonnull (fixture->registry);

    /* Register test type */
    lrg_registry_register (fixture->registry, "test-object", TEST_TYPE_OBJECT);

    /* Connect scripting to registry */
    lrg_scripting_lua_set_registry (fixture->scripting, fixture->registry);
}

static void
scripting_fixture_tear_down (ScriptingFixture *fixture,
                             gconstpointer     user_data)
{
    g_clear_object (&fixture->scripting);
    g_clear_object (&fixture->registry);
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_scripting_lua_new (void)
{
    g_autoptr(LrgScriptingLua) scripting = NULL;

    scripting = lrg_scripting_lua_new ();

    g_assert_nonnull (scripting);
    g_assert_true (LRG_IS_SCRIPTING_LUA (scripting));
    g_assert_true (LRG_IS_SCRIPTING (scripting));
}

/* ==========================================================================
 * Test Cases - Script Loading
 * ========================================================================== */

static void
test_scripting_load_string (ScriptingFixture *fixture,
                            gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "x = 42",
                                        &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_load_string_syntax_error (ScriptingFixture *fixture,
                                         gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "this is not valid lua syntax !!!",
                                        &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX);
}

static void
test_scripting_load_string_runtime_error (ScriptingFixture *fixture,
                                          gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;

    /* This will cause a runtime error when executed */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "nonexistent_function()",
                                        &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_RUNTIME);
}

/* ==========================================================================
 * Test Cases - Global Variables
 * ========================================================================== */

/* Helper function to get numeric value regardless of actual GValue type */
static gdouble
get_numeric_value (const GValue *value)
{
    if (G_VALUE_HOLDS_DOUBLE (value))
        return g_value_get_double (value);
    else if (G_VALUE_HOLDS_FLOAT (value))
        return (gdouble)g_value_get_float (value);
    else if (G_VALUE_HOLDS_INT (value))
        return (gdouble)g_value_get_int (value);
    else if (G_VALUE_HOLDS_INT64 (value))
        return (gdouble)g_value_get_int64 (value);
    else if (G_VALUE_HOLDS_UINT (value))
        return (gdouble)g_value_get_uint (value);
    else if (G_VALUE_HOLDS_UINT64 (value))
        return (gdouble)g_value_get_uint64 (value);
    else
        return 0.0;
}

static void
test_scripting_set_get_global_int (ScriptingFixture *fixture,
                                   gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            set_value = G_VALUE_INIT;
    GValue            get_value = G_VALUE_INIT;
    gboolean          result;

    g_value_init (&set_value, G_TYPE_INT);
    g_value_set_int (&set_value, 42);

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_int",
                                       &set_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_int",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Lua numbers may come back as various numeric types */
    g_assert_cmpfloat_with_epsilon (get_numeric_value (&get_value), 42.0, 0.001);

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_set_get_global_string (ScriptingFixture *fixture,
                                      gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            set_value = G_VALUE_INIT;
    GValue            get_value = G_VALUE_INIT;
    gboolean          result;

    g_value_init (&set_value, G_TYPE_STRING);
    g_value_set_string (&set_value, "hello world");

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_string",
                                       &set_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_string",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    g_assert_cmpstr (g_value_get_string (&get_value), ==, "hello world");

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

static void
test_scripting_set_get_global_boolean (ScriptingFixture *fixture,
                                       gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            set_value = G_VALUE_INIT;
    GValue            get_value = G_VALUE_INIT;
    gboolean          result;

    g_value_init (&set_value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&set_value, TRUE);

    result = lrg_scripting_set_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_bool",
                                       &set_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "test_bool",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);

    g_assert_true (g_value_get_boolean (&get_value));

    g_value_unset (&set_value);
    g_value_unset (&get_value);
}

/* ==========================================================================
 * Test Cases - Function Calls
 * ========================================================================== */

static void
test_scripting_call_function (ScriptingFixture *fixture,
                              gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            return_value = G_VALUE_INIT;
    GValue            args[2] = { G_VALUE_INIT, G_VALUE_INIT };
    gboolean          result;

    /* Load a function */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "function add(a, b) return a + b end",
                                        &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Call it with arguments */
    g_value_init (&args[0], G_TYPE_DOUBLE);
    g_value_init (&args[1], G_TYPE_DOUBLE);
    g_value_set_double (&args[0], 10.0);
    g_value_set_double (&args[1], 32.0);

    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "add",
                                          &return_value,
                                          2,
                                          args,
                                          &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpfloat_with_epsilon (get_numeric_value (&return_value), 42.0, 0.001);

    g_value_unset (&args[0]);
    g_value_unset (&args[1]);
    g_value_unset (&return_value);
}

static void
test_scripting_call_function_not_found (ScriptingFixture *fixture,
                                        gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            return_value = G_VALUE_INIT;
    gboolean          result;

    result = lrg_scripting_call_function (LRG_SCRIPTING (fixture->scripting),
                                          "nonexistent_function",
                                          &return_value,
                                          0,
                                          NULL,
                                          &error);

    g_assert_false (result);
    g_assert_error (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_NOT_FOUND);

    if (G_IS_VALUE (&return_value))
        g_value_unset (&return_value);
}

/* ==========================================================================
 * Test Cases - C Function Registration
 * ========================================================================== */

static gboolean
test_c_function (LrgScripting  *scripting,
                 guint          n_args,
                 const GValue  *args,
                 GValue        *return_value,
                 gpointer       user_data,
                 GError       **error)
{
    gdouble result = 0;
    guint   i;

    /* Sum all numeric arguments - Lua numbers can be G_TYPE_INT64 or G_TYPE_DOUBLE */
    for (i = 0; i < n_args; i++)
    {
        result += get_numeric_value (&args[i]);
    }

    g_value_init (return_value, G_TYPE_DOUBLE);
    g_value_set_double (return_value, result);

    return TRUE;
}

static void
test_scripting_register_function (ScriptingFixture *fixture,
                                  gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            get_value = G_VALUE_INIT;
    gboolean          result;

    result = lrg_scripting_register_function (LRG_SCRIPTING (fixture->scripting),
                                              "sum_all",
                                              test_c_function,
                                              NULL,
                                              &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Now call the registered function from Lua */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "result = sum_all(1, 2, 3, 4, 5)",
                                        &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Check the result */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "result",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpfloat_with_epsilon (get_numeric_value (&get_value), 15.0, 0.001);

    g_value_unset (&get_value);
}

/* ==========================================================================
 * Test Cases - Registry Integration
 * ========================================================================== */

static void
test_scripting_registry (ScriptingFixture *fixture,
                         gconstpointer     user_data)
{
    LrgRegistry *registry;

    registry = lrg_scripting_lua_get_registry (fixture->scripting);
    g_assert_nonnull (registry);
    g_assert_true (registry == fixture->registry);
}

/* ==========================================================================
 * Test Cases - Update Hooks
 * ========================================================================== */

static void
test_scripting_update_hooks (ScriptingFixture *fixture,
                             gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    GValue            get_value = G_VALUE_INIT;
    gboolean          result;

    /* Create an update hook */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "total_delta = 0\n"
                                        "function game_update(delta)\n"
                                        "    total_delta = total_delta + delta\n"
                                        "end",
                                        &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Register the update hook (void return, no error param) */
    lrg_scripting_lua_register_update_hook (fixture->scripting, "game_update");

    /* Call update several times */
    lrg_scripting_lua_update (fixture->scripting, 0.016f);
    lrg_scripting_lua_update (fixture->scripting, 0.016f);
    lrg_scripting_lua_update (fixture->scripting, 0.016f);

    /* Check total delta is approximately correct */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "total_delta",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_assert_no_error (error);
    g_assert_cmpfloat_with_epsilon (get_numeric_value (&get_value), 0.048, 0.001);

    g_value_unset (&get_value);
}

/* ==========================================================================
 * Test Cases - Reset
 * ========================================================================== */

static void
test_scripting_reset (ScriptingFixture *fixture,
                      gconstpointer     user_data)
{
    g_autoptr(GError) error = NULL;
    gboolean          result;
    GValue            get_value = G_VALUE_INIT;

    /* Set a global */
    result = lrg_scripting_load_string (LRG_SCRIPTING (fixture->scripting),
                                        "test",
                                        "persistent_value = 42",
                                        &error);
    g_assert_true (result);
    g_assert_no_error (error);

    /* Verify it exists */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "persistent_value",
                                       &get_value,
                                       &error);
    g_assert_true (result);
    g_value_unset (&get_value);

    /* Reset the scripting context */
    lrg_scripting_reset (LRG_SCRIPTING (fixture->scripting));

    /*
     * After reset, the global should no longer exist.
     * get_global may return TRUE with nil value, or the implementation
     * may have been cleared. Either way, verify the value is not 42.
     */
    result = lrg_scripting_get_global (LRG_SCRIPTING (fixture->scripting),
                                       "persistent_value",
                                       &get_value,
                                       &error);
    if (result && G_IS_VALUE (&get_value))
    {
        /* If we got a value, verify it's nil (not the old value) */
        g_assert_false (G_VALUE_HOLDS_INT64 (&get_value) ||
                        G_VALUE_HOLDS_DOUBLE (&get_value));
        g_value_unset (&get_value);
    }
    /* If result is FALSE, that's also acceptable - value doesn't exist */
}

/* ==========================================================================
 * Test Cases - Engine Integration
 * ========================================================================== */

static void
test_scripting_engine_integration (void)
{
    g_autoptr(LrgEngine) engine = NULL;
    g_autoptr(LrgScriptingLua) scripting = NULL;
    LrgScripting *retrieved;

    engine = g_object_new (LRG_TYPE_ENGINE, NULL);
    g_assert_nonnull (engine);

    scripting = lrg_scripting_lua_new ();
    g_assert_nonnull (scripting);

    /* Set scripting on engine */
    lrg_engine_set_scripting (engine, LRG_SCRIPTING (scripting));

    /* Get it back */
    retrieved = lrg_engine_get_scripting (engine);
    g_assert_nonnull (retrieved);
    g_assert_true (LRG_IS_SCRIPTING_LUA (retrieved));
    g_assert_true (retrieved == LRG_SCRIPTING (scripting));

    /* Clear scripting */
    lrg_engine_set_scripting (engine, NULL);
    retrieved = lrg_engine_get_scripting (engine);
    g_assert_null (retrieved);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/scripting/new", test_scripting_lua_new);

    /* Script loading */
    g_test_add ("/scripting/load-string/basic",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_load_string,
                scripting_fixture_tear_down);

    g_test_add ("/scripting/load-string/syntax-error",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_load_string_syntax_error,
                scripting_fixture_tear_down);

    g_test_add ("/scripting/load-string/runtime-error",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_load_string_runtime_error,
                scripting_fixture_tear_down);

    /* Global variables */
    g_test_add ("/scripting/global/int",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_set_get_global_int,
                scripting_fixture_tear_down);

    g_test_add ("/scripting/global/string",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_set_get_global_string,
                scripting_fixture_tear_down);

    g_test_add ("/scripting/global/boolean",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_set_get_global_boolean,
                scripting_fixture_tear_down);

    /* Function calls */
    g_test_add ("/scripting/call-function/basic",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_call_function,
                scripting_fixture_tear_down);

    g_test_add ("/scripting/call-function/not-found",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_call_function_not_found,
                scripting_fixture_tear_down);

    /* C function registration */
    g_test_add ("/scripting/register-function",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_register_function,
                scripting_fixture_tear_down);

    /* Registry integration */
    g_test_add ("/scripting/registry",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_registry,
                scripting_fixture_tear_down);

    /* Update hooks */
    g_test_add ("/scripting/update-hooks",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_update_hooks,
                scripting_fixture_tear_down);

    /* Reset */
    g_test_add ("/scripting/reset",
                ScriptingFixture, NULL,
                scripting_fixture_set_up,
                test_scripting_reset,
                scripting_fixture_tear_down);

    /* Engine integration */
    g_test_add_func ("/scripting/engine-integration", test_scripting_engine_integration);

    return g_test_run ();
}
