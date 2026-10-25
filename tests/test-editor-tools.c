/* test-editor-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the editor's per-user tooling: LrgEditorSettings persistence and
 * the LrgExternalEditor launcher ($VISUAL/$EDITOR resolution, presets,
 * placeholder expansion, terminal wrapping, Emacs-first routing).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

/* Clears every environment variable the launcher consults. */
static void
clear_env (void)
{
	g_unsetenv ("VISUAL");
	g_unsetenv ("EDITOR");
	g_unsetenv ("TERMINAL");
	g_unsetenv ("NVIM");
}

static void
assert_argv (gchar       **argv,
             const gchar  *expected[])
{
	guint i;

	g_assert_nonnull (argv);
	for (i = 0; expected[i] != NULL; i++)
	{
		g_assert_nonnull (argv[i]);
		g_assert_cmpstr (argv[i], ==, expected[i]);
	}
	g_assert_null (argv[i]);
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

static void
test_settings_defaults (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	gdouble                      scale;
	guint                        autosave;

	g_assert_cmpstr (lrg_editor_settings_get_external_editor (s), ==, "auto");
	g_assert_true (lrg_editor_settings_get_emacs_integration (s));
	g_object_get (s, "ui-scale", &scale, "autosave-interval", &autosave, NULL);
	g_assert_cmpfloat (scale, ==, 1.0);
	g_assert_cmpuint (autosave, ==, 120);
	g_assert_nonnull (lrg_editor_settings_get_recent_projects (s));
	g_assert_null (lrg_editor_settings_get_recent_projects (s)[0]);
}

static void
test_settings_roundtrip (void)
{
	g_autoptr(LrgEditorSettings) a = lrg_editor_settings_new ();
	g_autoptr(LrgEditorSettings) b = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *data = NULL;
	g_autofree gchar            *ed = NULL;
	g_autofree gchar            *cmd = NULL;
	gdouble                      snap;
	gboolean                     emacs;

	g_object_set (a,
	              "external-editor", "custom",
	              "external-editor-command", "my-ed --line {line} \"{file}\"",
	              "emacs-integration", FALSE,
	              "snap-translate", 0.25,
	              NULL);
	lrg_editor_settings_add_recent_project (a, "/tmp/p1");
	lrg_editor_settings_add_recent_project (a, "/tmp/p2");
	lrg_editor_settings_set_keybinding (a, "file.save", "Ctrl+Alt+S");
	lrg_editor_settings_set_keybinding (a, "edit.delete", "");

	data = lrg_editor_settings_to_data (a);
	g_assert_nonnull (strstr (data, "external-editor-command"));
	g_assert_true (lrg_editor_settings_load_from_data (b, data, &error));
	g_assert_no_error (error);

	g_object_get (b, "external-editor", &ed, "external-editor-command", &cmd,
	              "emacs-integration", &emacs, "snap-translate", &snap, NULL);
	g_assert_cmpstr (ed, ==, "custom");
	g_assert_cmpstr (cmd, ==, "my-ed --line {line} \"{file}\"");
	g_assert_false (emacs);
	g_assert_cmpfloat (snap, ==, 0.25);
	g_assert_cmpstr (lrg_editor_settings_get_recent_projects (b)[0], ==, "/tmp/p2");
	g_assert_cmpstr (lrg_editor_settings_get_recent_projects (b)[1], ==, "/tmp/p1");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (b, "file.save"), ==, "Ctrl+Alt+S");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (b, "edit.delete"), ==, "");
	g_assert_null (lrg_editor_settings_get_keybinding (b, "edit.undo"));
}

static void
test_settings_tolerant_load (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *out = NULL;
	gint                         font;
	gdouble                      scale;

	/* Bad values keep their defaults; unknown keys survive a save. */
	g_assert_true (lrg_editor_settings_load_from_data (s,
		"font-size: 900\n"
		"ui-scale: banana\n"
		"theme: latte\n"
		"future-option: [1, 2, 3]\n", &error));
	g_assert_no_error (error);
	g_object_get (s, "font-size", &font, "ui-scale", &scale, NULL);
	g_assert_cmpint (font, ==, 16);
	g_assert_cmpfloat (scale, ==, 1.0);

	out = lrg_editor_settings_to_data (s);
	g_assert_nonnull (strstr (out, "future-option"));
	g_assert_nonnull (strstr (out, "latte"));

	/* Not a mapping is an error. */
	g_assert_false (lrg_editor_settings_load_from_data (s, "- a\n- b\n", &error));
	g_assert_nonnull (error);
}

static void
test_settings_file_and_reset (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgEditorSettings) t = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *dir = g_dir_make_tmp ("lrg-settings-XXXXXX", NULL);
	g_autofree gchar            *path = g_build_filename (dir, "sub", "editor.yaml", NULL);
	g_autofree gchar            *theme = NULL;
	g_autofree gchar            *sub = g_path_get_dirname (path);
	guint                        i;

	/* A missing file is fine. */
	g_assert_true (lrg_editor_settings_load (t, path, &error));
	g_assert_no_error (error);

	g_object_set (s, "theme", "frappe", NULL);
	for (i = 0; i < LRG_EDITOR_SETTINGS_MAX_RECENT + 5; i++)
	{
		g_autofree gchar *lvl = g_strdup_printf ("/l/%u.rlevel", i);

		lrg_editor_settings_add_recent_level (s, lvl);
	}
	lrg_editor_settings_add_recent_level (s, "/l/3.rlevel");
	g_assert_cmpuint (g_strv_length ((gchar **) lrg_editor_settings_get_recent_levels (s)), ==,
	                  LRG_EDITOR_SETTINGS_MAX_RECENT);
	g_assert_cmpstr (lrg_editor_settings_get_recent_levels (s)[0], ==, "/l/3.rlevel");

	g_assert_true (lrg_editor_settings_save (s, path, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_settings_load (t, path, &error));
	g_object_get (t, "theme", &theme, NULL);
	g_assert_cmpstr (theme, ==, "frappe");

	lrg_editor_settings_reset (t);
	g_clear_pointer (&theme, g_free);
	g_object_get (t, "theme", &theme, NULL);
	g_assert_cmpstr (theme, ==, "mocha");

	g_unlink (path);
	g_rmdir (sub);
	g_rmdir (dir);
}

/* ==========================================================================
 * External editor
 * ========================================================================== */

static void
test_presets_lookup (void)
{
	const gchar * const *ids = lrg_external_editor_list_presets ();

	g_assert_cmpstr (ids[0], ==, "auto");
	g_assert_cmpstr (lrg_external_editor_preset_for_command ("emacsclient_tty"), ==, "emacsclient");
	g_assert_cmpstr (lrg_external_editor_preset_for_command ("/usr/bin/nvim"), ==, "nvim");
	g_assert_cmpstr (lrg_external_editor_preset_for_command ("vi"), ==, "vim");
	g_assert_cmpstr (lrg_external_editor_preset_for_command ("hx"), ==, "helix");
	g_assert_null (lrg_external_editor_preset_for_command ("notepad"));
	g_assert_true (lrg_external_editor_preset_is_terminal ("nano"));
	g_assert_false (lrg_external_editor_preset_is_terminal ("codium"));
	g_assert_true (lrg_external_editor_preset_is_emacs ("emacs"));
	g_assert_nonnull (lrg_external_editor_preset_get_label ("kate"));
	g_assert_null (lrg_external_editor_preset_get_template ("auto"));
}

static void
test_expand_template (void)
{
	g_auto(GStrv)     a = NULL;
	g_auto(GStrv)     b = NULL;
	g_autoptr(GError) error = NULL;
	const gchar      *exp_a[] = { "codium", "--goto", "/my dir/a b.lua:4:1", NULL };
	const gchar      *exp_b[] = { "ed", "-x", "/p", "/f.c", NULL };

	/* Spaces survive; a bare {project} vanishes without a project;
	 * column defaults to 1. */
	a = lrg_external_editor_expand_template ("codium {project} --goto {file}:{line}:{col}",
	                                         "/my dir/a b.lua", 4, 0, NULL, &error);
	g_assert_no_error (error);
	assert_argv (a, exp_a);

	/* No {file}: appended. */
	b = lrg_external_editor_expand_template ("ed -x {project}", "/f.c", 1, 1, "/p", &error);
	assert_argv (b, exp_b);

	/* Unbalanced quotes are an error. */
	g_assert_null (lrg_external_editor_expand_template ("ed \"{file}", "/f", 1, 1, NULL, &error));
	g_assert_nonnull (error);
}

static void
test_build_argv_presets (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgExternalEditor) ext = lrg_external_editor_new (s);
	g_autoptr(GError)            error = NULL;
	g_auto(GStrv)                a = NULL;
	g_auto(GStrv)                b = NULL;
	g_auto(GStrv)                c = NULL;
	const gchar                 *exp_a[] = { "emacsclient", "-n", "-r", "+3:7", "/x/y.lua", NULL };
	const gchar                 *exp_b[] = { "emacsclient", "-s", "work", "-n", "-r", "+3:7", "/x/y.lua", NULL };
	const gchar                 *exp_c[] = { "gst", "-e", "nvim", "+call cursor(3,7)", "/x/y.lua", NULL };

	clear_env ();
	g_object_set (s, "external-editor", "emacsclient", NULL);
	a = lrg_external_editor_build_argv (ext, "/x/y.lua", 3, 7, &error);
	g_assert_no_error (error);
	assert_argv (a, exp_a);

	g_object_set (s, "emacs-server-name", "work", NULL);
	b = lrg_external_editor_build_argv (ext, "/x/y.lua", 3, 7, &error);
	assert_argv (b, exp_b);

	g_object_set (s, "external-editor", "nvim", "terminal-command", "gst", NULL);
	c = lrg_external_editor_build_argv (ext, "/x/y.lua", 3, 7, &error);
	assert_argv (c, exp_c);
}

static void
test_build_argv_environment (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgExternalEditor) ext = lrg_external_editor_new (s);
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *source = NULL;
	g_autofree gchar            *preset = NULL;

	clear_env ();

	/* Nothing set: the desktop default handler. */
	preset = lrg_external_editor_resolve_preset (ext, &source);
	g_assert_cmpstr (preset, ==, "xdg-open");
	g_assert_cmpstr (source, ==, "desktop default");

	/* $EDITOR wrapper keeps the user's program, gains +LINE:COL. */
	g_setenv ("EDITOR", "emacsclient_tty", TRUE);
	{
		g_auto(GStrv) argv = lrg_external_editor_build_argv (ext, "/a.c", 10, 2, &error);
		const gchar  *exp[] = { "emacsclient_tty", "+10:2", "/a.c", NULL };

		assert_argv (argv, exp);
	}

	/* $VISUAL wins over $EDITOR. */
	g_setenv ("VISUAL", "kate", TRUE);
	{
		g_auto(GStrv) argv = lrg_external_editor_build_argv (ext, "/a.c", 10, 2, &error);
		const gchar  *exp[] = { "kate", "/a.c", "--line", "10", "--column", "2", NULL };

		assert_argv (argv, exp);
	}
	g_unsetenv ("VISUAL");

	/* Unknown $EDITOR programs are terminal editors (foot takes no flag). */
	g_setenv ("EDITOR", "my-ed --fancy", TRUE);
	g_object_set (s, "terminal-command", "foot", NULL);
	{
		g_auto(GStrv) argv = lrg_external_editor_build_argv (ext, "/a.c", 1, 1, &error);
		const gchar  *exp[] = { "foot", "my-ed", "--fancy", "/a.c", NULL };

		assert_argv (argv, exp);
	}

	/* ...unless the user says never. */
	g_object_set (s, "editor-in-terminal", "never", NULL);
	{
		g_auto(GStrv) argv = lrg_external_editor_build_argv (ext, "/a.c", 1, 1, &error);
		const gchar  *exp[] = { "my-ed", "--fancy", "/a.c", NULL };

		assert_argv (argv, exp);
	}
	g_object_set (s, "editor-in-terminal", "auto", NULL);

	/* Inside Neovim: --remote-send to that instance, escaped path. */
	g_setenv ("EDITOR", "nvim", TRUE);
	g_setenv ("NVIM", "/run/user/1000/nvim.123.0", TRUE);
	{
		g_auto(GStrv) argv = lrg_external_editor_build_argv (ext, "/my dir/a.c", 5, 3, &error);
		const gchar  *exp[] = { "nvim", "--server", "/run/user/1000/nvim.123.0", "--remote-send",
		                        "<C-\\><C-N>:e /my\\ dir/a.c<CR>:call cursor(5,3)<CR>", NULL };

		assert_argv (argv, exp);
	}

	clear_env ();
}

static void
test_build_argv_custom_and_errors (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgExternalEditor) ext = lrg_external_editor_new (s);
	g_autoptr(GError)            error = NULL;
	g_auto(GStrv)                argv = NULL;
	const gchar                 *exp[] = { "my-ed", "--at", "4:9", "/proj", "/proj/s.lua", NULL };

	clear_env ();
	lrg_external_editor_set_project_dir (ext, "/proj");
	g_object_set (s, "external-editor", "custom", NULL);
	g_assert_null (lrg_external_editor_build_argv (ext, "/proj/s.lua", 4, 9, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);

	g_object_set (s, "external-editor-command", "my-ed --at {line}:{col} {project} {file}", NULL);
	argv = lrg_external_editor_build_argv (ext, "/proj/s.lua", 4, 9, &error);
	g_assert_no_error (error);
	assert_argv (argv, exp);
}

static gboolean
handle_open (LrgExternalEditor *ext,
             const gchar       *file,
             gint               line,
             gint               column,
             gpointer           user_data)
{
	gchar **seen = user_data;

	*seen = g_strdup_printf ("%s:%d:%d", file, line, column);
	return TRUE;
}

static void
on_launched (LrgExternalEditor *ext,
             const gchar       *command,
             gpointer           user_data)
{
	*(gchar **) user_data = g_strdup (command);
}

static void
test_open_emacs_first_and_spawn (void)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgExternalEditor) ext = lrg_external_editor_new (s);
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *seen = NULL;
	g_autofree gchar            *launched = NULL;
	gulong                       id;

	clear_env ();
	g_object_set (s, "external-editor", "emacsclient", NULL);
	id = g_signal_connect (ext, "open-file", G_CALLBACK (handle_open), &seen);
	g_assert_true (lrg_external_editor_open (ext, "/tmp/z.lua", 2, 5, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (seen, ==, "/tmp/z.lua:2:5");

	/* Not routed when integration is off or the editor is not Emacs: a
	 * real (harmless) process is spawned instead. */
	g_signal_handler_disconnect (ext, id);
	g_clear_pointer (&seen, g_free);
	g_object_set (s, "external-editor", "custom", "external-editor-command", "true {file}", NULL);
	g_signal_connect (ext, "open-file", G_CALLBACK (handle_open), &seen);
	g_signal_connect (ext, "launched", G_CALLBACK (on_launched), &launched);
	g_assert_true (lrg_external_editor_open (ext, "/tmp/z.lua", 1, 1, &error));
	g_assert_no_error (error);
	g_assert_null (seen);
	g_assert_cmpstr (launched, ==, "true /tmp/z.lua");

	/* Let the child be reaped. */
	{
		gint64 deadline = g_get_monotonic_time () + G_USEC_PER_SEC / 5;

		while (g_get_monotonic_time () < deadline)
			g_main_context_iteration (NULL, FALSE);
	}

	/* A missing program reports an error. */
	g_object_set (s, "external-editor-command", "/nonexistent/editor-xyz {file}", NULL);
	g_assert_false (lrg_external_editor_open (ext, "/tmp/z.lua", 1, 1, &error));
	g_assert_nonnull (error);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-tools/settings/defaults", test_settings_defaults);
	g_test_add_func ("/editor-tools/settings/roundtrip", test_settings_roundtrip);
	g_test_add_func ("/editor-tools/settings/tolerant-load", test_settings_tolerant_load);
	g_test_add_func ("/editor-tools/settings/file-reset", test_settings_file_and_reset);

	g_test_add_func ("/editor-tools/external/presets", test_presets_lookup);
	g_test_add_func ("/editor-tools/external/expand", test_expand_template);
	g_test_add_func ("/editor-tools/external/argv-presets", test_build_argv_presets);
	g_test_add_func ("/editor-tools/external/argv-environment", test_build_argv_environment);
	g_test_add_func ("/editor-tools/external/argv-custom", test_build_argv_custom_and_errors);
	g_test_add_func ("/editor-tools/external/open", test_open_emacs_first_and_spawn);

	return g_test_run ();
}
