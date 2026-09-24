/* test-mod.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Mod module.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgModManifest *manifest;
} ManifestFixture;

typedef struct
{
    LrgModLoader *loader;
    gchar        *test_dir;
} LoaderFixture;

typedef struct
{
    LrgModManager *manager;
    gchar         *test_dir;
} ManagerFixture;

static void
manifest_fixture_set_up (ManifestFixture *fixture,
                         gconstpointer    user_data)
{
    (void)user_data;
    fixture->manifest = lrg_mod_manifest_new ("test-mod");
    g_assert_nonnull (fixture->manifest);
}

static void
manifest_fixture_tear_down (ManifestFixture *fixture,
                            gconstpointer    user_data)
{
    (void)user_data;
    g_clear_object (&fixture->manifest);
}

static gchar *
create_test_dir (void)
{
    gchar *template;
    gchar *path;

    template = g_build_filename (g_get_tmp_dir (), "lrg-mod-test-XXXXXX", NULL);
    path = g_mkdtemp (template);
    g_assert_nonnull (path);

    return path;
}

static void
remove_test_dir (const gchar *path)
{
    GDir *dir;
    const gchar *name;

    if (path == NULL)
        return;

    dir = g_dir_open (path, 0, NULL);
    if (dir != NULL)
    {
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            g_autofree gchar *child = g_build_filename (path, name, NULL);
            if (g_file_test (child, G_FILE_TEST_IS_DIR))
                remove_test_dir (child);
            else
                g_remove (child);
        }
        g_dir_close (dir);
    }

    g_rmdir (path);
}

static void
create_mod_manifest (const gchar *mod_dir,
                     const gchar *mod_id,
                     const gchar *extra_yaml)
{
    g_autofree gchar *manifest_path = NULL;
    GString *yaml;

    g_mkdir_with_parents (mod_dir, 0755);

    manifest_path = g_build_filename (mod_dir, "mod.yaml", NULL);

    yaml = g_string_new (NULL);
    g_string_append_printf (yaml, "id: %s\n", mod_id);
    g_string_append (yaml, "name: Test Mod\n");
    g_string_append (yaml, "version: 1.0.0\n");
    g_string_append (yaml, "type: data\n");

    if (extra_yaml != NULL)
        g_string_append (yaml, extra_yaml);

    g_file_set_contents (manifest_path, yaml->str, yaml->len, NULL);
    g_string_free (yaml, TRUE);
}

static void
loader_fixture_set_up (LoaderFixture *fixture,
                       gconstpointer  user_data)
{
    (void)user_data;
    fixture->loader = lrg_mod_loader_new ();
    fixture->test_dir = create_test_dir ();
    g_assert_nonnull (fixture->loader);
}

static void
loader_fixture_tear_down (LoaderFixture *fixture,
                          gconstpointer  user_data)
{
    (void)user_data;
    g_clear_object (&fixture->loader);
    remove_test_dir (fixture->test_dir);
    g_free (fixture->test_dir);
}

static void
manager_fixture_set_up (ManagerFixture *fixture,
                        gconstpointer   user_data)
{
    (void)user_data;
    fixture->manager = lrg_mod_manager_new ();
    fixture->test_dir = create_test_dir ();
    g_assert_nonnull (fixture->manager);
}

static void
manager_fixture_tear_down (ManagerFixture *fixture,
                           gconstpointer   user_data)
{
    (void)user_data;
    g_clear_object (&fixture->manager);
    remove_test_dir (fixture->test_dir);
    g_free (fixture->test_dir);
}

/* ==========================================================================
 * Mod Dependency Tests
 * ========================================================================== */

static void
test_mod_dependency_new (void)
{
    LrgModDependency *dep;

    dep = lrg_mod_dependency_new ("other-mod", "1.0", FALSE);
    g_assert_nonnull (dep);

    g_assert_cmpstr (lrg_mod_dependency_get_mod_id (dep), ==, "other-mod");
    g_assert_cmpstr (lrg_mod_dependency_get_min_version (dep), ==, "1.0");
    g_assert_false (lrg_mod_dependency_is_optional (dep));

    lrg_mod_dependency_free (dep);
}

static void
test_mod_dependency_optional (void)
{
    LrgModDependency *dep;

    dep = lrg_mod_dependency_new ("opt-mod", NULL, TRUE);
    g_assert_nonnull (dep);

    g_assert_cmpstr (lrg_mod_dependency_get_mod_id (dep), ==, "opt-mod");
    g_assert_null (lrg_mod_dependency_get_min_version (dep));
    g_assert_true (lrg_mod_dependency_is_optional (dep));

    lrg_mod_dependency_free (dep);
}

static void
test_mod_dependency_copy (void)
{
    LrgModDependency *dep;
    LrgModDependency *copy;

    dep = lrg_mod_dependency_new ("copy-mod", "2.0", TRUE);
    copy = lrg_mod_dependency_copy (dep);

    g_assert_nonnull (copy);
    g_assert_cmpstr (lrg_mod_dependency_get_mod_id (copy), ==, "copy-mod");
    g_assert_cmpstr (lrg_mod_dependency_get_min_version (copy), ==, "2.0");
    g_assert_true (lrg_mod_dependency_is_optional (copy));

    lrg_mod_dependency_free (dep);
    lrg_mod_dependency_free (copy);
}

static void
test_mod_dependency_copy_null (void)
{
    LrgModDependency *copy;

    copy = lrg_mod_dependency_copy (NULL);
    g_assert_null (copy);
}

static void
test_mod_dependency_free_null (void)
{
    /* Should not crash */
    lrg_mod_dependency_free (NULL);
}

/* ==========================================================================
 * Manifest Tests
 * ========================================================================== */

static void
test_manifest_new (ManifestFixture *fixture,
                   gconstpointer    user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_MOD_MANIFEST (fixture->manifest));
    g_assert_cmpstr (lrg_mod_manifest_get_id (fixture->manifest), ==, "test-mod");
}

static void
test_manifest_identity (ManifestFixture *fixture,
                        gconstpointer    user_data)
{
    (void)user_data;

    lrg_mod_manifest_set_name (fixture->manifest, "My Mod");
    g_assert_cmpstr (lrg_mod_manifest_get_name (fixture->manifest), ==, "My Mod");

    lrg_mod_manifest_set_version (fixture->manifest, "1.2.3");
    g_assert_cmpstr (lrg_mod_manifest_get_version (fixture->manifest), ==, "1.2.3");

    lrg_mod_manifest_set_description (fixture->manifest, "A test mod");
    g_assert_cmpstr (lrg_mod_manifest_get_description (fixture->manifest),
                     ==, "A test mod");

    lrg_mod_manifest_set_author (fixture->manifest, "Tester");
    g_assert_cmpstr (lrg_mod_manifest_get_author (fixture->manifest), ==, "Tester");
}

static void
test_manifest_type_and_priority (ManifestFixture *fixture,
                                 gconstpointer    user_data)
{
    (void)user_data;

    /* Default type */
    g_assert_cmpint (lrg_mod_manifest_get_mod_type (fixture->manifest),
                     ==, LRG_MOD_TYPE_DATA);

    lrg_mod_manifest_set_mod_type (fixture->manifest, LRG_MOD_TYPE_SCRIPT);
    g_assert_cmpint (lrg_mod_manifest_get_mod_type (fixture->manifest),
                     ==, LRG_MOD_TYPE_SCRIPT);

    /* Default priority */
    g_assert_cmpint (lrg_mod_manifest_get_priority (fixture->manifest),
                     ==, LRG_MOD_PRIORITY_NORMAL);

    lrg_mod_manifest_set_priority (fixture->manifest, LRG_MOD_PRIORITY_HIGH);
    g_assert_cmpint (lrg_mod_manifest_get_priority (fixture->manifest),
                     ==, LRG_MOD_PRIORITY_HIGH);
}

static void
test_manifest_dependencies (ManifestFixture *fixture,
                            gconstpointer    user_data)
{
    GPtrArray *deps;

    (void)user_data;

    /* No dependencies initially */
    deps = lrg_mod_manifest_get_dependencies (fixture->manifest);
    g_assert_cmpuint (deps->len, ==, 0);

    /* Add dependency */
    lrg_mod_manifest_add_dependency (fixture->manifest, "dep-mod", "1.0", FALSE);

    deps = lrg_mod_manifest_get_dependencies (fixture->manifest);
    g_assert_cmpuint (deps->len, ==, 1);

    g_assert_true (lrg_mod_manifest_has_dependency (fixture->manifest, "dep-mod"));
    g_assert_false (lrg_mod_manifest_has_dependency (fixture->manifest, "other"));
}

static void
test_manifest_load_order (ManifestFixture *fixture,
                          gconstpointer    user_data)
{
    GPtrArray *load_after;
    GPtrArray *load_before;

    (void)user_data;

    lrg_mod_manifest_add_load_after (fixture->manifest, "base-mod");
    lrg_mod_manifest_add_load_before (fixture->manifest, "ui-mod");

    load_after = lrg_mod_manifest_get_load_after (fixture->manifest);
    g_assert_cmpuint (load_after->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (load_after, 0), ==, "base-mod");

    load_before = lrg_mod_manifest_get_load_before (fixture->manifest);
    g_assert_cmpuint (load_before->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (load_before, 0), ==, "ui-mod");
}

static void
test_manifest_paths (ManifestFixture *fixture,
                     gconstpointer    user_data)
{
    (void)user_data;

    lrg_mod_manifest_set_data_path (fixture->manifest, "assets");
    g_assert_cmpstr (lrg_mod_manifest_get_data_path (fixture->manifest),
                     ==, "assets");

    lrg_mod_manifest_set_entry_point (fixture->manifest, "main.lua");
    g_assert_cmpstr (lrg_mod_manifest_get_entry_point (fixture->manifest),
                     ==, "main.lua");
}

/* ==========================================================================
 * Loader Tests
 * ========================================================================== */

static void
test_loader_new (LoaderFixture *fixture,
                 gconstpointer  user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_MOD_LOADER (fixture->loader));
}

static void
test_loader_search_paths (LoaderFixture *fixture,
                          gconstpointer  user_data)
{
    GPtrArray *paths;

    (void)user_data;

    paths = lrg_mod_loader_get_search_paths (fixture->loader);
    g_assert_cmpuint (paths->len, ==, 0);

    lrg_mod_loader_add_search_path (fixture->loader, "/path/to/mods");
    lrg_mod_loader_add_search_path (fixture->loader, "/another/path");

    paths = lrg_mod_loader_get_search_paths (fixture->loader);
    g_assert_cmpuint (paths->len, ==, 2);

    lrg_mod_loader_clear_search_paths (fixture->loader);
    paths = lrg_mod_loader_get_search_paths (fixture->loader);
    g_assert_cmpuint (paths->len, ==, 0);
}

static void
test_loader_manifest_filename (LoaderFixture *fixture,
                               gconstpointer  user_data)
{
    (void)user_data;

    /* Default */
    g_assert_cmpstr (lrg_mod_loader_get_manifest_filename (fixture->loader),
                     ==, "mod.yaml");

    lrg_mod_loader_set_manifest_filename (fixture->loader, "manifest.yml");
    g_assert_cmpstr (lrg_mod_loader_get_manifest_filename (fixture->loader),
                     ==, "manifest.yml");
}

static void
test_loader_load_mod (LoaderFixture *fixture,
                      gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "mymod", NULL);
    create_mod_manifest (mod_dir, "my-mod", NULL);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, &error);
    g_assert_no_error (error);
    g_assert_nonnull (mod);
    g_assert_cmpstr (lrg_mod_get_id (mod), ==, "my-mod");
}

static void
test_loader_load_mod_not_found (LoaderFixture *fixture,
                                gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "nonexistent", NULL);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, &error);
    g_assert_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_NOT_FOUND);
    g_assert_null (mod);
}

static void
test_loader_discover (LoaderFixture *fixture,
                      gconstpointer  user_data)
{
    g_autofree gchar *mod_dir1 = NULL;
    g_autofree gchar *mod_dir2 = NULL;
    g_autoptr(GPtrArray) mods = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    /* Create two mods */
    mod_dir1 = g_build_filename (fixture->test_dir, "mod-a", NULL);
    mod_dir2 = g_build_filename (fixture->test_dir, "mod-b", NULL);
    create_mod_manifest (mod_dir1, "mod-a", NULL);
    create_mod_manifest (mod_dir2, "mod-b", NULL);

    mods = lrg_mod_loader_discover_at (fixture->loader, fixture->test_dir, &error);
    g_assert_no_error (error);
    g_assert_nonnull (mods);
    g_assert_cmpuint (mods->len, ==, 2);
}

/* ==========================================================================
 * Mod Tests
 * ========================================================================== */

static void
test_mod_new (LoaderFixture *fixture,
              gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "test-mod", NULL);
    create_mod_manifest (mod_dir, "test-mod", NULL);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, NULL);
    g_assert_nonnull (mod);
    g_assert_true (LRG_IS_MOD (mod));
}

static void
test_mod_properties (LoaderFixture *fixture,
                     gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;
    LrgModManifest *manifest;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "prop-mod", NULL);
    create_mod_manifest (mod_dir, "prop-mod", NULL);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, NULL);
    g_assert_nonnull (mod);

    g_assert_cmpstr (lrg_mod_get_id (mod), ==, "prop-mod");
    g_assert_cmpstr (lrg_mod_get_base_path (mod), ==, mod_dir);

    manifest = lrg_mod_get_manifest (mod);
    g_assert_nonnull (manifest);
    g_assert_true (LRG_IS_MOD_MANIFEST (manifest));
}

static void
test_mod_state (LoaderFixture *fixture,
                gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autofree gchar *data_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "state-mod", NULL);
    create_mod_manifest (mod_dir, "state-mod", "data_path: data\n");

    /* Create the data directory that the mod expects */
    data_dir = g_build_filename (mod_dir, "data", NULL);
    g_mkdir_with_parents (data_dir, 0755);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, NULL);
    g_assert_nonnull (mod);

    /* Initial state */
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_DISCOVERED);
    g_assert_false (lrg_mod_is_loaded (mod));
    g_assert_true (lrg_mod_is_enabled (mod));

    /* Load mod */
    g_assert_true (lrg_mod_load (mod, &error));
    g_assert_no_error (error);
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_LOADED);
    g_assert_true (lrg_mod_is_loaded (mod));

    /* Unload mod */
    lrg_mod_unload (mod);
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_UNLOADED);
    g_assert_false (lrg_mod_is_loaded (mod));
}

static void
test_mod_enable_disable (LoaderFixture *fixture,
                         gconstpointer  user_data)
{
    g_autofree gchar *mod_dir = NULL;
    g_autoptr(LrgMod) mod = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "enable-mod", NULL);
    create_mod_manifest (mod_dir, "enable-mod", NULL);

    mod = lrg_mod_loader_load_mod (fixture->loader, mod_dir, NULL);
    g_assert_nonnull (mod);

    g_assert_true (lrg_mod_is_enabled (mod));

    lrg_mod_set_enabled (mod, FALSE);
    g_assert_false (lrg_mod_is_enabled (mod));

    lrg_mod_set_enabled (mod, TRUE);
    g_assert_true (lrg_mod_is_enabled (mod));
}

/* ==========================================================================
 * Manager Tests
 * ========================================================================== */

static void
test_manager_new (ManagerFixture *fixture,
                  gconstpointer   user_data)
{
    (void)user_data;
    g_assert_true (LRG_IS_MOD_MANAGER (fixture->manager));
}

static void
test_manager_get_default (void)
{
    LrgModManager *manager1;
    LrgModManager *manager2;

    manager1 = lrg_mod_manager_get_default ();
    g_assert_nonnull (manager1);

    manager2 = lrg_mod_manager_get_default ();
    g_assert_true (manager1 == manager2);
}

static void
test_manager_loader (ManagerFixture *fixture,
                     gconstpointer   user_data)
{
    LrgModLoader *loader;

    (void)user_data;

    loader = lrg_mod_manager_get_loader (fixture->manager);
    g_assert_nonnull (loader);
    g_assert_true (LRG_IS_MOD_LOADER (loader));
}

static void
test_manager_discover (ManagerFixture *fixture,
                       gconstpointer   user_data)
{
    g_autofree gchar *mod_dir = NULL;
    guint count;
    GPtrArray *mods;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "disc-mod", NULL);
    create_mod_manifest (mod_dir, "disc-mod", NULL);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);

    count = lrg_mod_manager_discover (fixture->manager, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (count, ==, 1);

    mods = lrg_mod_manager_get_mods (fixture->manager);
    g_assert_cmpuint (mods->len, ==, 1);
}

static void
test_manager_get_mod (ManagerFixture *fixture,
                      gconstpointer   user_data)
{
    g_autofree gchar *mod_dir = NULL;
    LrgMod *mod;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "get-mod", NULL);
    create_mod_manifest (mod_dir, "get-mod", NULL);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    mod = lrg_mod_manager_get_mod (fixture->manager, "get-mod");
    g_assert_nonnull (mod);
    g_assert_cmpstr (lrg_mod_get_id (mod), ==, "get-mod");

    g_assert_true (lrg_mod_manager_has_mod (fixture->manager, "get-mod"));
    g_assert_false (lrg_mod_manager_has_mod (fixture->manager, "nonexistent"));
}

static void
test_manager_load_all (ManagerFixture *fixture,
                       gconstpointer   user_data)
{
    g_autofree gchar *mod_dir1 = NULL;
    g_autofree gchar *mod_dir2 = NULL;
    GPtrArray *loaded;
    g_autoptr(GError) error = NULL;

    (void)user_data;

    mod_dir1 = g_build_filename (fixture->test_dir, "load-a", NULL);
    mod_dir2 = g_build_filename (fixture->test_dir, "load-b", NULL);
    create_mod_manifest (mod_dir1, "load-a", NULL);
    create_mod_manifest (mod_dir2, "load-b", NULL);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    g_assert_true (lrg_mod_manager_load_all (fixture->manager, &error));
    g_assert_no_error (error);

    loaded = lrg_mod_manager_get_loaded_mods (fixture->manager);
    g_assert_cmpuint (loaded->len, ==, 2);

    g_assert_true (lrg_mod_manager_is_mod_loaded (fixture->manager, "load-a"));
    g_assert_true (lrg_mod_manager_is_mod_loaded (fixture->manager, "load-b"));
}

static void
test_manager_unload_all (ManagerFixture *fixture,
                         gconstpointer   user_data)
{
    g_autofree gchar *mod_dir = NULL;
    GPtrArray *loaded;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "unload-mod", NULL);
    create_mod_manifest (mod_dir, "unload-mod", NULL);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);
    lrg_mod_manager_load_all (fixture->manager, NULL);

    loaded = lrg_mod_manager_get_loaded_mods (fixture->manager);
    g_assert_cmpuint (loaded->len, ==, 1);

    lrg_mod_manager_unload_all (fixture->manager);

    loaded = lrg_mod_manager_get_loaded_mods (fixture->manager);
    g_assert_cmpuint (loaded->len, ==, 0);
    g_assert_false (lrg_mod_manager_is_mod_loaded (fixture->manager, "unload-mod"));
}

static void
test_manager_enable_disable (ManagerFixture *fixture,
                             gconstpointer   user_data)
{
    g_autofree gchar *mod_dir = NULL;

    (void)user_data;

    mod_dir = g_build_filename (fixture->test_dir, "toggle-mod", NULL);
    create_mod_manifest (mod_dir, "toggle-mod", NULL);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    g_assert_true (lrg_mod_manager_disable_mod (fixture->manager, "toggle-mod"));
    g_assert_true (lrg_mod_manager_enable_mod (fixture->manager, "toggle-mod"));
    g_assert_false (lrg_mod_manager_disable_mod (fixture->manager, "nonexistent"));
}

static void
test_manager_load_order (ManagerFixture *fixture,
                         gconstpointer   user_data)
{
    g_autofree gchar *mod_dir1 = NULL;
    g_autofree gchar *mod_dir2 = NULL;
    g_autoptr(GPtrArray) order = NULL;

    (void)user_data;

    /* Create mod B which should load after A */
    mod_dir1 = g_build_filename (fixture->test_dir, "order-a", NULL);
    mod_dir2 = g_build_filename (fixture->test_dir, "order-b", NULL);

    create_mod_manifest (mod_dir1, "order-a", "priority: low\n");
    create_mod_manifest (mod_dir2, "order-b", "priority: high\nload_after:\n  - order-a\n");

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    order = lrg_mod_manager_get_load_order (fixture->manager);
    g_assert_cmpuint (order->len, ==, 2);

    /* order-a should come before order-b due to load_after */
    g_assert_cmpstr (g_ptr_array_index (order, 0), ==, "order-a");
    g_assert_cmpstr (g_ptr_array_index (order, 1), ==, "order-b");
}

/* ==========================================================================
 * Console Command Tests
 * ========================================================================== */

static gchar *
test_command_callback (LrgDebugConsole  *console,
                       guint             argc,
                       const gchar     **argv,
                       gpointer          user_data)
{
    (void)console;
    (void)argc;
    (void)argv;

    if (user_data != NULL)
        return g_strdup ((const gchar *)user_data);

    return g_strdup ("test output");
}

static void
test_console_command_new (void)
{
    LrgConsoleCommand *cmd;

    cmd = lrg_console_command_new ("test", "Test command", test_command_callback,
                                   NULL, NULL);
    g_assert_nonnull (cmd);
    g_assert_cmpstr (lrg_console_command_get_name (cmd), ==, "test");
    g_assert_cmpstr (lrg_console_command_get_description (cmd), ==, "Test command");
    g_assert_true (lrg_console_command_get_callback (cmd) == test_command_callback);
    g_assert_null (lrg_console_command_get_user_data (cmd));

    lrg_console_command_free (cmd);
}

static void
test_console_command_with_user_data (void)
{
    LrgConsoleCommand *cmd;
    gchar *user_data;

    user_data = g_strdup ("custom data");
    cmd = lrg_console_command_new ("test2", NULL, test_command_callback,
                                   user_data, g_free);
    g_assert_nonnull (cmd);
    g_assert_null (lrg_console_command_get_description (cmd));
    g_assert_cmpstr (lrg_console_command_get_user_data (cmd), ==, "custom data");

    /* user_data should be freed when command is freed */
    lrg_console_command_free (cmd);
}

static void
test_console_command_copy (void)
{
    LrgConsoleCommand *cmd;
    LrgConsoleCommand *copy;

    cmd = lrg_console_command_new ("original", "Original command",
                                   test_command_callback, NULL, NULL);
    copy = lrg_console_command_copy (cmd);
    g_assert_nonnull (copy);

    g_assert_cmpstr (lrg_console_command_get_name (copy), ==, "original");
    g_assert_cmpstr (lrg_console_command_get_description (copy), ==, "Original command");
    g_assert_true (lrg_console_command_get_callback (copy) == test_command_callback);

    lrg_console_command_free (cmd);
    lrg_console_command_free (copy);
}

static void
test_console_command_free_null (void)
{
    /* Should not crash */
    lrg_console_command_free (NULL);
}

/* ==========================================================================
 * Interface Type Tests
 * ========================================================================== */

static void
test_interface_types_exist (void)
{
    /* Verify all interface types are registered */
    g_assert_true (g_type_is_a (LRG_TYPE_MODABLE, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_ENTITY_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_ITEM_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_SCENE_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_DIALOG_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_QUEST_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_AI_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_COMMAND_PROVIDER, G_TYPE_INTERFACE));
    g_assert_true (g_type_is_a (LRG_TYPE_LOCALE_PROVIDER, G_TYPE_INTERFACE));
}

static void
test_console_command_gtype (void)
{
    GType type;

    type = lrg_console_command_get_type ();
    g_assert_true (G_TYPE_IS_BOXED (type));
    g_assert_cmpstr (g_type_name (type), ==, "LrgConsoleCommand");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

/* ==========================================================================
 * Load Order, Failure Handling, Script Mods and Extras
 * ========================================================================== */

/* Writes a manifest with free-form YAML (no defaults added) */
static void
write_raw_manifest (const gchar *mod_dir,
                    const gchar *yaml)
{
    g_autofree gchar *path = NULL;

    g_mkdir_with_parents (mod_dir, 0755);
    path = g_build_filename (mod_dir, "mod.yaml", NULL);
    g_assert_true (g_file_set_contents (path, yaml, -1, NULL));
}

/* Writes a file inside a mod folder */
static void
write_mod_file (const gchar *mod_dir,
                const gchar *name,
                const gchar *contents)
{
    g_autofree gchar *path = NULL;

    g_mkdir_with_parents (mod_dir, 0755);
    path = g_build_filename (mod_dir, name, NULL);
    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

/* Joins the computed load order into "a,b,c" */
static gchar *
load_order_string (LrgModManager *manager)
{
    g_autoptr(GPtrArray) order = lrg_mod_manager_get_load_order (manager);
    GString *out = g_string_new (NULL);
    guint i;

    for (i = 0; i < order->len; i++)
    {
        if (i > 0)
            g_string_append_c (out, ',');
        g_string_append (out, g_ptr_array_index (order, i));
    }
    return g_string_free (out, FALSE);
}

static void
on_loaded (LrgModManager *manager, LrgMod *mod, gpointer user_data)
{
    GString *log = user_data;

    (void)manager;
    g_string_append_printf (log, "%sloaded:%s", log->len > 0 ? " " : "", lrg_mod_get_id (mod));
}

static void
on_failed (LrgModManager *manager, LrgMod *mod, gpointer user_data)
{
    GString *log = user_data;

    (void)manager;
    g_string_append_printf (log, "%sfailed:%s", log->len > 0 ? " " : "", lrg_mod_get_id (mod));
}

static void
on_unloaded (LrgModManager *manager, LrgMod *mod, gpointer user_data)
{
    GString *log = user_data;

    (void)manager;
    g_string_append_printf (log, "%sunloaded:%s", log->len > 0 ? " " : "", lrg_mod_get_id (mod));
}

static void
test_manager_order_dependencies_beat_priority (ManagerFixture *fixture,
                                               gconstpointer   user_data)
{
    g_autofree gchar *dir_a = g_build_filename (fixture->test_dir, "a", NULL);
    g_autofree gchar *dir_b = g_build_filename (fixture->test_dir, "b", NULL);
    g_autofree gchar *dir_c = g_build_filename (fixture->test_dir, "c", NULL);
    g_autofree gchar *dir_d = g_build_filename (fixture->test_dir, "d", NULL);
    g_autofree gchar *order = NULL;

    (void)user_data;

    /*
     * "base" has the highest priority value (loads late on its own), but
     * "addon" depends on it with the lowest value: the dependency must win.
     * "early" asks to load before "base".  "plain" only has its priority.
     */
    write_raw_manifest (dir_a, "id: addon\ntype: data\npriority: lowest\ndependencies:\n  - base\n");
    write_raw_manifest (dir_b, "id: base\ntype: data\npriority: highest\n");
    write_raw_manifest (dir_c, "id: early\ntype: data\npriority: highest\nload_before:\n  - base\n");
    write_raw_manifest (dir_d, "id: plain\ntype: data\npriority: normal\n");

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    g_assert_cmpuint (lrg_mod_manager_discover (fixture->manager, NULL), ==, 4);

    order = load_order_string (fixture->manager);
    g_assert_cmpstr (order, ==, "plain,early,base,addon");

    g_assert_true (lrg_mod_manager_load_all (fixture->manager, NULL));
    g_assert_cmpuint (lrg_mod_manager_get_loaded_mods (fixture->manager)->len, ==, 4);
}

static void
test_manager_cycles_and_failed_dependencies (ManagerFixture *fixture,
                                             gconstpointer   user_data)
{
    g_autofree gchar *dir_x = g_build_filename (fixture->test_dir, "x", NULL);
    g_autofree gchar *dir_y = g_build_filename (fixture->test_dir, "y", NULL);
    g_autofree gchar *dir_s = g_build_filename (fixture->test_dir, "s", NULL);
    g_autofree gchar *dir_d = g_build_filename (fixture->test_dir, "d", NULL);
    g_autofree gchar *dir_o = g_build_filename (fixture->test_dir, "o", NULL);
    g_autoptr(GString) log = g_string_new (NULL);
    g_autoptr(GError) error = NULL;

    (void)user_data;

    /* x <-> y is a cycle; "script" has no loadable entry; "dependent" needs it */
    write_raw_manifest (dir_x, "id: x\ntype: data\ndependencies:\n  - y\n");
    write_raw_manifest (dir_y, "id: y\ntype: data\ndependencies:\n  - x\n");
    write_raw_manifest (dir_s, "id: script\ntype: script\nentry_point: missing.lua\n");
    write_raw_manifest (dir_d, "id: dependent\ntype: data\ndependencies:\n  - script\n  - id: absent\n    optional: true\n");
    write_raw_manifest (dir_o, "id: ok\ntype: data\n");

    g_signal_connect (fixture->manager, "mod-loaded", G_CALLBACK (on_loaded), log);
    g_signal_connect (fixture->manager, "mod-failed", G_CALLBACK (on_failed), log);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    /* Load failures are logged as warnings; they are expected here */
    g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_assert_false (lrg_mod_manager_load_all (fixture->manager, &error));
    g_assert_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED);

    /* Everything is reported; only "ok" loads */
    g_assert_nonnull (strstr (log->str, "failed:x"));
    g_assert_nonnull (strstr (log->str, "failed:y"));
    g_assert_nonnull (strstr (log->str, "failed:script"));
    g_assert_nonnull (strstr (log->str, "failed:dependent"));
    g_assert_nonnull (strstr (log->str, "loaded:ok"));
    g_assert_cmpuint (lrg_mod_manager_get_loaded_mods (fixture->manager)->len, ==, 1);

    g_assert_cmpstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "x")),
                     ==, "Circular dependency between mods");
    g_assert_cmpstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "dependent")),
                     ==, "Required dependency failed to load: script");
    g_assert_cmpint (lrg_mod_get_state (lrg_mod_manager_get_mod (fixture->manager, "dependent")),
                     ==, LRG_MOD_STATE_FAILED);

    /* The log dies with this test; the manager outlives it */
    g_signal_handlers_disconnect_by_data (fixture->manager, log);
}

/*
 * Only true cycle members fail.  "c" (discovered first) merely loads after a
 * member of the a <-> b cycle; it must keep loading, after the cycle.
 */
static void
test_manager_cycle_members_only (ManagerFixture *fixture,
                                 gconstpointer   user_data)
{
    g_autofree gchar *dir_c = g_build_filename (fixture->test_dir, "1-c", NULL);
    g_autofree gchar *dir_a = g_build_filename (fixture->test_dir, "2-a", NULL);
    g_autofree gchar *dir_b = g_build_filename (fixture->test_dir, "3-b", NULL);
    GPtrArray *order;

    (void)user_data;

    write_raw_manifest (dir_c, "id: c\ntype: data\nload_after: [a]\n");
    write_raw_manifest (dir_a, "id: a\ntype: data\nload_after: [b]\n");
    write_raw_manifest (dir_b, "id: b\ntype: data\nload_after: [a]\n");
    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);

    g_assert_false (lrg_mod_manager_load_all (fixture->manager, NULL));
    g_assert_cmpstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "a")),
                     ==, "Circular dependency between mods");
    g_assert_cmpstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "b")),
                     ==, "Circular dependency between mods");
    g_assert_true (lrg_mod_is_loaded (lrg_mod_manager_get_mod (fixture->manager, "c")));

    order = lrg_mod_manager_get_load_order (fixture->manager);
    g_assert_cmpuint (order->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (order, 0), ==, "a");
    g_assert_cmpstr (g_ptr_array_index (order, 1), ==, "b");
    g_assert_cmpstr (g_ptr_array_index (order, 2), ==, "c");
}

/* A reloaded dependency returns to its place: dependents still unload first. */
static void
test_manager_reload_keeps_order (ManagerFixture *fixture,
                                 gconstpointer   user_data)
{
    g_autofree gchar *dir_base = g_build_filename (fixture->test_dir, "base", NULL);
    g_autofree gchar *dir_top = g_build_filename (fixture->test_dir, "top", NULL);
    g_autoptr(GError) error = NULL;
    GPtrArray *loaded;

    (void)user_data;

    write_raw_manifest (dir_base, "id: base\ntype: data\n");
    write_raw_manifest (dir_top, "id: top\ntype: data\ndependencies:\n  - base\n");
    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_true (lrg_mod_manager_load_all (fixture->manager, NULL));

    g_assert_true (lrg_mod_manager_reload_mod (fixture->manager, "base", &error));
    g_assert_no_error (error);
    loaded = lrg_mod_manager_get_loaded_mods (fixture->manager);
    g_assert_cmpuint (loaded->len, ==, 2);
    g_assert_cmpstr (lrg_mod_get_id (g_ptr_array_index (loaded, 0)), ==, "base");
    g_assert_cmpstr (lrg_mod_get_id (g_ptr_array_index (loaded, 1)), ==, "top");

    /* Rediscovery unloads what was loaded instead of dropping it */
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_cmpuint (lrg_mod_manager_get_loaded_mods (fixture->manager)->len, ==, 0);
}

static void
test_manager_disabled_survives_discover (ManagerFixture *fixture,
                                         gconstpointer   user_data)
{
    g_autofree gchar *dir_a = g_build_filename (fixture->test_dir, "a", NULL);
    g_autofree gchar *dir_b = g_build_filename (fixture->test_dir, "b", NULL);

    (void)user_data;

    create_mod_manifest (dir_a, "alpha", NULL);
    create_mod_manifest (dir_b, "beta", "dependencies:\n  - alpha\n");
    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);

    /* Disabling before discovery is remembered */
    g_assert_false (lrg_mod_manager_disable_mod (fixture->manager, "alpha"));
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_false (lrg_mod_is_enabled (lrg_mod_manager_get_mod (fixture->manager, "alpha")));

    /* A disabled requirement fails its dependents with a clear reason */
    g_assert_false (lrg_mod_manager_load_all (fixture->manager, NULL));
    g_assert_cmpstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "beta")),
                     ==, "Required dependency is disabled: alpha");

    /* Rediscovery keeps the choice; enabling clears it */
    lrg_mod_manager_unload_all (fixture->manager);
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_false (lrg_mod_is_enabled (lrg_mod_manager_get_mod (fixture->manager, "alpha")));
    g_assert_true (lrg_mod_manager_enable_mod (fixture->manager, "alpha"));
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_true (lrg_mod_is_enabled (lrg_mod_manager_get_mod (fixture->manager, "alpha")));
    g_assert_true (lrg_mod_manager_load_all (fixture->manager, NULL));
}

static void
test_loader_invalid_and_implicit (ManagerFixture *fixture,
                                  gconstpointer   user_data)
{
    g_autofree gchar *bad = g_build_filename (fixture->test_dir, "broken", NULL);
    g_autofree gchar *escape = g_build_filename (fixture->test_dir, "escape", NULL);
    g_autofree gchar *lone = g_build_filename (fixture->test_dir, "lone", NULL);
    g_autofree gchar *two = g_build_filename (fixture->test_dir, "two", NULL);
    g_autofree gchar *hidden = g_build_filename (fixture->test_dir, ".hidden", NULL);
    g_autofree gchar *notes = g_build_filename (fixture->test_dir, "notes", NULL);
    LrgMod *mod;

    (void)user_data;

    write_raw_manifest (bad, "name: no id here\n");
    write_raw_manifest (escape, "id: escape\ntype: script\nentry_point: ../../etc/passwd.lua\n");
    write_mod_file (lone, "main.lua", "x = 1\n");
    write_mod_file (lone, "README.txt", "not a script\n");
    write_mod_file (two, "a.lua", "x = 1\n");
    write_mod_file (two, "b.py", "x = 1\n");
    write_mod_file (hidden, "main.lua", "x = 1\n");
    write_mod_file (notes, "notes.txt", "nothing\n");

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);

    /* Without implicit entries only manifest folders count */
    lrg_mod_manager_discover (fixture->manager, NULL);
    g_assert_false (lrg_mod_manager_has_mod (fixture->manager, "lone"));

    lrg_mod_loader_set_implicit_entries (lrg_mod_manager_get_loader (fixture->manager), TRUE);
    g_assert_true (lrg_mod_loader_get_implicit_entries (lrg_mod_manager_get_loader (fixture->manager)));
    lrg_mod_manager_discover (fixture->manager, NULL);

    /* Broken manifests become failed placeholders named after the folder */
    mod = lrg_mod_manager_get_mod (fixture->manager, "broken");
    g_assert_nonnull (mod);
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_FAILED);
    g_assert_nonnull (strstr (lrg_mod_get_error (mod), "'id'"));
    mod = lrg_mod_manager_get_mod (fixture->manager, "escape");
    g_assert_nonnull (mod);
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_FAILED);
    g_assert_nonnull (strstr (lrg_mod_get_error (mod), "inside the mod"));

    /* A lone script file is a script mod; ambiguous and hidden folders are not */
    mod = lrg_mod_manager_get_mod (fixture->manager, "lone");
    g_assert_nonnull (mod);
    g_assert_cmpint (lrg_mod_manifest_get_mod_type (lrg_mod_get_manifest (mod)), ==, LRG_MOD_TYPE_SCRIPT);
    g_assert_cmpstr (lrg_mod_manifest_get_entry_point (lrg_mod_get_manifest (mod)), ==, "main.lua");
    g_assert_false (lrg_mod_manager_has_mod (fixture->manager, "two"));
    g_assert_false (lrg_mod_manager_has_mod (fixture->manager, ".hidden"));
    g_assert_false (lrg_mod_manager_has_mod (fixture->manager, "notes"));
}

static void
test_manifest_extra (ManagerFixture *fixture,
                     gconstpointer   user_data)
{
    g_autofree gchar *dir = g_build_filename (fixture->test_dir, "extra", NULL);
    g_autofree gchar *path = g_build_filename (dir, "mod.yaml", NULL);
    g_autoptr(LrgModManifest) manifest = NULL;
    g_autoptr(GVariant) settings = NULL;
    g_autoptr(GVariant) tags = NULL;
    g_autoptr(GVariant) greeting = NULL;
    g_autoptr(GError) error = NULL;
    GVariant *extra;

    (void)user_data;

    write_raw_manifest (dir,
        "id: extra\n"
        "api_version: \"2.1\"\n"
        "settings:\n"
        "  greeting: hi\n"
        "  count: 3\n"
        "tags: [pvp, fun]\n"
        "empty:\n");
    manifest = lrg_mod_manifest_new_from_file (path, &error);
    g_assert_no_error (error);

    extra = lrg_mod_manifest_get_extra (manifest);
    g_assert_nonnull (extra);
    g_assert_true (g_variant_is_of_type (extra, G_VARIANT_TYPE_VARDICT));
    g_assert_cmpstr (lrg_mod_manifest_lookup_extra_string (manifest, "api_version"), ==, "2.1");
    g_assert_cmpstr (lrg_mod_manifest_lookup_extra_string (manifest, "id"), ==, "extra");
    g_assert_null (lrg_mod_manifest_lookup_extra_string (manifest, "settings"));
    g_assert_null (lrg_mod_manifest_lookup_extra_string (manifest, "nope"));

    settings = g_variant_lookup_value (extra, "settings", G_VARIANT_TYPE_VARDICT);
    g_assert_nonnull (settings);
    greeting = g_variant_lookup_value (settings, "greeting", G_VARIANT_TYPE_STRING);
    g_assert_cmpstr (g_variant_get_string (greeting, NULL), ==, "hi");
    tags = g_variant_lookup_value (extra, "tags", G_VARIANT_TYPE ("av"));
    g_assert_cmpuint (g_variant_n_children (tags), ==, 2);

    /* Manifests built in code have no extras */
    {
        g_autoptr(LrgModManifest) coded = lrg_mod_manifest_new ("code");

        g_assert_null (lrg_mod_manifest_get_extra (coded));
        g_assert_null (lrg_mod_manifest_lookup_extra_string (coded, "id"));
    }
}

#ifdef LRG_HAS_LUAJIT

/* Host function published during prepare-scripting */
static gboolean
host_note (LrgScripting  *scripting,
           guint          n_args,
           const GValue  *args,
           GValue        *return_value,
           gpointer       user_data,
           GError       **error)
{
    GString *log = user_data;

    (void)scripting;
    (void)return_value;
    (void)error;

    if (n_args == 1 && G_VALUE_HOLDS_STRING (&args[0]))
        g_string_append_printf (log, "%s%s", log->len > 0 ? " " : "", g_value_get_string (&args[0]));
    return TRUE;
}

static void
on_prepare (LrgModManager *manager,
            LrgMod        *mod,
            LrgScripting  *scripting,
            gpointer       user_data)
{
    (void)manager;
    (void)mod;
    g_assert_true (lrg_scripting_register_function (scripting, "note", host_note,
                                                    user_data, NULL));
}

static void
test_manager_script_mod_lua (ManagerFixture *fixture,
                             gconstpointer   user_data)
{
    g_autofree gchar *dir = g_build_filename (fixture->test_dir, "luamod", NULL);
    g_autofree gchar *broken = g_build_filename (fixture->test_dir, "broken", NULL);
    g_autoptr(GString) notes = g_string_new (NULL);
    g_autoptr(GString) signals_log = g_string_new (NULL);
    g_autoptr(GError) error = NULL;
    LrgMod *mod;
    GValue value = G_VALUE_INIT;

    (void)user_data;

    write_raw_manifest (dir, "id: luamod\ntype: script\nentry_point: main.lua\n");
    write_mod_file (dir, "helper.lua", "return { greet = function () return 'hello' end }\n");
    write_mod_file (dir, "main.lua",
        "local helper = require('helper')\n"
        "note('body')\n"
        "answer = 42\n"
        "function lrg_mod_init() note('init ' .. helper.greet()) end\n"
        "function lrg_mod_shutdown() note('shutdown') end\n");
    write_raw_manifest (broken, "id: broken\ntype: script\nentry_point: main.lua\n");
    write_mod_file (broken, "main.lua", "note('before')\nerror('boom')\n");

    g_signal_connect (fixture->manager, "mod-prepare-scripting", G_CALLBACK (on_prepare), notes);
    g_signal_connect (fixture->manager, "mod-loaded", G_CALLBACK (on_loaded), signals_log);
    g_signal_connect (fixture->manager, "mod-failed", G_CALLBACK (on_failed), signals_log);
    g_signal_connect (fixture->manager, "mod-unloaded", G_CALLBACK (on_unloaded), signals_log);

    lrg_mod_manager_add_search_path (fixture->manager, fixture->test_dir);
    lrg_mod_manager_discover (fixture->manager, NULL);
    /* The broken mod's load failure is logged as a warning */
    g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_assert_false (lrg_mod_manager_load_all (fixture->manager, &error));
    g_clear_error (&error);

    /* Host functions exist while the body runs; require() searches the mod */
    g_assert_cmpstr (notes->str, ==, "before body init hello");
    g_assert_cmpstr (signals_log->str, ==, "failed:broken loaded:luamod");
    g_assert_nonnull (strstr (lrg_mod_get_error (lrg_mod_manager_get_mod (fixture->manager, "broken")), "boom"));
    g_assert_null (lrg_mod_get_scripting (lrg_mod_manager_get_mod (fixture->manager, "broken")));

    mod = lrg_mod_manager_get_mod (fixture->manager, "luamod");
    g_assert_nonnull (lrg_mod_get_scripting (mod));
    g_assert_true (lrg_scripting_get_global (lrg_mod_get_scripting (mod), "answer", &value, &error));
    g_assert_cmpint (g_value_get_int64 (&value), ==, 42);
    g_value_unset (&value);

    /* reload_mod runs shutdown, then the body and init again */
    g_string_truncate (notes, 0);
    g_string_truncate (signals_log, 0);
    g_assert_true (lrg_mod_manager_reload_mod (fixture->manager, "luamod", &error));
    g_assert_no_error (error);
    g_assert_cmpstr (notes->str, ==, "shutdown body init hello");
    g_assert_cmpstr (signals_log->str, ==, "unloaded:luamod loaded:luamod");

    /* Unloading runs the shutdown hook and drops the context */
    g_string_truncate (notes, 0);
    lrg_mod_manager_unload_mod (fixture->manager, "luamod");
    g_assert_cmpstr (notes->str, ==, "shutdown");
    g_assert_null (lrg_mod_get_scripting (mod));
    g_assert_false (lrg_mod_is_loaded (mod));

    /* Marking a mod failed records the reason */
    g_assert_true (lrg_mod_manager_reload_mod (fixture->manager, "luamod", NULL));
    lrg_mod_mark_failed (mod, "too many errors");
    g_assert_cmpint (lrg_mod_get_state (mod), ==, LRG_MOD_STATE_FAILED);
    g_assert_cmpstr (lrg_mod_get_error (mod), ==, "too many errors");

    g_assert_false (lrg_mod_manager_reload_mod (fixture->manager, "nope", &error));
    g_assert_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_NOT_FOUND);

    /* The logs die with this test; the manager outlives it */
    g_signal_handlers_disconnect_by_data (fixture->manager, notes);
    g_signal_handlers_disconnect_by_data (fixture->manager, signals_log);
}

#endif /* LRG_HAS_LUAJIT */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Mod Dependency */
    g_test_add_func ("/mod/dependency/new", test_mod_dependency_new);
    g_test_add_func ("/mod/dependency/optional", test_mod_dependency_optional);
    g_test_add_func ("/mod/dependency/copy", test_mod_dependency_copy);
    g_test_add_func ("/mod/dependency/copy-null", test_mod_dependency_copy_null);
    g_test_add_func ("/mod/dependency/free-null", test_mod_dependency_free_null);

    /* Manifest */
    g_test_add ("/mod/manifest/new", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_new,
                manifest_fixture_tear_down);
    g_test_add ("/mod/manifest/identity", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_identity,
                manifest_fixture_tear_down);
    g_test_add ("/mod/manifest/type-and-priority", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_type_and_priority,
                manifest_fixture_tear_down);
    g_test_add ("/mod/manifest/dependencies", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_dependencies,
                manifest_fixture_tear_down);
    g_test_add ("/mod/manifest/load-order", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_load_order,
                manifest_fixture_tear_down);
    g_test_add ("/mod/manifest/paths", ManifestFixture, NULL,
                manifest_fixture_set_up, test_manifest_paths,
                manifest_fixture_tear_down);

    /* Loader */
    g_test_add ("/mod/loader/new", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_new,
                loader_fixture_tear_down);
    g_test_add ("/mod/loader/search-paths", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_search_paths,
                loader_fixture_tear_down);
    g_test_add ("/mod/loader/manifest-filename", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_manifest_filename,
                loader_fixture_tear_down);
    g_test_add ("/mod/loader/load-mod", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_load_mod,
                loader_fixture_tear_down);
    g_test_add ("/mod/loader/load-mod-not-found", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_load_mod_not_found,
                loader_fixture_tear_down);
    g_test_add ("/mod/loader/discover", LoaderFixture, NULL,
                loader_fixture_set_up, test_loader_discover,
                loader_fixture_tear_down);

    /* Mod */
    g_test_add ("/mod/mod/new", LoaderFixture, NULL,
                loader_fixture_set_up, test_mod_new,
                loader_fixture_tear_down);
    g_test_add ("/mod/mod/properties", LoaderFixture, NULL,
                loader_fixture_set_up, test_mod_properties,
                loader_fixture_tear_down);
    g_test_add ("/mod/mod/state", LoaderFixture, NULL,
                loader_fixture_set_up, test_mod_state,
                loader_fixture_tear_down);
    g_test_add ("/mod/mod/enable-disable", LoaderFixture, NULL,
                loader_fixture_set_up, test_mod_enable_disable,
                loader_fixture_tear_down);

    /* Manager */
    g_test_add ("/mod/manager/new", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_new,
                manager_fixture_tear_down);
    g_test_add_func ("/mod/manager/get-default", test_manager_get_default);
    g_test_add ("/mod/manager/loader", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_loader,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/discover", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_discover,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/get-mod", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_get_mod,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/load-all", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_load_all,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/unload-all", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_unload_all,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/enable-disable", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_enable_disable,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/load-order", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_load_order,
                manager_fixture_tear_down);

    /* Console Command */
    g_test_add_func ("/mod/console-command/new", test_console_command_new);
    g_test_add_func ("/mod/console-command/with-user-data", test_console_command_with_user_data);
    g_test_add_func ("/mod/console-command/copy", test_console_command_copy);
    g_test_add_func ("/mod/console-command/free-null", test_console_command_free_null);
    g_test_add_func ("/mod/console-command/gtype", test_console_command_gtype);

    /* Interfaces */
    g_test_add_func ("/mod/interfaces/types-exist", test_interface_types_exist);

    /* Ordering, failures, script mods and extras */
    g_test_add ("/mod/manager/order-dependencies-beat-priority", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_order_dependencies_beat_priority,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/cycles-and-failed-dependencies", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_cycles_and_failed_dependencies,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/cycle-members-only", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_cycle_members_only,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/reload-keeps-order", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_reload_keeps_order,
                manager_fixture_tear_down);
    g_test_add ("/mod/manager/disabled-survives-discover", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_disabled_survives_discover,
                manager_fixture_tear_down);
    g_test_add ("/mod/loader/invalid-and-implicit", ManagerFixture, NULL,
                manager_fixture_set_up, test_loader_invalid_and_implicit,
                manager_fixture_tear_down);
    g_test_add ("/mod/manifest/extra", ManagerFixture, NULL,
                manager_fixture_set_up, test_manifest_extra, manager_fixture_tear_down);
#ifdef LRG_HAS_LUAJIT
    g_test_add ("/mod/manager/script-mod-lua", ManagerFixture, NULL,
                manager_fixture_set_up, test_manager_script_mod_lua, manager_fixture_tear_down);
#endif

    return g_test_run ();
}
