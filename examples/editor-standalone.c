/* editor-standalone.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Embedding the libregnum editor application.
 *
 * Usage:
 *   editor-standalone [file.rlevel]
 *
 * The full editor ships as the `lrgeditor` program; this example shows
 * how a tool of your own can host the same LrgEditorApp: create an
 * LrgEditor document, wrap it in the app, load the user's preferences,
 * add a custom action (it appears in menus, the command palette and the
 * keybinding editor), start the IPC server so Emacs can connect, and run.
 *
 * Without a .rlevel path a few sample cubes are created. Press
 * Ctrl+Shift+P for the command palette and run "Scatter cubes", or see
 * Help > Keyboard Shortcuts.
 *
 * Build:
 *   make examples
 */

#include <libregnum.h>
#include <stdio.h>

/* =============================================================================
 * Seed an empty level with a few nodes so the viewport is not blank.
 * ========================================================================== */

static void
seed_empty_level (LrgEditor *editor)
{
    static const struct
    {
        const gchar *name;
        gfloat       x;
        gfloat       y;
        gfloat       z;
    } seeds[] = {
        { "Cube_A",  0.0f,  0.5f,  0.0f },
        { "Cube_B",  2.5f,  0.5f,  0.0f },
        { "Cube_C", -2.5f,  0.5f,  0.0f },
        { "Cube_D",  0.0f,  0.5f,  2.5f },
    };
    gsize i;

    for (i = 0; i < G_N_ELEMENTS (seeds); i++)
    {
        LrgNode       *node;
        LrgNodeVisual *vis;

        node = lrg_node_new (seeds[i].name);
        lrg_node_set_location_xyz (node, seeds[i].x, seeds[i].y, seeds[i].z);
        lrg_node_set_scale_xyz (node, 1.0f, 1.0f, 1.0f);

        vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
        lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
        lrg_node_set_visual (node, vis);
        g_object_unref (vis);

        lrg_editor_add_node (editor, node, NULL);
        g_object_unref (node);
    }
}

/* "example.scatter": moves every top-level node to a random spot, as one
 * undoable step. */
static void
scatter_cubes (gpointer user_data)
{
    LrgEditor *editor = LRG_EDITOR (user_data);
    GPtrArray *top;
    guint      i;

    top = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (editor)));
    lrg_editor_begin_group (editor, "Scatter cubes");
    for (i = 0; i < top->len; i++)
    {
        LrgNode *node = g_ptr_array_index (top, i);
        gfloat   loc[3];
        gfloat   rot[3] = { 0.0f, 0.0f, 0.0f };
        gfloat   scl[3] = { 1.0f, 1.0f, 1.0f };

        loc[0] = (gfloat) g_random_double_range (-5.0, 5.0);
        loc[1] = 0.5f;
        loc[2] = (gfloat) g_random_double_range (-5.0, 5.0);
        lrg_editor_set_node_transform (editor, node, loc, rot, scl);
    }
    lrg_editor_end_group (editor);
}

/* =============================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_autoptr(LrgEditor)    editor = NULL;
    g_autoptr(LrgEditorApp) app    = NULL;
    const gchar            *path   = NULL;
    gboolean                loaded = FALSE;

    /* Display guard: the widget toolkit (LrgLabel / LrgCanvas) loads fonts
     * through raylib which requires GLFW and therefore a live display.
     * Exit cleanly in headless / CI environments before touching any widget.
     * We treat an unset *or empty* env var as "no display". */
    {
        const gchar *display  = g_getenv ("DISPLAY");
        const gchar *wayland  = g_getenv ("WAYLAND_DISPLAY");
        gboolean     has_disp;

        has_disp = (display != NULL && display[0] != '\0') ||
                   (wayland != NULL && wayland[0] != '\0');

        if (!has_disp)
        {
            fprintf (stderr,
                     "editor-standalone: no graphical display "
                     "(DISPLAY/WAYLAND_DISPLAY not set). Nothing to do.\n");
            return 0;
        }
    }

    (void) argc;

    if (argc > 1)
        path = argv[1];

    editor = lrg_editor_new ();

    if (path != NULL)
    {
        g_autoptr(GError) err = NULL;
        loaded = lrg_editor_load_level (editor, path, &err);
        if (!loaded)
        {
            fprintf (stderr, "editor-standalone: failed to load '%s': %s\n",
                     path, err != NULL ? err->message : "unknown error");
            fprintf (stderr, "Starting with an empty level instead.\n");
        }
    }

    if (!loaded)
        seed_empty_level (editor);

    app = lrg_editor_app_new (editor);

    /* The user's preferences (theme, $EDITOR choice, keybindings...). */
    {
        g_autoptr(GError) err = NULL;

        if (!lrg_editor_app_load_settings (app, NULL, &err))
            fprintf (stderr, "editor-standalone: preferences: %s\n", err->message);
    }

    /* A custom action: menus, palette and keybindings pick it up. */
    lrg_editor_actions_add (lrg_editor_app_get_actions (app), "example.scatter", "Scatter cubes",
                            "Example", "Ctrl+Alt+R", scatter_cubes, NULL, NULL, editor);

    /* Emacs (M-x libregnum-connect) and scripts talk to this socket. */
    {
        g_autoptr(GError) err = NULL;

        if (!lrg_editor_app_start_server (app, NULL, &err))
            fprintf (stderr, "editor-standalone: no IPC server: %s\n", err->message);
    }

    /* Blocks until the window is closed. */
    return lrg_editor_app_run (app);
}
