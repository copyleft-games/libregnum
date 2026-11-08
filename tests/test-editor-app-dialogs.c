/* test-editor-app-dialogs.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the editor's dialogs, preferences, command palette, quick
 * open and code panel, driven headless through the canvas
 * (tests/editor-app-fixture.h) and the IPC:
 *
 *   preferences    theme switch re-themes the canvas, live settings,
 *                  editor.yaml saved on close
 *   keybindings    "press a key" capture changes an accelerator and the
 *                  new shortcut fires; conflicts; reset
 *   palette        fuzzy action search runs an action, "@" jumps to a
 *                  node, disabled actions do not run, quick open opens a
 *                  level after the async project scan
 *   project        project settings form saves project.ryaml
 *   new-script     the New Script dialog creates, attaches and opens
 *   code-panel     open / edit / dirty mark / panel-scoped Ctrl+S /
 *                  close with unsaved prompt / external reload
 *   code-ipc       code.open, code.list, code.save
 *   shortcuts      the live shortcut table filters and leads to the
 *                  keybindings page; about
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <libregnum.h>

#ifdef LRG_BUILD_EDITOR_UI

#include "editor-app-fixture.h"

typedef EditorAppFixture Fixture;

#define CTRL  LRG_INPUT_MODIFIER_CTRL
#define NOMOD LRG_INPUT_MODIFIER_NONE

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
new_project (Fixture *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Test Game", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
}

static guint
n_selected (Fixture *f)
{
	return lrg_editor_selection_get_count (lrg_editor_get_selection (f->editor));
}

/* The dialog ui.state reports, or NULL. */
static gchar *
ui_dialog (Fixture *f)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");
	JsonNode           *d = json_object_get_member (json_node_get_object (r), "dialog");

	return JSON_NODE_HOLDS_NULL (d) ? NULL : g_strdup (json_node_get_string (d));
}

static gboolean
ui_palette (Fixture *f)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");

	return json_object_get_boolean_member (json_node_get_object (r), "palette");
}

/* The topmost popup (dialogs, the palette) and named widgets inside it */
static LrgWidget *
top_popup (Fixture *f)
{
	return lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app));
}

static LrgWidget *
popup_find (Fixture     *f,
            const gchar *name)
{
	LrgWidget *popup = top_popup (f);

	g_assert_nonnull (popup);
	return lrg_widget_find (popup, name);
}

/* A button of the top dialog by its label */
static LrgWidget *
find_button (LrgWidget   *root,
             const gchar *text)
{
	GList *l;

	if (LRG_IS_BUTTON (root) && g_strcmp0 (lrg_button_get_text (LRG_BUTTON (root)), text) == 0)
		return root;
	if (!LRG_IS_CONTAINER (root))
		return NULL;
	for (l = lrg_container_get_children (LRG_CONTAINER (root)); l != NULL; l = l->next)
	{
		LrgWidget *found = find_button (l->data, text);

		if (found != NULL)
			return found;
	}
	return NULL;
}

static void
click_dialog_button (Fixture     *f,
                     const gchar *text)
{
	LrgWidget *button = find_button (top_popup (f), text);

	g_assert_nonnull (button);
	efx_click_widget (f, button);
	efx_frames (f, 2);
}

static LrgNode *
node_named (Fixture     *f,
            const gchar *name)
{
	GPtrArray *children = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (f->editor)));
	guint      i;

	for (i = 0; i < children->len; i++)
		if (g_strcmp0 (lrg_node_get_name (g_ptr_array_index (children, i)), name) == 0)
			return g_ptr_array_index (children, i);
	return NULL;
}

static LrgEditorCodePanel *
code_panel (Fixture *f)
{
	LrgDockPanel *panel = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "code");

	g_assert_nonnull (panel);
	return LRG_EDITOR_CODE_PANEL (lrg_dock_panel_get_child (panel));
}

static LrgEditorPalette *
palette (Fixture *f)
{
	LrgEditorPalette *p = g_object_get_data (G_OBJECT (f->app), "lrg-editor-palette");

	g_assert_nonnull (p);
	return p;
}

/* Runs frames until @done says so (the async quick-open scan) */
static void
frames_until_scanned (Fixture *f)
{
	gint i;

	for (i = 0; i < 2000 && lrg_editor_palette_get_scanning (palette (f)); i++)
	{
		g_main_context_iteration (NULL, TRUE);
		efx_frame (f);
	}
	g_assert_false (lrg_editor_palette_get_scanning (palette (f)));
}

static gchar *
write_file (Fixture     *f,
            const gchar *rel,
            const gchar *text)
{
	gchar            *path = g_build_filename (f->project, rel, NULL);
	g_autofree gchar *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, NULL));
	return path;
}

static gchar *
read_file (const gchar *path)
{
	gchar *text = NULL;

	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	return text;
}

/* ==========================================================================
 * Preferences
 * ========================================================================== */

/* Clicks the gallery card of theme @index (the gallery's order) */
static void
click_theme_card (Fixture   *f,
                  LrgWidget *gallery,
                  guint      index)
{
	gfloat x, y, w, h;
	gfloat cw;

	efx_rect_of (gallery, &x, &y, &w, &h);
	g_assert_cmpfloat (w, >, 0.0f);
	cw = (w - 10.0f * 3.0f) / 4.0f;
	efx_click (f, x + (gfloat) (index % 4) * (cw + 10.0f) + cw * 0.5f, y + (gfloat) (index / 4) * 110.0f + 40.0f);
}

static void
test_preferences_theme (Fixture       *f,
                        gconstpointer  data)
{
	LrgCanvas            *canvas;
	LrgEditorPreferences *prefs;
	LrgWidget            *gallery;
	LrgPropertyGrid      *grid;
	g_auto(GValue)        fov = G_VALUE_INIT;
	g_autofree gchar     *dialog = NULL;
	g_autofree gchar     *theme = NULL;
	g_autofree gchar     *yaml = NULL;
	g_autofree gchar     *settings = g_build_filename (f->dir, "editor.yaml", NULL);
	GrlColor              mocha_window;
	const GrlColor       *now;

	new_project (f);
	canvas = lrg_editor_app_get_canvas (f->app);
	mocha_window = *lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (canvas)), LRG_THEME_COLOR_WINDOW);

	/* Edit > Preferences opens the dialog ui.state calls "preferences" */
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.preferences"));
	efx_frames (f, 3);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "preferences");
	prefs = LRG_EDITOR_PREFERENCES (popup_find (f, "preferences"));
	g_assert_cmpstr (lrg_editor_preferences_get_page (prefs), ==, "general");

	/* Appearance: click the Latte card (index 3): the editor re-themes now */
	g_assert_true (lrg_editor_preferences_set_page (prefs, "appearance"));
	efx_frames (f, 3);
	gallery = popup_find (f, "theme-gallery");
	g_assert_nonnull (gallery);
	click_theme_card (f, gallery, 3);
	efx_frames (f, 2);
	g_object_get (lrg_editor_app_get_settings (f->app), "theme", &theme, NULL);
	g_assert_cmpstr (theme, ==, "catppuccin-latte");
	now = lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (canvas)), LRG_THEME_COLOR_WINDOW);
	g_assert_false (now->r == mocha_window.r && now->g == mocha_window.g && now->b == mocha_window.b);

	/* A shipped theme file (Macchiato, index 1) applies too */
	click_theme_card (f, gallery, 1);
	efx_frames (f, 2);
	g_free (theme);
	g_object_get (lrg_editor_app_get_settings (f->app), "theme", &theme, NULL);
	g_assert_cmpstr (theme, ==, "catppuccin-macchiato");

	/* A settings row applies live: the viewport camera's field of view */
	grid = lrg_editor_preferences_get_grid (prefs, "viewport");
	g_assert_nonnull (grid);
	g_value_init (&fov, G_TYPE_DOUBLE);
	g_value_set_double (&fov, 72.0);
	g_assert_true (lrg_property_source_set_value (lrg_property_grid_get_source (grid, 0), "camera-fov", &fov));
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_state_get_fov (lrg_editor_app_get_view_state (f->app)), 72.0, 1e-6);

	/* Escape closes and saves editor.yaml */
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 30);
	g_assert_null (ui_dialog (f));
	yaml = read_file (settings);
	g_assert_nonnull (strstr (yaml, "catppuccin-macchiato"));
}

static void
test_preferences_keybinding (Fixture       *f,
                             gconstpointer  data)
{
	LrgEditorActions     *actions = lrg_editor_app_get_actions (f->app);
	LrgEditorPreferences *prefs;
	LrgWidget            *filter;
	LrgWidget            *add;
	g_auto(GStrv)         accels = NULL;
	LrgNode              *ground;
	gboolean              visible;

	new_project (f);
	prefs = lrg_editor_preferences_present (f->app, "keybindings");
	g_assert_nonnull (prefs);
	efx_frames (f, 3);
	g_assert_cmpstr (lrg_editor_preferences_get_page (prefs), ==, "keybindings");

	/* Filter down to the action, then "+" captures the next key */
	filter = popup_find (f, "keymap-filter");
	g_assert_nonnull (filter);
	lrg_text_input_set_text (LRG_TEXT_INPUT (filter), "Toggle Visibility");
	efx_frames (f, 3);
	add = popup_find (f, "kb-add:node.toggle-visible");
	g_assert_nonnull (add);
	efx_click_widget (f, add);
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_preferences_get_capturing (prefs), ==, "node.toggle-visible");

	/* A modifier alone keeps waiting; Ctrl+J is the new alternative */
	efx_key (f, GRL_KEY_LEFT_CONTROL, CTRL);
	g_assert_cmpstr (lrg_editor_preferences_get_capturing (prefs), ==, "node.toggle-visible");
	efx_key (f, GRL_KEY_J, CTRL);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_preferences_get_capturing (prefs));
	accels = lrg_editor_actions_get_accels (actions, "node.toggle-visible");
	g_assert_true (g_strv_contains ((const gchar * const *) accels, "<Ctrl>j"));
	g_assert_true (g_strv_contains ((const gchar * const *) accels, "h"));

	/* Closing the dialog re-enables shortcuts: Ctrl+J toggles visibility */
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 30);
	g_assert_null (ui_dialog (f));
	ground = node_named (f, "Ground");
	g_assert_nonnull (ground);
	lrg_editor_select (f->editor, ground, FALSE);
	visible = lrg_node_get_visible (ground);
	efx_key (f, GRL_KEY_J, CTRL);
	efx_frames (f, 1);
	g_assert_cmpint (lrg_node_get_visible (ground), !=, visible);

	/* A conflict shows in the summary; Reset brings the default back */
	prefs = lrg_editor_preferences_present (f->app, "keybindings");
	efx_frames (f, 3);
	g_assert_true (lrg_editor_actions_set_accels (actions, "node.toggle-lock",
	                                              (const gchar * const []) { "<Ctrl>j", NULL }));
	efx_frames (f, 3);
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (popup_find (f, "keymap-conflicts"))), "conflict"));
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "keymap-filter")), "ctrl+j");
	efx_frames (f, 3);
	g_assert_nonnull (popup_find (f, "kb-row:node.toggle-lock"));
	efx_click_widget (f, popup_find (f, "kb-reset:node.toggle-lock"));
	efx_frames (f, 3);
	g_strfreev (accels);
	accels = lrg_editor_actions_get_accels (actions, "node.toggle-lock");
	g_assert_false (g_strv_contains ((const gchar * const *) accels, "<Ctrl>j"));

	/* Removing the captured alternative ("x" of chip 1) */
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "keymap-filter")), "Toggle Visibility");
	efx_frames (f, 3);
	efx_click_widget (f, popup_find (f, "kb-remove:node.toggle-visible:1"));
	efx_frames (f, 3);
	g_strfreev (accels);
	accels = lrg_editor_actions_get_accels (actions, "node.toggle-visible");
	g_assert_false (g_strv_contains ((const gchar * const *) accels, "<Ctrl>j"));
	(void) prefs;
}

static void
test_preferences_reset_all (Fixture       *f,
                            gconstpointer  data)
{
	LrgEditorPreferences *prefs;
	g_autofree gchar     *theme = NULL;
	g_autofree gchar     *layout_before = NULL;
	g_autofree gchar     *layout_after = NULL;

	new_project (f);
	g_object_set (lrg_editor_app_get_settings (f->app), "theme", "daylight", "history-limit", 42u, NULL);
	lrg_editor_actions_set_accels (lrg_editor_app_get_actions (f->app), "file.save",
	                               (const gchar * const []) { "<Ctrl><Alt>s", NULL });
	lrg_editor_app_save_settings (f->app, NULL);
	g_object_get (lrg_editor_app_get_settings (f->app), "ui-layout", &layout_before, NULL);

	prefs = lrg_editor_preferences_present (f->app, NULL);
	efx_frames (f, 3);
	efx_click_widget (f, popup_find (f, "prefs-reset-all"));
	efx_frames (f, 3);
	g_object_get (lrg_editor_app_get_settings (f->app), "theme", &theme, "ui-layout", &layout_after, NULL);
	g_assert_cmpstr (theme, !=, "daylight");
	g_assert_cmpstr (layout_after, ==, layout_before);
	g_assert_null (lrg_editor_settings_get_keybinding (lrg_editor_app_get_settings (f->app), "file.save"));
	g_assert_cmpstr (ui_dialog (f), ==, "preferences");
	(void) prefs;
}

/* ==========================================================================
 * Palette and quick open
 * ========================================================================== */

static void
test_palette_actions (Fixture       *f,
                      gconstpointer  data)
{
	LrgEditorPalette *p;
	LrgNode          *primary;
	guint             i;
	gboolean          found_disabled = FALSE;

	new_project (f);
	p = palette (f);
	lrg_editor_select (f->editor, NULL, FALSE);

	/* Ctrl+Shift+P opens it; typing fuzzy-filters; Enter runs the best */
	efx_key (f, GRL_KEY_P, CTRL | LRG_INPUT_MODIFIER_SHIFT);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_palette_get_open (p));
	g_assert_true (ui_palette (f));
	g_assert_cmpint (lrg_editor_palette_get_mode (p), ==, LRG_EDITOR_PALETTE_MODE_COMMANDS);
	g_assert_cmpuint (lrg_editor_palette_get_n_results (p), >, 20);
	efx_type (f, "select all");
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_palette_get_result (p, 0), ==, "edit.select-all");
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	efx_frames (f, 2);
	g_assert_cmpuint (n_selected (f), ==, 3);
	g_assert_false (lrg_editor_palette_get_open (p));
	g_assert_false (ui_palette (f));

	/* Disabled actions are listed but do not run (Ungroup, nothing grouped) */
	lrg_editor_select (f->editor, NULL, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	efx_frames (f, 2);
	lrg_editor_palette_set_query (p, "ungroup");
	for (i = 0; i < lrg_editor_palette_get_n_results (p); i++)
		if (g_strcmp0 (lrg_editor_palette_get_result (p, i), "node.ungroup") == 0)
		{
			lrg_editor_palette_set_selected (p, (gint) i);
			found_disabled = TRUE;
		}
	g_assert_true (found_disabled);
	g_assert_false (lrg_editor_palette_activate (p));
	g_assert_true (lrg_editor_palette_get_open (p));

	/* Escape closes; "@camera" jumps to the Camera node */
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 2);
	g_assert_false (lrg_editor_palette_get_open (p));
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	efx_frames (f, 2);
	efx_type (f, "@camera");
	efx_frames (f, 1);
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	efx_frames (f, 2);
	g_assert_cmpuint (n_selected (f), ==, 1);
	primary = lrg_editor_selection_get_primary (lrg_editor_get_selection (f->editor));
	g_assert_cmpstr (lrg_node_get_name (primary), ==, "Camera");

	/* Arrow keys move the highlight */
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_palette_get_selected (p), ==, 0);
	efx_key (f, GRL_KEY_DOWN, NOMOD);
	g_assert_cmpint (lrg_editor_palette_get_selected (p), ==, 1);
	efx_key (f, GRL_KEY_UP, NOMOD);
	efx_key (f, GRL_KEY_UP, NOMOD);
	g_assert_cmpint (lrg_editor_palette_get_selected (p), ==, (gint) lrg_editor_palette_get_n_results (p) - 1);
	lrg_editor_palette_dismiss (p);
}

static void
test_palette_quick_open (Fixture       *f,
                         gconstpointer  data)
{
	LrgEditorPalette *p;
	g_autofree gchar *main_level = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *second = NULL;
	g_autofree gchar *script = NULL;

	new_project (f);
	p = palette (f);
	main_level = g_build_filename (f->project, "levels", "main.rlevel", NULL);
	text = read_file (main_level);
	second = write_file (f, "levels/second.rlevel", text);
	script = write_file (f, "scripts/quick.lua", "return 1\n");
	write_file (f, ".hidden/secret.lua", "return 2\n");

	/* Ctrl+P: the file list arrives from the worker thread */
	efx_key (f, GRL_KEY_P, CTRL);
	efx_frames (f, 1);
	g_assert_true (lrg_editor_palette_get_open (p));
	g_assert_cmpint (lrg_editor_palette_get_mode (p), ==, LRG_EDITOR_PALETTE_MODE_FILES);
	frames_until_scanned (f);
	g_assert_cmpuint (lrg_editor_palette_get_n_files (p), >=, 3);

	/* Hidden folders are skipped */
	lrg_editor_palette_set_query (p, "secret");
	g_assert_cmpuint (lrg_editor_palette_get_n_results (p), ==, 0);

	/* Fuzzy: "secnd" finds levels/second.rlevel; Enter opens the level */
	lrg_editor_palette_set_query (p, "");
	efx_type (f, "secnd");
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_palette_get_result (p, 0), ==, "levels/second.rlevel");
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	efx_frames (f, 3);
	g_assert_false (lrg_editor_palette_get_open (p));
	g_assert_true (g_str_has_suffix (lrg_editor_get_path (f->editor), "second.rlevel"));

	/* With script-editor internal, a script opens in the code panel */
	g_object_set (lrg_editor_app_get_settings (f->app), "script-editor", "internal", NULL);
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.quick-open"));
	frames_until_scanned (f);
	lrg_editor_palette_set_query (p, "quick.lua");
	g_assert_true (lrg_editor_palette_activate (p));
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_code_panel_get_active_path (code_panel (f)), ==, script);
}

/* ==========================================================================
 * Project settings, new script, shortcuts, about
 * ========================================================================== */

static void
test_project_settings (Fixture       *f,
                       gconstpointer  data)
{
	g_autoptr(LrgProject) reopened = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *dialog = NULL;
	GPtrArray            *dirs;
	gboolean              has_audio = FALSE;
	guint                 i;

	new_project (f);
	g_assert_true (lrg_editor_app_activate_action (f->app, "file.project-settings"));
	efx_frames (f, 3);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "project-settings");

	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "project-name")), "Renamed Game");
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "project-run-command")), "lrgldr {module}");
	efx_click_widget (f, popup_find (f, "project-use-current"));
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "project-dir-input")), "assets/audio");
	efx_frames (f, 2);
	efx_click_widget (f, popup_find (f, "project-dir-add"));
	efx_frames (f, 2);
	click_dialog_button (f, "Save");
	efx_frames (f, 30);
	g_assert_null (ui_dialog (f));

	reopened = lrg_project_open (f->project, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_project_get_name (reopened), ==, "Renamed Game");
	g_assert_cmpstr (lrg_project_get_run_command (reopened), ==, "lrgldr {module}");
	g_assert_cmpstr (lrg_project_get_default_level (reopened), ==, "levels/main.rlevel");
	dirs = lrg_project_get_asset_dirs (reopened);
	for (i = 0; dirs != NULL && i < dirs->len; i++)
		if (g_strcmp0 (g_ptr_array_index (dirs, i), "assets/audio") == 0)
			has_audio = TRUE;
	g_assert_true (has_audio);
}

static void
test_new_script (Fixture       *f,
                 gconstpointer  data)
{
	LrgNode          *camera;
	g_autofree gchar *dialog = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *text = NULL;
	GPtrArray        *scripts;

	new_project (f);
	g_object_set (lrg_editor_app_get_settings (f->app), "script-editor", "internal", NULL);
	camera = node_named (f, "Camera");
	lrg_editor_select (f->editor, camera, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.attach-script"));
	efx_frames (f, 3);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "new-script");

	/* Python (index 1), named "mover" */
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "script-name")), "mover");
	lrg_combo_box_set_selected (LRG_COMBO_BOX (popup_find (f, "script-language")), 1);
	click_dialog_button (f, "Create");
	efx_frames (f, 30);
	g_assert_null (ui_dialog (f));

	path = g_build_filename (f->project, "scripts", "mover.py", NULL);
	g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
	scripts = lrg_node_get_scripts (camera);
	g_assert_cmpuint (scripts->len, ==, 1);
	g_assert_cmpstr (lrg_editor_code_panel_get_active_path (code_panel (f)), ==, path);
	text = lrg_text_editor_get_text (lrg_editor_code_panel_get_editor (code_panel (f), NULL));
	g_assert_cmpuint (strlen (text), >, 0);
}

static void
test_shortcuts_and_about (Fixture       *f,
                          gconstpointer  data)
{
	g_autofree gchar *dialog = NULL;
	g_autofree gchar *again = NULL;
	g_autofree gchar *about = NULL;

	new_project (f);
	g_assert_true (lrg_editor_app_activate_action (f->app, "help.shortcuts"));
	efx_frames (f, 3);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "shortcuts");

	/* Read-only, searchable by key name */
	g_assert_null (popup_find (f, "kb-add:file.save"));
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "keymap-filter")), "ctrl+s");
	efx_frames (f, 3);
	g_assert_nonnull (popup_find (f, "kb-row:file.save"));
	g_assert_null (popup_find (f, "kb-row:edit.undo"));

	/* It follows rebinding live */
	lrg_editor_actions_set_accels (lrg_editor_app_get_actions (f->app), "edit.undo",
	                               (const gchar * const []) { "<Ctrl><Shift>s", NULL });
	efx_frames (f, 3);
	g_assert_nonnull (popup_find (f, "kb-row:edit.undo"));

	/* "Edit Keybindings..." leads to the preferences page */
	click_dialog_button (f, "Edit Keybindings...");
	efx_frames (f, 3);
	again = ui_dialog (f);
	g_assert_cmpstr (again, ==, "preferences");
	g_assert_cmpstr (lrg_editor_preferences_get_page (LRG_EDITOR_PREFERENCES (popup_find (f, "preferences"))), ==,
	                 "keybindings");
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 30);

	g_assert_true (lrg_editor_app_activate_action (f->app, "help.about"));
	efx_frames (f, 3);
	about = ui_dialog (f);
	g_assert_cmpstr (about, ==, "about");
	g_assert_nonnull (popup_find (f, "about-source"));
	g_assert_nonnull (strstr (lrg_dialog_get_body (LRG_DIALOG (top_popup (f))), "AGPL"));
	g_assert_nonnull (strstr (lrg_dialog_get_body (LRG_DIALOG (top_popup (f))), LRG_VERSION_STRING));
}

/* ==========================================================================
 * Code panel
 * ========================================================================== */

static void
test_code_panel (Fixture       *f,
                 gconstpointer  data)
{
	LrgEditorCodePanel *panel;
	LrgTextEditor      *editor;
	g_autofree gchar   *path = NULL;
	g_autofree gchar   *disk = NULL;
	g_autofree gchar   *text = NULL;
	g_autofree gchar   *dialog = NULL;
	g_autoptr(GError)   error = NULL;
	LrgTabView         *tabs;

	new_project (f);
	panel = code_panel (f);
	path = write_file (f, "scripts/door.lua", "local door = {}\nreturn door\n");

	/* Open: one tab, Lua colours, caret on the line asked for */
	g_assert_true (lrg_editor_code_panel_open (panel, path, 2, &error));
	g_assert_no_error (error);
	efx_frames (f, 3);
	g_assert_cmpuint (lrg_editor_code_panel_get_n_files (panel), ==, 1);
	editor = lrg_editor_code_panel_get_editor (panel, path);
	g_assert_nonnull (lrg_text_editor_get_syntax (editor));
	g_assert_cmpstr (lrg_syntax_get_name (lrg_text_editor_get_syntax (editor)), ==, "lua");
	{
		guint line = 0;
		guint col = 0;

		lrg_text_editor_get_cursor (editor, &line, &col);
		g_assert_cmpuint (line, ==, 1);
	}
	g_assert_true (lrg_canvas_get_focused_widget (lrg_editor_app_get_canvas (f->app)) == LRG_WIDGET (editor));

	/* Typing marks the tab dirty (a dot in its title) */
	efx_type (f, "-- hi ");
	efx_frames (f, 1);
	g_assert_true (lrg_editor_code_panel_is_dirty (panel, path));
	tabs = LRG_TAB_VIEW (lrg_widget_find (LRG_WIDGET (panel), "code-tabs"));
	g_assert_nonnull (strstr (lrg_tab_view_get_tab_label (tabs, 0), "\xe2\x80\xa2"));

	/* Ctrl+S inside the panel saves the file, not the level */
	lrg_editor_mark_modified (f->editor);
	efx_key (f, GRL_KEY_S, CTRL);
	efx_frames (f, 1);
	g_assert_false (lrg_editor_code_panel_is_dirty (panel, path));
	g_assert_null (strstr (lrg_tab_view_get_tab_label (tabs, 0), "\xe2\x80\xa2"));
	disk = read_file (path);
	g_assert_nonnull (strstr (disk, "-- hi "));
	g_assert_true (lrg_editor_is_dirty (f->editor));

	/* Changed on disk while clean: reloads */
	g_free (disk);
	disk = g_strdup ("local door = { open = true }\nreturn door\n");
	g_assert_true (g_file_set_contents (path, disk, -1, NULL));
	g_assert_cmpuint (lrg_editor_code_panel_check_disk (panel), ==, 1);
	text = lrg_text_editor_get_text (editor);
	g_assert_cmpstr (text, ==, disk);
	g_assert_false (lrg_editor_code_panel_is_dirty (panel, path));

	/* Changed on disk while dirty: asks; Reload takes the disk version */
	lrg_text_editor_set_cursor (editor, 0, 0);
	lrg_text_editor_insert_text (editor, "-- mine\n");
	g_assert_true (lrg_editor_code_panel_is_dirty (panel, path));
	g_assert_true (g_file_set_contents (path, "return 42\n", -1, NULL));
	g_assert_cmpuint (lrg_editor_code_panel_check_disk (panel), ==, 0);
	efx_frames (f, 3);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "code-reload");
	click_dialog_button (f, "Reload");
	efx_frames (f, 30);
	g_free (text);
	text = lrg_text_editor_get_text (editor);
	g_assert_cmpstr (text, ==, "return 42\n");
	g_assert_false (lrg_editor_code_panel_is_dirty (panel, path));

	/* Closing a dirty tab asks first; Cancel keeps it, Don't Save drops it */
	lrg_text_editor_insert_text (editor, "-- unsaved\n");
	g_assert_false (lrg_editor_code_panel_close (panel, NULL, FALSE));
	efx_frames (f, 3);
	g_free (dialog);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "confirm");
	click_dialog_button (f, "Cancel");
	efx_frames (f, 30);
	g_assert_cmpuint (lrg_editor_code_panel_get_n_files (panel), ==, 1);
	g_assert_true (lrg_editor_app_activate_action (f->app, "code.close-tab"));
	efx_frames (f, 3);
	click_dialog_button (f, "Don't Save");
	efx_frames (f, 30);
	g_assert_cmpuint (lrg_editor_code_panel_get_n_files (panel), ==, 0);
	g_free (disk);
	disk = read_file (path);
	g_assert_cmpstr (disk, ==, "return 42\n");

	/* The "code" action group on the canvas: code.open with "path:line" */
	{
		g_autofree gchar *target = g_strdup_printf ("%s:2", path);

		g_assert_true (lrg_widget_activate_action (LRG_WIDGET (lrg_editor_app_get_canvas (f->app)), "code.open",
		                                           g_variant_new_string (target)));
	}
	g_assert_cmpuint (lrg_editor_code_panel_get_n_files (panel), ==, 1);
	lrg_editor_code_panel_close (panel, NULL, TRUE);
}

static void
test_code_ipc (Fixture       *f,
               gconstpointer  data)
{
	g_autofree gchar   *path = NULL;
	g_autofree gchar   *req = NULL;
	g_autofree gchar   *reply = NULL;
	g_autofree gchar   *disk = NULL;
	g_autoptr(JsonNode) r = NULL;
	g_autoptr(JsonNode) list = NULL;
	g_autoptr(JsonNode) saved = NULL;
	JsonObject         *o;
	LrgEditorCodePanel *panel;
	LrgNode            *camera;
	g_autoptr(LrgScriptBinding) binding = NULL;

	new_project (f);
	panel = code_panel (f);
	path = write_file (f, "scripts/ipc.lua", "print(1)\nprint(2)\nprint(3)\n");

	req = g_strdup_printf ("{\"id\":1,\"method\":\"code.open\",\"params\":{\"path\":\"%s\",\"line\":3}}", path);
	reply = lrg_editor_server_dispatch (lrg_editor_app_get_server (f->app), req);
	g_assert_nonnull (strstr (reply, "\"line\":3"));
	g_assert_nonnull (strstr (reply, "\"language\":\"lua\""));

	/* Relative paths resolve against the project */
	r = efx_ipc_result (f, "{'id':2,'method':'code.open','params':{'path':'scripts/ipc.lua'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, path);
	g_assert_cmpuint (lrg_editor_code_panel_get_n_files (panel), ==, 1);

	list = efx_ipc_result (f, "{'id':3,'method':'code.list'}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (list)), ==, 1);
	o = json_array_get_object_element (json_node_get_array (list), 0);
	g_assert_true (json_object_get_boolean_member (o, "active"));
	g_assert_false (json_object_get_boolean_member (o, "dirty"));

	lrg_text_editor_insert_text (lrg_editor_code_panel_get_editor (panel, NULL), "-- ipc\n");
	saved = efx_ipc_result (f, "{'id':4,'method':'code.save'}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (saved), "dirty"));
	disk = read_file (path);
	g_assert_nonnull (strstr (disk, "-- ipc"));

	/* Errors */
	g_free (reply);
	reply = efx_ipc (f, "{'id':5,'method':'code.open','params':{'path':'nope/missing.lua'}}");
	g_assert_nonnull (strstr (reply, "not-found"));
	g_free (reply);
	reply = efx_ipc (f, "{'id':6,'method':'code.open','params':{}}");
	g_assert_nonnull (strstr (reply, "invalid-params"));

	/* node.open-scripts follows script-editor: internal opens here */
	lrg_editor_code_panel_close (panel, NULL, TRUE);
	g_object_set (lrg_editor_app_get_settings (f->app), "script-editor", "internal", NULL);
	camera = node_named (f, "Camera");
	binding = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "scripts/ipc.lua");
	lrg_editor_add_script (f->editor, camera, binding);
	lrg_editor_select (f->editor, camera, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.open-scripts"));
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_code_panel_get_active_path (panel), ==, path);
}

static void
append_file (const gchar *path,
             const gchar *extra)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *joined = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_true (g_file_get_contents (path, &text, NULL, &error));
	g_assert_no_error (error);
	joined = g_strconcat (text, "\n", extra, NULL);
	g_assert_true (g_file_set_contents (path, joined, -1, &error));
	g_assert_no_error (error);
}

static void
test_export_dialog (Fixture      *f,
                    gconstpointer data)
{
	g_autofree gchar *out = NULL;
	g_autofree gchar *dialog = NULL;
	const gchar * const *ids;
	LrgWidget *log;
	guint i;
	guint rres;

	(void) data;
	new_project (f);
	out = g_build_filename (f->project, "out.rres", NULL);
	g_assert_true (lrg_editor_app_activate_action (f->app, "file.export"));
	efx_frames (f, 2);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "export");
	ids = lrg_pack_writer_profile_ids ();
	rres = 0;
	for (i = 0; ids != NULL && ids[i] != NULL; i++)
	{
		if (g_strcmp0 (ids[i], "rres") == 0)
			rres = i;
	}
	lrg_combo_box_set_selected (LRG_COMBO_BOX (popup_find (f, "export-profile")), rres);
	lrg_text_input_set_text (LRG_TEXT_INPUT (popup_find (f, "export-output")), out);
	efx_click_widget (f, popup_find (f, "export-run"));
	efx_frames (f, 2);
	log = popup_find (f, "export-log");
	g_assert_nonnull (log);
	g_assert_cmpuint (strlen (lrg_label_get_text (LRG_LABEL (log))), >, 0);
	g_assert_true (g_file_test (out, G_FILE_TEST_IS_REGULAR));
	g_free (dialog);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "export");
}

static void
test_steam_page (Fixture      *f,
                 gconstpointer data)
{
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *dialog = NULL;

	(void) data;
	new_project (f);
	manifest = g_build_filename (f->project, "project.ryaml", NULL);
	append_file (manifest,
	             "steam:\n"
	             "  app-id: \"4242\"\n"
	             "  achievements: \"true\"\n");
	g_assert_true (lrg_editor_app_activate_action (f->app, "file.project-settings"));
	efx_frames (f, 2);
	dialog = ui_dialog (f);
	g_assert_cmpstr (dialog, ==, "project-settings");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (popup_find (f, "project-steam-app-id"))), ==, "4242");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (popup_find (f, "project-steam-achievements"))), ==, "true");
}

static const gchar dialog_plugin_src[] =
	"typedef struct {\n"
	"  void (*add_action) (void *, const char *, const char *, const char *);\n"
	"  void (*add_panel) (void *, const char *, const char *);\n"
	"  void (*add_inspector) (void *, const char *);\n"
	"} Api;\n"
	"void lrg_plugin_init (void *manager, const Api *api)\n"
	"{\n"
	"  api->add_action (manager, \"demo.hello\", \"Hello\", \"Demo\");\n"
	"  api->add_panel (manager, \"demo-panel\", \"Demo\");\n"
	"  api->add_inspector (manager, \"LrgNode\");\n"
	"}\n";

static void
test_plugin_dock (Fixture      *f,
                  gconstpointer data)
{
	g_autofree gchar *plug = NULL;
	g_autofree gchar *src = NULL;
	g_autofree gchar *so = NULL;
	g_autofree gchar *yaml = NULL;
	g_autoptr(GError) error = NULL;
	LrgDockArea *dock;
	GMenu *menu;
	gchar *argv[7];
	gint status;
	const gchar *last;

	(void) data;
	if (g_find_program_in_path ("gcc") == NULL)
	{
		g_test_skip ("gcc is not on PATH");
		return;
	}
	new_project (f);
	plug = g_build_filename (f->project, "plugins", "demo", NULL);
	src = g_build_filename (plug, "plugin.c", NULL);
	so = g_build_filename (plug, "plugin.so", NULL);
	yaml = g_build_filename (plug, "plugin.yaml", NULL);
	g_assert_cmpint (g_mkdir_with_parents (plug, 0755), ==, 0);
	g_assert_true (g_file_set_contents (src, dialog_plugin_src, -1, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_set_contents (yaml, "name: demo\nentry: plugin.so\n", -1, &error));
	g_assert_no_error (error);
	argv[0] = (gchar *) "gcc";
	argv[1] = (gchar *) "-shared";
	argv[2] = (gchar *) "-fPIC";
	argv[3] = (gchar *) "-o";
	argv[4] = so;
	argv[5] = src;
	argv[6] = NULL;
	g_assert_true (g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
	                             NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_true (g_spawn_check_wait_status (status, &error));
	g_assert_no_error (error);

	g_assert_true (lrg_editor_app_open_project (f->app, f->project, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	dock = lrg_editor_window_get_dock (lrg_editor_app_get_window (f->app));
	g_assert_nonnull (lrg_dock_area_get_panel (dock, "demo-panel"));
	g_assert_nonnull (lrg_widget_find (LRG_WIDGET (lrg_editor_app_get_canvas (f->app)), "demo-panel"));
	g_assert_nonnull (lrg_widget_find (LRG_WIDGET (lrg_editor_app_get_canvas (f->app)),
	                                   "plugin-inspector-LrgNode"));
	g_assert_true (lrg_editor_actions_has (lrg_editor_app_get_actions (f->app), "demo.hello"));
	menu = g_object_get_data (G_OBJECT (f->app), "lrg-editor-plugins-menu");
	g_assert_nonnull (menu);
	g_assert_cmpuint (g_menu_model_get_n_items (G_MENU_MODEL (menu)), ==, 1);
	g_assert_true (lrg_editor_actions_activate (lrg_editor_app_get_actions (f->app), "demo.hello"));
	last = g_object_get_data (G_OBJECT (f->app), "lrg-plugin-last-action");
	g_assert_cmpstr (last, ==, "demo.hello");

	g_assert_true (lrg_editor_app_open_project (f->app, f->project, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (g_menu_model_get_n_items (G_MENU_MODEL (menu)), ==, 1);
	g_assert_nonnull (lrg_dock_area_get_panel (dock, "demo-panel"));
}

#endif /* LRG_BUILD_EDITOR_UI */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */

#ifdef LRG_BUILD_EDITOR_UI
#define ADD(path, func) \
	g_test_add ("/editor-app/dialogs/" path, Fixture, NULL, efx_setup, func, efx_teardown)

	ADD ("preferences-theme", test_preferences_theme);
	ADD ("preferences-keybinding", test_preferences_keybinding);
	ADD ("preferences-reset-all", test_preferences_reset_all);
	ADD ("palette", test_palette_actions);
	ADD ("quick-open", test_palette_quick_open);
	ADD ("project-settings", test_project_settings);
	ADD ("export-dialog", test_export_dialog);
	ADD ("steam-page", test_steam_page);
	ADD ("plugin-dock", test_plugin_dock);
	ADD ("new-script", test_new_script);
	ADD ("shortcuts-about", test_shortcuts_and_about);
	ADD ("code-panel", test_code_panel);
	ADD ("code-ipc", test_code_ipc);
#undef ADD
#endif

	return g_test_run ();
}
