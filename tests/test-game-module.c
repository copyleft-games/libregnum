/* test-game-module.c - Tests for loading games packaged as modules
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Exercises the LrgLoadedGame loader. The happy path needs a built game
 * module; point LRG_TEST_GAME_MODULE at one (e.g. the FPS demo module) to run
 * it, otherwise that test is skipped. The error-path test is unconditional.
 */

#include <glib.h>
#include <libregnum.h>

/* Loading a path that does not exist must fail cleanly with OPEN_FAILED. */
static void
test_game_module_load_missing (void)
{
    g_autoptr(GError)        error = NULL;
    g_autoptr(LrgLoadedGame) loaded = NULL;

    loaded = lrg_loaded_game_load ("/nonexistent/definitely-not-a-game.so",
                                   &error);

    g_assert_null (loaded);
    g_assert_error (error, LRG_LOADED_GAME_ERROR,
                    LRG_LOADED_GAME_ERROR_OPEN_FAILED);
}

/* Loading a real game module validates its ABI and instantiates the game. */
static void
test_game_module_load_from_env (void)
{
    const gchar             *path;
    g_autoptr(GError)        error = NULL;
    g_autoptr(LrgLoadedGame) loaded = NULL;
    const LrgGameModuleInfo *info;
    LrgGameTemplate         *game;

    path = g_getenv ("LRG_TEST_GAME_MODULE");
    if (path == NULL)
    {
        g_test_skip ("set LRG_TEST_GAME_MODULE to a built game .so to run this");
        return;
    }

    loaded = lrg_loaded_game_load (path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loaded);

    info = lrg_loaded_game_get_info (loaded);
    g_assert_nonnull (info);
    g_assert_cmpuint (info->abi_version, ==, LRG_GAME_MODULE_ABI_VERSION);
    g_assert_nonnull (info->game_id);
    g_assert_nonnull (info->get_type);

    game = lrg_loaded_game_get_game (loaded);
    g_assert_nonnull (game);
    g_assert_true (LRG_IS_GAME_TEMPLATE (game));

    /* Explicit unload releases the game; the accessor then returns NULL. */
    lrg_loaded_game_unload (loaded);
    g_assert_null (lrg_loaded_game_get_game (loaded));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/game-module/load-missing",
                     test_game_module_load_missing);
    g_test_add_func ("/game-module/load-from-env",
                     test_game_module_load_from_env);

    return g_test_run ();
}
