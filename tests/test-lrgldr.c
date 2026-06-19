/* test-lrgldr.c - Tests for the generic lrgldr loader binary
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spawns the built lrgldr binary as a subprocess and checks its exit codes,
 * --info output, and `--` argument forwarding. Forwarding is verified with
 * --dry-run (load + configure, no window) against the test-args-module fixture,
 * which records the argv it received to $LRG_TEST_ARGS_OUT — so the whole
 * split -> load -> apply pipeline is exercised headlessly.
 *
 * LRGLDR_BIN_PATH and TEST_ARGS_MODULE_PATH are injected by the Makefile.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/wait.h>

#ifndef LRGLDR_BIN_PATH
#define LRGLDR_BIN_PATH ""
#endif
#ifndef TEST_ARGS_MODULE_PATH
#define TEST_ARGS_MODULE_PATH ""
#endif

/* Run lrgldr with @args (NULL-terminated, excluding argv0) and an optional
 * extra environment variable. Returns the process exit code (or -1) and, if
 * @out_stdout is non-NULL, the captured stdout. */
static gint
run_lrgldr (const gchar *const *args,
            const gchar        *env_key,
            const gchar        *env_val,
            gchar             **out_stdout)
{
    GPtrArray         *argv;
    g_auto(GStrv)      envp = NULL;
    g_autofree gchar  *sout = NULL;
    g_autoptr(GError)  error = NULL;
    gint               status = -1;
    gint               i;
    gboolean           ok;

    argv = g_ptr_array_new ();
    g_ptr_array_add (argv, (gpointer) LRGLDR_BIN_PATH);
    for (i = 0; args != NULL && args[i] != NULL; i++)
        g_ptr_array_add (argv, (gpointer) args[i]);
    g_ptr_array_add (argv, NULL);

    envp = g_get_environ ();
    if (env_key != NULL)
        envp = g_environ_setenv (envp, env_key, env_val, TRUE);

    ok = g_spawn_sync (NULL,
                       (gchar **) argv->pdata,
                       envp,
                       G_SPAWN_DEFAULT,
                       NULL, NULL,
                       &sout, NULL,
                       &status, &error);
    g_ptr_array_free (argv, TRUE);

    if (!ok)
    {
        g_test_message ("spawn failed: %s", error->message);
        return -1;
    }

    if (out_stdout != NULL)
        *out_stdout = g_steal_pointer (&sout);

    if (WIFEXITED (status))
        return WEXITSTATUS (status);
    return -1;
}

static gboolean
have_binaries (void)
{
    if (!g_file_test (LRGLDR_BIN_PATH, G_FILE_TEST_IS_EXECUTABLE))
    {
        g_test_skip ("lrgldr binary not built");
        return FALSE;
    }
    return TRUE;
}

static void
test_lrgldr_version (void)
{
    g_autofree gchar  *out = NULL;
    const gchar *const args[] = { "--version", NULL };
    gint               rc;

    if (!have_binaries ())
        return;

    rc = run_lrgldr (args, NULL, NULL, &out);
    g_assert_cmpint (rc, ==, 0);
    g_assert_nonnull (out);
    g_assert_true (g_strstr_len (out, -1, "libregnum") != NULL);
}

static void
test_lrgldr_no_module (void)
{
    const gchar *const args[] = { NULL };
    gint               rc;

    if (!have_binaries ())
        return;

    rc = run_lrgldr (args, NULL, NULL, NULL);
    g_assert_cmpint (rc, ==, 2);
}

static void
test_lrgldr_missing_module (void)
{
    const gchar *const args[] = { "/nonexistent/definitely-not-a-game.so", NULL };
    gint               rc;

    if (!have_binaries ())
        return;

    rc = run_lrgldr (args, NULL, NULL, NULL);
    g_assert_cmpint (rc, ==, 1);
}

static void
test_lrgldr_info (void)
{
    g_autofree gchar  *out = NULL;
    const gchar *const args[] = { "--info", TEST_ARGS_MODULE_PATH, NULL };
    gint               rc;

    if (!have_binaries ())
        return;
    if (!g_file_test (TEST_ARGS_MODULE_PATH, G_FILE_TEST_EXISTS))
    {
        g_test_skip ("fixture module not built");
        return;
    }

    rc = run_lrgldr (args, NULL, NULL, &out);
    g_assert_cmpint (rc, ==, 0);
    g_assert_nonnull (out);
    g_assert_true (g_strstr_len (out, -1, "Test Args Module") != NULL);
}

static void
test_lrgldr_forward_args (void)
{
    g_autofree gchar  *tmp = NULL;
    g_autofree gchar  *contents = NULL;
    g_autoptr(GError)  error = NULL;
    const gchar *const args[] = { "--dry-run", TEST_ARGS_MODULE_PATH,
                                  "--", "--foo", "bar", NULL };
    gint               rc;
    gint               fd;

    if (!have_binaries ())
        return;
    if (!g_file_test (TEST_ARGS_MODULE_PATH, G_FILE_TEST_EXISTS))
    {
        g_test_skip ("fixture module not built");
        return;
    }

    fd = g_file_open_tmp ("lrgldr-args-XXXXXX", &tmp, &error);
    g_assert_no_error (error);
    g_close (fd, NULL);

    rc = run_lrgldr (args, "LRG_TEST_ARGS_OUT", tmp, NULL);
    g_assert_cmpint (rc, ==, 0);

    g_assert_true (g_file_get_contents (tmp, &contents, NULL, &error));
    g_assert_no_error (error);
    /* The module receives [basename, --foo, bar]; check the forwarded tail. */
    g_assert_true (g_strstr_len (contents, -1, "--foo bar") != NULL);

    g_unlink (tmp);
}

static void
test_lrgldr_forward_error (void)
{
    const gchar *const args[] = { "--dry-run", TEST_ARGS_MODULE_PATH,
                                  "--", "--fail", NULL };
    gint               rc;

    if (!have_binaries ())
        return;
    if (!g_file_test (TEST_ARGS_MODULE_PATH, G_FILE_TEST_EXISTS))
    {
        g_test_skip ("fixture module not built");
        return;
    }

    /* The fixture returns FALSE on --fail; lrgldr reports a config error. */
    rc = run_lrgldr (args, NULL, NULL, NULL);
    g_assert_cmpint (rc, ==, 1);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/lrgldr/version", test_lrgldr_version);
    g_test_add_func ("/lrgldr/no-module", test_lrgldr_no_module);
    g_test_add_func ("/lrgldr/missing-module", test_lrgldr_missing_module);
    g_test_add_func ("/lrgldr/info", test_lrgldr_info);
    g_test_add_func ("/lrgldr/forward-args", test_lrgldr_forward_args);
    g_test_add_func ("/lrgldr/forward-error", test_lrgldr_forward_error);

    return g_test_run ();
}
