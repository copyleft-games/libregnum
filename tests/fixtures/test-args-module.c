/* test-args-module.c - a minimal game module fixture for the lrgldr tests
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Built as a loadable .so (-DLRG_GAME_BUILD_MODULE). It overrides apply_args
 * (via #LrgConfigurable) to record the argument vector it receives into the file
 * named by $LRG_TEST_ARGS_OUT, and fails when it sees a `--fail` sentinel — so a
 * test can drive lrgldr and verify both forwarding and error propagation
 * headlessly (with --dry-run, no window).
 */

#include <libregnum.h>
#include <glib.h>

#define TEST_TYPE_ARGS_GAME (test_args_game_get_type ())
G_DECLARE_FINAL_TYPE (TestArgsGame, test_args_game, TEST, ARGS_GAME, LrgGameTemplate)

struct _TestArgsGame
{
    LrgGameTemplate parent_instance;
};

G_DEFINE_FINAL_TYPE (TestArgsGame, test_args_game, LRG_TYPE_GAME_TEMPLATE)

static gboolean
test_args_game_apply_args (LrgGameTemplate    *self,
                           const gchar *const *argv,
                           GError            **error)
{
    const gchar *out;
    gint         i;

    (void) self;

    if (argv != NULL)
    {
        for (i = 0; argv[i] != NULL; i++)
        {
            if (g_strcmp0 (argv[i], "--fail") == 0)
            {
                g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                             "fixture asked to fail");
                return FALSE;
            }
        }
    }

    out = g_getenv ("LRG_TEST_ARGS_OUT");
    if (out != NULL && *out != '\0')
    {
        gchar *joined;

        joined = (argv != NULL) ? g_strjoinv (" ", (gchar **) argv)
                                : g_strdup ("");
        g_file_set_contents (out, joined, -1, NULL);
        g_free (joined);
    }

    return TRUE;
}

static void
test_args_game_class_init (TestArgsGameClass *klass)
{
    LRG_GAME_TEMPLATE_CLASS (klass)->apply_args = test_args_game_apply_args;
}

static void
test_args_game_init (TestArgsGame *self)
{
    (void) self;
}

LRG_DEFINE_GAME_MODULE (TEST_TYPE_ARGS_GAME,
                        "com.podbielniak.libregnum.test-args",
                        "Test Args Module",
                        "1.0.0")
