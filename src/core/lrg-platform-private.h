/* lrg-platform-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal parts of lrg-platform.c that the tests check on any platform.
 */

#pragma once

#include <string.h>
#include <glib.h>

G_BEGIN_DECLS

/* Android's log priorities (<android/log.h>, android_LogPriority), mirrored
 * so the mapping below compiles and is tested everywhere */
#define LRG_ANDROID_LOG_VERBOSE (2)
#define LRG_ANDROID_LOG_DEBUG   (3)
#define LRG_ANDROID_LOG_INFO    (4)
#define LRG_ANDROID_LOG_WARN    (5)
#define LRG_ANDROID_LOG_ERROR   (6)
#define LRG_ANDROID_LOG_FATAL   (7)

/*
 * lrg_platform_android_priority:
 * @level: a GLib log level (only the most severe bit set counts)
 *
 * The logcat priority a GLib message of @level is written with: errors
 * are fatal, criticals errors, warnings warnings, messages and infos info,
 * debug debug.
 */
static inline gint
lrg_platform_android_priority (GLogLevelFlags level)
{
	if (level & G_LOG_LEVEL_ERROR)
		return LRG_ANDROID_LOG_FATAL;
	if (level & G_LOG_LEVEL_CRITICAL)
		return LRG_ANDROID_LOG_ERROR;
	if (level & G_LOG_LEVEL_WARNING)
		return LRG_ANDROID_LOG_WARN;
	if (level & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO))
		return LRG_ANDROID_LOG_INFO;
	if (level & G_LOG_LEVEL_DEBUG)
		return LRG_ANDROID_LOG_DEBUG;
	return LRG_ANDROID_LOG_VERBOSE;
}

/*
 * lrg_platform_asset_path_is_safe:
 * @path: a path from an asset manifest
 *
 * Whether extracting @path stays inside the destination: relative,
 * `/`-separated, no empty, `.` or `..` component, no backslash.
 */
static inline gboolean
lrg_platform_asset_path_is_safe (const gchar *path)
{
	g_auto(GStrv) parts = NULL;
	guint i;

	if (path == NULL || path[0] == '\0' || path[0] == '/' || strchr (path, '\\') != NULL)
		return FALSE;
	parts = g_strsplit (path, "/", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		if (parts[i][0] == '\0' || g_strcmp0 (parts[i], ".") == 0 || g_strcmp0 (parts[i], "..") == 0)
			return FALSE;
	}
	return TRUE;
}

G_END_DECLS
