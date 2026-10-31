/* test-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgEditorApp driven headless, frame by frame: projects and
 * levels, actions and shortcuts, the command palette, dialogs, play mode
 * restoring the level, save and dirty state, live reload of a level edited
 * outside the editor, and the app's IPC methods.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <libregnum.h>

#ifdef LRG_BUILD_EDITOR_UI

#define W (1600.0f)
#define H (960.0f)

typedef struct
{
	LrgEditor    *editor;
	LrgEditorApp *app;
	LrgGuiInput   input;
	gchar        *dir;
	gchar        *project;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *settings = NULL;

	f->dir = g_dir_make_tmp ("lrg-app-XXXXXX", NULL);
	settings = g_build_filename (f->dir, "editor.yaml", NULL);
	f->project = g_build_filename (f->dir, "game", NULL);

	f->editor = lrg_editor_new ();
	f->app = lrg_editor_app_new (f->editor);
	g_assert_true (lrg_editor_app_load_settings (f->app, settings, &error));
	g_assert_no_error (error);
	lrg_gui_input_init (&f->input);
}

static void
rm_rf (const gchar *path)
{
	g_autoptr(GDir) d = g_dir_open (path, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child = g_build_filename (path, name, NULL);

		if (g_file_test (child, G_FILE_TEST_IS_DIR) && !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
			rm_rf (child);
		else
			g_unlink (child);
	}
	g_rmdir (path);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->app);
	g_clear_object (&f->editor);
	rm_rf (f->dir);
	g_free (f->dir);
	g_free (f->project);
}

/* One UI frame; keys and text queued on f->input are consumed. */
static void
frame (Fixture *f)
{
	while (g_main_context_iteration (NULL, FALSE))
		;
	lrg_editor_app_frame (f->app, &f->input, W, H);
	lrg_gui_input_new_frame (&f->input, 1.0f / 60.0f);
}

/* Queues a key press; the key is released after the next frame (tests
 * have no real key-up events, and a held key would count as a repeat). */
static void
press (Fixture         *f,
       LrgGuiKey        key,
       LrgGuiModifiers  mods)
{
	lrg_gui_input_add_key (&f->input, key, mods);
	frame (f);
	f->input.key_down[key] = FALSE;
}

static void
frames (Fixture *f,
        gint     n)
{
	gint i;

	for (i = 0; i < n; i++)
		frame (f);
}

static void
new_project (Fixture *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Test Game", &error));
	g_assert_no_error (error);
}

static guint
n_nodes (Fixture *f)
{
	return lrg_level_count_nodes (lrg_editor_get_level (f->editor));
}

static guint
n_selected (Fixture *f)
{
	return lrg_editor_selection_get_count (lrg_editor_get_selection (f->editor));
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void
test_app_frames_empty (Fixture       *f,
                       gconstpointer  data)
{
	/* An empty editor survives many frames and resizes. */
	frames (f, 5);
	lrg_editor_app_frame (f->app, &f->input, 400.0f, 300.0f);
	frames (f, 2);
	g_assert_cmpuint (n_nodes (f), ==, 0);
	g_assert_false (lrg_editor_app_should_quit (f->app));
}

static void
test_app_new_project (Fixture       *f,
                      gconstpointer  data)
{
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *level = NULL;

	new_project (f);
	manifest = g_build_filename (f->project, "project.ryaml", NULL);
	level = g_build_filename (f->project, "levels", "main.rlevel", NULL);
	g_assert_true (g_file_test (manifest, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (level, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_editor_get_path (f->editor), ==, level);
	g_assert_cmpuint (n_nodes (f), ==, 3);
	g_assert_false (lrg_editor_is_dirty (f->editor));
	frames (f, 3);
}

static void
test_app_actions (Fixture       *f,
                  gconstpointer  data)
{
	new_project (f);
	frames (f, 2);

	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.select-all"));
	g_assert_cmpuint (n_selected (f), ==, 3);
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.duplicate"));
	g_assert_cmpuint (n_nodes (f), ==, 6);
	g_assert_true (lrg_editor_is_dirty (f->editor));
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.delete"));
	g_assert_cmpuint (n_nodes (f), ==, 3);
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.undo"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.undo"));
	g_assert_cmpuint (n_nodes (f), ==, 3);
	g_assert_false (lrg_editor_is_dirty (f->editor));

	/* Group, then ungroup puts the children back at the top level. */
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.select-all"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.group"));
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (lrg_editor_get_level (f->editor))), ==, 1);
	frames (f, 2);
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.ungroup"));
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (lrg_editor_get_level (f->editor))), ==, 3);
	g_assert_cmpuint (n_nodes (f), ==, 3);
	frames (f, 2);

	/* View actions keep working (frame-all used to free the nodes). */
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.frame-all"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.frame-all"));
	frames (f, 2);
	g_assert_cmpuint (n_nodes (f), ==, 3);

	g_assert_true (lrg_editor_app_activate_action (f->app, "tool.rotate"));
	g_assert_cmpint (lrg_editor_get_tool (f->editor), ==, LRG_EDITOR_TOOL_ROTATE);
	g_assert_false (lrg_editor_app_activate_action (f->app, "no.such-action"));
}

static void
test_app_shortcuts (Fixture       *f,
                    gconstpointer  data)
{
	new_project (f);
	frames (f, 2);

	/* Ctrl+A selects all, Delete deletes, Ctrl+Z undoes. */
	press (f, LRG_GUI_KEY_A, LRG_GUI_MOD_CTRL);
	g_assert_cmpuint (n_selected (f), ==, 3);
	press (f, LRG_GUI_KEY_DELETE, LRG_GUI_MOD_NONE);
	g_assert_cmpuint (n_nodes (f), ==, 0);
	press (f, LRG_GUI_KEY_Z, LRG_GUI_MOD_CTRL);
	g_assert_cmpuint (n_nodes (f), ==, 3);

	/* A user rebinding takes effect at once. */
	lrg_editor_settings_set_keybinding (lrg_editor_app_get_settings (f->app), "tool.scale", "Ctrl+Alt+K");
	press (f, LRG_GUI_KEY_K, LRG_GUI_MOD_CTRL | LRG_GUI_MOD_ALT);
	g_assert_cmpint (lrg_editor_get_tool (f->editor), ==, LRG_EDITOR_TOOL_SCALE);
}

static void
test_app_palette (Fixture       *f,
                  gconstpointer  data)
{
	new_project (f);
	frames (f, 2);
	lrg_editor_select (f->editor, NULL, FALSE);

	/* Open the palette, type, run the best match with Enter. */
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	frames (f, 2);
	lrg_gui_input_add_text (&f->input, "select all");
	frame (f);
	press (f, LRG_GUI_KEY_ENTER, LRG_GUI_MOD_NONE);
	frames (f, 1);
	g_assert_cmpuint (n_selected (f), ==, 3);

	/* "@name" jumps to a node. */
	lrg_editor_select (f->editor, NULL, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	frames (f, 2);
	lrg_gui_input_add_text (&f->input, "@camera");
	frame (f);
	press (f, LRG_GUI_KEY_ENTER, LRG_GUI_MOD_NONE);
	frames (f, 1);
	g_assert_cmpuint (n_selected (f), ==, 1);
	g_assert_cmpstr (lrg_node_get_name (lrg_editor_selection_get_primary (lrg_editor_get_selection (f->editor))),
	                 ==, "Camera");
}

static void
test_app_dialogs (Fixture       *f,
                  gconstpointer  data)
{
	gint i;
	static const gchar *actions[] = {
		"edit.preferences", "file.project-settings", "help.about", "help.shortcuts",
		"file.open-level", "file.save-as", "file.new-project", "node.attach-script"
	};

	new_project (f);
	frames (f, 2);

	/* Every dialog opens, draws for a few frames and closes on Escape. */
	for (i = 0; i < (gint) G_N_ELEMENTS (actions); i++)
	{
		if (g_strcmp0 (actions[i], "node.attach-script") == 0)
			lrg_editor_app_activate_action (f->app, "edit.select-all");
		g_assert_true (lrg_editor_app_activate_action (f->app, actions[i]));
		frames (f, 3);
		press (f, LRG_GUI_KEY_ESCAPE, LRG_GUI_MOD_NONE);
		frames (f, 1);
		/* Some dialogs focus a text field; a second Escape leaves it. */
		press (f, LRG_GUI_KEY_ESCAPE, LRG_GUI_MOD_NONE);
		frames (f, 1);
	}

	/* Shortcuts work again once no dialog is open. */
	lrg_editor_select (f->editor, NULL, FALSE);
	press (f, LRG_GUI_KEY_A, LRG_GUI_MOD_CTRL);
	g_assert_cmpuint (n_selected (f), ==, 3);
}

static gboolean
find_named (LrgNode  *node,
            gpointer  user_data)
{
	gpointer *data = user_data;

	if (g_strcmp0 (lrg_node_get_name (node), data[0]) == 0)
	{
		data[1] = node;
		return FALSE;
	}
	return TRUE;
}

static LrgNode *
node_named (Fixture     *f,
            const gchar *name)
{
	gpointer data[2] = { (gpointer) name, NULL };

	lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (f->editor)), find_named, data);
	return data[1];
}

static void
test_app_play_restores (Fixture       *f,
                        gconstpointer  data)
{
	LrgNode *ground;
	guint    undo;

	new_project (f);
	frames (f, 2);
	ground = node_named (f, "Ground");
	g_assert_nonnull (ground);
	undo = lrg_editor_get_undo_count (f->editor);

	g_assert_true (lrg_editor_app_activate_action (f->app, "play.start"));
	g_assert_true (lrg_editor_app_is_playing (f->app));
	frames (f, 5);

	/* Edits while playing are discarded on stop but stay redoable. */
	lrg_editor_rename_node (f->editor, ground, "Changed");
	lrg_node_set_location_xyz (ground, 5.0f, 6.0f, 7.0f);
	frames (f, 2);

	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	g_assert_false (lrg_editor_app_is_playing (f->app));
	g_assert_cmpstr (lrg_node_get_name (ground), ==, "Ground");
	g_assert_cmpfloat (lrg_node_get_location (ground)->x, ==, 0.0f);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo);
	g_assert_cmpuint (lrg_editor_get_redo_count (f->editor), ==, 1);
	g_assert_false (lrg_editor_is_dirty (f->editor));
}

static void
test_app_save (Fixture       *f,
               gconstpointer  data)
{
	g_autofree gchar *contents = NULL;

	new_project (f);
	frames (f, 2);
	lrg_editor_rename_node (f->editor, node_named (f, "Camera"), "MainCamera");
	g_assert_true (lrg_editor_is_dirty (f->editor));

	press (f, LRG_GUI_KEY_S, LRG_GUI_MOD_CTRL);
	g_assert_false (lrg_editor_is_dirty (f->editor));
	g_assert_true (g_file_get_contents (lrg_editor_get_path (f->editor), &contents, NULL, NULL));
	g_assert_nonnull (strstr (contents, "MainCamera"));
}

static void
test_app_live_reload (Fixture       *f,
                      gconstpointer  data)
{
	g_autofree gchar *path = NULL;
	gint64            deadline;
	static const gchar *edited =
		"type: level\n"
		"name: main\n"
		"nodes:\n"
		"  - name: OnlyNode\n";

	new_project (f);
	frames (f, 2);
	path = g_strdup (lrg_editor_get_path (f->editor));

	/* Someone (Emacs) rewrites the YAML; a clean document reloads. */
	g_assert_true (g_file_set_contents (path, edited, -1, NULL));
	deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
	while (n_nodes (f) != 1 && g_get_monotonic_time () < deadline)
	{
		g_main_context_iteration (NULL, FALSE);
		frame (f);
		g_usleep (5000);
	}
	g_assert_cmpuint (n_nodes (f), ==, 1);
	g_assert_nonnull (node_named (f, "OnlyNode"));
	g_assert_false (lrg_editor_is_dirty (f->editor));
}

static void
test_app_ipc (Fixture       *f,
              gconstpointer  data)
{
	LrgEditorServer  *server = lrg_editor_app_get_server (f->app);
	g_autofree gchar *list = NULL;
	g_autofree gchar *run = NULL;
	g_autofree gchar *view = NULL;
	g_autofree gchar *info = NULL;
	g_autofree gchar *problems = NULL;

	new_project (f);
	frames (f, 2);

	list = lrg_editor_server_dispatch (server, "{\"id\":1,\"method\":\"action.list\"}");
	g_assert_nonnull (strstr (list, "edit.undo"));
	run = lrg_editor_server_dispatch (server,
	                                  "{\"id\":2,\"method\":\"action.run\",\"params\":{\"id\":\"edit.select-all\"}}");
	g_assert_nonnull (strstr (run, "\"result\":true"));
	g_assert_cmpuint (n_selected (f), ==, 3);
	view = lrg_editor_server_dispatch (server, "{\"id\":3,\"method\":\"view.get\"}");
	g_assert_nonnull (strstr (view, "position"));
	info = lrg_editor_server_dispatch (server, "{\"id\":4,\"method\":\"project.info\"}");
	g_assert_nonnull (strstr (info, "Test Game"));
	problems = lrg_editor_server_dispatch (server, "{\"id\":5,\"method\":\"level.validate\"}");
	g_assert_nonnull (strstr (problems, "\"result\":[]"));
}

static void
test_app_quit_confirm (Fixture       *f,
                       gconstpointer  data)
{
	new_project (f);
	frames (f, 2);

	/* Headless, a quit request with unsaved work quits (no dialog). */
	lrg_editor_rename_node (f->editor, node_named (f, "Sun"), "Moon");
	lrg_editor_app_request_quit (f->app);
	frame (f);
	g_assert_true (lrg_editor_app_should_quit (f->app));
}


/* Moves the pointer and sets button states for the next frame. */
static void
mouse3 (Fixture *f,
        gfloat   x,
        gfloat   y,
        gboolean left,
        gboolean middle,
        gboolean right)
{
	f->input.mouse_x = x;
	f->input.mouse_y = y;
	f->input.mouse_down[LRG_GUI_MOUSE_LEFT] = left;
	f->input.mouse_down[LRG_GUI_MOUSE_MIDDLE] = middle;
	f->input.mouse_down[LRG_GUI_MOUSE_RIGHT] = right;
	frame (f);
}

static void
mouse (Fixture *f,
       gfloat   x,
       gfloat   y,
       gboolean left,
       gboolean right)
{
	mouse3 (f, x, y, left, FALSE, right);
}

/* The camera as JSON, to compare before/after. */
static gchar *
view_json (Fixture *f)
{
	g_autofree gchar *reply = lrg_editor_server_dispatch (lrg_editor_app_get_server (f->app),
	                                                      "{\"id\":1,\"method\":\"view.get\"}");

	return g_strdup (strchr (reply, '{') != NULL ? strstr (reply, "\"result\"") : reply);
}

static gboolean
view_ortho (Fixture *f)
{
	g_autofree gchar *v = view_json (f);

	return strstr (v, "\"orthographic\":true") != NULL;
}

/* Rect of the viewport panel, from ui.state. */
static void
viewport_rect (Fixture *f,
               gfloat  *x,
               gfloat  *y,
               gfloat  *w,
               gfloat  *h)
{
	g_autofree gchar *reply = lrg_editor_server_dispatch (lrg_editor_app_get_server (f->app),
	                                                      "{\"id\":1,\"method\":\"ui.state\"}");
	const gchar      *p = strstr (reply, "\"viewport\":[");

	g_assert_nonnull (p);
	g_assert_cmpint (sscanf (p, "\"viewport\":[%f,%f,%f,%f]", x, y, w, h), ==, 4);
}

static void
test_app_menubar_click (Fixture       *f,
                        gconstpointer  data)
{
	LrgGuiContext *gui = lrg_editor_app_get_gui (f->app);

	new_project (f);
	frames (f, 2);

	/* Press opens, release keeps it open, clicking the header closes it. */
	mouse (f, 20.0f, 12.0f, FALSE, FALSE);
	mouse (f, 20.0f, 12.0f, TRUE, FALSE);
	g_assert_true (lrg_gui_is_popup_open (gui, "File"));
	mouse (f, 20.0f, 12.0f, FALSE, FALSE);
	frames (f, 2);
	g_assert_true (lrg_gui_is_popup_open (gui, "File"));
	mouse (f, 20.0f, 12.0f, TRUE, FALSE);
	mouse (f, 20.0f, 12.0f, FALSE, FALSE);
	g_assert_false (lrg_gui_is_popup_open (gui, "File"));

	/* Alt + letter opens a menu from the keyboard; Escape closes it. */
	mouse (f, 700.0f, 500.0f, FALSE, FALSE);
	press (f, LRG_GUI_KEY_V, LRG_GUI_MOD_ALT);
	frame (f);
	g_assert_true (lrg_gui_is_popup_open (gui, "View"));
	press (f, LRG_GUI_KEY_ESCAPE, LRG_GUI_MOD_NONE);
	g_assert_false (lrg_gui_is_popup_open (gui, "View"));

	/* Ctrl+Alt+E is an action, not the Edit menu. */
	press (f, LRG_GUI_KEY_E, LRG_GUI_MOD_CTRL | LRG_GUI_MOD_ALT);
	frame (f);
	g_assert_false (lrg_gui_is_popup_open (gui, "Edit"));
}

static void
test_app_viewport_rotate (Fixture       *f,
                          gconstpointer  data)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	gfloat            vx, vy, vw, vh, cx, cy;
	gint              i;

	new_project (f);
	frames (f, 2);
	viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;

	/* Right drag flies the camera (look). */
	before = view_json (f);
	mouse (f, cx, cy, FALSE, FALSE);
	mouse (f, cx, cy, FALSE, TRUE);
	for (i = 1; i <= 10; i++)
		mouse (f, cx + i * 10.0f, cy + i * 3.0f, FALSE, TRUE);
	mouse (f, cx + 100.0f, cy + 30.0f, FALSE, FALSE);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);

	/* A right click without motion leaves the camera alone (it opens the
	 * context menu) and ends navigation. */
	{
		g_autofree gchar *still = NULL;
		g_autofree gchar *state = NULL;

		g_free (before);
		before = view_json (f);
		mouse (f, cx, cy, FALSE, TRUE);
		mouse (f, cx, cy, FALSE, FALSE);
		still = view_json (f);
		g_assert_cmpstr (before, ==, still);
		state = lrg_editor_server_dispatch (lrg_editor_app_get_server (f->app),
		                                    "{\"id\":1,\"method\":\"ui.state\"}");
		g_assert_nonnull (strstr (state, "\"nav\":\"none\""));
	}
}

static void
test_app_nav_gizmo (Fixture       *f,
                    gconstpointer  data)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	gfloat            vx, vy, vw, vh, gx, gy;
	gint              i;

	new_project (f);
	frames (f, 2);
	viewport_rect (f, &vx, &vy, &vw, &vh);
	gx = vx + 46.0f + 3.0f;
	gy = vy + vh - 46.0f + 3.0f;

	/* Dragging the orientation gizmo orbits... */
	before = view_json (f);
	mouse (f, gx, gy, FALSE, FALSE);
	mouse (f, gx, gy, TRUE, FALSE);
	for (i = 1; i <= 8; i++)
		mouse (f, gx + i * 6.0f, gy, TRUE, FALSE);
	mouse (f, gx + 48.0f, gy, FALSE, FALSE);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);

	/* ...and never selects or box-selects. */
	g_assert_cmpuint (n_selected (f), ==, 0);
}

static void
test_app_nav_keys_and_auto_perspective (Fixture       *f,
                                        gconstpointer  data)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;

	new_project (f);
	frames (f, 2);

	/* Num7: top view, orthographic. */
	press (f, LRG_GUI_KEY_KP_7, LRG_GUI_MOD_NONE);
	g_assert_true (view_ortho (f));

	/* Num4 orbits and returns to perspective. */
	before = view_json (f);
	press (f, LRG_GUI_KEY_KP_4, LRG_GUI_MOD_NONE);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);
	g_assert_false (view_ortho (f));

	/* Num5 toggles ortho explicitly; orbiting then keeps it. */
	press (f, LRG_GUI_KEY_KP_5, LRG_GUI_MOD_NONE);
	g_assert_true (view_ortho (f));
	press (f, LRG_GUI_KEY_KP_6, LRG_GUI_MOD_NONE);
	g_assert_true (view_ortho (f));
}

static void
test_app_nav_schemes (Fixture       *f,
                      gconstpointer  data)
{
	LrgEditorSettings *settings = lrg_editor_app_get_settings (f->app);
	g_autofree gchar  *a = NULL;
	g_autofree gchar  *b = NULL;
	g_autofree gchar  *c = NULL;
	gfloat             vx, vy, vw, vh, cx, cy;
	gint               i;

	new_project (f);
	frames (f, 2);
	viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;
	mouse (f, cx, cy, FALSE, FALSE);

	/* Trackpad: two-finger scroll orbits rather than zooming. */
	g_object_set (settings, "camera-navigation", "trackpad", NULL);
	a = view_json (f);
	f->input.wheel_x = 2.0f;
	f->input.wheel_y = 1.0f;
	frame (f);
	f->input.wheel_x = 0.0f;
	f->input.wheel_y = 0.0f;
	b = view_json (f);
	g_assert_cmpstr (a, !=, b);

	/* Blender: middle drag orbits. */
	g_object_set (settings, "camera-navigation", "blender", NULL);
	mouse3 (f, cx, cy, FALSE, TRUE, FALSE);
	for (i = 1; i <= 8; i++)
		mouse3 (f, cx + i * 8.0f, cy, FALSE, TRUE, FALSE);
	mouse3 (f, cx + 64.0f, cy, FALSE, FALSE, FALSE);
	c = view_json (f);
	g_assert_cmpstr (b, !=, c);
}

#endif /* LRG_BUILD_EDITOR_UI */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#ifdef LRG_BUILD_EDITOR_UI
#define ADD(path, func) \
	g_test_add ("/editor-app/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	ADD ("frames-empty", test_app_frames_empty);
	ADD ("new-project", test_app_new_project);
	ADD ("actions", test_app_actions);
	ADD ("shortcuts", test_app_shortcuts);
	ADD ("palette", test_app_palette);
	ADD ("dialogs", test_app_dialogs);
	ADD ("play-restores", test_app_play_restores);
	ADD ("save", test_app_save);
	ADD ("live-reload", test_app_live_reload);
	ADD ("ipc", test_app_ipc);
	ADD ("quit", test_app_quit_confirm);
	ADD ("menubar-click", test_app_menubar_click);
	ADD ("viewport-rotate", test_app_viewport_rotate);
	ADD ("nav-gizmo", test_app_nav_gizmo);
	ADD ("nav-keys", test_app_nav_keys_and_auto_perspective);
	ADD ("nav-schemes", test_app_nav_schemes);
#undef ADD
#endif

	return g_test_run ();
}
