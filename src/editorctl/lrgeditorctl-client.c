/* lrgeditorctl-client.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Talking to running editors. Each call opens its own connection: Unix
 * sockets are cheap, and it means a restarted editor is picked up by the
 * next call without any reconnect logic (the MCP bridge relies on this).
 */

#include "lrgeditorctl-client.h"
#include <gio/gunixsocketaddress.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TIMEOUT_MS (10000)

G_DEFINE_QUARK (lrg-ctl-error-quark, lrg_ctl_error)

void
lrg_ctl_session_free (LrgCtlSession *session)
{
	if (session == NULL)
		return;
	g_free (session->socket);
	g_free (session->project);
	g_free (session->level);
	g_free (session);
}

/* $XDG_RUNTIME_DIR/libregnum, where editors put their sockets. */
gchar *
lrg_ctl_socket_dir (void)
{
	return g_build_filename (g_get_user_runtime_dir (), "libregnum", NULL);
}

/* ==========================================================================
 * Line I/O
 * ========================================================================== */

static GSocketConnection *
connect_to (const gchar  *socket_path,
            guint         timeout_ms,
            GError      **error)
{
	g_autoptr(GSocketClient)  client = g_socket_client_new ();
	g_autoptr(GSocketAddress) addr = g_unix_socket_address_new (socket_path);
	GSocketConnection        *conn;

	g_socket_client_set_timeout (client, (timeout_ms + 999) / 1000);
	conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (addr), NULL, error);
	if (conn == NULL && error != NULL && *error != NULL)
		g_prefix_error (error, "Cannot connect to %s: ", socket_path);
	return conn;
}

static gboolean
send_line (GSocketConnection  *conn,
           const gchar        *line,
           GError            **error)
{
	GOutputStream *out = g_io_stream_get_output_stream (G_IO_STREAM (conn));

	return g_output_stream_write_all (out, line, strlen (line), NULL, NULL, error) &&
	       g_output_stream_write_all (out, "\n", 1, NULL, NULL, error);
}

static gchar *
request_line (const gchar *method,
              JsonObject  *params,
              gint64       id)
{
	g_autoptr(JsonBuilder)   b = json_builder_new ();
	g_autoptr(JsonGenerator) gen = json_generator_new ();
	g_autoptr(JsonNode)      root = NULL;

	json_builder_begin_object (b);
	json_builder_set_member_name (b, "id");
	json_builder_add_int_value (b, id);
	json_builder_set_member_name (b, "method");
	json_builder_add_string_value (b, method);
	json_builder_set_member_name (b, "params");
	if (params != NULL)
	{
		JsonNode *p = json_node_new (JSON_NODE_OBJECT);

		json_node_set_object (p, params);
		json_builder_add_value (b, p);
	}
	else
	{
		json_builder_begin_object (b);
		json_builder_end_object (b);
	}
	json_builder_end_object (b);

	root = json_builder_get_root (b);
	json_generator_set_root (gen, root);
	return json_generator_to_data (gen, NULL);
}

/* Turns a reply object into its result, or an error. */
static JsonNode *
reply_result (JsonObject  *reply,
              const gchar *method,
              GError     **error)
{
	if (json_object_has_member (reply, "error"))
	{
		JsonObject  *e = json_object_get_object_member (reply, "error");
		const gchar *code = (e != NULL && json_object_has_member (e, "code"))
		                    ? json_object_get_string_member (e, "code") : "failed";
		const gchar *msg = (e != NULL && json_object_has_member (e, "message"))
		                   ? json_object_get_string_member (e, "message") : "unknown error";

		g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "%s: %s (%s)", method, msg, code);
		return NULL;
	}
	if (!json_object_has_member (reply, "result"))
		return json_node_new (JSON_NODE_NULL);
	return json_node_copy (json_object_get_member (reply, "result"));
}

JsonNode *
lrg_ctl_call (const gchar  *socket_path,
              const gchar  *method,
              JsonObject   *params,
              guint         timeout_ms,
              GError      **error)
{
	g_autoptr(GSocketConnection) conn = NULL;
	g_autoptr(GDataInputStream)  in = NULL;
	g_autofree gchar            *line = NULL;
	gint64                       deadline;

	g_return_val_if_fail (socket_path != NULL && method != NULL, NULL);

	if (timeout_ms == 0)
		timeout_ms = DEFAULT_TIMEOUT_MS;
	conn = connect_to (socket_path, timeout_ms, error);
	if (conn == NULL)
		return NULL;

	line = request_line (method, params, 1);
	if (!send_line (conn, line, error))
		return NULL;

	/* Read until our reply; anything else (events) is skipped. */
	in = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	g_socket_set_timeout (g_socket_connection_get_socket (conn), (timeout_ms + 999) / 1000);
	deadline = g_get_monotonic_time () + (gint64) timeout_ms * 1000;
	while (g_get_monotonic_time () < deadline)
	{
		g_autofree gchar    *reply = NULL;
		g_autoptr(JsonParser) parser = json_parser_new ();
		JsonObject          *obj;
		gsize                len = 0;

		reply = g_data_input_stream_read_line_utf8 (in, &len, NULL, error);
		if (reply == NULL)
		{
			if (error != NULL && *error == NULL)
				g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_PROTOCOL,
				             "%s: the editor closed the connection", method);
			return NULL;
		}
		if (!json_parser_load_from_data (parser, reply, (gssize) len, NULL) ||
		    !JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
			continue;
		obj = json_node_get_object (json_parser_get_root (parser));
		if (!json_object_has_member (obj, "id") || json_object_has_member (obj, "event"))
			continue;
		return reply_result (obj, method, error);
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "%s: no reply within %u ms", method, timeout_ms);
	return NULL;
}

/* ==========================================================================
 * Sessions
 * ========================================================================== */

static gint
compare_sessions (gconstpointer a,
                  gconstpointer b)
{
	const LrgCtlSession *x = *(const LrgCtlSession * const *) a;
	const LrgCtlSession *y = *(const LrgCtlSession * const *) b;

	/* Newest first. */
	return (x->mtime < y->mtime) - (x->mtime > y->mtime);
}

static const gchar *
member_string (JsonObject  *o,
               const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

/* Probes one socket: a live editor answers "editor.info" quickly. */
static LrgCtlSession *
probe (const gchar *path)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(JsonNode) info = NULL;
	LrgCtlSession      *s;
	GStatBuf            st;
	JsonObject         *o;

	info = lrg_ctl_call (path, "editor.info", NULL, 1500, &error);
	if (info == NULL || !JSON_NODE_HOLDS_OBJECT (info))
		return NULL;
	o = json_node_get_object (info);

	s = g_new0 (LrgCtlSession, 1);
	s->socket = g_strdup (path);
	s->pid = json_object_has_member (o, "pid") ? (gint) json_object_get_int_member (o, "pid") : 0;
	s->project = g_strdup (member_string (o, "project"));
	s->level = g_strdup (member_string (o, "level"));
	s->mtime = (g_stat (path, &st) == 0) ? (gint64) st.st_mtime : 0;
	return s;
}

/* Every live editor in the socket directory (plus $LRG_EDITOR_SOCKET),
 * newest first. Stale sockets are skipped. */
GPtrArray *
lrg_ctl_list_sessions (void)
{
	GPtrArray        *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_ctl_session_free);
	g_autofree gchar *dir = lrg_ctl_socket_dir ();
	g_autoptr(GDir)   d = g_dir_open (dir, 0, NULL);
	const gchar      *env = g_getenv ("LRG_EDITOR_SOCKET");
	const gchar      *name;

	if (env != NULL && *env != '\0')
	{
		LrgCtlSession *s = probe (env);

		if (s != NULL)
			g_ptr_array_add (out, s);
	}

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *path = NULL;
		LrgCtlSession    *s;
		guint             i;
		gboolean          seen = FALSE;

		if (!g_str_has_suffix (name, ".sock"))
			continue;
		path = g_build_filename (dir, name, NULL);
		for (i = 0; i < out->len; i++)
			seen = seen || g_strcmp0 (((LrgCtlSession *) g_ptr_array_index (out, i))->socket, path) == 0;
		if (seen || (s = probe (path)) == NULL)
			continue;
		g_ptr_array_add (out, s);
	}

	g_ptr_array_sort (out, compare_sessions);
	return out;
}

static gboolean
all_digits (const gchar *s)
{
	if (s == NULL || *s == '\0')
		return FALSE;
	for (; *s != '\0'; s++)
		if (!g_ascii_isdigit (*s))
			return FALSE;
	return TRUE;
}

/* Picks the socket for @selector: a socket path, a PID, a substring of
 * the project or level path, or (NULL) $LRG_EDITOR_SOCKET / the newest. */
gchar *
lrg_ctl_resolve_session (const gchar  *selector,
                         GError      **error)
{
	g_autoptr(GPtrArray) sessions = NULL;
	guint                i;

	if (selector != NULL && (strchr (selector, '/') != NULL || g_str_has_suffix (selector, ".sock")))
		return g_strdup (selector);

	sessions = lrg_ctl_list_sessions ();
	if (sessions->len == 0)
	{
		g_autofree gchar *dir = lrg_ctl_socket_dir ();

		g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_NO_SESSION,
		             "No running editor found in %s (start one with `lrgeditor PROJECT` "
		             "or `lrgeditor --headless PROJECT`)", dir);
		return NULL;
	}
	if (selector == NULL || *selector == '\0')
		return g_strdup (((LrgCtlSession *) g_ptr_array_index (sessions, 0))->socket);

	for (i = 0; i < sessions->len; i++)
	{
		LrgCtlSession *s = g_ptr_array_index (sessions, i);

		if ((all_digits (selector) && s->pid == atoi (selector)) ||
		    (!all_digits (selector) &&
		     ((s->project != NULL && strstr (s->project, selector) != NULL) ||
		      (s->level != NULL && strstr (s->level, selector) != NULL))))
			return g_strdup (s->socket);
	}

	g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_NO_SESSION,
	             "No running editor matches '%s' (see `lrgeditorctl sessions`)", selector);
	return NULL;
}

/* An LrgCtlCallFunc: resolves the session on every call. */
JsonNode *
lrg_ctl_call_selected (const gchar  *method,
                       JsonObject   *params,
                       gpointer      selector,
                       GError      **error)
{
	g_autofree gchar *socket_path = lrg_ctl_resolve_session (selector, error);

	if (socket_path == NULL)
		return NULL;
	return lrg_ctl_call (socket_path, method, params, 0, error);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* Subscribes to @events (NULL: everything) and calls @func for each one
 * until it returns FALSE or the editor goes away. */
gboolean
lrg_ctl_follow_events (const gchar          *socket_path,
                       const gchar * const  *events,
                       LrgCtlEventFunc       func,
                       gpointer              user_data,
                       GError              **error)
{
	g_autoptr(GSocketConnection) conn = NULL;
	g_autoptr(GDataInputStream)  in = NULL;
	g_autoptr(JsonObject)        params = json_object_new ();
	g_autofree gchar            *line = NULL;
	JsonArray                   *subs = json_array_new ();
	JsonArray                   *caps = json_array_new ();
	guint                        i;

	conn = connect_to (socket_path, DEFAULT_TIMEOUT_MS, error);
	if (conn == NULL)
		return FALSE;

	if (events == NULL || events[0] == NULL)
		json_array_add_string_element (subs, "*");
	for (i = 0; events != NULL && events[i] != NULL; i++)
		json_array_add_string_element (subs, events[i]);
	json_object_set_string_member (params, "client", "lrgeditorctl");
	json_object_set_array_member (params, "subscribe", subs);
	json_object_set_array_member (params, "capabilities", caps);

	line = request_line ("hello", params, 1);
	if (!send_line (conn, line, error))
		return FALSE;

	in = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (conn)));
	for (;;)
	{
		g_autofree gchar     *msg = NULL;
		g_autoptr(JsonParser) parser = json_parser_new ();
		JsonObject           *obj;
		gsize                 len = 0;

		msg = g_data_input_stream_read_line_utf8 (in, &len, NULL, error);
		if (msg == NULL)
			return error == NULL || *error == NULL;   /* the editor quit */
		if (!json_parser_load_from_data (parser, msg, (gssize) len, NULL) ||
		    !JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
			continue;
		obj = json_node_get_object (json_parser_get_root (parser));
		if (!json_object_has_member (obj, "event"))
			continue;
		if (!func (json_object_get_string_member (obj, "event"),
		           json_object_has_member (obj, "data") ? json_object_get_member (obj, "data") : NULL,
		           user_data))
			return TRUE;
	}
}
