/* lrg-portable-private.h - POSIX calls with a Windows equivalent
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The few POSIX file-system calls libregnum uses that MinGW lacks, each
 * with the closest Windows behaviour.  On every other platform these are
 * the POSIX calls themselves (the same code as before).  Internal header;
 * not installed.  docs/platforms/windows.org lists the differences.
 */

#pragma once

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32

/*
 * lrg_realpath:
 * @path: a file path
 *
 * realpath(@path, NULL): the absolute, normalised path, malloc()ed (free()
 * it), or NULL when @path does not exist.  Windows: _fullpath(), which
 * makes the path absolute and folds "." and ".." but does not follow
 * junctions or symbolic links.
 */
static inline char *
lrg_realpath (const char *path)
{
	if (path == NULL || !g_file_test (path, G_FILE_TEST_EXISTS))
		return NULL;
	return _fullpath (NULL, path, 0);
}

#else

#define lrg_realpath(path) realpath ((path), NULL)

#endif

/* Windows' stat() never reports a symbolic link */
#ifndef S_ISLNK
#define S_ISLNK(mode) (0)
#endif

/*
 * lrg_portable_data_path:
 * @path: (inout): a relative path, edited in place
 *
 * A path written into data (levels, projects, sidecars, indexes) is
 * relative and separated by '/' on every OS, so a project made on Windows
 * opens elsewhere.  This turns Windows' '\' separators into '/'; on other
 * systems '\' is an ordinary file name character and is left alone.
 *
 * Returns: @path
 */
static inline gchar *
lrg_portable_data_path (gchar *path)
{
#ifdef G_OS_WIN32
	if (path != NULL)
		g_strdelimit (path, "\\", '/');
#endif
	return path;
}

/*
 * lrg_portable_relative_path:
 * @base: a directory
 * @path: a path that may lie under @base
 *
 * The part of @path below @base, as a data path (see
 * lrg_portable_data_path()): "" when @path is @base itself.  Either
 * separator may follow @base, since Windows paths often mix them.
 *
 * Returns: (transfer full) (nullable): the relative path, or %NULL when
 *   @path is not under @base
 */
static inline gchar *
lrg_portable_relative_path (const gchar *base,
                            const gchar *path)
{
	gsize len;

	if (base == NULL || path == NULL)
		return NULL;
	len = strlen (base);
	if (len == 0 || strncmp (path, base, len) != 0)
		return NULL;
	if (path[len] == '\0')
		return g_strdup ("");
	/* a base that ends in a separator ("/", "C:\\", "dir/") is followed
	 * directly by the rest; any other is followed by one separator */
	if (!G_IS_DIR_SEPARATOR (base[len - 1]))
	{
		if (!G_IS_DIR_SEPARATOR (path[len]))
			return NULL;
		len++;
	}
	return lrg_portable_data_path (g_strdup (path + len));
}

/*
 * lrg_portable_relative_to:
 * @from_dir: an absolute directory
 * @path: an absolute path
 *
 * @path relative to @from_dir, with ".." segments where it lies outside
 * ("../x/y.png"), as a data path: '/'-separated on every OS.  Both are
 * canonicalized first.  When they share no root (different drives on
 * Windows) no relative path exists and @path comes back canonical.
 *
 * Returns: (transfer full): the relative path
 */
static inline gchar *
lrg_portable_relative_to (const gchar *from_dir,
                          const gchar *path)
{
	gchar   *ca = g_canonicalize_filename (from_dir, NULL);
	gchar   *cb = g_canonicalize_filename (path, NULL);
	gchar  **pa = g_strsplit (ca, G_DIR_SEPARATOR_S, -1);
	gchar  **pb = g_strsplit (cb, G_DIR_SEPARATOR_S, -1);
	GString *out = g_string_new (NULL);
	guint    i = 0;
	guint    j;

	/* the common leading components */
	while (pa[i] != NULL && pb[i] != NULL && g_strcmp0 (pa[i], pb[i]) == 0)
		i++;

	if (i == 0)
	{
		/* no common root: different drives ("C:" vs "D:") */
		g_string_append (out, cb);
	}
	else
	{
		for (j = i; pa[j] != NULL; j++)
			if (*pa[j] != '\0')
				g_string_append (out, "../");
		for (j = i; pb[j] != NULL; j++)
		{
			g_string_append (out, pb[j]);
			if (pb[j + 1] != NULL)
				g_string_append_c (out, '/');
		}
	}

	g_strfreev (pa);
	g_strfreev (pb);
	g_free (ca);
	g_free (cb);
	return g_string_free (out, FALSE);
}

/*
 * lrg_portable_rename:
 * @from: the file or directory to move
 * @to: its new path
 *
 * g_rename(), but on Windows a short wait when another process has the
 * file open: Windows refuses to move an open file (EACCES), and a
 * thumbnail being generated, a virus scanner or an indexer opens files for
 * moments at a time.  It retries for up to two seconds before giving up;
 * elsewhere an open file moves freely and this is g_rename().
 *
 * Returns: 0 on success, -1 with errno set as by g_rename()
 */
static inline int
lrg_portable_rename (const gchar *from,
                     const gchar *to)
{
#ifdef G_OS_WIN32
	guint tries;
	int   rc = -1;

	for (tries = 0; tries < 100; tries++)
	{
		rc = g_rename (from, to);
		if (rc == 0 || errno != EACCES)
			break;
		g_usleep (20 * 1000);
	}
	return rc;
#else
	return g_rename (from, to);
#endif
}

#ifdef G_OS_WIN32
/* From <windows.h>, declared alone: that header's names (CloseWindow,
 * DrawText, ...) collide with raylib's in files that include both */
__declspec (dllimport) unsigned long __stdcall GetModuleFileNameW (void          *module,
                                                                   gunichar2     *filename,
                                                                   unsigned long  size);
#endif

/*
 * lrg_portable_exe_path:
 *
 * The running program's own file: /proc/self/exe on Linux (and Android),
 * GetModuleFileNameW() on Windows.  Used to find data shipped next to the
 * program (an uninstalled build tree, a relocatable install).
 *
 * Returns: (transfer full) (nullable): the absolute path, or %NULL where
 *   the system cannot say
 */
static inline gchar *
lrg_portable_exe_path (void)
{
#ifdef G_OS_WIN32
	gunichar2     buffer[4096];
	unsigned long n = GetModuleFileNameW (NULL, buffer, G_N_ELEMENTS (buffer));

	if (n == 0 || n >= G_N_ELEMENTS (buffer))
		return NULL;
	return g_utf16_to_utf8 (buffer, (glong) n, NULL, NULL, NULL);
#else
	return g_file_read_link ("/proc/self/exe", NULL);
#endif
}
