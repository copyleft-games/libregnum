/* test-asset-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgAssetManager.
 */

#include <glib.h>
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

    return g_test_run ();
}
