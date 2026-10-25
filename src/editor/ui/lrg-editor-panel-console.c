/* lrg-editor-panel-console.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Console, History and Problems panels.
 *
 * The console shows every captured log record (engine, game, editor) with
 * level toggles, a search filter, auto-scroll and click-to-source for
 * records that carry a file, and has a command line. Commands are:
 *
 *   help                      list commands and IPC methods
 *   :action-id                run an action ("::" lists them)
 *   method key=value ...      call an IPC method with simple parameters
 *   {"method": ...}           send a raw JSON request
 *   clear                     clear the log
 *
 * so everything the IPC server offers (and therefore everything Emacs can
 * do) is also available interactively.
 */

#include "lrg-editor-app-private.h"
#include <string.h>

static LrgGuiColor
level_color (LrgEditorApp   *self,
             GLogLevelFlags  level)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);

	if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
		return style->colors[LRG_GUI_COL_ERROR];
	if (level & G_LOG_LEVEL_WARNING)
		return style->colors[LRG_GUI_COL_WARNING];
	if (level & G_LOG_LEVEL_DEBUG)
		return style->colors[LRG_GUI_COL_TEXT_DISABLED];
	return style->colors[LRG_GUI_COL_TEXT];
}

/* Turns `key=value key2="two words"` into a JSON params object. */
static gchar *
params_from_words (gchar **words)
{
	g_autoptr(JsonBuilder)   b = json_builder_new ();
	g_autoptr(JsonGenerator) gen = json_generator_new ();
	g_autoptr(JsonNode)      root = NULL;
	guint                    i;

	json_builder_begin_object (b);
	for (i = 0; words[i] != NULL; i++)
	{
		gchar       *eq = strchr (words[i], '=');
		const gchar *v;
		gchar       *end = NULL;
		gdouble      num;

		if (eq == NULL)
			continue;
		*eq = '\0';
		v = eq + 1;
		json_builder_set_member_name (b, words[i]);

		/* true/false, numbers, [x,y,z], else strings. */
		if (g_strcmp0 (v, "true") == 0 || g_strcmp0 (v, "false") == 0)
			json_builder_add_boolean_value (b, v[0] == 't');
		else if (v[0] == '[')
		{
			g_auto(GStrv) parts = g_strsplit_set (v, "[], ", -1);
			guint         k;

			json_builder_begin_array (b);
			for (k = 0; parts[k] != NULL; k++)
				if (*parts[k] != '\0')
					json_builder_add_double_value (b, g_ascii_strtod (parts[k], NULL));
			json_builder_end_array (b);
		}
		else if ((num = g_ascii_strtod (v, &end)), end != v && *end == '\0')
		{
			if (strchr (v, '.') == NULL && strchr (v, 'e') == NULL)
				json_builder_add_int_value (b, (gint64) num);
			else
				json_builder_add_double_value (b, num);
		}
		else
			json_builder_add_string_value (b, v);
	}
	json_builder_end_object (b);

	root = json_builder_get_root (b);
	json_generator_set_root (gen, root);
	return json_generator_to_data (gen, NULL);
}

void
editor_console_execute (LrgEditorApp *self,
                        const gchar  *line)
{
	g_autofree gchar *trimmed = g_strstrip (g_strdup (line));
	g_autofree gchar *echo = g_strdup_printf ("> %s", trimmed);

	if (*trimmed == '\0')
		return;

	g_ptr_array_add (self->console_history, g_strdup (trimmed));
	self->console_history_pos = -1;
	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", echo);

	if (g_strcmp0 (trimmed, "clear") == 0)
	{
		lrg_editor_log_clear (self->log);
		return;
	}

	if (g_strcmp0 (trimmed, "help") == 0)
	{
		GList   *methods = lrg_editor_server_list_methods (self->server);
		GString *out = g_string_new ("Commands: help, clear, :action, method key=value..., raw JSON.\nMethods:");
		GList   *l;

		for (l = methods; l != NULL; l = l->next)
			g_string_append_printf (out, " %s", (const gchar *) l->data);
		g_list_free (methods);
		lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", out->str);
		g_string_free (out, TRUE);
		return;
	}

	if (trimmed[0] == ':')
	{
		if (trimmed[1] == ':' || trimmed[1] == '\0')
		{
			GList   *ids = lrg_editor_actions_list (self->actions);
			GString *out = g_string_new ("Actions:");
			GList   *l;

			for (l = ids; l != NULL; l = l->next)
				g_string_append_printf (out, " %s", (const gchar *) l->data);
			g_list_free (ids);
			lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", out->str);
			g_string_free (out, TRUE);
		}
		else if (!lrg_editor_actions_activate (self->actions, trimmed + 1))
		{
			g_autofree gchar *msg = g_strdup_printf ("No enabled action '%s' (try ::)", trimmed + 1);

			lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "console", msg);
		}
		return;
	}

	/* An IPC request, raw or in the short form. */
	{
		g_autofree gchar *request = NULL;
		g_autofree gchar *response = NULL;

		if (trimmed[0] == '{')
			request = g_strdup (trimmed);
		else
		{
			g_auto(GStrv)     words = NULL;
			g_autofree gchar *params = NULL;
			g_autoptr(GError) error = NULL;

			if (!g_shell_parse_argv (trimmed, NULL, &words, &error))
			{
				lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "console", error->message);
				return;
			}
			params = params_from_words (words + 1);
			request = g_strdup_printf ("{\"id\":0,\"method\":\"%s\",\"params\":%s}", words[0], params);
		}

		response = lrg_editor_server_dispatch (self->server, request);
		if (response != NULL)
			lrg_editor_log_append (self->log,
			                       strstr (response, "\"error\"") != NULL ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE,
			                       "console", response);
	}
}

void
editor_panel_console (LrgEditorApp *self)
{
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	guint             n, i;
	gboolean          errors = (self->console_levels & G_LOG_LEVEL_CRITICAL) != 0;
	gboolean          warnings = (self->console_levels & G_LOG_LEVEL_WARNING) != 0;
	gboolean          messages = (self->console_levels & G_LOG_LEVEL_MESSAGE) != 0;
	gboolean          debug = (self->console_levels & G_LOG_LEVEL_DEBUG) != 0;
	g_autofree gchar *needle = NULL;
	gfloat            avail_w, input_h;

	/* Filters. */
	if (lrg_gui_toggle_button (self->gui, "Errors", &errors))
		self->console_levels ^= G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_toggle_button (self->gui, "Warnings", &warnings))
		self->console_levels ^= G_LOG_LEVEL_WARNING;
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_toggle_button (self->gui, "Messages", &messages))
		self->console_levels ^= G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO;
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_toggle_button (self->gui, "Debug", &debug))
		self->console_levels ^= G_LOG_LEVEL_DEBUG;
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_get_content_avail (self->gui, &avail_w, NULL);
	lrg_gui_set_next_item_width (self->gui, MAX (80.0f, avail_w - 150.0f));
	lrg_gui_input_text (self->gui, "##console-filter", "Filter log", self->console_filter, 0);
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_checkbox (self->gui, "Follow", &self->console_autoscroll);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Clear"))
		lrg_editor_log_clear (self->log);

	needle = (self->console_filter->len > 0) ? g_utf8_casefold (self->console_filter->str, -1) : NULL;

	/* Records. */
	input_h = lrg_gui_get_frame_height (self->gui) + style->item_spacing_y * 2.0f;
	lrg_gui_begin_child (self->gui, "##log", 0.0f, -input_h, LRG_GUI_REGION_SCROLL_Y | LRG_GUI_REGION_SCROLL_X |
	                     LRG_GUI_REGION_BORDER);
	n = lrg_editor_log_get_n_records (self->log);
	for (i = 0; i < n; i++)
	{
		const LrgEditorLogRecord *r = lrg_editor_log_get_record (self->log, i);
		g_autofree gchar         *line = NULL;
		g_autoptr(GDateTime)      when = NULL;
		g_autofree gchar         *stamp = NULL;

		if (r == NULL || !(r->level & self->console_levels))
			continue;
		if (needle != NULL)
		{
			g_autofree gchar *folded = g_utf8_casefold (r->message, -1);

			if (strstr (folded, needle) == NULL &&
			    (r->domain == NULL || strstr (r->domain, needle) == NULL))
				continue;
		}

		when = g_date_time_new_from_unix_local_usec (r->timestamp);
		stamp = (when != NULL) ? g_date_time_format (when, "%H:%M:%S") : g_strdup ("--:--:--");
		line = g_strdup_printf ("%s %-8s %s%s%s", stamp, lrg_editor_log_level_to_string (r->level),
		                        r->domain != NULL ? r->domain : "", r->domain != NULL ? ": " : "", r->message);

		lrg_gui_push_id_int (self->gui, (gint64) r->serial);
		lrg_gui_text_mono (self->gui, level_color (self, r->level), line);
		if (r->file != NULL)
		{
			g_autofree gchar *tip = g_strdup_printf ("%s:%d (double-click to open)", r->file, r->line);

			lrg_gui_set_item_tooltip (self->gui, tip);
			if (lrg_gui_is_item_double_clicked (self->gui))
				editor_app_open_file (self, r->file, MAX (1, r->line), 1);
		}
		if (lrg_gui_begin_popup_context_item (self->gui, "##log-line"))
		{
			if (lrg_gui_menu_item (self->gui, "Copy Line", NULL, FALSE, TRUE))
				lrg_gui_context_set_clipboard (self->gui, line);
			if (lrg_gui_menu_item (self->gui, "Copy Message", NULL, FALSE, TRUE))
				lrg_gui_context_set_clipboard (self->gui, r->message);
			lrg_gui_end_popup (self->gui);
		}
		lrg_gui_pop_id (self->gui);
	}
	if (self->console_autoscroll && n != self->console_seen)
	{
		lrg_gui_dummy (self->gui, 1.0f, 1.0f);
		lrg_gui_scroll_to_item (self->gui, 1.0f);
		self->console_seen = n;
	}
	lrg_gui_end_region (self->gui);

	/* Command line with history (Up/Down). */
	lrg_gui_set_next_item_width (self->gui, 0.0f);
	if (lrg_gui_input_text (self->gui, "##console-input", "Command: help, :action, method key=value, {json}",
	                        self->console_input,
	                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE | LRG_GUI_INPUT_TEXT_MONOSPACE |
	                        LRG_GUI_INPUT_TEXT_KEEP_FOCUS))
	{
		g_autofree gchar *cmd = g_strdup (self->console_input->str);

		g_string_truncate (self->console_input, 0);
		editor_console_execute (self, cmd);
	}
	if (lrg_gui_is_item_active (self->gui) && self->console_history->len > 0)
	{
		gint dir = 0;

		if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_UP, TRUE))
			dir = -1;
		else if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_DOWN, TRUE))
			dir = 1;
		if (dir != 0)
		{
			gint len = (gint) self->console_history->len;

			if (self->console_history_pos < 0)
				self->console_history_pos = (dir < 0) ? len - 1 : -1;
			else
				self->console_history_pos = CLAMP (self->console_history_pos + dir, -1, len - 1);
			if (self->console_history_pos >= 0)
			{
				g_string_assign (self->console_input, g_ptr_array_index (self->console_history,
				                                                         self->console_history_pos));
				/* Refocus next frame to reload the edit buffer. */
				lrg_gui_clear_keyboard_focus (self->gui);
				lrg_gui_request_focus (self->gui, "##console-input");
			}
		}
	}
}

void
editor_panel_history (LrgEditorApp *self)
{
	guint n_undo = lrg_editor_get_undo_count (self->editor);
	guint n_redo = lrg_editor_get_redo_count (self->editor);
	guint i;

	if (lrg_gui_selectable (self->gui, "(initial state)", n_undo == 0, 0))
		lrg_editor_jump_to_history (self->editor, 0);

	for (i = 0; i < n_undo + n_redo; i++)
	{
		LrgEditorCommand *cmd = (i < n_undo) ? lrg_editor_get_undo_command (self->editor, i)
		                                     : lrg_editor_get_redo_command (self->editor, i - n_undo);
		g_autofree gchar *desc = lrg_editor_command_describe (cmd);
		g_autofree gchar *label = g_strdup_printf ("%u. %s%s", i + 1, desc, i >= n_undo ? "  (undone)" : "");

		lrg_gui_push_id_int (self->gui, i);
		if (lrg_gui_selectable (self->gui, label, i + 1 == n_undo,
		                        i >= n_undo ? LRG_GUI_SELECTABLE_NONE : LRG_GUI_SELECTABLE_NONE))
			lrg_editor_jump_to_history (self->editor, i + 1);
		if (i >= n_undo)
			lrg_gui_set_item_tooltip (self->gui, "Click to redo up to here");
		lrg_gui_pop_id (self->gui);
	}

	lrg_gui_spacing (self->gui);
	if (lrg_gui_button (self->gui, "Clear History"))
		lrg_editor_clear_history (self->editor);
	lrg_gui_same_line (self->gui, -1.0f);
	{
		g_autofree gchar *limit = g_strdup_printf ("limit %u steps", lrg_editor_get_history_limit (self->editor));

		lrg_gui_text_disabled (self->gui, limit);
	}
}

void
editor_panel_problems (LrgEditorApp *self)
{
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	guint        i;

	if (lrg_gui_button (self->gui, "Validate Now"))
	{
		self->problems_dirty = TRUE;
		self->problems_time = -1000.0;
	}
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_text_disabled (self->gui, "Checks run automatically after each change.");

	if (self->problems->len == 0)
	{
		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_SUCCESS], "No problems found.");
		return;
	}

	for (i = 0; i < self->problems->len; i++)
	{
		LrgEditorProblem *p = g_ptr_array_index (self->problems, i);
		LrgGuiStyleColor  col = (p->severity == LRG_EDITOR_PROBLEM_ERROR) ? LRG_GUI_COL_ERROR
		                       : (p->severity == LRG_EDITOR_PROBLEM_WARNING) ? LRG_GUI_COL_WARNING
		                                                                      : LRG_GUI_COL_TEXT_DISABLED;
		g_autofree gchar *line = g_strdup_printf ("%-7s %s%s%s", lrg_editor_problem_severity_to_string (p->severity),
		                                          p->node_path != NULL ? p->node_path : "",
		                                          p->node_path != NULL ? ": " : "", p->message);

		lrg_gui_push_id_int (self->gui, i);
		lrg_gui_text_colored (self->gui, style->colors[col], line);
		if (lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT) && p->node_guid != NULL)
		{
			LrgNode *n = lrg_editor_find_node (self->editor, p->node_guid);

			if (n != NULL)
				lrg_editor_select (self->editor, n, FALSE);
		}
		if (lrg_gui_is_item_double_clicked (self->gui))
		{
			if (p->file != NULL && g_file_test (p->file, G_FILE_TEST_EXISTS))
				editor_app_open_file (self, p->file, 1, 1);
			else
				editor_app_frame_selection (self);
		}
		lrg_gui_set_item_tooltip (self->gui, "Click to select the node, double-click to focus it");
		lrg_gui_pop_id (self->gui);
	}
}
