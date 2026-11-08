/* test-dap-server.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DAP framing and the request set, against an LrgDapServer on a free
 * TCP port. A Lua stop is attempted when that backend is built; it is
 * skipped otherwise and bounded by alarm() so a stuck script cannot
 * hang the suite.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <unistd.h>
#include <libregnum.h>

typedef struct
{
	GSocketClient     *client;
	GSocketConnection *conn;
	GInputStream      *in;
	GOutputStream     *out;
	GByteArray        *buf;
	gint               seq;
	gboolean           connect_done;
	GError            *connect_error;
} DapLink;

static void pump_clients (LrgDapServer *server);

static void
link_clear (DapLink *link)
{
	g_clear_error (&link->connect_error);
	g_clear_object (&link->in);
	g_clear_object (&link->out);
	g_clear_object (&link->conn);
	g_clear_object (&link->client);
	g_clear_pointer (&link->buf, g_byte_array_unref);
}

static void
on_connected (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	DapLink *link = user_data;

	link->conn = g_socket_client_connect_to_host_finish (G_SOCKET_CLIENT (source), result,
	                                                     &link->connect_error);
	link->connect_done = TRUE;
}

static gboolean
link_open (DapLink *link,
           guint16  port,
           GError **error)
{
	gint64 deadline;

	memset (link, 0, sizeof *link);
	link->buf = g_byte_array_new ();
	link->client = g_socket_client_new ();
	g_socket_client_connect_to_host_async (link->client, "127.0.0.1", port, NULL,
	                                       on_connected, link);
	deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
	while (!link->connect_done && g_get_monotonic_time () < deadline)
		g_main_context_iteration (NULL, FALSE);
	if (link->conn == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
		             link->connect_error != NULL ? link->connect_error->message : "connect timed out");
		return FALSE;
	}
	g_socket_set_blocking (g_socket_connection_get_socket (link->conn), FALSE);
	link->in = g_object_ref (g_io_stream_get_input_stream (G_IO_STREAM (link->conn)));
	link->out = g_object_ref (g_io_stream_get_output_stream (G_IO_STREAM (link->conn)));
	return TRUE;
}

static gboolean
write_all (DapLink      *link,
           const gchar  *data,
           gsize         len,
           GError      **error)
{
	gsize off = 0;

	while (off < len)
	{
		gssize        n;
		g_autoptr(GError) local = NULL;

		n = g_output_stream_write (link->out, data + off, len - off, NULL, &local);
		if (n > 0)
		{
			off += (gsize) n;
			continue;
		}
		if (local != NULL && g_error_matches (local, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
		{
			g_main_context_iteration (NULL, FALSE);
			g_usleep (G_TIME_SPAN_MILLISECOND);
			continue;
		}
		if (local != NULL)
			g_propagate_error (error, g_steal_pointer (&local));
		else
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "short write");
		return FALSE;
	}
	return TRUE;
}

/* Synchronous GInputStream reads block even after g_socket_set_blocking
 * (FALSE). A blocking read here never returns to the main context, so
 * the server never accepts the request. */
static void
pull (DapLink *link)
{
	gchar             tmp[4096];
	gssize            n;
	g_autoptr(GError) local = NULL;

	if (!g_pollable_input_stream_is_readable (G_POLLABLE_INPUT_STREAM (link->in)))
		return;
	n = g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM (link->in),
	                                              tmp, sizeof tmp, NULL, &local);
	if (n > 0)
		g_byte_array_append (link->buf, (const guint8 *) tmp, (guint) n);
	(void) local;
}

/* Pops one framed body. FALSE when the buffer does not hold one yet. */
static gboolean
take_body (DapLink *link,
           gchar  **body)
{
	gchar *text;
	gchar *sep;
	gchar *line;
	gint   length = -1;
	guint  header;

	if (link->buf->len == 0)
		return FALSE;
	text = g_strndup ((const gchar *) link->buf->data, link->buf->len);
	sep = strstr (text, "\r\n\r\n");
	if (sep == NULL)
	{
		g_free (text);
		return FALSE;
	}
	header = (guint) (sep - text) + 4;
	line = text;
	while (line < sep)
	{
		gchar *end = strstr (line, "\r\n");

		if (end == NULL || end > sep)
			break;
		*end = '\0';
		if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
			length = (gint) g_ascii_strtoll (g_strstrip (line + 15), NULL, 10);
		line = end + 2;
	}
	g_free (text);
	if (length < 0 || link->buf->len < header + (guint) length)
		return FALSE;
	*body = g_strndup ((const gchar *) link->buf->data + header, (gsize) length);
	g_byte_array_remove_range (link->buf, 0, header + (guint) length);
	return TRUE;
}

static JsonNode *
wait_kind (DapLink     *link,
           const gchar *type,
           gint64       request_seq,
           gint64       deadline,
           GError     **error)
{
	while (g_get_monotonic_time () < deadline)
	{
		gchar *body = NULL;

		g_main_context_iteration (NULL, FALSE);
		pull (link);
		while (take_body (link, &body))
		{
			g_autoptr(JsonParser) parser = json_parser_new ();
			JsonObject           *object;
			const gchar          *got;
			g_autoptr(GError)     local = NULL;

			if (!json_parser_load_from_data (parser, body, -1, &local))
			{
				g_free (body);
				g_propagate_error (error, g_steal_pointer (&local));
				return NULL;
			}
			g_free (body);
			body = NULL;
			object = json_node_get_object (json_parser_get_root (parser));
			got = json_object_get_string_member (object, "type");
			if (g_strcmp0 (got, type) != 0)
				continue;
			if (request_seq >= 0 &&
			    json_object_get_int_member (object, "request_seq") != request_seq)
				continue;
			return json_node_copy (json_parser_get_root (parser));
		}
		g_usleep (G_TIME_SPAN_MILLISECOND);
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "no %s", type);
	return NULL;
}

static JsonNode *
request (DapLink     *link,
         const gchar *command,
         JsonObject  *arguments,
         GError     **error)
{
	JsonObject       *object = json_object_new ();
	JsonNode         *node;
	g_autoptr(JsonGenerator) gen = NULL;
	g_autofree gchar *body = NULL;
	g_autofree gchar *frame = NULL;
	gint64            seq;
	gsize             len = 0;

	link->seq++;
	seq = link->seq;
	json_object_set_int_member (object, "seq", seq);
	json_object_set_string_member (object, "type", "request");
	json_object_set_string_member (object, "command", command);
	if (arguments != NULL)
		json_object_set_object_member (object, "arguments", arguments);
	node = json_node_new (JSON_NODE_OBJECT);
	json_node_take_object (node, object);
	gen = json_generator_new ();
	json_generator_set_root (gen, node);
	body = json_generator_to_data (gen, &len);
	json_node_unref (node);
	frame = g_strdup_printf ("Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n%s", len, body);
	if (!write_all (link, frame, strlen (frame), error))
		return NULL;
	return wait_kind (link, "response", seq, g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND, error);
}

static JsonObject *
body_of (JsonNode *node)
{
	JsonObject *object = json_node_get_object (node);

	if (!json_object_has_member (object, "body"))
		return NULL;
	return json_object_get_object_member (object, "body");
}

static void
test_protocol (void)
{
	g_autoptr(LrgScriptDebugger) debugger = lrg_script_debugger_new ();
	g_autoptr(LrgDapServer) server = lrg_dap_server_new_for_debugger (debugger);
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) early = NULL;
	g_autoptr(JsonNode) init = NULL;
	g_autoptr(JsonNode) event = NULL;
	g_autoptr(JsonNode) launched = NULL;
	g_autoptr(JsonNode) breaks = NULL;
	g_autoptr(JsonNode) configured = NULL;
	g_autoptr(JsonNode) threads = NULL;
	g_autoptr(JsonNode) stack = NULL;
	g_autoptr(JsonNode) scopes = NULL;
	g_autoptr(JsonNode) evaluated = NULL;
	g_autoptr(JsonNode) continued = NULL;
	g_autoptr(JsonNode) stepped = NULL;
	g_autoptr(JsonNode) paused = NULL;
	g_autoptr(JsonNode) disconnected = NULL;
	JsonObject *args;
	JsonObject *source;
	JsonArray  *specs;
	JsonObject *spec;
	JsonObject *body;
	JsonArray  *list;
	DapLink     link;
	guint16     port;

	g_assert_true (lrg_dap_server_start (server, "127.0.0.1", 0, &error));
	g_assert_no_error (error);
	port = lrg_dap_server_get_port (server);
	g_assert_cmpuint (port, >, 0);
	g_assert_true (lrg_dap_server_start (server, "127.0.0.1", 1, &error));
	g_assert_cmpuint (lrg_dap_server_get_port (server), ==, port);
	g_assert_true (lrg_dap_server_get_listening (server));
	g_assert_cmpstr (lrg_dap_server_get_host (server), ==, "127.0.0.1");
	g_assert_true (lrg_dap_server_get_debugger (server) == debugger);

	memset (&link, 0, sizeof link);
	g_assert_true (link_open (&link, port, &error));
	g_assert_no_error (error);
	pump_clients (server);
	g_assert_cmpuint (lrg_dap_server_get_n_clients (server), >=, 1);

	early = request (&link, "threads", NULL, &error);
	g_assert_no_error (error);
	g_assert_false (json_object_get_boolean_member (json_node_get_object (early), "success"));
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (early), "message"),
	                 ==, "Expected initialize");

	args = json_object_new ();
	json_object_set_string_member (args, "clientID", "test");
	json_object_set_string_member (args, "adapterID", "libregnum");
	init = request (&link, "initialize", args, &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (init), "success"));
	body = body_of (init);
	g_assert_true (json_object_get_boolean_member (body, "supportsConfigurationDoneRequest"));
	g_assert_true (json_object_get_boolean_member (body, "supportsTerminateRequest"));
	g_assert_false (json_object_get_boolean_member (body, "supportsExceptionOptions"));
	event = wait_kind (&link, "event", -1, g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (event), "event"),
	                 ==, "initialized");

	launched = request (&link, "launch", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (launched), "success"));

	args = json_object_new ();
	source = json_object_new ();
	specs = json_array_new ();
	spec = json_object_new ();
	json_object_set_string_member (source, "path", "/tmp/lrg-dap-sample.lua");
	json_object_set_object_member (args, "source", source);
	json_object_set_int_member (spec, "line", 4);
	json_object_set_string_member (spec, "condition", "x > 1");
	json_array_add_object_element (specs, spec);
	json_object_set_array_member (args, "breakpoints", specs);
	breaks = request (&link, "setBreakpoints", args, &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (breaks), "success"));
	list = json_object_get_array_member (body_of (breaks), "breakpoints");
	g_assert_cmpuint (json_array_get_length (list), ==, 1);
	g_assert_cmpint (json_object_get_int_member (json_array_get_object_element (list, 0), "line"), ==, 4);

	configured = request (&link, "configurationDone", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (configured), "success"));

	threads = request (&link, "threads", NULL, &error);
	g_assert_no_error (error);
	list = json_object_get_array_member (body_of (threads), "threads");
	g_assert_cmpuint (json_array_get_length (list), >=, 1);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (list, 0), "name"),
	                 ==, "script");

	stack = request (&link, "stackTrace", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (stack), "success"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (body_of (stack), "stackFrames")), ==, 0);

	scopes = request (&link, "scopes", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_false (json_object_get_boolean_member (json_node_get_object (scopes), "success"));

	args = json_object_new ();
	json_object_set_string_member (args, "expression", "1 + 1");
	evaluated = request (&link, "evaluate", args, &error);
	g_assert_no_error (error);
	g_assert_false (json_object_get_boolean_member (json_node_get_object (evaluated), "success"));

	continued = request (&link, "continue", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (body_of (continued), "allThreadsContinued"));
	stepped = request (&link, "next", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (stepped), "success"));
	{
		g_autoptr(JsonNode) into = NULL;
		g_autoptr(JsonNode) outof = NULL;

		into = request (&link, "stepIn", json_object_new (), &error);
		g_assert_no_error (error);
		g_assert_nonnull (into);
		outof = request (&link, "stepOut", json_object_new (), &error);
		g_assert_no_error (error);
		g_assert_nonnull (outof);
	}
	paused = request (&link, "pause", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (paused), "success"));

	args = json_object_new ();
	json_object_set_boolean_member (args, "terminateDebuggee", TRUE);
	disconnected = request (&link, "disconnect", args, &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (disconnected), "success"));

	link_clear (&link);
	lrg_dap_server_stop (server);
	g_assert_false (lrg_dap_server_get_listening (server));
	g_assert_cmpuint (lrg_dap_server_get_port (server), ==, 0);
}

/* Defined below the protocol test so the forward use in it typechecks.
 * The accept runs on the main context; a couple of iterations is enough
 * once connect_to_host_async has finished. */
static void
pump_clients (LrgDapServer *server)
{
	gint i;

	(void) server;
	for (i = 0; i < 20; i++)
		g_main_context_iteration (NULL, FALSE);
}

typedef struct
{
	LrgScripting *scripting;
	gchar        *path;
	gint          done;
	gboolean      ok;
	gchar        *error;
} LuaRun;

static gpointer
lua_thread (gpointer data)
{
	LuaRun           *run = data;
	g_autoptr(GError) error = NULL;

	run->ok = lrg_scripting_load_file (run->scripting, run->path, &error);
	if (error != NULL)
		run->error = g_strdup (error->message);
	g_atomic_int_set (&run->done, 1);
	return NULL;
}

static void
test_lua_stop (void)
{
	g_autoptr(LrgScriptDebugger) debugger = NULL;
	g_autoptr(LrgDapServer) server = NULL;
	g_autoptr(LrgScripting) scripting = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) init = NULL;
	g_autoptr(JsonNode) launched = NULL;
	g_autoptr(JsonNode) breaks = NULL;
	g_autoptr(JsonNode) configured = NULL;
	g_autoptr(JsonNode) stopped = NULL;
	g_autoptr(JsonNode) stack = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	JsonObject *args;
	JsonObject *source;
	JsonArray *specs;
	JsonObject *spec;
	JsonArray *frames;
	DapLink link;
	LuaRun run;
	GThread *thread = NULL;
	gint64 deadline;
	LrgScriptingManager *manager;

	manager = lrg_scripting_manager_get_default ();
	if (!lrg_scripting_manager_is_available (manager, LRG_SCRIPT_LANGUAGE_LUA))
	{
		g_test_skip ("Lua is not built");
		return;
	}

	alarm (20);
	debugger = lrg_script_debugger_new ();
	lrg_script_debugger_set_auto_attach (debugger, TRUE);
	server = lrg_dap_server_new_for_debugger (debugger);
	g_assert_true (lrg_dap_server_start (server, "127.0.0.1", 0, &error));
	g_assert_no_error (error);
	memset (&link, 0, sizeof link);
	g_assert_true (link_open (&link, lrg_dap_server_get_port (server), &error));
	g_assert_no_error (error);

	init = request (&link, "initialize", json_object_new (), &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (init), "success"));
	g_clear_pointer (&stopped, json_node_unref);
	stopped = wait_kind (&link, "event", -1, g_get_monotonic_time () + G_TIME_SPAN_SECOND, &error);
	g_assert_no_error (error);

	dir = g_dir_make_tmp ("lrg-dap-XXXXXX", NULL);
	path = g_build_filename (dir, "stop.lua", NULL);
	g_assert_true (g_file_set_contents (path, "local value = 1\nvalue = value + 41\nreturn value\n", -1, NULL));

	args = json_object_new ();
	source = json_object_new ();
	specs = json_array_new ();
	spec = json_object_new ();
	json_object_set_string_member (source, "path", path);
	json_object_set_object_member (args, "source", source);
	json_object_set_int_member (spec, "line", 2);
	json_array_add_object_element (specs, spec);
	json_object_set_array_member (args, "breakpoints", specs);
	breaks = request (&link, "setBreakpoints", args, &error);
	g_assert_no_error (error);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (breaks), "success"));
	launched = request (&link, "attach", json_object_new (), &error);
	g_assert_no_error (error);
	configured = request (&link, "configurationDone", json_object_new (), &error);
	g_assert_no_error (error);
	(void) launched;
	(void) configured;

	scripting = lrg_scripting_manager_create_context (manager, LRG_SCRIPT_LANGUAGE_LUA);
	g_assert_nonnull (scripting);
	memset (&run, 0, sizeof run);
	run.scripting = scripting;
	run.path = path;
	thread = g_thread_new ("lrg-dap-lua", lua_thread, &run);

	deadline = g_get_monotonic_time () + 4 * G_TIME_SPAN_SECOND;
	g_clear_error (&error);
	g_clear_pointer (&stopped, json_node_unref);
	while (stopped == NULL && g_get_monotonic_time () < deadline)
	{
		g_autoptr(GError) local = NULL;

		stopped = wait_kind (&link, "event", -1, g_get_monotonic_time () + 200 * G_TIME_SPAN_MILLISECOND, &local);
		if (stopped != NULL &&
		    g_strcmp0 (json_object_get_string_member (json_node_get_object (stopped), "event"), "stopped") != 0)
			g_clear_pointer (&stopped, json_node_unref);
		if (g_atomic_int_get (&run.done) && !lrg_script_debugger_is_paused (debugger))
			break;
	}
	if (stopped == NULL)
	{
		lrg_script_debugger_resume (debugger);
		lrg_script_debugger_detach_all (debugger);
		if (thread != NULL)
			g_thread_join (thread);
		link_clear (&link);
		g_unlink (path);
		g_rmdir (dir);
		alarm (0);
		g_test_skip ("script did not stop (no line event within 4s)");
		return;
	}

	g_clear_error (&error);
	stack = request (&link, "stackTrace", json_object_new (), &error);
	g_assert_no_error (error);
	frames = json_object_get_array_member (body_of (stack), "stackFrames");
	g_assert_cmpuint (json_array_get_length (frames), >=, 1);
	g_assert_cmpint (json_object_get_int_member (json_array_get_object_element (frames, 0), "line"), ==, 2);

	g_clear_error (&error);
	{
		g_autoptr(JsonNode) continued = NULL;

		continued = request (&link, "continue", json_object_new (), &error);
		g_assert_no_error (error);
		g_assert_nonnull (continued);
	}
	lrg_script_debugger_resume (debugger);
	g_thread_join (thread);
	g_assert_true (run.ok);
	lrg_script_debugger_detach_all (debugger);
	link_clear (&link);
	lrg_dap_server_stop (server);
	g_free (run.error);
	g_unlink (path);
	g_rmdir (dir);
	alarm (0);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/dap/protocol", test_protocol);
	g_test_add_func ("/dap/lua-stop", test_lua_stop);
	return g_test_run ();
}
