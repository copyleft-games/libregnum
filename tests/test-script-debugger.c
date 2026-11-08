/* test-script-debugger.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Backend-neutral debugger: breakpoints, stepping, variables, evaluate
 * and conditions on Lua and Python; unsupported backends fail cleanly.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

typedef struct
{
	LrgScriptLanguage  language;
	gint               break_line;
	gint               inner_return;
	gint               after_call;
	gint               loop_line;
	gint               first_line;
	const gchar       *falsy;
	const gchar       *truthy;
	const gchar       *source;
} DebugCase;

typedef struct
{
	gint     phase;
	gint     nudges;
	gint     stops;
	gint     expect_a;
	gint     expect_b;
	gboolean saw_value;
	gboolean saw_hidden;
	gboolean saw_child;
	gboolean eval_ok;
	gboolean failed;
	gchar   *output;
} Drive;

static void
drive_free (gpointer data)
{
	Drive *drive = (Drive *) data;

	g_free (drive->output);
	g_free (drive);
}

static gboolean
scope_has (LrgScriptDebugger *debugger,
           const gchar       *scope_name,
           const gchar       *var_name,
           const gchar       *fragment,
           gboolean           expand)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) scopes = NULL;
	guint i;

	scopes = lrg_script_debugger_get_scopes (debugger, 0, &error);
	if (scopes == NULL)
		return FALSE;
	for (i = 0; i < scopes->len; i++)
	{
		LrgScriptScope *scope = g_ptr_array_index (scopes, i);
		g_autoptr(GPtrArray) vars = NULL;
		guint j;

		if (g_strcmp0 (lrg_script_scope_get_name (scope), scope_name) != 0)
			continue;
		vars = lrg_script_debugger_get_variables (
			debugger,
			lrg_script_scope_get_variables_reference (scope),
			&error);
		if (vars == NULL)
			return FALSE;
		for (j = 0; j < vars->len; j++)
		{
			LrgScriptVariable *var = g_ptr_array_index (vars, j);
			g_autoptr(GPtrArray) kids = NULL;
			guint k;

			if (g_strcmp0 (lrg_script_variable_get_name (var), var_name) != 0)
				continue;
			if (fragment != NULL &&
			    (lrg_script_variable_get_value (var) == NULL ||
			     strstr (lrg_script_variable_get_value (var), fragment) == NULL))
				return FALSE;
			if (!expand || lrg_script_variable_get_variables_reference (var) == 0)
				return TRUE;
			kids = lrg_script_debugger_get_variables (
				debugger,
				lrg_script_variable_get_variables_reference (var),
				&error);
			if (kids == NULL)
				return FALSE;
			for (k = 0; k < kids->len; k++)
			{
				LrgScriptVariable *child = g_ptr_array_index (kids, k);

				if (g_strcmp0 (lrg_script_variable_get_name (child), "kept") == 0)
					return TRUE;
			}
			return FALSE;
		}
	}
	return FALSE;
}

static gint
stopped_line (LrgScriptDebugger *debugger)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) frames = NULL;
	LrgScriptStackFrame *frame;

	frames = lrg_script_debugger_get_stack_frames (debugger, &error);
	if (frames == NULL || frames->len == 0)
		return -1;
	frame = g_ptr_array_index (frames, 0);
	return lrg_script_stack_frame_get_line (frame);
}

static void
idle_step (LrgScriptDebugger *debugger,
           gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;
	gint line;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgScriptVariable) evaluated = NULL;

	line = stopped_line (debugger);
	if (drive->phase == 0)
	{
		g_assert_cmpint (lrg_script_debugger_get_stop_reason (debugger),
		                 ==, LRG_SCRIPT_STOP_REASON_BREAKPOINT);
		g_assert_cmpint (line, ==, drive->expect_a);
		drive->saw_value = scope_has (debugger, "Locals", "value", "3", FALSE);
		drive->saw_child = scope_has (debugger, "Locals", "box", NULL, TRUE);
		evaluated = lrg_script_debugger_evaluate (debugger, 0, "value + 1", &error);
		drive->eval_ok = evaluated != NULL &&
		                 lrg_script_variable_get_value (evaluated) != NULL &&
		                 strstr (lrg_script_variable_get_value (evaluated), "4") != NULL;
		drive->phase = 1;
		drive->nudges = 0;
		lrg_script_debugger_step_in (debugger);
		return;
	}
	if (drive->phase == 1)
	{
		if (line == drive->expect_b)
		{
			drive->saw_hidden = scope_has (debugger, "Locals", "hidden", "4", FALSE);
			drive->phase = 2;
			lrg_script_debugger_step_out (debugger);
			return;
		}
		drive->nudges++;
		if (drive->nudges > 6)
		{
			drive->failed = TRUE;
			lrg_script_debugger_resume (debugger);
			return;
		}
		lrg_script_debugger_step_over (debugger);
		return;
	}
	if (drive->phase == 2)
	{
		if (line != drive->expect_a + 1 && line != drive->expect_b)
		{
			/* The call's next line. */
		}
		drive->phase = 3;
		g_assert_cmpint (line, ==, drive->expect_a + 1);
		lrg_script_debugger_resume (debugger);
		return;
	}
	drive->failed = TRUE;
	lrg_script_debugger_resume (debugger);
}

static void
idle_count (LrgScriptDebugger *debugger,
            gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;

	drive->stops++;
	if (drive->stops > 8)
	{
		drive->failed = TRUE;
		lrg_script_debugger_resume (debugger);
		return;
	}
	lrg_script_debugger_resume (debugger);
}

static void
idle_once (LrgScriptDebugger *debugger,
           gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;

	drive->stops++;
	drive->expect_a = stopped_line (debugger);
	drive->phase = (gint) lrg_script_debugger_get_stop_reason (debugger);
	g_free (drive->output);
	drive->output = lrg_script_debugger_dup_stop_description (debugger);
	lrg_script_debugger_resume (debugger);
}

static void
idle_over (LrgScriptDebugger *debugger,
           gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;
	gint line;

	line = stopped_line (debugger);
	if (drive->phase == 0)
	{
		g_assert_cmpint (line, ==, drive->expect_a);
		drive->phase = 1;
		lrg_script_debugger_step_over (debugger);
		return;
	}
	g_assert_cmpint (line, ==, drive->expect_b);
	drive->phase = 2;
	lrg_script_debugger_resume (debugger);
}

static void
on_output (LrgScriptDebugger *debugger,
           const gchar       *category,
           const gchar       *text,
           gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;

	(void) debugger;
	(void) category;
	g_free (drive->output);
	drive->output = g_strdup (text);
}

static gchar *
debug_source (const DebugCase *spec)
{
	return g_strdup (spec->source);
}

static gboolean
open_debuggable (const DebugCase               *spec,
                 LrgScriptingManager           *manager,
                 LrgScripting                 **scripting_out,
                 LrgScriptDebugger            **debugger_out,
                 gchar                        **path_out)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *body = NULL;
	gchar *path;
	LrgScripting *scripting;
	LrgScriptDebugger *debugger;

	if (!lrg_scripting_manager_is_available (manager, spec->language))
		return FALSE;
	dir = g_dir_make_tmp ("lrg-debug-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, spec->language == LRG_SCRIPT_LANGUAGE_LUA ? "dbg.lua" : "dbg.py", NULL);
	body = debug_source (spec);
	g_assert_true (g_file_set_contents (path, body, -1, &error));
	g_assert_no_error (error);

	scripting = lrg_scripting_manager_create_context (manager, spec->language);
	g_assert_nonnull (scripting);
	g_assert_true (lrg_script_debugger_supports (scripting));
	debugger = lrg_script_debugger_new ();
	lrg_script_debugger_set_iterate_main_context (debugger, FALSE);
	g_assert_true (lrg_script_debugger_attach (debugger, scripting, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_script_debugger_is_attached (debugger, scripting));
	g_assert_cmpuint (lrg_script_debugger_get_thread_id (debugger, scripting), >=, 1);
	g_assert_true (lrg_scripting_load_file (scripting, path, &error));
	g_assert_no_error (error);

	*scripting_out = scripting;
	*debugger_out = debugger;
	*path_out = path;
	return TRUE;
}

static void
close_debug (LrgScripting      *scripting,
             LrgScriptDebugger *debugger,
             const gchar       *path)
{
	g_autofree gchar *dir = NULL;

	lrg_script_debugger_detach (debugger, scripting);
	g_object_unref (debugger);
	g_object_unref (scripting);
	dir = g_path_get_dirname (path);
	g_unlink (path);
	g_rmdir (dir);
	g_free ((gchar *) path);
}

static void
test_debug_step (gconstpointer user_data)
{
	const DebugCase *spec = (const DebugCase *) user_data;
	LrgScriptingManager *manager;
	LrgScripting *scripting = NULL;
	LrgScriptDebugger *debugger = NULL;
	gchar *path = NULL;
	Drive *drive;
	LrgScriptBreakpoint *bp;
	g_autoptr(GError) error = NULL;
	GValue value = G_VALUE_INIT;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, spec->language))
	{
		g_test_skip ("backend not built");
		return;
	}
	g_assert_true (open_debuggable (spec, manager, &scripting, &debugger, &path));

	drive = g_new0 (Drive, 1);
	drive->expect_a = spec->break_line;
	drive->expect_b = spec->inner_return;
	lrg_script_debugger_set_stopped_idle_func (debugger, idle_step, drive, NULL);
	bp = lrg_script_debugger_add_breakpoint (debugger, path, spec->break_line);
	g_assert_nonnull (bp);
	g_assert_true (lrg_script_debugger_wants_line_events (debugger));
	g_assert_true (lrg_script_debugger_line_may_break (debugger, spec->break_line));

	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_false (drive->failed);
	g_assert_cmpint (drive->phase, ==, 3);
	g_assert_true (drive->saw_value);
	g_assert_true (drive->saw_hidden);
	g_assert_true (drive->saw_child);
	g_assert_true (drive->eval_ok);
	g_assert_cmpuint (lrg_script_breakpoint_get_hit_count (bp), >=, 1);

	lrg_script_debugger_set_stopped_idle_func (debugger, NULL, NULL, NULL);
	drive_free (drive);
	close_debug (scripting, debugger, path);
}

static void
test_debug_step_over (gconstpointer user_data)
{
	const DebugCase *spec = (const DebugCase *) user_data;
	LrgScriptingManager *manager;
	LrgScripting *scripting = NULL;
	LrgScriptDebugger *debugger = NULL;
	gchar *path = NULL;
	Drive *drive;
	g_autoptr(GError) error = NULL;
	GValue value = G_VALUE_INIT;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, spec->language))
	{
		g_test_skip ("backend not built");
		return;
	}
	g_assert_true (open_debuggable (spec, manager, &scripting, &debugger, &path));
	drive = g_new0 (Drive, 1);
	drive->expect_a = spec->break_line;
	drive->expect_b = spec->after_call;
	lrg_script_debugger_set_stopped_idle_func (debugger, idle_over, drive, NULL);
	lrg_script_debugger_add_breakpoint (debugger, path, spec->break_line);

	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->phase, ==, 2);

	lrg_script_debugger_set_stopped_idle_func (debugger, NULL, NULL, NULL);
	drive_free (drive);
	close_debug (scripting, debugger, path);
}

static void
test_debug_conditions (gconstpointer user_data)
{
	const DebugCase *spec = (const DebugCase *) user_data;
	LrgScriptingManager *manager;
	LrgScripting *scripting = NULL;
	LrgScriptDebugger *debugger = NULL;
	gchar *path = NULL;
	Drive *drive;
	LrgScriptBreakpoint *bp;
	g_autoptr(GError) error = NULL;
	GValue value = G_VALUE_INIT;
	guint hits;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, spec->language))
	{
		g_test_skip ("backend not built");
		return;
	}
	g_assert_true (open_debuggable (spec, manager, &scripting, &debugger, &path));
	drive = g_new0 (Drive, 1);
	lrg_script_debugger_set_stopped_idle_func (debugger, idle_count, drive, NULL);
	g_signal_connect (debugger, "output", G_CALLBACK (on_output), drive);

	bp = lrg_script_debugger_add_breakpoint (debugger, path, spec->break_line);
	lrg_script_breakpoint_set_condition (bp, spec->falsy);
	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->stops, ==, 0);
	g_assert_cmpuint (lrg_script_breakpoint_get_hit_count (bp), ==, 0);

	lrg_script_breakpoint_set_condition (bp, spec->truthy);
	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->stops, ==, 1);
	hits = lrg_script_breakpoint_get_hit_count (bp);
	g_assert_cmpuint (hits, ==, 1);

	lrg_script_breakpoint_set_condition (bp, NULL);
	lrg_script_breakpoint_set_log_message (bp, "v={value}");
	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->stops, ==, 1);
	g_assert_nonnull (drive->output);
	g_assert_nonnull (strstr (drive->output, "v="));

	lrg_script_debugger_clear_breakpoints (debugger, path);
	bp = lrg_script_debugger_add_breakpoint (debugger, path, spec->loop_line);
	lrg_script_breakpoint_set_hit_condition (bp, "%2");
	lrg_script_breakpoint_reset_hit_count (bp);
	drive->stops = 0;
	g_assert_true (lrg_scripting_call_function (scripting, "loop", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->stops, ==, 2);
	g_assert_cmpuint (lrg_script_breakpoint_get_hit_count (bp), ==, 4);

	lrg_script_debugger_set_stopped_idle_func (debugger, NULL, NULL, NULL);
	drive_free (drive);
	close_debug (scripting, debugger, path);
}

static void
test_debug_pause_and_exception (gconstpointer user_data)
{
	const DebugCase *spec = (const DebugCase *) user_data;
	LrgScriptingManager *manager;
	LrgScripting *scripting = NULL;
	LrgScriptDebugger *debugger = NULL;
	gchar *path = NULL;
	Drive *drive;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *description = NULL;
	GValue value = G_VALUE_INIT;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, spec->language))
	{
		g_test_skip ("backend not built");
		return;
	}
	g_assert_true (open_debuggable (spec, manager, &scripting, &debugger, &path));
	drive = g_new0 (Drive, 1);
	lrg_script_debugger_set_stopped_idle_func (debugger, idle_once, drive, NULL);

	g_assert_cmpint (lrg_script_debugger_get_exception_mode (debugger),
	                 ==, LRG_SCRIPT_EXCEPTION_MODE_NEVER);
	lrg_script_debugger_pause (debugger);
	g_assert_true (lrg_scripting_call_function (scripting, "outer", &value, 0, NULL, &error));
	g_assert_no_error (error);
	g_value_unset (&value);
	g_assert_cmpint (drive->stops, ==, 1);
	g_assert_cmpint (drive->phase, ==, (gint) LRG_SCRIPT_STOP_REASON_PAUSE);
	g_assert_cmpint (drive->expect_a, ==, spec->first_line);
	g_assert_false (lrg_script_debugger_is_paused (debugger));

	drive->stops = 0;
	lrg_script_debugger_set_exception_mode (debugger, LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT);
	g_assert_false (lrg_scripting_call_function (scripting, "boom", &value, 0, NULL, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_cmpint (drive->stops, ==, 1);
	g_assert_cmpint (drive->phase, ==, (gint) LRG_SCRIPT_STOP_REASON_EXCEPTION);
	g_assert_nonnull (drive->output);
	g_assert_nonnull (strstr (drive->output, "boom-marker"));
	description = lrg_script_debugger_dup_stop_description (debugger);
	g_assert_null (description);

	lrg_script_debugger_set_stopped_idle_func (debugger, NULL, NULL, NULL);
	drive_free (drive);
	close_debug (scripting, debugger, path);
}

static void
idle_node (LrgScriptDebugger *debugger,
           gpointer           user_data)
{
	Drive *drive = (Drive *) user_data;
	g_autoptr(LrgScriptBreakpoint) bp = NULL;

	drive->stops++;
	drive->phase = (gint) lrg_script_debugger_get_stop_reason (debugger);
	bp = lrg_script_debugger_dup_stopped_breakpoint (debugger);
	drive->saw_value = bp != NULL &&
	                   g_strcmp0 (lrg_script_breakpoint_get_node_id (bp), "gate") == 0;
	lrg_script_debugger_resume (debugger);
}

static void
test_debug_api (void)
{
	LrgScriptingManager *manager;
	g_autoptr(LrgScriptDebugger) debugger = NULL;
	g_autoptr(GError) error = NULL;
	LrgScriptBreakpoint *bp;
	LrgScriptBreakpoint *again;
	g_autofree gchar *norm = NULL;
	g_autoptr(GPtrArray) all = NULL;
	const gchar *file;

	debugger = lrg_script_debugger_new ();
	g_assert_true (lrg_script_debugger_get_default () != NULL);
	g_assert_false (lrg_script_debugger_get_auto_attach (debugger));
	lrg_script_debugger_set_auto_attach (debugger, FALSE);

	file = "/tmp/lrg-debug-api.lua";
	norm = lrg_script_debugger_normalize_path ("@/tmp/lrg-debug-api.lua");
	g_assert_nonnull (norm);
	g_assert_null (strchr (norm, '@'));

	bp = lrg_script_debugger_add_breakpoint (debugger, file, 4);
	again = lrg_script_debugger_add_breakpoint (debugger, file, 4);
	g_assert_true (bp == again);
	g_assert_cmpuint (lrg_script_breakpoint_get_id (bp), >=, 1);
	g_assert_cmpint (lrg_script_breakpoint_get_line (bp), ==, 4);
	g_assert_true (lrg_script_breakpoint_get_enabled (bp));
	g_assert_true (lrg_script_breakpoint_get_verified (bp));
	lrg_script_breakpoint_set_enabled (bp, FALSE);
	g_assert_false (lrg_script_breakpoint_get_enabled (bp));
	lrg_script_breakpoint_set_enabled (bp, TRUE);

	again = lrg_script_debugger_add_node_breakpoint (debugger, file, "gate");
	g_assert_cmpstr (lrg_script_breakpoint_get_node_id (again), ==, "gate");
	g_assert_true (lrg_script_debugger_add_node_breakpoint (debugger, file, "gate") == again);
	g_assert_true (lrg_script_debugger_find_breakpoint (debugger, lrg_script_breakpoint_get_id (bp)) == bp);
	all = lrg_script_debugger_get_breakpoints (debugger);
	g_assert_cmpuint (all->len, >=, 2);
	g_assert_cmpstr (lrg_script_stop_reason_to_string (LRG_SCRIPT_STOP_REASON_BREAKPOINT),
	                 ==, "breakpoint");
	g_assert_true (lrg_script_debugger_state_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_script_stop_reason_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_script_exception_mode_get_type () != G_TYPE_INVALID);
	g_assert_true (lrg_script_debugger_error_get_type () != G_TYPE_INVALID);
	g_assert_cmpuint (lrg_script_debugger_error_quark (), !=, 0);

	g_assert_true (lrg_script_debugger_remove_breakpoint (debugger, bp));
	lrg_script_debugger_clear_breakpoints (debugger, NULL);

	manager = lrg_scripting_manager_get_default ();
	if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_GJS))
	{
		g_autoptr(LrgScripting) scripting = NULL;

		scripting = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_GJS);
		g_assert_false (lrg_script_debugger_supports (scripting));
		g_assert_false (lrg_script_debugger_attach (debugger, scripting, &error));
		g_assert_error (error, LRG_SCRIPT_DEBUGGER_ERROR, LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED);
		g_assert_nonnull (strstr (error->message, "SpiderMonkey"));
		g_clear_error (&error);
	}
	if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_CRISPY))
	{
		g_autoptr(LrgScripting) scripting = NULL;

		scripting = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_CRISPY);
		g_assert_false (lrg_script_debugger_attach (debugger, scripting, &error));
		g_assert_error (error, LRG_SCRIPT_DEBUGGER_ERROR, LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED);
		g_assert_nonnull (strstr (error->message, "Crispy"));
		g_clear_error (&error);
	}
	if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_NATIVE))
	{
		g_autoptr(LrgScripting) scripting = NULL;

		scripting = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_NATIVE);
		if (scripting != NULL && !lrg_script_debugger_supports (scripting))
		{
			g_assert_false (lrg_script_debugger_attach (debugger, scripting, &error));
			g_assert_error (error, LRG_SCRIPT_DEBUGGER_ERROR, LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED);
			g_assert_nonnull (strstr (error->message, "native"));
			g_clear_error (&error);
		}
	}
}

static void
test_debug_report_node (void)
{
	LrgScriptingManager *manager;
	g_autoptr(LrgScripting) scripting = NULL;
	g_autoptr(LrgScriptDebugger) debugger = NULL;
	g_autoptr(GError) error = NULL;
	Drive *drive;
	LrgScriptLanguage language = LRG_SCRIPT_LANGUAGE_NONE;

	manager = lrg_scripting_manager_get_default ();
	if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_LUA))
		language = LRG_SCRIPT_LANGUAGE_LUA;
	else if (lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_PYTHON))
		language = LRG_SCRIPT_LANGUAGE_PYTHON;
	else
	{
		g_test_skip ("no debuggable backend");
		return;
	}

	scripting = lrg_scripting_manager_create_context (manager, language);
	debugger = lrg_script_debugger_new ();
	lrg_script_debugger_set_iterate_main_context (debugger, FALSE);
	g_assert_true (lrg_script_debugger_attach (debugger, scripting, &error));
	g_assert_no_error (error);
	lrg_script_debugger_add_node_breakpoint (debugger, "graph.vscript", "gate");

	drive = g_new0 (Drive, 1);
	lrg_script_debugger_set_stopped_idle_func (debugger, idle_node, drive, NULL);
	lrg_script_debugger_report_node (debugger,
	                                 LRG_SCRIPT_DEBUGGABLE (scripting),
	                                 "graph.vscript",
	                                 "gate",
	                                 1);
	g_assert_cmpint (drive->stops, ==, 1);
	g_assert_cmpint (drive->phase, ==, (gint) LRG_SCRIPT_STOP_REASON_BREAKPOINT);
	g_assert_true (drive->saw_value);
	g_assert_cmpuint (lrg_script_debugger_get_stopped_thread_id (debugger), ==, 0);

	lrg_script_debugger_emit_output (debugger, "console", "note");
	lrg_script_debugger_set_stopped_idle_func (debugger, NULL, NULL, NULL);
	drive_free (drive);
}

static const gchar *LUA_SOURCE =
	"local function inner()\n"
	"    local hidden = 4\n"
	"    return hidden\n"
	"end\n"
	"\n"
	"function outer()\n"
	"    local value = 3\n"
	"    local box = { kept = value }\n"
	"    inner()\n"
	"    return value\n"
	"end\n"
	"\n"
	"function loop()\n"
	"    local i = 0\n"
	"    while i < 4 do\n"
	"        i = i + 1\n"
	"    end\n"
	"    return i\n"
	"end\n"
	"\n"
	"function boom()\n"
	"    error(\"boom-marker\")\n"
	"end\n";

static const gchar *PYTHON_SOURCE =
	"def inner():\n"
	"    hidden = 4\n"
	"    return hidden\n"
	"\n"
	"def outer():\n"
	"    value = 3\n"
	"    box = {'kept': value}\n"
	"    inner()\n"
	"    return value\n"
	"\n"
	"def loop():\n"
	"    i = 0\n"
	"    while i < 4:\n"
	"        i = i + 1\n"
	"    return i\n"
	"\n"
	"def boom():\n"
	"    raise RuntimeError('boom-marker')\n";

int
main (int   argc,
      char *argv[])
{
	static const DebugCase lua_case = {
		LRG_SCRIPT_LANGUAGE_LUA, 9, 3, 10, 16, 7, "false", "true", NULL
	};
	static const DebugCase python_case = {
		LRG_SCRIPT_LANGUAGE_PYTHON, 8, 3, 9, 14, 6, "False", "True", NULL
	};
	DebugCase lua;
	DebugCase python;

	g_test_init (&argc, &argv, NULL);

	lua = lua_case;
	lua.source = LUA_SOURCE;
	python = python_case;
	python.source = PYTHON_SOURCE;

	g_test_add_data_func ("/script-debugger/lua/step", &lua, test_debug_step);
	g_test_add_data_func ("/script-debugger/python/step", &python, test_debug_step);
	g_test_add_data_func ("/script-debugger/lua/step-over-stop", &lua, test_debug_step_over);
	g_test_add_data_func ("/script-debugger/python/step-over-stop", &python, test_debug_step_over);
	g_test_add_data_func ("/script-debugger/lua/conditions", &lua, test_debug_conditions);
	g_test_add_data_func ("/script-debugger/python/conditions", &python, test_debug_conditions);
	g_test_add_data_func ("/script-debugger/lua/pause-exception", &lua, test_debug_pause_and_exception);
	g_test_add_data_func ("/script-debugger/python/pause-exception", &python, test_debug_pause_and_exception);
	g_test_add_func ("/script-debugger/api", test_debug_api);
	g_test_add_func ("/script-debugger/report-node", test_debug_report_node);

	return g_test_run ();
}
