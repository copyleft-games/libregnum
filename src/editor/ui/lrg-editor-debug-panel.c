/* lrg-editor-debug-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDebugPanel: docked Debug panel. Call stack, one level of
 * variables, watches, breakpoints and an evaluate console. Registers
 * the debug.* actions and IPC methods. Does not dock itself.
 *
 * The panel drives lrg_script_debugger_get_default() directly (the
 * code panel's toolbar does the same) and owns the DAP server, which
 * listens only after debug.dap_start. Lines on the wire are 1-based.
 * The stop reason "entry" from the IPC draft is not produced: a pause
 * with no reason is reported as "pause".
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-app-private.h"
#include "lrg-editor-debug-panel.h"
#include "lrg-editor-code-panel.h"
#include "../../scripting/lrg-dap-server.h"
#include "../../scripting/lrg-script-debugger.h"
#include "../lrg-lsp-client.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-vbox.h"
#include <string.h>

#define PANEL_KEY   "lrg-editor-debug-panel"
#define CODE_KEY    "lrg-editor-code-panel"
#define DAP_PORT    (4711)
#define LOG_MAX     (8000)
#define VAR_CAP     (40)

struct _LrgEditorDebugPanel
{
	LrgContainer        parent_instance;

	LrgEditorApp       *app;       /* weak */
	LrgScriptDebugger  *debugger;  /* borrowed singleton */
	LrgDapServer       *dap;       /* owned */
	gulong              output_id;

	LrgHBox            *toolbar;
	LrgScrollBox       *scroll;
	LrgLabel           *stack_label;
	LrgLabel           *vars_label;
	LrgLabel           *watch_label;
	LrgLabel           *bp_label;
	LrgLabel           *console;
	LrgTextInput       *input;

	GPtrArray          *watches;   /* gchar* */
	GString            *log;
	gint64              poll_at;
};

G_DEFINE_FINAL_TYPE (LrgEditorDebugPanel, lrg_editor_debug_panel, LRG_TYPE_CONTAINER)

static void refresh (LrgEditorDebugPanel *self, gboolean force);
static JsonNode * state_node (LrgEditorDebugPanel *self);
static JsonNode * watches_node (LrgEditorDebugPanel *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgEditorDebugPanel *
panel_of (gpointer user_data)
{
	if (!LRG_IS_EDITOR_APP (user_data))
		return NULL;
	return g_object_get_data (G_OBJECT (user_data), PANEL_KEY);
}

static JsonNode *
node_object (JsonObject *object)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, object);
	return n;
}

static JsonNode *
node_array (JsonArray *array)
{
	JsonNode *n = json_node_new (JSON_NODE_ARRAY);

	json_node_take_array (n, array);
	return n;
}

static JsonNode *
node_empty (void)
{
	return node_object (json_object_new ());
}

static const gchar *
param_string (JsonObject  *params,
              const gchar *name)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return NULL;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gboolean
param_int (JsonObject  *params,
           const gchar *name,
           gint64      *out)
{
	JsonNode *n;
	GType     type;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return FALSE;
	type = json_node_get_value_type (n);
	if (type == G_TYPE_INT64)
	{
		*out = json_node_get_int (n);
		return TRUE;
	}
	if (type == G_TYPE_DOUBLE)
	{
		*out = (gint64) json_node_get_double (n);
		return TRUE;
	}
	return FALSE;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *name,
            gboolean    *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, name))
		return FALSE;
	n = json_object_get_member (params, name);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return FALSE;
	*out = json_node_get_boolean (n);
	return TRUE;
}

/* Absolute path. NULL / "" stays NULL (the caller picks the active tab). */
static gchar *
resolve_path (LrgEditorApp *app,
              const gchar  *path)
{
	if (path == NULL || path[0] == '\0')
		return NULL;
	if (app != NULL)
		return editor_app_project_path (app, path);
	return g_canonicalize_filename (path, NULL);
}

static const gchar *
active_path (LrgEditorApp *app)
{
	LrgEditorCodePanel *code;

	if (app == NULL)
		return NULL;
	code = g_object_get_data (G_OBJECT (app), CODE_KEY);
	if (!LRG_IS_EDITOR_CODE_PANEL (code))
		return NULL;
	return lrg_editor_code_panel_get_active_path (code);
}

/* 1-based line: the parameter, else the code caret, else 0 (missing). */
static gint
param_line (LrgEditorDebugPanel *self,
            JsonObject          *params,
            gboolean            *have)
{
	gint64 line = 0;

	*have = param_int (params, "line", &line);
	if (*have)
		return (gint) line;
	if (self->app != NULL)
	{
		LrgEditorCodePanel *code = g_object_get_data (G_OBJECT (self->app), CODE_KEY);
		LrgTextEditor      *editor;
		guint               caret = 0;

		if (LRG_IS_EDITOR_CODE_PANEL (code))
		{
			editor = lrg_editor_code_panel_get_editor (code, NULL);
			if (editor != NULL)
			{
				lrg_text_editor_get_cursor (editor, &caret, NULL);
				*have = TRUE;
				return (gint) caret + 1;
			}
		}
	}
	return 0;
}

static gboolean
same_file (const gchar *a,
           const gchar *b)
{
	g_autofree gchar *na = lrg_script_debugger_normalize_path (a);
	g_autofree gchar *nb = lrg_script_debugger_normalize_path (b);

	return g_strcmp0 (na, nb) == 0;
}

static const gchar *
reason_name (LrgScriptDebugger *debugger)
{
	LrgScriptStopReason reason;

	if (debugger == NULL || !lrg_script_debugger_is_paused (debugger))
		return NULL;
	reason = lrg_script_debugger_get_stop_reason (debugger);
	if (reason == LRG_SCRIPT_STOP_REASON_NONE)
		return "pause";
	return lrg_script_stop_reason_to_string (reason);
}

static const gchar *
state_name (LrgScriptDebugger *debugger)
{
	g_autoptr(GPtrArray) attached = NULL;

	if (debugger == NULL)
		return "idle";
	if (lrg_script_debugger_is_paused (debugger))
		return "paused";
	attached = lrg_script_debugger_get_attached (debugger);
	if ((attached != NULL && attached->len > 0) || lrg_script_debugger_get_auto_attach (debugger))
		return "running";
	return "idle";
}

static void
set_label (LrgLabel    *label,
           const gchar *text)
{
	if (g_strcmp0 (lrg_label_get_text (label), text) == 0)
		return;
	lrg_label_set_text (label, text);
}

static void
console_append (LrgEditorDebugPanel *self,
                const gchar         *text)
{
	if (self->log == NULL || text == NULL)
		return;
	g_string_append (self->log, text);
	if (text[0] != '\0' && !g_str_has_suffix (text, "\n"))
		g_string_append_c (self->log, '\n');
	if (self->log->len > LOG_MAX)
		g_string_erase (self->log, 0, self->log->len - (LOG_MAX - 2000));
	set_label (self->console, self->log->len > 0 ? self->log->str : "(console)");
}

static void
on_debugger_output (LrgScriptDebugger *debugger,
                    const gchar       *category,
                    const gchar       *text,
                    gpointer           user_data)
{
	LrgEditorDebugPanel *self = user_data;
	g_autofree gchar    *line = NULL;

	(void) debugger;
	if (category != NULL && category[0] != '\0')
		line = g_strdup_printf ("[%s] %s", category, text != NULL ? text : "");
	else
		line = g_strdup (text != NULL ? text : "");
	console_append (self, line);
}

static void
hook_output (LrgEditorDebugPanel *self)
{
	guint        id;
	GSignalQuery query;

	if (self->debugger == NULL || self->output_id != 0)
		return;
	id = g_signal_lookup ("output", LRG_TYPE_SCRIPT_DEBUGGER);
	if (id == 0)
		return;
	g_signal_query (id, &query);
	if (query.n_params != 2 || query.param_types[0] != G_TYPE_STRING ||
	    query.param_types[1] != G_TYPE_STRING)
		return;
	self->output_id = g_signal_connect (self->debugger, "output",
	                                    G_CALLBACK (on_debugger_output), self);
}

/* ==========================================================================
 * Breakpoints and state
 * ========================================================================== */

static JsonObject *
bp_object (LrgScriptBreakpoint *bp)
{
	JsonObject  *o = json_object_new ();
	const gchar *s;

	json_object_set_int_member (o, "id", (gint64) lrg_script_breakpoint_get_id (bp));
	json_object_set_string_member (o, "path", lrg_script_breakpoint_get_file (bp));
	json_object_set_int_member (o, "line", lrg_script_breakpoint_get_line (bp));
	s = lrg_script_breakpoint_get_condition (bp);
	if (s != NULL)
		json_object_set_string_member (o, "condition", s);
	else
		json_object_set_null_member (o, "condition");
	s = lrg_script_breakpoint_get_hit_condition (bp);
	if (s != NULL)
		json_object_set_string_member (o, "hit_condition", s);
	else
		json_object_set_null_member (o, "hit_condition");
	s = lrg_script_breakpoint_get_log_message (bp);
	if (s != NULL)
		json_object_set_string_member (o, "log_message", s);
	else
		json_object_set_null_member (o, "log_message");
	json_object_set_boolean_member (o, "enabled", lrg_script_breakpoint_get_enabled (bp));
	json_object_set_boolean_member (o, "verified", lrg_script_breakpoint_get_verified (bp));
	json_object_set_int_member (o, "hits", (gint64) lrg_script_breakpoint_get_hit_count (bp));
	return o;
}

static LrgScriptBreakpoint *
find_bp (LrgScriptDebugger *debugger,
         const gchar       *path,
         gint               line)
{
	g_autoptr(GPtrArray) all = NULL;
	guint                i;

	if (debugger == NULL || path == NULL)
		return NULL;
	all = lrg_script_debugger_get_breakpoints (debugger);
	if (all == NULL)
		return NULL;
	for (i = 0; i < all->len; i++)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (all, i);

		if (lrg_script_breakpoint_get_line (bp) == line &&
		    same_file (lrg_script_breakpoint_get_file (bp), path))
			return g_object_ref (bp);
	}
	return NULL;
}

static void
apply_bp_fields (LrgScriptBreakpoint *bp,
                 JsonObject          *params)
{
	const gchar *s;
	gboolean     enabled = TRUE;

	s = param_string (params, "condition");
	if (params != NULL && json_object_has_member (params, "condition"))
		lrg_script_breakpoint_set_condition (bp, s);
	s = param_string (params, "hit_condition");
	if (params != NULL && json_object_has_member (params, "hit_condition"))
		lrg_script_breakpoint_set_hit_condition (bp, s);
	s = param_string (params, "log_message");
	if (params != NULL && json_object_has_member (params, "log_message"))
		lrg_script_breakpoint_set_log_message (bp, s);
	if (param_bool (params, "enabled", &enabled))
		lrg_script_breakpoint_set_enabled (bp, enabled);
}

static JsonNode *
state_node (LrgEditorDebugPanel *self)
{
	JsonObject        *o = json_object_new ();
	JsonObject        *dap = json_object_new ();
	LrgScriptDebugger *debugger = self->debugger;
	const gchar       *reason = reason_name (debugger);
	g_autofree gchar  *path = NULL;
	gint               line = 0;

	json_object_set_string_member (o, "state", state_name (debugger));
	if (reason != NULL)
		json_object_set_string_member (o, "reason", reason);
	else
		json_object_set_null_member (o, "reason");

	if (debugger != NULL && lrg_script_debugger_is_paused (debugger))
	{
		g_autoptr(GPtrArray) frames = NULL;
		g_autoptr(GError)    error = NULL;
		g_autofree gchar    *message = lrg_script_debugger_dup_stop_description (debugger);

		frames = lrg_script_debugger_get_stack_frames (debugger, &error);
		if (frames != NULL && frames->len > 0)
		{
			LrgScriptStackFrame *frame = g_ptr_array_index (frames, 0);

			path = g_strdup (lrg_script_stack_frame_get_file (frame));
			line = lrg_script_stack_frame_get_line (frame);
		}
		if (message != NULL)
			json_object_set_string_member (o, "message", message);
		else
			json_object_set_null_member (o, "message");
		if (path != NULL)
			json_object_set_string_member (o, "path", path);
		else
			json_object_set_null_member (o, "path");
	}
	else
	{
		json_object_set_null_member (o, "message");
		json_object_set_null_member (o, "path");
	}
	if (line > 0)
		json_object_set_int_member (o, "line", line);
	else
		json_object_set_null_member (o, "line");
	if (path != NULL && lrg_lsp_language_for_path (path) != NULL)
		json_object_set_string_member (o, "language", lrg_lsp_language_for_path (path));
	else
		json_object_set_null_member (o, "language");

	json_object_set_boolean_member (dap, "listening",
	                                self->dap != NULL && lrg_dap_server_get_listening (self->dap));
	if (self->dap != NULL && lrg_dap_server_get_listening (self->dap))
		json_object_set_int_member (dap, "port", (gint64) lrg_dap_server_get_port (self->dap));
	else
		json_object_set_null_member (dap, "port");
	json_object_set_int_member (dap, "clients",
	                            self->dap != NULL ? (gint64) lrg_dap_server_get_n_clients (self->dap) : 0);
	json_object_set_object_member (o, "dap", dap);
	return node_object (o);
}

static gboolean
require_panel (gpointer   user_data,
               LrgEditorDebugPanel **out,
               GError   **error)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL || self->debugger == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The debug panel is gone");
		return FALSE;
	}
	*out = self;
	return TRUE;
}

static gboolean
require_paused (LrgScriptDebugger *debugger,
                GError           **error)
{
	if (debugger == NULL || !lrg_script_debugger_is_paused (debugger))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "not paused");
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static JsonNode *
m_state (LrgEditorServer *server,
         JsonObject      *params,
         gpointer         user_data,
         GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	return state_node (self);
}

static JsonNode *
m_show (LrgEditorServer *server,
        JsonObject      *params,
        gpointer         user_data,
        GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	if (self->app != NULL)
		editor_app_show_panel (self->app, "debug");
	return node_empty ();
}

static JsonNode *
m_breakpoints (LrgEditorServer *server,
               JsonObject      *params,
               gpointer         user_data,
               GError         **error)
{
	LrgEditorDebugPanel  *self;
	g_autoptr(GPtrArray)  all = NULL;
	g_autofree gchar     *path = NULL;
	JsonArray            *a;
	guint                 i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	path = resolve_path (self->app, param_string (params, "path"));
	all = lrg_script_debugger_get_breakpoints (self->debugger);
	a = json_array_new ();
	if (all != NULL)
	{
		for (i = 0; i < all->len; i++)
		{
			LrgScriptBreakpoint *bp = g_ptr_array_index (all, i);

			if (path != NULL && !same_file (lrg_script_breakpoint_get_file (bp), path))
				continue;
			json_array_add_object_element (a, bp_object (bp));
		}
	}
	return node_array (a);
}

static JsonNode *
m_set_breakpoint (LrgEditorServer *server,
                  JsonObject      *params,
                  gpointer         user_data,
                  GError         **error)
{
	LrgEditorDebugPanel *self;
	g_autofree gchar    *path = NULL;
	gint64               line = 0;
	LrgScriptBreakpoint *bp;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	path = resolve_path (self->app, param_string (params, "path"));
	if (path == NULL || !param_int (params, "line", &line) || line < 1)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "path and line (1-based) are required");
		return NULL;
	}
	bp = find_bp (self->debugger, path, (gint) line);
	if (bp == NULL)
		bp = g_object_ref (lrg_script_debugger_add_breakpoint (self->debugger, path, (gint) line));
	apply_bp_fields (bp, params);
	{
		JsonNode *n = node_object (bp_object (bp));

		g_object_unref (bp);
		self->poll_at = 0;
		return n;
	}
}

static JsonNode *
m_remove_breakpoint (LrgEditorServer *server,
                     JsonObject      *params,
                     gpointer         user_data,
                     GError         **error)
{
	LrgEditorDebugPanel *self;
	g_autofree gchar    *path = NULL;
	gint64               line = 0;
	g_autoptr(LrgScriptBreakpoint) bp = NULL;
	JsonObject          *o;
	gboolean             removed = FALSE;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	path = resolve_path (self->app, param_string (params, "path"));
	if (path == NULL)
		path = g_strdup (active_path (self->app));
	if (path == NULL || !param_int (params, "line", &line) || line < 1)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "path and line (1-based) are required");
		return NULL;
	}
	bp = find_bp (self->debugger, path, (gint) line);
	if (bp != NULL)
		removed = lrg_script_debugger_remove_breakpoint (self->debugger, bp);
	o = json_object_new ();
	json_object_set_boolean_member (o, "removed", removed);
	self->poll_at = 0;
	return node_object (o);
}

static JsonNode *
m_toggle_breakpoint (LrgEditorServer *server,
                     JsonObject      *params,
                     gpointer         user_data,
                     GError         **error)
{
	LrgEditorDebugPanel *self;
	g_autofree gchar    *path = NULL;
	gboolean             have = FALSE;
	gint                 line;
	g_autoptr(LrgScriptBreakpoint) bp = NULL;
	JsonObject          *o;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	path = resolve_path (self->app, param_string (params, "path"));
	if (path == NULL)
		path = g_strdup (active_path (self->app));
	line = param_line (self, params, &have);
	if (path == NULL || !have || line < 1)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "path and line (1-based) are required");
		return NULL;
	}
	bp = find_bp (self->debugger, path, line);
	o = json_object_new ();
	if (bp != NULL)
	{
		lrg_script_debugger_remove_breakpoint (self->debugger, bp);
		json_object_set_boolean_member (o, "set", FALSE);
		json_object_set_null_member (o, "breakpoint");
	}
	else
	{
		LrgScriptBreakpoint *added = lrg_script_debugger_add_breakpoint (self->debugger, path, line);

		json_object_set_boolean_member (o, "set", TRUE);
		json_object_set_object_member (o, "breakpoint", bp_object (added));
	}
	self->poll_at = 0;
	return node_object (o);
}

static JsonNode *
m_clear_breakpoints (LrgEditorServer *server,
                     JsonObject      *params,
                     gpointer         user_data,
                     GError         **error)
{
	LrgEditorDebugPanel  *self;
	g_autofree gchar     *path = NULL;
	g_autoptr(GPtrArray)  all = NULL;
	JsonObject           *o;
	gint                  removed = 0;
	guint                 i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	path = resolve_path (self->app, param_string (params, "path"));
	all = lrg_script_debugger_get_breakpoints (self->debugger);
	if (all != NULL)
	{
		for (i = 0; i < all->len; i++)
		{
			LrgScriptBreakpoint *bp = g_ptr_array_index (all, i);

			if (path == NULL || same_file (lrg_script_breakpoint_get_file (bp), path))
				removed++;
		}
	}
	lrg_script_debugger_clear_breakpoints (self->debugger, path);
	o = json_object_new ();
	json_object_set_int_member (o, "removed", removed);
	self->poll_at = 0;
	return node_object (o);
}

static JsonNode *
m_continue (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	lrg_script_debugger_resume (self->debugger);
	self->poll_at = 0;
	return state_node (self);
}

static JsonNode *
m_pause (LrgEditorServer *server,
         JsonObject      *params,
         gpointer         user_data,
         GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	lrg_script_debugger_pause (self->debugger);
	self->poll_at = 0;
	return state_node (self);
}

static JsonNode *
step_call (gpointer user_data,
           void   (*step) (LrgScriptDebugger *debugger),
           GError **error)
{
	LrgEditorDebugPanel *self;

	if (!require_panel (user_data, &self, error))
		return NULL;
	if (!require_paused (self->debugger, error))
		return NULL;
	step (self->debugger);
	self->poll_at = 0;
	return state_node (self);
}

static JsonNode *
m_step_over (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	(void) server;
	(void) params;
	return step_call (user_data, lrg_script_debugger_step_over, error);
}

static JsonNode *
m_step_in (LrgEditorServer *server,
           JsonObject      *params,
           gpointer         user_data,
           GError         **error)
{
	(void) server;
	(void) params;
	return step_call (user_data, lrg_script_debugger_step_in, error);
}

static JsonNode *
m_step_out (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	(void) server;
	(void) params;
	return step_call (user_data, lrg_script_debugger_step_out, error);
}

static JsonNode *
m_stop (LrgEditorServer *server,
        JsonObject      *params,
        gpointer         user_data,
        GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	lrg_script_debugger_detach_all (self->debugger);
	self->poll_at = 0;
	return state_node (self);
}

static JsonNode *
m_stack (LrgEditorServer *server,
         JsonObject      *params,
         gpointer         user_data,
         GError         **error)
{
	LrgEditorDebugPanel  *self;
	g_autoptr(GPtrArray)  frames = NULL;
	g_autoptr(GError)     dbg_error = NULL;
	JsonArray            *a;
	guint                 i;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	if (!require_paused (self->debugger, error))
		return NULL;
	frames = lrg_script_debugger_get_stack_frames (self->debugger, &dbg_error);
	if (frames == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     dbg_error != NULL ? dbg_error->message : "no stack");
		return NULL;
	}
	a = json_array_new ();
	for (i = 0; i < frames->len; i++)
	{
		LrgScriptStackFrame *frame = g_ptr_array_index (frames, i);
		JsonObject          *o = json_object_new ();
		const gchar         *file = lrg_script_stack_frame_get_file (frame);
		const gchar         *lang = lrg_lsp_language_for_path (file);

		json_object_set_int_member (o, "id", (gint64) lrg_script_stack_frame_get_id (frame));
		json_object_set_string_member (o, "name", lrg_script_stack_frame_get_name (frame));
		if (file != NULL)
			json_object_set_string_member (o, "path", file);
		else
			json_object_set_null_member (o, "path");
		json_object_set_int_member (o, "line", lrg_script_stack_frame_get_line (frame));
		json_object_set_int_member (o, "column", 1);
		if (lang != NULL)
			json_object_set_string_member (o, "language", lang);
		else
			json_object_set_null_member (o, "language");
		json_array_add_object_element (a, o);
	}
	return node_array (a);
}

static JsonNode *
m_scopes (LrgEditorServer *server,
          JsonObject      *params,
          gpointer         user_data,
          GError         **error)
{
	LrgEditorDebugPanel  *self;
	g_autoptr(GPtrArray)  scopes = NULL;
	g_autoptr(GError)     dbg_error = NULL;
	gint64                frame = 0;
	JsonArray            *a;
	guint                 i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	if (!require_paused (self->debugger, error))
		return NULL;
	if (param_int (params, "frame", &frame) && frame < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "frame must be >= 0");
		return NULL;
	}
	scopes = lrg_script_debugger_get_scopes (self->debugger, (guint) frame, &dbg_error);
	if (scopes == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     dbg_error != NULL ? dbg_error->message : "no scopes");
		return NULL;
	}
	a = json_array_new ();
	for (i = 0; i < scopes->len; i++)
	{
		LrgScriptScope *scope = g_ptr_array_index (scopes, i);
		JsonObject     *o = json_object_new ();

		json_object_set_string_member (o, "name", lrg_script_scope_get_name (scope));
		json_object_set_int_member (o, "reference",
		                            (gint64) lrg_script_scope_get_variables_reference (scope));
		json_object_set_boolean_member (o, "expensive", lrg_script_scope_get_expensive (scope));
		json_array_add_object_element (a, o);
	}
	return node_array (a);
}

static JsonNode *
m_variables (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	LrgEditorDebugPanel  *self;
	g_autoptr(GPtrArray)  vars = NULL;
	g_autoptr(GError)     dbg_error = NULL;
	gint64                reference = 0;
	JsonArray            *a;
	guint                 i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	if (!require_paused (self->debugger, error))
		return NULL;
	if (!param_int (params, "reference", &reference) || reference < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "reference is required");
		return NULL;
	}
	vars = lrg_script_debugger_get_variables (self->debugger, (guint) reference, &dbg_error);
	if (vars == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                     dbg_error != NULL ? dbg_error->message : "no such reference");
		return NULL;
	}
	a = json_array_new ();
	for (i = 0; i < vars->len; i++)
	{
		LrgScriptVariable *var = g_ptr_array_index (vars, i);
		JsonObject        *o = json_object_new ();

		json_object_set_string_member (o, "name", lrg_script_variable_get_name (var));
		json_object_set_string_member (o, "value", lrg_script_variable_get_value (var));
		json_object_set_string_member (o, "type", lrg_script_variable_get_type_name (var));
		json_object_set_int_member (o, "reference",
		                            (gint64) lrg_script_variable_get_variables_reference (var));
		json_array_add_object_element (a, o);
	}
	return node_array (a);
}

static JsonNode *
m_evaluate (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	LrgEditorDebugPanel *self;
	const gchar         *expression;
	gint64               frame = 0;
	g_autoptr(LrgScriptVariable) var = NULL;
	g_autoptr(GError)    dbg_error = NULL;
	JsonObject          *o;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	expression = param_string (params, "expression");
	if (expression == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "expression is required");
		return NULL;
	}
	if (!require_paused (self->debugger, error))
		return NULL;
	if (param_int (params, "frame", &frame) && frame < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "frame must be >= 0");
		return NULL;
	}
	var = lrg_script_debugger_evaluate (self->debugger, (guint) frame, expression, &dbg_error);
	if (var == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     dbg_error != NULL ? dbg_error->message : "evaluate failed");
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "result", lrg_script_variable_get_value (var));
	json_object_set_string_member (o, "type", lrg_script_variable_get_type_name (var));
	json_object_set_int_member (o, "reference",
	                            (gint64) lrg_script_variable_get_variables_reference (var));
	return node_object (o);
}

static JsonNode *
watches_node (LrgEditorDebugPanel *self)
{
	JsonArray *a = json_array_new ();
	gboolean   paused = self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger);
	guint      i;

	for (i = 0; i < self->watches->len; i++)
	{
		const gchar *expression = g_ptr_array_index (self->watches, i);
		JsonObject  *o = json_object_new ();

		json_object_set_string_member (o, "expression", expression);
		if (!paused)
		{
			json_object_set_null_member (o, "value");
			json_object_set_null_member (o, "type");
			json_object_set_string_member (o, "error", "not paused");
		}
		else
		{
			g_autoptr(GError) error = NULL;
			g_autoptr(LrgScriptVariable) var = NULL;

			var = lrg_script_debugger_evaluate (self->debugger, 0, expression, &error);
			if (var == NULL)
			{
				json_object_set_null_member (o, "value");
				json_object_set_null_member (o, "type");
				json_object_set_string_member (o, "error",
				                               error != NULL ? error->message : "evaluate failed");
			}
			else
			{
				json_object_set_string_member (o, "value", lrg_script_variable_get_value (var));
				json_object_set_string_member (o, "type", lrg_script_variable_get_type_name (var));
				json_object_set_null_member (o, "error");
			}
		}
		json_array_add_object_element (a, o);
	}
	return node_array (a);
}

static JsonNode *
m_watches (LrgEditorServer *server,
           JsonObject      *params,
           gpointer         user_data,
           GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	return watches_node (self);
}

static JsonNode *
m_add_watch (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	LrgEditorDebugPanel *self;
	const gchar         *expression;
	guint                i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	expression = param_string (params, "expression");
	if (expression == NULL || expression[0] == '\0')
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "expression is required");
		return NULL;
	}
	for (i = 0; i < self->watches->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (self->watches, i), expression) == 0)
			return watches_node (self);
	}
	g_ptr_array_add (self->watches, g_strdup (expression));
	self->poll_at = 0;
	return watches_node (self);
}

static JsonNode *
m_remove_watch (LrgEditorServer *server,
                JsonObject      *params,
                gpointer         user_data,
                GError         **error)
{
	LrgEditorDebugPanel *self;
	const gchar         *expression;
	guint                i;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	expression = param_string (params, "expression");
	if (expression == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "expression is required");
		return NULL;
	}
	for (i = 0; i < self->watches->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (self->watches, i), expression) == 0)
		{
			g_ptr_array_remove_index (self->watches, i);
			break;
		}
	}
	self->poll_at = 0;
	return watches_node (self);
}

static JsonNode *
m_dap_start (LrgEditorServer *server,
             JsonObject      *params,
             gpointer         user_data,
             GError         **error)
{
	LrgEditorDebugPanel *self;
	gint64               port = DAP_PORT;
	const gchar         *host;
	JsonObject          *o;
	g_autoptr(GError)    start_error = NULL;

	(void) server;
	if (!require_panel (user_data, &self, error))
		return NULL;
	if (param_int (params, "port", &port) && (port < 0 || port > 65535))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "port is out of range");
		return NULL;
	}
	host = param_string (params, "host");
	if (host == NULL || host[0] == '\0')
		host = "127.0.0.1";
	if (!lrg_dap_server_get_listening (self->dap))
	{
		if (!lrg_dap_server_start (self->dap, host, (guint) port, &start_error))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
			                     start_error != NULL ? start_error->message : "DAP listen failed");
			return NULL;
		}
	}
	o = json_object_new ();
	json_object_set_int_member (o, "port", (gint64) lrg_dap_server_get_port (self->dap));
	return node_object (o);
}

static JsonNode *
m_dap_stop (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	LrgEditorDebugPanel *self;

	(void) server;
	(void) params;
	if (!require_panel (user_data, &self, error))
		return NULL;
	lrg_dap_server_stop (self->dap);
	return node_empty ();
}

/* ==========================================================================
 * Actions and the view
 * ========================================================================== */

static void
a_continue (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL)
		return;
	lrg_script_debugger_resume (self->debugger);
	self->poll_at = 0;
}

static void
a_pause (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL)
		return;
	lrg_script_debugger_pause (self->debugger);
	self->poll_at = 0;
}

static void
a_over (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL || !lrg_script_debugger_is_paused (self->debugger))
		return;
	lrg_script_debugger_step_over (self->debugger);
	self->poll_at = 0;
}

static void
a_in (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL || !lrg_script_debugger_is_paused (self->debugger))
		return;
	lrg_script_debugger_step_in (self->debugger);
	self->poll_at = 0;
}

static void
a_out (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL || !lrg_script_debugger_is_paused (self->debugger))
		return;
	lrg_script_debugger_step_out (self->debugger);
	self->poll_at = 0;
}

static void
a_stop (gpointer user_data)
{
	LrgEditorDebugPanel *self = panel_of (user_data);

	if (self == NULL)
		return;
	lrg_script_debugger_detach_all (self->debugger);
	self->poll_at = 0;
}

static void
a_show (gpointer user_data)
{
	LrgEditorApp *app = user_data;

	if (LRG_IS_EDITOR_APP (app))
		editor_app_show_panel (app, "debug");
}

static void
on_toolbar (LrgButton *button,
            gpointer   user_data)
{
	LrgEditorDebugPanel *self = user_data;
	const gchar         *name = lrg_widget_get_name (LRG_WIDGET (button));

	if (g_strcmp0 (name, "debug-continue") == 0)
		a_continue (self->app);
	else if (g_strcmp0 (name, "debug-pause") == 0)
		a_pause (self->app);
	else if (g_strcmp0 (name, "debug-step-over") == 0)
		a_over (self->app);
	else if (g_strcmp0 (name, "debug-step-in") == 0)
		a_in (self->app);
	else if (g_strcmp0 (name, "debug-step-out") == 0)
		a_out (self->app);
	else if (g_strcmp0 (name, "debug-stop") == 0)
		a_stop (self->app);
}

static void
add_watch_text (LrgEditorDebugPanel *self,
                const gchar         *expression)
{
	guint i;

	for (i = 0; i < self->watches->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (self->watches, i), expression) == 0)
			return;
	}
	g_ptr_array_add (self->watches, g_strdup (expression));
}

static void
on_submit (LrgTextInput *input,
           gpointer      user_data)
{
	LrgEditorDebugPanel *self = user_data;
	const gchar         *text = lrg_text_input_get_text (input);
	g_autofree gchar    *trimmed = NULL;

	if (text == NULL)
		return;
	trimmed = g_strdup (text);
	g_strstrip (trimmed);
	if (trimmed[0] == '\0')
		return;
	if (g_str_has_prefix (trimmed, "/watch "))
	{
		add_watch_text (self, g_strstrip (trimmed + 7));
		console_append (self, trimmed);
	}
	else if (g_str_has_prefix (trimmed, "/unwatch "))
	{
		const gchar *expression = g_strstrip (trimmed + 9);
		guint        i;

		for (i = 0; i < self->watches->len; i++)
		{
			if (g_strcmp0 (g_ptr_array_index (self->watches, i), expression) == 0)
			{
				g_ptr_array_remove_index (self->watches, i);
				break;
			}
		}
		console_append (self, trimmed);
	}
	else if (self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger))
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LrgScriptVariable) var = NULL;
		g_autofree gchar *line = NULL;

		var = lrg_script_debugger_evaluate (self->debugger, 0, trimmed, &error);
		if (var == NULL)
			line = g_strdup_printf ("> %s\n%s", trimmed,
			                        error != NULL ? error->message : "evaluate failed");
		else
			line = g_strdup_printf ("> %s\n%s", trimmed, lrg_script_variable_get_value (var));
		console_append (self, line);
	}
	else
		console_append (self, "not paused");
	lrg_text_input_set_text (input, "");
	self->poll_at = 0;
}

static LrgButton *
toolbar_button (LrgEditorDebugPanel *self,
                const gchar         *label,
                const gchar         *name)
{
	LrgButton *button = lrg_button_new (label);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	lrg_widget_add_class (LRG_WIDGET (button), "flat");
	g_signal_connect_object (button, "clicked", G_CALLBACK (on_toolbar), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (button));
	g_object_unref (button);
	return button;
}

static LrgLabel *
section_label (const gchar *name,
               const gchar *text)
{
	LrgLabel *label = lrg_label_new (text);

	lrg_widget_set_name (LRG_WIDGET (label), name);
	lrg_widget_set_margin_all (LRG_WIDGET (label), 4.0f);
	return label;
}

static void
refresh (LrgEditorDebugPanel *self,
         gboolean             force)
{
	gint64            now = g_get_monotonic_time ();
	GString          *stack;
	GString          *vars;
	GString          *watches;
	GString          *bps;
	gboolean          paused;
	guint             i;

	if (!force && now - self->poll_at < 100000)
		return;
	self->poll_at = now;
	paused = self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger);

	stack = g_string_new ("Call stack\n");
	vars = g_string_new ("Variables\n");
	if (!paused)
	{
		g_string_append (stack, "(not paused)\n");
		g_string_append (vars, "(not paused)\n");
	}
	else
	{
		g_autoptr(GPtrArray) frames = NULL;
		g_autoptr(GError)    error = NULL;

		frames = lrg_script_debugger_get_stack_frames (self->debugger, &error);
		if (frames == NULL || frames->len == 0)
			g_string_append (stack, error != NULL ? error->message : "(empty)\n");
		else
		{
			for (i = 0; i < frames->len; i++)
			{
				LrgScriptStackFrame *frame = g_ptr_array_index (frames, i);

				g_string_append_printf (stack, "%s  %s:%d\n",
				                        lrg_script_stack_frame_get_name (frame),
				                        lrg_script_stack_frame_get_file (frame) != NULL
				                            ? lrg_script_stack_frame_get_file (frame) : "?",
				                        lrg_script_stack_frame_get_line (frame));
			}
			{
				g_autoptr(GPtrArray) scopes = NULL;
				g_autoptr(GError)    scope_error = NULL;
				guint                id = lrg_script_stack_frame_get_id (g_ptr_array_index (frames, 0));

				scopes = lrg_script_debugger_get_scopes (self->debugger, id, &scope_error);
				if (scopes == NULL)
					g_string_append (vars, scope_error != NULL ? scope_error->message : "(no scopes)\n");
				else
				{
					guint n = 0;
					guint s;

					for (s = 0; s < scopes->len && n < VAR_CAP; s++)
					{
						LrgScriptScope       *scope = g_ptr_array_index (scopes, s);
						g_autoptr(GPtrArray)  got = NULL;
						g_autoptr(GError)     var_error = NULL;
						guint                 v;

						g_string_append_printf (vars, "%s\n", lrg_script_scope_get_name (scope));
						got = lrg_script_debugger_get_variables (self->debugger,
						        lrg_script_scope_get_variables_reference (scope), &var_error);
						if (got == NULL)
							continue;
						for (v = 0; v < got->len && n < VAR_CAP; v++, n++)
						{
							LrgScriptVariable *var = g_ptr_array_index (got, v);

							g_string_append_printf (vars, "  %s = %s\n",
							                        lrg_script_variable_get_name (var),
							                        lrg_script_variable_get_value (var));
						}
					}
				}
			}
		}
	}

	watches = g_string_new ("Watches\n");
	if (self->watches->len == 0)
		g_string_append (watches, "(none)\n");
	for (i = 0; i < self->watches->len; i++)
	{
		const gchar *expression = g_ptr_array_index (self->watches, i);

		if (!paused)
			g_string_append_printf (watches, "%s  (not paused)\n", expression);
		else
		{
			g_autoptr(GError) error = NULL;
			g_autoptr(LrgScriptVariable) var = NULL;

			var = lrg_script_debugger_evaluate (self->debugger, 0, expression, &error);
			if (var == NULL)
				g_string_append_printf (watches, "%s  %s\n", expression,
				                        error != NULL ? error->message : "error");
			else
				g_string_append_printf (watches, "%s = %s\n", expression,
				                        lrg_script_variable_get_value (var));
		}
	}

	bps = g_string_new ("Breakpoints\n");
	{
		g_autoptr(GPtrArray) all = lrg_script_debugger_get_breakpoints (self->debugger);

		if (all == NULL || all->len == 0)
			g_string_append (bps, "(none)\n");
		else
		{
			for (i = 0; i < all->len; i++)
			{
				LrgScriptBreakpoint *bp = g_ptr_array_index (all, i);

				g_string_append_printf (bps, "%s:%d%s%s\n",
				                        lrg_script_breakpoint_get_file (bp),
				                        lrg_script_breakpoint_get_line (bp),
				                        lrg_script_breakpoint_get_enabled (bp) ? "" : "  (disabled)",
				                        lrg_script_breakpoint_get_verified (bp) ? "" : "  (unverified)");
			}
		}
	}

	set_label (self->stack_label, stack->str);
	set_label (self->vars_label, vars->str);
	set_label (self->watch_label, watches->str);
	set_label (self->bp_label, bps->str);
	g_string_free (stack, TRUE);
	g_string_free (vars, TRUE);
	g_string_free (watches, TRUE);
	g_string_free (bps, TRUE);
}

static void
install (LrgEditorDebugPanel *self)
{
	LrgEditorApp *app = self->app;

	if (app == NULL)
		return;
	if (app->actions != NULL)
	{
		lrg_editor_actions_add (app->actions, "debug.show", "Debug Panel", "View", NULL,
		                        a_show, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.continue", "Continue", "Debug", NULL,
		                        a_continue, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.pause", "Pause", "Debug", NULL,
		                        a_pause, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.step-over", "Step Over", "Debug", NULL,
		                        a_over, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.step-in", "Step In", "Debug", NULL,
		                        a_in, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.step-out", "Step Out", "Debug", NULL,
		                        a_out, NULL, NULL, app);
		lrg_editor_actions_add (app->actions, "debug.stop", "Stop Debugging", "Debug", NULL,
		                        a_stop, NULL, NULL, app);
	}
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "debug.state", m_state, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.show", m_show, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.breakpoints", m_breakpoints, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.set_breakpoint", m_set_breakpoint, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.remove_breakpoint", m_remove_breakpoint, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.toggle_breakpoint", m_toggle_breakpoint, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.clear_breakpoints", m_clear_breakpoints, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.continue", m_continue, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.pause", m_pause, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.step_over", m_step_over, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.step_in", m_step_in, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.step_out", m_step_out, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.stop", m_stop, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.stack", m_stack, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.scopes", m_scopes, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.variables", m_variables, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.evaluate", m_evaluate, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.watches", m_watches, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.add_watch", m_add_watch, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.remove_watch", m_remove_watch, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.dap_start", m_dap_start, app, NULL);
		lrg_editor_server_register_method (app->server, "debug.dap_stop", m_dap_stop, app, NULL);
	}
}

static void
lrg_editor_debug_panel_layout_children (LrgContainer *container)
{
	LrgEditorDebugPanel *self = LRG_EDITOR_DEBUG_PANEL (container);
	gfloat               w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat               h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat               tw = 0.0f;
	gfloat               th = 0.0f;
	gfloat               ih = 0.0f;

	if (self->toolbar == NULL || self->scroll == NULL || self->input == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->toolbar), &tw, &th);
	lrg_widget_get_preferred_size (LRG_WIDGET (self->input), &tw, &ih);
	lrg_widget_place_in_slot (LRG_WIDGET (self->toolbar), 0.0f, 0.0f, w, th);
	lrg_widget_place_in_slot (LRG_WIDGET (self->input), 0.0f, MAX (h - ih, th), w, ih);
	lrg_widget_place_in_slot (LRG_WIDGET (self->scroll), 0.0f, th, w, MAX (h - th - ih, 0.0f));
}

static void
lrg_editor_debug_panel_measure (LrgWidget *widget,
                                gfloat    *width,
                                gfloat    *height)
{
	(void) widget;
	*width = 240.0f;
	*height = 160.0f;
}

static gboolean
lrg_editor_debug_panel_step (LrgWidget *widget,
                             gfloat     delta_time)
{
	LrgEditorDebugPanel *self = LRG_EDITOR_DEBUG_PANEL (widget);
	LrgWidgetClass      *parent = LRG_WIDGET_CLASS (lrg_editor_debug_panel_parent_class);
	gboolean             busy = parent->step != NULL ? parent->step (widget, delta_time) : FALSE;

	refresh (self, FALSE);
	return busy;
}

static void
lrg_editor_debug_panel_dispose (GObject *object)
{
	LrgEditorDebugPanel *self = LRG_EDITOR_DEBUG_PANEL (object);

	if (self->output_id != 0 && self->debugger != NULL)
		g_signal_handler_disconnect (self->debugger, self->output_id);
	self->output_id = 0;
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_weak_pointer (&self->app);
	if (self->dap != NULL)
		lrg_dap_server_stop (self->dap);
	g_clear_object (&self->dap);
	G_OBJECT_CLASS (lrg_editor_debug_panel_parent_class)->dispose (object);
}

static void
lrg_editor_debug_panel_finalize (GObject *object)
{
	LrgEditorDebugPanel *self = LRG_EDITOR_DEBUG_PANEL (object);

	g_clear_pointer (&self->watches, g_ptr_array_unref);
	if (self->log != NULL)
		g_string_free (self->log, TRUE);
	G_OBJECT_CLASS (lrg_editor_debug_panel_parent_class)->finalize (object);
}

static void
lrg_editor_debug_panel_class_init (LrgEditorDebugPanelClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_editor_debug_panel_dispose;
	object_class->finalize = lrg_editor_debug_panel_finalize;
	widget_class->measure = lrg_editor_debug_panel_measure;
	widget_class->step = lrg_editor_debug_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_debug_panel_layout_children;
}

static void
lrg_editor_debug_panel_init (LrgEditorDebugPanel *self)
{
	LrgVBox  *box;
	LrgLabel *label;

	self->debugger = lrg_script_debugger_get_default ();
	self->dap = lrg_dap_server_new_for_debugger (self->debugger);
	self->watches = g_ptr_array_new_with_free_func (g_free);
	self->log = g_string_new ("");
	hook_output (self);

	self->toolbar = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 4.0f);
	lrg_container_set_padding (LRG_CONTAINER (self->toolbar), 4.0f);
	lrg_widget_set_name (LRG_WIDGET (self->toolbar), "debug-toolbar");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->toolbar));
	g_object_unref (self->toolbar);
	toolbar_button (self, "Continue", "debug-continue");
	toolbar_button (self, "Pause", "debug-pause");
	toolbar_button (self, "Over", "debug-step-over");
	toolbar_button (self, "In", "debug-step-in");
	toolbar_button (self, "Out", "debug-step-out");
	toolbar_button (self, "Stop", "debug-stop");

	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 2.0f);
	label = section_label ("debug-stack", "Call stack\n(not paused)\n");
	self->stack_label = label;
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	label = section_label ("debug-variables", "Variables\n(not paused)\n");
	self->vars_label = label;
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	label = section_label ("debug-watches", "Watches\n(none)\n");
	self->watch_label = label;
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	label = section_label ("debug-breakpoints", "Breakpoints\n(none)\n");
	self->bp_label = label;
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	label = section_label ("debug-console", "(console)");
	self->console = label;
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);

	self->scroll = lrg_scroll_box_new ();
	lrg_widget_set_name (LRG_WIDGET (self->scroll), "debug-scroll");
	lrg_scroll_box_set_child (self->scroll, LRG_WIDGET (box));
	g_object_unref (box);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->scroll));
	g_object_unref (self->scroll);

	self->input = lrg_text_input_new_with_placeholder ("Evaluate, /watch expr, /unwatch expr");
	lrg_widget_set_name (LRG_WIDGET (self->input), "debug-evaluate");
	g_signal_connect_object (self->input, "submitted", G_CALLBACK (on_submit), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->input));
	g_object_unref (self->input);
}

LrgEditorDebugPanel *
lrg_editor_debug_panel_new (LrgEditorApp *app)
{
	LrgEditorDebugPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_get_data (G_OBJECT (app), PANEL_KEY);
	if (self != NULL)
		return g_object_ref (self);

	self = g_object_new (LRG_TYPE_EDITOR_DEBUG_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);
	install (self);
	return self;
}

LrgEditorApp *
lrg_editor_debug_panel_get_app (LrgEditorDebugPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DEBUG_PANEL (self), NULL);
	return self->app;
}

LrgDapServer *
lrg_editor_debug_panel_get_dap (LrgEditorDebugPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DEBUG_PANEL (self), NULL);
	return self->dap;
}
