/* test-scripting-gi.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgScriptingGI base class.
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Subclass for Testing Abstract LrgScriptingGI
 *
 * A minimal concrete implementation of LrgScriptingGI for testing
 * the base class functionality.
 * ========================================================================== */

#define TEST_TYPE_SCRIPTING_GI_MOCK (test_scripting_gi_mock_get_type ())
G_DECLARE_FINAL_TYPE (TestScriptingGIMock, test_scripting_gi_mock,
                      TEST, SCRIPTING_GI_MOCK, LrgScriptingGI)

struct _TestScriptingGIMock
{
    LrgScriptingGI parent_instance;

    gboolean  init_called;
    gboolean  finalize_called;
    gint      update_hook_call_count;
    gfloat    last_delta;
    gchar    *last_hook_name;
};

G_DEFINE_TYPE (TestScriptingGIMock, test_scripting_gi_mock, LRG_TYPE_SCRIPTING_GI)

static gboolean
test_scripting_gi_mock_init_interpreter (LrgScriptingGI  *self,
                                         GError         **error)
{
    TestScriptingGIMock *mock = TEST_SCRIPTING_GI_MOCK (self);

    mock->init_called = TRUE;

    return TRUE;
}

static void
test_scripting_gi_mock_finalize_interpreter (LrgScriptingGI *self)
{
    TestScriptingGIMock *mock = TEST_SCRIPTING_GI_MOCK (self);

    mock->finalize_called = TRUE;
}

static gboolean
test_scripting_gi_mock_expose_typelib (LrgScriptingGI  *self,
                                       const gchar     *namespace_,
                                       const gchar     *version,
                                       GError         **error)
{
    /* Mock implementation - just accept anything */
    (void)self;
    (void)namespace_;
    (void)version;

    return TRUE;
}

static gboolean
test_scripting_gi_mock_expose_gobject (LrgScriptingGI  *self,
                                       const gchar     *name,
                                       GObject         *object,
                                       GError         **error)
{
    /* Mock implementation - just accept anything */
    (void)self;
    (void)name;
    (void)object;

    return TRUE;
}

static gboolean
test_scripting_gi_mock_call_update_hook (LrgScriptingGI  *self,
                                         const gchar     *func_name,
                                         gfloat           delta,
                                         GError         **error)
{
    TestScriptingGIMock *mock = TEST_SCRIPTING_GI_MOCK (self);

    mock->update_hook_call_count++;
    mock->last_delta = delta;
    g_free (mock->last_hook_name);
    mock->last_hook_name = g_strdup (func_name);

    return TRUE;
}

static void
test_scripting_gi_mock_update_search_paths (LrgScriptingGI *self)
{
    /* Mock implementation - nothing to do */
    (void)self;
}

static const gchar *
test_scripting_gi_mock_get_interpreter_name (LrgScriptingGI *self)
{
    (void)self;
    return "MockGI";
}

static gboolean
test_scripting_gi_mock_load_file (LrgScripting  *scripting,
                                  const gchar   *filename,
                                  GError       **error)
{
    (void)scripting;
    (void)filename;
    (void)error;

    return TRUE;
}

static gboolean
test_scripting_gi_mock_load_string (LrgScripting  *scripting,
                                    const gchar   *name,
                                    const gchar   *code,
                                    GError       **error)
{
    (void)scripting;
    (void)name;
    (void)code;
    (void)error;

    return TRUE;
}

static void
test_scripting_gi_mock_finalize (GObject *object)
{
    TestScriptingGIMock *self = TEST_SCRIPTING_GI_MOCK (object);

    g_free (self->last_hook_name);

    G_OBJECT_CLASS (test_scripting_gi_mock_parent_class)->finalize (object);
}

static void
test_scripting_gi_mock_class_init (TestScriptingGIMockClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgScriptingClass   *scripting_class = LRG_SCRIPTING_CLASS (klass);
    LrgScriptingGIClass *gi_class = LRG_SCRIPTING_GI_CLASS (klass);

    object_class->finalize = test_scripting_gi_mock_finalize;

    /* Override LrgScripting virtual methods */
    scripting_class->load_file = test_scripting_gi_mock_load_file;
    scripting_class->load_string = test_scripting_gi_mock_load_string;

    /* Override LrgScriptingGI virtual methods */
    gi_class->init_interpreter = test_scripting_gi_mock_init_interpreter;
    gi_class->finalize_interpreter = test_scripting_gi_mock_finalize_interpreter;
    gi_class->expose_typelib = test_scripting_gi_mock_expose_typelib;
    gi_class->expose_gobject = test_scripting_gi_mock_expose_gobject;
    gi_class->call_update_hook = test_scripting_gi_mock_call_update_hook;
    gi_class->update_search_paths = test_scripting_gi_mock_update_search_paths;
    gi_class->get_interpreter_name = test_scripting_gi_mock_get_interpreter_name;
}

static void
test_scripting_gi_mock_init (TestScriptingGIMock *self)
{
    self->init_called = FALSE;
    self->finalize_called = FALSE;
    self->update_hook_call_count = 0;
    self->last_delta = 0.0f;
    self->last_hook_name = NULL;
}

static TestScriptingGIMock *
test_scripting_gi_mock_new (void)
{
    return g_object_new (TEST_TYPE_SCRIPTING_GI_MOCK, NULL);
}

/* ==========================================================================
 * Test Fixture
 * ========================================================================== */

typedef struct
{
    TestScriptingGIMock *mock;
    LrgRegistry         *registry;
} GIScriptingFixture;

static void
gi_fixture_set_up (GIScriptingFixture *fixture,
                   gconstpointer       user_data)
{
    (void)user_data;

    fixture->mock = test_scripting_gi_mock_new ();
    g_assert_nonnull (fixture->mock);

    fixture->registry = lrg_registry_new ();
    g_assert_nonnull (fixture->registry);
}

static void
gi_fixture_tear_down (GIScriptingFixture *fixture,
                      gconstpointer       user_data)
{
    (void)user_data;

    g_clear_object (&fixture->mock);
    g_clear_object (&fixture->registry);
}

/* ==========================================================================
 * Construction Tests
 * ========================================================================== */

static void
test_scripting_gi_new (void)
{
    g_autoptr(TestScriptingGIMock) mock = NULL;

    mock = test_scripting_gi_mock_new ();

    g_assert_nonnull (mock);
    g_assert_true (LRG_IS_SCRIPTING_GI (mock));
    g_assert_true (LRG_IS_SCRIPTING (mock));
}

static void
test_scripting_gi_type_hierarchy (void)
{
    g_autoptr(TestScriptingGIMock) mock = NULL;

    mock = test_scripting_gi_mock_new ();

    /* Verify type hierarchy */
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (mock, LRG_TYPE_SCRIPTING_GI));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (mock, LRG_TYPE_SCRIPTING));
    g_assert_true (G_TYPE_CHECK_INSTANCE_TYPE (mock, G_TYPE_OBJECT));
}

/* ==========================================================================
 * Registry Integration Tests
 * ========================================================================== */

static void
test_scripting_gi_registry_set_get (GIScriptingFixture *fixture,
                                    gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    LrgRegistry    *retrieved;

    (void)user_data;

    /* Initially NULL */
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_null (retrieved);

    /* Set registry */
    lrg_scripting_gi_set_registry (gi_self, fixture->registry);

    /* Verify retrieval */
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_true (retrieved == fixture->registry);

    /* Set to NULL */
    lrg_scripting_gi_set_registry (gi_self, NULL);
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_null (retrieved);
}

static void
test_scripting_gi_registry_weak_reference (void)
{
    g_autoptr(TestScriptingGIMock) mock = NULL;
    LrgScriptingGI *gi_self;
    LrgRegistry    *registry;
    LrgRegistry    *retrieved;

    mock = test_scripting_gi_mock_new ();
    gi_self = LRG_SCRIPTING_GI (mock);

    /* Create a registry and set it */
    registry = lrg_registry_new ();
    lrg_scripting_gi_set_registry (gi_self, registry);

    /* Verify it's set */
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_true (retrieved == registry);

    /*
     * Note: The registry pointer is stored without GObject weak pointer
     * semantics. It's a "weak reference" in the sense that it doesn't
     * hold a reference count - the caller is responsible for ensuring
     * the registry outlives the scripting context or clearing it before
     * destruction.
     */

    /* Clean up - clear the reference before destroying the registry */
    lrg_scripting_gi_set_registry (gi_self, NULL);
    g_object_unref (registry);

    /* Should now be NULL */
    retrieved = lrg_scripting_gi_get_registry (gi_self);
    g_assert_null (retrieved);
}

/* ==========================================================================
 * Engine Integration Tests
 * ========================================================================== */

static void
test_scripting_gi_engine_set_get (GIScriptingFixture *fixture,
                                  gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    LrgEngine      *engine;
    LrgEngine      *retrieved;

    (void)user_data;

    /* Initially NULL */
    retrieved = lrg_scripting_gi_get_engine (gi_self);
    g_assert_null (retrieved);

    /* Get and set engine */
    engine = lrg_engine_get_default ();
    lrg_scripting_gi_set_engine (gi_self, engine);

    /* Verify retrieval */
    retrieved = lrg_scripting_gi_get_engine (gi_self);
    g_assert_true (retrieved == engine);

    /* Set to NULL */
    lrg_scripting_gi_set_engine (gi_self, NULL);
    retrieved = lrg_scripting_gi_get_engine (gi_self);
    g_assert_null (retrieved);
}

/* ==========================================================================
 * Search Paths Tests
 * ========================================================================== */

static void
test_scripting_gi_search_paths_add (GIScriptingFixture *fixture,
                                    gconstpointer       user_data)
{
    LrgScriptingGI     *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    const gchar *const *paths;

    (void)user_data;

    /* Initially empty */
    paths = lrg_scripting_gi_get_search_paths (gi_self);
    g_assert_nonnull (paths);
    g_assert_null (paths[0]);

    /* Add path */
    lrg_scripting_gi_add_search_path (gi_self, "/path/one");
    paths = lrg_scripting_gi_get_search_paths (gi_self);
    g_assert_nonnull (paths);
    g_assert_cmpstr (paths[0], ==, "/path/one");
    g_assert_null (paths[1]);

    /* Add another path */
    lrg_scripting_gi_add_search_path (gi_self, "/path/two");
    paths = lrg_scripting_gi_get_search_paths (gi_self);
    g_assert_nonnull (paths);
    g_assert_cmpstr (paths[0], ==, "/path/one");
    g_assert_cmpstr (paths[1], ==, "/path/two");
    g_assert_null (paths[2]);
}

static void
test_scripting_gi_search_paths_clear (GIScriptingFixture *fixture,
                                      gconstpointer       user_data)
{
    LrgScriptingGI     *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    const gchar *const *paths;

    (void)user_data;

    /* Add some paths */
    lrg_scripting_gi_add_search_path (gi_self, "/path/one");
    lrg_scripting_gi_add_search_path (gi_self, "/path/two");

    /* Clear them */
    lrg_scripting_gi_clear_search_paths (gi_self);

    /* Should be empty */
    paths = lrg_scripting_gi_get_search_paths (gi_self);
    g_assert_nonnull (paths);
    g_assert_null (paths[0]);
}

/* ==========================================================================
 * Update Hooks Tests
 * ========================================================================== */

static void
test_scripting_gi_update_hooks_register (GIScriptingFixture *fixture,
                                         gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);

    (void)user_data;

    /* Register a hook */
    lrg_scripting_gi_register_update_hook (gi_self, "game_update");

    /* Call update - hook should be called */
    lrg_scripting_gi_update (gi_self, 0.016f);

    g_assert_cmpint (fixture->mock->update_hook_call_count, ==, 1);
    g_assert_cmpstr (fixture->mock->last_hook_name, ==, "game_update");
    g_assert_cmpfloat_with_epsilon (fixture->mock->last_delta, 0.016f, 0.0001f);
}

static void
test_scripting_gi_update_hooks_multiple (GIScriptingFixture *fixture,
                                         gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);

    (void)user_data;

    /* Register multiple hooks */
    lrg_scripting_gi_register_update_hook (gi_self, "update1");
    lrg_scripting_gi_register_update_hook (gi_self, "update2");
    lrg_scripting_gi_register_update_hook (gi_self, "update3");

    /* Call update - all hooks should be called */
    lrg_scripting_gi_update (gi_self, 0.033f);

    g_assert_cmpint (fixture->mock->update_hook_call_count, ==, 3);
}

static void
test_scripting_gi_update_hooks_unregister (GIScriptingFixture *fixture,
                                           gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    gboolean        removed;

    (void)user_data;

    /* Register and then unregister */
    lrg_scripting_gi_register_update_hook (gi_self, "my_update");

    removed = lrg_scripting_gi_unregister_update_hook (gi_self, "my_update");
    g_assert_true (removed);

    /* Try to unregister again - should return FALSE */
    removed = lrg_scripting_gi_unregister_update_hook (gi_self, "my_update");
    g_assert_false (removed);

    /* Call update - no hooks should be called */
    lrg_scripting_gi_update (gi_self, 0.016f);
    g_assert_cmpint (fixture->mock->update_hook_call_count, ==, 0);
}

static void
test_scripting_gi_update_hooks_clear (GIScriptingFixture *fixture,
                                      gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);

    (void)user_data;

    /* Register multiple hooks */
    lrg_scripting_gi_register_update_hook (gi_self, "update1");
    lrg_scripting_gi_register_update_hook (gi_self, "update2");

    /* Clear all */
    lrg_scripting_gi_clear_update_hooks (gi_self);

    /* Call update - no hooks should be called */
    lrg_scripting_gi_update (gi_self, 0.016f);
    g_assert_cmpint (fixture->mock->update_hook_call_count, ==, 0);
}

/* ==========================================================================
 * Typelib Loading Tests
 * ========================================================================== */

static void
test_scripting_gi_typelib_require_glib (GIScriptingFixture *fixture,
                                        gconstpointer       user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    g_autoptr(GError) error = NULL;
    gboolean          result;

    (void)user_data;

    /* Load GLib typelib - should succeed */
    result = lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error);

    g_assert_true (result);
    g_assert_no_error (error);
}

static void
test_scripting_gi_typelib_require_not_found (GIScriptingFixture *fixture,
                                             gconstpointer       user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    g_autoptr(GError) error = NULL;
    gboolean          result;

    (void)user_data;

    /* Try to load non-existent typelib */
    result = lrg_scripting_gi_require_typelib (gi_self, "NonExistent", "1.0", &error);

    /* Should fail with an error - the exact domain depends on GIRepository */
    g_assert_false (result);
    g_assert_nonnull (error);
}

static void
test_scripting_gi_typelib_duplicate_load (GIScriptingFixture *fixture,
                                          gconstpointer       user_data)
{
    LrgScriptingGI   *gi_self = LRG_SCRIPTING_GI (fixture->mock);
    g_autoptr(GError) error = NULL;
    gboolean          result;

    (void)user_data;

    /* Load GLib typelib twice - second should succeed (cached) */
    result = lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error);
    g_assert_true (result);
    g_assert_no_error (error);

    result = lrg_scripting_gi_require_typelib (gi_self, "GLib", "2.0", &error);
    g_assert_true (result);
    g_assert_no_error (error);
}

/* ==========================================================================
 * Registered Functions Tests
 * ========================================================================== */

static void
test_scripting_gi_has_registered_function (GIScriptingFixture *fixture,
                                           gconstpointer       user_data)
{
    LrgScriptingGI *gi_self = LRG_SCRIPTING_GI (fixture->mock);

    (void)user_data;

    /* Initially no functions registered */
    g_assert_false (lrg_scripting_gi_has_registered_function (gi_self, "my_func"));
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
    g_test_add_func ("/scripting-gi/new", test_scripting_gi_new);
    g_test_add_func ("/scripting-gi/type-hierarchy", test_scripting_gi_type_hierarchy);

    /* Registry integration tests */
    g_test_add ("/scripting-gi/registry/set-get",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_registry_set_get,
                gi_fixture_tear_down);

    g_test_add_func ("/scripting-gi/registry/weak-reference",
                     test_scripting_gi_registry_weak_reference);

    /* Engine integration tests */
    g_test_add ("/scripting-gi/engine/set-get",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_engine_set_get,
                gi_fixture_tear_down);

    /* Search paths tests */
    g_test_add ("/scripting-gi/search-paths/add",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_search_paths_add,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/search-paths/clear",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_search_paths_clear,
                gi_fixture_tear_down);

    /* Update hooks tests */
    g_test_add ("/scripting-gi/update-hooks/register",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_update_hooks_register,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/update-hooks/multiple",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_update_hooks_multiple,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/update-hooks/unregister",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_update_hooks_unregister,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/update-hooks/clear",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_update_hooks_clear,
                gi_fixture_tear_down);

    /* Typelib loading tests */
    g_test_add ("/scripting-gi/typelib/require-glib",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_typelib_require_glib,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/typelib/require-not-found",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_typelib_require_not_found,
                gi_fixture_tear_down);

    g_test_add ("/scripting-gi/typelib/duplicate-load",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_typelib_duplicate_load,
                gi_fixture_tear_down);

    /* Registered functions tests */
    g_test_add ("/scripting-gi/functions/has",
                GIScriptingFixture, NULL,
                gi_fixture_set_up,
                test_scripting_gi_has_registered_function,
                gi_fixture_tear_down);

    return g_test_run ();
}
