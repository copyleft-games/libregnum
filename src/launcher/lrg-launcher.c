/* lrg-launcher.c - Generic shim that loads and runs a libregnum game module
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This is the thin standalone launcher: it loads a game packaged as a shared
 * module and runs it with a standalone host (real window + main loop). The
 * SAME module can instead be loaded and driven by any embedding host that
 * links libregnum (e.g. an editor or cmacs).
 *
 * The module is resolved, in priority order, from:
 *   1. argv[1]
 *   2. the $LRG_GAME_MODULE environment variable
 *   3. a compiled-in default (-DLRG_LAUNCHER_DEFAULT_MODULE="...")
 */

#include <libregnum.h>
#include <glib.h>

int
main (int argc, char **argv)
{
    g_autofree gchar        *module_path = NULL;
    g_autoptr(GError)        error = NULL;
    g_autoptr(LrgLoadedGame) loaded = NULL;
    const LrgGameModuleInfo *info;
    LrgGameTemplate         *game;
    const gchar             *env_path;

    if (argc > 1)
        module_path = g_strdup (argv[1]);
    else if ((env_path = g_getenv ("LRG_GAME_MODULE")) != NULL)
        module_path = g_strdup (env_path);
#ifdef LRG_LAUNCHER_DEFAULT_MODULE
    else
        module_path = g_strdup (LRG_LAUNCHER_DEFAULT_MODULE);
#endif

    if (module_path == NULL)
    {
        g_printerr ("usage: %s <game-module.so> [game args...]\n", argv[0]);
        g_printerr ("   or: set $LRG_GAME_MODULE to the module path\n");
        return 2;
    }

    loaded = lrg_loaded_game_load (module_path, &error);
    if (loaded == NULL)
    {
        g_printerr ("%s: failed to load game module '%s': %s\n",
                    argv[0], module_path,
                    error != NULL ? error->message : "unknown error");
        return 1;
    }

    info = lrg_loaded_game_get_info (loaded);
    if (info != NULL)
        g_message ("Launching %s %s [%s]",
                   info->game_name != NULL ? info->game_name : "game",
                   info->game_version != NULL ? info->game_version : "",
                   info->game_id != NULL ? info->game_id : "");

    game = lrg_loaded_game_get_game (loaded);

    return lrg_game_run_standalone (game, argc, argv);
}
