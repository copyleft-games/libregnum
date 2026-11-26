/* test-args-file.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Arguments files (lrg-args-file.h): parsing (comments, blank lines,
 * quoting, CRLF, malformed lines) and merging after argv[0] (order,
 * missing file, empty file, unreadable or malformed file leaving the
 * arguments untouched).
 */

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* write @text to a new file in a fresh temporary directory */
static gchar *
write_temp (const gchar *text)
{
	g_autofree gchar *dir = NULL;
	gchar *path;

	dir = g_dir_make_tmp ("lrg-args-XXXXXX", NULL);
	g_assert_nonnull (dir);
	path = g_build_filename (dir, "game.args", NULL);
	g_assert_true (g_file_set_contents (path, text, -1, NULL));
	return path;
}

static void
test_parse (void)
{
	g_auto(GStrv) args = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_args_file_parse ("# a comment\n"
	                                    "\n"
	                                    "  --standalone scourgetide  \r\n"
	                                    "   # indented comment\n"
	                                    "--set \"client.locale=en US\"\n"
	                                    "-s'a=b'\n",
	                                    &args, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_strv_length (args), ==, 5);
	g_assert_cmpstr (args[0], ==, "--standalone");
	g_assert_cmpstr (args[1], ==, "scourgetide");
	g_assert_cmpstr (args[2], ==, "--set");
	g_assert_cmpstr (args[3], ==, "client.locale=en US");
	g_assert_cmpstr (args[4], ==, "-sa=b");
}

static void
test_parse_empty (void)
{
	g_auto(GStrv) args = NULL;

	g_assert_true (lrg_args_file_parse ("", &args, NULL));
	g_assert_cmpuint (g_strv_length (args), ==, 0);
	g_clear_pointer (&args, g_strfreev);
	g_assert_true (lrg_args_file_parse ("# only\n\n   \n", &args, NULL));
	g_assert_cmpuint (g_strv_length (args), ==, 0);
}

static void
test_parse_malformed (void)
{
	g_auto(GStrv) args = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_false (lrg_args_file_parse ("--ok\n--bad \"unterminated\n", &args, &error));
	g_assert_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING);
	g_assert_true (g_str_has_prefix (error->message, "line 2: "));
	g_assert_null (args);
}

static void
test_merge (void)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;
	gchar *original[] = { (gchar *) "game", (gchar *) "--cli", NULL };
	gchar **argv;
	gint argc;

	path = write_temp ("--file-a\n--file-b value\n");
	argc = 2;
	argv = original;
	g_assert_true (lrg_args_file_merge (path, &argc, &argv, &error));
	g_assert_no_error (error);
	g_assert_true (argv != original);
	g_assert_cmpint (argc, ==, 5);
	g_assert_cmpstr (argv[0], ==, "game");
	g_assert_cmpstr (argv[1], ==, "--file-a");
	g_assert_cmpstr (argv[2], ==, "--file-b");
	g_assert_cmpstr (argv[3], ==, "value");
	g_assert_cmpstr (argv[4], ==, "--cli");
	g_assert_null (argv[5]);
	g_strfreev (argv);
	g_unlink (path);
}

static void
test_merge_missing_or_empty (void)
{
	g_autofree gchar *path = NULL;
	gchar *original[] = { (gchar *) "game", NULL };
	gchar **argv;
	gint argc;

	argc = 1;
	argv = original;
	g_assert_true (lrg_args_file_merge ("/nonexistent/lrg/game.args", &argc, &argv, NULL));
	g_assert_true (argv == original);
	g_assert_cmpint (argc, ==, 1);

	path = write_temp ("# nothing\n");
	g_assert_true (lrg_args_file_merge (path, &argc, &argv, NULL));
	g_assert_true (argv == original);
	g_assert_cmpint (argc, ==, 1);
	g_unlink (path);
}

static void
test_merge_bad_leaves_args (void)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) error = NULL;
	gchar *original[] = { (gchar *) "game", NULL };
	gchar **argv;
	gint argc;

	/* malformed */
	path = write_temp ("--x 'open\n");
	argc = 1;
	argv = original;
	g_assert_false (lrg_args_file_merge (path, &argc, &argv, &error));
	g_assert_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING);
	g_assert_nonnull (strstr (error->message, path));
	g_assert_true (argv == original);
	g_assert_cmpint (argc, ==, 1);
	g_clear_error (&error);

	/* a directory is not readable as a file */
	dir = g_path_get_dirname (path);
	g_assert_false (lrg_args_file_merge (dir, &argc, &argv, &error));
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_ISDIR);
	g_assert_true (argv == original);
	g_unlink (path);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

	g_test_add_func ("/args-file/parse", test_parse);
	g_test_add_func ("/args-file/parse-empty", test_parse_empty);
	g_test_add_func ("/args-file/parse-malformed", test_parse_malformed);
	g_test_add_func ("/args-file/merge", test_merge);
	g_test_add_func ("/args-file/merge-missing-or-empty", test_merge_missing_or_empty);
	g_test_add_func ("/args-file/merge-bad-leaves-args", test_merge_bad_leaves_args);

	return g_test_run ();
}
