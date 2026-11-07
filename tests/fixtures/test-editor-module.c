/* test-editor-module.c - a game module fixture with an editor hook
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built as a loadable .so (-DLRG_GAME_BUILD_MODULE) for test-editor-sources
 * and the lrgeditor smoke checks. Besides the usual game entry point
 * (LRG_DEFINE_GAME_MODULE) it exports the editor hook
 * (LRG_DEFINE_GAME_MODULE_EDITOR), which registers a tiny asset source:
 *
 *   fixture:
 *     Props/            folder
 *     Props/crate.mdl   model, exports crate.obj (a unit cube)
 *
 * The hook fails (returns FALSE with an error) when the app carries the
 * object data "lrg-test-editor-hook-fail" (or the environment variable
 * LRG_TEST_EDITOR_HOOK_FAIL is set, for lrgeditor runs), so the error
 * path of lrg_editor_app_init_module() can be tested too.
 */

#include <libregnum.h>
#include <glib.h>
#include <gio/gio.h>

/* ==========================================================================
 * The game type (the module's required entry point)
 * ========================================================================== */

#define TEST_TYPE_EDITOR_GAME (test_editor_game_get_type ())
G_DECLARE_FINAL_TYPE (TestEditorGame, test_editor_game, TEST, EDITOR_GAME, LrgGameTemplate)

struct _TestEditorGame
{
    LrgGameTemplate parent_instance;
};

G_DEFINE_FINAL_TYPE (TestEditorGame, test_editor_game, LRG_TYPE_GAME_TEMPLATE)

static void
test_editor_game_class_init (TestEditorGameClass *klass)
{
    (void) klass;
}

static void
test_editor_game_init (TestEditorGame *self)
{
    (void) self;
}

/* ==========================================================================
 * The asset source the editor hook registers
 * ========================================================================== */

#define TEST_TYPE_FIXTURE_SOURCE (test_fixture_source_get_type ())
G_DECLARE_FINAL_TYPE (TestFixtureSource, test_fixture_source, TEST, FIXTURE_SOURCE, GObject)

struct _TestFixtureSource
{
    GObject parent_instance;
};

static void test_fixture_source_iface_init (LrgAssetSourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (TestFixtureSource, test_fixture_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_ASSET_SOURCE, test_fixture_source_iface_init))

static const gchar *
fixture_get_id (LrgAssetSource *self)
{
    return "fixture";
}

static const gchar *
fixture_get_title (LrgAssetSource *self)
{
    return "Fixture Source";
}

/* Root: one folder; Props: one model. */
static GPtrArray *
fixture_list (LrgAssetSource  *self,
              const gchar     *folder,
              GError         **error)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func (g_object_unref);

    if (*folder == '\0')
        g_ptr_array_add (entries, lrg_asset_source_entry_new ("Props", TRUE, LRG_ASSET_TYPE_UNKNOWN, 0));
    else if (g_strcmp0 (folder, "Props") == 0)
        g_ptr_array_add (entries, lrg_asset_source_entry_new ("Props/crate.mdl", FALSE, LRG_ASSET_TYPE_MODEL, 64));
    else
    {
        g_ptr_array_unref (entries);
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No folder '%s'", folder);
        return NULL;
    }
    return entries;
}

/* Writes a unit cube as crate.obj. */
static gboolean
fixture_export (LrgAssetSource  *self,
                const gchar     *path,
                const gchar     *dest_dir,
                gchar          **out_file,
                GError         **error)
{
    static const gchar *cube =
        "o crate\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
        "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 5 1 4 8\n";
    gchar *file;

    if (g_strcmp0 (path, "Props/crate.mdl") != 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No entry '%s'", path);
        return FALSE;
    }
    file = g_build_filename (dest_dir, "crate.obj", NULL);
    if (!g_file_set_contents (file, cube, -1, error))
    {
        g_free (file);
        return FALSE;
    }
    *out_file = file;
    return TRUE;
}

static void
test_fixture_source_iface_init (LrgAssetSourceInterface *iface)
{
    iface->get_id = fixture_get_id;
    iface->get_title = fixture_get_title;
    iface->list = fixture_list;
    iface->export_entry = fixture_export;
}

static void
test_fixture_source_class_init (TestFixtureSourceClass *klass)
{
    (void) klass;
}

static void
test_fixture_source_init (TestFixtureSource *self)
{
    (void) self;
}

/* ==========================================================================
 * The editor hook
 * ========================================================================== */

static gboolean
fixture_editor_init (LrgEditorApp  *app,
                     GError       **error)
{
    g_autoptr(TestFixtureSource) source = NULL;

    if (g_object_get_data (G_OBJECT (app), "lrg-test-editor-hook-fail") != NULL ||
        g_getenv ("LRG_TEST_EDITOR_HOOK_FAIL") != NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "fixture hook asked to fail");
        return FALSE;
    }
    source = g_object_new (TEST_TYPE_FIXTURE_SOURCE, NULL);
    lrg_editor_app_add_asset_source (app, LRG_ASSET_SOURCE (source));
    return TRUE;
}

LRG_DEFINE_GAME_MODULE (TEST_TYPE_EDITOR_GAME,
                        "com.podbielniak.libregnum.test-editor",
                        "Test Editor Module",
                        "1.0.0")

LRG_DEFINE_GAME_MODULE_EDITOR (fixture_editor_init)
