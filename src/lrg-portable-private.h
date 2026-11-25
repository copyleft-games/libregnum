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

#include <stdlib.h>
#include <sys/stat.h>
#include <glib.h>

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
