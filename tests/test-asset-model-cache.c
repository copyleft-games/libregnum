/* test-asset-model-cache.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the LrgAssetManager model and animation caches: canonical
 * cache keys, search-path priority, headless animation loading, error
 * paths and, when a display exists, real model loading through a hidden
 * GL window (skipped headlessly).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>  /* SetConfigFlags / FLAG_WINDOW_HIDDEN */

#include "lrg-test-glb.h"

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

#define SKIP_IF_NO_GRAPHICS() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("Graphics context not available"); \
            return; \
        } \
    } while (0)

/* init_graphics_context:
 * Opens a hidden 1x1 window when a display exists. */
static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (1, 1, "lrg-model-cache-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

/* ========================================================================== */
/*                                 Fixture                                    */
/* ========================================================================== */

typedef struct
{
    gchar           *root;      /* temp dir */
    gchar           *base;      /* root/base (search path 1) */
    gchar           *mod;       /* root/mod  (search path 2) */
    LrgAssetManager *manager;
} ModelFixture;

static const gchar *const clip_names[] = { "Idle", "Walk" };

static void
fixture_set_up (ModelFixture  *fixture,
                gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *models = NULL;
    g_autofree gchar *path = NULL;

    fixture->root = g_dir_make_tmp ("lrg-model-cache-XXXXXX", &error);
    g_assert_no_error (error);
    fixture->base = g_build_filename (fixture->root, "base", NULL);
    fixture->mod = g_build_filename (fixture->root, "mod", NULL);

    models = g_build_filename (fixture->base, "models", NULL);
    g_assert_cmpint (g_mkdir_with_parents (models, 0700), ==, 0);
    g_free (models);
    models = g_build_filename (fixture->mod, "models", NULL);
    g_assert_cmpint (g_mkdir_with_parents (models, 0700), ==, 0);

    path = g_build_filename (fixture->base, "models", "hero.glb", NULL);
    test_glb_write_skinned_triangle (path, clip_names, G_N_ELEMENTS (clip_names));
    g_free (path);
    path = g_build_filename (fixture->base, "models", "rock.glb", NULL);
    test_glb_write_static_triangle (path);
    g_free (path);
    path = g_build_filename (fixture->mod, "models", "rock.glb", NULL);
    test_glb_write_static_triangle (path);

    fixture->manager = lrg_asset_manager_new ();
    lrg_asset_manager_add_search_path (fixture->manager, fixture->base);
}

static void
remove_tree (const gchar *path)
{
    GDir        *dir;
    const gchar *entry;

    if (g_file_test (path, G_FILE_TEST_IS_DIR))
    {
        dir = g_dir_open (path, 0, NULL);
        while (dir != NULL && (entry = g_dir_read_name (dir)) != NULL)
        {
            g_autofree gchar *child = g_build_filename (path, entry, NULL);

            remove_tree (child);
        }
        if (dir != NULL)
            g_dir_close (dir);
    }
    g_remove (path);
}

static void
fixture_tear_down (ModelFixture  *fixture,
                   gconstpointer  user_data)
{
    g_clear_object (&fixture->manager);
    remove_tree (fixture->root);
    g_free (fixture->root);
    g_free (fixture->base);
    g_free (fixture->mod);
}

/* ========================================================================== */
/*                                 Tests                                      */
/* ========================================================================== */

static void
test_resolve_canonical (ModelFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autofree gchar *plain = NULL;
    g_autofree gchar *dotted = NULL;
    g_autofree gchar *doubled = NULL;
    g_autofree gchar *absolute = NULL;
    g_autofree gchar *expected = NULL;
    g_autofree gchar *weird = NULL;

    expected = g_build_filename (fixture->base, "models", "hero.glb", NULL);
    plain = lrg_asset_manager_resolve_path (fixture->manager, "models/hero.glb");
    dotted = lrg_asset_manager_resolve_path (fixture->manager, "models/../models/./hero.glb");
    doubled = lrg_asset_manager_resolve_path (fixture->manager, "models//hero.glb");
    weird = g_build_filename (fixture->base, "models", "..", "models", "hero.glb", NULL);
    absolute = lrg_asset_manager_resolve_path (fixture->manager, weird);

    g_assert_cmpstr (plain, ==, expected);
    g_assert_cmpstr (dotted, ==, expected);
    g_assert_cmpstr (doubled, ==, expected);
    g_assert_cmpstr (absolute, ==, expected);
    g_assert_true (g_path_is_absolute (plain));

    g_assert_null (lrg_asset_manager_resolve_path (fixture->manager, "models/missing.glb"));
}

static void
test_resolve_priority (ModelFixture  *fixture,
                       gconstpointer  user_data)
{
    g_autofree gchar *before = NULL;
    g_autofree gchar *after = NULL;
    g_autofree gchar *expected_base = NULL;
    g_autofree gchar *expected_mod = NULL;

    expected_base = g_build_filename (fixture->base, "models", "rock.glb", NULL);
    expected_mod = g_build_filename (fixture->mod, "models", "rock.glb", NULL);

    before = lrg_asset_manager_resolve_path (fixture->manager, "models/rock.glb");
    g_assert_cmpstr (before, ==, expected_base);

    /* The later search path overrides. */
    lrg_asset_manager_add_search_path (fixture->manager, fixture->mod);
    after = lrg_asset_manager_resolve_path (fixture->manager, "models/rock.glb");
    g_assert_cmpstr (after, ==, expected_mod);
}

static void
test_animations_headless (ModelFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *absolute = NULL;
    GPtrArray *clips;
    GPtrArray *again;
    GPtrArray *other;

    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 0);
    g_assert_false (lrg_asset_manager_is_cached (fixture->manager, "models/hero.glb"));

    clips = lrg_asset_manager_load_model_animations (fixture->manager, "models/hero.glb", &error);
    g_assert_no_error (error);
    g_assert_nonnull (clips);
    g_assert_cmpuint (clips->len, ==, 2);
    g_assert_cmpstr (grl_model_animation_get_name (g_ptr_array_index (clips, 0)), ==, "Idle");
    g_assert_cmpstr (grl_model_animation_get_name (g_ptr_array_index (clips, 1)), ==, "Walk");
    /* 0.5 s at 60 fps, both ends inclusive. */
    g_assert_cmpint (grl_model_animation_get_frame_count (g_ptr_array_index (clips, 0)), ==, 31);
    g_assert_cmpint (grl_model_animation_get_bone_count (g_ptr_array_index (clips, 1)), ==, 1);

    /* Different spellings share the cache entry. */
    again = lrg_asset_manager_load_model_animations (fixture->manager, "models/./hero.glb", &error);
    g_assert_no_error (error);
    g_assert_true (again == clips);
    absolute = g_build_filename (fixture->base, "models", "hero.glb", NULL);
    other = lrg_asset_manager_load_model_animations (fixture->manager, absolute, &error);
    g_assert_no_error (error);
    g_assert_true (other == clips);
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 1);

    g_assert_true (lrg_asset_manager_is_cached (fixture->manager, "models/hero.glb"));
    g_assert_true (lrg_asset_manager_is_cached (fixture->manager, absolute));

    /* Unloading by any spelling drops the entry. */
    g_assert_true (lrg_asset_manager_unload (fixture->manager, "models//hero.glb"));
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 0);
    g_assert_false (lrg_asset_manager_unload (fixture->manager, "models/hero.glb"));

    /* Reload, then unload_all. */
    clips = lrg_asset_manager_load_model_animations (fixture->manager, "models/hero.glb", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (clips->len, ==, 2);
    lrg_asset_manager_unload_all (fixture->manager);
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 0);
}

static void
test_animations_none (ModelFixture  *fixture,
                      gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    GPtrArray *clips;

    /* No skin: an empty, cached set rather than an error. */
    clips = lrg_asset_manager_load_model_animations (fixture->manager, "models/rock.glb", &error);
    g_assert_no_error (error);
    g_assert_nonnull (clips);
    g_assert_cmpuint (clips->len, ==, 0);
    g_assert_true (lrg_asset_manager_load_model_animations (fixture->manager, "models/rock.glb", NULL) == clips);
}

static void
test_animations_keyed_by_file (ModelFixture  *fixture,
                               gconstpointer  user_data)
{
    GPtrArray *base_clips;
    GPtrArray *mod_clips;
    g_autofree gchar *mod_hero = NULL;

    mod_hero = g_build_filename (fixture->mod, "models", "hero.glb", NULL);
    test_glb_write_skinned_triangle (mod_hero, clip_names, 1);

    base_clips = lrg_asset_manager_load_model_animations (fixture->manager, "models/hero.glb", NULL);
    g_assert_nonnull (base_clips);
    g_assert_cmpuint (base_clips->len, ==, 2);

    /* Same relative name, new search path: a different file and entry. */
    lrg_asset_manager_add_search_path (fixture->manager, fixture->mod);
    mod_clips = lrg_asset_manager_load_model_animations (fixture->manager, "models/hero.glb", NULL);
    g_assert_nonnull (mod_clips);
    g_assert_true (mod_clips != base_clips);
    g_assert_cmpuint (mod_clips->len, ==, 1);
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 2);
}

static void
test_errors (ModelFixture  *fixture,
             gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *missing = NULL;

    g_assert_null (lrg_asset_manager_load_model (fixture->manager, "models/missing.glb", &error));
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
    g_clear_error (&error);

    g_assert_null (lrg_asset_manager_load_model_animations (fixture->manager, "models/missing.glb", &error));
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
    g_clear_error (&error);

    missing = g_build_filename (fixture->root, "nope.glb", NULL);
    g_assert_null (lrg_asset_manager_load_model_animations (fixture->manager, missing, &error));
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_NOT_FOUND);
    g_clear_error (&error);

    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 0);
}

static void
test_model_headless (ModelFixture  *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    if (graphics_available)
    {
        g_test_skip ("A graphics context exists; covered by /model-cache/model/load");
        return;
    }

    g_assert_null (lrg_asset_manager_load_model (fixture->manager, "models/rock.glb", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
    g_clear_error (&error);

    g_assert_null (lrg_asset_manager_load_asset (fixture->manager, "models/rock.glb", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 0);
}

static void
test_model_load (ModelFixture  *fixture,
                 gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *absolute = NULL;
    GrlModel *model;
    GrlModel *again;
    GrlModel *rock;
    GObject  *dispatched;

    SKIP_IF_NO_GRAPHICS ();

    model = lrg_asset_manager_load_model (fixture->manager, "models/hero.glb", &error);
    g_assert_no_error (error);
    g_assert_nonnull (model);
    g_assert_true (grl_model_is_valid (model));
    g_assert_cmpint (grl_model_get_mesh_count (model), ==, 1);
    g_assert_cmpint (grl_model_get_bone_count (model), ==, 1);

    absolute = g_build_filename (fixture->base, "models", "..", "models", "hero.glb", NULL);
    again = lrg_asset_manager_load_model (fixture->manager, absolute, &error);
    g_assert_no_error (error);
    g_assert_true (again == model);

    dispatched = lrg_asset_manager_load_asset (fixture->manager, "models/hero.glb", &error);
    g_assert_no_error (error);
    g_assert_true (dispatched == (GObject *)model);
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 1);

    rock = lrg_asset_manager_load_model (fixture->manager, "models/rock.glb", &error);
    g_assert_no_error (error);
    g_assert_true (rock != model);
    g_assert_cmpint (grl_model_get_bone_count (rock), ==, 0);
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 2);

    /* Model and its animations share the key; unload drops both. */
    g_assert_nonnull (lrg_asset_manager_load_model_animations (fixture->manager, "models/hero.glb", NULL));
    g_assert_true (lrg_asset_manager_unload (fixture->manager, "models/hero.glb"));
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 1);
    g_assert_cmpuint (lrg_asset_manager_get_animation_cache_size (fixture->manager), ==, 0);

    /* Reloading after an unload yields a fresh, valid model. */
    model = lrg_asset_manager_load_model (fixture->manager, "models/hero.glb", &error);
    g_assert_no_error (error);
    g_assert_true (grl_model_is_valid (model));

    lrg_asset_manager_unload_all (fixture->manager);
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 0);
}

static void
test_model_load_failed (ModelFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *path = NULL;

    SKIP_IF_NO_GRAPHICS ();

    /* A file with the right extension but no glTF content. */
    path = g_build_filename (fixture->base, "models", "broken.glb", NULL);
    g_assert_true (g_file_set_contents (path, "not a model", -1, NULL));
    g_assert_null (lrg_asset_manager_load_model (fixture->manager, "models/broken.glb", &error));
    g_assert_error (error, LRG_ASSET_MANAGER_ERROR, LRG_ASSET_MANAGER_ERROR_LOAD_FAILED);
    g_assert_cmpuint (lrg_asset_manager_get_model_cache_size (fixture->manager), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

#define ADD(path, func) \
    g_test_add (path, ModelFixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/model-cache/resolve/canonical", test_resolve_canonical);
    ADD ("/model-cache/resolve/priority", test_resolve_priority);
    ADD ("/model-cache/animations/headless", test_animations_headless);
    ADD ("/model-cache/animations/none", test_animations_none);
    ADD ("/model-cache/animations/keyed-by-file", test_animations_keyed_by_file);
    ADD ("/model-cache/errors", test_errors);
    ADD ("/model-cache/model/headless", test_model_headless);
    ADD ("/model-cache/model/load", test_model_load);
    ADD ("/model-cache/model/load-failed", test_model_load_failed);

#undef ADD

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
