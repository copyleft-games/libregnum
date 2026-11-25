/* test-platform.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Platform preparation (lrg-platform.h) on any host: the data root,
 * asset extraction from a manifest (against a directory-backed reader
 * standing in for an APK), path safety, the logcat priority mapping, and
 * lrg_platform_prepare() being a no-op off Android.
 */

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libregnum.h>
#include "core/lrg-platform-private.h"

/* ==========================================================================
 * A fake APK: files in a directory, read whole
 * ========================================================================== */

static GBytes *
read_from_dir (const gchar  *path,
               gpointer      user_data,
               GError      **error)
{
	g_autofree gchar *full = g_build_filename ((const gchar *) user_data, path, NULL);
	gchar *contents = NULL;
	gsize length = 0;

	if (!g_file_get_contents (full, &contents, &length, error))
		return NULL;
	return g_bytes_new_take (contents, length);
}

/* write_file: <dir>/<path> = text, creating parents */
static void
write_file (const gchar *dir,
            const gchar *path,
            const gchar *text)
{
	g_autofree gchar *full = g_build_filename (dir, path, NULL);
	g_autofree gchar *parent = g_path_get_dirname (full);

	g_assert_cmpint (g_mkdir_with_parents (parent, 0700), ==, 0);
	g_assert_true (g_file_set_contents (full, text, -1, NULL));
}

/* read_text: <dir>/<path>, or NULL when it does not exist */
static gchar *
read_text (const gchar *dir,
           const gchar *path)
{
	g_autofree gchar *full = g_build_filename (dir, path, NULL);
	gchar *contents = NULL;

	if (!g_file_get_contents (full, &contents, NULL, NULL))
		return NULL;
	return contents;
}

typedef struct
{
	gchar *root;    /* scratch directory */
	gchar *apk;     /* the fake APK's assets/ */
	gchar *dest;    /* where assets are extracted */
} Fixture;

static void
fixture_set_up (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(GError) error = NULL;

	f->root = g_dir_make_tmp ("lrg-platform-XXXXXX", &error);
	g_assert_no_error (error);
	f->apk = g_build_filename (f->root, "apk", NULL);
	f->dest = g_build_filename (f->root, "internal", "assets", NULL);
	g_assert_cmpint (g_mkdir_with_parents (f->apk, 0700), ==, 0);
}

/* remove_all: test cleanup (mirrors what the library does) */
static void
remove_all (const gchar *path)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *name;

	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		dir = g_dir_open (path, 0, NULL);
		while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			remove_all (child);
		}
		g_rmdir (path);
	}
	else
		g_unlink (path);
}

static void
fixture_tear_down (Fixture       *f,
                   gconstpointer  data)
{
	remove_all (f->root);
	g_free (f->root);
	g_free (f->apk);
	g_free (f->dest);
}

/* ==========================================================================
 * Extraction
 * ========================================================================== */

/* the first extraction writes every listed file, nested ones included */
static void
test_extract_first (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	g_autofree gchar *stamp = NULL;
	g_autofree gchar *unlisted = NULL;

	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST,
	            "# lrg-assets v1\nlevels/one.yaml\n\n# a comment\ntextures/ui/button.png\n");
	write_file (f->apk, "levels/one.yaml", "level: one\n");
	write_file (f->apk, "textures/ui/button.png", "PNG1");
	write_file (f->apk, "not-listed.txt", "skipped");

	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);

	a = read_text (f->dest, "levels/one.yaml");
	b = read_text (f->dest, "textures/ui/button.png");
	stamp = read_text (f->dest, ".lrg-assets-stamp");
	g_assert_cmpstr (a, ==, "level: one\n");
	g_assert_cmpstr (b, ==, "PNG1");
	g_assert_cmpstr (stamp, ==, "v1");
	unlisted = read_text (f->dest, "not-listed.txt");
	g_assert_null (unlisted);
}

/* an unchanged stamp copies nothing (local edits survive) */
static void
test_extract_same_stamp (Fixture       *f,
                         gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;

	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, "# lrg-assets v1\nfile.txt\n");
	write_file (f->apk, "file.txt", "original");
	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);

	write_file (f->dest, "file.txt", "edited in place");
	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);
	text = read_text (f->dest, "file.txt");
	g_assert_cmpstr (text, ==, "edited in place");
}

/* a new stamp replaces the tree: new contents, dropped files gone */
static void
test_extract_new_stamp (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *kept = NULL;
	g_autofree gchar *gone = NULL;
	g_autofree gchar *added = NULL;
	g_autofree gchar *staging = NULL;
	g_autofree gchar *retired = NULL;

	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, "# lrg-assets v1\nkept.txt\nold.txt\n");
	write_file (f->apk, "kept.txt", "v1");
	write_file (f->apk, "old.txt", "dropped in v2");
	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);

	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, "# lrg-assets v2\nkept.txt\nnew/added.txt\n");
	write_file (f->apk, "kept.txt", "v2");
	write_file (f->apk, "new/added.txt", "added in v2");
	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);

	kept = read_text (f->dest, "kept.txt");
	gone = read_text (f->dest, "old.txt");
	added = read_text (f->dest, "new/added.txt");
	g_assert_cmpstr (kept, ==, "v2");
	g_assert_null (gone);
	g_assert_cmpstr (added, ==, "added in v2");

	/* no staging or retired copy is left behind */
	staging = g_strconcat (f->dest, ".new", NULL);
	retired = g_strconcat (f->dest, ".old", NULL);
	g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));
	g_assert_false (g_file_test (retired, G_FILE_TEST_EXISTS));
}

/* a staging directory left by an interrupted run does not leak into the
 * next extraction */
static void
test_extract_interrupted (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *staging = NULL;
	g_autofree gchar *stale = NULL;

	staging = g_strconcat (f->dest, ".new", NULL);
	write_file (staging, "half-written.txt", "from a killed process");
	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, "# lrg-assets v1\nfile.txt\n");
	write_file (f->apk, "file.txt", "complete");

	g_assert_true (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_no_error (error);
	stale = read_text (f->dest, "half-written.txt");
	g_assert_null (stale);
}

/* every unsafe manifest path is refused before anything is written */
static void
test_extract_unsafe (Fixture       *f,
                     gconstpointer  data)
{
	static const gchar *const unsafe[] = {
		"../escape.txt", "a/../../escape.txt", "/etc/passwd", "a//b.txt",
		"./here.txt", "a/./b.txt", "a\\b.txt", NULL
	};
	guint i;

	for (i = 0; unsafe[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;
		g_autofree gchar *manifest = g_strdup_printf ("# lrg-assets v1\nok.txt\n%s\n", unsafe[i]);

		write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, manifest);
		write_file (f->apk, "ok.txt", "fine");
		g_assert_false (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
		g_assert_false (g_file_test (f->dest, G_FILE_TEST_EXISTS));
	}
}

/* a manifest without the magic line, or without a stamp, is refused */
static void
test_extract_bad_manifest (Fixture       *f,
                           gconstpointer  data)
{
	static const gchar *const bad[] = { "file.txt\n", "# lrg-assets \nfile.txt\n", "", NULL };
	guint i;

	write_file (f->apk, "file.txt", "x");
	for (i = 0; bad[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;

		write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, bad[i]);
		g_assert_false (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	}
}

/* no manifest, or a listed file missing: the reader's error comes back */
static void
test_extract_missing (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;

	g_assert_false (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error));
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);

	write_file (f->apk, LRG_PLATFORM_ASSET_MANIFEST, "# lrg-assets v1\nmissing.txt\n");
	g_assert_false (lrg_platform_extract_assets (read_from_dir, f->apk, f->dest, &error2));
	g_assert_error (error2, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	/* nothing half-extracted took the destination's name */
	g_assert_false (g_file_test (f->dest, G_FILE_TEST_EXISTS));
}

/* ==========================================================================
 * Data root, path safety, log priorities, prepare
 * ========================================================================== */

static void
test_data_root (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *root = g_build_filename (f->root, "home", NULL);
	static const gchar *const vars[][2] = {
		{ "HOME", "" }, { "XDG_DATA_HOME", "data" }, { "XDG_CONFIG_HOME", "config" },
		{ "XDG_CACHE_HOME", "cache" }, { "TMPDIR", "tmp" }
	};
	gchar *saved[G_N_ELEMENTS (vars)];
	guint i;

	for (i = 0; i < G_N_ELEMENTS (vars); i++)
		saved[i] = g_strdup (g_getenv (vars[i][0]));

	g_assert_true (lrg_platform_set_data_root (root, &error));
	g_assert_no_error (error);
	for (i = 0; i < G_N_ELEMENTS (vars); i++)
	{
		g_autofree gchar *expected = vars[i][1][0] == '\0'
			? g_strdup (root) : g_build_filename (root, vars[i][1], NULL);

		g_assert_cmpstr (g_getenv (vars[i][0]), ==, expected);
		g_assert_true (g_file_test (expected, G_FILE_TEST_IS_DIR));
	}

	/* put the environment back for the rest of the binary */
	for (i = 0; i < G_N_ELEMENTS (vars); i++)
	{
		if (saved[i] != NULL)
			g_setenv (vars[i][0], saved[i], TRUE);
		else
			g_unsetenv (vars[i][0]);
		g_free (saved[i]);
	}
}

static void
test_data_root_unwritable (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *saved = g_strdup (g_getenv ("HOME"));

	/* a regular file where the root should be */
	g_assert_false (lrg_platform_set_data_root ("/dev/null/lrg", &error));
	g_assert_nonnull (error);
	g_assert_true (error->domain == G_IO_ERROR);
	g_assert_cmpstr (g_getenv ("HOME"), ==, saved);
}

static void
test_path_safety (void)
{
	g_assert_true (lrg_platform_asset_path_is_safe ("a.txt"));
	g_assert_true (lrg_platform_asset_path_is_safe ("dir/sub/a.txt"));
	g_assert_true (lrg_platform_asset_path_is_safe ("..hidden/a.txt"));
	g_assert_true (lrg_platform_asset_path_is_safe ("a..b"));
	g_assert_false (lrg_platform_asset_path_is_safe (NULL));
	g_assert_false (lrg_platform_asset_path_is_safe (""));
	g_assert_false (lrg_platform_asset_path_is_safe ("/abs"));
	g_assert_false (lrg_platform_asset_path_is_safe (".."));
	g_assert_false (lrg_platform_asset_path_is_safe ("a/.."));
	g_assert_false (lrg_platform_asset_path_is_safe ("./a"));
	g_assert_false (lrg_platform_asset_path_is_safe ("a//b"));
	g_assert_false (lrg_platform_asset_path_is_safe ("a/"));
	g_assert_false (lrg_platform_asset_path_is_safe ("a\\b"));
}

static void
test_log_priorities (void)
{
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL), ==, LRG_ANDROID_LOG_FATAL);
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_CRITICAL), ==, LRG_ANDROID_LOG_ERROR);
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_WARNING), ==, LRG_ANDROID_LOG_WARN);
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_MESSAGE), ==, LRG_ANDROID_LOG_INFO);
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_INFO), ==, LRG_ANDROID_LOG_INFO);
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_DEBUG), ==, LRG_ANDROID_LOG_DEBUG);
	g_assert_cmpint (lrg_platform_android_priority (0), ==, LRG_ANDROID_LOG_VERBOSE);
	/* the most severe bit wins */
	g_assert_cmpint (lrg_platform_android_priority (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_DEBUG), ==, LRG_ANDROID_LOG_WARN);
}

static void
test_prepare_off_android (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *home = g_strdup (g_getenv ("HOME"));
	g_autofree gchar *cwd = g_get_current_dir ();
	g_autofree gchar *after = NULL;

	g_assert_true (lrg_platform_prepare (&error));
	g_assert_no_error (error);
	/* and twice */
	g_assert_true (lrg_platform_prepare (NULL));
	/* nothing about the process changed */
	after = g_get_current_dir ();
	g_assert_cmpstr (g_getenv ("HOME"), ==, home);
	g_assert_cmpstr (after, ==, cwd);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/platform/extract/first", Fixture, NULL, fixture_set_up, test_extract_first, fixture_tear_down);
	g_test_add ("/platform/extract/same-stamp", Fixture, NULL, fixture_set_up, test_extract_same_stamp, fixture_tear_down);
	g_test_add ("/platform/extract/new-stamp", Fixture, NULL, fixture_set_up, test_extract_new_stamp, fixture_tear_down);
	g_test_add ("/platform/extract/interrupted", Fixture, NULL, fixture_set_up, test_extract_interrupted, fixture_tear_down);
	g_test_add ("/platform/extract/unsafe-paths", Fixture, NULL, fixture_set_up, test_extract_unsafe, fixture_tear_down);
	g_test_add ("/platform/extract/bad-manifest", Fixture, NULL, fixture_set_up, test_extract_bad_manifest, fixture_tear_down);
	g_test_add ("/platform/extract/missing", Fixture, NULL, fixture_set_up, test_extract_missing, fixture_tear_down);
	g_test_add ("/platform/data-root/set", Fixture, NULL, fixture_set_up, test_data_root, fixture_tear_down);
	g_test_add_func ("/platform/data-root/unwritable", test_data_root_unwritable);
	g_test_add_func ("/platform/asset-path-safety", test_path_safety);
	g_test_add_func ("/platform/android-log-priority", test_log_priorities);
	g_test_add_func ("/platform/prepare/off-android", test_prepare_off_android);

	return g_test_run ();
}
