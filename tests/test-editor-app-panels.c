/* test-editor-app-panels.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The utility panels of lrgeditor, driven headless through the canvas
 * (tests/editor-app-fixture.h) and IPC:
 *
 *   console    level filters and counts, search, copy / copy all, clear,
 *              the command line (:action, METHOD k=v, raw JSON, help)
 *              and its Up / Down history
 *   history    rows per undo step, click to jump (undo and redo)
 *   problems   validation rows, Validate Now, click selects the node
 *   profiler   frame ring, graph hover, pause / clear, profiler.sample
 *   timeline   key the selection (I), drag keys (one undo step, 10 ms
 *              snap), delete keys, scrub the ruler, play / pause (K)
 *   tiles      grid, layer add / rename / hide (undoable), tool chips,
 *              the tileset drop zone and the palette size
 *   latch      lrg_canvas_input_apply_latch (clicks and chords shorter
 *              than a frame), also through a canvas
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <math.h>
#include <raylib.h>
#include <libregnum.h>

#include "editor-app-fixture.h"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The widget of a docked panel, presented (its tab raised) and laid out. */
static LrgWidget *
present (EditorAppFixture *f,
         const gchar      *id)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *panel = lrg_dock_area_get_panel (dock, id);

	g_assert_nonnull (panel);
	lrg_dock_area_present_panel (dock, panel);
	efx_frames (f, 2);
	return lrg_dock_panel_get_child (panel);
}

/* Docks @widget as a new tab next to Assets and presents it (for the
 * History and Problems panels until the shell creates them itself). */
static LrgWidget *
dock_extra (EditorAppFixture *f,
            const gchar      *id,
            LrgWidget        *widget)
{
	LrgDockPanel *panel = lrg_dock_panel_new (id, id);

	lrg_dock_panel_set_child (panel, widget);
	g_object_unref (widget);
	lrg_editor_app_add_panel (f->app, panel, "tab:assets");
	lrg_dock_area_present_panel (lrg_editor_app_get_dock (f->app), panel);
	efx_frames (f, 2);
	return widget;
}

static LrgWidget *
find (EditorAppFixture *f,
      const gchar      *name)
{
	LrgWidget *w = efx_find (f, name);

	if (w == NULL)
		g_error ("no widget named %s", name);
	return w;
}

/* Scrolls @name into view. The jump is instant: the tiles panel is
 * taller than its viewport, and a spring leaves the click short. */
static void
reveal (EditorAppFixture *f,
        const gchar      *scroll,
        const gchar      *name)
{
	LrgScrollBox *box = LRG_SCROLL_BOX (find (f, scroll));

	lrg_scroll_box_set_kinetic (box, FALSE);
	lrg_scroll_box_scroll_to_widget (box, find (f, name));
	efx_frames (f, 2);
}

/* A named widget inside @root (the docked copy of a panel shares names) */
static LrgWidget *
in_panel (LrgWidget   *root,
          const gchar *name)
{
	LrgWidget *w = lrg_widget_find (root, name);

	if (w == NULL)
		g_error ("no widget named %s", name);
	return w;
}

/* Creates a node over IPC and returns its GUID (owned). */
static gchar *
create_node (EditorAppFixture *f,
             const gchar      *params)
{
	g_autofree gchar   *req = g_strdup_printf ("{'id':1,'method':'node.create','params':%s}", params);
	g_autoptr(JsonNode) r = efx_ipc_result (f, req);

	return g_strdup (json_object_get_string_member (json_node_get_object (r), "guid"));
}

static void
select_guid (EditorAppFixture *f,
             const gchar      *guid)
{
	LrgEditor *editor = lrg_editor_app_get_editor (f->app);

	lrg_editor_select (editor, lrg_editor_find_node (editor, guid), FALSE);
	efx_frame (f);
}

static void
type_line (EditorAppFixture *f,
           LrgWidget        *input,
           const gchar      *text)
{
	const gchar *p = text;

	/* One frame carries at most LRG_CANVAS_INPUT_MAX_KEYS characters */
	efx_click_widget (f, input);
	while (*p != '\0')
	{
		g_autofree gchar *chunk = NULL;
		const gchar      *end = p;
		guint             n = 0;

		while (*end != '\0' && n < LRG_CANVAS_INPUT_MAX_KEYS)
		{
			end = g_utf8_next_char (end);
			n++;
		}
		chunk = g_strndup (p, (gsize) (end - p));
		efx_type (f, chunk);
		p = end;
	}
	efx_key (f, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
}

static const gchar *
clipboard (EditorAppFixture *f)
{
	return g_object_get_data (G_OBJECT (f->app), "lrg-editor-clipboard");
}

/* ==========================================================================
 * Console
 * ========================================================================== */

static void
test_console_filters (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorConsolePanel *console = LRG_EDITOR_CONSOLE_PANEL (present (f, "console"));
	LrgEditorLog          *log = lrg_editor_log_install ();
	guint                  errors, warnings, messages, debug, shown;
	gfloat                 x, y, w, h;

	lrg_editor_log_clear (log);
	lrg_editor_log_append (log, G_LOG_LEVEL_CRITICAL, "test", "first failure");
	lrg_editor_log_append (log, G_LOG_LEVEL_CRITICAL, "test", "second failure");
	lrg_editor_log_append (log, G_LOG_LEVEL_WARNING, "test", "a warning about Doors");
	lrg_editor_log_append (log, G_LOG_LEVEL_MESSAGE, "test", "hello one");
	lrg_editor_log_append (log, G_LOG_LEVEL_MESSAGE, "test", "hello two");
	lrg_editor_log_append (log, G_LOG_LEVEL_INFO, "game", "door opened");
	lrg_editor_log_append (log, G_LOG_LEVEL_DEBUG, "test", "debug detail");
	efx_frame (f);

	/* Counts on the chips; the default filter hides debug */
	errors = lrg_editor_console_panel_get_level_count (console, LRG_EDITOR_CONSOLE_LEVEL_ERROR);
	warnings = lrg_editor_console_panel_get_level_count (console, LRG_EDITOR_CONSOLE_LEVEL_WARNING);
	messages = lrg_editor_console_panel_get_level_count (console, LRG_EDITOR_CONSOLE_LEVEL_MESSAGE);
	debug = lrg_editor_console_panel_get_level_count (console, LRG_EDITOR_CONSOLE_LEVEL_DEBUG);
	g_assert_cmpuint (errors, ==, 2);
	g_assert_cmpuint (warnings, ==, 1);
	g_assert_cmpuint (messages, >=, 3);
	g_assert_cmpuint (debug, ==, 1);
	shown = lrg_editor_console_panel_get_n_shown (console);
	g_assert_cmpuint (shown, ==, errors + warnings + messages);

	/* Debug chip on, errors chip off */
	efx_click_widget (f, find (f, "console-debug"));
	g_assert_cmpuint (lrg_editor_console_panel_get_n_shown (console), ==, shown + 1);
	efx_click_widget (f, find (f, "console-errors"));
	g_assert_cmpuint (lrg_editor_console_panel_get_n_shown (console), ==, shown + 1 - errors);
	g_assert_false (lrg_editor_console_panel_get_levels (console) & LRG_EDITOR_CONSOLE_LEVEL_ERROR);
	efx_click_widget (f, find (f, "console-errors"));

	/* Search: case-insensitive, message or domain */
	efx_click_widget (f, find (f, "console-search"));
	efx_type (f, "DOOR");
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_console_panel_get_search (console), ==, "DOOR");
	g_assert_cmpuint (lrg_editor_console_panel_get_n_shown (console), ==, 2);
	g_assert_nonnull (strstr (lrg_editor_console_panel_get_row_text (console, 0), "Doors"));

	/* Copy the selected row, copy all */
	g_assert_true (lrg_list_view_get_row_rect (LRG_LIST_VIEW (find (f, "console-list")), 1, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	efx_click_widget (f, find (f, "console-copy"));
	g_assert_nonnull (strstr (clipboard (f), "door opened"));
	efx_click_widget (f, find (f, "console-copy-all"));
	g_assert_nonnull (strstr (clipboard (f), "Doors"));
	g_assert_nonnull (strstr (clipboard (f), "door opened"));

	/* Clear empties the log */
	lrg_editor_console_panel_set_search (console, NULL);
	efx_click_widget (f, find (f, "console-clear"));
	g_assert_cmpuint (lrg_editor_console_panel_get_level_count (console, LRG_EDITOR_CONSOLE_LEVEL_ALL), ==, 0);
	g_assert_cmpuint (lrg_editor_console_panel_get_n_shown (console), ==, 0);
}

/* The newest row whose text contains @needle, or NULL. */
static const gchar *
find_row (LrgEditorConsolePanel *console,
          const gchar           *needle)
{
	guint i = lrg_editor_console_panel_get_n_shown (console);

	while (i-- > 0)
	{
		const gchar *text = lrg_editor_console_panel_get_row_text (console, i);

		if (strstr (text, needle) != NULL)
			return text;
	}
	return NULL;
}

static void
test_console_commands (EditorAppFixture *f,
                       gconstpointer     data)
{
	LrgEditorConsolePanel *console = LRG_EDITOR_CONSOLE_PANEL (present (f, "console"));
	LrgEditor             *editor = lrg_editor_app_get_editor (f->app);
	LrgWidget             *cmd = find (f, "console-command");
	g_autoptr(GPtrArray)   found = NULL;

	lrg_editor_log_clear (lrg_editor_log_install ());
	lrg_editor_console_panel_set_levels (console, LRG_EDITOR_CONSOLE_LEVEL_ALL);

	/* METHOD key=value: an IPC call, the reply is logged */
	type_line (f, cmd, "node.create name=\"Big Door\" primitive=cube");
	found = lrg_editor_find_nodes (editor, "Big Door");
	g_assert_cmpuint (found->len, ==, 1);
	g_assert_nonnull (find_row (console, "> node.create"));
	g_assert_nonnull (find_row (console, "\"result\""));
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (cmd)), ==, "");

	/* :action runs an action; an unknown one warns */
	g_assert_true (lrg_editor_can_undo (editor));
	type_line (f, cmd, ":edit.undo");
	g_assert_false (lrg_editor_can_undo (editor));
	type_line (f, cmd, ":no.such.action");
	g_assert_nonnull (find_row (console, "No enabled action 'no.such.action'"));

	/* Raw JSON and help */
	type_line (f, cmd, "{\"id\":7,\"method\":\"ping\"}");
	g_assert_nonnull (find_row (console, "\"id\":7"));
	lrg_editor_console_panel_execute (console, "help");
	g_assert_nonnull (find_row (console, "profiler.sample"));

	/* Up / Down walk the history, newest first */
	efx_click_widget (f, cmd);
	efx_key (f, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (cmd)), ==, "help");
	efx_key (f, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (cmd)), ==, "{\"id\":7,\"method\":\"ping\"}");
	efx_key (f, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (cmd)), ==, "help");
	efx_key (f, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (cmd)), ==, "");

	/* The registered actions */
	g_assert_true (lrg_editor_actions_has (lrg_editor_app_get_actions (f->app), "console.clear"));
	g_assert_true (lrg_editor_app_activate_action (f->app, "console.clear"));
	g_assert_cmpuint (lrg_editor_console_panel_get_n_shown (console), ==, 0);
}

/* ==========================================================================
 * History and Problems
 * ========================================================================== */

static void
test_history_jump (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorHistoryPanel *history;
	LrgEditor             *editor = lrg_editor_app_get_editor (f->app);
	LrgListView           *list;
	g_autofree gchar      *a = NULL;
	g_autofree gchar      *b = NULL;
	g_autofree gchar      *c = NULL;
	gfloat                 x, y, w, h;

	history = LRG_EDITOR_HISTORY_PANEL (dock_extra (f, "history-p5",
	                                                LRG_WIDGET (lrg_editor_history_panel_new (f->app))));
	a = create_node (f, "{'name':'A'}");
	b = create_node (f, "{'name':'B'}");
	c = create_node (f, "{'name':'C'}");
	efx_frame (f);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 3);
	g_assert_cmpuint (lrg_editor_history_panel_get_n_rows (history), ==, 4);
	g_assert_cmpstr (lrg_editor_history_panel_get_row_label (history, 0), ==, "0. (initial state)");

	/* Click the first step: two steps are undone (and stay listed) */
	list = LRG_LIST_VIEW (lrg_widget_find (LRG_WIDGET (history), "history-list"));
	g_assert_nonnull (list);
	g_assert_true (lrg_list_view_get_row_rect (list, 1, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 1);
	g_assert_cmpuint (lrg_editor_get_redo_count (editor), ==, 2);
	g_assert_nonnull (lrg_editor_find_node (editor, a));
	g_assert_null (lrg_editor_find_node (editor, c));
	g_assert_cmpuint (lrg_editor_history_panel_get_n_rows (history), ==, 4);
	g_assert_nonnull (strstr (lrg_editor_history_panel_get_row_label (history, 3), "(undone)"));

	/* Click the last row: redo everything */
	g_assert_true (lrg_list_view_get_row_rect (list, 3, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 3);
	g_assert_nonnull (lrg_editor_find_node (editor, c));

	/* Initial state */
	lrg_editor_history_panel_activate_row (history, 0);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, 0);
	g_assert_null (lrg_editor_find_node (editor, a));

	/* Clear History */
	lrg_editor_history_panel_activate_row (history, 2);
	efx_click_widget (f, find (f, "history-clear"));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor) + lrg_editor_get_redo_count (editor), ==, 0);
	g_assert_cmpuint (lrg_editor_history_panel_get_n_rows (history), ==, 1);
}

static void
test_problems_select (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorProblemsPanel *problems;
	LrgEditor              *editor = lrg_editor_app_get_editor (f->app);
	g_autofree gchar       *bad = NULL;
	LrgListView            *list;
	gfloat                  x, y, w, h;
	guint                   n;
	gint                    row;

	problems = LRG_EDITOR_PROBLEMS_PANEL (dock_extra (f, "problems-p5",
	                                                  LRG_WIDGET (lrg_editor_problems_panel_new (f->app))));
	efx_click_widget (f, in_panel (LRG_WIDGET (problems), "problems-validate"));
	g_assert_cmpuint (lrg_editor_problems_panel_get_n_rows (problems), ==, 0);
	g_assert_true (lrg_widget_get_visible (in_panel (LRG_WIDGET (problems), "problems-none")));

	/* A name with a slash is a problem; Validate Now lists it */
	bad = create_node (f, "{'name':'left/right'}");
	lrg_editor_selection_clear (lrg_editor_get_selection (editor));
	efx_click_widget (f, in_panel (LRG_WIDGET (problems), "problems-validate"));
	n = lrg_editor_problems_panel_get_n_rows (problems);
	g_assert_cmpuint (n, >=, 1);
	g_assert_false (lrg_widget_get_visible (in_panel (LRG_WIDGET (problems), "problems-none")));

	/* Clicking the row selects the node (the level had no other problem) */
	list = LRG_LIST_VIEW (in_panel (LRG_WIDGET (problems), "problems-list"));
	row = 0;
	g_assert_true (lrg_list_view_get_row_rect (list, (guint) row, &x, &y, &w, &h));
	efx_click (f, x + w * 0.5f, y + h * 0.5f);
	g_assert_cmpstr (lrg_node_get_guid (lrg_editor_selection_get_primary (lrg_editor_get_selection (editor))), ==,
	                 bad);
}

/* ==========================================================================
 * Profiler
 * ========================================================================== */

static void
test_profiler_sample (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorProfilerPanel *prof = LRG_EDITOR_PROFILER_PANEL (present (f, "profiler"));
	LrgProfiler            *profiler = lrg_editor_profiler_panel_get_profiler (prof);
	g_autoptr(JsonNode)     r = NULL;
	g_autofree gfloat      *frames = NULL;
	JsonObject             *o;
	JsonArray              *sections;
	LrgWidget              *graph;
	guint                   n, i;
	gboolean                seen = FALSE;
	gfloat                  x, y, w, h;

	lrg_editor_profiler_panel_clear (prof);
	efx_frames (f, 10);
	g_assert_true (lrg_profiler_is_enabled (profiler));

	/* One frame recorded per canvas update, in milliseconds */
	frames = lrg_editor_profiler_panel_get_frames (prof, &n);
	g_assert_cmpuint (n, ==, 10);
	g_assert_cmpfloat_with_epsilon (frames[n - 1], 1000.0f / 60.0f, 0.01f);

	/* A section timed between frames shows up in the table and IPC */
	lrg_profiler_begin_section (profiler, "test.section");
	lrg_profiler_end_section (profiler, "test.section");
	efx_frame (f);
	g_assert_cmpuint (lrg_editor_profiler_panel_get_n_sections (prof), >=, 1);

	r = efx_ipc_result (f, "{'id':1,'method':'profiler.sample','params':{'frames':5}}");
	o = json_node_get_object (r);
	g_assert_true (json_object_get_boolean_member (o, "enabled"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "frames")), ==, 5);
	sections = json_object_get_array_member (o, "sections");
	for (i = 0; i < json_array_get_length (sections); i++)
		if (g_strcmp0 (json_object_get_string_member (json_array_get_object_element (sections, i), "name"),
		               "test.section") == 0)
			seen = TRUE;
	g_assert_true (seen);

	/* Hovering the newest bar */
	graph = find (f, "profiler-graph");
	efx_rect_of (graph, &x, &y, &w, &h);
	efx_move (f, x + w - 1.0f, y + h * 0.5f);
	g_assert_cmpint (lrg_editor_profiler_panel_get_hovered_frame (prof), >=, 0);

	/* Pause freezes the ring, Clear empties it.
	 * The readout updates on an idle and the toolbar then drops labels,
	 * which shifts every chip. Land that reflow before the click samples. */
	efx_frame (f);
	efx_click_widget (f, find (f, "profiler-pause"));
	g_assert_true (lrg_editor_profiler_panel_get_paused (prof));
	g_free (frames);
	frames = lrg_editor_profiler_panel_get_frames (prof, &n);
	efx_frames (f, 5);
	g_free (frames);
	frames = lrg_editor_profiler_panel_get_frames (prof, &i);
	g_assert_cmpuint (i, ==, n);
	efx_click_widget (f, find (f, "profiler-clear"));
	g_free (frames);
	frames = lrg_editor_profiler_panel_get_frames (prof, &n);
	g_assert_cmpuint (n, ==, 0);
	/* The shell reports its own phases (ui.update, ui.input) */
	efx_frame (f);
	g_assert_cmpuint (lrg_editor_profiler_panel_get_n_sections (prof), >=, 2);

	/* Hidden and idle: the panel turns the profiler off again */
	present (f, "assets");
	efx_frames (f, 2);
	g_assert_false (lrg_profiler_is_enabled (profiler));

	/* ... unless pinned through IPC */
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':2,'method':'profiler.sample','params':{'enable':true}}");
	efx_frames (f, 2);
	g_assert_true (lrg_profiler_is_enabled (profiler));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':3,'method':'profiler.sample','params':{'enable':false}}");
	efx_frames (f, 2);
	g_assert_false (lrg_profiler_is_enabled (profiler));
}

/* ==========================================================================
 * Timeline
 * ========================================================================== */

/* The world centre of a key in the view. */
static void
key_point (LrgEditorTimelinePanel *panel,
           const gchar            *guid,
           const gchar            *channel,
           gfloat                  time,
           gfloat                 *x,
           gfloat                 *y)
{
	LrgTimelineView      *view = lrg_editor_timeline_panel_get_view (panel);
	LrgTimelineViewTrack *track = lrg_editor_timeline_panel_find_track (panel, guid, channel);
	gfloat                ty = 0.0f, th = 0.0f;

	g_assert_nonnull (track);
	g_assert_true (lrg_timeline_view_get_track_row (view, track, &ty, &th));
	*x = lrg_timeline_view_time_to_x (view, time);
	*y = ty + th * 0.5f;
}

static gint
key_index (LrgEditorApp *app,
           const gchar  *guid,
           const gchar  *channel,
           gfloat        time)
{
	LrgLevel    *level = lrg_editor_get_level (lrg_editor_app_get_editor (app));
	LrgTimeline *tl = lrg_level_get_timeline (level, 0);
	gint         t = lrg_timeline_find_track (tl, guid, channel);

	if (t < 0)
		return -1;
	return lrg_timeline_track_find_key (lrg_timeline_get_track (tl, (guint) t), time, 0.001f);
}

static void
test_timeline_edit (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorTimelinePanel *panel;
	LrgEditor              *editor = lrg_editor_app_get_editor (f->app);
	LrgEditorViewState     *view_state = lrg_editor_app_get_view_state (f->app);
	LrgTimelineView        *view;
	g_autofree gchar       *guid = NULL;
	g_autoptr(JsonNode)     seek = NULL;
	guint                   undo;
	gfloat                  x, y, x2, rx, ry, rw, rh;
	guint                   id;

	panel = LRG_EDITOR_TIMELINE_PANEL (present (f, "timeline"));
	view = lrg_editor_timeline_panel_get_view (panel);
	g_assert_true (lrg_widget_get_visible (find (f, "timeline-empty")));
	g_assert_cmpint (lrg_editor_timeline_panel_get_look (panel), ==, LRG_CANVAS_LOOK_AUTO);

	/* Key the selection with I at 0 s and 1 s: one undo step each */
	guid = create_node (f, "{'name':'Mover','primitive':'cube'}");
	select_guid (f, guid);
	undo = lrg_editor_get_undo_count (editor);
	efx_key (f, GRL_KEY_I, LRG_INPUT_MODIFIER_NONE);
	efx_frame (f);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	g_assert_cmpuint (lrg_level_get_n_timelines (lrg_editor_get_level (editor)), ==, 1);
	g_assert_false (lrg_widget_get_visible (find (f, "timeline-empty")));
	g_assert_cmpuint (lrg_editor_timeline_panel_find_keyframe (panel, guid, "location.x", 0.0f), !=,
	                  LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY);
	seek = efx_ipc_result (f, "{'id':1,'method':'timeline.seek','params':{'time':1.0}}");
	efx_frame (f);
	efx_key (f, GRL_KEY_I, LRG_INPUT_MODIFIER_NONE);
	efx_frame (f);
	g_assert_cmpint (key_index (f->app, guid, "location.x", 1.0f), >=, 0);

	/* Drag the 1 s key of location.x to 1.5 s: one undo step, snapped */
	undo = lrg_editor_get_undo_count (editor);
	key_point (panel, guid, "location.x", 1.0f, &x, &y);
	x2 = lrg_timeline_view_time_to_x (view, 1.503);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x2, y, 6);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	g_assert_cmpint (key_index (f->app, guid, "location.x", 1.0f), <, 0);
	g_assert_cmpint (key_index (f->app, guid, "location.x", 1.5f), >=, 0);
	lrg_editor_undo (editor);
	efx_frame (f);
	g_assert_cmpint (key_index (f->app, guid, "location.x", 1.0f), >=, 0);

	/* The node's summary key (group row) retimes every channel at once */
	undo = lrg_editor_get_undo_count (editor);
	key_point (panel, guid, NULL, 1.0f, &x, &y);
	x2 = lrg_timeline_view_time_to_x (view, 1.25);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x2, y, 6);
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	g_assert_cmpint (key_index (f->app, guid, "location.x", 1.25f), >=, 0);
	g_assert_cmpint (key_index (f->app, guid, "scale.z", 1.25f), >=, 0);
	g_assert_cmpint (key_index (f->app, guid, "rotation.y", 1.0f), <, 0);
	lrg_editor_undo (editor);
	efx_frame (f);
	g_assert_cmpint (key_index (f->app, guid, "rotation.y", 1.0f), >=, 0);

	/* Easing of the selected key */
	id = lrg_editor_timeline_panel_find_keyframe (panel, guid, "location.y", 1.0f);
	g_assert_cmpuint (id, !=, LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY);
	lrg_timeline_view_unselect_all (view);
	lrg_timeline_view_select_keyframe (view, id, FALSE);
	undo = lrg_editor_get_undo_count (editor);
	lrg_editor_timeline_panel_set_key_easing (panel, LRG_EASING_EASE_IN_OUT_CUBIC);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	{
		LrgTimeline   *tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
		LrgEasingType  easing = LRG_EASING_LINEAR;
		gint           t = lrg_timeline_find_track (tl, guid, "location.y");

		lrg_timeline_track_get_key (lrg_timeline_get_track (tl, (guint) t),
		                            (guint) key_index (f->app, guid, "location.y", 1.0f), NULL, NULL, &easing);
		g_assert_cmpint (easing, ==, LRG_EASING_EASE_IN_OUT_CUBIC);
	}

	/* Delete Keys removes the selection in one step */
	id = lrg_editor_timeline_panel_find_keyframe (panel, guid, "location.y", 1.0f);
	lrg_timeline_view_select_keyframe (view, id, FALSE);
	undo = lrg_editor_get_undo_count (editor);
	efx_click_widget (f, find (f, "timeline-delete-keys"));
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	g_assert_cmpint (key_index (f->app, guid, "location.y", 1.0f), <, 0);

	/* Scrub on the ruler previews at the pointer's time */
	efx_rect_of (LRG_WIDGET (view), &rx, &ry, &rw, &rh);
	x = lrg_timeline_view_time_to_x (view, 0.5);
	efx_click (f, x, ry + lrg_timeline_view_get_ruler_height (view) * 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_state_get_timeline_time (view_state), 0.5f, 0.02f);
	g_assert_true (lrg_editor_view_state_get_timeline_previewing (view_state));

	/* Play / pause from the transport and with K */
	efx_click_widget (f, find (f, "timeline-play"));
	g_assert_true (lrg_editor_view_state_get_timeline_playing (view_state));
	efx_frames (f, 6);
	g_assert_cmpfloat (lrg_editor_view_state_get_timeline_time (view_state), >, 0.55f);
	/* The view follows the playhead (one frame behind: the app ticks the
	 * playback after the canvas update) */
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (view),
	                                lrg_editor_view_state_get_timeline_time (view_state), 0.02);
	efx_key (f, GRL_KEY_K, LRG_INPUT_MODIFIER_NONE);
	g_assert_false (lrg_editor_view_state_get_timeline_playing (view_state));
	efx_click_widget (f, find (f, "timeline-stop"));
	g_assert_false (lrg_editor_view_state_get_timeline_previewing (view_state));

	/* Loop chip and the look property */
	efx_click_widget (f, find (f, "timeline-loop"));
	g_assert_true (lrg_timeline_get_loop (lrg_level_get_timeline (lrg_editor_get_level (editor), 0)));
	lrg_editor_timeline_panel_set_look (panel, LRG_CANVAS_LOOK_NEON);
	g_assert_cmpint (lrg_timeline_view_get_look (view), ==, LRG_CANVAS_LOOK_NEON);
}

/* ==========================================================================
 * Tiles
 * ========================================================================== */

static void
test_tiles_layers (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorTilesPanel *tiles;
	LrgEditor           *editor = lrg_editor_app_get_editor (f->app);
	LrgEditorViewState  *view_state = lrg_editor_app_get_view_state (f->app);
	g_autofree gchar    *guid = NULL;
	g_autofree gchar    *png = NULL;
	g_autoptr(JsonNode)  tool = NULL;
	LrgNodeTilemap      *grid;
	LrgNode             *node;
	guint                cols, rows, undo;
	gfloat               x, y, w, h;

	tiles = LRG_EDITOR_TILES_PANEL (present (f, "tiles"));
	g_assert_true (lrg_widget_get_visible (find (f, "tiles-empty")));

	guid = create_node (f, "{'name':'Ground','kind':'tilemap'}");
	select_guid (f, guid);
	efx_frame (f);
	node = lrg_editor_find_node (editor, guid);
	g_assert_true (lrg_editor_tiles_panel_get_target (tiles) == node);
	g_assert_false (lrg_widget_get_visible (find (f, "tiles-empty")));

	/* Create the grid, add a layer, rename and hide it; all undoable */
	efx_click_widget (f, find (f, "tiles-create-grid"));
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_nonnull (grid);
	g_assert_cmpuint (lrg_node_tilemap_get_n_layers (grid), ==, 1);
	reveal (f, "tiles-scroll", "tiles-add-layer");
	efx_click_widget (f, find (f, "tiles-add-layer"));
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_cmpuint (lrg_node_tilemap_get_n_layers (grid), ==, 2);
	efx_frame (f);
	g_assert_nonnull (efx_find (f, "tiles-layer-1"));

	undo = lrg_editor_get_undo_count (editor);
	reveal (f, "tiles-scroll", "tiles-layer-name");
	type_line (f, find (f, "tiles-layer-name"), "Walls");
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_cmpstr (lrg_node_tilemap_get_layer_name (grid, 1), ==, "Walls");
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);

	efx_frame (f);
	reveal (f, "tiles-scroll", "tiles-layer-eye-1");
	efx_click_widget (f, find (f, "tiles-layer-eye-1"));
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_false (lrg_node_tilemap_get_layer_visible (grid, 1));
	lrg_editor_undo (editor);
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_true (lrg_node_tilemap_get_layer_visible (grid, 1));

	/* Click the bottom layer to make it current, then remove it */
	efx_frame (f);
	reveal (f, "tiles-scroll", "tiles-layer-0");
	efx_click_widget (f, find (f, "tiles-layer-0"));
	reveal (f, "tiles-scroll", "tiles-remove-layer");
	efx_click_widget (f, find (f, "tiles-remove-layer"));
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	g_assert_cmpuint (lrg_node_tilemap_get_n_layers (grid), ==, 1);
	g_assert_cmpstr (lrg_node_tilemap_get_layer_name (grid, 0), ==, "Walls");
	lrg_editor_undo (editor);
	efx_frame (f);

	/* Tools sit at the top of the panel, above the layer rows */
	reveal (f, "tiles-scroll", "tiles-paint");
	efx_click_widget (f, find (f, "tiles-tool-fill"));
	g_assert_cmpint (lrg_editor_view_state_get_tile_tool (view_state), ==, LRG_EDITOR_TILE_TOOL_FILL);
	g_assert_true (lrg_editor_view_state_get_tile_mode (view_state));
	efx_click_widget (f, find (f, "tiles-paint"));
	g_assert_false (lrg_editor_view_state_get_tile_mode (view_state));
	tool = efx_ipc_result (f, "{'id':1,'method':'tilemap.tool','params':{'tool':'eraser'}}");
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_view_state_get_tile_tool (view_state), ==, LRG_EDITOR_TILE_TOOL_ERASER);

	/* No image: 8 x 4 swatches; picking one switches back to the brush */
	lrg_editor_tiles_panel_get_palette_size (tiles, &cols, &rows);
	g_assert_cmpuint (cols, ==, 8);
	g_assert_cmpuint (rows, ==, 4);
	reveal (f, "tiles-scroll", "tiles-palette");
	efx_rect_of (find (f, "tiles-palette"), &x, &y, &w, &h);
	efx_click (f, x + w / 8.0f * 2.5f, y + h / 4.0f * 0.5f);
	g_assert_cmpint (lrg_editor_view_state_get_tile_tool (view_state), ==, LRG_EDITOR_TILE_TOOL_BRUSH);

	/* Drop a 64 x 32 tileset image (application/x-lrg-asset) on the zone */
	png = g_build_filename (f->dir, "tiles.png", NULL);
	{
		Image img = GenImageColor (64, 32, (Color) { 200, 100, 50, 255 });

		g_assert_true (ExportImage (img, png));
		UnloadImage (img);
	}
	{
		LrgCanvas                *canvas = lrg_editor_app_get_canvas (f->app);
		g_autoptr(LrgDragPayload) payload = lrg_drag_payload_new_string (LRG_EDITOR_TILES_ASSET_TYPE, png,
		                                                                 LRG_DRAG_ACTION_COPY);

		reveal (f, "tiles-scroll", "tiles-tileset");
		efx_rect_of (find (f, "tiles-tileset"), &x, &y, &w, &h);
		g_assert_true (lrg_canvas_begin_drag (canvas, NULL, payload, x + 4.0f, y + 4.0f));
		efx_move (f, x + w * 0.5f, y + h * 0.5f);
		efx_move (f, x + w * 0.5f + 1.0f, y + h * 0.5f);
		undo = lrg_editor_get_undo_count (editor);
		efx_button (f, x + w * 0.5f, y + h * 0.5f, GRL_MOUSE_BUTTON_LEFT, TRUE);
		efx_button (f, x + w * 0.5f, y + h * 0.5f, GRL_MOUSE_BUTTON_LEFT, FALSE);
		g_assert_false (lrg_canvas_is_dragging (canvas));
	}
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (node)), ==, png);
	g_assert_cmpuint (lrg_editor_get_undo_count (editor), ==, undo + 1);
	lrg_editor_tiles_panel_get_palette_size (tiles, &cols, &rows);
	g_assert_cmpuint (cols, ==, 64 / 16);
	g_assert_cmpuint (rows, ==, 32 / 16);
}

/* ==========================================================================
 * Canvas input latch
 * ========================================================================== */

static void
test_latch_click (void)
{
	LrgCanvasInput in;
	guint          left = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	guint          right = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT);

	lrg_canvas_input_clear_latch ();

	/* Press and release between two polls: both edges, button up */
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_LEFT, TRUE);
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_LEFT, FALSE);
	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.buttons_pressed, ==, left);
	g_assert_cmpuint (in.buttons_released, ==, left);
	g_assert_cmpuint (in.buttons_down, ==, 0);

	/* The latch is empty afterwards */
	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.buttons_pressed | in.buttons_released, ==, 0);

	/* A press the poll saw (held): nothing added */
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_RIGHT, TRUE);
	lrg_canvas_input_init (&in);
	in.buttons_down = right;
	in.buttons_pressed = right;
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.buttons_pressed, ==, right);
	g_assert_cmpuint (in.buttons_released, ==, 0);

	/* A press the poll missed while still held: a press edge only */
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_RIGHT, TRUE);
	lrg_canvas_input_init (&in);
	in.buttons_down = right;
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.buttons_pressed, ==, right);
	g_assert_cmpuint (in.buttons_released, ==, 0);

	/* Out-of-range buttons are ignored */
	lrg_canvas_input_latch_button ((GrlMouseButton) 7, TRUE);
	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.buttons_pressed, ==, 0);
}

static void
test_latch_chord (void)
{
	LrgCanvasInput in;
	guint          i;
	gboolean       s_up = FALSE, ctrl_up = FALSE;

	lrg_canvas_input_clear_latch ();

	/* Ctrl down, S, S up, Ctrl up, all within one frame: the queue has
	 * the presses, the sampled modifiers are already empty */
	lrg_canvas_input_latch_key (GRL_KEY_LEFT_CONTROL, TRUE, LRG_INPUT_MODIFIER_NONE);
	lrg_canvas_input_latch_key (GRL_KEY_S, TRUE, LRG_INPUT_MODIFIER_CTRL);
	lrg_canvas_input_latch_key (GRL_KEY_S, FALSE, LRG_INPUT_MODIFIER_CTRL);
	lrg_canvas_input_latch_key (GRL_KEY_LEFT_CONTROL, FALSE, LRG_INPUT_MODIFIER_NONE);
	lrg_canvas_input_init (&in);
	lrg_canvas_input_add_key (&in, GRL_KEY_LEFT_CONTROL);
	lrg_canvas_input_add_key (&in, GRL_KEY_S);
	lrg_canvas_input_apply_latch (&in);
	g_assert_true (in.modifiers & LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (in.n_keys, ==, 2);
	for (i = 0; i < in.n_keys_released; i++)
	{
		s_up |= in.keys_released[i] == GRL_KEY_S;
		ctrl_up |= in.keys_released[i] == GRL_KEY_LEFT_CONTROL;
	}
	g_assert_true (s_up);
	g_assert_true (ctrl_up);

	/* A press the queue missed is appended; no keys, no modifiers */
	lrg_canvas_input_latch_key (GRL_KEY_A, TRUE, LRG_INPUT_MODIFIER_SHIFT);
	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.n_keys, ==, 1);
	g_assert_cmpint (in.keys[0], ==, GRL_KEY_A);
	g_assert_true (in.modifiers & LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpuint (in.n_keys_released, ==, 0);

	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.n_keys, ==, 0);
	g_assert_cmpint (in.modifiers, ==, LRG_INPUT_MODIFIER_NONE);

	/* A release alone (pressed in an earlier frame) is raylib's to report */
	lrg_canvas_input_latch_key (GRL_KEY_B, FALSE, LRG_INPUT_MODIFIER_NONE);
	lrg_canvas_input_init (&in);
	lrg_canvas_input_apply_latch (&in);
	g_assert_cmpuint (in.n_keys_released, ==, 0);

	/* The latch toggle */
	g_assert_true (lrg_canvas_input_get_latch_enabled ());
	lrg_canvas_input_set_latch_enabled (FALSE);
	g_assert_false (lrg_canvas_input_get_latch_enabled ());
	lrg_canvas_input_set_latch_enabled (TRUE);
	lrg_canvas_input_detach_latch ();
}

static void
on_clicked (LrgButton *button,
            gpointer   user_data)
{
	(*(guint *) user_data)++;
}

/* A click shorter than a frame reaches a button through the canvas. */
static void
test_latch_canvas (void)
{
	g_autoptr(LrgCanvas) canvas = g_object_ref_sink (lrg_canvas_new ());
	LrgButton           *button = lrg_button_new ("Go");
	LrgCanvasInput       in;
	guint                clicks = 0;
	gfloat               cx, cy;

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 300.0f);
	lrg_widget_set_position (LRG_WIDGET (button), 50.0f, 50.0f);
	lrg_widget_set_size (LRG_WIDGET (button), 100.0f, 40.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (button));
	g_object_unref (button);
	g_signal_connect (button, "clicked", G_CALLBACK (on_clicked), &clicks);
	cx = 100.0f;
	cy = 70.0f;

	lrg_canvas_input_init (&in);
	in.pointer_x = cx;
	in.pointer_y = cy;
	in.delta_time = 1.0f / 60.0f;
	lrg_canvas_process_input (canvas, &in);

	lrg_canvas_input_clear_latch ();
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_LEFT, TRUE);
	lrg_canvas_input_latch_button (GRL_MOUSE_BUTTON_LEFT, FALSE);
	lrg_canvas_input_init (&in);
	in.pointer_x = cx;
	in.pointer_y = cy;
	in.delta_time = 1.0f / 60.0f;
	lrg_canvas_input_apply_latch (&in);
	lrg_canvas_process_input (canvas, &in);
	lrg_canvas_input_init (&in);
	in.pointer_x = cx;
	in.pointer_y = cy;
	in.delta_time = 1.0f / 60.0f;
	lrg_canvas_process_input (canvas, &in);
	g_assert_cmpuint (clicks, ==, 1);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, fn) g_test_add (path, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("/editor-app/panels/console/filters", test_console_filters);
	ADD ("/editor-app/panels/console/commands", test_console_commands);
	ADD ("/editor-app/panels/history/jump", test_history_jump);
	ADD ("/editor-app/panels/problems/select", test_problems_select);
	ADD ("/editor-app/panels/profiler/sample", test_profiler_sample);
	ADD ("/editor-app/panels/timeline/edit", test_timeline_edit);
	ADD ("/editor-app/panels/tiles/layers", test_tiles_layers);
#undef ADD
	g_test_add_func ("/editor-app/panels/latch/click", test_latch_click);
	g_test_add_func ("/editor-app/panels/latch/chord", test_latch_chord);
	g_test_add_func ("/editor-app/panels/latch/canvas", test_latch_canvas);

	return g_test_run ();
}
