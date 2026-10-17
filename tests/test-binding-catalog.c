/* test-binding-catalog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgBindingCatalog: groups and order, owners, keys
 * compared without case, and every conflict policy of bind().
 */

#include <glib.h>
#include <libregnum.h>

static LrgBindingCatalog *
make_catalog (void)
{
    static const gchar *const jump[] = { "Space", NULL };
    static const gchar *const forward[] = { "W", "Up", NULL };
    static const gchar *const back[] = { "S", "Down", NULL };
    static const gchar *const map[] = { "M", NULL };
    LrgBindingCatalog *catalog;

    catalog = lrg_binding_catalog_new ();
    lrg_binding_catalog_add_group (catalog, "movement", "Movement");
    lrg_binding_catalog_add_group (catalog, "interface", "Interface");
    g_assert_true (lrg_binding_catalog_add_action (catalog, "move-forward", "Forward", "movement", NULL, forward));
    g_assert_true (lrg_binding_catalog_add_action (catalog, "move-backward", "Backward", "movement", NULL, back));
    g_assert_true (lrg_binding_catalog_add_action (catalog, "jump", NULL, "movement", NULL, jump));
    g_assert_true (lrg_binding_catalog_add_action (catalog, "automap", "Map", "interface", NULL, map));
    return catalog;
}

static void
test_groups_and_actions (void)
{
    g_autoptr(LrgBindingCatalog) catalog = make_catalog ();
    g_auto(GStrv) groups = NULL;
    g_auto(GStrv) movement = NULL;
    g_auto(GStrv) all = NULL;
    static const gchar *const fish[] = { "F", "f", " ", "G", NULL };
    const gchar * const *keys;

    groups = lrg_binding_catalog_get_groups (catalog);
    g_assert_cmpuint (g_strv_length (groups), ==, 2);
    g_assert_cmpstr (groups[1], ==, "interface");
    g_assert_cmpstr (lrg_binding_catalog_get_group_label (catalog, "movement"), ==, "Movement");
    movement = lrg_binding_catalog_get_actions (catalog, "movement");
    g_assert_cmpuint (g_strv_length (movement), ==, 3);
    g_assert_cmpstr (movement[2], ==, "jump");
    g_assert_cmpstr (lrg_binding_catalog_get_label (catalog, "jump"), ==, "jump");
    g_assert_cmpstr (lrg_binding_catalog_get_group (catalog, "automap"), ==, "interface");
    g_assert_false (lrg_binding_catalog_add_action (catalog, "jump", "again", "movement", NULL, NULL));

    /* a mod's action in a new group, duplicates and blanks dropped */
    g_assert_true (lrg_binding_catalog_add_action (catalog, "mod-fish", "Cast a line", "mods", "fishing", fish));
    keys = lrg_binding_catalog_get_keys (catalog, "mod-fish");
    g_assert_cmpuint (g_strv_length ((gchar **)keys), ==, 2);
    g_assert_cmpstr (keys[0], ==, "F");
    g_assert_cmpstr (keys[1], ==, "G");
    g_assert_cmpstr (lrg_binding_catalog_get_group_label (catalog, "mods"), ==, "mods");
    g_assert_cmpstr (lrg_binding_catalog_get_owner (catalog, "mod-fish"), ==, "fishing");
    g_assert_cmpuint (lrg_binding_catalog_remove_owner (catalog, "fishing"), ==, 1);
    g_assert_false (lrg_binding_catalog_has_action (catalog, "mod-fish"));
    g_assert_null (lrg_binding_catalog_get_keys (catalog, "mod-fish"));
    all = lrg_binding_catalog_get_actions (catalog, NULL);
    g_assert_cmpuint (g_strv_length (all), ==, 4);
    g_assert_true (lrg_binding_catalog_remove_action (catalog, "automap"));
    g_assert_false (lrg_binding_catalog_remove_action (catalog, "automap"));
}

static void
test_conflicts (void)
{
    g_autoptr(LrgBindingCatalog) catalog = make_catalog ();
    g_auto(GStrv) conflicts = NULL;
    g_auto(GStrv) found = NULL;
    g_auto(GStrv) keys = NULL;
    const gchar * const *k;

    /* found without case */
    found = lrg_binding_catalog_find_actions (catalog, "space", NULL);
    g_assert_cmpuint (g_strv_length (found), ==, 1);
    g_assert_cmpstr (found[0], ==, "jump");

    /* refuse: nothing changes, the conflict is reported */
    g_assert_false (lrg_binding_catalog_bind (catalog, "automap", 0, "w", LRG_BINDING_CONFLICT_REFUSE, &conflicts));
    g_assert_cmpuint (g_strv_length (conflicts), ==, 1);
    g_assert_cmpstr (conflicts[0], ==, "move-forward");
    g_assert_cmpstr (lrg_binding_catalog_get_keys (catalog, "automap")[0], ==, "M");
    g_clear_pointer (&conflicts, g_strfreev);

    /* keep: both hold it */
    g_assert_true (lrg_binding_catalog_bind (catalog, "automap", 1, "W", LRG_BINDING_CONFLICT_KEEP, NULL));
    keys = lrg_binding_catalog_get_conflicted_keys (catalog);
    g_assert_cmpuint (g_strv_length (keys), ==, 1);
    g_assert_cmpstr (keys[0], ==, "w");
    g_clear_pointer (&keys, g_strfreev);

    /* replace: the other loses it */
    g_assert_true (lrg_binding_catalog_bind (catalog, "jump", 0, "Up", LRG_BINDING_CONFLICT_REPLACE, &conflicts));
    g_assert_cmpstr (conflicts[0], ==, "move-forward");
    g_clear_pointer (&conflicts, g_strfreev);
    k = lrg_binding_catalog_get_keys (catalog, "jump");
    g_assert_cmpstr (k[0], ==, "Up");
    g_assert_null (k[1]);
    k = lrg_binding_catalog_get_keys (catalog, "move-forward");
    g_assert_cmpstr (k[0], ==, "W");
    g_assert_null (k[1]);

    /* swap: the other gets jump's old key in that slot */
    g_assert_true (lrg_binding_catalog_bind (catalog, "jump", 0, "S", LRG_BINDING_CONFLICT_SWAP, NULL));
    g_assert_cmpstr (lrg_binding_catalog_get_keys (catalog, "jump")[0], ==, "S");
    k = lrg_binding_catalog_get_keys (catalog, "move-backward");
    g_assert_cmpstr (k[0], ==, "Up");
    g_assert_cmpstr (k[1], ==, "Down");

    /* a new slot past the last, and unbinding */
    g_assert_true (lrg_binding_catalog_bind (catalog, "jump", 9, "J", LRG_BINDING_CONFLICT_REFUSE, NULL));
    g_assert_cmpstr (lrg_binding_catalog_get_keys (catalog, "jump")[1], ==, "J");
    g_assert_true (lrg_binding_catalog_unbind (catalog, "jump", "j"));
    g_assert_false (lrg_binding_catalog_unbind (catalog, "jump", "j"));
    g_assert_true (lrg_binding_catalog_unbind (catalog, "jump", NULL));
    g_assert_null (lrg_binding_catalog_get_keys (catalog, "jump")[0]);
    g_assert_false (lrg_binding_catalog_bind (catalog, "nothing", 0, "K", LRG_BINDING_CONFLICT_KEEP, NULL));
    g_assert_false (lrg_binding_catalog_bind (catalog, "jump", 0, "  ", LRG_BINDING_CONFLICT_KEEP, NULL));
}

static void
on_changed (LrgBindingCatalog *catalog,
            const gchar       *action,
            gpointer           user_data)
{
    GString *log = user_data;

    (void)catalog;
    g_string_append_printf (log, "%s;", action != NULL ? action : "*");
}

static void
test_signals_and_normalize (void)
{
    g_autoptr(LrgBindingCatalog) catalog = make_catalog ();
    g_autoptr(GString) log = g_string_new (NULL);
    g_autofree gchar *normal = NULL;

    g_signal_connect (catalog, "changed", G_CALLBACK (on_changed), log);
    lrg_binding_catalog_bind (catalog, "jump", 0, "W", LRG_BINDING_CONFLICT_REPLACE, NULL);
    g_assert_cmpstr (log->str, ==, "move-forward;jump;");
    normal = lrg_binding_catalog_normalize_key ("  Shift+A ");
    g_assert_cmpstr (normal, ==, "shift+a");
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/binding-catalog/groups-and-actions", test_groups_and_actions);
    g_test_add_func ("/binding-catalog/conflicts", test_conflicts);
    g_test_add_func ("/binding-catalog/signals", test_signals_and_normalize);

    return g_test_run ();
}
