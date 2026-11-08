/* test-editor-app-ai.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 AI tools of lrgeditor, in a headless LrgEditorApp
 * (tests/editor-app-fixture.h):
 *
 *  - the `.rbt` behaviour tree editor: the registry picks it for .rbt,
 *    cards and wires from the document, nodes added / moved / retyped /
 *    removed through the API as single undo steps, generated ids written
 *    on the first structural edit, positions in the `editor:` block, auto
 *    layout, the blackboard schema and subtrees, validation (structure,
 *    undeclared subtrees, bindings the editor lacks), a byte-stable save
 *    the engine loads back, canvas gestures (wire, delete), every bt.*
 *    IPC method;
 *  - the `.rfsm` state-machine editor: states (nested, with breadcrumbs),
 *    transitions with events, timers, conditions and actions, renames
 *    that rewrite targets, removals that drop the transitions into the
 *    removed state, validation, save and engine reload, every fsm.*
 *    method;
 *  - the AI panel: watched decision brains (a scripted decider), the
 *    decision log, the ai.* methods;
 *  - play in editor: a level node with an `ai` component running a tree
 *    and a machine; the live entity list, the trees' live statuses, the
 *    machine's active state, blackboard writes and events over IPC.
 */

#include "editor-app-fixture.h"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gchar *
project_path (EditorAppFixture *f,
              const gchar      *rel)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	gchar            *path;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	return path;
}

static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path = project_path (f, rel);

	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

static gchar *
doc_text (LrgEditorDataEditor *editor)
{
	return lrg_editor_document_to_data (lrg_editor_data_editor_get_document (editor), NULL);
}

static JsonObject *
res_obj (JsonNode *n)
{
	return json_node_get_object (n);
}

/* An IPC call expected to fail; returns the error code's nick
 * ("invalid-params", "not-found", "failed"). */
static const gchar *
ipc_error_code (EditorAppFixture *f,
                const gchar      *request)
{
	g_autofree gchar     *reply = efx_ipc (f, request);
	g_autoptr(JsonParser) parser = json_parser_new ();
	JsonObject           *o;

	g_assert_true (json_parser_load_from_data (parser, reply, -1, NULL));
	o = json_node_get_object (json_parser_get_root (parser));
	g_assert_true (json_object_has_member (o, "error"));
	{
		static gchar code[64];

		g_strlcpy (code, json_object_get_string_member (json_object_get_object_member (o, "error"), "code"),
		           sizeof code);
		return code;
	}
}

static const gchar KOBOLD_RBT[] =
	"version: 1\n"
	"name: kobold\n"
	"blackboard:\n"
	"- {name: mode, type: string, default: calm}\n"
	"- {name: hp, type: float, default: 30.0}\n"
	"root:\n"
	"  type: selector\n"
	"  id: root\n"
	"  children:\n"
	"  - type: guard\n"
	"    id: when_alert\n"
	"    key: mode\n"
	"    value: alert\n"
	"    child: {type: set, key: hp, value: 1.0}\n"
	"  - {type: wait, id: idle, duration: 0.5}\n";

static const gchar MOOD_RFSM[] =
	"version: 1\n"
	"name: mood\n"
	"blackboard:\n"
	"- {name: mode, type: string, default: calm}\n"
	"- {name: hp, type: float, default: 30.0}\n"
	"initial: calm\n"
	"transitions:\n"
	"- to: dead\n"
	"  when: [{key: hp, op: le, value: 0.0}]\n"
	"states:\n"
	"- name: calm\n"
	"  enter: [{set: mode, value: calm}]\n"
	"  transitions: [{to: alert, event: hurt}]\n"
	"- name: alert\n"
	"  enter: [{set: mode, value: alert}]\n"
	"  transitions: [{to: calm, after: 5.0}]\n"
	"- name: dead\n";

/* ==========================================================================
 * Behaviour trees
 * ========================================================================== */

static LrgEditorBtEditor *
open_bt (EditorAppFixture *f,
         const gchar      *rel,
         const gchar      *text)
{
	g_autofree gchar  *path = write_project_file (f, rel, text);
	g_autoptr(GError)  error = NULL;
	LrgEditorBtEditor *bt = lrg_editor_bt_editor_open (f->app, path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (bt);
	efx_frames (f, 2);
	return bt;
}

static void
test_bt_open (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorBtEditor *bt = open_bt (f, "ai/kobold.rbt", KOBOLD_RBT);
	LrgNodeGraph      *canvas = lrg_editor_bt_editor_get_canvas (bt);
	g_auto(GStrv)      ids = lrg_editor_bt_editor_dup_node_ids (bt);

	g_assert_true (lrg_editor_data_editor_lookup_type (lrg_editor_document_detect_format ("x.rbt", NULL, 0)) ==
	               LRG_TYPE_EDITOR_BT_EDITOR);
	g_assert_nonnull (lrg_editor_bt_editor_get_asset (bt));
	g_assert_null (lrg_editor_bt_editor_get_load_error (bt));

	/* root, guard, its set child (generated id), the wait */
	g_assert_cmpuint (g_strv_length (ids), ==, 4);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (canvas), ==, 4);
	g_assert_cmpuint (lrg_node_graph_get_n_links (canvas), ==, 3);
	g_assert_cmpstr (lrg_editor_bt_editor_get_node_parent (bt, "idle"), ==, "root");
	g_assert_cmpstr (lrg_editor_bt_editor_get_node_path (bt, "when_alert"), ==, "/root/children/0");
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "when_alert"));

	/* the generated id is the one the engine gives the set node */
	{
		LrgBtAssetNode *n = lrg_bt_asset_find_node (lrg_editor_bt_editor_get_asset (bt), ids[2]);

		g_assert_nonnull (n);
		g_assert_cmpstr (lrg_bt_asset_node_get_type_name (n), ==, "set");
		g_assert_false (lrg_bt_asset_node_has_explicit_id (n));
	}
	/* the tree validates */
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (bt)), ==, 0);
}

static void
test_bt_edit (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorBtEditor   *bt = open_bt (f, "ai/edit.rbt", KOBOLD_RBT);
	LrgEditorDocument   *doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (bt));
	g_autoptr(GError)    error = NULL;
	g_autofree gchar    *seq = NULL;
	g_autofree gchar    *chk = NULL;
	g_autofree gchar    *act = NULL;
	g_autofree gchar    *before = doc_text (LRG_EDITOR_DATA_EDITOR (bt));
	g_autofree gchar    *text = NULL;
	gfloat               x, y;

	/* a sequence with a check and an action, each one undo step */
	seq = lrg_editor_bt_editor_add_node (bt, "root", 1, "sequence", NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (seq, ==, "sequence");
	g_assert_cmpstr (lrg_editor_bt_editor_get_node_path (bt, "sequence"), ==, "/root/children/1");
	/* the first structural edit wrote the generated id of the set node */
	{
		g_auto(GStrv) ids = lrg_editor_bt_editor_dup_node_ids (bt);
		guint         i;

		for (i = 0; ids[i] != NULL; i++)
		{
			g_autofree gchar *idp = g_strconcat (lrg_editor_bt_editor_get_node_path (bt, ids[i]), "/id", NULL);

			g_assert_cmpstr (lrg_editor_document_get_scalar (doc, idp), ==, ids[i]);
		}
	}
	chk = lrg_editor_bt_editor_add_node (bt, seq, -1, "check", "low", &error);
	g_assert_no_error (error);
	g_assert_true (lrg_editor_bt_editor_set_node_prop (bt, chk, "key", "hp", &error));
	g_assert_true (lrg_editor_bt_editor_set_node_prop (bt, chk, "op", "lt", &error));
	g_assert_true (lrg_editor_bt_editor_set_node_prop (bt, chk, "value", "8.0", &error));
	act = lrg_editor_bt_editor_add_node (bt, seq, -1, "action", "flee", &error);
	g_assert_no_error (error);
	g_assert_true (lrg_editor_bt_editor_set_node_prop (bt, act, "script", "flee_from", &error));
	g_assert_true (lrg_editor_bt_editor_set_node_param (bt, act, "speed", "6.5", &error));
	g_assert_no_error (error);

	/* a leaf takes no children, ids are unique, a decorator one child */
	g_assert_null (lrg_editor_bt_editor_add_node (bt, "low", -1, "wait", NULL, &error));
	g_clear_error (&error);
	g_assert_null (lrg_editor_bt_editor_add_node (bt, "root", -1, "wait", "low", &error));
	g_clear_error (&error);
	g_assert_null (lrg_editor_bt_editor_add_node (bt, "when_alert", -1, "wait", NULL, &error));
	g_clear_error (&error);

	/* new nodes got positions next to their parents */
	g_assert_true (lrg_editor_bt_editor_get_node_position (bt, "low", &x, &y));
	g_assert_cmpfloat (x, >, 0.0f);

	/* the engine sees it */
	efx_frames (f, 1);
	{
		LrgBtAsset     *asset = lrg_editor_bt_editor_get_asset (bt);
		LrgBtAssetNode *n = lrg_bt_asset_find_node (asset, "flee");

		g_assert_nonnull (n);
		g_assert_cmpfloat (lrg_bt_params_get_double (lrg_bt_asset_node_get_params (n), "speed", 0.0), ==, 6.5);
		g_assert_cmpuint (lrg_bt_asset_node_get_child_count (lrg_bt_asset_find_node (asset, "sequence")), ==, 2);
	}

	/* move: the action goes first in its sequence, then under the root */
	g_assert_true (lrg_editor_bt_editor_move_node (bt, "flee", "sequence", 0, &error));
	g_assert_cmpstr (lrg_editor_bt_editor_get_node_path (bt, "flee"), ==, "/root/children/1/children/0");
	g_assert_true (lrg_editor_bt_editor_move_node (bt, "flee", "root", -1, &error));
	g_assert_cmpstr (lrg_editor_bt_editor_get_node_parent (bt, "flee"), ==, "root");
	g_assert_false (lrg_editor_bt_editor_move_node (bt, "root", "sequence", -1, &error));
	g_clear_error (&error);
	g_assert_false (lrg_editor_bt_editor_move_node (bt, "sequence", "low", -1, &error));
	g_clear_error (&error);

	/* retype: a sequence with two children cannot become a decorator */
	g_assert_true (lrg_editor_bt_editor_move_node (bt, "flee", "sequence", -1, &error));
	g_assert_false (lrg_editor_bt_editor_set_node_type (bt, "sequence", "inverter", &error));
	g_clear_error (&error);
	g_assert_true (lrg_editor_bt_editor_set_node_type (bt, "sequence", "selector", &error));
	g_assert_no_error (error);

	/* rename: the saved position follows */
	g_assert_true (lrg_editor_bt_editor_set_node_id (bt, "low", "low_hp", &error));
	g_assert_null (lrg_editor_bt_editor_get_node_path (bt, "low"));
	g_assert_true (lrg_editor_bt_editor_get_node_position (bt, "low_hp", NULL, NULL));

	/* blackboard key and subtree */
	g_assert_true (lrg_editor_bt_editor_add_key (bt, "target", "object", NULL, &error));
	g_assert_false (lrg_editor_bt_editor_add_key (bt, "target", "object", NULL, &error));
	g_clear_error (&error);
	g_assert_true (lrg_editor_bt_editor_set_subtree (bt, "patrol", NULL, &error));
	g_assert_true (lrg_editor_bt_editor_show_subtree (bt, "patrol"));
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (lrg_editor_bt_editor_get_canvas (bt)), ==, 1);
	g_assert_true (lrg_editor_bt_editor_show_subtree (bt, NULL));
	{
		g_autofree gchar *sub = lrg_editor_bt_editor_add_node (bt, "root", -1, "subtree", "patrolling", &error);

		g_assert_nonnull (sub);
		g_assert_true (lrg_editor_bt_editor_set_node_prop (bt, sub, "ref", "patrol", &error));
		g_assert_true (lrg_editor_bt_editor_open_subtree (bt, sub, &error) == bt);
		g_assert_cmpstr (lrg_editor_bt_editor_get_shown_subtree (bt), ==, "patrol");
		lrg_editor_bt_editor_show_subtree (bt, NULL);
	}
	efx_frames (f, 1);
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (bt)), ==, 0);

	/* remove takes the subtree and its positions */
	g_assert_true (lrg_editor_bt_editor_remove_node (bt, "sequence", &error));
	g_assert_null (lrg_editor_bt_editor_get_node_path (bt, "low_hp"));
	text = doc_text (LRG_EDITOR_DATA_EDITOR (bt));
	g_assert_null (strstr (text, "low_hp"));

	/* undo walks it all back */
	while (lrg_editor_document_can_undo (doc))
		lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (bt));
	g_free (text);
	text = doc_text (LRG_EDITOR_DATA_EDITOR (bt));
	g_assert_cmpstr (text, ==, before);
}

static void
test_bt_layout_save (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgEditorBtEditor *bt = open_bt (f, "ai/layout.rbt", KOBOLD_RBT);
	const gchar       *path = lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (bt));
	g_autoptr(GError)  error = NULL;
	g_autoptr(LrgBtAsset) reloaded = NULL;
	g_autofree gchar  *saved = NULL;
	gfloat             rx, ry, ix, iy;

	g_assert_cmpuint (lrg_editor_bt_editor_auto_layout (bt), ==, 4);
	g_assert_true (lrg_editor_bt_editor_get_node_position (bt, "root", &rx, &ry));
	g_assert_true (lrg_editor_bt_editor_get_node_position (bt, "idle", &ix, &iy));
	g_assert_cmpfloat (ix, >, rx);
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (bt), &error));
	g_assert_no_error (error);

	/* the engine loads it back and keeps the editor block */
	reloaded = lrg_bt_asset_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (lrg_bt_asset_find_node (reloaded, "when_alert"));
	g_assert_true (g_file_get_contents (path, &saved, NULL, NULL));
	g_assert_nonnull (strstr (saved, "editor:"));
	g_assert_nonnull (strstr (saved, "positions:"));

	/* save is byte-stable from there on */
	{
		g_autofree gchar *again = NULL;

		g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (bt), &error));
		g_assert_true (g_file_get_contents (path, &again, NULL, NULL));
		g_assert_cmpstr (again, ==, saved);
	}
}

static void
test_bt_validate (EditorAppFixture *f,
                  gconstpointer     data)
{
	static const gchar bad[] =
		"version: 1\n"
		"name: bad\n"
		"blackboard: [{name: hp, type: float}]\n"
		"root:\n"
		"  type: sequence\n"
		"  id: root\n"
		"  children:\n"
		"  - {type: check, id: typo, key: hpp, op: lt, value: 3.0}\n"
		"  - {type: wait, id: rest, duration: 1.0}\n";
	static const gchar unbound[] =
		"version: 1\n"
		"name: unbound\n"
		"root:\n"
		"  type: sequence\n"
		"  id: root\n"
		"  children:\n"
		"  - {type: wait, id: rest, duration: 1.0}\n"
		"  - {type: action, id: hit, action: editor_test_missing_action}\n";
	LrgEditorBtEditor *ub = open_bt (f, "ai/unbound.rbt", unbound);
	LrgEditorBtEditor *bt;
	GPtrArray         *issues;
	guint              errors, i;
	gboolean           saw_key = FALSE, saw_binding = FALSE;

	/* a binding the editor process lacks is a note, not an error */
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ub)), ==, 0);
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (ub));
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		if (strstr (issue->message, "editor_test_missing_action") != NULL)
		{
			saw_binding = TRUE;
			g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_INFO);
			g_assert_cmpstr (issue->path, ==, "/root/children/1");
		}
	}

	/* an undeclared key is an error located on its node */
	bt = open_bt (f, "ai/bad.rbt", bad);
	errors = lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (bt));
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (bt));
	g_assert_cmpuint (errors, >=, 1);
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *issue = g_ptr_array_index (issues, i);

		if (strstr (issue->message, "hpp") != NULL)
		{
			saw_key = TRUE;
			g_assert_cmpint (issue->severity, ==, LRG_EDITOR_PROBLEM_ERROR);
			g_assert_nonnull (issue->path);
			g_assert_true (g_str_has_prefix (issue->path, "/root/children/0"));
		}
	}
	g_assert_true (saw_key);
	g_assert_true (saw_binding);

	/* bt.validate names the node */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'bt.validate','params':{'path':'ai/bad.rbt'}}");
		JsonArray          *a = json_object_get_array_member (res_obj (r), "issues");
		gboolean            named = FALSE;

		g_assert_false (json_object_get_boolean_member (res_obj (r), "ok"));
		for (i = 0; i < json_array_get_length (a); i++)
		{
			JsonObject *io = json_array_get_object_element (a, i);

			if (!json_node_is_null (json_object_get_member (io, "node")) &&
			    g_strcmp0 (json_object_get_string_member (io, "node"), "typo") == 0)
				named = TRUE;
		}
		g_assert_true (named);
	}
}

static void
test_bt_canvas (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorBtEditor *bt = open_bt (f, "ai/canvas.rbt", KOBOLD_RBT);
	LrgEditorDocument *doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (bt));
	LrgNodeGraph      *canvas = lrg_editor_bt_editor_get_canvas (bt);
	LrgGraphNode      *idle = lrg_node_graph_get_node (canvas, "idle");
	LrgGraphNode      *guard = lrg_node_graph_get_node (canvas, "when_alert");
	guint              undo_before;

	/* selecting a card inspects it */
	lrg_node_graph_select_node (canvas, idle, FALSE);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_bt_editor_get_selected_node (bt), ==, "idle");

	/* a guard already has its child: the canvas refuses a second wire */
	g_assert_false (lrg_node_graph_can_connect (canvas, lrg_graph_node_find_port (guard, LRG_GRAPH_PORT_OUTPUT, "out"),
	                                            lrg_graph_node_find_port (idle, LRG_GRAPH_PORT_INPUT, "in")));

	/* Delete on the card removes the node (one undo step) */
	lrg_editor_document_clear_undo (doc);
	undo_before = lrg_editor_document_can_undo (doc) ? 1 : 0;
	g_assert_cmpuint (undo_before, ==, 0);
	lrg_node_graph_select_node (canvas, idle, FALSE);
	g_assert_cmpuint (lrg_node_graph_delete_selection (canvas), ==, 1);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_bt_editor_get_node_path (bt, "idle"));
	g_assert_true (lrg_editor_document_can_undo (doc));
	lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (bt));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_editor_bt_editor_get_node_path (bt, "idle"));
	g_assert_nonnull (lrg_node_graph_get_node (canvas, "idle"));

	/* the palette chips exist for every built-in type */
	g_assert_nonnull (efx_find (f, "bt-palette-sequence"));
	g_assert_nonnull (efx_find (f, "bt-palette-cooldown"));
}

static void
test_bt_ipc (EditorAppFixture *f,
             gconstpointer     data)
{
	g_autofree gchar *path = project_path (f, "ai/ipc.rbt");
	JsonArray        *nodes;
	guint             i;
	gboolean          found = FALSE;

	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'bt.create','params':{'path':'ai/ipc.rbt'}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "path"), ==, path);
	}
	g_assert_cmpstr (ipc_error_code (f, "{'id':1,'method':'bt.create','params':{'path':'ai/ipc.rbt'}}"), ==, "failed");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'bt.palette','params':{}}");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "types")), >=, 14);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':2,'method':'bt.add_node','params':{'path':'ai/ipc.rbt',"
		                                           "'parent':'root','type':'cooldown','id':'cd','props':{'seconds':1.5}}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "id"), ==, "cd");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'bt.add_node','params':{'path':'ai/ipc.rbt',"
		                                           "'parent':'cd','type':'action','name':'Swing',"
		                                           "'props':{'action':'melee'},'params':{'damage':4}}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "id"), ==, "action");
	}
	g_free (efx_ipc (f, "{'id':4,'method':'bt.set_node','params':{'path':'ai/ipc.rbt','id':'action','new_id':'swing',"
	                     "'params':{'damage':null,'element':'fire'}}}"));
	g_free (efx_ipc (f, "{'id':5,'method':'bt.add_key','params':{'path':'ai/ipc.rbt','name':'hp','type':'float',"
	                     "'default':30.0}}"));
	g_free (efx_ipc (f, "{'id':6,'method':'bt.set_position','params':{'path':'ai/ipc.rbt','id':'swing','x':480,'y':40}}"));
	g_free (efx_ipc (f, "{'id':7,'method':'bt.select','params':{'path':'ai/ipc.rbt','id':'swing'}}"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':8,'method':'bt.get','params':{'path':'ai/ipc.rbt'}}");
		JsonObject         *o = res_obj (r);

		g_assert_cmpstr (json_object_get_string_member (o, "root"), ==, "root");
		g_assert_cmpstr (json_object_get_string_member (o, "selected"), ==, "swing");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "blackboard")), ==, 1);
		nodes = json_object_get_array_member (o, "nodes");
		for (i = 0; i < json_array_get_length (nodes); i++)
		{
			JsonObject *n = json_array_get_object_element (nodes, i);

			if (g_strcmp0 (json_object_get_string_member (n, "id"), "swing") != 0)
				continue;
			found = TRUE;
			g_assert_cmpstr (json_object_get_string_member (n, "parent"), ==, "cd");
			g_assert_cmpstr (json_object_get_string_member (n, "kind"), ==, "leaf");
			g_assert_cmpstr (json_object_get_string_member (n, "name"), ==, "Swing");
			g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (n, "props"), "action"), ==,
			                 "melee");
			g_assert_false (json_object_has_member (json_object_get_object_member (n, "params"), "damage"));
			g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (n, "params"), "element"),
			                 ==, "fire");
			g_assert_cmpfloat (json_object_get_double_member (n, "x"), ==, 480.0);
		}
		g_assert_true (found);
	}
	g_assert_cmpstr (ipc_error_code (f, "{'id':9,'method':'bt.remove_node','params':{'path':'ai/ipc.rbt','id':'nope'}}"),
	                 ==, "not-found");
	g_assert_cmpstr (ipc_error_code (f, "{'id':9,'method':'bt.add_node','params':{'path':'ai/ipc.rbt','parent':'cd',"
	                                    "'type':'wait'}}"), ==, "invalid-params");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':10,'method':'bt.layout','params':{'path':'ai/ipc.rbt'}}");

		g_assert_cmpint (json_object_get_int_member (res_obj (r), "nodes"), ==, 3);
	}
	g_free (efx_ipc (f, "{'id':11,'method':'bt.set_subtree','params':{'path':'ai/ipc.rbt','name':'walk',"
	                     "'file':'walk.rbt'}}"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':12,'method':'bt.validate','params':{'path':'ai/ipc.rbt'}}");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "ok"));
	}
	{
		g_autoptr(JsonNode)   r = efx_ipc_result (f, "{'id':13,'method':'bt.save','params':{'path':'ai/ipc.rbt'}}");
		g_autoptr(GError)     error = NULL;
		g_autoptr(LrgBtAsset) asset = lrg_bt_asset_new_from_file (path, &error);

		g_assert_no_error (error);
		g_assert_nonnull (lrg_bt_asset_find_node (asset, "swing"));
		g_assert_cmpstr (lrg_bt_asset_get_subtree_path (asset, "walk"), ==, "walk.rbt");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':14,'method':'bt.live','params':{'path':'ai/ipc.rbt'}}");

		g_assert_false (json_object_get_boolean_member (res_obj (r), "attached"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':15,'method':'bt.remove_node','params':{'path':'ai/ipc.rbt',"
		                                           "'id':'cd'}}");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "removed"));
	}
}

/* ==========================================================================
 * State machines
 * ========================================================================== */

static LrgEditorFsmEditor *
open_fsm (EditorAppFixture *f,
          const gchar      *rel,
          const gchar      *text)
{
	g_autofree gchar   *path = write_project_file (f, rel, text);
	g_autoptr(GError)   error = NULL;
	LrgEditorFsmEditor *fsm = lrg_editor_fsm_editor_open (f->app, path, &error);

	g_assert_no_error (error);
	g_assert_nonnull (fsm);
	efx_frames (f, 2);
	return fsm;
}

static void
test_fsm_open (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorFsmEditor *fsm = open_fsm (f, "ai/mood.rfsm", MOOD_RFSM);
	LrgNodeGraph       *canvas = lrg_editor_fsm_editor_get_canvas (fsm);
	g_auto(GStrv)       states = lrg_editor_fsm_editor_dup_state_paths (fsm);
	g_auto(GStrv)       crumbs = lrg_editor_fsm_editor_dup_breadcrumbs (fsm);

	g_assert_true (lrg_editor_data_editor_lookup_type (lrg_editor_document_detect_format ("x.rfsm", NULL, 0)) ==
	               LRG_TYPE_EDITOR_FSM_EDITOR);
	g_assert_nonnull (lrg_editor_fsm_editor_get_machine (fsm));
	g_assert_cmpuint (g_strv_length (states), ==, 3);
	g_assert_cmpstr (crumbs[0], ==, "mood");
	/* three states and the any-state card; calm->alert, alert->calm, any->dead */
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (canvas), ==, 4);
	g_assert_cmpuint (lrg_node_graph_get_n_links (canvas), ==, 3);
	g_assert_nonnull (lrg_node_graph_get_node (canvas, LRG_EDITOR_FSM_EDITOR_OWNER_CARD));
	g_assert_cmpuint (lrg_editor_fsm_editor_get_transition_count (fsm, ""), ==, 1);
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (fsm)), ==, 0);
}

static void
test_fsm_edit (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorFsmEditor *fsm = open_fsm (f, "ai/edit.rfsm", MOOD_RFSM);
	LrgEditorDocument  *doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (fsm));
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *before = doc_text (LRG_EDITOR_DATA_EDITOR (fsm));
	g_autofree gchar   *combat = NULL;
	g_autofree gchar   *approach = NULL;
	g_autofree gchar   *attack = NULL;
	g_autofree gchar   *renamed = NULL;
	g_autofree gchar   *text = NULL;
	gint                index;

	/* a hierarchical state with two children */
	combat = lrg_editor_fsm_editor_add_state (fsm, "", "combat", 400, 0, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (combat, ==, "combat");
	approach = lrg_editor_fsm_editor_add_state (fsm, "combat", "approach", 0, 0, &error);
	attack = lrg_editor_fsm_editor_add_state (fsm, "combat", "attack", 240, 0, &error);
	g_assert_cmpstr (attack, ==, "combat/attack");
	g_assert_null (lrg_editor_fsm_editor_add_state (fsm, "combat", "attack", 0, 0, &error));
	g_clear_error (&error);
	g_assert_true (lrg_editor_fsm_editor_set_initial (fsm, "combat/approach", &error));

	/* transitions: alert -> combat on an event, approach -> attack on a timer with a condition */
	index = lrg_editor_fsm_editor_add_transition (fsm, "alert", "combat", "spotted", -1.0, &error);
	g_assert_cmpint (index, ==, 1);
	index = lrg_editor_fsm_editor_add_transition (fsm, "combat/approach", "attack", NULL, 0.5, &error);
	g_assert_cmpint (index, ==, 0);
	g_assert_true (lrg_editor_fsm_editor_set_transition_field (fsm, "combat/approach", 0, "when",
	                                                           "[{key: hp, op: gt, value: 5.0}]", &error));
	g_assert_true (lrg_editor_fsm_editor_set_actions (fsm, "combat/attack", LRG_LOGIC_PHASE_ENTER,
	                                                  "[{set: mode, value: attack}, {event: swung}]", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (fsm)), ==, 0);
	{
		LrgLogicMachine *m = lrg_editor_fsm_editor_get_machine (fsm);
		LrgLogicState   *s = lrg_logic_machine_find_state (m, "combat/approach");

		g_assert_nonnull (s);
		g_assert_cmpuint (lrg_logic_state_get_transition_count (s), ==, 1);
		g_assert_cmpfloat (lrg_logic_transition_get_after (lrg_logic_state_get_transition (s, 0)), ==, 0.5);
		g_assert_cmpuint (lrg_logic_transition_get_condition_count (lrg_logic_state_get_transition (s, 0)), ==, 1);
		g_assert_cmpuint (lrg_logic_state_get_action_count (lrg_logic_machine_find_state (m, "combat/attack"),
		                                                    LRG_LOGIC_PHASE_ENTER), ==, 2);
	}

	/* entering the sub-machine shows its children and an owner card */
	g_assert_true (lrg_editor_fsm_editor_enter (fsm, "combat"));
	efx_frames (f, 1);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (lrg_editor_fsm_editor_get_canvas (fsm)), ==, 3);
	{
		g_auto(GStrv) crumbs = lrg_editor_fsm_editor_dup_breadcrumbs (fsm);

		g_assert_cmpuint (g_strv_length (crumbs), ==, 2);
		g_assert_cmpstr (crumbs[1], ==, "combat");
	}
	g_assert_true (lrg_editor_fsm_editor_go_up (fsm));
	g_assert_cmpstr (lrg_editor_fsm_editor_get_shown (fsm), ==, "");
	g_assert_cmpstr (lrg_editor_fsm_editor_get_selected_state (fsm), ==, "combat");

	/* renaming rewrites targets (relative and from inside) and initial */
	renamed = lrg_editor_fsm_editor_rename_state (fsm, "alert", "wary", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (renamed, ==, "wary");
	text = doc_text (LRG_EDITOR_DATA_EDITOR (fsm));
	g_assert_null (strstr (text, "to: alert"));
	g_assert_nonnull (strstr (text, "wary"));
	g_free (renamed);
	renamed = lrg_editor_fsm_editor_rename_state (fsm, "combat/approach", "close_in", &error);
	g_assert_cmpstr (renamed, ==, "combat/close_in");
	efx_frames (f, 1);
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (fsm)), ==, 0);

	/* removing a state removes the transitions into it */
	g_assert_true (lrg_editor_fsm_editor_remove_state (fsm, "dead", &error));
	g_assert_cmpuint (lrg_editor_fsm_editor_get_transition_count (fsm, ""), ==, 0);
	efx_frames (f, 1);
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (fsm)), ==, 0);

	/* everything undoes */
	while (lrg_editor_document_can_undo (doc))
		lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (fsm));
	g_free (text);
	text = doc_text (LRG_EDITOR_DATA_EDITOR (fsm));
	g_assert_cmpstr (text, ==, before);
}

static void
test_fsm_canvas (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorFsmEditor *fsm = open_fsm (f, "ai/canvas.rfsm", MOOD_RFSM);
	LrgNodeGraph       *canvas = lrg_editor_fsm_editor_get_canvas (fsm);
	LrgGraphNode       *dead = lrg_node_graph_get_node (canvas, "dead");
	LrgGraphNode       *calm = lrg_node_graph_get_node (canvas, "calm");
	LrgGraphLink       *link;
	g_autoptr(GError)   error = NULL;
	g_autoptr(LrgLogicMachine) m = NULL;

	/* a wire from dead to calm adds a transition */
	link = lrg_node_graph_connect (canvas, lrg_graph_node_find_port (dead, LRG_GRAPH_PORT_OUTPUT, "out"),
	                               lrg_graph_node_find_port (calm, LRG_GRAPH_PORT_INPUT, "in"), &error);
	g_assert_no_error (error);
	g_assert_nonnull (link);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_fsm_editor_get_transition_count (fsm, "dead"), ==, 1);
	{
		guint        index = 99;
		const gchar *from = lrg_editor_fsm_editor_get_selected_transition (fsm, &index);

		g_assert_cmpstr (from, ==, "dead");
		g_assert_cmpuint (index, ==, 0);
	}

	/* Delete on a card removes the state and the wires into it */
	lrg_node_graph_select_node (canvas, lrg_node_graph_get_node (canvas, "dead"), FALSE);
	lrg_node_graph_delete_selection (canvas);
	efx_frames (f, 2);
	g_assert_false (g_strv_contains ((const gchar * const *) lrg_editor_fsm_editor_dup_state_paths (fsm), "dead"));
	g_assert_cmpuint (lrg_editor_fsm_editor_get_transition_count (fsm, ""), ==, 0);

	/* save: the engine reads the file */
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (fsm), &error));
	m = lrg_logic_machine_new_from_file (lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (fsm)), &error);
	g_assert_no_error (error);
	g_assert_null (lrg_logic_machine_find_state (m, "dead"));
	g_assert_nonnull (lrg_logic_machine_find_state (m, "calm"));
}

static void
test_fsm_ipc (EditorAppFixture *f,
              gconstpointer     data)
{
	g_autofree gchar *path = project_path (f, "ai/door.rfsm");

	g_free (efx_ipc (f, "{'id':1,'method':'fsm.create','params':{'path':'ai/door.rfsm','name':'door'}}"));
	g_free (efx_ipc (f, "{'id':2,'method':'fsm.add_key','params':{'path':'ai/door.rfsm','name':'locked','type':'bool'}}"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'fsm.add_state','params':{'path':'ai/door.rfsm',"
		                                           "'name':'open'}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "state"), ==, "open");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':4,'method':'fsm.add_transition','params':{'path':"
		                                           "'ai/door.rfsm','from':'idle','to':'open','event':'use',"
		                                           "'when':[{'key':'locked','value':false}],"
		                                           "'do':[{'action':'play_anim','params':{'clip':'open'}}]}}");

		g_assert_cmpint (json_object_get_int_member (res_obj (r), "index"), ==, 0);
	}
	g_free (efx_ipc (f, "{'id':5,'method':'fsm.add_transition','params':{'path':'ai/door.rfsm','from':'open',"
	                     "'to':'idle','after':5.0}}"));
	g_free (efx_ipc (f, "{'id':6,'method':'fsm.set_actions','params':{'path':'ai/door.rfsm','state':'open',"
	                     "'phase':'enter','actions':[{'set':'locked','value':false}]}}"));
	g_free (efx_ipc (f, "{'id':7,'method':'fsm.set_transition','params':{'path':'ai/door.rfsm','from':'open',"
	                     "'index':0,'event':'use','after':null}}"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':8,'method':'fsm.get','params':{'path':'ai/door.rfsm'}}");
		JsonObject         *o = res_obj (r);
		JsonArray          *ta = json_object_get_array_member (o, "transitions");
		guint               i;
		gboolean            saw = FALSE;

		g_assert_cmpstr (json_object_get_string_member (o, "name"), ==, "door");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "states")), ==, 2);
		g_assert_cmpuint (json_array_get_length (ta), ==, 2);
		for (i = 0; i < json_array_get_length (ta); i++)
		{
			JsonObject *t = json_array_get_object_element (ta, i);

			if (g_strcmp0 (json_object_get_string_member (t, "from"), "open") == 0)
			{
				saw = TRUE;
				g_assert_cmpstr (json_object_get_string_member (t, "event"), ==, "use");
				g_assert_true (json_node_is_null (json_object_get_member (t, "after")));
			}
		}
		g_assert_true (saw);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':9,'method':'fsm.rename_state','params':{'path':"
		                                           "'ai/door.rfsm','state':'idle','name':'closed'}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "state"), ==, "closed");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':10,'method':'fsm.validate','params':{'path':'ai/door.rfsm'}}");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "ok"));
	}
	g_free (efx_ipc (f, "{'id':11,'method':'fsm.save','params':{'path':'ai/door.rfsm'}}"));
	{
		g_autoptr(GError)          error = NULL;
		g_autoptr(LrgLogicMachine) m = lrg_logic_machine_new_from_file (path, &error);
		LrgLogicState             *closed;

		g_assert_no_error (error);
		closed = lrg_logic_machine_find_state (m, "closed");
		g_assert_nonnull (closed);
		g_assert_cmpstr (lrg_logic_transition_get_event (lrg_logic_state_get_transition (closed, 0)), ==, "use");
		g_assert_cmpstr (lrg_logic_transition_get_target (lrg_logic_state_get_transition (
		                     lrg_logic_machine_find_state (m, "open"), 0)), ==, "closed");
	}
	g_assert_cmpstr (ipc_error_code (f, "{'id':12,'method':'fsm.remove_state','params':{'path':'ai/door.rfsm',"
	                                    "'state':'nope'}}"), ==, "not-found");
	g_assert_cmpstr (ipc_error_code (f, "{'id':13,'method':'fsm.send_event','params':{'path':'ai/door.rfsm',"
	                                    "'event':'use'}}"), ==, "failed");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':14,'method':'fsm.live','params':{'path':'ai/door.rfsm'}}");

		g_assert_false (json_object_get_boolean_member (res_obj (r), "attached"));
	}
}

/* ==========================================================================
 * The AI panel: decisions
 * ========================================================================== */

static gchar *
scripted_answer (LrgDecider   *decider,
                 const gchar  *request_json,
                 gpointer      user_data,
                 GError      **error)
{
	return g_strdup ("{\"model\":\"test\",\"answers\":{\"hostile\":{\"type\":\"noul\",\"noul\":0.9}},"
	                 "\"usage\":{\"input_tokens\":10,\"output_tokens\":2}}");
}

static LrgEditorAiDebugPanel *
ai_panel (EditorAppFixture *f)
{
	LrgEditorAiDebugPanel *panel = lrg_editor_ai_debug_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_AI_DEBUG_PANEL_ID, "AI");

		panel = lrg_editor_ai_debug_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:inspector");
	}
	return panel;
}

static void
test_panel_decisions (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorAiDebugPanel        *panel = ai_panel (f);
	g_autoptr(LrgDecider)         decider = lrg_decider_new_scripted (scripted_answer, NULL, NULL);
	g_autoptr(LrgDecisionBrain)   brain = lrg_decision_brain_new (decider);
	g_autoptr(LrgDecisionRequest) request = lrg_decision_request_new ();
	g_autoptr(GError)             error = NULL;
	g_auto(GStrv)                 names = NULL;
	gint                          i;

	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "ai-panel"));
	lrg_editor_ai_debug_panel_watch_brain (f->app, "npc", brain);
	names = lrg_editor_ai_debug_panel_dup_brain_names (f->app);
	g_assert_cmpstrv (names, ((const gchar *[]) { "npc", NULL }));
	g_assert_true (lrg_editor_ai_debug_panel_get_brain (f->app, "npc") == brain);

	g_assert_true (lrg_decision_request_add_noul (request, "hostile", "Is it hostile?", NULL, NULL, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_decision_brain_ask (brain, "talk", request, NULL, &error));
	g_assert_no_error (error);
	for (i = 0; i < 50 && lrg_decision_brain_is_busy (brain, "talk"); i++)
		efx_frame (f);
	efx_frames (f, 2);

	/* the log has the question and the answer */
	g_assert_cmpuint (lrg_editor_ai_debug_panel_get_log_length (f->app), >=, 1);
	{
		g_autofree gchar *first = lrg_editor_ai_debug_panel_dup_log_entry (f->app, 0);

		g_assert_true (g_str_has_prefix (first, "npc asked talk"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ai.brains','params':{}}");
		JsonArray          *a = json_object_get_array_member (res_obj (r), "brains");
		JsonObject         *b = json_array_get_object_element (a, 0);

		g_assert_cmpuint (json_array_get_length (a), ==, 1);
		g_assert_cmpstr (json_object_get_string_member (b, "provider"), ==, "scripted");
		g_assert_cmpint (json_object_get_int_member (b, "asked"), ==, 1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (b, "topics")), ==, 1);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':2,'method':'ai.topic','params':{'brain':'npc',"
		                                           "'topic':'talk'}}");

		g_assert_nonnull (strstr (json_object_get_string_member (res_obj (r), "request"), "hostile"));
		g_assert_cmpstr (lrg_editor_ai_debug_panel_get_selected_topic (panel), ==, "talk");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'ai.log','params':{'limit':1}}");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "entries")), ==, 1);
	}
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "ai-topic-talk"));
	{
		g_autofree gchar *text = lrg_editor_ai_debug_panel_describe (panel);

		g_assert_nonnull (strstr (text, "npc"));
		g_assert_nonnull (strstr (text, "asked 1"));
	}
	g_assert_cmpstr (ipc_error_code (f, "{'id':4,'method':'ai.topic','params':{'brain':'x','topic':'talk'}}"), ==, "not-found");
	/* not playing: no agents, state fails */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':5,'method':'ai.entities','params':{}}");

		g_assert_false (json_object_get_boolean_member (res_obj (r), "playing"));
	}
	g_assert_cmpstr (ipc_error_code (f, "{'id':6,'method':'ai.state','params':{}}"), ==, "failed");
	g_assert_true (lrg_editor_ai_debug_panel_unwatch_brain (f->app, "npc"));
}

/* ==========================================================================
 * Play in editor
 * ========================================================================== */

static void
test_play (EditorAppFixture *f,
           gconstpointer     data)
{
	g_autofree gchar            *tree = write_project_file (f, "ai/kobold.rbt", KOBOLD_RBT);
	g_autofree gchar            *machine = write_project_file (f, "ai/mood.rfsm", MOOD_RFSM);
	g_autoptr(LrgComponentDesc)  desc = lrg_component_desc_new ("ai");
	g_autoptr(LrgNode)           node = lrg_node_new ("Kobold");
	LrgEditorAiDebugPanel       *panel = ai_panel (f);
	LrgEditorBtEditor           *bt;
	LrgEditorFsmEditor          *fsm;
	g_autoptr(GError)            error = NULL;
	gint                         i;

	lrg_component_desc_set_string (desc, "tree-path", tree);
	lrg_component_desc_set_string (desc, "machine-path", machine);
	lrg_node_add_component (node, desc);
	lrg_editor_add_node (f->editor, node, NULL);
	bt = lrg_editor_bt_editor_open (f->app, tree, &error);
	fsm = lrg_editor_fsm_editor_open (f->app, machine, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);

	if (!lrg_editor_app_activate_action (f->app, "play.start") || !lrg_editor_app_is_playing (f->app))
	{
		g_test_skip ("play-in-editor is not available here");
		return;
	}
	efx_frames (f, 10);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'ai.entities','params':{}}");
		JsonArray          *a = json_object_get_array_member (res_obj (r), "entities");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "playing"));
		if (json_array_get_length (a) == 0)
		{
			g_test_skip ("the play world does not materialise the ai component here");
			lrg_editor_app_activate_action (f->app, "play.stop");
			return;
		}
		g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 0), "name"), ==, "Kobold");
		g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 0), "current_state"), ==,
		                 "calm");
	}

	/* follow it and look at the live tree: the wait runs */
	g_free (efx_ipc (f, "{'id':2,'method':'ai.select','params':{'entity':'Kobold'}}"));
	g_assert_nonnull (lrg_editor_ai_debug_panel_get_focus_component (f->app));
	efx_frames (f, 3);
	g_assert_nonnull (lrg_editor_bt_editor_get_live_runner (bt));
	g_assert_cmpint (lrg_editor_bt_editor_get_live_status (bt, "idle", NULL), ==, LRG_BT_STATUS_RUNNING);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'bt.live','params':{'path':'ai/kobold.rbt'}}");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "attached"));
		g_assert_cmpint (json_object_get_int_member (res_obj (r), "tick"), >, 0);
	}

	/* an event over IPC moves the machine; the tree's guard then holds */
	g_free (efx_ipc (f, "{'id':4,'method':'ai.send_event','params':{'event':'hurt'}}"));
	for (i = 0; i < 5; i++)
		efx_frame (f);
	g_assert_nonnull (lrg_editor_fsm_editor_get_live_machine (fsm));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':5,'method':'fsm.live','params':{'path':'ai/mood.rfsm'}}");
		JsonArray          *act = json_object_get_array_member (res_obj (r), "active");

		g_assert_true (json_object_get_boolean_member (res_obj (r), "attached"));
		g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (act, 0), "state"), ==, "alert");
	}
	{
		/* the selector resumes its running wait (0.5 s), then re-checks the guard */
		gboolean    visited = FALSE;
		LrgBTStatus st = LRG_BT_STATUS_INVALID;

		for (i = 0; i < 120 && !(visited && st == LRG_BT_STATUS_SUCCESS); i++)
		{
			efx_frame (f);
			st = lrg_editor_bt_editor_get_live_status (bt, "when_alert", &visited);
		}
		g_assert_true (visited);
		g_assert_cmpint (st, ==, LRG_BT_STATUS_SUCCESS);
	}

	/* a blackboard write: hp to 0 and the any-state transition fires */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':6,'method':'ai.set_key','params':{'key':'hp','value':0.0}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "key"), ==, "hp");
	}
	for (i = 0; i < 5; i++)
		efx_frame (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':7,'method':'ai.state','params':{}}");

		g_assert_nonnull (strstr (json_object_get_string_member (res_obj (r), "debug"), "dead"));
	}
	{
		g_autofree gchar *text = lrg_editor_ai_debug_panel_describe (panel);

		g_assert_nonnull (strstr (text, "* Kobold"));
	}
	g_assert_nonnull (efx_find (f, "ai-agent-0"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_bt_editor_get_live_runner (bt));
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GError) error = NULL;

	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	/* the registry that builds the level's `ai` component is made at
	 * startup (headless-safe) */
	if (!lrg_engine_startup (lrg_engine_get_default (), &error))
		g_printerr ("# engine startup: %s\n", error->message);

	g_test_add ("/editor-app/ai/bt/open", EditorAppFixture, NULL, efx_setup, test_bt_open, efx_teardown);
	g_test_add ("/editor-app/ai/bt/edit", EditorAppFixture, NULL, efx_setup, test_bt_edit, efx_teardown);
	g_test_add ("/editor-app/ai/bt/layout-save", EditorAppFixture, NULL, efx_setup, test_bt_layout_save, efx_teardown);
	g_test_add ("/editor-app/ai/bt/validate", EditorAppFixture, NULL, efx_setup, test_bt_validate, efx_teardown);
	g_test_add ("/editor-app/ai/bt/canvas", EditorAppFixture, NULL, efx_setup, test_bt_canvas, efx_teardown);
	g_test_add ("/editor-app/ai/bt/ipc", EditorAppFixture, NULL, efx_setup, test_bt_ipc, efx_teardown);
	g_test_add ("/editor-app/ai/fsm/open", EditorAppFixture, NULL, efx_setup, test_fsm_open, efx_teardown);
	g_test_add ("/editor-app/ai/fsm/edit", EditorAppFixture, NULL, efx_setup, test_fsm_edit, efx_teardown);
	g_test_add ("/editor-app/ai/fsm/canvas", EditorAppFixture, NULL, efx_setup, test_fsm_canvas, efx_teardown);
	g_test_add ("/editor-app/ai/fsm/ipc", EditorAppFixture, NULL, efx_setup, test_fsm_ipc, efx_teardown);
	g_test_add ("/editor-app/ai/panel/decisions", EditorAppFixture, NULL, efx_setup, test_panel_decisions,
	            efx_teardown);
	g_test_add ("/editor-app/ai/play", EditorAppFixture, NULL, efx_setup, test_play, efx_teardown);
	return g_test_run ();
}
