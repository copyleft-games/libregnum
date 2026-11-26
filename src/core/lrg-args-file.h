/* lrg-args-file.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Command-line arguments read from a file, for platforms that start a
 * game without any (Android's NativeActivity calls main() with argv[0]
 * alone).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-args-file
 * @title: Arguments files
 * @short_description: Command-line arguments from a text file
 *
 * An Android app has no command line: the NativeActivity glue calls the
 * game's main() with `argv[0]` only.  A game that takes options on the
 * desktop can read the same options from a text file the player can
 * edit, such as `<external files dir>/<game>.args`
 * (lrg_platform_get_external_data_dir()), which is reachable with `adb
 * push` and a file manager.
 *
 * The format: one or more arguments per line, split with shell quoting
 * rules (g_shell_parse_argv(), so `--name "two words"` is two
 * arguments); blank lines and lines whose first non-blank character is
 * `#` are skipped.
 *
 * |[<!-- language="plain" -->
 * # librewow.args
 * --standalone scourgetide
 * --set "client.locale=enUS"
 * ]|
 *
 * The file's arguments go after `argv[0]` and before the real command
 * line's, so on a desktop an option given on the command line wins over
 * the same option in the file (GOptionContext keeps the last value).
 */

/**
 * lrg_args_file_parse:
 * @contents: the file's text
 * @out_args: (out) (transfer full) (array zero-terminated=1): the
 *   arguments, in order
 * @error: return location for a #GError, or %NULL
 *
 * Splits an arguments file into its arguments.  A line that does not
 * parse (an unterminated quote) fails the whole file with
 * %G_SHELL_ERROR, the message naming the line; nothing is returned
 * then.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_args_file_parse            (const gchar   *contents,
                                             gchar       ***out_args,
                                             GError       **error);

/**
 * lrg_args_file_merge:
 * @path: (type filename): the arguments file
 * @argc: (inout): the argument count
 * @argv: (inout) (array length=argc) (transfer full): the arguments;
 *   replaced by a new array when the file adds any
 * @error: return location for a #GError, or %NULL
 *
 * Inserts the arguments of @path after `(*argv)[0]`.  A missing file is
 * not an error: nothing changes.  When arguments are added, *@argv is
 * replaced by a newly allocated %NULL-terminated array (free it with
 * g_strfreev()); the old array is NOT freed (it usually is main()'s).
 * On failure (unreadable or malformed file) nothing changes.
 *
 * Returns: %TRUE when the arguments are ready (with or without the file)
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_args_file_merge            (const gchar   *path,
                                             gint          *argc,
                                             gchar       ***argv,
                                             GError       **error);

G_END_DECLS
