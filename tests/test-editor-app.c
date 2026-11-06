/* test-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgEditorApp driven headless, frame by frame, through its
 * canvas (tests/editor-app-fixture.h): projects and levels, actions and
 * shortcuts, the menu bar and dialogs, play mode restoring the level,
 * save and dirty state, live reload of a level edited outside the
 * editor, viewport navigation, tile painting, timeline preview,
 * thumbnails, the app's IPC methods (ui.state schema included), the
 * deprecated LrgGuiInput entry point, and the headless IPC methods for
 * asset sidecars, git status and linked prefabs.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <libregnum.h>

#ifdef LRG_BUILD_EDITOR_UI

#include "editor-app-fixture.h"

typedef EditorAppFixture Fixture;

#define CTRL  LRG_INPUT_MODIFIER_CTRL
#define ALT   LRG_INPUT_MODIFIER_ALT
#define SHIFT LRG_INPUT_MODIFIER_SHIFT
#define NOMOD LRG_INPUT_MODIFIER_NONE

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

/* The camera as JSON, to compare before/after. */
static gchar *
view_json (Fixture *f)
{
	g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'view.get'}");

	return g_strdup (strstr (reply, "\"result\"") != NULL ? strstr (reply, "\"result\"") : reply);
}

static gboolean
view_ortho (Fixture *f)
{
	g_autofree gchar *v = view_json (f);

	return strstr (v, "\"orthographic\":true") != NULL;
}

/* The dialog ui.state reports, or NULL. */
static gchar *
ui_dialog (Fixture *f)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");
	JsonNode           *d = json_object_get_member (json_node_get_object (r), "dialog");

	return JSON_NODE_HOLDS_NULL (d) ? NULL : g_strdup (json_node_get_string (d));
}

static gint64
efx_ipc_int (Fixture     *f,
             const gchar *request,
             const gchar *member)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, request);

	return json_object_get_int_member (json_node_get_object (r), member);
}

/* Location component @axis of node @name. */
static gdouble
node_loc (Fixture     *f,
          const gchar *name,
          guint        axis)
{
	LrgNode *node = lrg_editor_find_node (f->editor, name);

	g_assert_nonnull (node);
	return axis == 0 ? lrg_node_get_location (node)->x
	       : axis == 1 ? lrg_node_get_location (node)->y : lrg_node_get_location (node)->z;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

static void
test_app_frames_empty (Fixture       *f,
                       gconstpointer  data)
{
	LrgCanvasInput input;

	/* An empty editor survives many frames and resizes */
	efx_frames (f, 5);
	lrg_canvas_input_init (&input);
	input.delta_time = EFX_DT;
	lrg_editor_app_process_frame (f->app, &input, 400, 300);
	efx_frames (f, 2);
	g_assert_cmpuint (n_nodes (f), ==, 0);
	g_assert_false (lrg_editor_app_should_quit (f->app));

	/* The deprecated GUI context is gone; the canvas and window exist */
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	g_assert_null (lrg_editor_app_get_gui (f->app));
	G_GNUC_END_IGNORE_DEPRECATIONS
	g_assert_true (LRG_IS_CANVAS (lrg_editor_app_get_canvas (f->app)));
	g_assert_true (LRG_IS_EDITOR_WINDOW (lrg_editor_app_get_window (f->app)));
	g_assert_nonnull (lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "viewport"));
	g_assert_nonnull (lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "hierarchy"));
	g_assert_nonnull (lrg_editor_app_get_view_state (f->app));
	g_assert_nonnull (lrg_editor_app_get_viewport_nav (f->app));
	g_assert_nonnull (lrg_editor_app_get_play_session (f->app));
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
	efx_frames (f, 3);
}

static void
test_app_actions (Fixture       *f,
                  gconstpointer  data)
{
	new_project (f);
	efx_frames (f, 2);

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
	efx_frames (f, 2);
	g_assert_true (lrg_editor_app_activate_action (f->app, "node.ungroup"));
	g_assert_cmpuint (lrg_node_get_n_children (lrg_level_get_root (lrg_editor_get_level (f->editor))), ==, 3);
	g_assert_cmpuint (n_nodes (f), ==, 3);
	efx_frames (f, 2);

	/* View actions keep working (frame-all used to free the nodes). */
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.frame-all"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.frame-all"));
	efx_frames (f, 2);
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
	efx_frames (f, 2);

	/* Ctrl+A selects all, Delete deletes, Ctrl+Z undoes. */
	efx_key (f, GRL_KEY_A, CTRL);
	g_assert_cmpuint (n_selected (f), ==, 3);
	efx_key (f, GRL_KEY_DELETE, NOMOD);
	g_assert_cmpuint (n_nodes (f), ==, 0);
	efx_key (f, GRL_KEY_Z, CTRL);
	g_assert_cmpuint (n_nodes (f), ==, 3);

	/* A user rebinding takes effect at once. */
	lrg_editor_settings_set_keybinding (lrg_editor_app_get_settings (f->app), "tool.scale", "Ctrl+Alt+K");
	efx_key (f, GRL_KEY_K, CTRL | ALT);
	g_assert_cmpint (lrg_editor_get_tool (f->editor), ==, LRG_EDITOR_TOOL_SCALE);
}

static void
test_app_play_restores (Fixture       *f,
                        gconstpointer  data)
{
	LrgNode *ground;
	guint    undo;

	new_project (f);
	efx_frames (f, 2);
	ground = node_named (f, "Ground");
	g_assert_nonnull (ground);
	undo = lrg_editor_get_undo_count (f->editor);

	g_assert_true (lrg_editor_app_activate_action (f->app, "play.start"));
	g_assert_true (lrg_editor_app_is_playing (f->app));
	efx_frames (f, 5);

	/* Edits while playing are discarded on stop but stay redoable. */
	lrg_editor_rename_node (f->editor, ground, "Changed");
	lrg_node_set_location_xyz (ground, 5.0f, 6.0f, 7.0f);
	efx_frames (f, 2);

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
	efx_frames (f, 2);
	lrg_editor_rename_node (f->editor, node_named (f, "Camera"), "MainCamera");
	g_assert_true (lrg_editor_is_dirty (f->editor));

	efx_key (f, GRL_KEY_S, CTRL);
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
	efx_frames (f, 2);
	path = g_strdup (lrg_editor_get_path (f->editor));

	/* Someone (Emacs) rewrites the YAML; a clean document reloads. */
	g_assert_true (g_file_set_contents (path, edited, -1, NULL));
	deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
	while (n_nodes (f) != 1 && g_get_monotonic_time () < deadline)
	{
		g_main_context_iteration (NULL, FALSE);
		efx_frame (f);
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
	efx_frames (f, 2);

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
	efx_frames (f, 2);

	/* Headless, a quit request with unsaved work quits (no dialog). */
	lrg_editor_rename_node (f->editor, node_named (f, "Sun"), "Moon");
	lrg_editor_app_request_quit (f->app);
	efx_frame (f);
	g_assert_true (lrg_editor_app_should_quit (f->app));
}


/* Moves the pointer and sets button states for the next frame. */
static void
test_app_tile_paint (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	gfloat              vx, vy, vw, vh, cx, cy, px;
	guint               undo0;
	gint                i;

	new_project (f);
	efx_frames (f, 2);

	/* A 8x8 tilemap at the origin, the 2D view centred on cell (4, 4). */
	r = efx_ipc_result (f, "{'id':1,'method':'node.create','params':{'name':'TileMap','kind':'tilemap'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'tilemap.configure','params':{'node':'TileMap','width':8,"
	                   "'height':8}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':3,'method':'selection.set','params':{'nodes':['TileMap']}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':4,'method':'view.set','params':{'mode_2d':true}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':5,'method':'view.set','params':{'position':[4.5,-4.5,50],"
	                   "'target':[4.5,-4.5,0],'ortho_height':20}}");
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 2);

	/* B turns paint mode on (and shows the Tiles panel). */
	efx_key (f, GRL_KEY_B, NOMOD);
	efx_frames (f, 1);
	r = efx_ipc_result (f, "{'id':6,'method':'tilemap.tool','params':{'tile':3}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "mode"));
	g_clear_pointer (&r, json_node_unref);

	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;
	px = vh / 20.0f;   /* pixels per world unit (ortho height 20) */

	/* Hovering reports the cell under the pointer. */
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	r = efx_ipc_result (f, "{'id':7,'method':'tilemap.tool'}");
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "hover"), 0),
	                 ==, 4);
	g_clear_pointer (&r, json_node_unref);

	/* A drag three cells right is one stroke and one undo step. */
	undo0 = lrg_editor_get_undo_count (f->editor);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	for (i = 1; i <= 6; i++)
		efx_button (f, cx + px * 0.5f * i, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, cx + px * 3.0f, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpint (efx_ipc_int (f, "{'id':8,'method':'tilemap.pick','params':{'node':'TileMap','x':4,"
	                             "'y':4}}", "tile"), ==, 3);
	g_assert_cmpint (efx_ipc_int (f, "{'id':9,'method':'tilemap.pick','params':{'node':'TileMap','x':7,"
	                             "'y':4}}", "tile"), ==, 3);
	g_assert_cmpint (efx_ipc_int (f, "{'id':9,'method':'tilemap.pick','params':{'node':'TileMap','x':4,"
	                             "'y':5}}", "tile"), ==, 0);
	/* Painting did not change the selection or move the node. */
	g_assert_cmpuint (n_selected (f), ==, 1);
	g_assert_cmpfloat (node_loc (f, "TileMap", 0), ==, 0.0);

	/* Fill tool: one click floods the empty area. */
	r = efx_ipc_result (f, "{'id':10,'method':'tilemap.tool','params':{'tool':'fill','tile':5}}");
	g_clear_pointer (&r, json_node_unref);
	efx_button (f, cx, cy + px * 2.0f, GRL_MOUSE_BUTTON_LEFT, FALSE);
	efx_button (f, cx, cy + px * 2.0f, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, cx, cy + px * 2.0f, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpint (efx_ipc_int (f, "{'id':11,'method':'tilemap.pick','params':{'node':'TileMap','x':0,"
	                             "'y':0}}", "tile"), ==, 5);

	/* Ctrl + click picks; undo the fill, then the stroke. */
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	f->mods = CTRL;
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	f->mods = NOMOD;
	r = efx_ipc_result (f, "{'id':12,'method':'tilemap.tool'}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "tile"), ==, 3);
	g_clear_pointer (&r, json_node_unref);

	efx_key (f, GRL_KEY_Z, CTRL);
	g_assert_cmpint (efx_ipc_int (f, "{'id':13,'method':'tilemap.pick','params':{'node':'TileMap','x':0,"
	                             "'y':0}}", "tile"), ==, 0);
	efx_key (f, GRL_KEY_Z, CTRL);
	g_assert_cmpint (efx_ipc_int (f, "{'id':14,'method':'tilemap.pick','params':{'node':'TileMap','x':5,"
	                             "'y':4}}", "tile"), ==, 0);

	/* Escape leaves paint mode; clicks select again. */
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 1);
	r = efx_ipc_result (f, "{'id':15,'method':'tilemap.tool'}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "mode"));
}

static void
test_app_timeline (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *saved = NULL;
	g_autoptr(GError)   error = NULL;
	gint                i;

	new_project (f);
	efx_frames (f, 2);
	r = efx_ipc_result (f, "{'id':1,'method':'node.create','params':{'name':'Mover','kind':'primitive'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'selection.set','params':{'nodes':['Mover']}}");
	g_clear_pointer (&r, json_node_unref);

	/* I keys the selection's transform into a fresh timeline. */
	efx_key (f, GRL_KEY_I, NOMOD);
	r = efx_ipc_result (f, "{'id':3,'method':'timeline.list'}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (r)), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':4,'method':'timeline.key','params':{'name':'Timeline','node':'Mover',"
	                   "'time':2,'channel':'location.x','value':10}}");
	g_clear_pointer (&r, json_node_unref);

	/* Scrubbing previews without touching history... */
	r = efx_ipc_result (f, "{'id':5,'method':'timeline.seek','params':{'time':1}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "previewing"));
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpfloat_with_epsilon (node_loc (f, "Mover", 0), 5.0, 1e-4);

	/* ...and saving writes the rest pose, not the preview. */
	r = efx_ipc_result (f, "{'id':6,'method':'level.save','params':{'path':'levels/anim.rlevel'}}");
	g_assert_true (g_file_get_contents (json_node_get_string (r), &saved, NULL, &error));
	g_clear_pointer (&r, json_node_unref);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (saved, "timelines:"));
	g_assert_cmpfloat_with_epsilon (node_loc (f, "Mover", 0), 0.0, 1e-4);

	/* Playback advances with frames and loops when asked. */
	r = efx_ipc_result (f, "{'id':7,'method':'timeline.set','params':{'name':'Timeline','loop':true}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'level.save'}");
	g_clear_pointer (&r, json_node_unref);
	g_assert_false (lrg_editor_is_dirty (f->editor));
	r = efx_ipc_result (f, "{'id':8,'method':'timeline.play'}");
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 30);
	r = efx_ipc_result (f, "{'id':9,'method':'timeline.state'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_assert_cmpfloat (json_object_get_double_member (json_node_get_object (r), "time"), >, 0.3);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpfloat (node_loc (f, "Mover", 0), >, 1.0);
	r = efx_ipc_result (f, "{'id':10,'method':'timeline.stop'}");
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpfloat_with_epsilon (node_loc (f, "Mover", 0), 0.0, 1e-4);
	g_assert_false (lrg_editor_is_dirty (f->editor));

	/* Autoplay timelines run during play-in-editor and are undone by Stop. */
	r = efx_ipc_result (f, "{'id':11,'method':'timeline.set','params':{'name':'Timeline','autoplay':true}}");
	g_clear_pointer (&r, json_node_unref);
	efx_key (f, GRL_KEY_F5, NOMOD);
	for (i = 0; i < 40; i++)
		efx_frame (f);
	g_assert_cmpfloat (node_loc (f, "Mover", 0), >, 1.0);
	efx_key (f, GRL_KEY_F8, NOMOD);
	g_assert_cmpfloat_with_epsilon (node_loc (f, "Mover", 0), 0.0, 1e-4);

	/* Panel open with the timeline: frames render it without trouble. */
	r = efx_ipc_result (f, "{'id':12,'method':'action.run','params':{'id':'timeline.show-panel'}}");
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 3);
}

static void
test_app_thumbnails (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *png = NULL;
	g_autofree gchar   *png2 = NULL;
	g_autofree gchar   *first = NULL;
	GrlColor            c = { 200, 40, 40, 255 };
	g_autoptr(GrlImage) img = NULL;
	gint                i;

	new_project (f);
	efx_frames (f, 2);
	png = g_build_filename (f->project, "crate.png", NULL);
	png2 = g_build_filename (f->project, "barrel.png", NULL);
	img = grl_image_new_color (64, 32, &c);
	g_assert_true (grl_image_export (img, png));
	g_assert_true (grl_image_export (img, png2));

	/* Synchronous generation lands in the project's .cache. */
	r = efx_ipc_result (f, "{'id':1,'method':'asset.thumbnail','params':{'path':'crate.png','wait':true}}");
	first = g_strdup (json_object_get_string_member (json_node_get_object (r), "thumbnail"));
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (strstr (first, ".cache/thumbnails/"));
	g_assert_true (g_file_test (first, G_FILE_TEST_IS_REGULAR));

	/* Asynchronous: pending first, ready after a few frames. */
	r = efx_ipc_result (f, "{'id':2,'method':'asset.thumbnail','params':{'path':'barrel.png'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "pending"));
	g_clear_pointer (&r, json_node_unref);
	for (i = 0; i < 200; i++)
	{
		gboolean pending;

		efx_frame (f);
		r = efx_ipc_result (f, "{'id':3,'method':'asset.thumbnail','params':{'path':'barrel.png'}}");
		pending = json_object_get_boolean_member (json_node_get_object (r), "pending");
		g_clear_pointer (&r, json_node_unref);
		if (!pending)
			break;
		g_usleep (5000);
	}
	g_assert_cmpint (i, <, 200);

	/* Live reload: rewriting the file drops its old thumbnail. */
	g_usleep (20000);
	g_clear_object (&img);
	img = grl_image_new_color (16, 16, &c);
	g_assert_true (grl_image_export (img, png));
	for (i = 0; i < 300 && g_file_test (first, G_FILE_TEST_EXISTS); i++)
	{
		efx_frame (f);
		g_usleep (5000);
	}
	g_assert_false (g_file_test (first, G_FILE_TEST_EXISTS));

	/* Unsupported kinds are an error; models need a window. */
	{
		g_autofree gchar *txt = g_build_filename (f->project, "notes.txt", NULL);
		g_autofree gchar *reply = NULL;

		g_assert_true (g_file_set_contents (txt, "hi", -1, NULL));
		reply = efx_ipc (f, "{'id':4,'method':'asset.thumbnail','params':{'path':'notes.txt'}}");
		g_assert_nonnull (strstr (reply, "\"error\""));
	}
}

static void
test_app_palette (Fixture       *f,
                  gconstpointer  data)
{
	/* Fuzzy action search runs "Select All" */
	new_project (f);
	efx_frames (f, 2);
	lrg_editor_select (f->editor, NULL, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	efx_frames (f, 2);
	efx_type (f, "select all");
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	efx_frames (f, 1);
	g_assert_cmpuint (n_selected (f), ==, 3);

	/* "@name" jumps to a node */
	lrg_editor_select (f->editor, NULL, FALSE);
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.palette"));
	efx_frames (f, 2);
	efx_type (f, "@camera");
	efx_key (f, GRL_KEY_ENTER, NOMOD);
	efx_frames (f, 1);
	g_assert_cmpuint (n_selected (f), ==, 1);
	g_assert_cmpstr (lrg_node_get_name (lrg_editor_selection_get_primary (lrg_editor_get_selection (f->editor))),
	                 ==, "Camera");
}

static void
test_app_dialogs (Fixture       *f,
                  gconstpointer  data)
{
	static const struct { const gchar *action; const gchar *dialog; } cases[] = {
		{ "file.project-settings", "project-settings" },
		{ "help.about", "about" },
		{ "help.shortcuts", "shortcuts" },
		{ "file.open-level", "file" },
		{ "file.save-as", "file" },
		{ "file.open-project", "file" },
		{ "file.new-project", "new-project" },
		{ "node.attach-script", "new-script" },
	};
	gsize i;

	new_project (f);
	efx_frames (f, 2);

	/* Every dialog opens with the 0.8.0 id, draws a few frames and closes
	 * on Escape (a second Escape is harmless) */
	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		g_autofree gchar *open = NULL;
		g_autofree gchar *closed = NULL;

		if (g_strcmp0 (cases[i].action, "node.attach-script") == 0)
			lrg_editor_app_activate_action (f->app, "edit.select-all");
		g_assert_true (lrg_editor_app_activate_action (f->app, cases[i].action));
		efx_frames (f, 3);
		open = ui_dialog (f);
		g_assert_cmpstr (open, ==, cases[i].dialog);

		/* Shortcuts do not fire behind a dialog */
		lrg_editor_select (f->editor, NULL, FALSE);
		efx_key (f, GRL_KEY_A, CTRL);
		g_assert_cmpuint (n_selected (f), ==, 0);

		efx_key (f, GRL_KEY_ESCAPE, NOMOD);
		efx_frames (f, 1);
		efx_key (f, GRL_KEY_ESCAPE, NOMOD);
		efx_frames (f, 30);
		closed = ui_dialog (f);
		g_assert_null (closed);
	}

	/* Preferences: a real dialog, Escape closes it */
	g_assert_true (lrg_editor_app_activate_action (f->app, "edit.preferences"));
	efx_frames (f, 3);
	{
		g_autofree gchar *p = ui_dialog (f);

		g_assert_cmpstr (p, ==, "preferences");
	}
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frames (f, 30);
	g_assert_null (ui_dialog (f));

	/* Shortcuts work again once no dialog is open */
	lrg_editor_select (f->editor, NULL, FALSE);
	efx_key (f, GRL_KEY_A, CTRL);
	g_assert_cmpuint (n_selected (f), ==, 3);

	/* Unsaved changes ask before quitting... (with a window only; the
	 * confirm dialog is opened directly here) */
}

static void
test_app_menubar_click (Fixture       *f,
                        gconstpointer  data)
{
	g_autoptr(JsonNode) state = NULL;
	JsonObject         *o;
	JsonArray          *file;
	gfloat              fx, fy;
	guint               i;
	static const gchar *titles[] = { "File", "Edit", "Create", "Node", "View", "Play", "Tools", "Help" };

	new_project (f);
	efx_frames (f, 2);

	/* ui.state keeps its 0.8.0 schema: menubar rects by label, frame
	 * height, viewport rect, dialog, nav, input echo, fps... */
	state = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");
	o = json_node_get_object (state);
	for (i = 0; i < G_N_ELEMENTS (titles); i++)
		g_assert_true (json_object_has_member (json_object_get_object_member (o, "menubar"), titles[i]));
	g_assert_cmpfloat (json_object_get_double_member (o, "frame_height"), >, 0.0);
	g_assert_true (json_object_has_member (o, "viewport"));
	g_assert_true (json_object_has_member (o, "dialog"));
	g_assert_cmpstr (json_object_get_string_member (o, "nav"), ==, "none");
	g_assert_true (json_object_has_member (o, "input"));
	g_assert_true (json_object_has_member (o, "fps"));
	g_assert_true (JSON_NODE_HOLDS_NULL (json_object_get_member (o, "menu")));
	/* ...plus the 0.8.1 fields */
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "viewports")), >=, 1);
	g_assert_cmpint (json_object_get_int_member (o, "active_viewport"), ==, 0);
	g_assert_cmpstr (json_object_get_string_member (o, "layout"), ==, "single");

	file = json_object_get_array_member (json_object_get_object_member (o, "menubar"), "File");
	fx = (gfloat) (json_array_get_double_element (file, 0) + json_array_get_double_element (file, 2) * 0.5);
	fy = (gfloat) (json_array_get_double_element (file, 1) + json_array_get_double_element (file, 3) * 0.5);
	g_assert_cmpfloat (json_array_get_double_element (file, 2), >, 0.0);

	/* Press opens, release keeps it open, clicking the header closes it */
	efx_move (f, fx, fy);
	efx_button (f, fx, fy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	g_assert_cmpstr (efx_open_menu (f), ==, "File");
	efx_button (f, fx, fy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	efx_frames (f, 2);
	g_assert_cmpstr (efx_open_menu (f), ==, "File");
	{
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':1,'method':'ui.state'}");

		g_assert_cmpstr (json_object_get_string_member (json_node_get_object (s), "menu"), ==, "File");
	}
	efx_click (f, fx, fy);
	efx_frames (f, 2);
	g_assert_null (efx_open_menu (f));

	/* Alt + letter opens a menu from the keyboard; Escape closes it */
	efx_move (f, 700.0f, 500.0f);
	efx_key (f, GRL_KEY_V, ALT);
	efx_frame (f);
	g_assert_cmpstr (efx_open_menu (f), ==, "View");
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frame (f);
	g_assert_null (efx_open_menu (f));
	efx_key (f, GRL_KEY_F, ALT);
	efx_frame (f);
	g_assert_cmpstr (efx_open_menu (f), ==, "File");
	efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	efx_frame (f);

	/* Ctrl+Alt+E is an action, not the Edit menu */
	efx_key (f, GRL_KEY_E, CTRL | ALT);
	efx_frame (f);
	g_assert_null (efx_open_menu (f));

	/* A menu item click runs its action: Play > Play in Editor, the first
	 * row, one row height under the title (as editor-x11-smoke.sh does) */
	{
		JsonArray *play = json_object_get_array_member (json_object_get_object_member (o, "menubar"), "Play");
		gfloat     px = (gfloat) (json_array_get_double_element (play, 0) + json_array_get_double_element (play, 2) * 0.5);
		gfloat     py = (gfloat) (json_array_get_double_element (play, 1) + json_array_get_double_element (play, 3) * 0.5);
		gfloat     rowh = (gfloat) json_object_get_double_member (o, "frame_height");

		efx_click (f, px, py);
		efx_frames (f, 2);
		g_assert_cmpstr (efx_open_menu (f), ==, "Play");
		efx_click (f, px + 10.0f, py + rowh + 12.0f);
		efx_frames (f, 2);
		g_assert_true (lrg_editor_app_is_playing (f->app));
		g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	}

	/* Toolbar: the Move button runs tool.move and shows it checked */
	efx_click_widget (f, lrg_editor_window_get_widget (lrg_editor_app_get_window (f->app), "tool:tool.move"));
	efx_frame (f);
	g_assert_cmpint (lrg_editor_get_tool (f->editor), ==, LRG_EDITOR_TOOL_TRANSLATE);
	g_assert_true (lrg_widget_get_state_flags (lrg_editor_window_get_widget (lrg_editor_app_get_window (f->app),
	                                                                         "tool:tool.move"))
	               & LRG_WIDGET_STATE_CHECKED);
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
	efx_frames (f, 2);
	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;

	/* Right drag flies the camera (look) */
	before = view_json (f);
	efx_move (f, cx, cy);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_RIGHT, TRUE);
	for (i = 1; i <= 10; i++)
		efx_move (f, cx + i * 10.0f, cy + i * 3.0f);
	efx_button (f, cx + 100.0f, cy + 30.0f, GRL_MOUSE_BUTTON_RIGHT, FALSE);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);

	/* A right click without motion leaves the camera alone (it opens the
	 * context menu) and ends navigation */
	{
		g_autofree gchar *still = NULL;
		g_autofree gchar *state = NULL;

		g_free (before);
		before = view_json (f);
		efx_button (f, cx, cy, GRL_MOUSE_BUTTON_RIGHT, TRUE);
		efx_button (f, cx, cy, GRL_MOUSE_BUTTON_RIGHT, FALSE);
		still = view_json (f);
		g_assert_cmpstr (before, ==, still);
		state = efx_ipc (f, "{'id':1,'method':'ui.state'}");
		g_assert_nonnull (strstr (state, "\"nav\":\"none\""));
		g_assert_cmpuint (lrg_canvas_get_popup_count (lrg_editor_app_get_canvas (f->app)), >=, 1);
		efx_key (f, GRL_KEY_ESCAPE, NOMOD);
	}

	/* A left click on a node selects it; on empty space clears */
	{
		g_autoptr(JsonNode) r = NULL;

		r = efx_ipc_result (f, "{'id':2,'method':'view.set','params':{'position':[2,6,14],'target':[0,0,0]}}");
		efx_frame (f);
		efx_click (f, cx, cy);
		efx_frame (f);
		g_assert_cmpuint (n_selected (f), ==, 1);
		efx_click (f, cx, vy + 2.0f);
		efx_frame (f);
		g_assert_cmpuint (n_selected (f), ==, 0);
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
	efx_frames (f, 2);
	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	gx = vx + LRG_EDITOR_NAV_GIZMO_MARGIN + 3.0f;
	gy = vy + vh - LRG_EDITOR_NAV_GIZMO_MARGIN + 3.0f;

	/* Dragging the orientation gizmo orbits... */
	before = view_json (f);
	efx_move (f, gx, gy);
	efx_button (f, gx, gy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	for (i = 1; i <= 8; i++)
		efx_move (f, gx + i * 6.0f, gy);
	efx_button (f, gx + 48.0f, gy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);

	/* ...and never selects or box-selects */
	g_assert_cmpuint (n_selected (f), ==, 0);
}

static void
test_app_nav_keys_and_auto_perspective (Fixture       *f,
                                        gconstpointer  data)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;

	new_project (f);
	efx_frames (f, 2);

	/* Num7: top view, orthographic */
	efx_key (f, GRL_KEY_KP_7, NOMOD);
	g_assert_true (view_ortho (f));

	/* Num4 orbits and returns to perspective */
	before = view_json (f);
	efx_key (f, GRL_KEY_KP_4, NOMOD);
	after = view_json (f);
	g_assert_cmpstr (before, !=, after);
	g_assert_false (view_ortho (f));

	/* Num5 toggles ortho explicitly; orbiting then keeps it */
	efx_key (f, GRL_KEY_KP_5, NOMOD);
	g_assert_true (view_ortho (f));
	efx_key (f, GRL_KEY_KP_6, NOMOD);
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
	efx_frames (f, 2);
	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;
	efx_move (f, cx, cy);

	/* Trackpad: two-finger scroll orbits rather than zooming */
	g_object_set (settings, "camera-navigation", "trackpad", NULL);
	a = view_json (f);
	f->wheel_x = 2.0f;
	f->wheel_y = 1.0f;
	efx_frame (f);
	b = view_json (f);
	g_assert_cmpstr (a, !=, b);

	/* Blender: middle drag orbits */
	g_object_set (settings, "camera-navigation", "blender", NULL);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_MIDDLE, TRUE);
	for (i = 1; i <= 8; i++)
		efx_move (f, cx + i * 8.0f, cy);
	efx_button (f, cx + 64.0f, cy, GRL_MOUSE_BUTTON_MIDDLE, FALSE);
	c = view_json (f);
	g_assert_cmpstr (b, !=, c);
}

/* The deprecated 0.8.0 entry point: an LrgGuiInput click on the File
 * title opens the File menu, Escape (a key press, then its release)
 * closes it. */
static void
test_app_legacy_frame (Fixture       *f,
                       gconstpointer  data)
{
	LrgMenuBar  *bar = lrg_editor_window_get_menu_bar (lrg_editor_app_get_window (f->app));
	LrgGuiInput  input;
	gfloat       x, y, w, h;
	gint         i;

	lrg_gui_input_init (&input);
	input.delta = EFX_DT;
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	for (i = 0; i < 2; i++)
		lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	g_assert_true (lrg_menu_bar_get_title_rect (bar, 0, &x, &y, &w, &h));
	input.mouse_x = x + w * 0.5f;
	input.mouse_y = y + h * 0.5f;
	lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	input.mouse_down[LRG_GUI_MOUSE_LEFT] = TRUE;
	lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	input.mouse_down[LRG_GUI_MOUSE_LEFT] = FALSE;
	lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	g_assert_cmpstr (efx_open_menu (f), ==, "File");

	lrg_gui_input_add_key (&input, LRG_GUI_KEY_ESCAPE, LRG_GUI_MOD_NONE);
	lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	lrg_gui_input_new_frame (&input, EFX_DT);
	input.key_down[LRG_GUI_KEY_ESCAPE] = FALSE;
	lrg_editor_app_frame (f->app, &input, EFX_W, EFX_H);
	G_GNUC_END_IGNORE_DEPRECATIONS
	g_assert_null (efx_open_menu (f));

	/* The key table covers letters, digits, F-keys and the keypad */
	g_assert_cmpint (lrg_editor_gui_key_to_grl (LRG_GUI_KEY_A), ==, GRL_KEY_A);
	g_assert_cmpint (lrg_editor_gui_key_to_grl (LRG_GUI_KEY_9), ==, GRL_KEY_NINE);
	g_assert_cmpint (lrg_editor_gui_key_to_grl (LRG_GUI_KEY_F10), ==, GRL_KEY_F10);
	g_assert_cmpint (lrg_editor_gui_key_to_grl (LRG_GUI_KEY_KP_7), ==, GRL_KEY_KP_7);
	g_assert_cmpint (lrg_editor_gui_key_to_grl (LRG_GUI_KEY_NONE), ==, GRL_KEY_NULL);
}

/* Custom panels join the dock and View > Panels. */
static void
test_app_add_panel (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgDockPanel) panel = lrg_dock_panel_new ("my-tool", "My Tool");
	LrgDockArea            *dock = lrg_editor_app_get_dock (f->app);

	lrg_editor_app_add_panel (f->app, panel, "tab:inspector");
	efx_frames (f, 2);
	g_assert_true (lrg_dock_area_get_panel (dock, "my-tool") == panel);

	/* View > Reset Layout keeps every panel docked */
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.reset-layout"));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_dock_area_get_panel (dock, "my-tool"));
	g_assert_nonnull (lrg_dock_area_get_panel (dock, "viewport"));
}

#endif /* LRG_BUILD_EDITOR_UI */

/* ==========================================================================
 * Headless IPC: asset.meta.*, vcs.status, prefab.* (v0.8.1)
 *
 * Self-contained: needs <glib.h>, <glib/gstdio.h>, <gio/gio.h>,
 * <string.h>, <json-glib/json-glib.h> and <libregnum.h>. Every test makes
 * its own LrgEditor + LrgEditorServer (no app, no window) and drives it
 * through lrg_editor_server_dispatch().
 *
 * Registration (inside main, outside any LRG_BUILD_EDITOR_UI guard — the
 * methods are headless):
 *
 *   g_test_add_func ("/editor-app/ipc-asset-meta", test_ipc_asset_meta);
 *   g_test_add_func ("/editor-app/ipc-vcs-status", test_ipc_vcs_status);
 *   g_test_add_func ("/editor-app/ipc-prefab-link", test_ipc_prefab_link);
 *   g_test_add_func ("/editor-app/ipc-prefab-save-syncs", test_ipc_prefab_save_syncs);
 * ========================================================================== */

/* Recursively removes @path (a file or a directory tree). */
static void
ipc_rm_rf (const gchar *path)
{
	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		g_autoptr(GDir) d = g_dir_open (path, 0, NULL);
		const gchar    *name;

		while (d != NULL && (name = g_dir_read_name (d)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			ipc_rm_rf (child);
		}
		g_rmdir (path);
	}
	else
		g_remove (path);
}

/* Writes @text to @dir/@name, asserting success. */
static void
ipc_write (const gchar *dir,
           const gchar *name,
           const gchar *text)
{
	g_autofree gchar *p = g_build_filename (dir, name, NULL);
	g_autoptr(GError) error = NULL;

	g_assert_true (g_file_set_contents (p, text, -1, &error));
	g_assert_no_error (error);
}

/* Dispatches one request; returns the parsed response object (full). */
static JsonNode *
ipc_call (LrgEditorServer *server,
          const gchar     *request)
{
	g_autofree gchar     *line = lrg_editor_server_dispatch (server, request);
	g_autoptr(JsonParser) parser = json_parser_new ();

	g_assert_nonnull (line);
	g_assert_true (json_parser_load_from_data (parser, line, -1, NULL));
	return json_node_copy (json_parser_get_root (parser));
}

/* Like ipc_call(), formatting the request with printf rules. */
static JsonNode *ipc_callf (LrgEditorServer *server, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

static JsonNode *
ipc_callf (LrgEditorServer *server,
           const gchar     *format,
           ...)
{
	va_list           args;
	g_autofree gchar *request = NULL;

	va_start (args, format);
	request = g_strdup_vprintf (format, args);
	va_end (args);
	return ipc_call (server, request);
}

/* The "result" member of @resp (transfer none), asserting no error. */
static JsonNode *
ipc_result (JsonNode *resp)
{
	JsonObject *o = json_node_get_object (resp);

	if (json_object_has_member (o, "error"))
		g_error ("unexpected IPC error: %s",
		         json_object_get_string_member (json_object_get_object_member (o, "error"), "message"));
	g_assert_true (json_object_has_member (o, "result"));
	return json_object_get_member (o, "result");
}

/* The result as an object (transfer none). */
static JsonObject *
ipc_obj (JsonNode *resp)
{
	JsonNode *r = ipc_result (resp);

	g_assert_true (JSON_NODE_HOLDS_OBJECT (r));
	return json_node_get_object (r);
}

/* Asserts that @resp is an error with @code ("invalid-params", ...). */
static void
ipc_assert_error (JsonNode    *resp,
                  const gchar *code)
{
	JsonObject *o = json_node_get_object (resp);

	g_assert_true (json_object_has_member (o, "error"));
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (o, "error"), "code"), ==, code);
}

/* Whether the "overrides" array of @o contains @key. */
static gboolean
ipc_has_override (JsonObject  *o,
                  const gchar *key)
{
	JsonArray *arr = json_object_get_array_member (o, "overrides");
	guint      i;

	for (i = 0; i < json_array_get_length (arr); i++)
		if (g_strcmp0 (json_array_get_string_element (arr, i), key) == 0)
			return TRUE;
	return FALSE;
}

/* Sets "visible" through the editor, so the change marks an override. */
static void
ipc_set_visible (LrgEditor *editor,
                 LrgNode   *node,
                 gboolean   visible)
{
	GValue v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, visible);
	lrg_editor_set_node_property (editor, node, "visible", &v);
	g_value_unset (&v);
}

/* ---- asset.meta.get / asset.meta.set ---- */

static void
test_ipc_asset_meta (void)
{
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("lrg-ipc-meta-XXXXXX", &error);
	g_autofree gchar          *asset = NULL;
	g_autofree gchar          *meta_path = NULL;
	g_autofree gchar          *legacy = lrg_asset_meta_compute_legacy_guid ("textures/brick.png");
	g_autoptr(LrgEditor)       editor = lrg_editor_new ();
	g_autoptr(LrgEditorServer) server = lrg_editor_server_new (editor);
	g_autofree gchar          *sub = NULL;
	g_autoptr(LrgAssetMeta)    on_disk = NULL;
	JsonNode                  *r;
	JsonObject                *o;
	JsonObject                *settings;

	g_assert_no_error (error);
	sub = g_build_filename (dir, "textures", NULL);
	g_assert_cmpint (g_mkdir_with_parents (sub, 0755), ==, 0);
	ipc_write (sub, "brick.png", "not really a png");
	asset = g_build_filename (dir, "textures", "brick.png", NULL);
	meta_path = g_strconcat (asset, ".meta", NULL);
	lrg_editor_server_set_project_dir (server, dir);

	/* No sidecar yet: described in memory with the 0.8.0 guid, nothing
	 * is written. */
	r = ipc_call (server, "{\"id\":1,\"method\":\"asset.meta.get\",\"params\":{\"path\":\"textures/brick.png\"}}");
	o = ipc_obj (r);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, asset);
	g_assert_false (json_object_get_boolean_member (o, "exists"));
	g_assert_cmpstr (json_object_get_string_member (o, "guid"), ==, legacy);
	g_assert_cmpstr (json_object_get_string_member (o, "importer"), ==, "texture");
	settings = json_object_get_object_member (o, "settings");
	g_assert_cmpstr (json_object_get_string_member (settings, "filter"), ==, "bilinear");
	g_assert_false (json_object_get_boolean_member (settings, "mipmaps"));
	json_node_unref (r);
	g_assert_false (g_file_test (meta_path, G_FILE_TEST_EXISTS));

	/* Valid settings are written and echoed back; the guid is kept. */
	r = ipc_call (server, "{\"id\":2,\"method\":\"asset.meta.set\",\"params\":{\"path\":\"textures/brick.png\","
	                      "\"settings\":{\"filter\":\"point\",\"max-size\":512,\"mipmaps\":true}}}");
	o = ipc_obj (r);
	g_assert_true (json_object_get_boolean_member (o, "exists"));
	g_assert_cmpstr (json_object_get_string_member (o, "guid"), ==, legacy);
	settings = json_object_get_object_member (o, "settings");
	g_assert_cmpstr (json_object_get_string_member (settings, "filter"), ==, "point");
	g_assert_cmpint (json_object_get_int_member (settings, "max-size"), ==, 512);
	g_assert_true (json_object_get_boolean_member (settings, "mipmaps"));
	json_node_unref (r);
	g_assert_true (g_file_test (meta_path, G_FILE_TEST_IS_REGULAR));
	on_disk = lrg_asset_meta_load_for_asset (asset, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_asset_meta_get_guid (on_disk), ==, legacy);
	g_assert_cmpint (lrg_asset_meta_get_texture_max_size (on_disk), ==, 512);
	g_clear_object (&on_disk);

	/* All or nothing: one bad value rejects the whole request. */
	r = ipc_call (server, "{\"id\":3,\"method\":\"asset.meta.set\",\"params\":{\"path\":\"textures/brick.png\","
	                      "\"settings\":{\"wrap\":\"clamp\",\"filter\":\"bogus\"}}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":4,\"method\":\"asset.meta.set\",\"params\":{\"path\":\"textures/brick.png\","
	                      "\"settings\":{\"max-size\":99999}}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":5,\"method\":\"asset.meta.get\",\"params\":{\"path\":\"textures/brick.png\"}}");
	settings = json_object_get_object_member (ipc_obj (r), "settings");
	g_assert_cmpstr (json_object_get_string_member (settings, "wrap"), ==, "repeat");
	g_assert_cmpstr (json_object_get_string_member (settings, "filter"), ==, "point");
	g_assert_cmpint (json_object_get_int_member (settings, "max-size"), ==, 512);
	json_node_unref (r);

	/* null resets to the default; "guid" replaces the identifier; the
	 * sidecar path itself is accepted as "path". */
	r = ipc_callf (server, "{\"id\":6,\"method\":\"asset.meta.set\",\"params\":{\"path\":\"%s\","
	                       "\"settings\":{\"filter\":null},\"guid\":\"0f0f0f0f-1111-4222-8333-444455556666\"}}",
	               meta_path);
	o = ipc_obj (r);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, asset);
	g_assert_cmpstr (json_object_get_string_member (o, "guid"), ==, "0f0f0f0f-1111-4222-8333-444455556666");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (o, "settings"), "filter"), ==,
	                 "bilinear");
	json_node_unref (r);

	/* Errors: no path, unknown asset, wrong settings type. */
	r = ipc_call (server, "{\"id\":7,\"method\":\"asset.meta.get\",\"params\":{}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":8,\"method\":\"asset.meta.get\",\"params\":{\"path\":\"nope.png\"}}");
	ipc_assert_error (r, "not-found");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":9,\"method\":\"asset.meta.set\",\"params\":{\"path\":\"textures/brick.png\","
	                      "\"settings\":[1,2]}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);

	ipc_rm_rf (dir);
}

/* ---- vcs.status ---- */

/* Runs git with @args in @dir; asserts it exits 0. */
static void
ipc_git (const gchar         *dir,
         const gchar * const *args)
{
	g_autoptr(GPtrArray) argv = g_ptr_array_new ();
	g_autoptr(GError)    error = NULL;
	gint                 status = 0;
	guint                i;

	/* No user/system config: no signing, hooks or identity surprises. */
	g_ptr_array_add (argv, (gpointer) "git");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "user.name=Test");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "user.email=test@example.invalid");
	g_ptr_array_add (argv, (gpointer) "-c");
	g_ptr_array_add (argv, (gpointer) "commit.gpgsign=false");
	for (i = 0; args[i] != NULL; i++)
		g_ptr_array_add (argv, (gpointer) args[i]);
	g_ptr_array_add (argv, NULL);

	g_assert_true (g_spawn_sync (dir, (gchar **) argv->pdata, NULL,
	                             G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
	                             NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_true (g_spawn_check_wait_status (status, NULL));
}

/* The "files" entry for @path in @o, or NULL. */
static JsonObject *
ipc_vcs_file (JsonObject  *o,
              const gchar *path)
{
	JsonArray *files = json_object_get_array_member (o, "files");
	guint      i;

	for (i = 0; i < json_array_get_length (files); i++)
	{
		JsonObject *e = json_array_get_object_element (files, i);

		if (g_strcmp0 (json_object_get_string_member (e, "path"), path) == 0)
			return e;
	}
	return NULL;
}

static void
test_ipc_vcs_status (void)
{
	static const gchar *const init[] = { "init", "-q", "-b", "main", NULL };
	static const gchar *const add[] = { "add", "tracked.txt", NULL };
	static const gchar *const commit[] = { "commit", "-q", "--no-verify", "-m", "initial", NULL };
	g_autofree gchar          *git = g_find_program_in_path ("git");
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *plain = NULL;
	g_autofree gchar          *repo = NULL;
	g_autoptr(LrgEditor)       editor = NULL;
	g_autoptr(LrgEditorServer) server = NULL;
	JsonNode                  *r;
	JsonObject                *o;
	JsonObject                *e;

	if (git == NULL)
	{
		g_test_skip ("git is not installed");
		return;
	}

	editor = lrg_editor_new ();
	server = lrg_editor_server_new (editor);

	/* Outside a work tree: not an error, just no repository. */
	plain = g_dir_make_tmp ("lrg-ipc-novcs-XXXXXX", &error);
	g_assert_no_error (error);
	lrg_editor_server_set_project_dir (server, plain);
	r = ipc_call (server, "{\"id\":1,\"method\":\"vcs.status\"}");
	o = ipc_obj (r);
	if (json_object_get_boolean_member (o, "repository"))
	{
		/* TMPDIR itself is inside a git work tree: cannot test this. */
		g_test_message ("temporary directory is inside a git repository");
	}
	else
	{
		g_assert_true (json_node_is_null (json_object_get_member (o, "branch")));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "files")), ==, 0);
	}
	json_node_unref (r);

	/* A repository with one modified and one untracked file. */
	repo = g_dir_make_tmp ("lrg-ipc-vcs-XXXXXX", &error);
	g_assert_no_error (error);
	ipc_git (repo, init);
	ipc_write (repo, "tracked.txt", "one\n");
	ipc_git (repo, add);
	ipc_git (repo, commit);
	ipc_write (repo, "tracked.txt", "two\n");
	ipc_write (repo, "new.txt", "new\n");

	/* Switching the project directory switches the tracker. */
	lrg_editor_server_set_project_dir (server, repo);
	r = ipc_call (server, "{\"id\":2,\"method\":\"vcs.status\",\"params\":{}}");
	o = ipc_obj (r);
	g_assert_true (json_object_get_boolean_member (o, "repository"));
	g_assert_cmpstr (json_object_get_string_member (o, "branch"), ==, "main");
	e = ipc_vcs_file (o, "tracked.txt");
	g_assert_nonnull (e);
	g_assert_cmpstr (json_object_get_string_member (e, "status"), ==, "modified");
	g_assert_cmpstr (json_object_get_string_member (e, "letter"), ==, "M");
	e = ipc_vcs_file (o, "new.txt");
	g_assert_nonnull (e);
	g_assert_cmpstr (json_object_get_string_member (e, "status"), ==, "untracked");
	g_assert_cmpstr (json_object_get_string_member (e, "letter"), ==, "U");
	json_node_unref (r);

	/* One path. */
	r = ipc_call (server, "{\"id\":3,\"method\":\"vcs.status\",\"params\":{\"path\":\"tracked.txt\"}}");
	o = ipc_obj (r);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, "tracked.txt");
	g_assert_cmpstr (json_object_get_string_member (o, "status"), ==, "modified");
	g_assert_cmpstr (json_object_get_string_member (o, "letter"), ==, "M");
	json_node_unref (r);

	/* A clean committed state after the edit is reverted. */
	ipc_write (repo, "tracked.txt", "one\n");
	r = ipc_call (server, "{\"id\":4,\"method\":\"vcs.status\",\"params\":{\"path\":\"tracked.txt\"}}");
	g_assert_cmpstr (json_object_get_string_member (ipc_obj (r), "status"), ==, "unmodified");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":5,\"method\":\"vcs.status\",\"params\":{\"path\":7}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);

	ipc_rm_rf (plain);
	ipc_rm_rf (repo);
}

/* ---- prefab.overrides / apply / revert / sync / relink / unlink ---- */

/* Saves a "Crate" prefab with a "Lid" child to @dir/crate.rprefab. */
static gchar *
ipc_make_crate (const gchar *dir)
{
	g_autoptr(LrgNode) root = lrg_node_new ("Crate");
	g_autoptr(LrgNode) lid = lrg_node_new ("Lid");
	g_autoptr(GError)  error = NULL;
	gchar             *path = g_build_filename (dir, "crate.rprefab", NULL);

	lrg_node_set_location_xyz (lid, 0.0f, 1.0f, 0.0f);
	lrg_node_add_child (root, lid);
	g_assert_true (lrg_prefab_save (root, path, &error));
	g_assert_no_error (error);
	return path;
}

static void
test_ipc_prefab_link (void)
{
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("lrg-ipc-prefab-XXXXXX", &error);
	g_autofree gchar          *path = NULL;
	g_autoptr(LrgEditor)       editor = lrg_editor_new ();
	g_autoptr(LrgEditorServer) server = lrg_editor_server_new (editor);
	g_autoptr(LrgNode)         plain = lrg_node_new ("Plain");
	LrgNode                   *a;
	LrgNode                   *b;
	JsonNode                  *r;
	JsonObject                *o;

	g_assert_no_error (error);
	path = ipc_make_crate (dir);
	lrg_editor_server_set_project_dir (server, dir);
	a = lrg_editor_instantiate_prefab (editor, path, NULL, &error);
	g_assert_no_error (error);
	b = lrg_editor_instantiate_prefab (editor, path, NULL, &error);
	g_assert_no_error (error);
	lrg_editor_add_node (editor, plain, NULL);

	/* A fresh instance: linked, no overrides. */
	r = ipc_callf (server, "{\"id\":1,\"method\":\"prefab.overrides\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (a));
	o = ipc_obj (r);
	g_assert_cmpstr (json_object_get_string_member (o, "node"), ==, lrg_node_get_guid (a));
	g_assert_cmpstr (json_object_get_string_member (o, "source"), ==, path);
	g_assert_true (json_object_get_boolean_member (o, "linked"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "overrides")), ==, 0);
	json_node_unref (r);

	/* An edit is an override; asking about a child reports its instance. */
	ipc_set_visible (editor, a, FALSE);
	r = ipc_callf (server, "{\"id\":2,\"method\":\"prefab.overrides\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (lrg_node_find_by_path (a, "Lid")));
	o = ipc_obj (r);
	g_assert_cmpstr (json_object_get_string_member (o, "node"), ==, lrg_node_get_guid (a));
	g_assert_true (ipc_has_override (o, "visible"));
	json_node_unref (r);

	/* Revert: unknown keys are rejected up front; a known key reverts
	 * and is undoable. */
	r = ipc_callf (server, "{\"id\":3,\"method\":\"prefab.revert\",\"params\":{\"node\":\"%s\",\"keys\":[\"bogus\"]}}",
	               lrg_node_get_guid (a));
	ipc_assert_error (r, "not-found");
	json_node_unref (r);
	g_assert_false (lrg_node_get_visible (a));
	r = ipc_callf (server, "{\"id\":4,\"method\":\"prefab.revert\",\"params\":{\"node\":\"%s\",\"keys\":[\"visible\"]}}",
	               lrg_node_get_guid (a));
	g_assert_false (ipc_has_override (ipc_obj (r), "visible"));
	json_node_unref (r);
	g_assert_true (lrg_node_get_visible (a));
	lrg_editor_undo (editor);
	g_assert_false (lrg_node_get_visible (a));
	g_assert_true (lrg_prefab_link_contains_override (lrg_node_get_prefab_link (a), "visible"));

	/* Apply: written to the file, the other instance follows. */
	g_assert_true (lrg_node_get_visible (b));
	r = ipc_callf (server, "{\"id\":5,\"method\":\"prefab.apply\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (a));
	o = ipc_obj (r);
	g_assert_cmpint (json_object_get_int_member (o, "applied"), ==, 1);
	g_assert_cmpint (json_object_get_int_member (o, "synced"), >=, 1);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "overrides")), ==, 0);
	json_node_unref (r);
	g_assert_false (lrg_node_get_visible (b));

	/* Sync after an outside edit of the file: by path, then everything
	 * (already current, so nothing changes the second time). */
	{
		g_autoptr(LrgNode) prefab = lrg_prefab_load (path, &error);

		g_assert_no_error (error);
		lrg_node_set_visible (prefab, TRUE);
		g_assert_true (lrg_prefab_save (prefab, path, &error));
		g_assert_no_error (error);
	}
	r = ipc_call (server, "{\"id\":6,\"method\":\"prefab.sync\",\"params\":{\"path\":\"crate.rprefab\"}}");
	g_assert_cmpint (json_object_get_int_member (ipc_obj (r), "synced"), ==, 2);
	json_node_unref (r);
	g_assert_true (lrg_node_get_visible (a));
	g_assert_true (lrg_node_get_visible (b));
	r = ipc_call (server, "{\"id\":7,\"method\":\"prefab.sync\"}");
	o = ipc_obj (r);
	g_assert_cmpint (json_object_get_int_member (o, "synced"), ==, 0);
	g_assert_cmpint (json_object_get_int_member (o, "prefabs"), ==, 1);
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":8,\"method\":\"prefab.sync\",\"params\":{\"path\":\"missing.rprefab\"}}");
	ipc_assert_error (r, "not-found");
	json_node_unref (r);

	/* Unlink, then relink to the file. */
	r = ipc_callf (server, "{\"id\":9,\"method\":\"prefab.unlink\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (b));
	o = ipc_obj (r);
	g_assert_true (json_object_get_boolean_member (o, "unlinked"));
	g_assert_false (json_object_get_boolean_member (o, "linked"));
	json_node_unref (r);
	g_assert_null (lrg_node_get_prefab_link (b));
	r = ipc_callf (server, "{\"id\":10,\"method\":\"prefab.overrides\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (b));
	g_assert_false (json_object_get_boolean_member (ipc_obj (r), "linked"));
	json_node_unref (r);
	r = ipc_callf (server, "{\"id\":11,\"method\":\"prefab.relink\",\"params\":{\"node\":\"%s\",\"path\":\"missing.rprefab\"}}",
	               lrg_node_get_guid (b));
	ipc_assert_error (r, "not-found");
	json_node_unref (r);
	r = ipc_callf (server, "{\"id\":12,\"method\":\"prefab.relink\",\"params\":{\"node\":\"%s\",\"path\":\"crate.rprefab\"}}",
	               lrg_node_get_guid (b));
	o = ipc_obj (r);
	g_assert_true (json_object_get_boolean_member (o, "linked"));
	g_assert_cmpstr (json_object_get_string_member (o, "source"), ==, path);
	json_node_unref (r);
	g_assert_nonnull (lrg_node_get_prefab_link (b));

	/* Errors: missing node, unknown node, plain node, bad keys. */
	r = ipc_call (server, "{\"id\":13,\"method\":\"prefab.apply\",\"params\":{}}");
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);
	r = ipc_call (server, "{\"id\":14,\"method\":\"prefab.overrides\",\"params\":{\"node\":\"no-such-node\"}}");
	ipc_assert_error (r, "not-found");
	json_node_unref (r);
	r = ipc_callf (server, "{\"id\":15,\"method\":\"prefab.apply\",\"params\":{\"node\":\"%s\"}}",
	               lrg_node_get_guid (plain));
	ipc_assert_error (r, "failed");
	json_node_unref (r);
	r = ipc_callf (server, "{\"id\":16,\"method\":\"prefab.revert\",\"params\":{\"node\":\"%s\",\"keys\":\"visible\"}}",
	               lrg_node_get_guid (a));
	ipc_assert_error (r, "invalid-params");
	json_node_unref (r);

	ipc_rm_rf (dir);
}

/* ---- prefab.save syncs linked instances ---- */

static void
test_ipc_prefab_save_syncs (void)
{
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *dir = g_dir_make_tmp ("lrg-ipc-prefab-save-XXXXXX", &error);
	g_autofree gchar          *path = NULL;
	g_autoptr(LrgEditor)       editor = lrg_editor_new ();
	g_autoptr(LrgEditorServer) server = lrg_editor_server_new (editor);
	LrgNode                   *a;
	LrgNode                   *b;
	JsonNode                  *r;
	JsonNode                  *res;

	g_assert_no_error (error);
	path = ipc_make_crate (dir);
	lrg_editor_server_set_project_dir (server, dir);
	a = lrg_editor_instantiate_prefab (editor, path, NULL, &error);
	g_assert_no_error (error);
	b = lrg_editor_instantiate_prefab (editor, path, NULL, &error);
	g_assert_no_error (error);

	/* Saving an instance over its own source applies its overrides and
	 * syncs the other instance (0.8.0 only wrote the file). */
	ipc_set_visible (editor, a, FALSE);
	r = ipc_callf (server, "{\"id\":1,\"method\":\"prefab.save\",\"params\":{\"node\":\"%s\",\"path\":\"crate.rprefab\"}}",
	               lrg_node_get_guid (a));
	res = ipc_result (r);
	g_assert_cmpstr (json_node_get_string (res), ==, path);
	json_node_unref (r);
	g_assert_false (lrg_node_get_visible (b));
	g_assert_cmpuint (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (a)), ==, 0);

	ipc_rm_rf (dir);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#ifdef LRG_BUILD_EDITOR_UI
#define ADD(path, func) \
	g_test_add ("/editor-app/" path, Fixture, NULL, efx_setup, func, efx_teardown)

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
	ADD ("tile-paint", test_app_tile_paint);
	ADD ("timeline", test_app_timeline);
	ADD ("thumbnails", test_app_thumbnails);
	ADD ("legacy-frame", test_app_legacy_frame);
	ADD ("add-panel", test_app_add_panel);
#undef ADD
#endif

	/* Headless IPC methods (no app needed) */
	g_test_add_func ("/editor-app/ipc-asset-meta", test_ipc_asset_meta);
	g_test_add_func ("/editor-app/ipc-vcs-status", test_ipc_vcs_status);
	g_test_add_func ("/editor-app/ipc-prefab-link", test_ipc_prefab_link);
	g_test_add_func ("/editor-app/ipc-prefab-save-syncs", test_ipc_prefab_save_syncs);

	return g_test_run ();
}
