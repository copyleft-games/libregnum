/* lrg-args-file.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Arguments files (lrg-args-file.h): parse line by line with shell
 * quoting, then splice the result after argv[0].
 */

#include "config.h"
#include "lrg-args-file.h"

#include <string.h>

/*
 * lrg_args_file_parse:
 *
 * Each line is trimmed; blank and comment lines are skipped; the rest
 * go through g_shell_parse_argv(), whose "empty string" error for a
 * line of only quotes-less whitespace cannot happen after the blank
 * check.  The error names the 1-based line number.
 */
gboolean
lrg_args_file_parse (const gchar   *contents,
                     gchar       ***out_args,
                     GError       **error)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	g_auto(GStrv) lines = NULL;
	guint i;

	g_return_val_if_fail (contents != NULL, FALSE);
	g_return_val_if_fail (out_args != NULL, FALSE);

	builder = g_strv_builder_new ();
	lines = g_strsplit (contents, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_auto(GStrv) words = NULL;
		gchar *line;
		gint n;
		gint j;

		/* trim (also a trailing \r from a file edited on Windows) */
		line = g_strstrip (lines[i]);
		if (line[0] == '\0' || line[0] == '#')
			continue;

		if (!g_shell_parse_argv (line, &n, &words, error))
		{
			g_prefix_error (error, "line %u: ", i + 1);
			return FALSE;
		}
		for (j = 0; j < n; j++)
			g_strv_builder_add (builder, words[j]);
	}

	*out_args = g_strv_builder_end (builder);
	return TRUE;
}

/*
 * lrg_args_file_merge:
 *
 * Reads and parses first, so a bad file leaves argc/argv untouched.
 * The new array is argv[0], the file's arguments, then argv[1..argc-1].
 */
gboolean
lrg_args_file_merge (const gchar   *path,
                     gint          *argc,
                     gchar       ***argv,
                     GError       **error)
{
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) added = NULL;
	g_autoptr(GError) local_error = NULL;
	GStrvBuilder *builder;
	gchar **merged;
	guint n_added;
	gint i;

	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (argc != NULL && *argc >= 1, FALSE);
	g_return_val_if_fail (argv != NULL && *argv != NULL, FALSE);

	/* 1. read: a missing file means "no extra arguments" */
	if (!g_file_get_contents (path, &contents, NULL, &local_error))
	{
		if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			return TRUE;
		g_propagate_prefixed_error (error, g_steal_pointer (&local_error), "%s: ", path);
		return FALSE;
	}

	/* 2. parse */
	if (!lrg_args_file_parse (contents, &added, error))
	{
		g_prefix_error (error, "%s: ", path);
		return FALSE;
	}
	n_added = g_strv_length (added);
	if (n_added == 0)
		return TRUE;

	/* 3. splice after argv[0] */
	builder = g_strv_builder_new ();
	g_strv_builder_add (builder, (*argv)[0]);
	g_strv_builder_addv (builder, (const gchar **) added);
	for (i = 1; i < *argc; i++)
		g_strv_builder_add (builder, (*argv)[i]);
	merged = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);

	*argc += (gint) n_added;
	*argv = merged;
	return TRUE;
}
