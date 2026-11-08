/* test-editor-app-debug.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Text-editor gutter / diagnostics / completion, and the Debug panel's
 * debug.* IPC. The shell docks one panel; new() returns that one.
 */

#include "editor-app-fixture.h"
#include <string.h>

typedef struct
{
	EditorAppFixture     efx;
	LrgEditorDebugPanel *panel;
} DebugFix;

static void
debug_setup (DebugFix    *fix,
             gconstpointer data)
{
	(void) data;
	efx_setup (&fix->efx, NULL);
	fix->panel = lrg_editor_debug_panel_new (fix->efx.app);
	g_assert_nonnull (fix->panel);
}

static void
debug_teardown (DebugFix    *fix,
                gconstpointer data)
{
	g_autofree gchar *cleared = NULL;
	g_autofree gchar *stopped = NULL;

	(void) data;
	cleared = efx_ipc (&fix->efx, "{'id':1,'method':'debug.clear_breakpoints','params':{}}");
	stopped = efx_ipc (&fix->efx, "{'id':1,'method':'debug.dap_stop','params':{}}");
	g_clear_object (&fix->panel);
	efx_teardown (&fix->efx, NULL);
}

static gchar *
error_code (EditorAppFixture *fix,
            const gchar      *request)
{
	g_autofree gchar     *reply = efx_ipc (fix, request);
	g_autoptr(JsonParser) parser = json_parser_new ();
	JsonObject           *object;
	JsonObject           *err;

	g_assert_true (json_parser_load_from_data (parser, reply, -1, NULL));
	object = json_node_get_object (json_parser_get_root (parser));
	g_assert_true (json_object_has_member (object, "error"));
	err = json_object_get_object_member (object, "error");
	return g_strdup (json_object_get_string_member (err, "code"));
}

static void
test_text_editor_marks (void)
{
	g_autoptr(LrgTextEditor) editor = lrg_text_editor_new ();
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GPtrArray) diags = NULL;
	g_autoptr(GPtrArray) got = NULL;
	g_autofree gchar *text = NULL;
	LrgTextCompletion item;
	LrgTextCompletion *copied;
	LrgTextDiagnostic diag;
	LrgTextDiagnostic *diag_copy;
	LrgTextDiagnostic *back;

	g_assert_cmpuint (g_signal_lookup ("gutter-clicked", LRG_TYPE_TEXT_EDITOR), !=, 0);
	g_assert_cmpuint (g_signal_lookup ("goto-definition", LRG_TYPE_TEXT_EDITOR), !=, 0);
	g_assert_true (lrg_text_gutter_mark_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_text_diagnostic_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_text_diagnostic_severity_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_text_completion_get_type () != G_TYPE_INVALID);

	g_assert_false (lrg_text_editor_get_show_gutter_marks (editor));
	lrg_text_editor_set_show_gutter_marks (editor, TRUE);
	g_assert_true (lrg_text_editor_get_show_gutter_marks (editor));
	lrg_text_editor_set_gutter_mark (editor, 2, LRG_TEXT_GUTTER_MARK_BREAKPOINT);
	g_assert_cmpuint (lrg_text_editor_get_gutter_mark (editor, 2), ==, LRG_TEXT_GUTTER_MARK_BREAKPOINT);
	lrg_text_editor_set_execution_line (editor, 2);
	g_assert_cmpint (lrg_text_editor_get_execution_line (editor), ==, 2);
	lrg_text_editor_clear_gutter_marks (editor);
	g_assert_cmpuint (lrg_text_editor_get_gutter_mark (editor, 2), ==, LRG_TEXT_GUTTER_MARK_NONE);
	g_assert_cmpint (lrg_text_editor_get_execution_line (editor), ==, 2);
	lrg_text_editor_set_execution_line (editor, -1);
	g_assert_cmpint (lrg_text_editor_get_execution_line (editor), ==, -1);

	memset (&diag, 0, sizeof diag);
	diag.line = 0;
	diag.column = 1;
	diag.end_line = 0;
	diag.end_column = 3;
	diag.severity = LRG_TEXT_DIAGNOSTIC_WARNING;
	diag.message = (gchar *) "watch this";
	diag_copy = lrg_text_diagnostic_copy (&diag);
	g_assert_cmpstr (diag_copy->message, ==, "watch this");
	diags = g_ptr_array_new ();
	g_ptr_array_add (diags, diag_copy);
	lrg_text_editor_set_diagnostics (editor, diags);
	lrg_text_editor_set_text (editor, "foo bar\n");
	got = lrg_text_editor_get_diagnostics (editor);
	g_assert_cmpuint (got->len, ==, 1);
	back = g_ptr_array_index (got, 0);
	g_assert_cmpuint (back->column, ==, 1);
	g_assert_cmpint (back->severity, ==, LRG_TEXT_DIAGNOSTIC_WARNING);
	lrg_text_diagnostic_free (diag_copy);
	g_ptr_array_unref (diags);
	diags = NULL;

	memset (&item, 0, sizeof item);
	item.label = (gchar *) "foo_bar";
	item.detail = (gchar *) "var";
	item.insert_text = (gchar *) "foo_bar";
	copied = lrg_text_completion_copy (&item);
	g_assert_cmpstr (copied->insert_text, ==, "foo_bar");
	lrg_text_completion_free (copied);
	items = g_ptr_array_new ();
	g_ptr_array_add (items, &item);
	lrg_text_editor_set_cursor (editor, 0, 3);
	lrg_text_editor_show_completion (editor, items);
	g_assert_true (lrg_text_editor_get_completion_visible (editor));
	g_assert_cmpint (lrg_text_editor_get_completion_selected (editor), ==, 0);
	lrg_text_editor_completion_move (editor, 1);
	g_assert_cmpint (lrg_text_editor_get_completion_selected (editor), ==, 0);
	g_assert_true (lrg_text_editor_completion_accept (editor));
	g_assert_false (lrg_text_editor_get_completion_visible (editor));
	text = lrg_text_editor_get_text (editor);
	g_assert_true (g_str_has_prefix (text, "foo_bar"));

	lrg_text_editor_show_hover (editor, "a tip");
	g_assert_cmpstr (lrg_text_editor_get_hover_text (editor), ==, "a tip");
	lrg_text_editor_hide_hover (editor);
	g_assert_null (lrg_text_editor_get_hover_text (editor));
	g_assert_false (lrg_text_editor_completion_accept (editor));
}

static void
test_debug_ipc (DebugFix    *fix,
                gconstpointer data)
{
	g_autoptr(JsonNode) state = NULL;
	g_autoptr(JsonNode) set = NULL;
	g_autoptr(JsonNode) list = NULL;
	g_autoptr(JsonNode) toggled = NULL;
	g_autoptr(JsonNode) watches = NULL;
	g_autoptr(JsonNode) started = NULL;
	g_autoptr(JsonNode) servers = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *request = NULL;
	g_autofree gchar *code = NULL;
	JsonObject *object;
	JsonArray *array;
	LrgDapServer *dap;

	(void) data;
	g_assert_true (lrg_editor_debug_panel_get_app (fix->panel) == fix->efx.app);
	dap = lrg_editor_debug_panel_get_dap (fix->panel);
	g_assert_nonnull (dap);
	g_assert_false (lrg_dap_server_get_listening (dap));

	state = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.state','params':{}}");
	object = json_node_get_object (state);
	g_assert_cmpstr (json_object_get_string_member (object, "state"), ==, "idle");
	g_assert_true (json_object_get_null_member (object, "reason"));
	g_assert_false (json_object_get_boolean_member (json_object_get_object_member (object, "dap"), "listening"));

	{
		g_autoptr(JsonNode) shown = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.show','params':{}}");

		g_assert_nonnull (shown);
	}

	path = g_build_filename (fix->efx.dir, "door.lua", NULL);
	request = g_strdup_printf ("{'id':1,'method':'debug.set_breakpoint','params':{'path':'%s','line':4,'condition':'x > 0'}}", path);
	set = efx_ipc_result (&fix->efx, request);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (set), "line"), ==, 4);
	g_clear_pointer (&request, g_free);
	request = g_strdup_printf ("{'id':1,'method':'debug.breakpoints','params':{'path':'%s'}}", path);
	list = efx_ipc_result (&fix->efx, request);
	array = json_node_get_array (list);
	g_assert_cmpuint (json_array_get_length (array), ==, 1);

	g_clear_pointer (&request, g_free);
	request = g_strdup_printf ("{'id':1,'method':'debug.toggle_breakpoint','params':{'path':'%s','line':4}}", path);
	toggled = efx_ipc_result (&fix->efx, request);
	g_assert_false (json_object_get_boolean_member (json_node_get_object (toggled), "set"));

	code = error_code (&fix->efx, "{'id':1,'method':'debug.step_over','params':{}}");
	g_assert_cmpstr (code, ==, "failed");
	g_clear_pointer (&code, g_free);
	code = error_code (&fix->efx, "{'id':1,'method':'debug.evaluate','params':{'expression':'1+1'}}");
	g_assert_cmpstr (code, ==, "failed");

	watches = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.add_watch','params':{'expression':'x'}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (watches)), ==, 1);
	g_clear_pointer (&watches, json_node_unref);
	watches = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.remove_watch','params':{'expression':'x'}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (watches)), ==, 0);

	{
		g_autoptr(JsonNode) went = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.continue','params':{}}");
		g_autoptr(JsonNode) halted = NULL;

		g_assert_nonnull (went);
		started = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.dap_start','params':{'port':0}}");
		g_assert_cmpint (json_object_get_int_member (json_node_get_object (started), "port"), >, 0);
		g_assert_true (lrg_dap_server_get_listening (dap));
		halted = efx_ipc_result (&fix->efx, "{'id':1,'method':'debug.dap_stop','params':{}}");
		g_assert_nonnull (halted);
		g_assert_false (lrg_dap_server_get_listening (dap));
	}

	servers = efx_ipc_result (&fix->efx, "{'id':1,'method':'lsp.servers','params':{}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (servers)), >=, 1);
	g_clear_pointer (&code, g_free);
	code = error_code (&fix->efx, "{'id':1,'method':'lsp.status','params':{}}");
	g_assert_cmpstr (code, ==, "failed");
	g_clear_pointer (&code, g_free);
	code = error_code (&fix->efx, "{'id':1,'method':'lsp.restart','params':{'language':'no-such'}}");
	g_assert_cmpstr (code, ==, "invalid-params");
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_add_func ("/editor/text-editor/marks", test_text_editor_marks);
	g_test_add ("/editor/debug/ipc", DebugFix, NULL, debug_setup, test_debug_ipc, debug_teardown);
	return g_test_run ();
}
