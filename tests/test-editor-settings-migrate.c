/* test-editor-settings-migrate.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the 0.8.0 -> 0.8.1 editor settings migration
 * (lrg-editor-settings-migrate.h): the legacy dock layout grammar, the
 * keybinding and theme conversions, the one-time backup, idempotency,
 * key preservation, the viewport-cameras key and the theme files shipped
 * in data/editor/themes.
 *
 * Everything is headless: settings files live in a g_dir_make_tmp()
 * directory, and converted layouts are checked by loading them into a
 * real LrgDockArea (no canvas needed) and comparing
 * lrg_dock_area_describe().
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <unistd.h>
#include <libregnum.h>
#include <yaml-glib.h>

/* ==========================================================================
 * Fixtures: editor.yaml files as 0.8.0 wrote them
 *
 * 0.8.0's LrgEditorSettings wrote every property it had (no
 * settings-version, ui-layout, script-editor, viewport-layout), the
 * keybinding overrides as a mapping in "Ctrl+S" syntax, and kept keys it
 * did not know (here a plugin's mapping and a scalar).
 * ========================================================================== */

#define LEGACY_LAYOUT \
	"split(v,0.180,tabs(0,hierarchy),split(v,0.730,split(h,0.700,tabs(0,viewport)," \
	"tabs(1,console,assets,problems,timeline)),tabs(0,inspector,history,tiles)))"

#define LEGACY_LAYOUT_SHAPE \
	"H(0.18,[hierarchy*],H(0.73,V(0.70,[viewport*],[console,assets*,problems,timeline])," \
	"[inspector*,history,tiles]))"

static const gchar *fixture_full =
	"external-editor: emacsclient\n"
	"external-editor-command: null\n"
	"terminal-command: gst -e\n"
	"editor-in-terminal: auto\n"
	"emacs-integration: true\n"
	"emacs-server-name: null\n"
	"theme: macchiato\n"
	"ui-scale: 1.25\n"
	"font-size: 18\n"
	"font-path: null\n"
	"autosave-interval: 120\n"
	"history-limit: 1000\n"
	"reload-on-external-change: true\n"
	"confirm-on-close: true\n"
	"restore-last-session: true\n"
	"snap-enabled: true\n"
	"snap-translate: 0.5\n"
	"snap-rotate-degrees: 15\n"
	"snap-scale: 0.1\n"
	"grid-visible: true\n"
	"grid-size: 1\n"
	"grid-extent: 50\n"
	"camera-fly-speed: 8\n"
	"camera-orbit-speed: 0.35\n"
	"camera-invert-y: false\n"
	"camera-navigation: blender\n"
	"camera-fov: 60\n"
	"server-enabled: true\n"
	"server-socket: null\n"
	"recent-projects:\n"
	"- /home/me/games/dungeon\n"
	"- /home/me/games/space\n"
	"recent-levels:\n"
	"- /home/me/games/dungeon/levels/start.rlevel\n"
	"layout: " LEGACY_LAYOUT "\n"
	"keybindings:\n"
	"  edit.redo: Ctrl+Shift+Z|Ctrl+Y\n"
	"  edit.select-all: ''\n"
	"  file.quit: <Ctrl>q\n"
	"  file.save: Ctrl+S\n"
	"  play.stop: Shift+F5|Wibble+Wobble\n"
	"  tool.bogus: Hyper+Banana\n"
	"  view.2d: Ctrl+Shift+2\n"
	"  view.top: Num7\n"
	"  edit.preferences: Ctrl+,\n"
	"plugin-foo:\n"
	"  enabled: true\n"
	"  level: 3\n"
	"x-custom-note: kept by 0.8.0\n";

/* A minimal 0.8.0 file: just a theme (and so no settings-version) */
#define FIXTURE_THEME_FMT "theme: %s\nui-scale: 1.5\n"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

typedef struct
{
	gchar *dir;
	gchar *path;
} Fixture;

static void
fixture_setup (Fixture       *fx,
               gconstpointer  user_data)
{
	g_autoptr(GError) error = NULL;

	fx->dir = g_dir_make_tmp ("lrg-settings-migrate-XXXXXX", &error);
	g_assert_no_error (error);
	fx->path = g_build_filename (fx->dir, "editor.yaml", NULL);
}

/* rm_rf: removes a directory tree made by a test */
static void
rm_rf (const gchar *path)
{
	GDir        *dir;
	const gchar *name;

	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		dir = g_dir_open (path, 0, NULL);
		if (dir != NULL)
		{
			while ((name = g_dir_read_name (dir)) != NULL)
			{
				g_autofree gchar *child = g_build_filename (path, name, NULL);

				rm_rf (child);
			}
			g_dir_close (dir);
		}
		g_rmdir (path);
		return;
	}
	g_unlink (path);
}

static void
fixture_teardown (Fixture       *fx,
                  gconstpointer  user_data)
{
	rm_rf (fx->dir);
	g_free (fx->dir);
	g_free (fx->path);
}

static void
write_file (const gchar *path,
            const gchar *text)
{
	g_autoptr(GError) error = NULL;

	g_file_set_contents (path, text, -1, &error);
	g_assert_no_error (error);
}

static gchar *
read_file (const gchar *path)
{
	g_autoptr(GError) error = NULL;
	gchar            *text = NULL;

	g_file_get_contents (path, &text, NULL, &error);
	g_assert_no_error (error);
	return text;
}

static gchar *
backup_path (const gchar *path)
{
	return g_strconcat (path, LRG_EDITOR_SETTINGS_BACKUP_SUFFIX, NULL);
}

/* load_settings: settings read from @path */
static LrgEditorSettings *
load_settings (const gchar *path)
{
	g_autoptr(GError)  error = NULL;
	LrgEditorSettings *s = lrg_editor_settings_new ();

	g_assert_true (lrg_editor_settings_load (s, path, &error));
	g_assert_no_error (error);
	return s;
}

/* Log capture: every message logged while installed */
static void
capture_log (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        user_data)
{
	g_ptr_array_add ((GPtrArray *) user_data, g_strdup (message));
}

static gboolean
log_has (GPtrArray   *log,
         const gchar *a,
         const gchar *b)
{
	guint i;

	for (i = 0; i < log->len; i++)
	{
		const gchar *m = g_ptr_array_index (log, i);

		if (strstr (m, a) != NULL && (b == NULL || strstr (m, b) != NULL))
			return TRUE;
	}
	return FALSE;
}

/* The nine 0.8.0 editor panels */
static const gchar * const panel_ids[] = {
	"viewport", "hierarchy", "inspector", "assets", "console",
	"problems", "history", "tiles", "timeline", NULL
};

/*
 * layout_shape:
 *
 * Loads dock YAML into an LrgDockArea holding exactly the panels @ids
 * and returns lrg_dock_area_describe().
 */
static gchar *
layout_shape (const gchar         *yaml,
              const gchar * const *ids)
{
	g_autoptr(GError) error = NULL;
	LrgDockArea      *area = lrg_dock_area_new ();
	gchar            *shape;
	guint             i;

	for (i = 0; ids[i] != NULL; i++)
	{
		LrgDockPanel *p = lrg_dock_panel_new (ids[i], ids[i]);

		lrg_dock_area_add_panel (area, p, NULL, LRG_DOCK_POSITION_CENTER);
		g_object_unref (p);
	}
	g_assert_true (lrg_dock_area_load_layout (area, yaml, &error));
	g_assert_no_error (error);
	shape = lrg_dock_area_describe (area);
	g_object_unref (area);
	return shape;
}

/*
 * shipped_theme_dir:
 *
 * data/editor/themes of the source tree: $G_TEST_SRCDIR/.. under
 * "make test", else the working directory (tests run from the root).
 */
static gchar *
shipped_theme_dir (void)
{
	const gchar *srcdir = g_getenv ("G_TEST_SRCDIR");
	gchar       *dir;

	if (srcdir != NULL)
	{
		dir = g_build_filename (srcdir, "..", "data", "editor", "themes", NULL);
		if (g_file_test (dir, G_FILE_TEST_IS_DIR))
			return dir;
		g_free (dir);
	}
	dir = g_build_filename (g_get_current_dir (), "data", "editor", "themes", NULL);
	if (g_file_test (dir, G_FILE_TEST_IS_DIR))
		return dir;
	g_free (dir);
	return NULL;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
test_layout_convert (void)
{
	static const struct
	{
		const gchar *legacy;
		const gchar *shape;
	} cases[] = {
		/* A single stack */
		{ "tabs(0,viewport)", "[viewport*]" },
		/* The example from lrg-gui-dock.h: v is side by side (H), h stacked (V) */
		{ "split(v,0.20,tabs(0,hierarchy),split(h,0.72,tabs(0,viewport),tabs(1,console,assets)))",
		  "H(0.20,[hierarchy*],V(0.72,[viewport*],[console,assets*]))" },
		/* A 0.8.0 default-style layout with every panel */
		{ LEGACY_LAYOUT, LEGACY_LAYOUT_SHAPE },
		/* Nested splits on both sides, blanks between tokens */
		{ " split( h , 0.6 ,\n split(v,0.3,tabs(2,hierarchy,history,tiles),tabs(0,viewport)),"
		  "split(v,0.5,tabs(0,console),split(h,0.25,tabs(0,inspector),tabs(0,assets,problems,timeline)))) ",
		  "V(0.60,H(0.30,[hierarchy,history,tiles*],[viewport*]),H(0.50,[console*],"
		  "V(0.25,[inspector*],[assets*,problems,timeline])))" },
		/* Out-of-range ratio and active index are clamped like 0.8.0 */
		{ "split(v,1.5,tabs(9,viewport,inspector),tabs(-3,hierarchy))",
		  "H(0.95,[viewport,inspector*],[hierarchy*])" },
		{ "split(h,0.0,tabs(0,viewport),tabs(0,console))", "V(0.05,[viewport*],[console*])" },
		/* An empty stack collapses its split when loaded */
		{ "split(v,0.5,tabs(0),tabs(0,viewport))", "[viewport*]" },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(GError)    error = NULL;
		g_autofree gchar    *yaml = NULL;
		g_autofree gchar    *shape = NULL;
		g_autoptr(GPtrArray) ids = g_ptr_array_new ();
		guint                j;

		yaml = lrg_editor_settings_convert_legacy_layout (cases[i].legacy, &error);
		g_assert_no_error (error);
		g_assert_nonnull (yaml);

		/* Only the panels the layout names, so none is appended */
		for (j = 0; panel_ids[j] != NULL; j++)
			if (strstr (cases[i].shape, panel_ids[j]) != NULL)
				g_ptr_array_add (ids, (gpointer) panel_ids[j]);
		g_ptr_array_add (ids, NULL);

		shape = layout_shape (yaml, (const gchar * const *) ids->pdata);
		g_assert_cmpstr (shape, ==, cases[i].shape);
	}
}

static void
test_layout_yaml_shape (void)
{
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *yaml = NULL;
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	YamlMapping          *top;
	YamlMapping          *root;
	YamlMapping          *first;
	YamlNode             *floating;

	/* The document has the shape lrg_dock_area_save_layout() writes */
	yaml = lrg_editor_settings_convert_legacy_layout (
		"split(v,0.25,tabs(1,hierarchy,history),tabs(0,viewport))", &error);
	g_assert_no_error (error);
	g_assert_true (yaml_parser_load_from_data (parser, yaml, -1, &error));
	g_assert_no_error (error);

	top = yaml_node_get_mapping (yaml_parser_get_root (parser));
	g_assert_cmpint (yaml_mapping_get_int_member (top, "version"), ==, 1);
	floating = yaml_mapping_get_member (top, "floating");
	g_assert_nonnull (floating);
	g_assert_cmpint (yaml_node_get_node_type (floating), ==, YAML_NODE_SEQUENCE);
	g_assert_cmpuint (yaml_sequence_get_length (yaml_node_get_sequence (floating)), ==, 0);

	root = yaml_mapping_get_mapping_member (top, "root");
	g_assert_cmpstr (yaml_mapping_get_string_member (root, "split"), ==, "horizontal");
	g_assert_cmpfloat_with_epsilon (yaml_mapping_get_double_member (root, "ratio"), 0.25, 1e-9);
	first = yaml_mapping_get_mapping_member (root, "first");
	g_assert_cmpstr (yaml_mapping_get_string_member (first, "active"), ==, "history");
	g_assert_cmpuint (yaml_sequence_get_length (yaml_mapping_get_sequence_member (first, "tabs")), ==, 2);
}

static void
test_layout_invalid (void)
{
	static const gchar * const bad[] = {
		"",
		"   ",
		"grid(1,viewport)",
		"split(x,0.5,tabs(0,viewport),tabs(0,console))",
		"split(v,0.5,tabs(0,viewport))",
		"split(v,0.5,tabs(0,viewport),tabs(0,console)",
		"tabs(0,viewport",
		"tabs(0,)",
		"tabs(0,viewport) tabs(0,console)",
		"tabs(0,view port)",
		"split(v;0.5,tabs(0,viewport),tabs(0,console))",
		NULL
	};
	guint i;

	for (i = 0; bad[i] != NULL; i++)
	{
		g_autoptr(GError) error = NULL;
		g_autofree gchar *yaml = lrg_editor_settings_convert_legacy_layout (bad[i], &error);

		g_assert_null (yaml);
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	}
}

/* nested: @n splits nested in their first child */
static gchar *
nested (guint n)
{
	GString *s = g_string_new (NULL);
	guint    i;

	for (i = 0; i < n; i++)
		g_string_append (s, "split(v,0.5,");
	g_string_append (s, "tabs(0,viewport)");
	for (i = 0; i < n; i++)
		g_string_append (s, ",tabs(0,console))");
	return g_string_free (s, FALSE);
}

static void
test_layout_depth_limit (void)
{
	g_autofree gchar *ok = nested (64);
	g_autofree gchar *deep = nested (65);
	g_autofree gchar *yaml = NULL;
	g_autofree gchar *none = NULL;
	g_autoptr(GError) error = NULL;

	/* 0.8.0 accepted 64 levels and refused 65; so does the converter */
	yaml = lrg_editor_settings_convert_legacy_layout (ok, &error);
	g_assert_no_error (error);
	g_assert_nonnull (yaml);
	none = lrg_editor_settings_convert_legacy_layout (deep, &error);
	g_assert_null (none);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

/* ==========================================================================
 * Keybindings and theme names
 * ========================================================================== */

static void
test_shortcut_convert (void)
{
	static const struct
	{
		const gchar *in;
		const gchar *out;
	} cases[] = {
		{ "Ctrl+S", "<Ctrl>s" },
		{ "Ctrl+Shift+Z|Ctrl+Y", "<Ctrl><Shift>z|<Ctrl>y" },
		{ "Ctrl+Shift+2", "<Ctrl><Shift>2" },
		{ "Num7", "KP_7" },
		{ "Ctrl+,", "<Ctrl>comma" },
		{ "Alt+Up", "<Alt>Up" },
		{ "<Ctrl>q", "<Ctrl>q" },                    /* already accelerators */
		{ "<Ctrl><Shift>z|<Ctrl>y", "<Ctrl><Shift>z|<Ctrl>y" },
		{ "KP_7", "KP_7" },
		{ "Shift+F5|Wibble+Wobble", "<Shift>F5" },   /* invalid alternative dropped */
		{ "Bogus|Ctrl+S|Nope", "<Ctrl>s" },
		{ "", "" },                                  /* unbinding kept */
		{ "Hyper+Banana", NULL },                    /* nothing valid */
		{ "Ctrl+", NULL },
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autofree gchar *got = lrg_editor_settings_convert_legacy_shortcut (cases[i].in);

		g_assert_cmpstr (got, ==, cases[i].out);
	}
}

static void
test_theme_convert (void)
{
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("mocha"), ==, "catppuccin-mocha");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("latte"), ==, "catppuccin-latte");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("macchiato"), ==, "catppuccin-macchiato");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("frappe"), ==, "catppuccin-frappe");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("Mocha"), ==, "catppuccin-mocha");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("high-contrast"), ==, "high-contrast");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("/x/dusk.yaml"), ==, "/x/dusk.yaml");
	g_assert_cmpstr (lrg_editor_settings_convert_legacy_theme ("something"), ==, "something");
	g_assert_null (lrg_editor_settings_convert_legacy_theme (NULL));
}

/* ==========================================================================
 * Whole-file migration
 * ========================================================================== */

static void
test_migrate_full (Fixture       *fx,
                   gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(LrgEditorSettings) again = NULL;
	g_autoptr(GError)            error = NULL;
	g_autoptr(GPtrArray)         log = g_ptr_array_new_with_free_func (g_free);
	g_autofree gchar            *bak = backup_path (fx->path);
	g_autofree gchar            *bak_text = NULL;
	g_autofree gchar            *saved = NULL;
	g_autofree gchar            *layout = NULL;
	g_autofree gchar            *shape = NULL;
	g_autofree gchar            *theme = NULL;
	g_auto(GStrv)                recents = NULL;
	gdouble                      scale = 0.0;
	gint                         font_size = 0;
	GLogFunc                     old_handler;
	gpointer                     old_data = NULL;

	write_file (fx->path, fixture_full);
	s = load_settings (fx->path);
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, 1);
	g_assert_true (lrg_editor_settings_needs_migration (s));
	g_assert_cmpstr (lrg_editor_settings_get_path (s), ==, fx->path);

	/* Migrate to the loaded path, capturing the log */
	old_handler = g_log_set_default_handler (capture_log, log);
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_log_set_default_handler (old_handler, old_data);
	g_assert_no_error (error);

	/* Version bump */
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, LRG_EDITOR_SETTINGS_VERSION);
	g_assert_false (lrg_editor_settings_needs_migration (s));

	/* Backup: the original bytes */
	g_assert_true (g_file_test (bak, G_FILE_TEST_IS_REGULAR));
	bak_text = read_file (bak);
	g_assert_cmpstr (bak_text, ==, fixture_full);

	/* Layout: converted into ui-layout, the old key untouched */
	g_object_get (s, "layout", &layout, NULL);
	g_assert_cmpstr (layout, ==, LEGACY_LAYOUT);
	g_assert_nonnull (lrg_editor_settings_get_ui_layout (s));
	shape = layout_shape (lrg_editor_settings_get_ui_layout (s), panel_ids);
	g_assert_cmpstr (shape, ==, LEGACY_LAYOUT_SHAPE);

	/* Keybindings */
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "file.save"), ==, "<Ctrl>s");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "edit.redo"), ==, "<Ctrl><Shift>z|<Ctrl>y");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "view.top"), ==, "KP_7");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "view.2d"), ==, "<Ctrl><Shift>2");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "edit.preferences"), ==, "<Ctrl>comma");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "file.quit"), ==, "<Ctrl>q");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "play.stop"), ==, "<Shift>F5");
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "edit.select-all"), ==, "");
	g_assert_null (lrg_editor_settings_get_keybinding (s, "tool.bogus"));
	g_assert_true (log_has (log, "tool.bogus", "Hyper+Banana"));
	g_assert_true (log_has (log, "play.stop", NULL));
	g_assert_true (log_has (log, "backed up", NULL));

	/* Theme */
	g_object_get (s, "theme", &theme, "ui-scale", &scale, "font-size", &font_size,
	              "recent-projects", &recents, NULL);
	g_assert_cmpstr (theme, ==, "catppuccin-macchiato");

	/* Everything else as it was */
	g_assert_cmpfloat (scale, ==, 1.25);
	g_assert_cmpint (font_size, ==, 18);
	g_assert_cmpuint (g_strv_length (recents), ==, 2);
	g_assert_cmpstr (recents[0], ==, "/home/me/games/dungeon");
	g_assert_cmpstr (lrg_editor_settings_get_external_editor (s), ==, "emacsclient");

	/* The saved file: version, new key, unknown keys preserved */
	saved = read_file (fx->path);
	g_assert_nonnull (strstr (saved, "settings-version: 2"));
	g_assert_nonnull (strstr (saved, "ui-layout:"));
	g_assert_nonnull (strstr (saved, "plugin-foo:"));
	g_assert_nonnull (strstr (saved, "level: 3"));
	g_assert_nonnull (strstr (saved, "x-custom-note: kept by 0.8.0"));

	/* Read back: the same values, already current */
	again = load_settings (fx->path);
	g_assert_cmpint (lrg_editor_settings_get_settings_version (again), ==, 2);
	g_assert_false (lrg_editor_settings_needs_migration (again));
	g_assert_cmpstr (lrg_editor_settings_get_ui_layout (again), ==, lrg_editor_settings_get_ui_layout (s));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (again, "edit.redo"), ==, "<Ctrl><Shift>z|<Ctrl>y");
	g_assert_null (lrg_editor_settings_get_keybinding (again, "tool.bogus"));
}

static void
test_migrate_idempotent (Fixture       *fx,
                         gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(LrgEditorSettings) reread = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *bak = backup_path (fx->path);
	g_autofree gchar            *first = NULL;
	g_autofree gchar            *second = NULL;
	g_autofree gchar            *third = NULL;
	g_autofree gchar            *bak_text = NULL;

	write_file (fx->path, fixture_full);
	s = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_assert_no_error (error);
	first = read_file (fx->path);

	/* The same object again: nothing to do, nothing written */
	g_unlink (fx->path);
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_assert_no_error (error);
	g_assert_false (g_file_test (fx->path, G_FILE_TEST_EXISTS));
	write_file (fx->path, first);

	/* A fresh load of the migrated file: still nothing to do */
	reread = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_migrate (reread, NULL, &error));
	g_assert_no_error (error);
	second = read_file (fx->path);
	g_assert_cmpstr (second, ==, first);

	/* And a migrated file saved again round-trips byte for byte */
	g_assert_true (lrg_editor_settings_save (reread, NULL, &error));
	g_assert_no_error (error);
	third = read_file (fx->path);
	g_assert_cmpstr (third, ==, first);

	/* The backup is still the 0.8.0 file */
	bak_text = read_file (bak);
	g_assert_cmpstr (bak_text, ==, fixture_full);
}

static void
test_migrate_backup_once (Fixture       *fx,
                          gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) a = NULL;
	g_autoptr(LrgEditorSettings) b = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *bak = backup_path (fx->path);
	g_autofree gchar            *bak_text = NULL;
	g_autofree gchar            *other = g_strdup_printf (FIXTURE_THEME_FMT, "latte");

	/* First migration: the backup holds the original */
	write_file (fx->path, fixture_full);
	a = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_migrate (a, NULL, &error));
	g_assert_no_error (error);

	/* A downgrade wrote a 0.8.0 file again; migrating it keeps the
	 * first backup instead of overwriting it */
	write_file (fx->path, other);
	b = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_needs_migration (b));
	g_assert_true (lrg_editor_settings_migrate (b, NULL, &error));
	g_assert_no_error (error);
	bak_text = read_file (bak);
	g_assert_cmpstr (bak_text, ==, fixture_full);
}

static void
test_migrate_no_file (Fixture       *fx,
                      gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *bak = backup_path (fx->path);
	g_autofree gchar            *theme = NULL;

	/* Settings from data, never on disk: no backup, just the new file */
	g_assert_true (lrg_editor_settings_load_from_data (s, "theme: latte\n", &error));
	g_assert_no_error (error);
	g_assert_null (lrg_editor_settings_get_path (s));
	g_assert_true (lrg_editor_settings_migrate (s, fx->path, &error));
	g_assert_no_error (error);
	g_assert_false (g_file_test (bak, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (fx->path, G_FILE_TEST_IS_REGULAR));
	g_object_get (s, "theme", &theme, NULL);
	g_assert_cmpstr (theme, ==, "catppuccin-latte");
}

static void
test_migrate_current_untouched (Fixture       *fx,
                                gconstpointer  user_data)
{
	static const gchar *current =
		"# hand-written 0.8.1 file\n"
		"settings-version: 2\n"
		"theme: mocha\n"
		"layout: tabs(0,viewport)\n"
		"keybindings:\n"
		"  file.save: Ctrl+S\n";
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *bak = backup_path (fx->path);
	g_autofree gchar            *text = NULL;

	/* A version-2 file is not rewritten (comments survive), not backed
	 * up, and its values are not converted */
	write_file (fx->path, current);
	s = load_settings (fx->path);
	g_assert_false (lrg_editor_settings_needs_migration (s));
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_assert_no_error (error);
	text = read_file (fx->path);
	g_assert_cmpstr (text, ==, current);
	g_assert_false (g_file_test (bak, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "file.save"), ==, "Ctrl+S");
	g_assert_null (lrg_editor_settings_get_ui_layout (s));
}

static void
test_migrate_bad_layout (Fixture       *fx,
                         gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(GError)            error = NULL;
	g_autoptr(GPtrArray)         log = g_ptr_array_new_with_free_func (g_free);
	g_autofree gchar            *layout = NULL;
	GLogFunc                     old_handler;

	/* An unparseable layout: ui-layout stays empty, the old key stays,
	 * the rest still migrates */
	write_file (fx->path, "layout: split(v,0.5,tabs(0,viewport)\nkeybindings:\n  file.save: Ctrl+S\n");
	s = load_settings (fx->path);
	old_handler = g_log_set_default_handler (capture_log, log);
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_log_set_default_handler (old_handler, NULL);
	g_assert_no_error (error);

	g_assert_true (lrg_editor_settings_get_ui_layout (s) == NULL ||
	               *lrg_editor_settings_get_ui_layout (s) == '\0');
	g_object_get (s, "layout", &layout, NULL);
	g_assert_cmpstr (layout, ==, "split(v,0.5,tabs(0,viewport)");
	g_assert_true (log_has (log, "layout", "default layout"));
	g_assert_cmpstr (lrg_editor_settings_get_keybinding (s, "file.save"), ==, "<Ctrl>s");
	g_assert_cmpint (lrg_editor_settings_get_settings_version (s), ==, 2);
}

static void
test_migrate_existing_ui_layout (Fixture       *fx,
                                 gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(GError)            error = NULL;

	/* A ui-layout already present (e.g. written by a 0.8.1 build, then the
	 * version key lost) is never replaced by the converted old layout */
	write_file (fx->path, "layout: tabs(0,viewport)\nui-layout: 'version: 1'\n");
	s = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_needs_migration (s));
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_settings_get_ui_layout (s), ==, "version: 1");
}

static void
test_migrate_symlink (Fixture       *fx,
                      gconstpointer  user_data)
{
	g_autoptr(LrgEditorSettings) s = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *target = g_build_filename (fx->dir, "dotfiles-editor.yaml", NULL);
	g_autofree gchar            *text = NULL;

	/* A Stow-style symlinked editor.yaml stays a symlink: the migrated
	 * settings are written through to its target */
	write_file (target, fixture_full);
	g_assert_cmpint (symlink (target, fx->path), ==, 0);
	s = load_settings (fx->path);
	g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (fx->path, G_FILE_TEST_IS_SYMLINK));
	text = read_file (target);
	g_assert_nonnull (strstr (text, "settings-version: 2"));
}

static void
test_migrate_themes (Fixture       *fx,
                     gconstpointer  user_data)
{
	static const struct
	{
		const gchar *before;
		const gchar *after;
	} cases[] = {
		{ "mocha", "catppuccin-mocha" },
		{ "latte", "catppuccin-latte" },
		{ "macchiato", "catppuccin-macchiato" },
		{ "frappe", "catppuccin-frappe" },
		{ "catppuccin-mocha", "catppuccin-mocha" },      /* preset passthrough */
		{ "high-contrast", "high-contrast" },
		{ "/home/me/themes/dusk.yaml", "/home/me/themes/dusk.yaml" },  /* path passthrough */
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autoptr(LrgEditorSettings) s = NULL;
		g_autoptr(GError)            error = NULL;
		g_autofree gchar            *text = g_strdup_printf (FIXTURE_THEME_FMT, cases[i].before);
		g_autofree gchar            *bak = backup_path (fx->path);
		g_autofree gchar            *theme = NULL;
		gdouble                      scale = 0.0;

		g_unlink (bak);
		write_file (fx->path, text);
		s = load_settings (fx->path);
		g_assert_true (lrg_editor_settings_needs_migration (s));
		g_assert_true (lrg_editor_settings_migrate (s, NULL, &error));
		g_assert_no_error (error);
		g_object_get (s, "theme", &theme, "ui-scale", &scale, NULL);
		g_assert_cmpstr (theme, ==, cases[i].after);
		g_assert_cmpfloat (scale, ==, 1.5);  /* ui-scale untouched */
	}
}

/* ==========================================================================
 * Theme lookup and the shipped theme files
 * ========================================================================== */

static void
test_theme_lookup (Fixture       *fx,
                   gconstpointer  user_data)
{
	g_autofree gchar             *dir = shipped_theme_dir ();
	g_autofree gchar             *expected = NULL;
	g_autofree gchar             *found = NULL;
	g_autofree gchar             *with_ext = NULL;
	g_autofree gchar             *user_theme = NULL;
	g_autofree gchar             *user_found = NULL;
	g_autofree gchar             *search = NULL;
	g_autoptr(LrgEditorSettings)  s = lrg_editor_settings_new ();

	if (dir == NULL)
	{
		g_test_skip ("data/editor/themes not found (run from the source tree)");
		return;
	}

	/* $LRG_EDITOR_THEME_PATH: a list; the first match wins */
	user_theme = g_build_filename (fx->dir, "dusk.yaml", NULL);
	write_file (user_theme, "name: dusk\n");
	search = g_strjoin (G_SEARCHPATH_SEPARATOR_S, fx->dir, dir, NULL);
	g_setenv ("LRG_EDITOR_THEME_PATH", search, TRUE);

	expected = g_build_filename (dir, "catppuccin-macchiato.yaml", NULL);
	found = lrg_editor_settings_find_theme_file ("catppuccin-macchiato");
	g_assert_cmpstr (found, ==, expected);
	with_ext = lrg_editor_settings_find_theme_file ("catppuccin-macchiato.yaml");
	g_assert_cmpstr (with_ext, ==, expected);
	user_found = lrg_editor_settings_find_theme_file ("dusk");
	g_assert_cmpstr (user_found, ==, user_theme);

	/* Only bare names */
	g_assert_null (lrg_editor_settings_find_theme_file (""));
	g_assert_null (lrg_editor_settings_find_theme_file (".."));
	g_assert_null (lrg_editor_settings_find_theme_file ("../themes/catppuccin-frappe"));
	g_assert_null (lrg_editor_settings_find_theme_file ("no-such-theme-anywhere"));

	/* The settings resolve a migrated name to the shipped file */
	g_object_set (s, "theme", "catppuccin-frappe", NULL);
	g_assert_null (lrg_editor_settings_get_theme_preset (s));
	g_assert_nonnull (lrg_editor_settings_get_theme_file (s));
	g_assert_true (g_str_has_suffix (lrg_editor_settings_get_theme_file (s), "catppuccin-frappe.yaml"));

	/* Presets and legacy names never look at files */
	g_object_set (s, "theme", "macchiato", NULL);
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (s), ==, "catppuccin-mocha");
	g_assert_null (lrg_editor_settings_get_theme_file (s));
	g_object_set (s, "theme", "catppuccin-latte", NULL);
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (s), ==, "catppuccin-latte");
	g_assert_null (lrg_editor_settings_get_theme_file (s));

	/* A name without a file falls back to the default preset */
	g_object_set (s, "theme", "no-such-theme-anywhere", NULL);
	g_assert_cmpstr (lrg_editor_settings_get_theme_preset (s), ==, "catppuccin-mocha");
	g_assert_null (lrg_editor_settings_get_theme_file (s));

	/* Without the variable an uninstalled build still finds its themes
	 * (walking up from the test binary to data/editor/themes) */
	g_unsetenv ("LRG_EDITOR_THEME_PATH");
	{
		g_autofree gchar *tree = lrg_editor_settings_find_theme_file ("catppuccin-frappe");

		g_assert_nonnull (tree);
		g_assert_true (g_str_has_suffix (tree, "catppuccin-frappe.yaml"));
	}
}

/* check_color: @role of @theme is #rrggbb @hex */
static void
check_color (LrgTheme      *theme,
             LrgThemeColor  role,
             guint32        hex)
{
	const GrlColor *c = lrg_theme_get_color (theme, role);

	g_assert_cmpuint (c->r, ==, (hex >> 16) & 0xff);
	g_assert_cmpuint (c->g, ==, (hex >> 8) & 0xff);
	g_assert_cmpuint (c->b, ==, hex & 0xff);
}

static void
test_shipped_themes_load (void)
{
	static const struct
	{
		const gchar *name;
		guint32      window;   /* crust */
		guint32      panel;    /* base */
		guint32      text;
		guint32      accent;   /* mauve */
		guint32      link;     /* blue */
	} themes[] = {
		{ "catppuccin-macchiato", 0x181926, 0x24273a, 0xcad3f5, 0xc6a0f6, 0x8aadf4 },
		{ "catppuccin-frappe", 0x232634, 0x303446, 0xc6d0f5, 0xca9ee6, 0x8caaee },
	};
	g_autofree gchar *dir = shipped_theme_dir ();
	guint             i;

	if (dir == NULL)
	{
		g_test_skip ("data/editor/themes not found (run from the source tree)");
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (themes); i++)
	{
		g_autofree gchar   *file = g_strconcat (themes[i].name, ".yaml", NULL);
		g_autofree gchar   *path = g_build_filename (dir, file, NULL);
		g_autoptr(GError)   error = NULL;
		g_autoptr(LrgTheme) theme = lrg_theme_new_from_file (path, &error);
		g_autoptr(LrgTheme) mocha = lrg_theme_new_preset ("catppuccin-mocha");

		g_assert_no_error (error);
		g_assert_nonnull (theme);
		g_assert_cmpstr (lrg_theme_get_name (theme), ==, themes[i].name);
		g_assert_true (lrg_theme_get_dark (theme));
		g_assert_cmpint (lrg_theme_get_motion (theme), ==, lrg_theme_get_motion (mocha));

		check_color (theme, LRG_THEME_COLOR_WINDOW, themes[i].window);
		check_color (theme, LRG_THEME_COLOR_PANEL, themes[i].panel);
		check_color (theme, LRG_THEME_COLOR_TEXT, themes[i].text);
		check_color (theme, LRG_THEME_COLOR_ACCENT, themes[i].accent);
		check_color (theme, LRG_THEME_COLOR_FOCUS, themes[i].accent);
		check_color (theme, LRG_THEME_COLOR_LINK, themes[i].link);

		/* Same metrics as the mocha preset they are modelled on */
		g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS), ==,
		                   lrg_theme_get_metric (mocha, LRG_THEME_METRIC_CORNER_RADIUS));
		g_assert_cmpfloat (lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS), ==,
		                   lrg_theme_get_metric (mocha, LRG_THEME_METRIC_POPUP_RADIUS));
	}
}

/* ==========================================================================
 * viewport-cameras
 * ========================================================================== */

static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
	(*(guint *) user_data)++;
}

static void
test_viewport_cameras_key (void)
{
	static const gchar *cams = "0,5,10,0,0,0,50,10,0,0,-1,0;1,2,3,4,5,6,50,10,1,0,0,0";
	g_autoptr(LrgEditorSettings) s = lrg_editor_settings_new ();
	g_autoptr(LrgEditorSettings) back = lrg_editor_settings_new ();
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *data = NULL;
	guint                        n = 0;

	/* Default "", explicit notify only on a change */
	g_assert_cmpstr (lrg_editor_settings_get_viewport_cameras (s), ==, "");
	g_assert_nonnull (g_object_class_find_property (G_OBJECT_GET_CLASS (s), "viewport-cameras"));
	g_signal_connect (s, "notify::viewport-cameras", G_CALLBACK (count_notify), &n);
	g_object_set (s, "viewport-cameras", cams, NULL);
	g_object_set (s, "viewport-cameras", cams, NULL);
	g_assert_cmpuint (n, ==, 1);

	/* Persisted and read back */
	data = lrg_editor_settings_to_data (s);
	g_assert_nonnull (strstr (data, "viewport-cameras:"));
	g_assert_true (lrg_editor_settings_load_from_data (back, data, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_cameras (back), ==, cams);

	/* Reset restores the default; NULL stores "" */
	lrg_editor_settings_reset (back);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_cameras (back), ==, "");
	g_object_set (s, "viewport-cameras", NULL, NULL);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_cameras (s), ==, "");
	g_assert_cmpuint (n, ==, 2);

	/* An empty value round-trips through the file as "" */
	g_free (data);
	data = lrg_editor_settings_to_data (s);
	g_assert_true (lrg_editor_settings_load_from_data (back, data, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_settings_get_viewport_cameras (back), ==, "");
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/settings-migrate/layout/convert", test_layout_convert);
	g_test_add_func ("/editor/settings-migrate/layout/yaml-shape", test_layout_yaml_shape);
	g_test_add_func ("/editor/settings-migrate/layout/invalid", test_layout_invalid);
	g_test_add_func ("/editor/settings-migrate/layout/depth-limit", test_layout_depth_limit);
	g_test_add_func ("/editor/settings-migrate/keybindings/convert", test_shortcut_convert);
	g_test_add_func ("/editor/settings-migrate/theme/convert", test_theme_convert);
	g_test_add ("/editor/settings-migrate/file/full", Fixture, NULL,
	            fixture_setup, test_migrate_full, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/idempotent", Fixture, NULL,
	            fixture_setup, test_migrate_idempotent, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/backup-once", Fixture, NULL,
	            fixture_setup, test_migrate_backup_once, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/no-file", Fixture, NULL,
	            fixture_setup, test_migrate_no_file, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/current-untouched", Fixture, NULL,
	            fixture_setup, test_migrate_current_untouched, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/bad-layout", Fixture, NULL,
	            fixture_setup, test_migrate_bad_layout, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/existing-ui-layout", Fixture, NULL,
	            fixture_setup, test_migrate_existing_ui_layout, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/symlink", Fixture, NULL,
	            fixture_setup, test_migrate_symlink, fixture_teardown);
	g_test_add ("/editor/settings-migrate/file/themes", Fixture, NULL,
	            fixture_setup, test_migrate_themes, fixture_teardown);
	g_test_add ("/editor/settings-migrate/theme/lookup", Fixture, NULL,
	            fixture_setup, test_theme_lookup, fixture_teardown);
	g_test_add_func ("/editor/settings-migrate/theme/shipped-files-load", test_shipped_themes_load);
	g_test_add_func ("/editor/settings-migrate/settings/viewport-cameras", test_viewport_cameras_key);

	return g_test_run ();
}
