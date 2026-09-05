/* test-asset-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAssetManager.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgAssetManager *manager;
} AssetManagerFixture;

static void
asset_manager_fixture_set_up (AssetManagerFixture *fixture,
                              gconstpointer        user_data)
{
    fixture->manager = lrg_asset_manager_new ();
    g_assert_nonnull (fixture->manager);
}

static void
asset_manager_fixture_tear_down (AssetManagerFixture *fixture,
                                 gconstpointer        user_data)
{
    g_clear_object (&fixture->manager);
}

/* ==========================================================================
 * Test Cases - Construction
 * ========================================================================== */

static void
test_asset_manager_new (void)
{
    g_autoptr(LrgAssetManager) manager = NULL;

    manager = lrg_asset_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_ASSET_MANAGER (manager));
}

/* ==========================================================================
 * Test Cases - Search Paths
 * ========================================================================== */

static void
test_asset_manager_search_paths_add (AssetManagerFixture *fixture,
                                     gconstpointer        user_data)
{
    const GPtrArray *paths;

    lrg_asset_manager_add_search_path (fixture->manager, "/path/one");
    lrg_asset_manager_add_search_path (fixture->manager, "/path/two");

    paths = lrg_asset_manager_get_search_paths (fixture->manager);

    g_assert_nonnull (paths);
    g_assert_cmpuint (paths->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (paths, 0), ==, "/path/one");
    g_assert_cmpstr (g_ptr_array_index (paths, 1), ==, "/path/two");
}

static void
test_asset_manager_search_paths_remove (AssetManagerFixture *fixture,
                                        gconstpointer        user_data)
{
    const GPtrArray *paths;
    gboolean         result;

    lrg_asset_manager_add_search_path (fixture->manager, "/path/one");
    lrg_asset_manager_add_search_path (fixture->manager, "/path/two");
    lrg_asset_manager_add_search_path (fixture->manager, "/path/three");

    result = lrg_asset_manager_remove_search_path (fixture->manager, "/path/two");
    g_assert_true (result);

    paths = lrg_asset_manager_get_search_paths (fixture->manager);
    g_assert_cmpuint (paths->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (paths, 0), ==, "/path/one");
    g_assert_cmpstr (g_ptr_array_index (paths, 1), ==, "/path/three");

    /* Removing non-existent path returns FALSE */
    result = lrg_asset_manager_remove_search_path (fixture->manager, "/path/nonexistent");
    g_assert_false (result);
}

static void
test_asset_manager_search_paths_clear (AssetManagerFixture *fixture,
                                       gconstpointer        user_data)
{
    const GPtrArray *paths;

    lrg_asset_manager_add_search_path (fixture->manager, "/path/one");
    lrg_asset_manager_add_search_path (fixture->manager, "/path/two");

    lrg_asset_manager_clear_search_paths (fixture->manager);

    paths = lrg_asset_manager_get_search_paths (fixture->manager);
    g_assert_nonnull (paths);
    g_assert_cmpuint (paths->len, ==, 0);
}

static void
test_asset_manager_search_paths_empty (AssetManagerFixture *fixture,
                                       gconstpointer        user_data)
{
    const GPtrArray *paths;

    paths = lrg_asset_manager_get_search_paths (fixture->manager);
    g_assert_nonnull (paths);
    g_assert_cmpuint (paths->len, ==, 0);
}

/* ==========================================================================
 * Test Cases - Cache Management
 * ========================================================================== */

static void
test_asset_manager_cache_sizes_initial (AssetManagerFixture *fixture,
                                        gconstpointer        user_data)
{
    /* Initially all caches should be empty */
    g_assert_cmpuint (lrg_asset_manager_get_texture_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_font_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_sound_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_music_cache_size (fixture->manager), ==, 0);
}

static void
test_asset_manager_is_cached_empty (AssetManagerFixture *fixture,
                                    gconstpointer        user_data)
{
    /* Nothing should be cached initially */
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, "nonexistent.png"));
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, "anything.ttf"));
}

static void
test_asset_manager_unload_nonexistent (AssetManagerFixture *fixture,
                                       gconstpointer        user_data)
{
    gboolean result;

    /* Unloading something that doesn't exist should return FALSE */
    result = lrg_asset_manager_unload (fixture->manager, "nonexistent.png");
    g_assert_false (result);
}

static void
test_asset_manager_unload_all_empty (AssetManagerFixture *fixture,
                                     gconstpointer        user_data)
{
    /* Calling unload_all on empty caches should not crash */
    lrg_asset_manager_unload_all (fixture->manager);

    g_assert_cmpuint (lrg_asset_manager_get_texture_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_font_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_sound_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_music_cache_size (fixture->manager), ==, 0);
}

/* ==========================================================================
 * Test Cases - Load Errors (No Search Paths)
 * ========================================================================== */

static void
test_asset_manager_load_texture_not_found (AssetManagerFixture *fixture,
                                           gconstpointer        user_data)
{
    g_autoptr(GError) error = NULL;
    GrlTexture       *texture;

    /* With no search paths, loading should fail */
    texture = lrg_asset_manager_load_texture (fixture->manager, "sprites/test.png", &error);

    g_assert_null (texture);
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
}

static void
test_asset_manager_load_font_not_found (AssetManagerFixture *fixture,
                                        gconstpointer        user_data)
{
    g_autoptr(GError) error = NULL;
    GrlFont          *font;

    /* With no search paths, loading should fail */
    font = lrg_asset_manager_load_font (fixture->manager, "fonts/test.ttf", 24, &error);

    g_assert_null (font);
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
}

static void
test_asset_manager_load_sound_not_found (AssetManagerFixture *fixture,
                                         gconstpointer        user_data)
{
    g_autoptr(GError) error = NULL;
    GrlSound         *sound;

    /* With no search paths, loading should fail */
    sound = lrg_asset_manager_load_sound (fixture->manager, "sounds/test.wav", &error);

    g_assert_null (sound);
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
}

static void
test_asset_manager_load_music_not_found (AssetManagerFixture *fixture,
                                         gconstpointer        user_data)
{
    g_autoptr(GError) error = NULL;
    GrlMusic         *music;

    /* With no search paths, loading should fail */
    music = lrg_asset_manager_load_music (fixture->manager, "music/test.ogg", &error);

    g_assert_null (music);
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Test Cases - Engine Integration
 * ========================================================================== */

static void
test_asset_manager_engine_accessor (void)
{
    g_autoptr(GError)  error = NULL;
    LrgEngine         *engine;
    LrgAssetManager   *manager;

    engine = lrg_engine_get_default ();
    g_assert_nonnull (engine);

    /* Before startup, asset manager should be NULL */
    manager = lrg_engine_get_asset_manager (engine);
    g_assert_null (manager);

    /* After startup, asset manager should exist */
    g_assert_true (lrg_engine_startup (engine, &error));
    g_assert_no_error (error);

    manager = lrg_engine_get_asset_manager (engine);
    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_ASSET_MANAGER (manager));

    lrg_engine_shutdown (engine);

    /* After shutdown, asset manager should be NULL again */
    manager = lrg_engine_get_asset_manager (engine);
    g_assert_null (manager);

    g_object_unref (engine);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

typedef struct
{
    LrgAssetManager *manager;
    gchar *directory;
    gchar *path;
    guint reloaded;
    guint failed;
} DefinitionFixture;

static void
on_definition_reloaded (LrgAssetManager *manager,
                        const gchar     *name,
                        GObject         *previous,
                        GObject         *replacement,
                        DefinitionFixture *fixture)
{
    g_assert_true (previous != replacement);
    g_assert_true (lrg_asset_manager_load_object (manager, name, NULL) == replacement);
    g_assert_true (LRG_IS_ITEM_DEF (previous));
    fixture->reloaded++;
}

static void
on_definition_failed (LrgAssetManager  *manager,
                      const gchar      *name,
                      GError           *error,
                      DefinitionFixture *fixture)
{
    g_assert_nonnull (error);
    fixture->failed++;
}

static void
write_definition (DefinitionFixture *fixture,
                  const gchar       *contents)
{
    g_autoptr(GError) error = NULL;

    g_assert_true (g_file_set_contents (fixture->path, contents, -1, &error));
    g_assert_no_error (error);
}

static void
definition_set_up (DefinitionFixture *fixture,
                   gconstpointer      user_data)
{
    g_autoptr(LrgRegistry) registry = lrg_registry_new ();
    g_autoptr(LrgDataLoader) loader = lrg_data_loader_new ();
    g_autoptr(GError) error = NULL;

    fixture->directory = g_dir_make_tmp ("libregnum-definition-XXXXXX", &error);
    g_assert_no_error (error);
    fixture->path = g_build_filename (fixture->directory, "item.yaml", NULL);
    fixture->manager = lrg_asset_manager_new ();
    fixture->reloaded = 0;
    fixture->failed = 0;
    lrg_registry_register (registry, "item", LRG_TYPE_ITEM_DEF);
    lrg_registry_register (registry, "other", LRG_TYPE_QUEST_DEF);
    lrg_data_loader_set_registry (loader, registry);
    lrg_asset_manager_set_data_loader (fixture->manager, loader);
    lrg_asset_manager_add_search_path (fixture->manager, fixture->directory);
    g_signal_connect (fixture->manager, "object-reloaded",
                      G_CALLBACK (on_definition_reloaded), fixture);
    g_signal_connect (fixture->manager, "object-reload-failed",
                      G_CALLBACK (on_definition_failed), fixture);
    write_definition (fixture, "type: item\nid: sword\nname: Original\nvalue: 42\n");
}

static void
definition_tear_down (DefinitionFixture *fixture,
                      gconstpointer      user_data)
{
    g_clear_object (&fixture->manager);
    g_remove (fixture->path);
    g_rmdir (fixture->directory);
    g_free (fixture->path);
    g_free (fixture->directory);
}

static void
test_definition_cache (DefinitionFixture *fixture,
                       gconstpointer      user_data)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) retained = NULL;
    GObject *object;

    object = lrg_asset_manager_load_asset (fixture->manager, "item.yaml", &error);
    g_assert_no_error (error);
    g_assert_true (LRG_IS_ITEM_DEF (object));
    g_assert_cmpstr (lrg_item_def_get_id (LRG_ITEM_DEF (object)), ==, "sword");
    g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (object)), ==, 42);
    g_assert_true (lrg_asset_manager_is_cached (fixture->manager, "item.yaml"));
    g_assert_true (lrg_asset_manager_load_object (fixture->manager, "item.yaml", &error) == object);
    retained = g_object_ref (object);
    g_assert_true (lrg_asset_manager_unload (fixture->manager, "item.yaml"));
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, "item.yaml"));
    g_assert_cmpstr (lrg_item_def_get_name (LRG_ITEM_DEF (retained)), ==, "Original");
    object = lrg_asset_manager_load_object (fixture->manager, fixture->path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (object);
    lrg_asset_manager_unload_all (fixture->manager);
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, fixture->path));
}

static void
test_definition_invalid_reload (DefinitionFixture *fixture,
                                gconstpointer      user_data)
{
    const gchar *invalid[] = {
        "type: item\nname: [unterminated\n",
        "type: unknown\n",
        "type: other\nid: quest\n",
        "type: item\nvalue: nonsense\n",
        "type: item\nvalue: 4294967296\n",
        "type: item\nmax-stack: -1\n",
        "type: item\nmax-stack: 0\n",
        "type: item\nstackable: perhaps\n",
        "type: item\nvalue: null\n",
        "type: item\nmax-stack: null\n",
        "type: item\nstackable: null\n",
        "type: item\nitem-type: null\n",
        "type: item\nitem-type: invented-type\n",
        "type: item\nname: [a, b]\n",
        "type: item\nunknown-field: ignored\n",
        ""
    };
    g_autoptr(GObject) original = NULL;
    g_autoptr(GError) error = NULL;
    guint i;

    original = g_object_ref (lrg_asset_manager_load_object (fixture->manager, "item.yaml", &error));
    g_assert_no_error (error);
    for (i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
        write_definition (fixture, invalid[i]);
        g_assert_false (lrg_asset_manager_reload_object (fixture->manager, "item.yaml", &error));
        g_assert_nonnull (error);
        g_clear_error (&error);
        g_assert_true (lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL) == original);
    }
    g_assert_cmpuint (fixture->failed, ==, G_N_ELEMENTS (invalid));
    g_assert_cmpuint (fixture->reloaded, ==, 0);
    write_definition (fixture, "type: item\nid: sword\nname: Improved\nvalue: 123\n");
    g_assert_true (lrg_asset_manager_reload_object (fixture->manager, "item.yaml", &error));
    g_assert_no_error (error);
    g_assert_cmpuint (fixture->reloaded, ==, 1);
    g_assert_cmpstr (lrg_item_def_get_name (LRG_ITEM_DEF (original)), ==, "Original");
    g_assert_cmpstr (lrg_item_def_get_name (LRG_ITEM_DEF (
        lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL))), ==, "Improved");
}

static void
iterate_for (GMainContext *context,
             gint64        milliseconds)
{
    gint64 deadline = g_get_monotonic_time () + milliseconds * 1000;

    while (g_get_monotonic_time () < deadline)
    {
        while (g_main_context_iteration (context, FALSE))
            ;
        g_usleep (1000);
    }
}

static void
await_definition_signal (GMainContext *context,
                         guint        *count,
                         guint         expected)
{
    gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;

    while (*count < expected && g_get_monotonic_time () < deadline)
        iterate_for (context, 10);
    g_assert_cmpuint (*count, ==, expected);
}

static void
test_definition_watch (DefinitionFixture *fixture,
                       gconstpointer      user_data)
{
    g_autoptr(GMainContext) context = g_main_context_new ();
    g_autoptr(GError) error = NULL;
    g_autofree gchar *replacement_path = NULL;
    g_autoptr(GObject) original = NULL;

    g_main_context_push_thread_default (context);
    g_assert_true (lrg_asset_manager_watch_object (fixture->manager, "item.yaml", &error));
    g_assert_no_error (error);
    original = g_object_ref (lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL));
    /* A burst of writes must publish only the final version. */
    write_definition (fixture, "type: item\nvalue: 1\n");
    write_definition (fixture, "type: item\nvalue: 2\n");
    write_definition (fixture, "type: item\nvalue: 3\n");
    await_definition_signal (context, &fixture->reloaded, 1);
    iterate_for (context, 200);
    g_assert_cmpuint (fixture->reloaded, ==, 1);
    g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (
        lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL))), ==, 3);
    g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (original)), ==, 42);

    write_definition (fixture, "type: item\nvalue: invalid\n");
    await_definition_signal (context, &fixture->failed, 1);
    g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (
        lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL))), ==, 3);
    g_assert_cmpint (g_remove (fixture->path), ==, 0);
    await_definition_signal (context, &fixture->failed, 2);
    /* Recreate by atomic rename after deletion. */
    replacement_path = g_build_filename (fixture->directory, "item.tmp", NULL);
    g_assert_true (g_file_set_contents (replacement_path, "type: item\nvalue: 4\n", -1, &error));
    g_assert_cmpint (g_rename (replacement_path, fixture->path), ==, 0);
    await_definition_signal (context, &fixture->reloaded, 2);
    g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (
        lrg_asset_manager_load_object (fixture->manager, "item.yaml", NULL))), ==, 4);
    /* Replace an existing file atomically; the directory watch must survive. */
    g_assert_true (g_file_set_contents (replacement_path, "type: item\nvalue: 5\n", -1, &error));
    g_assert_cmpint (g_rename (replacement_path, fixture->path), ==, 0);
    await_definition_signal (context, &fixture->reloaded, 3);
    g_assert_no_error (error);
    g_assert_true (lrg_asset_manager_unwatch_object (fixture->manager, "item.yaml"));
    write_definition (fixture, "type: item\nvalue: 6\n");
    iterate_for (context, 250);
    g_assert_cmpuint (fixture->reloaded, ==, 3);
    g_main_context_pop_thread_default (context);
}

static void
test_definition_watch_teardown (DefinitionFixture *fixture,
                                gconstpointer      user_data)
{
    g_autoptr(GError) error = NULL;
    gpointer weak_manager;

    g_assert_true (lrg_asset_manager_watch_object (fixture->manager, "item.yaml", &error));
    g_assert_no_error (error);
    write_definition (fixture, "type: item\nvalue: 7\n");
    iterate_for (NULL, 20);
    weak_manager = fixture->manager;
    g_object_add_weak_pointer (G_OBJECT (fixture->manager), &weak_manager);
    g_clear_object (&fixture->manager);
    g_assert_null (weak_manager);
    iterate_for (NULL, 250);
    g_assert_cmpuint (fixture->reloaded, ==, 0);
}

static void
unload_on_definition_reload (LrgAssetManager *manager,
                             const gchar     *name,
                             GObject         *previous,
                             GObject         *replacement,
                             gpointer         user_data)
{
    g_assert_true (lrg_asset_manager_unload (manager, name));
    g_assert_true (LRG_IS_ITEM_DEF (previous));
    g_assert_true (LRG_IS_ITEM_DEF (replacement));
}

static void
test_definition_reload_unload (DefinitionFixture *fixture,
                               gconstpointer      user_data)
{
    g_autoptr(GError) error = NULL;

    g_assert_true (lrg_asset_manager_watch_object (fixture->manager, "item.yaml", &error));
    g_assert_no_error (error);
    g_signal_connect (fixture->manager, "object-reloaded",
                      G_CALLBACK (unload_on_definition_reload), NULL);
    write_definition (fixture, "type: item\nvalue: 88\n");
    await_definition_signal (NULL, &fixture->reloaded, 1);
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, "item.yaml"));
    write_definition (fixture, "type: item\nvalue: 89\n");
    iterate_for (NULL, 250);
    g_assert_cmpuint (fixture->reloaded, ==, 1);
    g_assert_cmpuint (fixture->failed, ==, 0);
}

static void
test_asset_dispatch_errors (void)
{
    const gchar *names[] = { "missing.PNG", "missing.TTF", "missing.wav", "missing.ogg" };
    g_autoptr(LrgAssetManager) manager = lrg_asset_manager_new ();
    g_autoptr(GError) error = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (names); i++)
    {
        g_assert_null (lrg_asset_manager_load_asset (manager, names[i], &error));
        g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
        g_clear_error (&error);
    }
    g_assert_null (lrg_asset_manager_load_asset (manager, "unsupported.txt", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error (&error);
    g_assert_null (lrg_asset_manager_load_asset (manager, "unconfigured.yaml", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Construction */
    g_test_add_func ("/asset-manager/new", test_asset_manager_new);

    /* Search paths */
    g_test_add ("/asset-manager/search-paths/add",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_search_paths_add,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/search-paths/remove",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_search_paths_remove,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/search-paths/clear",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_search_paths_clear,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/search-paths/empty",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_search_paths_empty,
                asset_manager_fixture_tear_down);

    /* Cache management */
    g_test_add ("/asset-manager/cache/sizes-initial",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_cache_sizes_initial,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/cache/is-cached-empty",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_is_cached_empty,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/cache/unload-nonexistent",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_unload_nonexistent,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/cache/unload-all-empty",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_unload_all_empty,
                asset_manager_fixture_tear_down);

    /* Load errors */
    g_test_add ("/asset-manager/load/texture-not-found",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_load_texture_not_found,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/load/font-not-found",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_load_font_not_found,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/load/sound-not-found",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_load_sound_not_found,
                asset_manager_fixture_tear_down);

    g_test_add ("/asset-manager/load/music-not-found",
                AssetManagerFixture, NULL,
                asset_manager_fixture_set_up,
                test_asset_manager_load_music_not_found,
                asset_manager_fixture_tear_down);

    /* Engine integration */
    g_test_add_func ("/asset-manager/engine-accessor", test_asset_manager_engine_accessor);

    g_test_add ("/asset-manager/definitions/cache", DefinitionFixture, NULL,
                definition_set_up, test_definition_cache, definition_tear_down);
    g_test_add ("/asset-manager/definitions/invalid-reload", DefinitionFixture, NULL,
                definition_set_up, test_definition_invalid_reload, definition_tear_down);
    g_test_add ("/asset-manager/definitions/watch", DefinitionFixture, NULL,
                definition_set_up, test_definition_watch, definition_tear_down);
    g_test_add ("/asset-manager/definitions/watch-teardown", DefinitionFixture, NULL,
                definition_set_up, test_definition_watch_teardown, definition_tear_down);
    g_test_add_func ("/asset-manager/load/dispatch-errors", test_asset_dispatch_errors);

    g_test_add ("/asset-manager/definitions/reload-unload", DefinitionFixture, NULL,
                definition_set_up, test_definition_reload_unload, definition_tear_down);

    return g_test_run ();
}
