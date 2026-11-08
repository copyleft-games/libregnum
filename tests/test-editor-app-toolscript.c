/* test-editor-app-toolscript.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editor-time tool scripts: LrgEditorScriptApi, the console REPL and
 * run-in-editor bindings. Lua and Python cases skip when that backend
 * is not in this build.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <libregnum.h>

#ifdef LRG_BUILD_EDITOR_UI
#include "editor-app-fixture.h"
#endif

static gboolean
have_lang (LrgScriptLanguage language)
{
	return lrg_editor_script_api_language_is_supported (language);
}

static void
test_language_names (void)
{
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("lua"), ==, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("Python"), ==, LRG_SCRIPT_LANGUAGE_PYTHON);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("py"), ==, LRG_SCRIPT_LANGUAGE_PYTHON);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("pygobject"), ==, LRG_SCRIPT_LANGUAGE_NONE);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("JavaScript"), ==, LRG_SCRIPT_LANGUAGE_GJS);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("GJS"), ==, LRG_SCRIPT_LANGUAGE_GJS);
	g_assert_cmpint (lrg_editor_script_api_language_from_name ("nope"), ==, LRG_SCRIPT_LANGUAGE_NONE);
	g_assert_cmpint (lrg_editor_script_api_language_from_name (NULL), ==, LRG_SCRIPT_LANGUAGE_NONE);
}

static void
test_binding_flag (void)
{
	g_autoptr(LrgScriptBinding) a = NULL;
	g_autoptr(LrgScriptBinding) b = NULL;
	g_autoptr(LrgScriptComponent) comp = NULL;

	a = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "a.lua");
	b = lrg_script_binding_copy (a);
	g_assert_true (lrg_script_binding_equal (a, b));
	lrg_script_binding_set_run_in_editor (b, TRUE);
	g_assert_true (lrg_script_binding_get_run_in_editor (b));
	g_assert_false (lrg_script_binding_equal (a, b));

	comp = lrg_script_component_new (LRG_SCRIPT_LANGUAGE_LUA, "a.lua");
	g_assert_false (lrg_script_component_get_run_in_editor (comp));
	g_assert_false (lrg_script_component_should_update (comp, TRUE));
	g_assert_true (lrg_script_component_should_update (comp, FALSE));
	lrg_script_component_set_run_in_editor (comp, TRUE);
	g_assert_true (lrg_script_component_should_update (comp, TRUE));
}

static void
test_set_run_in_editor_undo (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autoptr(LrgNode) node = NULL;
	g_autoptr(LrgScriptBinding) binding = NULL;
	g_autoptr(GError) error = NULL;
	guint before;
	GPtrArray *scripts;

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	node = lrg_node_new ("Actor");
	lrg_editor_add_node (editor, node, NULL);
	binding = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, "actor.lua");
	lrg_node_add_script (node, binding);

	before = lrg_editor_get_undo_count (editor);
	g_assert_true (lrg_editor_script_api_set_run_in_editor (api, node, 0, TRUE, &error));
	g_assert_no_error (error);
	scripts = lrg_node_get_scripts (node);
	g_assert_true (lrg_script_binding_get_run_in_editor (g_ptr_array_index (scripts, 0)));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, before + 1);
	lrg_editor_undo (editor);
	g_assert_false (lrg_script_binding_get_run_in_editor (g_ptr_array_index (scripts, 0)));
}

typedef struct
{
	GPtrArray *lines;
} OutBag;

static void
on_output (LrgEditorScriptApi *api,
           const gchar        *line,
           gpointer            user_data)
{
	OutBag *bag = (OutBag *) user_data;

	(void) api;
	g_ptr_array_add (bag->lines, g_strdup (line));
}

static gboolean
write_file (const gchar *path,
            const gchar *text)
{
	return g_file_set_contents (path, text, -1, NULL);
}

static void
test_lua_create_undo (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *result = NULL;
	guint before;

	if (!have_lang (LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	before = lrg_editor_get_undo_count (editor);
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_LUA,
	                                                "editor:create_node('Crate', 'cube')",
	                                                "Make crate", &result, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_editor_find_node (editor, "Crate"));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, before + 1);
	g_assert_cmpstr (lrg_editor_command_get_label (
	                     lrg_editor_get_undo_command (editor, lrg_editor_get_undo_count (editor) - 1)),
	                 ==, "Make crate");
	lrg_editor_undo (editor);
	g_assert_null (lrg_editor_find_node (editor, "Crate"));
}

static void
test_lua_menu_and_repl (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *result = NULL;
	g_auto(GStrv) tools = NULL;
	OutBag bag;

	if (!have_lang (LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	bag.lines = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (api, "output", G_CALLBACK (on_output), &bag);

	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_LUA,
	                                                "function greet()\n"
	                                                "  editor:log('greeted')\n"
	                                                "end\n"
	                                                "editor:add_menu_item('greet', 'Hello/Greet', 'greet', nil)\n",
	                                                NULL, &result, &error));
	g_assert_no_error (error);
	g_free (result);
	result = NULL;
	tools = lrg_editor_script_api_get_tools (api);
	g_assert_nonnull (tools);
	g_assert_cmpstr (tools[0], ==, "script.tool.greet");
	g_assert_cmpstr (lrg_editor_script_api_get_tool_label (api, "script.tool.greet"), ==, "Hello/Greet");
	g_assert_true (lrg_editor_script_api_run_tool (api, "greet", &error));
	g_assert_no_error (error);
	g_assert_cmpuint (bag.lines->len, >=, 1);
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, bag.lines->len - 1), ==, "greeted");

	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_LUA,
	                                                "x = 41", NULL, &result, &error));
	g_assert_no_error (error);
	g_free (result);
	result = NULL;
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_LUA,
	                                                "return x + 1", NULL, &result, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (result, ==, "42");

	g_signal_handlers_disconnect_by_data (api, &bag);
	g_ptr_array_unref (bag.lines);
}

static void
test_python_repl (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *result = NULL;
	g_auto(GStrv) tools = NULL;
	guint before;
	OutBag bag;

	if (!have_lang (LRG_SCRIPT_LANGUAGE_PYTHON))
	{
		g_test_skip ("Python is not built");
		return;
	}

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	before = lrg_editor_get_undo_count (editor);
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_PYTHON,
	                                                "editor.create_node('PyCrate', 'cube')",
	                                                "Make py crate", &result, &error));
	g_assert_no_error (error);
	g_assert_nonnull (lrg_editor_find_node (editor, "PyCrate"));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, before + 1);
	lrg_editor_undo (editor);
	g_assert_null (lrg_editor_find_node (editor, "PyCrate"));

	g_free (result);
	result = NULL;
	bag.lines = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (api, "output", G_CALLBACK (on_output), &bag);
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_PYTHON,
	                                                "def greet():\n"
	                                                "    editor.log('py-greeted')\n"
	                                                "editor.add_menu_item('pygreet', 'Py Greet', 'greet')\n",
	                                                NULL, &result, &error));
	g_assert_no_error (error);
	tools = lrg_editor_script_api_get_tools (api);
	g_assert_cmpstr (tools[0], ==, "script.tool.pygreet");
	g_assert_true (lrg_editor_script_api_run_tool (api, "pygreet", &error));
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, bag.lines->len - 1), ==, "py-greeted");
	g_signal_handlers_disconnect_by_data (api, &bag);
	g_ptr_array_unref (bag.lines);

	g_free (result);
	result = NULL;
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_PYTHON,
	                                                "x = 41", NULL, &result, &error));
	g_assert_no_error (error);
	g_free (result);
	result = NULL;
	g_assert_true (lrg_editor_script_api_run_string (api, LRG_SCRIPT_LANGUAGE_PYTHON,
	                                                "x + 1", NULL, &result, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (result, ==, "42");
}

static void
test_editor_scripts_order (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *scripts = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	g_auto(GStrv) tools = NULL;
	OutBag bag;

	if (!have_lang (LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	dir = g_dir_make_tmp ("lrg-tools-XXXXXX", NULL);
	g_assert_nonnull (dir);
	scripts = g_build_filename (dir, "editor_scripts", NULL);
	g_assert_cmpint (g_mkdir (scripts, 0700), ==, 0);
	b = g_build_filename (scripts, "b_tool.lua", NULL);
	a = g_build_filename (scripts, "a_tool.lua", NULL);
	g_assert_true (write_file (b, "function tool_b()\nend\neditor:add_menu_item('beta', 'Beta', 'tool_b')\neditor:log('bbb')\n"));
	g_assert_true (write_file (a, "function tool_a()\nend\neditor:add_menu_item('alpha', 'Alpha', 'tool_a')\neditor:log('aaa')\n"));

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	bag.lines = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (api, "output", G_CALLBACK (on_output), &bag);
	lrg_editor_script_api_set_project_dir (api, dir);
	tools = lrg_editor_script_api_get_tools (api);
	g_assert_nonnull (tools);
	g_assert_cmpstr (tools[0], ==, "script.tool.alpha");
	g_assert_cmpstr (tools[1], ==, "script.tool.beta");
	g_assert_null (tools[2]);
	g_assert_cmpuint (bag.lines->len, >=, 2);
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, 0), ==, "aaa");
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, 1), ==, "bbb");

	g_signal_handlers_disconnect_by_data (api, &bag);
	g_ptr_array_unref (bag.lines);
	g_unlink (a);
	g_unlink (b);
	g_rmdir (scripts);
	g_rmdir (dir);
}

static void
test_run_in_editor (void)
{
	g_autoptr(LrgEditor) editor = NULL;
	g_autoptr(LrgEditorScriptApi) api = NULL;
	g_autoptr(LrgNode) node = NULL;
	g_autoptr(LrgScriptBinding) binding = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *good = NULL;
	g_autofree gchar *bad = NULL;
	OutBag bag;
	guint n_after_fail;

	if (!have_lang (LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	dir = g_dir_make_tmp ("lrg-rie-XXXXXX", NULL);
	g_assert_nonnull (dir);
	good = g_build_filename (dir, "good.lua", NULL);
	bad = g_build_filename (dir, "bad.lua", NULL);
	g_assert_true (write_file (good,
	                           "function lrg_script_start()\n"
	                           "  editor:log('started')\n"
	                           "end\n"
	                           "function lrg_script_update(dt)\n"
	                           "  editor:log('tick')\n"
	                           "end\n"
	                           "function lrg_script_detach()\n"
	                           "  editor:log('detached')\n"
	                           "end\n"));
	g_assert_true (write_file (bad,
	                           "function lrg_script_start()\n"
	                           "  error('boom')\n"
	                           "end\n"));

	editor = lrg_editor_new ();
	api = lrg_editor_script_api_new (editor);
	node = lrg_node_new ("Pawn");
	lrg_editor_add_node (editor, node, NULL);
	binding = lrg_script_binding_new (LRG_SCRIPT_LANGUAGE_LUA, good);
	lrg_script_binding_set_run_in_editor (binding, TRUE);
	lrg_node_add_script (node, binding);

	bag.lines = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (api, "output", G_CALLBACK (on_output), &bag);

	lrg_editor_script_api_tick (api, 0.016f);
	g_assert_cmpuint (lrg_editor_script_api_get_n_editor_scripts (api), ==, 1);
	g_assert_cmpuint (bag.lines->len, >=, 2);
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, 0), ==, "started");
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, 1), ==, "tick");

	lrg_editor_script_api_stop_editor_scripts (api);
	g_assert_cmpuint (lrg_editor_script_api_get_n_editor_scripts (api), ==, 0);
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, bag.lines->len - 1), ==, "detached");

	/* A failed start sticks until the binding's script changes. */
	g_ptr_array_set_size (bag.lines, 0);
	lrg_script_binding_set_script (binding, bad);
	lrg_script_binding_set_run_in_editor (binding, TRUE);
	lrg_editor_script_api_tick (api, 0.016f);
	g_assert_cmpuint (lrg_editor_script_api_get_n_editor_scripts (api), ==, 1);
	n_after_fail = bag.lines->len;
	g_assert_cmpuint (n_after_fail, >=, 1);
	lrg_editor_script_api_tick (api, 0.016f);
	g_assert_cmpuint (bag.lines->len, ==, n_after_fail);

	lrg_script_binding_set_script (binding, good);
	lrg_editor_script_api_tick (api, 0.016f);
	g_assert_cmpstr ((const gchar *) g_ptr_array_index (bag.lines, bag.lines->len - 1), ==, "tick");

	g_signal_handlers_disconnect_by_data (api, &bag);
	g_ptr_array_unref (bag.lines);
	g_unlink (good);
	g_unlink (bad);
	g_rmdir (dir);
}

#ifdef LRG_BUILD_EDITOR_UI
static void
test_console_ipc_repl (EditorAppFixture *f,
                       gconstpointer     data)
{
	g_autoptr(JsonNode) langs = NULL;
	g_autoptr(JsonNode) first = NULL;
	g_autoptr(JsonNode) second = NULL;
	LrgWidget *cmd;
	LrgEditorConsolePanel *panel;
	g_autofree gchar *result = NULL;
	g_autoptr(GError) error = NULL;

	(void) data;
	langs = efx_ipc_result (f, "{'id':1,'method':'script.languages'}");
	g_assert_true (JSON_NODE_HOLDS_ARRAY (langs));
	g_assert_cmpint (json_array_get_length (json_node_get_array (langs)), ==, 3);

	if (!have_lang (LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	first = efx_ipc_result (f, "{'id':2,'method':'script.run','params':{'code':'x = 41','language':'lua'}}");
	second = efx_ipc_result (f, "{'id':3,'method':'script.run','params':{'code':'return x + 1','language':'lua'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (second), "result"), ==, "42");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (second), "language"), ==, "lua");

	cmd = efx_find (f, "console-command");
	g_assert_nonnull (cmd);
	panel = LRG_EDITOR_CONSOLE_PANEL (lrg_widget_get_parent (LRG_WIDGET (lrg_widget_get_parent (cmd))));
	g_assert_true (LRG_IS_EDITOR_SCRIPT_API (lrg_editor_console_panel_get_script_api (panel)));
	lrg_editor_console_panel_set_repl_language (panel, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_cmpint (lrg_editor_console_panel_get_repl_language (panel), ==, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_true (lrg_editor_console_panel_repl (panel, "y = 7", &result, &error));
	g_assert_no_error (error);
	g_free (result);
	result = NULL;
	g_assert_true (lrg_editor_console_panel_repl (panel, "return y + 1", &result, &error));
	g_assert_cmpstr (result, ==, "8");
	g_assert_true (lrg_editor_console_panel_repl (panel, "editor:log('from-repl')", NULL, &error));
	g_assert_cmpstr (lrg_editor_console_panel_get_repl_output (panel), ==, "from-repl");
}
#endif

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_add_func ("/toolscript/language-names", test_language_names);
	g_test_add_func ("/toolscript/binding-flag", test_binding_flag);
	g_test_add_func ("/toolscript/set-run-in-editor-undo", test_set_run_in_editor_undo);
	g_test_add_func ("/toolscript/lua-create-undo", test_lua_create_undo);
	g_test_add_func ("/toolscript/lua-menu-repl", test_lua_menu_and_repl);
	g_test_add_func ("/toolscript/python-repl", test_python_repl);
	g_test_add_func ("/toolscript/editor-scripts-order", test_editor_scripts_order);
	g_test_add_func ("/toolscript/run-in-editor", test_run_in_editor);
#ifdef LRG_BUILD_EDITOR_UI
	g_test_add ("/toolscript/console-ipc-repl", EditorAppFixture, NULL,
	            efx_setup, test_console_ipc_repl, efx_teardown);
#endif
	return g_test_run ();
}
