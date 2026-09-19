/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <glib/gstdio.h>

typedef struct
{
    LrgAssetManager *manager;
    gchar *directory;
    GString *order;
    guint expected;
    gboolean clear;
} Fixture;

static void
write_asset (Fixture *f, const gchar *name, const gchar *yaml)
{
    g_autofree gchar *path = g_build_filename (f->directory, name, NULL);
    g_autoptr(GError) error = NULL;

    g_assert_true (g_file_set_contents (path, yaml, -1, &error));
    g_assert_no_error (error);
}

static void
on_reload (LrgAssetManager *manager, const gchar *name,
           GObject *previous, GObject *replacement, gpointer data)
{
    Fixture *f = data;
    g_autoptr(GError) error = NULL;

    g_assert_true (LRG_IS_ITEM_DEF (previous));
    g_assert_true (LRG_IS_ITEM_DEF (replacement));
    if (f->order->len == 0 && f->expected != 0)
    {
        const gchar *names[] = { "a.yaml", "b.yaml", "c.yaml", "d.yaml" };
        guint i;

        for (i = 0; i < 4; i++)
            g_assert_cmpint (lrg_item_def_get_value (LRG_ITEM_DEF (
                lrg_asset_manager_load_object (manager, names[i], NULL))), ==, f->expected);
    }
    g_string_append_c (f->order, name[0]);
    g_assert_false (lrg_asset_manager_reload_object (manager, name, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    if (f->clear)
        lrg_asset_manager_unload_all (manager);
}

static void
setup (Fixture *f, gconstpointer data)
{
    g_autoptr(LrgRegistry) registry = lrg_registry_new ();
    g_autoptr(LrgDataLoader) loader = lrg_data_loader_new ();
    const gchar *names[] = { "a.yaml", "b.yaml", "c.yaml", "d.yaml" };
    guint i;

    f->directory = g_dir_make_tmp ("lrg-dependencies-XXXXXX", NULL);
    g_assert_nonnull (f->directory);
    f->manager = lrg_asset_manager_new ();
    f->order = g_string_new (NULL);
    lrg_registry_register_builtin (registry);
    lrg_data_loader_set_registry (loader, registry);
    lrg_asset_manager_set_data_loader (f->manager, loader);
    lrg_asset_manager_add_search_path (f->manager, f->directory);
    for (i = 0; i < 4; i++)
    {
        write_asset (f, names[i], "type: item-def\nvalue: 1\n");
        g_assert_nonnull (lrg_asset_manager_load_object (f->manager, names[i], NULL));
    }
    g_assert_true (lrg_asset_manager_add_object_dependency (f->manager, "b.yaml", "a.yaml", NULL));
    g_assert_true (lrg_asset_manager_add_object_dependency (f->manager, "c.yaml", "a.yaml", NULL));
    g_assert_true (lrg_asset_manager_add_object_dependency (f->manager, "d.yaml", "b.yaml", NULL));
    g_assert_true (lrg_asset_manager_add_object_dependency (f->manager, "d.yaml", "c.yaml", NULL));
    g_signal_connect (f->manager, "object-reloaded", G_CALLBACK (on_reload), f);
}

static void
teardown (Fixture *f, gconstpointer data)
{
    const gchar *names[] = { "a.yaml", "b.yaml", "c.yaml", "d.yaml" };
    guint i;

    g_object_unref (f->manager);
    for (i = 0; i < 4; i++)
    {
        g_autofree gchar *path = g_build_filename (f->directory, names[i], NULL);

        g_assert_cmpint (g_remove (path), ==, 0);
    }
    g_assert_cmpint (g_rmdir (f->directory), ==, 0);
    g_free (f->directory);
    g_string_free (f->order, TRUE);
}

static void
test_diamond (Fixture *f, gconstpointer data)
{
    const gchar *names[] = { "a.yaml", "b.yaml", "c.yaml", "d.yaml" };
    guint i;

    for (i = 0; i < 4; i++)
        write_asset (f, names[i], "type: item-def\nvalue: 9\n");
    f->expected = 9;
    g_assert_true (lrg_asset_manager_reload_object (f->manager, "a.yaml", NULL));
    g_assert_cmpstr (f->order->str, ==, "abcd");
}

static void
test_rollback (Fixture *f, gconstpointer data)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GObject) original = g_object_ref (lrg_asset_manager_load_object (f->manager, "a.yaml", NULL));

    write_asset (f, "a.yaml", "type: item-def\nvalue: 9\n");
    write_asset (f, "d.yaml", "type: item-def\nvalue: invalid\n");
    g_assert_false (lrg_asset_manager_reload_object (f->manager, "a.yaml", &error));
    g_assert_nonnull (error);
    g_assert_nonnull (strstr (error->message, "d.yaml"));
    g_assert_true (original == lrg_asset_manager_load_object (f->manager, "a.yaml", NULL));
    g_assert_cmpuint (f->order->len, ==, 0);
    write_asset (f, "d.yaml", "type: item-def\nvalue: 2\n");
    g_assert_true (lrg_asset_manager_reload_object (f->manager, "a.yaml", NULL));
    g_assert_cmpstr (f->order->str, ==, "abcd");
}

static void
test_links (Fixture *f, gconstpointer data)
{
    g_autoptr(GError) error = NULL;

    g_assert_false (lrg_asset_manager_add_object_dependency (f->manager, "a.yaml", "d.yaml", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_false (lrg_asset_manager_add_object_dependency (f->manager, "a.yaml", "missing", &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_true (lrg_asset_manager_add_object_dependency (f->manager, "b.yaml", "a.yaml", NULL));
    g_assert_true (lrg_asset_manager_remove_object_dependency (f->manager, "b.yaml", "a.yaml"));
    g_assert_false (lrg_asset_manager_remove_object_dependency (f->manager, "b.yaml", "a.yaml"));
    g_assert_true (lrg_asset_manager_unload (f->manager, "c.yaml"));
    g_assert_nonnull (lrg_asset_manager_load_object (f->manager, "c.yaml", NULL));
    g_assert_true (lrg_asset_manager_reload_object (f->manager, "a.yaml", NULL));
    g_assert_cmpstr (f->order->str, ==, "a");
}

static void
test_clear_during_signal (Fixture *f, gconstpointer data)
{
    f->clear = TRUE;
    g_assert_true (lrg_asset_manager_reload_object (f->manager, "a.yaml", NULL));
    g_assert_cmpstr (f->order->str, ==, "abcd");
    g_assert_false (lrg_asset_manager_is_cached (f->manager, "a.yaml"));
}

static void
test_watch (Fixture *f, gconstpointer data)
{
    gint64 deadline;

    g_assert_true (lrg_asset_manager_watch_object (f->manager, "a.yaml", NULL));
    write_asset (f, "a.yaml", "type: item-def\nvalue: 3\n");
    deadline = g_get_monotonic_time () + 5000000;
    while (f->order->len < 4 && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }
    g_assert_cmpstr (f->order->str, ==, "abcd");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add ("/dependencies/diamond", Fixture, NULL, setup, test_diamond, teardown);
    g_test_add ("/dependencies/rollback", Fixture, NULL, setup, test_rollback, teardown);
    g_test_add ("/dependencies/links", Fixture, NULL, setup, test_links, teardown);
    g_test_add ("/dependencies/signal-clear", Fixture, NULL, setup, test_clear_during_signal, teardown);
    g_test_add ("/dependencies/watch", Fixture, NULL, setup, test_watch, teardown);
    return g_test_run ();
}
