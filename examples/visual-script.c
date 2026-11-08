/* visual-script.c - Build a .rvscript, round-trip it, and run it headless.
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An event.start node fires a debug.log. The log text is a math.add of 2
 * and 3, so the line is the sum. The program is written to canonical YAML,
 * parsed back, and the two texts are compared. Then LrgScriptingGraph runs
 * the text: load_string dispatches start, and one update follows.
 *
 * Usage: visual-script
 */

#include <libregnum.h>
#include <stdio.h>
#include <string.h>

static gboolean
fail (const gchar *what,
      GError      *error)
{
	g_printerr ("visual-script: %s: %s\n", what,
	            error != NULL ? error->message : "failed");
	return FALSE;
}

int
main (int argc,
      char *argv[])
{
	g_autoptr(LrgVscriptProgram) program = NULL;
	g_autoptr(LrgVscriptProgram) again = NULL;
	g_autoptr(LrgScriptingGraph) running = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *text_again = NULL;
	g_auto(GStrv) log = NULL;
	LrgVscriptGraph *graph;
	LrgVscriptNode *sum;
	GValue dt = G_VALUE_INIT;
	guint i;

	(void) argc;
	(void) argv;

	program = lrg_vscript_program_new ();
	lrg_vscript_program_set_name (program, "hello");
	lrg_vscript_program_set_seed (program, 1);
	graph = lrg_vscript_program_get_main (program);

	if (lrg_vscript_graph_create_node (graph, "event.start", "start", 40, 40, &error) == NULL)
		return fail ("start", error) ? 0 : 1;
	sum = lrg_vscript_graph_create_node (graph, "math.add", "sum", 40, 160, &error);
	if (sum == NULL)
		return fail ("sum", error) ? 0 : 1;
	if (lrg_vscript_graph_create_node (graph, "debug.log", "log", 280, 40, &error) == NULL)
		return fail ("log", error) ? 0 : 1;

	lrg_vscript_node_set_input (sum, "a", g_variant_new_double (2));
	lrg_vscript_node_set_input (sum, "b", g_variant_new_double (3));

	if (!lrg_vscript_program_connect (program, NULL, "start", "then", "log", "exec", &error))
		return fail ("connect exec", error) ? 0 : 1;
	if (!lrg_vscript_program_connect (program, NULL, "sum", "result", "log", "text", &error))
		return fail ("connect data", error) ? 0 : 1;

	text = lrg_vscript_program_to_data (program, &error);
	if (text == NULL)
		return fail ("to_data", error) ? 0 : 1;
	again = lrg_vscript_program_new_from_data (text, -1, &error);
	if (again == NULL)
		return fail ("parse", error) ? 0 : 1;
	text_again = lrg_vscript_program_to_data (again, &error);
	if (text_again == NULL)
		return fail ("to_data again", error) ? 0 : 1;
	if (strcmp (text, text_again) != 0)
	{
		g_printerr ("visual-script: round trip changed the text\n");
		return 1;
	}

	running = lrg_scripting_graph_new ();
	if (!lrg_scripting_load_string (LRG_SCRIPTING (running), "hello", text, &error))
		return fail ("load", error) ? 0 : 1;

	g_value_init (&dt, G_TYPE_DOUBLE);
	g_value_set_double (&dt, 1.0 / 60.0);
	if (!lrg_scripting_call_function (LRG_SCRIPTING (running), "update", NULL, 1, &dt, &error))
	{
		g_value_unset (&dt);
		return fail ("update", error) ? 0 : 1;
	}
	g_value_unset (&dt);

	log = lrg_scripting_graph_dup_log (running);
	if (log == NULL || log[0] == NULL || strstr (log[0], "5") == NULL)
	{
		g_printerr ("visual-script: expected the sum 5 in the log, got %s\n",
		            (log != NULL && log[0] != NULL) ? log[0] : "(empty)");
		return 1;
	}
	for (i = 0; log[i] != NULL; i++)
		g_print ("%s\n", log[i]);
	return 0;
}
