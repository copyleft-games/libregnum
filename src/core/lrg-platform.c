/* lrg-platform.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Platform preparation: the portable parts (data root, asset extraction)
 * and, on Android, the glue that applies them to the app's internal
 * storage and APK, plus a GLib log writer for logcat.
 */

/* The NDK headers used below define plain `inline' helpers that call APIs newer
 * than libregnum's minimum (ANativeWindow_clearFrameRate, API 31).  Under
 * gnu89 a plain inline function is emitted out of line in every file, so
 * its reference to the newer API would be a link error.  This is the NDK's
 * own switch for "use newer APIs only when present": such references
 * become weak (null on older devices), and nothing here calls them.  It
 * must come before the first system header, hence before config.h. */
#ifdef LRG_PLATFORM_ANDROID
#define __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ 1
#endif

#include "config.h"

#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "lrg-platform.h"
#include "lrg-platform-private.h"

#ifdef LRG_PLATFORM_ANDROID
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

/* raylib's PLATFORM_ANDROID (rcore_android.c) keeps the app its
 * android_main() was given and exposes it without a header */
extern struct android_app *GetAndroidApp (void);
#endif

/* ==========================================================================
 * Data root
 * ========================================================================== */

/* set_dir: one environment variable pointing at a created directory */
static gboolean
set_dir (const gchar  *variable,
         const gchar  *path,
         GError      **error)
{
	if (g_mkdir_with_parents (path, 0700) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot create %s (%s): %s", path, variable, g_strerror (saved));
		return FALSE;
	}
	if (!g_setenv (variable, path, TRUE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot set %s", variable);
		return FALSE;
	}
	return TRUE;
}

/* lrg_platform_set_data_root: documented in lrg-platform.h */
gboolean
lrg_platform_set_data_root (const gchar  *root,
                            GError      **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *config = NULL;
	g_autofree gchar *cache = NULL;
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail (root != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	data = g_build_filename (root, "data", NULL);
	config = g_build_filename (root, "config", NULL);
	cache = g_build_filename (root, "cache", NULL);
	tmp = g_build_filename (root, "tmp", NULL);

	return set_dir ("HOME", root, error) &&
	       set_dir ("XDG_DATA_HOME", data, error) &&
	       set_dir ("XDG_CONFIG_HOME", config, error) &&
	       set_dir ("XDG_CACHE_HOME", cache, error) &&
	       set_dir ("TMPDIR", tmp, error);
}

/* ==========================================================================
 * Asset extraction
 * ========================================================================== */

#define STAMP_FILE ".lrg-assets-stamp"
#define MANIFEST_MAGIC "# lrg-assets "

/* parse_manifest: the stamp and the file list of a manifest, every path
 * checked before anything is written */
static gboolean
parse_manifest (GBytes      *bytes,
                gchar      **out_stamp,
                GPtrArray  **out_paths,
                GError     **error)
{
	g_autofree gchar *text = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GPtrArray) paths = NULL;
	gsize size;
	const gchar *data;
	guint i;

	data = g_bytes_get_data (bytes, &size);
	text = g_strndup (data, size);
	lines = g_strsplit (text, "\n", -1);
	if (lines[0] == NULL || !g_str_has_prefix (lines[0], MANIFEST_MAGIC) ||
	    g_strstrip (lines[0] + strlen (MANIFEST_MAGIC))[0] == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		             "%s does not start with \"%s<stamp>\"",
		             LRG_PLATFORM_ASSET_MANIFEST, MANIFEST_MAGIC);
		return FALSE;
	}

	paths = g_ptr_array_new_with_free_func (g_free);
	for (i = 1; lines[i] != NULL; i++)
	{
		gchar *line = g_strstrip (lines[i]);

		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (!lrg_platform_asset_path_is_safe (line))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			             "%s line %u: unsafe path \"%s\"",
			             LRG_PLATFORM_ASSET_MANIFEST, i + 1, line);
			return FALSE;
		}
		g_ptr_array_add (paths, g_strdup (line));
	}

	*out_stamp = g_strdup (g_strstrip (lines[0] + strlen (MANIFEST_MAGIC)));
	*out_paths = g_steal_pointer (&paths);
	return TRUE;
}

/* remove_tree: deletes @path and everything below it, without following
 * symbolic links (they are removed, not entered) */
static gboolean
remove_tree (const gchar  *path,
             GError      **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *name;

	if (!g_file_test (path, G_FILE_TEST_EXISTS) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
		return TRUE;
	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		dir = g_dir_open (path, 0, error);
		if (dir == NULL)
			return FALSE;
		while ((name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			if (!remove_tree (child, error))
				return FALSE;
		}
		if (g_rmdir (path) != 0)
		{
			gint saved = errno;

			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			             "Cannot remove %s: %s", path, g_strerror (saved));
			return FALSE;
		}
		return TRUE;
	}
	if (g_unlink (path) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot remove %s: %s", path, g_strerror (saved));
		return FALSE;
	}
	return TRUE;
}

/* lrg_platform_extract_assets: documented in lrg-platform.h */
gboolean
lrg_platform_extract_assets (LrgPlatformAssetReadFunc   read,
                             gpointer                   user_data,
                             const gchar               *dest,
                             GError                   **error)
{
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(GPtrArray) paths = NULL;
	g_autofree gchar *stamp = NULL;
	g_autofree gchar *stamp_file = NULL;
	g_autofree gchar *current = NULL;
	g_autofree gchar *staging = NULL;
	g_autofree gchar *retired = NULL;
	g_autofree gchar *staged_stamp = NULL;
	guint i;

	g_return_val_if_fail (read != NULL, FALSE);
	g_return_val_if_fail (dest != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* the manifest decides everything; a bad one writes nothing */
	manifest = read (LRG_PLATFORM_ASSET_MANIFEST, user_data, error);
	if (manifest == NULL)
		return FALSE;
	if (!parse_manifest (manifest, &stamp, &paths, error))
		return FALSE;

	/* up to date already? */
	stamp_file = g_build_filename (dest, STAMP_FILE, NULL);
	if (g_file_get_contents (stamp_file, &current, NULL, NULL) &&
	    g_strcmp0 (g_strstrip (current), stamp) == 0)
		return TRUE;

	/* extract into <dest>.new (left over from an interrupted run: start
	 * clean) */
	staging = g_strconcat (dest, ".new", NULL);
	retired = g_strconcat (dest, ".old", NULL);
	if (!remove_tree (staging, error) || !remove_tree (retired, error))
		return FALSE;
	if (g_mkdir_with_parents (staging, 0700) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot create %s: %s", staging, g_strerror (saved));
		return FALSE;
	}
	for (i = 0; i < paths->len; i++)
	{
		const gchar *path = g_ptr_array_index (paths, i);
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *target = NULL;
		g_autofree gchar *parent = NULL;
		gconstpointer data;
		gsize size;

		bytes = read (path, user_data, error);
		if (bytes == NULL)
			return FALSE;
		target = g_build_filename (staging, path, NULL);
		parent = g_path_get_dirname (target);
		if (g_mkdir_with_parents (parent, 0700) != 0)
		{
			gint saved = errno;

			g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
			             "Cannot create %s: %s", parent, g_strerror (saved));
			return FALSE;
		}
		data = g_bytes_get_data (bytes, &size);
		if (!g_file_set_contents (target, data, (gssize) size, error))
			return FALSE;
	}
	staged_stamp = g_build_filename (staging, STAMP_FILE, NULL);
	if (!g_file_set_contents (staged_stamp, stamp, -1, error))
		return FALSE;

	/* swap: the old tree steps aside, the new one takes its name */
	if (g_file_test (dest, G_FILE_TEST_EXISTS) && g_rename (dest, retired) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot move %s aside: %s", dest, g_strerror (saved));
		return FALSE;
	}
	if (g_rename (staging, dest) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot move %s into place: %s", staging, g_strerror (saved));
		return FALSE;
	}
	return remove_tree (retired, error);
}

/* ==========================================================================
 * Android
 * ========================================================================== */

#ifdef LRG_PLATFORM_ANDROID

/* read_apk_asset: LrgPlatformAssetReadFunc over the APK's assets/ */
static GBytes *
read_apk_asset (const gchar  *path,
                gpointer      user_data,
                GError      **error)
{
	AAssetManager *manager = user_data;
	AAsset *asset;
	const void *buffer;
	off_t length;
	GBytes *bytes;

	asset = AAssetManager_open (manager, path, AASSET_MODE_BUFFER);
	if (asset == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "The APK has no asset %s", path);
		return NULL;
	}
	length = AAsset_getLength (asset);
	buffer = AAsset_getBuffer (asset);
	if (buffer == NULL)
	{
		AAsset_close (asset);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot read asset %s", path);
		return NULL;
	}
	bytes = g_bytes_new (buffer, (gsize) length);
	AAsset_close (asset);
	return bytes;
}

/* log_writer: GLib structured log -> logcat, tag = the log domain */
static GLogWriterOutput
log_writer (GLogLevelFlags   level,
            const GLogField *fields,
            gsize            n_fields,
            gpointer         user_data)
{
	const gchar *domain = "libregnum";
	const gchar *message = "";
	gsize i;

	for (i = 0; i < n_fields; i++)
	{
		if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0 && fields[i].value != NULL)
			domain = fields[i].value;
		else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0 && fields[i].value != NULL)
			message = fields[i].value;
	}
	__android_log_write (lrg_platform_android_priority (level), domain, message);
	return G_LOG_WRITER_HANDLED;
}

static gboolean
prepare_android (GError **error)
{
	struct android_app *app;
	g_autofree gchar *assets = NULL;
	const gchar *internal;

	/* logging first, so everything after reaches logcat */
	g_log_set_writer_func (log_writer, NULL, NULL);

	app = GetAndroidApp ();
	if (app == NULL || app->activity == NULL || app->activity->internalDataPath == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		             "lrg_platform_prepare() runs before raylib's android_main()");
		return FALSE;
	}
	internal = app->activity->internalDataPath;

	if (!lrg_platform_set_data_root (internal, error))
		return FALSE;

	assets = g_build_filename (internal, "assets", NULL);
	if (!lrg_platform_extract_assets (read_apk_asset, app->activity->assetManager, assets, error))
		return FALSE;
	if (g_chdir (assets) != 0)
	{
		gint saved = errno;

		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved),
		             "Cannot change into %s: %s", assets, g_strerror (saved));
		return FALSE;
	}
	g_info ("Android: data root %s, assets in %s", internal, assets);
	return TRUE;
}

#endif /* LRG_PLATFORM_ANDROID */

/* lrg_platform_prepare: documented in lrg-platform.h */
gboolean
lrg_platform_prepare (GError **error)
{
#ifdef LRG_PLATFORM_ANDROID
	static gsize once = 0;
	static gboolean result = FALSE;
	static GError *first_error = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (g_once_init_enter (&once))
	{
		result = prepare_android (&first_error);
		g_once_init_leave (&once, 1);
	}
	if (!result && first_error != NULL)
		g_propagate_error (error, g_error_copy (first_error));
	return result;
#else
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	return TRUE;
#endif
}
