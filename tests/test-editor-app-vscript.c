/* test-editor-app-vscript.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The visual-script editor over IPC: create, the graph, undo, a headless
 * run, the blocks view and the read-only queries.
 */

#include "editor-app-fixture.h"

static gchar *
write_project_file (EditorAppFixture *f,
                    const gchar      *rel,
                    const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path;
	gchar            *dir;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	g_free (dir);
	g_assert_true (g_file_set_contents (path, text, -1, &error));
	g_assert_no_error (error);
	return path;
}

static gboolean
log_has (JsonObject  *obj,
         const gchar *text)
{
	JsonArray *log = json_object_get_array_member (obj, "log");
	guint      i;

	for (i = 0; i < json_array_get_length (log); i++)
	{
		if (g_strcmp0 (json_array_get_string_element (log, i), text) == 0)
			return TRUE;
	}
	return FALSE;
}

/*
 * The canvas mirrors the program: every link the program accepted is
 * drawn (string into an "any" input, int into float), comment frames
 * become canvas notes, a node with literals gets a value line, the
 * first show fits the graph, and vscript.frame answers with the view.
 * string.from turns a number into text a Concat takes.
 */
static void
test_vscript_canvas (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(JsonNode)     result = NULL;
	g_autofree gchar       *reply = NULL;
	LrgEditorVscriptEditor *editor;
	LrgNodeGraph           *canvas;
	JsonObject             *obj;
	guint                   links;

	(void) data;
	g_free (write_project_file (f, "readme.txt", "x"));
	result = efx_ipc_result (f, "{'id':1,'method':'vscript.create','params':{'path':'scripts/canvas.rvscript'}}");
	g_clear_pointer (&result, json_node_unref);

	/* --- num (float) -> conv (To String) -> fmt.b; fmt (string) -> log.text (any) --- */
	result = efx_ipc_result (f, "{'id':2,'method':'vscript.add_node','params':{'path':'scripts/canvas.rvscript',"
	                            "'type':'value.float','id':'num','x':40,'y':200,'params':{'value':5}}}");
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':3,'method':'vscript.add_node','params':{'path':'scripts/canvas.rvscript',"
	                            "'type':'string.from','id':'conv','x':260,'y':200}}");
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':4,'method':'vscript.add_node','params':{'path':'scripts/canvas.rvscript',"
	                            "'type':'string.concat','id':'fmt','x':480,'y':200,'inputs':{'a':'hp: '}}}");
	g_clear_pointer (&result, json_node_unref);

	/* a float straight into Concat stays a type error */
	reply = efx_ipc (f, "{'id':5,'method':'vscript.connect','params':{'path':'scripts/canvas.rvscript',"
	                    "'from':'num.value','to':'fmt.b'}}");
	g_assert_nonnull (strstr (reply, "invalid-params"));
	g_clear_pointer (&reply, g_free);

	result = efx_ipc_result (f, "{'id':6,'method':'vscript.connect','params':{'path':'scripts/canvas.rvscript',"
	                            "'from':'num.value','to':'conv.value'}}");
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':7,'method':'vscript.connect','params':{'path':'scripts/canvas.rvscript',"
	                            "'from':'conv.result','to':'fmt.b'}}");
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':8,'method':'vscript.connect','params':{'path':'scripts/canvas.rvscript',"
	                            "'from':'fmt.result','to':'log.text'}}");
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':9,'method':'vscript.add_comment','params':{'path':'scripts/canvas.rvscript',"
	                            "'text':'format the value','x':20,'y':170,'width':640,'height':140}}");
	g_clear_pointer (&result, json_node_unref);

	/* --- the canvas draws what the program holds --- */
	editor = lrg_editor_vscript_editor_open (f->app, "scripts/canvas.rvscript", NULL);
	g_assert_nonnull (editor);
	canvas = lrg_editor_vscript_editor_get_canvas (editor);
	links = lrg_vscript_graph_get_links (lrg_vscript_program_get_main (lrg_editor_vscript_editor_get_program (editor)))->len;
	g_assert_cmpuint (links, ==, 4);
	g_assert_cmpuint (lrg_node_graph_get_n_links (canvas), ==, links);
	g_assert_cmpuint (lrg_node_graph_get_n_notes (canvas), ==, 1);

	/* literals show; a node with none has no body; a linked input's literal is hidden */
	g_assert_nonnull (lrg_graph_node_get_body (lrg_node_graph_get_node (canvas, "num")));
	g_assert_nonnull (lrg_graph_node_get_body (lrg_node_graph_get_node (canvas, "fmt")));
	g_assert_null (lrg_graph_node_get_body (lrg_node_graph_get_node (canvas, "start")));
	g_assert_null (lrg_graph_node_get_body (lrg_node_graph_get_node (canvas, "log")));

	/* --- vscript.frame: Home over IPC --- */
	result = efx_ipc_result (f, "{'id':10,'method':'vscript.frame','params':{'path':'scripts/canvas.rvscript'}}");
	obj = json_node_get_object (result);
	g_assert_true (json_object_has_member (obj, "zoom"));
	g_assert_cmpfloat (json_object_get_double_member (obj, "zoom"), >, 0.0);
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':11,'method':'vscript.frame','params':{'path':'scripts/canvas.rvscript',"
	                            "'selection':true,'animate':true}}");
	g_clear_pointer (&result, json_node_unref);

	/* --- string.from runs: 5 becomes text --- */
	result = efx_ipc_result (f, "{'id':12,'method':'vscript.run','params':{'path':'scripts/canvas.rvscript'}}");
	obj = json_node_get_object (result);
	g_assert_true (json_object_get_boolean_member (obj, "ok"));
	g_assert_true (log_has (obj, "hp: 5"));
	g_clear_pointer (&result, json_node_unref);
}

static void
test_vscript_ipc (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autoptr(JsonNode)   result = NULL;
	g_autofree gchar     *reply = NULL;
	g_autofree gchar     *path = NULL;
	JsonObject           *obj;
	LrgEditorVscriptEditor *editor;
	guint                 before;

	(void) data;
	g_free (write_project_file (f, "readme.txt", "x"));

	result = efx_ipc_result (f, "{'id':1,'method':'vscript.create','params':{'path':'scripts/hello.rvscript'}}");
	path = g_strdup (json_object_get_string_member (json_node_get_object (result), "path"));
	g_assert_true (g_str_has_suffix (path, "hello.rvscript"));
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "view"), ==, "graph");
	g_clear_pointer (&result, json_node_unref);

	reply = efx_ipc (f, "{'id':2,'method':'vscript.create','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);

	result = efx_ipc_result (f, "{'id':3,'method':'vscript.get','params':{'path':'scripts/hello.rvscript'}}");
	obj = json_node_get_object (result);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj, "nodes")), ==, 2);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj, "links")), ==, 1);
	g_assert_cmpstr (json_object_get_string_member (obj, "view"), ==, "graph");
	g_assert_false (json_object_get_boolean_member (obj, "dirty"));
	g_clear_pointer (&result, json_node_unref);

	editor = lrg_editor_vscript_editor_open (f->app, "scripts/hello.rvscript", NULL);
	g_assert_nonnull (editor);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (lrg_editor_vscript_editor_get_canvas (editor)), ==, 2);
	g_assert_cmpuint (lrg_block_editor_get_n_kinds (lrg_editor_vscript_editor_get_blocks (editor)), >, 10);

	result = efx_ipc_result (f, "{'id':4,'method':'vscript.run','params':{'path':'scripts/hello.rvscript'}}");
	obj = json_node_get_object (result);
	g_assert_true (json_object_get_boolean_member (obj, "ok"));
	g_assert_true (log_has (obj, "hello"));
	g_assert_cmpuint (json_object_get_int_member (obj, "steps"), >, 0);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':5,'method':'vscript.node_types','params':{'query':'math.add'}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (result), "types")),
	                  >=, 1);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':6,'method':'vscript.gi_search','params':{'query':'Object','limit':5}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "available"));
	g_assert_nonnull (json_object_get_array_member (json_node_get_object (result), "results"));
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':7,'method':'vscript.add_node','params':{'path':'scripts/hello.rvscript',"
	                            "'type':'math.add','id':'sum','x':40,'y':200,'inputs':{'a':2,'b':3}}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "id"), ==, "sum");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (result), "pins")),
	                  ==, 3);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':8,'method':'vscript.connect','params':{'path':'scripts/hello.rvscript',"
	                            "'from':'sum.result','to':'log.text'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "to"), ==, "log.text");
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':9,'method':'vscript.set_variable','params':{'path':'scripts/hello.rvscript',"
	                            "'name':'hp','type':'int','default':4}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "name"), ==, "hp");
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':10,'method':'vscript.add_comment','params':{'path':'scripts/hello.rvscript',"
	                            "'text':'note','x':10,'y':10,'width':80,'height':40}}");
	g_assert_true (json_object_has_member (json_node_get_object (result), "id"));
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':11,'method':'vscript.add_subgraph','params':{'path':'scripts/hello.rvscript',"
	                            "'name':'util','inputs':[{'name':'n','type':'int'}]}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "name"), ==, "util");
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':12,'method':'vscript.set_program','params':{'path':'scripts/hello.rvscript',"
	                            "'name':'Hello','seed':7}}");
	obj = json_node_get_object (result);
	g_assert_cmpstr (json_object_get_string_member (obj, "name"), ==, "Hello");
	g_assert_cmpint (json_object_get_int_member (obj, "seed"), ==, 7);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj, "nodes")), ==, 3);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj, "comments")), ==, 1);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj, "subgraphs")), ==, 1);
	g_assert_true (json_object_get_boolean_member (obj, "dirty"));
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':13,'method':'vscript.validate','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "ok"));
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':14,'method':'vscript.select','params':{'path':'scripts/hello.rvscript',"
	                            "'nodes':['start','sum']}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (result), "selected")),
	                  ==, 2);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':15,'method':'vscript.run','params':{'path':'scripts/hello.rvscript',"
	                            "'variables':{'hp':9}}}");
	obj = json_node_get_object (result);
	g_assert_true (json_object_get_boolean_member (obj, "ok"));
	g_assert_cmpint (json_object_get_int_member (json_object_get_object_member (obj, "variables"), "hp"), ==, 9);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':16,'method':'vscript.save','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_true (g_str_has_suffix (json_object_get_string_member (json_node_get_object (result), "path"),
	                                 "hello.rvscript"));
	g_clear_pointer (&result, json_node_unref);
	g_assert_false (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (editor)));

	before = lrg_vscript_graph_get_n_nodes (lrg_vscript_program_get_main (lrg_editor_vscript_editor_get_program (editor)));
	result = efx_ipc_result (f, "{'id':17,'method':'vscript.add_node','params':{'path':'scripts/hello.rvscript',"
	                            "'type':'flow.branch','id':'branch'}}");
	g_clear_pointer (&result, json_node_unref);
	g_assert_cmpuint (lrg_vscript_graph_get_n_nodes (lrg_vscript_program_get_main (
	                          lrg_editor_vscript_editor_get_program (editor))), ==, before + 1);
	result = efx_ipc_result (f, "{'id':18,'method':'data.undo','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "ok"));
	g_clear_pointer (&result, json_node_unref);
	g_assert_cmpuint (lrg_vscript_graph_get_n_nodes (lrg_vscript_program_get_main (
	                          lrg_editor_vscript_editor_get_program (editor))), ==, before);

	reply = efx_ipc (f, "{'id':19,'method':'vscript.connect','params':{'path':'scripts/hello.rvscript',"
	                    "'from':'missing.pin','to':'log.text'}}");
	g_assert_nonnull (strstr (reply, "not-found"));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':20,'method':'vscript.connect','params':{'path':'scripts/hello.rvscript',"
	                    "'from':'start','to':'log.text'}}");
	g_assert_nonnull (strstr (reply, "invalid-params"));
	g_clear_pointer (&reply, g_free);

	result = efx_ipc_result (f, "{'id':21,'method':'vscript.live','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (result), "playing"));
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (result), "instances"), ==, 0);
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':22,'method':'vscript.breakpoint','params':{'path':'scripts/hello.rvscript',"
	                            "'node':'start','enabled':true}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "enabled"));
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':23,'method':'vscript.continue','params':{'path':'scripts/hello.rvscript'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (result), "resumed"), ==, 0);
	g_clear_pointer (&result, json_node_unref);

	result = efx_ipc_result (f, "{'id':24,'method':'vscript.open','params':{'path':'scripts/hello.rvscript',"
	                            "'view':'blocks'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result), "view"), ==, "blocks");
	g_clear_pointer (&result, json_node_unref);
	g_assert_true (lrg_editor_data_editor_get_content (LRG_EDITOR_DATA_EDITOR (editor)) ==
	               LRG_WIDGET (lrg_editor_vscript_editor_get_blocks (editor)));
	g_assert_cmpuint (lrg_block_editor_get_n_blocks (lrg_editor_vscript_editor_get_blocks (editor)), >=, 2);

	result = efx_ipc_result (f, "{'id':25,'method':'vscript.remove_node','params':{'path':'scripts/hello.rvscript',"
	                            "'id':'sum'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "removed"));
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':26,'method':'vscript.disconnect','params':{'path':'scripts/hello.rvscript',"
	                            "'from':'start.then','to':'log.exec'}}");
	g_assert_true (json_object_has_member (json_node_get_object (result), "removed"));
	g_clear_pointer (&result, json_node_unref);
	result = efx_ipc_result (f, "{'id':27,'method':'vscript.remove_subgraph','params':{'path':'scripts/hello.rvscript',"
	                            "'name':'util'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (result), "removed"));
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add ("/editor-app/vscript/ipc", EditorAppFixture, NULL, efx_setup, test_vscript_ipc, efx_teardown);
	g_test_add ("/editor-app/vscript/canvas", EditorAppFixture, NULL, efx_setup, test_vscript_canvas, efx_teardown);
	return g_test_run ();
}
