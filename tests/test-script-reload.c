/* test-script-reload.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hot reload: each built backend, state hand-over, and a syntax error that
 * leaves the previous code running.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

typedef struct
{
	LrgScriptLanguage  language;
	const gchar       *tag;
	gint               good;
	gint               kept_init;
	gint               replaced;
} ReloadCase;

static gchar *scratch_root = NULL;

static void
remove_tree (const gchar *path)
{
	GDir *dir;
	const gchar *name;

	dir = g_dir_open (path, 0, NULL);
	if (dir == NULL)
	{
		g_unlink (path);
		return;
	}
	while ((name = g_dir_read_name (dir)) != NULL)
	{
		gchar *child = g_build_filename (path, name, NULL);

		remove_tree (child);
		g_free (child);
	}
	g_dir_close (dir);
	g_rmdir (path);
}

static gboolean
call_number (LrgScripting *scripting,
             const gchar *name,
             gdouble     *out)
{
	GValue value = G_VALUE_INIT;
	g_autoptr(GError) error = NULL;

	if (!lrg_scripting_call_function (scripting, name, &value, 0, NULL, &error))
		return FALSE;
	if (G_VALUE_HOLDS_DOUBLE (&value))
		*out = g_value_get_double (&value);
	else if (G_VALUE_HOLDS_FLOAT (&value))
		*out = g_value_get_float (&value);
	else if (G_VALUE_HOLDS_INT (&value))
		*out = g_value_get_int (&value);
	else if (G_VALUE_HOLDS_UINT (&value))
		*out = g_value_get_uint (&value);
	else if (G_VALUE_HOLDS_INT64 (&value))
		*out = (gdouble) g_value_get_int64 (&value);
	else if (G_VALUE_HOLDS_UINT64 (&value))
		*out = (gdouble) g_value_get_uint64 (&value);
	else if (G_VALUE_HOLDS_LONG (&value))
		*out = (gdouble) g_value_get_long (&value);
	else
	{
		g_value_unset (&value);
		return FALSE;
	}
	g_value_unset (&value);
	return TRUE;
}

static gchar *
script_body (LrgScriptLanguage language,
             gint              behavior,
             gint              kept)
{
	switch (language)
	{
	case LRG_SCRIPT_LANGUAGE_LUA:
		return g_strdup_printf (
			"function on_before_reload()\n"
			"    return kept\n"
			"end\n"
			"function on_reload(saved)\n"
			"    kept = saved\n"
			"end\n"
			"function behavior()\n"
			"    return %d\n"
			"end\n"
			"function get_kept()\n"
			"    return kept\n"
			"end\n"
			"kept = %d\n",
			behavior, kept);
	case LRG_SCRIPT_LANGUAGE_PYTHON:
		return g_strdup_printf (
			"def on_before_reload():\n"
			"    return kept\n"
			"def on_reload(saved):\n"
			"    global kept\n"
			"    kept = saved\n"
			"def behavior():\n"
			"    return %d\n"
			"def get_kept():\n"
			"    return kept\n"
			"kept = %d\n",
			behavior, kept);
	case LRG_SCRIPT_LANGUAGE_GJS:
		return g_strdup_printf (
			"var kept = %d;\n"
			"function on_before_reload() { return kept; }\n"
			"function on_reload(saved) { kept = saved; }\n"
			"function behavior() { return %d; }\n"
			"function get_kept() { return kept; }\n",
			kept, behavior);
	case LRG_SCRIPT_LANGUAGE_CRISPY:
		return g_strdup_printf (
			"#include <glib-object.h>\n"
			"#include <gmodule.h>\n"
			"static int kept_value = %d;\n"
			"static int behavior_value = %d;\n"
			"G_MODULE_EXPORT gboolean on_before_reload (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
			"{ (void) s; (void) n; (void) a; (void) e;\n"
			"  g_value_init (r, G_TYPE_INT); g_value_set_int (r, kept_value); return TRUE; }\n"
			"G_MODULE_EXPORT gboolean on_reload (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
			"{ (void) s; (void) r; (void) e;\n"
			"  if (n > 0 && G_VALUE_HOLDS_INT (&a[0])) kept_value = g_value_get_int (&a[0]);\n"
			"  else if (n > 0 && G_VALUE_HOLDS_DOUBLE (&a[0])) kept_value = (int) g_value_get_double (&a[0]);\n"
			"  else if (n > 0 && G_VALUE_HOLDS_INT64 (&a[0])) kept_value = (int) g_value_get_int64 (&a[0]);\n"
			"  return TRUE; }\n"
			"G_MODULE_EXPORT gboolean behavior (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
			"{ (void) s; (void) n; (void) a; (void) e;\n"
			"  g_value_init (r, G_TYPE_INT); g_value_set_int (r, behavior_value); return TRUE; }\n"
			"G_MODULE_EXPORT gboolean get_kept (void *s, guint n, const GValue *a, GValue *r, GError **e)\n"
			"{ (void) s; (void) n; (void) a; (void) e;\n"
			"  g_value_init (r, G_TYPE_INT); g_value_set_int (r, kept_value); return TRUE; }\n",
			kept, behavior);
	default:
		return NULL;
	}
}

static const gchar *
script_name (LrgScriptLanguage language)
{
	switch (language)
	{
	case LRG_SCRIPT_LANGUAGE_LUA:
		return "reload.lua";
	case LRG_SCRIPT_LANGUAGE_PYTHON:
		return "reload.py";
	case LRG_SCRIPT_LANGUAGE_GJS:
		return "reload.js";
	case LRG_SCRIPT_LANGUAGE_CRISPY:
		return "reload.c";
	default:
		return "reload.txt";
	}
}

static gchar *
syntax_body (LrgScriptLanguage language)
{
	switch (language)
	{
	case LRG_SCRIPT_LANGUAGE_LUA:
		return g_strdup ("function behavior(\n");
	case LRG_SCRIPT_LANGUAGE_PYTHON:
		return g_strdup ("def behavior(\n");
	case LRG_SCRIPT_LANGUAGE_GJS:
		return g_strdup ("function behavior( {\n");
	case LRG_SCRIPT_LANGUAGE_CRISPY:
		return g_strdup ("this is not c (\n");
	default:
		return g_strdup ("???\n");
	}
}

static void
prepare_context (LrgScripting      *scripting,
                 LrgScriptLanguage  language)
{
#ifdef LRG_HAS_CRISPY
	if (language == LRG_SCRIPT_LANGUAGE_CRISPY)
	{
		gchar *cache = g_build_filename (scratch_root, "crispy-cache", NULL);

		g_mkdir_with_parents (cache, 0700);
		lrg_scripting_crispy_set_cache_dir (LRG_SCRIPTING_CRISPY (scripting), cache);
		g_free (cache);
	}
#else
	(void) scripting;
	(void) language;
#endif
}

static void
test_reload_language (gconstpointer user_data)
{
	const ReloadCase *spec = (const ReloadCase *) user_data;
	LrgScriptingManager *manager;
	g_autoptr(LrgScripting) scripting = NULL;
	g_autoptr(LrgScriptReloader) reloader = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *body = NULL;
	g_autofree gchar *broken = NULL;
	g_autofree gchar *err_file = NULL;
	g_autofree gchar *last = NULL;
	g_autoptr(GError) error = NULL;
	gdouble number = 0;
	gint line = 0;
	guint before;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, spec->language))
	{
		g_test_skip ("backend not built");
		return;
	}

	dir = g_dir_make_tmp ("lrg-reload-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, script_name (spec->language), NULL);

	scripting = lrg_scripting_manager_create_context (manager, spec->language);
	g_assert_nonnull (scripting);
	prepare_context (scripting, spec->language);

	body = script_body (spec->language, spec->good, spec->kept_init);
	g_assert_true (g_file_set_contents (path, body, -1, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_scripting_load_file (scripting, path, &error));
	g_assert_no_error (error);
	g_assert_true (call_number (scripting, "behavior", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->good);
	g_assert_true (call_number (scripting, "get_kept", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->kept_init);

	g_free (body);
	body = script_body (spec->language, spec->replaced, 99);
	g_assert_true (g_file_set_contents (path, body, -1, &error));
	g_assert_no_error (error);

	reloader = lrg_script_reloader_new ();
	g_assert_cmpuint (lrg_script_reloader_get_debounce_ms (reloader), ==, 100);
	lrg_script_reloader_set_debounce_ms (reloader, 0);
	g_assert_false (lrg_script_reloader_get_watch_all (reloader));
	lrg_script_reloader_watch_scripting (reloader, scripting);
	before = lrg_script_reloader_get_reload_count (reloader);
	g_assert_true (lrg_script_reloader_reload_file (reloader, path, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_script_reloader_get_reload_count (reloader), ==, before + 1);
	g_assert_null (lrg_script_reloader_dup_last_error (reloader));

	g_assert_true (call_number (scripting, "behavior", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->replaced);
	g_assert_true (call_number (scripting, "get_kept", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->kept_init);

	broken = syntax_body (spec->language);
	g_assert_true (g_file_set_contents (path, broken, -1, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_script_reloader_reload_file (reloader, path, &error));
	g_assert_nonnull (error);
	g_assert_cmpuint (lrg_script_reloader_get_failure_count (reloader), >=, 1);
	last = lrg_script_reloader_dup_last_error (reloader);
	g_assert_nonnull (last);
	g_assert_nonnull (strstr (last, ":"));
	g_assert_true (lrg_scripting_get_error_location (scripting, &err_file, &line));
	g_assert_nonnull (err_file);
	if (spec->language == LRG_SCRIPT_LANGUAGE_LUA ||
	    spec->language == LRG_SCRIPT_LANGUAGE_PYTHON)
		g_assert_cmpint (line, >, 0);

	g_assert_true (call_number (scripting, "behavior", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->replaced);
	g_assert_true (call_number (scripting, "get_kept", &number));
	g_assert_cmpfloat (number, ==, (gdouble) spec->kept_init);

	remove_tree (dir);
}

static gboolean changed_seen = FALSE;

static void
on_changed (LrgScriptReloader *reloader,
            const gchar       *path,
            gpointer           user_data)
{
	(void) reloader;
	(void) user_data;
	if (path != NULL)
		changed_seen = TRUE;
}

static void
test_reloader_watch_edit (void)
{
	LrgScriptingManager *manager;
	g_autoptr(LrgScripting) scripting = NULL;
	g_autoptr(LrgScriptReloader) reloader = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *body = NULL;
	g_auto(GStrv) watched = NULL;
	g_autoptr(GError) error = NULL;
	LrgScriptLanguage language = LRG_SCRIPT_LANGUAGE_NONE;
	gdouble number = 0;
	guint before;
	gint i;

	manager = lrg_scripting_manager_get_default ();
	if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_LUA))
		language = LRG_SCRIPT_LANGUAGE_LUA;
	else if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_PYTHON))
		language = LRG_SCRIPT_LANGUAGE_PYTHON;
	else if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_GJS))
		language = LRG_SCRIPT_LANGUAGE_GJS;
	else
	{
		g_test_skip ("no file-backed script backend");
		return;
	}

	dir = g_dir_make_tmp ("lrg-reload-watch-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, script_name (language), NULL);
	body = script_body (language, 1, 5);
	g_assert_true (g_file_set_contents (path, body, -1, &error));
	g_assert_no_error (error);

	scripting = lrg_scripting_manager_create_context (manager, language);
	g_assert_true (lrg_scripting_load_file (scripting, path, &error));
	g_assert_no_error (error);

	reloader = lrg_script_reloader_new ();
	lrg_script_reloader_set_debounce_ms (reloader, 0);
	lrg_script_reloader_set_enabled (reloader, TRUE);
	g_assert_true (lrg_script_reloader_get_enabled (reloader));
	changed_seen = FALSE;
	g_signal_connect (reloader, "changed", G_CALLBACK (on_changed), NULL);
	g_assert_true (lrg_script_reloader_watch_path (reloader, dir, &error));
	g_assert_no_error (error);
	lrg_script_reloader_watch_scripting (reloader, scripting);
	watched = lrg_script_reloader_get_watched_paths (reloader);
	g_assert_nonnull (watched);
	g_assert_nonnull (watched[0]);

	g_free (body);
	body = script_body (language, 4, 5);
	g_assert_true (g_file_set_contents (path, body, -1, &error));
	g_assert_no_error (error);

	before = lrg_script_reloader_get_reload_count (reloader);
	for (i = 0; i < 40; i++)
	{
		lrg_script_reloader_process_pending (reloader);
		if (lrg_script_reloader_get_reload_count (reloader) > before)
			break;
		g_usleep (50 * G_TIME_SPAN_MILLISECOND);
	}
	g_assert_cmpuint (lrg_script_reloader_get_reload_count (reloader), >, before);
	g_assert_true (changed_seen);
	g_assert_true (call_number (scripting, "behavior", &number));
	g_assert_cmpfloat (number, ==, 4.0);

	lrg_script_reloader_set_enabled (reloader, FALSE);
	lrg_script_reloader_unwatch_scripting (reloader, scripting);
	lrg_script_reloader_unwatch_path (reloader, dir);
	remove_tree (dir);
}

static void
test_reloader_missing_path (void)
{
	g_autoptr(LrgScriptReloader) reloader = NULL;
	g_autoptr(GError) error = NULL;

	reloader = lrg_script_reloader_new ();
	g_assert_false (lrg_script_reloader_watch_path (reloader,
	                                                "/no/such/lrg-reload-path",
	                                                &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_true (lrg_script_reloader_get_default () != NULL);
}

int
main (int   argc,
      char *argv[])
{
	static const ReloadCase lua_case = { LRG_SCRIPT_LANGUAGE_LUA, "lua", 1, 5, 2 };
	static const ReloadCase python_case = { LRG_SCRIPT_LANGUAGE_PYTHON, "python", 1, 5, 2 };
	static const ReloadCase gjs_case = { LRG_SCRIPT_LANGUAGE_GJS, "gjs", 1, 5, 2 };
	static const ReloadCase crispy_case = { LRG_SCRIPT_LANGUAGE_CRISPY, "crispy", 1, 5, 2 };
	g_autoptr(GError) error = NULL;

	g_test_init (&argc, &argv, NULL);
	scratch_root = g_dir_make_tmp ("lrg-reload-root-XXXXXX", &error);
	g_assert_no_error (error);

	g_test_add_data_func ("/script-reload/lua", &lua_case, test_reload_language);
	g_test_add_data_func ("/script-reload/python", &python_case, test_reload_language);
	g_test_add_data_func ("/script-reload/gjs", &gjs_case, test_reload_language);
	g_test_add_data_func ("/script-reload/crispy", &crispy_case, test_reload_language);
	g_test_add_func ("/script-reload/watch-edit", test_reloader_watch_edit);
	g_test_add_func ("/script-reload/missing-path", test_reloader_missing_path);

	return g_test_run ();
}
