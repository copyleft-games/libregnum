/* lrg-dap-server.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDapServer: Debug Adapter Protocol (Content-Length + JSON) over TCP
 * or stdio. Requests run on the thread that called start(); that thread's
 * main context is what a stopped script iterates, so stack and variable
 * requests are served while the script is paused. The editor listens on
 * TCP. stdio exists for a standalone adapter.
 *
 * Stopped and continued events are produced by polling the debugger.
 * LrgScriptDebugger::output is forwarded when that signal is two strings.
 * Events wait until the client has sent configurationDone.
 */

#include "config.h"

#include "lrg-dap-server.h"

#include <json-glib/json-glib.h>
#include <gio/gio.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <unistd.h>
#endif

#define DAP_POLL_MS      (50)
#define DAP_MAX_BODY     (8 * 1024 * 1024)
#define DAP_MAX_HEADERS  (32)

typedef struct _DapClient DapClient;

typedef struct
{
	LrgDapServer *server;
	gchar        *category;
	gchar        *text;
} OutIdle;

struct _DapClient
{
	LrgDapServer     *server;       /* borrowed */
	gint              ref;
	GSocketConnection *connection;  /* nullable (stdio) */
	GDataInputStream *in;
	GOutputStream    *out;
	GCancellable     *cancellable;
	GByteArray       *body;
	gsize             need;
	gint              headers;
	gboolean          closed;
	gboolean          saw_length;
	gboolean          initialized;
	gboolean          configured;
	gboolean          announced;    /* initialized event sent */
};

struct _LrgDapServer
{
	GObject            parent_instance;

	LrgScriptDebugger *debugger;
	gulong             output_id;
	GSocketService    *service;
	GPtrArray         *clients;     /* DapClient*, server holds one ref */
	GMainContext      *context;
	guint              poll_id;
	gint64             seq;
	guint16            port;
	gchar             *host;
	gboolean           listening;
	gboolean           stopping;
	gboolean           was_paused;
};

G_DEFINE_FINAL_TYPE (LrgDapServer, lrg_dap_server, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_DEBUGGER,
	PROP_LISTENING,
	PROP_PORT,
	PROP_N_CLIENTS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void          client_unref      (DapClient *client);
static void          client_close      (DapClient *client);
static void          client_read_header (DapClient *client);
static void          clients_sweep     (LrgDapServer *self);
static gboolean      client_send       (DapClient *client,
                                        JsonNode  *node);
static void          server_broadcast  (LrgDapServer *self,
                                        JsonNode     *node,
                                        gboolean      configured_only);
static JsonNode     *dap_event         (LrgDapServer *self,
                                        const gchar  *name,
                                        JsonObject   *body);
static void          hook_output       (LrgDapServer *self);
static void          unhook_output     (LrgDapServer *self);

G_DEFINE_QUARK (lrg-dap-server-error-quark, lrg_dap_server_error)

GType
lrg_dap_server_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_DAP_SERVER_ERROR_FAILED, "LRG_DAP_SERVER_ERROR_FAILED", "failed" },
			{ LRG_DAP_SERVER_ERROR_BIND, "LRG_DAP_SERVER_ERROR_BIND", "bind" },
			{ LRG_DAP_SERVER_ERROR_PROTOCOL, "LRG_DAP_SERVER_ERROR_PROTOCOL", "protocol" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgDapServerError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Clients
 * ========================================================================== */

static DapClient *
client_new (LrgDapServer  *server,
            GInputStream  *in_base,
            GOutputStream *out)
{
	DapClient *client = g_new0 (DapClient, 1);

	client->server = server;
	client->ref = 1;
	client->in = g_data_input_stream_new (in_base);
	client->out = g_object_ref (out);
	client->cancellable = g_cancellable_new ();
	client->body = g_byte_array_new ();
	g_data_input_stream_set_newline_type (client->in, G_DATA_STREAM_NEWLINE_TYPE_ANY);
	g_ptr_array_add (server->clients, client);
	g_object_notify_by_pspec (G_OBJECT (server), properties[PROP_N_CLIENTS]);
	return client;
}

static DapClient *
client_ref (DapClient *client)
{
	client->ref++;
	return client;
}

static void
client_unref (DapClient *client)
{
	if (client == NULL)
		return;
	client->ref--;
	if (client->ref > 0)
		return;
	g_clear_object (&client->in);
	g_clear_object (&client->out);
	g_clear_object (&client->connection);
	g_clear_object (&client->cancellable);
	if (client->body != NULL)
		g_byte_array_unref (client->body);
	g_free (client);
}

static void
client_close (DapClient *client)
{
	if (client->closed)
		return;
	client->closed = TRUE;
	if (client->cancellable != NULL)
		g_cancellable_cancel (client->cancellable);
}

static void
clients_sweep (LrgDapServer *self)
{
	guint i = 0;

	while (i < self->clients->len)
	{
		DapClient *client = (DapClient *) g_ptr_array_index (self->clients, i);

		if (client->closed)
		{
			g_ptr_array_remove_index (self->clients, i);
			client_unref (client);
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_CLIENTS]);
		}
		else
			i++;
	}
}

static gboolean
client_send (DapClient *client,
             JsonNode  *node)
{
	g_autoptr(JsonGenerator) gen = NULL;
	g_autofree gchar        *body = NULL;
	g_autofree gchar        *header = NULL;
	g_autoptr(GError)        error = NULL;
	gsize                    len = 0;
	gboolean                 ok;

	if (client->closed || node == NULL)
		return FALSE;
	gen = json_generator_new ();
	json_generator_set_root (gen, node);
	body = json_generator_to_data (gen, &len);
	header = g_strdup_printf ("Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n", len);
	ok = g_output_stream_write_all (client->out, header, strlen (header), NULL, NULL, &error);
	if (ok)
		ok = g_output_stream_write_all (client->out, body, len, NULL, NULL, &error);
	if (!ok)
		client_close (client);
	return ok;
}

static gint64
next_seq (LrgDapServer *self)
{
	self->seq++;
	return self->seq;
}

static JsonNode *
dap_message (LrgDapServer *self,
             const gchar  *type,
             JsonObject   *extra)
{
	JsonObject *object = extra != NULL ? extra : json_object_new ();
	JsonNode   *node = json_node_new (JSON_NODE_OBJECT);

	json_object_set_int_member (object, "seq", next_seq (self));
	json_object_set_string_member (object, "type", type);
	json_node_take_object (node, object);
	return node;
}

static JsonNode *
dap_response (LrgDapServer *self,
              gint64        request_seq,
              const gchar  *command,
              gboolean      success,
              const gchar  *message,
              JsonObject   *body)
{
	JsonObject *object = json_object_new ();
	JsonNode   *node;

	json_object_set_int_member (object, "request_seq", request_seq);
	json_object_set_boolean_member (object, "success", success);
	json_object_set_string_member (object, "command", command != NULL ? command : "");
	if (message != NULL)
		json_object_set_string_member (object, "message", message);
	if (body != NULL)
		json_object_set_object_member (object, "body", body);
	node = dap_message (self, "response", object);
	return node;
}

static JsonNode *
dap_event (LrgDapServer *self,
           const gchar  *name,
           JsonObject   *body)
{
	JsonObject *object = json_object_new ();

	json_object_set_string_member (object, "event", name);
	if (body != NULL)
		json_object_set_object_member (object, "body", body);
	return dap_message (self, "event", object);
}

static void
server_broadcast (LrgDapServer *self,
                  JsonNode     *node,
                  gboolean      configured_only)
{
	guint i;

	for (i = 0; i < self->clients->len; i++)
	{
		DapClient *client = (DapClient *) g_ptr_array_index (self->clients, i);

		if (client->closed)
			continue;
		if (configured_only && !client->configured)
			continue;
		client_send (client, node);
	}
	clients_sweep (self);
}

/* ==========================================================================
 * JSON helpers
 * ========================================================================== */

static JsonObject *
args_of (JsonObject *request)
{
	JsonNode *node;

	if (request == NULL || !json_object_has_member (request, "arguments"))
		return NULL;
	node = json_object_get_member (request, "arguments");
	if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
		return NULL;
	return json_node_get_object (node);
}

static const gchar *
arg_string (JsonObject  *args,
            const gchar *name)
{
	JsonNode *node;

	if (args == NULL || !json_object_has_member (args, name))
		return NULL;
	node = json_object_get_member (args, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (node);
}

static gboolean
arg_int (JsonObject  *args,
         const gchar *name,
         gint64      *out)
{
	JsonNode *node;

	if (args == NULL || !json_object_has_member (args, name))
		return FALSE;
	node = json_object_get_member (args, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return FALSE;
	if (json_node_get_value_type (node) != G_TYPE_INT64)
		return FALSE;
	*out = json_node_get_int (node);
	return TRUE;
}

static gboolean
arg_bool (JsonObject  *args,
          const gchar *name,
          gboolean     fallback)
{
	JsonNode *node;

	if (args == NULL || !json_object_has_member (args, name))
		return fallback;
	node = json_object_get_member (args, name);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_BOOLEAN)
		return fallback;
	return json_node_get_boolean (node);
}

static gchar *
source_path (JsonObject *args)
{
	JsonObject *source;
	JsonNode   *node;
	const gchar *path;
	const gchar *uri;

	if (args == NULL || !json_object_has_member (args, "source"))
		return NULL;
	node = json_object_get_member (args, "source");
	if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
		return NULL;
	source = json_node_get_object (node);
	path = arg_string (source, "path");
	if (path != NULL && path[0] != '\0')
		return lrg_script_debugger_normalize_path (path);
	uri = arg_string (source, "uri");
	if (uri != NULL && g_str_has_prefix (uri, "file://"))
	{
		g_autofree gchar *unescaped = g_uri_unescape_string (uri + 7, NULL);

		return lrg_script_debugger_normalize_path (unescaped != NULL ? unescaped : uri + 7);
	}
	return NULL;
}

static const gchar *
stop_reason_name (LrgScriptDebugger *debugger)
{
	LrgScriptStopReason reason;

	if (debugger == NULL || !lrg_script_debugger_is_paused (debugger))
		return "pause";
	reason = lrg_script_debugger_get_stop_reason (debugger);
	if (reason == LRG_SCRIPT_STOP_REASON_NONE)
		return "pause";
	return lrg_script_stop_reason_to_string (reason);
}

static guint
stopped_thread (LrgScriptDebugger *debugger)
{
	guint id;

	if (debugger == NULL || !lrg_script_debugger_is_paused (debugger))
		return 1;
	id = lrg_script_debugger_get_stopped_thread_id (debugger);
	return id != 0 ? id : 1;
}

static JsonObject *
stopped_body (LrgDapServer *self)
{
	JsonObject     *body = json_object_new ();
	g_autofree gchar *text = NULL;

	json_object_set_string_member (body, "reason", stop_reason_name (self->debugger));
	json_object_set_int_member (body, "threadId", (gint64) stopped_thread (self->debugger));
	json_object_set_boolean_member (body, "allThreadsStopped", TRUE);
	if (self->debugger != NULL)
		text = lrg_script_debugger_dup_stop_description (self->debugger);
	if (text != NULL && text[0] != '\0')
		json_object_set_string_member (body, "text", text);
	return body;
}

static void
emit_stopped (LrgDapServer *self)
{
	g_autoptr(JsonNode) event = NULL;

	if (self->stopping)
		return;
	event = dap_event (self, "stopped", stopped_body (self));
	server_broadcast (self, event, TRUE);
}

static void
emit_continued (LrgDapServer *self)
{
	JsonObject          *body = json_object_new ();
	g_autoptr(JsonNode)  event = NULL;

	json_object_set_int_member (body, "threadId", (gint64) stopped_thread (self->debugger));
	json_object_set_boolean_member (body, "allThreadsContinued", TRUE);
	event = dap_event (self, "continued", body);
	server_broadcast (self, event, TRUE);
}

static void
emit_output_now (LrgDapServer *self,
                 const gchar  *category,
                 const gchar  *text)
{
	JsonObject          *body = json_object_new ();
	g_autofree gchar    *line = NULL;
	g_autoptr(JsonNode)  event = NULL;

	if (text == NULL)
		text = "";
	if (g_str_has_suffix (text, "\n"))
		line = g_strdup (text);
	else
		line = g_strdup_printf ("%s\n", text);
	json_object_set_string_member (body, "category", category != NULL ? category : "console");
	json_object_set_string_member (body, "output", line);
	event = dap_event (self, "output", body);
	server_broadcast (self, event, TRUE);
}

static gboolean
out_idle (gpointer data)
{
	OutIdle *message = (OutIdle *) data;

	if (!message->server->stopping)
		emit_output_now (message->server, message->category, message->text);
	g_object_unref (message->server);
	g_free (message->category);
	g_free (message->text);
	g_free (message);
	return G_SOURCE_REMOVE;
}

static void
on_debugger_output (LrgScriptDebugger *debugger,
                    const gchar       *category,
                    const gchar       *text,
                    gpointer           user_data)
{
	LrgDapServer *self = user_data;
	OutIdle      *message;

	(void) debugger;
	if (self->stopping || self->context == NULL)
		return;
	message = g_new0 (OutIdle, 1);
	message->server = g_object_ref (self);
	message->category = g_strdup (category);
	message->text = g_strdup (text);
	g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT, out_idle, message, NULL);
}

static gboolean
dap_poll (gpointer data)
{
	LrgDapServer *self = data;
	gboolean      paused;

	if (self->stopping || self->debugger == NULL)
		return G_SOURCE_CONTINUE;
	paused = lrg_script_debugger_is_paused (self->debugger);
	if (paused && !self->was_paused)
	{
		self->was_paused = TRUE;
		emit_stopped (self);
	}
	else if (!paused && self->was_paused)
	{
		self->was_paused = FALSE;
		emit_continued (self);
	}
	return G_SOURCE_CONTINUE;
}

/* ==========================================================================
 * Requests
 * ========================================================================== */

static JsonObject *
breakpoint_json (LrgScriptBreakpoint *breakpoint,
                 gint                 line)
{
	JsonObject *object = json_object_new ();

	json_object_set_int_member (object, "line", breakpoint != NULL
	                           ? lrg_script_breakpoint_get_line (breakpoint) : line);
	json_object_set_boolean_member (object, "verified",
	                                breakpoint != NULL && lrg_script_breakpoint_get_verified (breakpoint));
	if (breakpoint != NULL)
		json_object_set_int_member (object, "id", (gint64) lrg_script_breakpoint_get_id (breakpoint));
	return object;
}

static void
apply_breakpoint_fields (LrgScriptBreakpoint *breakpoint,
                         JsonObject          *spec)
{
	const gchar *condition;
	const gchar *hits;
	const gchar *log;

	if (breakpoint == NULL || spec == NULL)
		return;
	condition = arg_string (spec, "condition");
	hits = arg_string (spec, "hitCondition");
	log = arg_string (spec, "logMessage");
	if (condition != NULL)
		lrg_script_breakpoint_set_condition (breakpoint, condition);
	if (hits != NULL)
		lrg_script_breakpoint_set_hit_condition (breakpoint, hits);
	if (log != NULL)
		lrg_script_breakpoint_set_log_message (breakpoint, log);
}

static JsonNode *
handle_initialize (LrgDapServer *self,
                   gint64        seq)
{
	JsonObject *body = json_object_new ();
	JsonArray  *filters = json_array_new ();
	JsonObject *uncaught = json_object_new ();
	JsonObject *raised = json_object_new ();

	json_object_set_boolean_member (body, "supportsConfigurationDoneRequest", TRUE);
	json_object_set_boolean_member (body, "supportsEvaluateForHovers", TRUE);
	json_object_set_boolean_member (body, "supportsTerminateRequest", TRUE);
	json_object_set_boolean_member (body, "supportsConditionalBreakpoints", TRUE);
	json_object_set_boolean_member (body, "supportsHitConditionalBreakpoints", TRUE);
	json_object_set_boolean_member (body, "supportsLogPoints", TRUE);
	json_object_set_boolean_member (body, "supportsExceptionOptions", FALSE);
	json_object_set_boolean_member (body, "supportTerminateDebuggee", TRUE);
	json_object_set_string_member (uncaught, "filter", "uncaught");
	json_object_set_string_member (uncaught, "label", "Uncaught Exceptions");
	json_object_set_boolean_member (uncaught, "default", FALSE);
	json_object_set_string_member (raised, "filter", "raised");
	json_object_set_string_member (raised, "label", "All Exceptions");
	json_object_set_boolean_member (raised, "default", FALSE);
	json_array_add_object_element (filters, uncaught);
	json_array_add_object_element (filters, raised);
	json_object_set_array_member (body, "exceptionBreakpointFilters", filters);
	return dap_response (self, seq, "initialize", TRUE, NULL, body);
}

static JsonNode *
handle_set_breakpoints (LrgDapServer *self,
                        JsonObject   *request,
                        gint64        seq)
{
	JsonObject *args = args_of (request);
	g_autofree gchar *path = source_path (args);
	JsonArray  *specs = NULL;
	JsonArray  *out;
	JsonObject *body;
	JsonNode   *node;
	guint       i;

	if (self->debugger == NULL)
		return dap_response (self, seq, "setBreakpoints", FALSE, "No debugger", NULL);
	if (path == NULL)
		return dap_response (self, seq, "setBreakpoints", FALSE, "Missing source.path", NULL);
	lrg_script_debugger_clear_breakpoints (self->debugger, path);
	if (args != NULL && json_object_has_member (args, "breakpoints"))
	{
		node = json_object_get_member (args, "breakpoints");
		if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
			specs = json_node_get_array (node);
	}
	out = json_array_new ();
	if (specs != NULL)
	{
		for (i = 0; i < json_array_get_length (specs); i++)
		{
			JsonNode            *element = json_array_get_element (specs, i);
			JsonObject          *spec = NULL;
			gint64               line = 0;
			LrgScriptBreakpoint *breakpoint = NULL;

			if (element != NULL && JSON_NODE_HOLDS_OBJECT (element))
			{
				spec = json_node_get_object (element);
				arg_int (spec, "line", &line);
			}
			if (line > 0)
			{
				breakpoint = lrg_script_debugger_add_breakpoint (self->debugger, path, (gint) line);
				apply_breakpoint_fields (breakpoint, spec);
			}
			json_array_add_object_element (out, breakpoint_json (breakpoint, (gint) line));
		}
	}
	else if (args != NULL && json_object_has_member (args, "lines"))
	{
		node = json_object_get_member (args, "lines");
		if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
		{
			JsonArray *lines = json_node_get_array (node);

			for (i = 0; i < json_array_get_length (lines); i++)
			{
				gint64               line = json_array_get_int_element (lines, i);
				LrgScriptBreakpoint *breakpoint = NULL;

				if (line > 0)
					breakpoint = lrg_script_debugger_add_breakpoint (self->debugger, path, (gint) line);
				json_array_add_object_element (out, breakpoint_json (breakpoint, (gint) line));
			}
		}
	}
	body = json_object_new ();
	json_object_set_array_member (body, "breakpoints", out);
	return dap_response (self, seq, "setBreakpoints", TRUE, NULL, body);
}

static JsonNode *
handle_set_exception_breakpoints (LrgDapServer *self,
                                  JsonObject   *request,
                                  gint64        seq)
{
	JsonObject             *args = args_of (request);
	LrgScriptExceptionMode  mode = LRG_SCRIPT_EXCEPTION_MODE_NEVER;
	JsonNode               *node;
	guint                   i;

	if (self->debugger == NULL)
		return dap_response (self, seq, "setExceptionBreakpoints", FALSE, "No debugger", NULL);
	if (args != NULL && json_object_has_member (args, "filters"))
	{
		node = json_object_get_member (args, "filters");
		if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
		{
			JsonArray *filters = json_node_get_array (node);

			for (i = 0; i < json_array_get_length (filters); i++)
			{
				const gchar *filter = json_array_get_string_element (filters, i);

				if (g_strcmp0 (filter, "raised") == 0 || g_strcmp0 (filter, "all") == 0)
					mode = LRG_SCRIPT_EXCEPTION_MODE_ALL;
				else if (g_strcmp0 (filter, "uncaught") == 0 && mode != LRG_SCRIPT_EXCEPTION_MODE_ALL)
					mode = LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT;
			}
		}
	}
	lrg_script_debugger_set_exception_mode (self->debugger, mode);
	return dap_response (self, seq, "setExceptionBreakpoints", TRUE, NULL, NULL);
}

static JsonNode *
handle_threads (LrgDapServer *self,
                gint64        seq)
{
	JsonArray           *threads = json_array_new ();
	JsonObject          *body = json_object_new ();
	g_autoptr(GPtrArray) attached = NULL;
	guint                i;

	if (self->debugger != NULL)
		attached = lrg_script_debugger_get_attached (self->debugger);
	if (attached != NULL)
	{
		for (i = 0; i < attached->len; i++)
		{
			LrgScripting *scripting = (LrgScripting *) g_ptr_array_index (attached, i);
			guint         id = lrg_script_debugger_get_thread_id (self->debugger, scripting);
			JsonObject   *thread;

			if (id == 0)
				continue;
			thread = json_object_new ();
			json_object_set_int_member (thread, "id", (gint64) id);
			json_object_set_string_member (thread, "name", "script");
			json_array_add_object_element (threads, thread);
		}
	}
	if (json_array_get_length (threads) == 0)
	{
		JsonObject *thread = json_object_new ();

		json_object_set_int_member (thread, "id", 1);
		json_object_set_string_member (thread, "name", "script");
		json_array_add_object_element (threads, thread);
	}
	json_object_set_array_member (body, "threads", threads);
	return dap_response (self, seq, "threads", TRUE, NULL, body);
}

static JsonNode *
handle_stack (LrgDapServer *self,
              JsonObject   *request,
              gint64        seq)
{
	JsonObject          *args = args_of (request);
	JsonArray           *frames = json_array_new ();
	JsonObject          *body = json_object_new ();
	g_autoptr(GPtrArray) stack = NULL;
	g_autoptr(GError)    error = NULL;
	gint64               start = 0;
	gint64               levels = 0;
	guint                i;
	guint                begin;
	guint                end;

	if (self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger))
		stack = lrg_script_debugger_get_stack_frames (self->debugger, &error);
	arg_int (args, "startFrame", &start);
	arg_int (args, "levels", &levels);
	if (start < 0)
		start = 0;
	begin = (guint) start;
	end = stack != NULL ? stack->len : 0;
	if (levels > 0 && begin + (guint) levels < end)
		end = begin + (guint) levels;
	for (i = begin; stack != NULL && i < end && i < stack->len; i++)
	{
		LrgScriptStackFrame *frame = (LrgScriptStackFrame *) g_ptr_array_index (stack, i);
		JsonObject          *object = json_object_new ();
		const gchar         *file = lrg_script_stack_frame_get_file (frame);
		gint                 line = lrg_script_stack_frame_get_line (frame);

		json_object_set_int_member (object, "id", (gint64) lrg_script_stack_frame_get_id (frame));
		json_object_set_string_member (object, "name", lrg_script_stack_frame_get_name (frame));
		json_object_set_int_member (object, "line", line > 0 ? line : 1);
		json_object_set_int_member (object, "column", 1);
		if (file != NULL)
		{
			JsonObject *source = json_object_new ();

			json_object_set_string_member (source, "path", file);
			json_object_set_object_member (object, "source", source);
		}
		json_array_add_object_element (frames, object);
	}
	json_object_set_array_member (body, "stackFrames", frames);
	json_object_set_int_member (body, "totalFrames", stack != NULL ? (gint64) stack->len : 0);
	return dap_response (self, seq, "stackTrace", TRUE, NULL, body);
}

static JsonNode *
handle_scopes (LrgDapServer *self,
               JsonObject   *request,
               gint64        seq)
{
	JsonObject          *args = args_of (request);
	gint64               frame_id = 0;
	g_autoptr(GPtrArray) scopes = NULL;
	g_autoptr(GError)    error = NULL;
	JsonArray           *out = json_array_new ();
	JsonObject          *body = json_object_new ();
	guint                i;

	if (self->debugger == NULL || !lrg_script_debugger_is_paused (self->debugger))
	{
		json_array_unref (out);
		json_object_unref (body);
		return dap_response (self, seq, "scopes", FALSE, "Not paused", NULL);
	}
	arg_int (args, "frameId", &frame_id);
	scopes = lrg_script_debugger_get_scopes (self->debugger, (guint) frame_id, &error);
	if (scopes == NULL)
	{
		json_array_unref (out);
		json_object_unref (body);
		return dap_response (self, seq, "scopes", FALSE,
		                     error != NULL ? error->message : "No scopes", NULL);
	}
	for (i = 0; i < scopes->len; i++)
	{
		LrgScriptScope *scope = (LrgScriptScope *) g_ptr_array_index (scopes, i);
		JsonObject     *object = json_object_new ();

		json_object_set_string_member (object, "name", lrg_script_scope_get_name (scope));
		json_object_set_int_member (object, "variablesReference",
		                            (gint64) lrg_script_scope_get_variables_reference (scope));
		json_object_set_boolean_member (object, "expensive", lrg_script_scope_get_expensive (scope));
		json_array_add_object_element (out, object);
	}
	json_object_set_array_member (body, "scopes", out);
	return dap_response (self, seq, "scopes", TRUE, NULL, body);
}

static JsonNode *
handle_variables (LrgDapServer *self,
                  JsonObject   *request,
                  gint64        seq)
{
	JsonObject          *args = args_of (request);
	gint64               reference = 0;
	g_autoptr(GPtrArray) vars = NULL;
	g_autoptr(GError)    error = NULL;
	JsonArray           *out = json_array_new ();
	JsonObject          *body = json_object_new ();
	guint                i;

	if (self->debugger == NULL || !lrg_script_debugger_is_paused (self->debugger))
	{
		json_array_unref (out);
		json_object_unref (body);
		return dap_response (self, seq, "variables", FALSE, "Not paused", NULL);
	}
	if (!arg_int (args, "variablesReference", &reference) || reference <= 0)
	{
		json_array_unref (out);
		json_object_unref (body);
		return dap_response (self, seq, "variables", FALSE, "Missing variablesReference", NULL);
	}
	vars = lrg_script_debugger_get_variables (self->debugger, (guint) reference, &error);
	if (vars == NULL)
	{
		json_array_unref (out);
		json_object_unref (body);
		return dap_response (self, seq, "variables", FALSE,
		                     error != NULL ? error->message : "No variables", NULL);
	}
	for (i = 0; i < vars->len; i++)
	{
		LrgScriptVariable *var = (LrgScriptVariable *) g_ptr_array_index (vars, i);
		JsonObject        *object = json_object_new ();
		const gchar       *type_name = lrg_script_variable_get_type_name (var);

		json_object_set_string_member (object, "name", lrg_script_variable_get_name (var));
		json_object_set_string_member (object, "value", lrg_script_variable_get_value (var));
		json_object_set_int_member (object, "variablesReference",
		                            (gint64) lrg_script_variable_get_variables_reference (var));
		if (type_name != NULL)
			json_object_set_string_member (object, "type", type_name);
		json_array_add_object_element (out, object);
	}
	json_object_set_array_member (body, "variables", out);
	return dap_response (self, seq, "variables", TRUE, NULL, body);
}

static JsonNode *
handle_evaluate (LrgDapServer *self,
                 JsonObject   *request,
                 gint64        seq)
{
	JsonObject               *args = args_of (request);
	const gchar              *expression = arg_string (args, "expression");
	gint64                    frame_id = 0;
	g_autoptr(LrgScriptVariable) result = NULL;
	g_autoptr(GError)         error = NULL;
	JsonObject               *body;
	const gchar              *type_name;

	if (expression == NULL || expression[0] == '\0')
		return dap_response (self, seq, "evaluate", FALSE, "Missing expression", NULL);
	if (self->debugger == NULL || !lrg_script_debugger_is_paused (self->debugger))
		return dap_response (self, seq, "evaluate", FALSE, "Not paused", NULL);
	arg_int (args, "frameId", &frame_id);
	result = lrg_script_debugger_evaluate (self->debugger, (guint) frame_id, expression, &error);
	if (result == NULL)
		return dap_response (self, seq, "evaluate", FALSE,
		                     error != NULL ? error->message : "Evaluate failed", NULL);
	body = json_object_new ();
	json_object_set_string_member (body, "result", lrg_script_variable_get_value (result));
	type_name = lrg_script_variable_get_type_name (result);
	if (type_name != NULL)
		json_object_set_string_member (body, "type", type_name);
	json_object_set_int_member (body, "variablesReference",
	                            (gint64) lrg_script_variable_get_variables_reference (result));
	return dap_response (self, seq, "evaluate", TRUE, NULL, body);
}

static JsonNode *
handle_launch (LrgDapServer *self,
               JsonObject   *request,
               gint64        seq,
               const gchar  *command)
{
	JsonObject *args = args_of (request);

	if (self->debugger != NULL && arg_bool (args, "stopOnEntry", FALSE))
		lrg_script_debugger_pause (self->debugger);
	return dap_response (self, seq, command, TRUE, NULL, NULL);
}

static void
dispatch_request (LrgDapServer *self,
                  DapClient    *client,
                  JsonObject   *request)
{
	const gchar       *command = arg_string (request, "command");
	gint64             seq = 0;
	g_autoptr(JsonNode) response = NULL;
	gboolean           initialized_event = FALSE;
	gboolean           terminated = FALSE;
	gboolean           disconnect = FALSE;

	arg_int (request, "seq", &seq);
	if (command == NULL)
		command = "";
	if (!client->initialized && g_strcmp0 (command, "initialize") != 0)
	{
		response = dap_response (self, seq, command, FALSE, "Expected initialize", NULL);
	}
	else if (g_strcmp0 (command, "initialize") == 0)
	{
		client->initialized = TRUE;
		initialized_event = !client->announced;
		client->announced = TRUE;
		response = handle_initialize (self, seq);
	}
	else if (g_strcmp0 (command, "launch") == 0 || g_strcmp0 (command, "attach") == 0)
		response = handle_launch (self, request, seq, command);
	else if (g_strcmp0 (command, "setBreakpoints") == 0)
		response = handle_set_breakpoints (self, request, seq);
	else if (g_strcmp0 (command, "setExceptionBreakpoints") == 0)
		response = handle_set_exception_breakpoints (self, request, seq);
	else if (g_strcmp0 (command, "configurationDone") == 0)
	{
		client->configured = TRUE;
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "threads") == 0)
		response = handle_threads (self, seq);
	else if (g_strcmp0 (command, "stackTrace") == 0)
		response = handle_stack (self, request, seq);
	else if (g_strcmp0 (command, "scopes") == 0)
		response = handle_scopes (self, request, seq);
	else if (g_strcmp0 (command, "variables") == 0)
		response = handle_variables (self, request, seq);
	else if (g_strcmp0 (command, "evaluate") == 0)
		response = handle_evaluate (self, request, seq);
	else if (g_strcmp0 (command, "continue") == 0)
	{
		JsonObject *body = json_object_new ();

		if (self->debugger != NULL)
			lrg_script_debugger_resume (self->debugger);
		json_object_set_boolean_member (body, "allThreadsContinued", TRUE);
		response = dap_response (self, seq, command, TRUE, NULL, body);
	}
	else if (g_strcmp0 (command, "next") == 0)
	{
		if (self->debugger != NULL)
			lrg_script_debugger_step_over (self->debugger);
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "stepIn") == 0)
	{
		if (self->debugger != NULL)
			lrg_script_debugger_step_in (self->debugger);
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "stepOut") == 0)
	{
		if (self->debugger != NULL)
			lrg_script_debugger_step_out (self->debugger);
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "pause") == 0)
	{
		if (self->debugger != NULL)
			lrg_script_debugger_pause (self->debugger);
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "disconnect") == 0)
	{
		JsonObject *args = args_of (request);

		if (self->debugger != NULL && arg_bool (args, "terminateDebuggee", FALSE))
			lrg_script_debugger_detach_all (self->debugger);
		disconnect = TRUE;
		terminated = arg_bool (args, "terminateDebuggee", FALSE);
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "terminate") == 0)
	{
		if (self->debugger != NULL)
			lrg_script_debugger_detach_all (self->debugger);
		terminated = TRUE;
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	}
	else if (g_strcmp0 (command, "cancel") == 0)
		response = dap_response (self, seq, command, TRUE, NULL, NULL);
	else
		response = dap_response (self, seq, command, FALSE, "Unsupported command", NULL);

	if (response != NULL)
		client_send (client, response);
	if (initialized_event && !client->closed)
	{
		g_autoptr(JsonNode) event = dap_event (self, "initialized", NULL);

		client_send (client, event);
	}
	if (client->configured && self->was_paused && g_strcmp0 (command, "configurationDone") == 0 && !client->closed)
	{
		g_autoptr(JsonNode) event = dap_event (self, "stopped", stopped_body (self));

		client_send (client, event);
	}
	if (terminated && !client->closed)
	{
		g_autoptr(JsonNode) event = dap_event (self, "terminated", NULL);

		client_send (client, event);
	}
	if (disconnect)
		client_close (client);
	clients_sweep (self);
}

static void
handle_body (DapClient *client)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)     error = NULL;
	JsonNode             *root;
	JsonObject           *object;

	if (!json_parser_load_from_data (parser, (const gchar *) client->body->data,
	                                 (gssize) client->body->len, &error))
	{
		client_close (client);
		return;
	}
	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
	{
		client_close (client);
		return;
	}
	object = json_node_get_object (root);
	if (g_strcmp0 (arg_string (object, "type"), "request") == 0)
		dispatch_request (client->server, client, object);
}

/* ==========================================================================
 * Framing
 * ========================================================================== */

static void on_header_line (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data);
static void on_body        (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data);

static void
client_read_header (DapClient *client)
{
	if (client->closed || client->server->stopping)
		return;
	client_ref (client);
	g_data_input_stream_read_line_async (client->in, G_PRIORITY_DEFAULT, client->cancellable,
	                                     on_header_line, client);
}

static void
client_read_body (DapClient *client)
{
	gsize chunk;

	if (client->closed || client->server->stopping)
		return;
	chunk = client->need - client->body->len;
	client_ref (client);
	g_input_stream_read_bytes_async (G_INPUT_STREAM (client->in), chunk, G_PRIORITY_DEFAULT,
	                                 client->cancellable, on_body, client);
}

static void
on_header_line (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	DapClient        *client = user_data;
	g_autoptr(GError) error = NULL;
	gsize             len = 0;
	gchar            *line;

	(void) source;
	line = g_data_input_stream_read_line_finish (client->in, result, &len, &error);
	if (client->closed || client->server->stopping)
	{
		g_free (line);
		client_unref (client);
		return;
	}
	if (line == NULL)
	{
		client_close (client);
		clients_sweep (client->server);
		client_unref (client);
		return;
	}
	if (line[0] == '\0')
	{
		g_free (line);
		client->headers = 0;
		if (!client->saw_length || client->need == 0 || client->need > DAP_MAX_BODY)
		{
			client_close (client);
			clients_sweep (client->server);
			client_unref (client);
			return;
		}
		g_byte_array_set_size (client->body, 0);
		client_read_body (client);
		client_unref (client);
		return;
	}
	if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
	{
		client->need = (gsize) g_ascii_strtoull (g_strstrip (line + 15), NULL, 10);
		client->saw_length = TRUE;
	}
	g_free (line);
	client->headers++;
	if (client->headers > DAP_MAX_HEADERS)
	{
		client_close (client);
		clients_sweep (client->server);
		client_unref (client);
		return;
	}
	client_read_header (client);
	client_unref (client);
}

static void
on_body (GObject      *source,
         GAsyncResult *result,
         gpointer      user_data)
{
	DapClient        *client = user_data;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	gsize             size = 0;
	gconstpointer     data;

	(void) source;
	bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (client->in), result, &error);
	if (client->closed || client->server->stopping)
	{
		client_unref (client);
		return;
	}
	if (bytes == NULL)
	{
		client_close (client);
		clients_sweep (client->server);
		client_unref (client);
		return;
	}
	data = g_bytes_get_data (bytes, &size);
	if (size == 0)
	{
		client_close (client);
		clients_sweep (client->server);
		client_unref (client);
		return;
	}
	g_byte_array_append (client->body, data, size);
	if (client->body->len < client->need)
	{
		client_read_body (client);
		client_unref (client);
		return;
	}
	handle_body (client);
	client->saw_length = FALSE;
	client->need = 0;
	g_byte_array_set_size (client->body, 0);
	if (!client->closed && !client->server->stopping)
		client_read_header (client);
	else
		clients_sweep (client->server);
	client_unref (client);
}

static gboolean
on_incoming (GSocketService    *service,
             GSocketConnection *connection,
             GObject           *source,
             gpointer           user_data)
{
	LrgDapServer *self = user_data;
	DapClient    *client;

	(void) service;
	(void) source;
	if (self->stopping)
		return TRUE;
	client = client_new (self, g_io_stream_get_input_stream (G_IO_STREAM (connection)),
	                     g_io_stream_get_output_stream (G_IO_STREAM (connection)));
	client->connection = g_object_ref (connection);
	client_read_header (client);
	return TRUE;
}

/* ==========================================================================
 * Debugger signal
 * ========================================================================== */

static void
unhook_output (LrgDapServer *self)
{
	if (self->output_id != 0 && self->debugger != NULL)
		g_signal_handler_disconnect (self->debugger, self->output_id);
	self->output_id = 0;
}

static void
hook_output (LrgDapServer *self)
{
	guint        id;
	GSignalQuery query;

	unhook_output (self);
	if (self->debugger == NULL)
		return;
	id = g_signal_lookup ("output", G_OBJECT_TYPE (self->debugger));
	if (id == 0)
		return;
	g_signal_query (id, &query);
	if (query.n_params != 2 || query.param_types[0] != G_TYPE_STRING || query.param_types[1] != G_TYPE_STRING)
		return;
	self->output_id = g_signal_connect (self->debugger, "output", G_CALLBACK (on_debugger_output), self);
}

static void
attach_poll (LrgDapServer *self)
{
	GSource *source;

	if (self->poll_id != 0)
		return;
	source = g_timeout_source_new (DAP_POLL_MS);
	g_source_set_callback (source, dap_poll, g_object_ref (self), g_object_unref);
	self->poll_id = g_source_attach (source, self->context);
	g_source_unref (source);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_dap_server_dispose (GObject *object)
{
	LrgDapServer *self = LRG_DAP_SERVER (object);

	self->stopping = TRUE;
	lrg_dap_server_stop (self);
	unhook_output (self);
	g_clear_object (&self->debugger);
	G_OBJECT_CLASS (lrg_dap_server_parent_class)->dispose (object);
}

static void
lrg_dap_server_finalize (GObject *object)
{
	LrgDapServer *self = LRG_DAP_SERVER (object);

	g_clear_pointer (&self->clients, g_ptr_array_unref);
	g_clear_pointer (&self->context, g_main_context_unref);
	g_free (self->host);
	G_OBJECT_CLASS (lrg_dap_server_parent_class)->finalize (object);
}

static void
lrg_dap_server_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgDapServer *self = LRG_DAP_SERVER (object);

	switch (prop_id)
	{
	case PROP_DEBUGGER:
		g_value_set_object (value, self->debugger);
		break;
	case PROP_LISTENING:
		g_value_set_boolean (value, self->listening);
		break;
	case PROP_PORT:
		g_value_set_uint (value, self->port);
		break;
	case PROP_N_CLIENTS:
		g_value_set_uint (value, self->clients != NULL ? self->clients->len : 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dap_server_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgDapServer *self = LRG_DAP_SERVER (object);

	switch (prop_id)
	{
	case PROP_DEBUGGER:
		lrg_dap_server_set_debugger (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dap_server_class_init (LrgDapServerClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	const GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	const GParamFlags ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_dap_server_dispose;
	object_class->finalize = lrg_dap_server_finalize;
	object_class->get_property = lrg_dap_server_get_property;
	object_class->set_property = lrg_dap_server_set_property;

	/**
	 * LrgDapServer:debugger:
	 *
	 * The #LrgScriptDebugger requests act on.
	 */
	properties[PROP_DEBUGGER] =
		g_param_spec_object ("debugger", "Debugger", "Script debugger",
		                     LRG_TYPE_SCRIPT_DEBUGGER, rw);

	/**
	 * LrgDapServer:listening:
	 *
	 * Whether a TCP port or stdio client is active.
	 */
	properties[PROP_LISTENING] =
		g_param_spec_boolean ("listening", "Listening", "Transport is active", FALSE, ro);

	/**
	 * LrgDapServer:port:
	 *
	 * Bound TCP port, or 0.
	 */
	properties[PROP_PORT] =
		g_param_spec_uint ("port", "Port", "TCP port", 0, G_MAXUINT16, 0, ro);

	/**
	 * LrgDapServer:n-clients:
	 *
	 * Connected DAP clients.
	 */
	properties[PROP_N_CLIENTS] =
		g_param_spec_uint ("n-clients", "Clients", "Connected clients", 0, G_MAXUINT, 0, ro);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_dap_server_init (LrgDapServer *self)
{
	self->clients = g_ptr_array_new ();
	self->seq = 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDapServer *
lrg_dap_server_new (void)
{
	return lrg_dap_server_new_for_debugger (lrg_script_debugger_get_default ());
}

LrgDapServer *
lrg_dap_server_new_for_debugger (LrgScriptDebugger *debugger)
{
	LrgDapServer *self;

	g_return_val_if_fail (debugger == NULL || LRG_IS_SCRIPT_DEBUGGER (debugger), NULL);

	self = g_object_new (LRG_TYPE_DAP_SERVER, NULL);
	if (debugger != NULL)
		lrg_dap_server_set_debugger (self, debugger);
	return self;
}

void
lrg_dap_server_set_debugger (LrgDapServer      *self,
                             LrgScriptDebugger *debugger)
{
	g_return_if_fail (LRG_IS_DAP_SERVER (self));
	g_return_if_fail (debugger == NULL || LRG_IS_SCRIPT_DEBUGGER (debugger));

	if (self->debugger == debugger)
		return;
	unhook_output (self);
	g_set_object (&self->debugger, debugger);
	hook_output (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEBUGGER]);
}

LrgScriptDebugger *
lrg_dap_server_get_debugger (LrgDapServer *self)
{
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), NULL);
	return self->debugger;
}

gboolean
lrg_dap_server_start (LrgDapServer  *self,
                      const gchar   *host,
                      guint16        port,
                      GError       **error)
{
	g_autoptr(GSocketAddress) address = NULL;
	g_autoptr(GSocketAddress) bound = NULL;
	g_autoptr(GError)         local = NULL;
	gboolean                  ok;

	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), FALSE);

	if (self->listening)
		return TRUE;
	self->stopping = FALSE;
	if (host == NULL || host[0] == '\0')
		host = "127.0.0.1";
	address = g_inet_socket_address_new_from_string (host, port);
	if (address == NULL)
	{
		g_set_error (error, LRG_DAP_SERVER_ERROR, LRG_DAP_SERVER_ERROR_BIND,
		             "Cannot bind DAP server to %s", host);
		return FALSE;
	}
	self->service = g_socket_service_new ();
	ok = g_socket_listener_add_address (G_SOCKET_LISTENER (self->service), address,
	                                    G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
	                                    NULL, &bound, &local);
	if (!ok)
	{
		g_clear_object (&self->service);
		g_set_error (error, LRG_DAP_SERVER_ERROR, LRG_DAP_SERVER_ERROR_BIND,
		             "Cannot bind DAP server: %s", local != NULL ? local->message : host);
		return FALSE;
	}
	self->port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (bound));
	g_free (self->host);
	self->host = g_strdup (host);
	g_signal_connect (self->service, "incoming", G_CALLBACK (on_incoming), self);
	g_socket_service_start (self->service);
	self->context = g_main_context_ref_thread_default ();
	self->listening = TRUE;
	self->was_paused = self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger);
	attach_poll (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LISTENING]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORT]);
	return TRUE;
}

gboolean
lrg_dap_server_start_stdio (LrgDapServer  *self,
                            GError       **error)
{
#ifdef G_OS_UNIX
	GInputStream  *in_stream;
	GOutputStream *out_stream;
	DapClient     *client;

	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), FALSE);

	if (self->listening)
	{
		g_set_error_literal (error, LRG_DAP_SERVER_ERROR, LRG_DAP_SERVER_ERROR_FAILED,
		                     "DAP server is already listening");
		return FALSE;
	}
	self->stopping = FALSE;
	in_stream = g_unix_input_stream_new (STDIN_FILENO, FALSE);
	out_stream = g_unix_output_stream_new (STDOUT_FILENO, FALSE);
	self->context = g_main_context_ref_thread_default ();
	self->listening = TRUE;
	self->port = 0;
	client = client_new (self, in_stream, out_stream);
	g_object_unref (in_stream);
	g_object_unref (out_stream);
	self->was_paused = self->debugger != NULL && lrg_script_debugger_is_paused (self->debugger);
	attach_poll (self);
	client_read_header (client);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LISTENING]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORT]);
	return TRUE;
#else
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), FALSE);
	g_set_error_literal (error, LRG_DAP_SERVER_ERROR, LRG_DAP_SERVER_ERROR_FAILED,
	                     "stdio transport is unavailable on this platform");
	return FALSE;
#endif
}

void
lrg_dap_server_stop (LrgDapServer *self)
{
	guint i;

	g_return_if_fail (LRG_IS_DAP_SERVER (self));

	self->stopping = TRUE;
	if (self->poll_id != 0)
	{
		g_source_remove (self->poll_id);
		self->poll_id = 0;
	}
	if (self->service != NULL)
	{
		g_socket_service_stop (self->service);
		g_socket_listener_close (G_SOCKET_LISTENER (self->service));
		g_clear_object (&self->service);
	}
	for (i = 0; i < self->clients->len; i++)
		client_close ((DapClient *) g_ptr_array_index (self->clients, i));
	clients_sweep (self);
	if (self->listening)
	{
		self->listening = FALSE;
		self->port = 0;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LISTENING]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PORT]);
	}
}

gboolean
lrg_dap_server_get_listening (LrgDapServer *self)
{
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), FALSE);
	return self->listening;
}

guint16
lrg_dap_server_get_port (LrgDapServer *self)
{
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), 0);
	return self->port;
}

guint
lrg_dap_server_get_n_clients (LrgDapServer *self)
{
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), 0);
	return self->clients != NULL ? self->clients->len : 0;
}

const gchar *
lrg_dap_server_get_host (LrgDapServer *self)
{
	g_return_val_if_fail (LRG_IS_DAP_SERVER (self), NULL);
	return self->host;
}
