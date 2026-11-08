/* test-editor-app-data-1.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The data editors, part 1: the base panel (LrgEditorDataEditor) and the
 * input map, locale, sound bank and theme editors, driven through the
 * canvas of a headless LrgEditorApp (tests/editor-app-fixture.h) and the
 * data.* IPC methods.
 */

#include "editor-app-fixture.h"
#include <utime.h>
#include <time.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static const gchar *INPUT_YAML =
	"# player controls\n"
	"actions:\n"
	"  jump:\n"
	"    bindings:\n"
	"      - { type: keyboard, key: SPACE }\n"
	"      - { type: gamepad_button, gamepad: 0, button: RIGHT_FACE_DOWN }\n"
	"  fire:\n"
	"    description: shoots\n"
	"    bindings:\n"
	"      - { type: mouse_button, button: LEFT, modifiers: [SHIFT] }\n";

/* A project with @rel holding @text; returns the absolute path. */
static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path;
	g_autofree gchar *dir = NULL;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

static gchar *
read_file (const gchar *path)
{
	gchar *text = NULL;

	g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
	return text;
}

/* Another program writes @path: new content and a modification time
 * that differs from any earlier write (fast writes share a timestamp). */
static void
write_external (const gchar *path,
                const gchar *text)
{
	static gint64      bump = 0;
	struct utimbuf     t;

	g_assert_true (g_file_set_contents (path, text, -1, NULL));
	bump += 10;
	t.actime = (time_t) (g_get_real_time () / G_USEC_PER_SEC + bump);
	t.modtime = t.actime;
	g_assert_cmpint (g_utime (path, (gpointer) &t), ==, 0);
}

/* Opens @path through the data.open IPC method and lays out a frame. */
static LrgEditorDataEditor *
open_data (EditorAppFixture *f,
           const gchar      *path)
{
	g_autofree gchar    *req = g_strdup_printf ("{'id':1,'method':'data.open','params':{'path':'%s'}}", path);
	g_autoptr(JsonNode)  res = efx_ipc_result (f, req);
	LrgEditorDataEditor *e;

	g_assert_nonnull (res);
	efx_frames (f, 3);
	e = lrg_editor_data_editor_find (f->app, path);
	g_assert_nonnull (e);
	return e;
}

/* A named widget inside @root. */
static LrgWidget *
find_in (gpointer     root,
         const gchar *name)
{
	LrgWidget *w = lrg_widget_find (LRG_WIDGET (root), name);

	g_assert_nonnull (w);
	return w;
}

static void
click_in (EditorAppFixture *f,
          gpointer          root,
          const gchar      *name)
{
	efx_click_widget (f, find_in (root, name));
	efx_frames (f, 2);
}

/* Clicks inside the editor's content (focusing it) at a neutral spot. */
static void
focus_editor (EditorAppFixture    *f,
              LrgEditorDataEditor *e)
{
	lrg_canvas_set_focused_widget (lrg_editor_app_get_canvas (f->app), LRG_WIDGET (e));
	efx_frame (f);
}

static JsonObject *
result_object (JsonNode *node)
{
	g_assert_true (JSON_NODE_HOLDS_OBJECT (node));
	return json_node_get_object (node);
}

/* The engine loaders run by validation warn about what they reject;
 * those warnings are expected, not fatal. */
static gboolean
loader_warnings_ok (const gchar    *domain,
                    GLogLevelFlags  level,
                    const gchar    *message,
                    gpointer        user_data)
{
	return !(domain != NULL && g_str_has_prefix (domain, "Libregnum-") && (level & G_LOG_LEVEL_WARNING) &&
	         g_strcmp0 (domain, "Libregnum-Editor") != 0);
}

/* ==========================================================================
 * Base panel
 * ========================================================================== */

/* data.open picks the editor by format, docks it as "data:<path>", reuses
 * the tab, and data.list reports it */
static void
test_open_and_reuse (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autofree gchar    *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	g_autofree gchar    *id = g_strconcat ("data:", path, NULL);
	LrgEditorDataEditor *e = open_data (f, path);
	LrgDockArea         *dock = lrg_editor_app_get_dock (f->app);
	g_autoptr(JsonNode)  list = NULL;
	g_autoptr(GError)    error = NULL;
	JsonObject          *o;

	g_assert_true (LRG_IS_EDITOR_INPUT_MAP_EDITOR (e));
	g_assert_cmpstr (lrg_editor_data_editor_get_panel_id (e), ==, id);
	g_assert_nonnull (lrg_dock_area_get_panel (dock, id));
	g_assert_true (lrg_dock_area_get_active_panel (dock) == lrg_dock_area_get_panel (dock, id));
	g_assert_cmpstr (lrg_editor_data_editor_get_title (e), ==, "input.yaml");
	g_assert_cmpuint (lrg_editor_data_editor_get_issues (e)->len, ==, 0);

	/* a second open (relative path) reuses the tab */
	g_assert_true (lrg_editor_data_editor_open (f->app, "data/input.yaml", &error) == e);
	g_assert_no_error (error);
	list = efx_ipc_result (f, "{'id':2,'method':'data.list'}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (list)), ==, 1);
	o = json_array_get_object_element (json_node_get_array (list), 0);
	g_assert_cmpstr (json_object_get_string_member (o, "format"), ==, "input-map");
	g_assert_cmpstr (json_object_get_string_member (o, "editor"), ==, "LrgEditorInputMapEditor");
	g_assert_false (json_object_get_boolean_member (o, "dirty"));

	/* not a data format */
	g_assert_null (lrg_editor_data_editor_open (f->app, "project.ryaml", &error));
	g_assert_nonnull (error);
}

/* Edits go to the document's own undo stack: Ctrl+Z / Ctrl+Shift+Z in the
 * panel undo and redo them, the level's history is untouched */
static void
test_undo_document_only (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	LrgEditorDataEditor     *e = open_data (f, path);
	LrgEditorInputMapEditor *im = LRG_EDITOR_INPUT_MAP_EDITOR (e);
	LrgEditorDocument       *doc = lrg_editor_data_editor_get_document (e);
	gfloat                   x, y, w, h;

	/* click the first chip of "jump" to select it, then Remove */
	g_assert_true (lrg_editor_input_map_editor_get_chip_rect (im, "jump", 0, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_input_map_editor_get_selected_action (im), ==, "jump");
	g_assert_cmpint (lrg_editor_input_map_editor_get_selected_slot (im), ==, 0);
	click_in (f, e, "input-remove");
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/jump/bindings"), ==, 1);
	g_assert_true (lrg_editor_data_editor_get_dirty (e));
	g_assert_true (g_str_has_suffix (lrg_editor_data_editor_get_title (e), "\xe2\x80\xa2"));

	/* Ctrl+Z inside the panel: the document, not the level */
	focus_editor (f, e);
	efx_key (f, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/jump/bindings"), ==, 2);
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
	g_assert_false (lrg_editor_can_undo (f->editor));
	g_assert_cmpstr (lrg_editor_data_editor_get_title (e), ==, "input.yaml");

	efx_key (f, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/jump/bindings"), ==, 1);
	g_assert_false (lrg_editor_can_redo (f->editor));
}

/* Save writes the file (keeping comments and unknown keys), Revert reloads */
static void
test_save_and_revert (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autofree gchar    *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	LrgEditorDataEditor *e = open_data (f, path);
	LrgEditorDocument   *doc = lrg_editor_data_editor_get_document (e);
	g_autofree gchar    *text = NULL;
	g_autoptr(GError)    error = NULL;

	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "W", 0, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	click_in (f, e, "data-save");
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
	text = read_file (path);
	g_assert_nonnull (strstr (text, "key: W"));
	g_assert_nonnull (strstr (text, "# player controls"));
	g_assert_nonnull (strstr (text, "description: shoots"));

	/* Revert of a clean document reloads without asking */
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "Q", 0, &error));
	efx_frames (f, 2);
	g_assert_true (lrg_editor_data_editor_get_dirty (e));
	g_assert_true (lrg_editor_data_editor_revert (e, &error));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/jump/bindings/0/key"), ==, "W");
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
}

/* Another program changes the file: a clean document reloads, a dirty one
 * shows the banner (Keep mine / Reload) */
static void
test_external_change_banner (EditorAppFixture *f,
                             gconstpointer     data)
{
	g_autofree gchar    *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	LrgEditorDataEditor *e = open_data (f, path);
	LrgEditorDocument   *doc = lrg_editor_data_editor_get_document (e);
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *changed = NULL;

	/* clean: silent reload */
	changed = g_strdup_printf ("%s  dash:\n    bindings: []\n", INPUT_YAML);
	write_external (path, changed);
	g_assert_true (lrg_editor_data_editor_check_disk (e));
	efx_frames (f, 2);
	g_assert_false (lrg_editor_data_editor_get_banner_visible (e));
	g_assert_true (lrg_editor_document_has_path (doc, "/actions/dash"));

	/* dirty: the banner, Keep mine hides it and keeps the edit */
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "W", 0, &error));
	write_external (path, INPUT_YAML);
	g_assert_true (lrg_editor_data_editor_check_disk (e));
	efx_frames (f, 2);
	g_assert_true (lrg_editor_data_editor_get_banner_visible (e));
	g_assert_true (lrg_widget_get_visible (find_in (e, "data-banner")));
	click_in (f, e, "data-banner-keep");
	g_assert_false (lrg_editor_data_editor_get_banner_visible (e));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/jump/bindings/0/key"), ==, "W");

	/* again, and this time Reload */
	write_external (path, changed);
	lrg_editor_document_reload (doc, NULL);
	g_assert_true (lrg_editor_document_set_string (doc, "/actions/jump/bindings/0/key", "E", 0, &error));
	write_external (path, INPUT_YAML);
	g_assert_true (lrg_editor_data_editor_check_disk (e));
	efx_frames (f, 2);
	g_assert_true (lrg_editor_data_editor_get_banner_visible (e));
	click_in (f, e, "data-banner-reload");
	g_assert_false (lrg_editor_data_editor_get_banner_visible (e));
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
	g_assert_false (lrg_editor_document_has_path (doc, "/actions/dash"));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/jump/bindings/0/key"), ==, "SPACE");
}

/* Validation: the chip counts issues, the list opens, clicking a row
 * reveals the path in the subclass */
static void
test_issues_click_through (EditorAppFixture *f,
                           gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "data/input.yaml",
	                                                    "actions:\n"
	                                                    "  jump:\n"
	                                                    "    bindings:\n"
	                                                    "      - { type: keyboard, key: SPACE }\n"
	                                                    "  fire:\n"
	                                                    "    bindings:\n"
	                                                    "      - { type: keyboard, key: SPACE }\n"
	                                                    "      - { type: keyboard }\n");
	LrgEditorDataEditor     *e = open_data (f, path);
	LrgEditorInputMapEditor *im = LRG_EDITOR_INPUT_MAP_EDITOR (e);
	LrgWidget               *list;
	GPtrArray               *issues;
	LrgEditorDocumentIssue  *issue;
	guint                    i, target = G_MAXUINT;
	gfloat                   rx = 0.0f, ry = 0.0f;
	g_autoptr(JsonNode)      res = NULL;

	/* (re-read after every validation: it replaces the array) */
	click_in (f, e, "data-validate");
	issues = lrg_editor_data_editor_get_issues (e);
	g_assert_cmpuint (issues->len, >, 0);
	for (i = 0; i < issues->len; i++)
	{
		issue = g_ptr_array_index (issues, i);
		if (issue->path != NULL && g_str_has_prefix (issue->path, "/actions/fire/bindings/1"))
			target = i;
	}
	g_assert_cmpuint (target, !=, G_MAXUINT);

	/* Validate opened the list; a click on the row reveals the binding */
	list = find_in (e, "data-issues-list");
	g_assert_true (lrg_widget_get_visible (list));
	g_assert_true (lrg_list_view_get_row_rect (LRG_LIST_VIEW (list), target, &rx, &ry, NULL, NULL));
	efx_click (f, rx + 30.0f, ry + 6.0f);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_input_map_editor_get_selected_action (im), ==, "fire");
	g_assert_cmpint (lrg_editor_input_map_editor_get_selected_slot (im), ==, 1);

	/* data.validate reports the same issues */
	res = efx_ipc_result (f, "{'id':3,'method':'data.validate'}");
	g_assert_false (json_object_get_boolean_member (result_object (res), "ok"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (res), "issues")), ==,
	                  issues->len);
}

/* Closing a dirty tab asks first; Don't Save closes, the file is unchanged */
static void
test_close_prompt (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autofree gchar    *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	g_autofree gchar    *id = g_strconcat ("data:", path, NULL);
	LrgEditorDataEditor *e = open_data (f, path);
	LrgDialog           *dialog;
	g_autofree gchar    *text = NULL;

	g_assert_true (lrg_editor_document_remove (lrg_editor_data_editor_get_document (e), "/actions/fire", NULL));
	g_assert_false (lrg_editor_data_editor_close (e, FALSE));
	efx_frames (f, 2);
	dialog = g_object_get_data (G_OBJECT (f->app), "lrg-editor-dialog");
	g_assert_true (LRG_IS_DIALOG (dialog));
	lrg_dialog_respond (dialog, 2);    /* Don't Save */
	efx_frames (f, 2);
	g_assert_null (lrg_editor_data_editor_find (f->app, path));
	g_assert_null (lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), id));
	text = read_file (path);
	g_assert_cmpstr (text, ==, INPUT_YAML);
}

/* ==========================================================================
 * Input maps
 * ========================================================================== */

/* "+" starts capture; a key chord becomes a keyboard binding, a mouse
 * click on the veil a mouse binding, Escape cancels */
static void
test_input_capture (EditorAppFixture *f,
                    gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	LrgEditorDataEditor     *e = open_data (f, path);
	LrgEditorInputMapEditor *im = LRG_EDITOR_INPUT_MAP_EDITOR (e);
	LrgEditorDocument       *doc = lrg_editor_data_editor_get_document (e);
	g_autofree gchar        *desc = NULL;
	gfloat                   x, y, w, h;

	/* the "+" chip of fire (slot 1 = after its one binding) */
	g_assert_true (lrg_editor_input_map_editor_get_chip_rect (im, "fire", 1, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_input_map_editor_get_capturing (im));
	g_assert_true (lrg_widget_get_visible (find_in (e, "input-capture")));

	/* a lone Ctrl waits; Ctrl+F records */
	efx_key (f, GRL_KEY_LEFT_CONTROL, LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (lrg_editor_input_map_editor_get_capturing (im));
	efx_key (f, GRL_KEY_F, LRG_INPUT_MODIFIER_CTRL);
	efx_frames (f, 2);
	g_assert_false (lrg_editor_input_map_editor_get_capturing (im));
	g_assert_cmpint (lrg_editor_document_get_length (doc, "/actions/fire/bindings"), ==, 2);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/bindings/1/key"), ==, "F");
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/bindings/1/modifiers/0"), ==, "CTRL");
	desc = lrg_editor_input_map_editor_describe_binding (im, "fire", 1);
	g_assert_cmpstr (desc, ==, "Ctrl+F");
	g_assert_cmpint (lrg_editor_input_map_editor_get_selected_slot (im), ==, 1);

	/* clicking the selected chip again re-captures it; a right click on
	 * the veil records the mouse button */
	g_assert_true (lrg_editor_input_map_editor_get_chip_rect (im, "fire", 1, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_input_map_editor_get_capturing (im));
	efx_rect_of (find_in (e, "input-capture"), &x, &y, &w, &h);
	efx_move (f, x + w * 0.5f, y + h * 0.5f);
	efx_button (f, x + w * 0.5f, y + h * 0.5f, 1, TRUE);
	efx_button (f, x + w * 0.5f, y + h * 0.5f, 1, FALSE);
	efx_frames (f, 2);
	g_assert_false (lrg_editor_input_map_editor_get_capturing (im));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/bindings/1/type"), ==, "mouse_button");
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/bindings/1/button"), ==, "RIGHT");

	/* Escape cancels without a change */
	g_assert_true (lrg_editor_input_map_editor_start_capture (im, "jump", 0));
	efx_frames (f, 1);
	efx_key (f, GRL_KEY_ESCAPE, 0);
	efx_frames (f, 2);
	g_assert_false (lrg_editor_input_map_editor_get_capturing (im));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/jump/bindings/0/key"), ==, "SPACE");

	/* one undo step per capture */
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/bindings/1/key"), ==, "F");
}

/* Actions: add, rename through the grid, type change rewrites the binding */
static void
test_input_actions (EditorAppFixture *f,
                    gconstpointer     data)
{
	g_autofree gchar        *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	LrgEditorDataEditor     *e = open_data (f, path);
	LrgEditorInputMapEditor *im = LRG_EDITOR_INPUT_MAP_EDITOR (e);
	LrgEditorDocument       *doc = lrg_editor_data_editor_get_document (e);
	LrgPropertyGrid         *grid = LRG_PROPERTY_GRID (find_in (e, "input-grid"));
	g_auto(GValue)           v = G_VALUE_INIT;
	g_auto(GStrv)            actions = NULL;

	click_in (f, e, "input-add-action");
	g_assert_cmpstr (lrg_editor_input_map_editor_get_selected_action (im), ==, "action_1");
	g_assert_true (lrg_editor_document_has_path (doc, "/actions/action_1/bindings"));

	/* rename through the grid row (as typing would apply it) */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "dash");
	lrg_property_row_apply_value (lrg_property_grid_get_row (grid, "action-name"), &v);
	efx_frames (f, 2);
	actions = lrg_editor_document_input_actions (doc);
	g_assert_cmpstr (actions[2], ==, "dash");
	g_assert_cmpstr (lrg_editor_input_map_editor_get_selected_action (im), ==, "dash");

	/* switch jump's first binding to a gamepad button */
	g_assert_true (lrg_editor_input_map_editor_select (im, "jump", 0));
	efx_frames (f, 1);
	g_value_set_string (&v, "gamepad_button");
	lrg_property_row_apply_value (lrg_property_grid_get_row (grid, "type"), &v);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/jump/bindings/0/type"), ==, "gamepad_button");
	g_assert_nonnull (lrg_property_grid_get_row (grid, "button"));
	g_assert_null (lrg_property_grid_get_row (grid, "key"));

	/* the description key of fire survived everything */
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/actions/fire/description"), ==, "shoots");
}

/* ==========================================================================
 * Locales
 * ========================================================================== */

static const gchar *EN_YAML =
	"code: en\n"
	"name: English\n"
	"strings:\n"
	"  greeting: 'Hello, %s!'\n"
	"  farewell: Goodbye\n"
	"  items:\n"
	"    one: '%d item'\n"
	"    other: '%d items'\n";

static const gchar *FR_YAML =
	"code: fr\n"
	"name: Fran\xc3\xa7" "ais\n"
	"strings:\n"
	"  greeting: 'Bonjour, %s !'\n"
	"  items:\n"
	"    one: '%d objet'\n";

/* The grid loads every locale of the folder, highlights what the other
 * languages lack, and a cell edited through the edit bar fills it */
static void
test_locale_grid (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autofree gchar      *en = write_project_file (f, "locale/en.yaml", EN_YAML);
	g_autofree gchar      *fr = write_project_file (f, "locale/fr.yaml", FR_YAML);
	LrgEditorDataEditor   *e = open_data (f, en);
	LrgEditorLocaleEditor *le = LRG_EDITOR_LOCALE_EDITOR (e);
	LrgEditorDocument     *frdoc;
	g_auto(GStrv)          keys = NULL;
	g_autofree gchar      *text = NULL;
	gfloat                 x, y, w, h;
	GPtrArray             *issues;
	guint                  i;
	gboolean               found = FALSE;

	g_assert_true (LRG_IS_EDITOR_LOCALE_EDITOR (e));
	g_assert_cmpuint (lrg_editor_locale_editor_get_n_languages (le), ==, 2);
	g_assert_cmpstr (lrg_editor_locale_editor_get_language (le, 0), ==, "en");
	g_assert_cmpstr (lrg_editor_locale_editor_get_language (le, 1), ==, "fr");
	keys = lrg_editor_locale_editor_get_keys (le);
	g_assert_cmpuint (g_strv_length (keys), ==, 4);
	g_assert_cmpstr (keys[2], ==, "items[one]");

	/* missing cells: farewell and items[other] in fr */
	g_assert_true (lrg_editor_locale_editor_is_missing (le, "farewell", "fr"));
	g_assert_true (lrg_editor_locale_editor_is_missing (le, "items[other]", "fr"));
	g_assert_false (lrg_editor_locale_editor_is_missing (le, "greeting", "fr"));
	g_assert_false (lrg_editor_locale_editor_is_missing (le, "farewell", "en"));

	/* ... reported as warnings that reveal the missing cell */
	issues = lrg_editor_data_editor_get_issues (e);
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		if (g_strcmp0 (issue->path, "/strings/farewell") == 0)
		{
			found = TRUE;
			g_assert_true (lrg_editor_data_editor_activate_issue (e, i));
		}
	}
	g_assert_true (found);
	g_assert_cmpstr (lrg_editor_locale_editor_get_selected_key (le), ==, "farewell");
	g_assert_cmpstr (lrg_editor_locale_editor_get_selected_language (le), ==, "fr");

	/* click the fr cell of farewell, type, Enter */
	efx_frames (f, 2);
	g_assert_true (lrg_editor_locale_editor_get_cell_rect (le, "farewell", "fr", &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_frames (f, 2);
	g_assert_true (lrg_canvas_get_focused_widget (lrg_editor_app_get_canvas (f->app)) ==
	               find_in (e, "locale-edit-input"));
	efx_type (f, "Au revoir");
	efx_key (f, GRL_KEY_ENTER, 0);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_locale_editor_get_text (le, "farewell", "fr"), ==, "Au revoir");
	g_assert_false (lrg_editor_locale_editor_is_missing (le, "farewell", "fr"));
	frdoc = lrg_editor_locale_editor_get_language_document (le, "fr");
	g_assert_true (lrg_editor_document_get_dirty (frdoc));
	g_assert_true (lrg_editor_data_editor_get_dirty (e));
	g_assert_false (lrg_editor_document_get_dirty (lrg_editor_data_editor_get_document (e)));

	/* undo goes to the language that was edited */
	focus_editor (f, e);
	efx_key (f, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_locale_editor_get_text (le, "farewell", "fr"));
	g_assert_true (lrg_editor_data_editor_redo (e));
	g_assert_cmpstr (lrg_editor_locale_editor_get_text (le, "farewell", "fr"), ==, "Au revoir");

	/* a plural form; Save writes every language */
	g_assert_true (lrg_editor_locale_editor_set_text (le, "items[other]", "fr", "%d objets", NULL));
	click_in (f, e, "data-save");
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
	text = read_file (fr);
	g_assert_nonnull (strstr (text, "Au revoir"));
	g_assert_nonnull (strstr (text, "%d objets"));
}

/* Export to CSV and import it back */
static void
test_locale_csv (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autofree gchar      *en = write_project_file (f, "locale/en.yaml", EN_YAML);
	g_autofree gchar      *fr = write_project_file (f, "locale/fr.yaml", FR_YAML);
	LrgEditorDataEditor   *e = open_data (f, en);
	LrgEditorLocaleEditor *le = LRG_EDITOR_LOCALE_EDITOR (e);
	g_autofree gchar      *csv = lrg_editor_locale_editor_export_csv (le);
	g_autoptr(GPtrArray)   rows = NULL;
	g_autoptr(GError)      error = NULL;
	GStrv                  row;
	gint                   n;

	/* the export parses back with lrg_csv_parse */
	rows = lrg_csv_parse (csv, -1, ',', &error);
	g_assert_no_error (error);
	g_assert_cmpuint (rows->len, ==, 5);
	row = g_ptr_array_index (rows, 0);
	g_assert_cmpstr (row[0], ==, "key");
	g_assert_cmpstr (row[1], ==, "en");
	g_assert_cmpstr (row[2], ==, "fr");
	row = g_ptr_array_index (rows, 1);
	g_assert_cmpstr (row[0], ==, "greeting");
	g_assert_cmpstr (row[1], ==, "Hello, %s!");    /* quoted: it holds a comma */
	g_assert_cmpstr (row[2], ==, "Bonjour, %s !");
	row = g_ptr_array_index (rows, 2);
	g_assert_cmpstr (row[2], ==, "");               /* farewell missing in fr */

	/* a translator fills the gaps in the CSV and it comes back (the
	 * unknown "de" column is skipped with a warning) */
	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*'de'*");
	n = lrg_editor_locale_editor_import_csv (le,
	                                         "key,fr,de\n"
	                                         "farewell,\"Au revoir, \"\"ami\"\"\",Tsch\xc3\xbcss\n"
	                                         "items[other],%d objets,\n"
	                                         "greeting,,\n",
	                                         &error);
	g_test_assert_expected_messages ();
	g_assert_no_error (error);
	g_assert_cmpint (n, ==, 2);
	g_assert_cmpstr (lrg_editor_locale_editor_get_text (le, "farewell", "fr"), ==, "Au revoir, \"ami\"");
	g_assert_cmpstr (lrg_editor_locale_editor_get_text (le, "items[other]", "fr"), ==, "%d objets");
	g_assert_cmpstr (lrg_editor_locale_editor_get_text (le, "greeting", "fr"), ==, "Bonjour, %s !");

	/* one undo step for the whole import */
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_null (lrg_editor_locale_editor_get_text (le, "farewell", "fr"));
	g_assert_null (lrg_editor_locale_editor_get_text (le, "items[other]", "fr"));

	/* round trip: importing an export changes nothing */
	g_assert_true (lrg_editor_data_editor_redo (e));
	g_free (csv);
	csv = lrg_editor_locale_editor_export_csv (le);
	g_assert_cmpint (lrg_editor_locale_editor_import_csv (le, csv, &error), ==, 0);
	{
		g_autofree gchar *again = lrg_editor_locale_editor_export_csv (le);

		g_assert_cmpstr (again, ==, csv);
	}
	(void) fr;
}

/* ==========================================================================
 * Sound banks
 * ========================================================================== */

static void
test_sound_bank (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autofree gchar         *bank = write_project_file (f, "audio/player.yaml",
	                                                     "name: player\n"
	                                                     "base_path: sounds/\n"
	                                                     "sounds:\n"
	                                                     "  jump: jump.wav\n"
	                                                     "  land: land.ogg\n");
	g_autofree gchar         *wav = write_project_file (f, "audio/sounds/jump.wav", "RIFF");
	g_autofree gchar         *boom = write_project_file (f, "audio/sounds/fx/boom.wav", "RIFF");
	g_autofree gchar         *land2 = write_project_file (f, "audio/land2.ogg", "OggS");
	LrgEditorDataEditor      *e = open_data (f, bank);
	LrgEditorSoundBankEditor *sb = LRG_EDITOR_SOUND_BANK_EDITOR (e);
	LrgEditorDocument        *doc = lrg_editor_data_editor_get_document (e);
	LrgCanvas                *canvas = lrg_editor_app_get_canvas (f->app);
	g_autoptr(LrgDragPayload) payload = NULL;
	LrgPropertyGrid          *grid;
	GPtrArray                *issues = lrg_editor_data_editor_get_issues (e);
	g_autoptr(GError)         error = NULL;
	g_auto(GValue)            v = G_VALUE_INIT;
	gfloat                    x, y, w, h;
	guint                     i;
	gboolean                  land_missing = FALSE;

	g_assert_true (LRG_IS_EDITOR_SOUND_BANK_EDITOR (e));

	/* land.ogg does not exist: a warning with its path */
	for (i = 0; i < issues->len; i++)
		if (g_strcmp0 (((LrgEditorDocumentIssue *) g_ptr_array_index (issues, i))->path, "/sounds/land") == 0)
			land_missing = TRUE;
	g_assert_true (land_missing);

	/* drop an audio asset (project-relative, as the assets panel drags it)
	 * on the sound list: a new sound with a bank-relative path */
	payload = lrg_drag_payload_new_string ("application/x-lrg-asset", "audio/sounds/fx/boom.wav",
	                                       LRG_DRAG_ACTION_COPY);
	efx_rect_of (find_in (e, "sound-list"), &x, &y, &w, &h);
	g_assert_true (lrg_canvas_begin_drag (canvas, NULL, payload, x + 4.0f, y + 4.0f));
	efx_move (f, x + w * 0.5f, y + h * 0.5f);
	efx_move (f, x + w * 0.5f, y + h * 0.5f + 2.0f);
	efx_click (f, x + w * 0.5f, y + h * 0.5f + 2.0f);
	efx_frames (f, 2);
	g_assert_false (lrg_canvas_is_dragging (canvas));
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/sounds/boom"), ==, "fx/boom.wav");
	g_assert_cmpstr (lrg_editor_sound_bank_editor_get_selected (sb), ==, "boom");
	{
		g_autofree gchar *resolved = lrg_editor_document_sound_bank_resolve (doc, "boom");

		g_assert_true (g_file_test (resolved, G_FILE_TEST_IS_REGULAR));
	}

	/* the File row takes a project-relative asset too: outside the sound
	 * folder it becomes "../" relative */
	g_assert_true (lrg_editor_sound_bank_editor_select (sb, "land"));
	efx_frames (f, 2);
	grid = LRG_PROPERTY_GRID (find_in (e, "sound-grid"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "sound-file"));
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "audio/land2.ogg");
	g_assert_true (lrg_property_grid_drop_reference (grid, "sound-file", "application/x-lrg-asset", &v));
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/sounds/land"), ==, "../land2.ogg");

	/* remove and undo */
	click_in (f, e, "sound-remove");
	g_assert_false (lrg_editor_document_has_path (doc, "/sounds/land"));
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_true (lrg_editor_document_has_path (doc, "/sounds/land"));

	/* preview of a missing file fails without opening audio */
	g_assert_true (lrg_editor_document_set_string (doc, "/sounds/land", "nope.ogg", 0, NULL));
	g_assert_false (lrg_editor_sound_bank_editor_play (sb, "land", &error));
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
	(void) wav;
	(void) boom;
	(void) land2;
}

/* ==========================================================================
 * Themes
 * ========================================================================== */

static const gchar *THEME_YAML =
	"# my theme\n"
	"name: ember\n"
	"preset: catppuccin-mocha\n"
	"accent: '#ff7744'\n"
	"metrics:\n"
	"  corner-radius: 6\n";

static void
test_theme_preview_and_use (EditorAppFixture *f,
                            gconstpointer     data)
{
	g_autofree gchar     *path = write_project_file (f, "themes/ember.yaml", THEME_YAML);
	LrgEditorDataEditor  *e = open_data (f, path);
	LrgEditorThemeEditor *te = LRG_EDITOR_THEME_EDITOR (e);
	LrgEditorDocument    *doc = lrg_editor_data_editor_get_document (e);
	LrgPropertyGrid      *grid = LRG_PROPERTY_GRID (find_in (e, "theme-grid"));
	LrgTheme             *preview = lrg_editor_theme_editor_get_preview_theme (te);
	const GrlColor       *c;
	g_auto(GValue)        v = G_VALUE_INIT;
	g_autofree gchar     *text = NULL;
	gchar                *theme_setting = NULL;

	g_assert_true (LRG_IS_EDITOR_THEME_EDITOR (e));
	g_assert_cmpstr (lrg_theme_get_name (preview), ==, "ember");
	c = lrg_theme_get_accent_color (preview);
	g_assert_cmpuint (c->r, ==, 0xff);
	g_assert_cmpuint (c->g, ==, 0x77);
	g_assert_true (lrg_widget_get_theme (lrg_editor_theme_editor_get_gallery (te)) == preview);

	/* edit the accent through the grid: the gallery follows */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "#22aa66");
	lrg_property_row_apply_value (lrg_property_grid_get_row (grid, "accent"), &v);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/accent"), ==, "#22aa66");
	preview = lrg_editor_theme_editor_get_preview_theme (te);
	c = lrg_theme_get_accent_color (preview);
	g_assert_cmpuint (c->g, ==, 0xaa);
	g_assert_true (lrg_widget_get_theme (lrg_editor_theme_editor_get_gallery (te)) == preview);

	/* a colour role that is not in the file yet: /colors is created */
	efx_frames (f, 2);
	g_value_set_string (&v, "#101010");
	lrg_property_row_apply_value (lrg_property_grid_get_row (grid, "color-window"), &v);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_document_get_scalar (doc, "/colors/window"), ==, "#101010");
	g_assert_true (lrg_editor_data_editor_undo (e));
	g_assert_false (lrg_editor_document_has_path (doc, "/colors"));

	/* a broken value keeps the last good preview and says why */
	g_assert_true (lrg_editor_document_set_string (doc, "/motion", "jiggly", 0, NULL));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_editor_theme_editor_get_preview_error (te));
	g_assert_cmpuint (lrg_theme_get_accent_color (lrg_editor_theme_editor_get_preview_theme (te))->g, ==, 0xaa);
	g_assert_true (lrg_editor_data_editor_undo (e));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_theme_editor_get_preview_error (te));

	/* Use for editor: saved, set as the editor theme, applied */
	click_in (f, e, "theme-use");
	g_assert_false (lrg_editor_data_editor_get_dirty (e));
	text = read_file (path);
	g_assert_nonnull (strstr (text, "#22aa66"));
	g_assert_nonnull (strstr (text, "# my theme"));
	g_object_get (lrg_editor_app_get_settings (f->app), "theme", &theme_setting, NULL);
	g_assert_cmpstr (theme_setting, ==, path);
	g_free (theme_setting);
	c = lrg_theme_get_accent_color (lrg_widget_get_theme (LRG_WIDGET (lrg_editor_app_get_canvas (f->app))));
	g_assert_cmpuint (c->g, ==, 0xaa);

	/* later saves re-apply it */
	efx_frames (f, 2);
	g_value_set_string (&v, "#3355ff");
	lrg_property_row_apply_value (lrg_property_grid_get_row (grid, "accent"), &v);
	efx_frames (f, 2);
	click_in (f, e, "data-save");
	c = lrg_theme_get_accent_color (lrg_widget_get_theme (LRG_WIDGET (lrg_editor_app_get_canvas (f->app))));
	g_assert_cmpuint (c->b, ==, 0xff);
}

/* ==========================================================================
 * Assets panel hook
 * ========================================================================== */

/* "Open" on a data file in the assets panel opens its editor */
static void
test_assets_open (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autofree gchar   *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	g_autoptr(JsonNode) r = NULL;
	LrgWidget          *assets;

	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.refresh-assets"));
	efx_frames (f, 3);
	r = efx_ipc_result (f, "{'id':1,'method':'asset.reveal','params':{'path':'data/input.yaml'}}");
	efx_frames (f, 2);
	assets = efx_find (f, "assets-panel");
	g_assert_nonnull (assets);
	g_assert_true (lrg_widget_activate_action (assets, "assets.open", NULL));
	efx_frames (f, 3);
	g_assert_nonnull (lrg_editor_data_editor_find (f->app, path));
	g_assert_true (LRG_IS_EDITOR_INPUT_MAP_EDITOR (lrg_editor_data_editor_find (f->app, NULL)));
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	g_autofree gchar   *path = write_project_file (f, "data/input.yaml", INPUT_YAML);
	g_autofree gchar   *req = NULL;
	g_autoptr(JsonNode) res = NULL;
	g_autofree gchar   *reply = NULL;
	g_autofree gchar   *text = NULL;
	JsonObject         *o;

	open_data (f, path);

	res = efx_ipc_result (f, "{'id':1,'method':'data.get','params':{'at':'/actions'}}");
	o = result_object (res);
	g_assert_cmpstr (json_object_get_string_member (o, "kind"), ==, "mapping");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "keys")), ==, 2);
	g_clear_pointer (&res, json_node_unref);

	res = efx_ipc_result (f, "{'id':2,'method':'data.set','params':{'at':'/actions/jump/bindings/0/key',"
	                         "'value':'W'}}");
	g_assert_true (json_object_get_boolean_member (result_object (res), "dirty"));
	g_clear_pointer (&res, json_node_unref);

	res = efx_ipc_result (f, "{'id':3,'method':'data.edit','params':{'op':'rename_key','at':'/actions',"
	                         "'old':'fire','new':'shoot'}}");
	g_clear_pointer (&res, json_node_unref);
	res = efx_ipc_result (f, "{'id':4,'method':'data.get','params':{'at':'/actions/shoot/bindings/0/button'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (res), "value"), ==, "LEFT");
	g_clear_pointer (&res, json_node_unref);

	res = efx_ipc_result (f, "{'id':5,'method':'data.undo'}");
	g_assert_true (json_object_get_boolean_member (result_object (res), "ok"));
	g_clear_pointer (&res, json_node_unref);
	res = efx_ipc_result (f, "{'id':6,'method':'data.redo'}");
	g_assert_true (json_object_get_boolean_member (result_object (res), "ok"));
	g_clear_pointer (&res, json_node_unref);

	req = g_strdup_printf ("{'id':7,'method':'data.save','params':{'path':'%s'}}", path);
	res = efx_ipc_result (f, req);
	g_assert_false (json_object_get_boolean_member (result_object (res), "dirty"));
	g_clear_pointer (&res, json_node_unref);
	text = read_file (path);
	g_assert_nonnull (strstr (text, "shoot:"));
	g_assert_nonnull (strstr (text, "key: W"));

	res = efx_ipc_result (f, "{'id':8,'method':'data.reload'}");
	g_clear_pointer (&res, json_node_unref);

	/* errors */
	reply = efx_ipc (f, "{'id':9,'method':'data.get','params':{'path':'nope.yaml'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_free (reply);
	reply = efx_ipc (f, "{'id':10,'method':'data.edit','params':{'op':'explode','at':'/actions'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);

#define ADD(name, fn) g_test_add ("/editor-app-data/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("open-and-reuse", test_open_and_reuse);
	ADD ("undo-document-only", test_undo_document_only);
	ADD ("save-and-revert", test_save_and_revert);
	ADD ("external-change-banner", test_external_change_banner);
	ADD ("issues-click-through", test_issues_click_through);
	ADD ("close-prompt", test_close_prompt);
	ADD ("input/capture", test_input_capture);
	ADD ("input/actions", test_input_actions);
	ADD ("locale/grid", test_locale_grid);
	ADD ("locale/csv", test_locale_csv);
	ADD ("sound-bank", test_sound_bank);
	ADD ("theme/preview-and-use", test_theme_preview_and_use);
	ADD ("assets-open", test_assets_open);
	ADD ("ipc", test_ipc);
#undef ADD

	return g_test_run ();
}
