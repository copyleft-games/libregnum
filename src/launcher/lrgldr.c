/* lrgldr.c - generic libregnum game-module loader/runner
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * `lrgldr` loads any libregnum game packaged as a shared module (.so) and runs
 * it with a standalone host (real window + main loop). It accepts its own
 * options and forwards everything after a `--` separator to the loaded module,
 * which configures itself through the #LrgConfigurable interface:
 *
 *     lrgldr [LRGLDR-OPTIONS] <module.so> [-- MODULE-ARGS...]
 *
 * The module is resolved, in priority order, from:
 *   1. the first positional argument
 *   2. the $LRG_GAME_MODULE environment variable
 *   3. a compiled-in default (-DLRG_LAUNCHER_DEFAULT_MODULE="...")
 *
 * If no `--` is given, module arguments fall back to $LRG_GAME_ARGS (parsed with
 * shell quoting). The SAME module can instead be loaded and driven by any
 * embedding host that links libregnum (e.g. an editor or cmacs).
 */

#include <libregnum.h>
#include <glib.h>

static gboolean opt_info       = FALSE;
static gboolean opt_fullscreen = FALSE;
static gboolean opt_windowed   = FALSE;
static gboolean opt_dry_run    = FALSE;
static gboolean opt_version    = FALSE;
static gint     opt_width      = -1;
static gint     opt_height     = -1;

static const GOptionEntry lrgldr_entries[] = {
    { "info", 'i', 0, G_OPTION_ARG_NONE, &opt_info,
      "Print the module's metadata and exit (do not run it)", NULL },
    { "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &opt_fullscreen,
      "Start the module in fullscreen", NULL },
    { "windowed", 0, 0, G_OPTION_ARG_NONE, &opt_windowed,
      "Start the module windowed", NULL },
    { "width", 'W', 0, G_OPTION_ARG_INT, &opt_width,
      "Window width in pixels", "N" },
    { "height", 'H', 0, G_OPTION_ARG_INT, &opt_height,
      "Window height in pixels", "N" },
    { "dry-run", 0, 0, G_OPTION_ARG_NONE, &opt_dry_run,
      "Load and configure the module, then exit without opening a window", NULL },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
      "Print version and exit", NULL },
    { NULL }
};

int
main (int argc, char **argv)
{
    g_autoptr(GOptionContext)  ctx = NULL;
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgLoadedGame)   loaded = NULL;
    g_auto(GStrv)              front = NULL;
    g_auto(GStrv)              module_argv = NULL;
    g_autofree gchar          *module_path = NULL;
    const LrgGameModuleInfo   *info;
    LrgGameTemplate           *game;
    GPtrArray                 *m;
    const gchar               *env_path;
    gint                       sep = -1;
    gint                       front_len;
    gint                       module_argc;
    gint                       i;

    /* Split argv at the first "--": everything before is for lrgldr, everything
     * after is forwarded verbatim to the module. Done manually so GOption never
     * sees (and consumes) the separator. */
    for (i = 1; i < argc; i++)
    {
        if (g_strcmp0 (argv[i], "--") == 0)
        {
            sep = i;
            break;
        }
    }
    front_len = (sep < 0) ? argc : sep;

    front = g_new0 (gchar *, (gsize) front_len + 1);
    for (i = 0; i < front_len; i++)
        front[i] = g_strdup (argv[i]);

    ctx = g_option_context_new ("<module.so> [-- MODULE-ARGS...]");
    g_option_context_set_summary (
        ctx,
        "Load and run a libregnum game module. Options before '--' configure the\n"
        "launcher; everything after '--' is forwarded to the module.\n"
        "\n"
        "The module is resolved from the first positional argument, else\n"
        "$LRG_GAME_MODULE. Module args fall back to $LRG_GAME_ARGS when no '--'\n"
        "is given.");
    g_option_context_add_main_entries (ctx, lrgldr_entries, NULL);

    if (!g_option_context_parse_strv (ctx, &front, &error))
    {
        g_printerr ("lrgldr: %s\n", error->message);
        return 2;
    }

    if (opt_version)
    {
        g_print ("lrgldr (libregnum %s), game-module ABI %d\n",
                 LRG_VERSION_STRING, LRG_GAME_MODULE_ABI_VERSION);
        return 0;
    }

    /* Resolve the module path: positional -> $LRG_GAME_MODULE -> compiled default. */
    if (front[0] != NULL && front[1] != NULL)
        module_path = g_strdup (front[1]);
    else if ((env_path = g_getenv ("LRG_GAME_MODULE")) != NULL && *env_path != '\0')
        module_path = g_strdup (env_path);
#ifdef LRG_LAUNCHER_DEFAULT_MODULE
    else
        module_path = g_strdup (LRG_LAUNCHER_DEFAULT_MODULE);
#endif

    if (module_path == NULL)
    {
        g_printerr ("usage: %s [options] <module.so> [-- module args...]\n",
                    argv[0]);
        g_printerr ("   or: set $LRG_GAME_MODULE to the module path\n");
        return 2;
    }

    loaded = lrg_loaded_game_load (module_path, &error);
    if (loaded == NULL)
    {
        g_printerr ("lrgldr: failed to load '%s': %s\n", module_path,
                    error != NULL ? error->message : "unknown error");
        return 1;
    }

    info = lrg_loaded_game_get_info (loaded);
    game = lrg_loaded_game_get_game (loaded);

    if (opt_info)
    {
        if (info != NULL)
        {
            g_print ("name:    %s\n", info->game_name != NULL ? info->game_name : "");
            g_print ("id:      %s\n", info->game_id != NULL ? info->game_id : "");
            g_print ("version: %s\n", info->game_version != NULL ? info->game_version : "");
            g_print ("abi:     %u\n", info->abi_version);
            g_print ("built against libregnum %u.%u.%u\n",
                     info->lrg_major, info->lrg_minor, info->lrg_micro);
        }
        return 0;
    }

    /* Apply launcher window options to the template before it starts up. */
    if (opt_width > 0 || opt_height > 0)
    {
        gint cw = 0, ch = 0;
        lrg_game_template_get_window_size (game, &cw, &ch);
        lrg_game_template_set_window_size (game,
                                           opt_width  > 0 ? opt_width  : cw,
                                           opt_height > 0 ? opt_height : ch);
    }
    if (opt_fullscreen)
        g_object_set (game, "fullscreen-mode", LRG_FULLSCREEN_FULLSCREEN, NULL);
    else if (opt_windowed)
        g_object_set (game, "fullscreen-mode", LRG_FULLSCREEN_WINDOWED, NULL);

    /* Build the module's argv: a synthetic program name + forwarded args. */
    m = g_ptr_array_new ();
    g_ptr_array_add (m, g_path_get_basename (module_path));
    if (sep >= 0)
    {
        for (i = sep + 1; i < argc; i++)
            g_ptr_array_add (m, g_strdup (argv[i]));
    }
    else
    {
        const gchar *env_args = g_getenv ("LRG_GAME_ARGS");
        if (env_args != NULL && *env_args != '\0')
        {
            gint    n = 0;
            gchar **parsed = NULL;

            if (!g_shell_parse_argv (env_args, &n, &parsed, &error))
            {
                g_printerr ("lrgldr: bad $LRG_GAME_ARGS: %s\n", error->message);
                g_ptr_array_set_free_func (m, g_free);
                g_ptr_array_free (m, TRUE);
                return 2;
            }
            for (i = 0; i < n; i++)
                g_ptr_array_add (m, g_strdup (parsed[i]));
            g_strfreev (parsed);
        }
    }
    g_ptr_array_add (m, NULL);
    module_argv = (GStrv) g_ptr_array_free (m, FALSE);
    module_argc = (gint) g_strv_length (module_argv);

    /* Dry run: configure the module headlessly (no window) and exit. */
    if (opt_dry_run)
    {
        if (!lrg_game_template_apply_args (game,
                                           (const gchar *const *) module_argv,
                                           &error))
        {
            g_printerr ("lrgldr: %s\n", error->message);
            return 1;
        }
        return 0;
    }

    /* run_standalone applies module_argv via LrgConfigurable before startup. */
    return lrg_game_run_standalone (game, module_argc, module_argv);
}
