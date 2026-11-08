/* test-vscript.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVscriptProgram and LrgScriptingGraph: YAML round trip (unknown keys
 * kept), one node from each family, seeded determinism and the step budget.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <libregnum.h>

static LrgVscriptNode *
add_node (LrgVscriptGraph *graph,
          const gchar     *kind,
          const gchar     *id)
{
	g_autoptr(GError) error = NULL;
	LrgVscriptNode *node;

	node = lrg_vscript_graph_create_node (graph, kind, id, 0.0, 0.0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (node);
	return node;
}

static void
link_pins (LrgVscriptProgram *program,
           LrgVscriptGraph   *graph,
           const gchar       *from_node,
           const gchar       *from_pin,
           const gchar       *to_node,
           const gchar       *to_pin)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_vscript_program_connect (program, graph,
	                                            from_node, from_pin,
	                                            to_node, to_pin,
	                                            &error));
	g_assert_no_error (error);
}

static gboolean
graph_has_prefix (LrgVscriptGraph *graph,
                  const gchar     *prefix)
{
	guint i;
	guint n;

	n = lrg_vscript_graph_get_n_nodes (graph);
	for (i = 0; i < n; i++)
	{
		LrgVscriptNode *node = lrg_vscript_graph_get_nth_node (graph, i);

		if (g_str_has_prefix (lrg_vscript_node_get_kind (node), prefix))
			return TRUE;
	}
	return FALSE;
}

static gchar *
run_log_line (const gchar *text)
{
	g_autoptr(LrgScriptingGraph) graph = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) log = NULL;

	graph = lrg_scripting_graph_new ();
	g_assert_true (lrg_scripting_load_string (LRG_SCRIPTING (graph), "t", text, &error));
	g_assert_no_error (error);
	log = lrg_scripting_graph_dup_log (graph);
	g_assert_nonnull (log);
	g_assert_nonnull (log[0]);
	return g_strdup (log[0]);
}

static void
test_round_trip_and_families (void)
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(LrgVscriptProgram) again = NULL;
	g_autoptr(LrgVscriptProgram) from_file = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) issues = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *again_text = NULL;
	g_autofree gchar *injected = NULL;
	g_autofree gchar *kept = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *log_line = NULL;
	g_autofree gchar *expected = NULL;
	g_auto(GStrv) gi_hits = NULL;
	LrgVscriptGraph *main_graph;
	LrgVscriptGraph *helper;
	LrgVscriptNode *log_node;
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
	gchar *nl;
	gboolean ok;

	program = lrg_vscript_program_new ();
	lrg_vscript_program_set_name (program, "demo");
	lrg_vscript_program_set_variable (program, "score", "int", g_variant_new_int64 (0));
	main_graph = lrg_vscript_program_get_main (program);

	add_node (main_graph, "event.start", "start");
	log_node = add_node (main_graph, "debug.log", "log");
	add_node (main_graph, "math.add", "add");
	add_node (main_graph, "value.float", "fa");
	add_node (main_graph, "value.float", "fb");
	add_node (main_graph, "flow.branch", "branch");
	add_node (main_graph, "logic.not", "not");
	add_node (main_graph, "compare.eq", "eq");
	add_node (main_graph, "string.concat", "concat");
	add_node (main_graph, "var.get", "score_get");
	lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "fa"),
	                            "value", g_variant_new_double (2.0));
	lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "fb"),
	                            "value", g_variant_new_double (3.0));
	lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "score_get"),
	                            "name", g_variant_new_string ("score"));
	link_pins (program, NULL, "start", "then", "log", "exec");
	link_pins (program, NULL, "add", "result", "log", "text");
	link_pins (program, NULL, "fa", "value", "add", "a");
	link_pins (program, NULL, "fb", "value", "add", "b");

	helper = lrg_vscript_program_add_subgraph (program, "helper", &error);
	g_assert_no_error (error);
	g_assert_true (graph_has_prefix (helper, "subgraph."));
	add_node (main_graph, "subgraph.call", "call_helper");
	lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "call_helper"),
	                            "name", g_variant_new_string ("helper"));

	if (lrg_vscript_gi_available ())
	{
		gi_hits = lrg_vscript_gi_search ("basename", 8);
		g_assert_nonnull (gi_hits);
		g_assert_nonnull (gi_hits[0]);
		add_node (main_graph, "gi.call", "gi");
		lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "gi"),
		                            "function", g_variant_new_string (gi_hits[0]));
		lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "gi"),
		                            "pure", g_variant_new_boolean (TRUE));
	}

	g_assert_true (graph_has_prefix (main_graph, "event."));
	g_assert_true (graph_has_prefix (main_graph, "flow."));
	g_assert_true (graph_has_prefix (main_graph, "math."));
	g_assert_true (graph_has_prefix (main_graph, "logic."));
	g_assert_true (graph_has_prefix (main_graph, "compare."));
	g_assert_true (graph_has_prefix (main_graph, "string."));
	g_assert_true (graph_has_prefix (main_graph, "var."));
	g_assert_true (graph_has_prefix (main_graph, "subgraph."));
	if (lrg_vscript_gi_available ())
		g_assert_true (graph_has_prefix (main_graph, "gi."));

	ok = lrg_vscript_program_validate (program, &issues);
	if (!ok && issues != NULL && issues->len > 0)
	{
		LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (issues, 0);

		g_error ("validate: %s", issue->message);
	}
	g_assert_true (ok);

	text = lrg_vscript_program_to_data (program, &error);
	g_assert_no_error (error);
	again = lrg_vscript_program_new_from_data (text, -1, &error);
	g_assert_no_error (error);
	again_text = lrg_vscript_program_to_data (again, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, again_text);

	/* An unknown top-level key is kept and written back. */
	nl = strchr (text, '\n');
	g_assert_nonnull (nl);
	injected = g_strdup_printf ("%.*s\nnote: kept%s", (gint) (nl - text), text, nl);
	g_clear_object (&again);
	again = lrg_vscript_program_new_from_data (injected, -1, &error);
	g_assert_no_error (error);
	kept = lrg_vscript_program_to_data (again, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (kept, "note:"));
	g_assert_nonnull (strstr (kept, "kept"));

	path = g_strdup ("/tmp/lrg-v090-vscript-roundtrip.rvscript");
	g_assert_true (lrg_vscript_program_save_to_file (program, path, &error));
	g_assert_no_error (error);
	from_file = lrg_vscript_program_new_from_file (path, &error);
	g_assert_no_error (error);
	g_unlink (path);
	g_assert_cmpstr (lrg_vscript_program_get_name (from_file), ==, "demo");
	g_assert_nonnull (lrg_vscript_program_get_graph (from_file, "helper"));

	log_line = run_log_line (text);
	g_ascii_dtostr (buf, sizeof buf, 5.0);
	expected = g_strdup (buf);
	g_assert_cmpstr (log_line, ==, expected);
	(void) log_node;
}

static void
test_determinism (void)
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	LrgVscriptGraph *graph;

	program = lrg_vscript_program_new ();
	lrg_vscript_program_set_seed (program, 42);
	graph = lrg_vscript_program_get_main (program);
	add_node (graph, "event.start", "start");
	add_node (graph, "debug.log", "log");
	add_node (graph, "math.random", "roll");
	link_pins (program, NULL, "start", "then", "log", "exec");
	link_pins (program, NULL, "roll", "result", "log", "text");
	text = lrg_vscript_program_to_data (program, &error);
	g_assert_no_error (error);

	a = run_log_line (text);
	b = run_log_line (text);
	g_assert_cmpstr (a, ==, b);
	g_assert_cmpstr (a, !=, "");
}

static void
test_step_budget (void)
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(LrgScriptingGraph) graph = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	LrgVscriptGraph *main_graph;

	program = lrg_vscript_program_new ();
	lrg_vscript_program_set_step_budget (program, 4);
	main_graph = lrg_vscript_program_get_main (program);
	add_node (main_graph, "event.start", "start");
	add_node (main_graph, "flow.while", "loop");
	add_node (main_graph, "value.bool", "yes");
	lrg_vscript_node_set_param (lrg_vscript_graph_get_node (main_graph, "yes"),
	                            "value", g_variant_new_boolean (TRUE));
	link_pins (program, NULL, "start", "then", "loop", "exec");
	link_pins (program, NULL, "yes", "value", "loop", "condition");
	text = lrg_vscript_program_to_data (program, &error);
	g_assert_no_error (error);

	graph = lrg_scripting_graph_new ();
	g_assert_false (lrg_scripting_load_string (LRG_SCRIPTING (graph), "loop", text, &error));
	g_assert_error (error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_BUDGET);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/vscript/round-trip-and-families", test_round_trip_and_families);
	g_test_add_func ("/vscript/determinism", test_determinism);
	g_test_add_func ("/vscript/step-budget", test_step_budget);
	return g_test_run ();
}
