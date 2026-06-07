/* editor-standalone.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone level editor example for libregnum.
 *
 * Usage:
 *   editor-standalone [file.rlevel]
 *
 * If a .rlevel path is given, the level is loaded from disk; otherwise an
 * empty level is created with a handful of sample nodes so there is something
 * visible on first launch.
 *
 * Controls (window must have focus):
 *   Right-mouse drag  -- orbit camera
 *   Scroll wheel      -- zoom in / out
 *   Left-click (viewport) -- cycle node selection
 *   N                 -- add a new cube node
 *   Delete            -- delete selected node
 *   Ctrl+Z            -- undo
 *   Ctrl+Y            -- redo
 *   Ctrl+S            -- save (only if a file path was provided)
 *   Escape            -- quit
 *
 * Build:
 *   make BUILD_EDITOR_UI=1 examples
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

    /* Run blocks until the window is closed (or exits immediately if headless) */
    lrg_editor_app_run (app);

    return 0;
}
