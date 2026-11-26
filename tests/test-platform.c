/* test-platform.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Platform preparation (lrg-platform.h) on any host: the data root,
 * asset extraction from a manifest (against a directory-backed reader
 * standing in for an APK), path safety, the logcat priority mapping, and
 * lrg_platform_prepare() being a no-op off Android; the desktop side of
 * the external data directory, density, soft keyboard and character
 * queue, and the lifecycle signals through the simulation hook.
 */

#include <errno.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libregnum.h>
#include <raylib.h>
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

/* ==========================================================================
 * External storage, density, soft keyboard, characters (desktop)
 * ========================================================================== */

static void
test_external_dir_desktop (void)
{
	const gchar *dir;
	g_autofree gchar *name = g_path_get_basename (g_get_prgname ());
	g_autofree gchar *expected = NULL;

	/* the user data dir is isolated per test (G_TEST_OPTION_ISOLATE_DIRS);
	 * the program name is argv[0], a path here: its last component */
	g_assert_cmpstr (name, ==, "test-platform");
	expected = g_build_filename (g_get_user_data_dir (), name, NULL);
	g_assert_false (g_file_test (expected, G_FILE_TEST_IS_DIR));

	dir = lrg_platform_get_external_data_dir ();
	g_assert_nonnull (dir);
	g_assert_cmpstr (dir, ==, expected);
	g_assert_true (g_file_test (dir, G_FILE_TEST_IS_DIR));

	/* the same answer is the same (interned) string */
	g_assert_true (lrg_platform_get_external_data_dir () == dir);
}

static void
test_external_dir_unwritable (void)
{
	g_autofree gchar *name = g_path_get_basename (g_get_prgname ());
	g_autofree gchar *blocker = NULL;

	/* a file where the program's directory would go: no directory, NULL */
	blocker = g_build_filename (g_get_user_data_dir (), name, NULL);
	g_assert_cmpint (g_mkdir_with_parents (g_get_user_data_dir (), 0700), ==, 0);
	g_assert_true (g_file_set_contents (blocker, "not a directory", -1, NULL));

	g_assert_null (lrg_platform_get_external_data_dir ());
}

static void
test_dp_scale_desktop (void)
{
	g_assert_cmpfloat (lrg_platform_get_dp_scale (), ==, 1.0f);
}

static void
test_soft_keyboard_desktop (void)
{
	/* no keyboard to show; the request is still remembered */
	lrg_platform_hide_soft_keyboard ();
	g_assert_false (lrg_platform_is_soft_keyboard_requested ());
	lrg_platform_show_soft_keyboard ();
	g_assert_true (lrg_platform_is_soft_keyboard_requested ());
	lrg_platform_show_soft_keyboard ();
	g_assert_true (lrg_platform_is_soft_keyboard_requested ());
	lrg_platform_hide_soft_keyboard ();
	g_assert_false (lrg_platform_is_soft_keyboard_requested ());
}

static void
test_char_pressed_desktop (void)
{
	/* no window, no typed characters: raylib's empty queue */
	g_assert_cmpuint (lrg_platform_get_char_pressed (), ==, 0);
	g_assert_cmpuint (lrg_platform_get_char_pressed (), ==, 0);
}

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

typedef struct
{
	guint    paused;
	guint    resumed;
	guint    notified;
	gboolean last_lost;
} LifecycleLog;

static void
on_paused (LrgPlatform *platform,
           gpointer     user_data)
{
	LifecycleLog *log = user_data;

	/* the state is already the new one when the signal runs */
	g_assert_false (lrg_platform_is_running (platform));
	log->paused++;
}

static void
on_resumed (LrgPlatform *platform,
            gboolean     gl_context_lost,
            gpointer     user_data)
{
	LifecycleLog *log = user_data;

	g_assert_true (lrg_platform_is_running (platform));
	log->resumed++;
	log->last_lost = gl_context_lost;
}

static void
on_running_notify (GObject    *object,
                   GParamSpec *pspec,
                   gpointer    user_data)
{
	LifecycleLog *log = user_data;

	log->notified++;
}

static void
test_lifecycle_simulated (void)
{
	LrgPlatform *platform = lrg_platform_get_default ();
	LifecycleLog log = { 0, 0, 0, FALSE };
	gulong id_paused, id_resumed, id_notify;
	gboolean running = FALSE;

	g_assert_true (LRG_IS_PLATFORM (platform));
	g_assert_true (lrg_platform_get_default () == platform);
	g_assert_true (lrg_platform_is_running (platform));

	/* the per-frame poll does nothing here and emits nothing */
	id_paused = g_signal_connect (platform, "paused", G_CALLBACK (on_paused), &log);
	id_resumed = g_signal_connect (platform, "resumed", G_CALLBACK (on_resumed), &log);
	id_notify = g_signal_connect (platform, "notify::running", G_CALLBACK (on_running_notify), &log);
	lrg_platform_poll ();
	g_assert_cmpuint (log.paused + log.resumed + log.notified, ==, 0);

	/* resuming a running app emits nothing */
	lrg_platform_simulate_resume (platform, TRUE);
	g_assert_cmpuint (log.resumed, ==, 0);

	/* pause once; a second pause is no change */
	lrg_platform_simulate_pause (platform);
	lrg_platform_simulate_pause (platform);
	g_assert_cmpuint (log.paused, ==, 1);
	g_assert_cmpuint (log.notified, ==, 1);
	g_object_get (platform, "running", &running, NULL);
	g_assert_false (running);

	/* back, the context kept */
	lrg_platform_simulate_resume (platform, FALSE);
	g_assert_cmpuint (log.resumed, ==, 1);
	g_assert_false (log.last_lost);

	/* again, the context lost this time */
	lrg_platform_simulate_pause (platform);
	lrg_platform_simulate_resume (platform, TRUE);
	g_assert_cmpuint (log.paused, ==, 2);
	g_assert_cmpuint (log.resumed, ==, 2);
	g_assert_true (log.last_lost);
	g_assert_cmpuint (log.notified, ==, 4);
	g_assert_true (lrg_platform_is_running (platform));

	g_signal_handler_disconnect (platform, id_paused);
	g_signal_handler_disconnect (platform, id_resumed);
	g_signal_handler_disconnect (platform, id_notify);
}

/* the display's size is Android's to give: off Android, FALSE and the
 * outputs untouched (NULL outputs allowed) */
static void
test_display_size_desktop (void)
{
	gint width = -7;
	gint height = -9;

	g_assert_false (lrg_platform_get_display_size (&width, &height));
	g_assert_cmpint (width, ==, -7);
	g_assert_cmpint (height, ==, -9);
	g_assert_false (lrg_platform_get_display_size (NULL, NULL));
}

/* the session's runtime directory (the Wayland socket's), saved before
 * G_TEST_OPTION_ISOLATE_DIRS points XDG_RUNTIME_DIR at a scratch one */
static gchar *session_runtime_dir = NULL;

/* no display to open a window on is a skip, not a fatal warning */
static gboolean
ignore_no_display (const gchar    *log_domain,
                   GLogLevelFlags  log_level,
                   const gchar    *message,
                   gpointer        user_data)
{
	return message == NULL || strstr (message, "could not initialize") == NULL;
}

/* window-resized: what it carries, how often it comes */
typedef struct
{
	guint count;
	gint  width;
	gint  height;
} ResizeLog;

static void
on_window_resized (LrgPlatform *platform,
                   gint         width,
                   gint         height,
                   gpointer     user_data)
{
	ResizeLog *log = user_data;

	/* raylib already reports the new size when the signal runs */
	g_assert_cmpint (GetScreenWidth (), ==, width);
	g_assert_cmpint (GetScreenHeight (), ==, height);
	log->count++;
	log->width = width;
	log->height = height;
}

/* no window: nothing to resize, nothing emitted; bad sizes refused */
static void
test_window_size_no_window (void)
{
	LrgPlatform *platform = lrg_platform_get_default ();
	ResizeLog log = { 0, 0, 0 };
	gulong id;

	if (IsWindowReady ())
	{
		g_test_skip ("A window is open");
		return;
	}
	id = g_signal_connect (platform, "window-resized", G_CALLBACK (on_window_resized), &log);
	g_assert_false (lrg_platform_apply_window_size (platform, 1280, 720));
	g_assert_false (lrg_platform_apply_window_size (platform, 0, 720));
	g_assert_false (lrg_platform_apply_window_size (platform, 1280, -1));
	lrg_platform_poll ();
	g_assert_cmpuint (log.count, ==, 0);
	g_signal_handler_disconnect (platform, id);
}

/* a window (hidden; skips without a display): a new size reaches raylib
 * and is emitted once, the same size again is no change, and a frame
 * later raylib no longer reports a resize */
static void
test_window_size_apply (void)
{
	g_autoptr(GrlWindow) window = NULL;
	LrgPlatform *platform = lrg_platform_get_default ();
	ResizeLog log = { 0, 0, 0 };
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");
	gulong id;

	if ((display == NULL || display[0] == '\0') &&
	    (wayland == NULL || wayland[0] == '\0'))
	{
		g_test_skip ("No display / GL context available");
		return;
	}
	if (session_runtime_dir != NULL)
		g_setenv ("XDG_RUNTIME_DIR", session_runtime_dir, TRUE);
	g_test_log_set_fatal_handler (ignore_no_display, NULL);
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	window = grl_window_new (64, 48, "lrg-platform-test");
	g_test_log_set_fatal_handler (NULL, NULL);
	if (window == NULL || !grl_window_is_ready (window))
	{
		g_test_skip ("No display / GL context available");
		return;
	}

	id = g_signal_connect (platform, "window-resized", G_CALLBACK (on_window_resized), &log);
	g_assert_false (lrg_platform_apply_window_size (platform, 64, 48));
	g_assert_cmpuint (log.count, ==, 0);

	/* a phone unfolded: landscape cover screen to the inner screen */
	g_test_expect_message (NULL, G_LOG_LEVEL_MESSAGE, "The window is now 220x200");
	g_assert_true (lrg_platform_apply_window_size (platform, 220, 200));
	g_test_assert_expected_messages ();
	g_assert_cmpuint (log.count, ==, 1);
	g_assert_cmpint (log.width, ==, 220);
	g_assert_cmpint (log.height, ==, 200);
	g_assert_cmpint (GetRenderWidth (), ==, 220);
	g_assert_true (IsWindowResized ());
	g_assert_false (lrg_platform_apply_window_size (platform, 220, 200));
	g_assert_cmpuint (log.count, ==, 1);

	g_signal_handler_disconnect (platform, id);
}

int
main (int    argc,
      char **argv)
{
	session_runtime_dir = g_strdup (g_getenv ("XDG_RUNTIME_DIR"));
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

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
	g_test_add_func ("/platform/external-dir/desktop", test_external_dir_desktop);
	g_test_add_func ("/platform/external-dir/unwritable", test_external_dir_unwritable);
	g_test_add_func ("/platform/dp-scale/desktop", test_dp_scale_desktop);
	g_test_add_func ("/platform/display-size/desktop", test_display_size_desktop);
	g_test_add_func ("/platform/soft-keyboard/desktop", test_soft_keyboard_desktop);
	g_test_add_func ("/platform/char-pressed/desktop", test_char_pressed_desktop);
	g_test_add_func ("/platform/lifecycle/simulated", test_lifecycle_simulated);
	g_test_add_func ("/platform/window-size/no-window", test_window_size_no_window);
	g_test_add_func ("/platform/window-size/apply", test_window_size_apply);

	return g_test_run ();
}
