/* test-editor-server.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgEditorServer: the JSON-lines IPC protocol, both in-process
 * (lrg_editor_server_dispatch) and over a real Unix socket, including
 * events, Emacs open-file routing, log streaming and socket lifecycle.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <libregnum.h>

/* ==========================================================================
 * Fixture
 * ========================================================================== */

typedef struct
{
	LrgEditor       *editor;
	LrgEditorServer *server;
	gchar           *dir;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	f->editor = lrg_editor_new ();
	f->server = lrg_editor_server_new (f->editor);
	f->dir = g_dir_make_tmp ("lrg-server-XXXXXX", NULL);
	lrg_editor_server_set_project_dir (f->server, f->dir);
}

static void
remove_tree (const gchar *dir)
{
	g_autoptr(GDir) d = g_dir_open (dir, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *p = g_build_filename (dir, name, NULL);

		if (g_file_test (p, G_FILE_TEST_IS_DIR))
			remove_tree (p);
		else
			g_unlink (p);
	}
	g_rmdir (dir);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	lrg_editor_server_stop (f->server);
	g_clear_object (&f->server);
	g_clear_object (&f->editor);
	remove_tree (f->dir);
	g_free (f->dir);
}

/* Dispatches @request and returns the parsed response object. */
static JsonNode *
call (Fixture     *f,
      const gchar *request)
{
	g_autofree gchar     *line = lrg_editor_server_dispatch (f->server, request);
	g_autoptr(JsonParser) parser = json_parser_new ();

	g_assert_nonnull (line);
	g_assert_null (strchr (line, '\n'));
	g_assert_true (json_parser_load_from_data (parser, line, -1, NULL));
	return json_node_copy (json_parser_get_root (parser));
}

/* Returns the "result" member, asserting there was no error. */
static JsonNode *
result_of (JsonNode *resp)
{
	JsonObject *obj = json_node_get_object (resp);

	if (json_object_has_member (obj, "error"))
		g_error ("unexpected error: %s",
		         json_object_get_string_member (json_object_get_object_member (obj, "error"), "message"));
	return json_object_get_member (obj, "result");
}

static const gchar *
error_code_of (JsonNode *resp)
{
	JsonObject *obj = json_node_get_object (resp);

	g_assert_true (json_object_has_member (obj, "error"));
	return json_object_get_string_member (json_object_get_object_member (obj, "error"), "code");
}

/* ==========================================================================
 * In-process protocol
 * ========================================================================== */

static void
test_protocol_basics (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *none = NULL;

	r = call (f, "{\"id\":1,\"method\":\"ping\"}");
	g_assert_cmpstr (json_node_get_string (result_of (r)), ==, "pong");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "id"), ==, 1);
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":\"abc\",\"method\":\"hello\",\"params\":{\"client\":\"test\"}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result_of (r)), "server"),
	                 ==, "libregnum-editor");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "id"), ==, "abc");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":2,\"method\":\"nope\"}");
	g_assert_cmpstr (error_code_of (r), ==, "method-not-found");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{not json");
	g_assert_cmpstr (error_code_of (r), ==, "parse");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "[1,2]");
	g_assert_cmpstr (error_code_of (r), ==, "invalid-request");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":3,\"method\":\"node.get\",\"params\":5}");
	g_assert_cmpstr (error_code_of (r), ==, "invalid-params");
	g_clear_pointer (&r, json_node_unref);

	/* Notifications (no id) produce no response. */
	none = lrg_editor_server_dispatch (f->server, "{\"method\":\"ping\"}");
	g_assert_null (none);

	r = call (f, "{\"id\":4,\"method\":\"methods\"}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (result_of (r))), >, 30);
}

static void
test_node_methods (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	JsonObject         *node;
	LrgNode            *house, *door;
	g_autofree gchar   *req = NULL;

	r = call (f, "{\"id\":1,\"method\":\"node.create\",\"params\":{\"name\":\"House\","
	             "\"primitive\":\"cube\",\"location\":[1,2,3],\"rotation_degrees\":[0,90,0]}}");
	node = json_node_get_object (result_of (r));
	g_assert_cmpstr (json_object_get_string_member (node, "name"), ==, "House");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (node, "visual"),
	                                                "primitive"), ==, "cube");
	house = lrg_editor_find_node (f->editor, "House");
	g_assert_nonnull (house);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_location (house)->y, 2.0f, 1e-5);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_rotation (house)->y, G_PI_2, 1e-5);
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":2,\"method\":\"node.create\",\"params\":{\"name\":\"Door\",\"parent\":\"House\","
	             "\"kind\":\"light\"}}");
	door = lrg_editor_find_node (f->editor, "House/Door");
	g_assert_nonnull (door);
	g_clear_pointer (&r, json_node_unref);

	/* node.set is one undo step for all fields. */
	req = g_strdup_printf ("{\"id\":3,\"method\":\"node.set\",\"params\":{\"node\":\"%s\","
	                       "\"name\":\"Door2\",\"scale\":[2,2,2],\"visual\":{\"color\":[1,0,0]}}}",
	                       lrg_node_get_guid (door));
	r = call (f, req);
	result_of (r);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpstr (lrg_node_get_name (door), ==, "Door2");
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, 3);
	{
		gfloat cr, cg, cb, ca;

		lrg_material3d_get_color (lrg_node_visual_get_material (lrg_node_get_visual (door)),
		                          &cr, &cg, &cb, &ca);
		g_assert_cmpfloat (cr, ==, 1.0f);
		g_assert_cmpfloat (cg, ==, 0.0f);
	}

	/* A bad field rejects the whole request and changes nothing. */
	r = call (f, "{\"id\":4,\"method\":\"node.set\",\"params\":{\"node\":\"House\",\"name\":\"X\","
	             "\"location\":[1,2]}}");
	g_assert_cmpstr (error_code_of (r), ==, "invalid-params");
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpstr (lrg_node_get_name (house), ==, "House");
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, 3);

	r = call (f, "{\"id\":5,\"method\":\"node.get\",\"params\":{\"node\":\"Missing\"}}");
	g_assert_cmpstr (error_code_of (r), ==, "not-found");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":6,\"method\":\"node.tree\"}");
	{
		JsonArray  *top = json_node_get_array (result_of (r));
		JsonObject *h = json_array_get_object_element (top, 0);

		g_assert_cmpuint (json_array_get_length (top), ==, 1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (h, "nodes")), ==, 1);
	}
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":7,\"method\":\"node.list\"}");
	{
		JsonArray  *list = json_node_get_array (result_of (r));
		JsonObject *d = json_array_get_object_element (list, 1);

		g_assert_cmpuint (json_array_get_length (list), ==, 2);
		g_assert_cmpstr (json_object_get_string_member (d, "path"), ==, "House/Door2");
		g_assert_cmpint (json_object_get_int_member (d, "depth"), ==, 1);
		g_assert_cmpstr (json_object_get_string_member (d, "parent"), ==, lrg_node_get_guid (house));
	}
	g_clear_pointer (&r, json_node_unref);

	/* Move out keeping world placement, then delete via the selection. */
	r = call (f, "{\"id\":8,\"method\":\"node.move\",\"params\":{\"node\":\"House/Door2\"}}");
	g_assert_true (json_node_get_boolean (result_of (r)));
	g_clear_pointer (&r, json_node_unref);
	g_assert_null (lrg_node_get_parent (lrg_node_get_parent (door)));

	r = call (f, "{\"id\":9,\"method\":\"node.move\",\"params\":{\"node\":\"House\",\"parent\":\"House\"}}");
	g_assert_cmpstr (error_code_of (r), ==, "invalid-params");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":10,\"method\":\"node.duplicate\",\"params\":{\"node\":\"House\"}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (result_of (r))), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (lrg_editor_find_node (f->editor, "House2"));

	r = call (f, "{\"id\":11,\"method\":\"node.delete\"}");   /* the selection (House2) */
	g_assert_true (json_node_get_boolean (result_of (r)));
	g_clear_pointer (&r, json_node_unref);
	g_assert_null (lrg_editor_find_node (f->editor, "House2"));

	r = call (f, "{\"id\":12,\"method\":\"node.find\",\"params\":{\"query\":\"door\"}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (result_of (r))), ==, 1);
}

static void
test_component_script_methods (Fixture       *f,
                               gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	LrgNode            *n;
	LrgComponentDesc   *desc;
	LrgScriptBinding   *script;

	r = call (f, "{\"id\":1,\"method\":\"node.create\",\"params\":{\"name\":\"N\"}}");
	g_clear_pointer (&r, json_node_unref);
	n = lrg_editor_find_node (f->editor, "N");

	r = call (f, "{\"id\":2,\"method\":\"component.add\",\"params\":{\"node\":\"N\",\"type\":\"health\","
	             "\"props\":{\"max\":10,\"regen\":0.5,\"boss\":true,\"tag\":\"x\"}}}");
	result_of (r);
	g_clear_pointer (&r, json_node_unref);
	desc = g_ptr_array_index (lrg_node_get_components (n), 0);
	g_assert_cmpint (g_value_get_int64 (lrg_component_desc_get_value (desc, "max")), ==, 10);
	g_assert_cmpfloat (g_value_get_double (lrg_component_desc_get_value (desc, "regen")), ==, 0.5);

	r = call (f, "{\"id\":3,\"method\":\"component.set\",\"params\":{\"node\":\"N\",\"key\":\"max\",\"value\":20}}");
	g_clear_pointer (&r, json_node_unref);
	desc = g_ptr_array_index (lrg_node_get_components (n), 0);
	g_assert_cmpint (g_value_get_int64 (lrg_component_desc_get_value (desc, "max")), ==, 20);

	r = call (f, "{\"id\":4,\"method\":\"component.set\",\"params\":{\"node\":\"N\",\"key\":\"tag\",\"value\":null}}");
	g_clear_pointer (&r, json_node_unref);
	desc = g_ptr_array_index (lrg_node_get_components (n), 0);
	g_assert_false (lrg_component_desc_has (desc, "tag"));

	r = call (f, "{\"id\":5,\"method\":\"node.get\",\"params\":{\"node\":\"N\"}}");
	{
		JsonObject *o = json_node_get_object (result_of (r));
		JsonObject *c = json_array_get_object_element (json_object_get_array_member (o, "components"), 0);

		g_assert_cmpstr (json_object_get_string_member (c, "type"), ==, "health");
		g_assert_true (json_object_get_boolean_member (json_object_get_object_member (c, "props"), "boss"));
	}
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":6,\"method\":\"script.add\",\"params\":{\"node\":\"N\",\"script\":\"ai.py\"}}");
	g_clear_pointer (&r, json_node_unref);
	script = g_ptr_array_index (lrg_node_get_scripts (n), 0);
	g_assert_cmpint (lrg_script_binding_get_language (script), ==, LRG_SCRIPT_LANGUAGE_PYTHON);

	r = call (f, "{\"id\":7,\"method\":\"script.add\",\"params\":{\"node\":\"N\",\"script\":\"a\",\"language\":\"klingon\"}}");
	g_assert_cmpstr (error_code_of (r), ==, "invalid-params");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":8,\"method\":\"script.remove\",\"params\":{\"node\":\"N\",\"index\":3}}");
	g_assert_cmpstr (error_code_of (r), ==, "not-found");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":9,\"method\":\"component.remove\",\"params\":{\"node\":\"N\",\"index\":0}}");
	result_of (r);
	g_assert_cmpuint (lrg_node_get_components (n)->len, ==, 0);
}

static void
test_edit_and_level_methods (Fixture       *f,
                             gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *path = g_build_filename (f->dir, "a.rlevel", NULL);
	g_autofree gchar   *req = NULL;
	g_autofree gchar   *clip = NULL;

	r = call (f, "{\"id\":1,\"method\":\"node.create\",\"params\":{\"name\":\"A\"}}");
	g_clear_pointer (&r, json_node_unref);
	r = call (f, "{\"id\":2,\"method\":\"node.create\",\"params\":{\"name\":\"B\"}}");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":3,\"method\":\"edit.history\"}");
	{
		JsonObject *h = json_node_get_object (result_of (r));

		g_assert_cmpint (json_object_get_int_member (h, "current"), ==, 2);
		g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (h, "steps"), 1),
		                 ==, "Add B");
	}
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":4,\"method\":\"edit.undo\"}");
	g_assert_true (json_node_get_boolean (result_of (r)));
	g_clear_pointer (&r, json_node_unref);
	g_assert_null (lrg_editor_find_node (f->editor, "B"));

	r = call (f, "{\"id\":5,\"method\":\"edit.jump\",\"params\":{\"index\":2}}");
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (lrg_editor_find_node (f->editor, "B"));

	r = call (f, "{\"id\":6,\"method\":\"selection.set\",\"params\":{\"nodes\":[\"A\"]}}");
	g_clear_pointer (&r, json_node_unref);
	r = call (f, "{\"id\":7,\"method\":\"edit.copy\"}");
	clip = g_strdup (json_node_get_string (result_of (r)));
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (strstr (clip, "A"));
	r = call (f, "{\"id\":8,\"method\":\"edit.paste\",\"params\":{\"parent\":\"B\"}}");
	g_assert_cmpuint (json_array_get_length (json_node_get_array (result_of (r))), ==, 1);
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (lrg_editor_find_node (f->editor, "B/A"));

	r = call (f, "{\"id\":9,\"method\":\"selection.set\",\"params\":{\"nodes\":[\"nope\"]}}");
	g_assert_cmpstr (error_code_of (r), ==, "not-found");
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":10,\"method\":\"tool.set\",\"params\":{\"tool\":\"rotate\",\"space\":\"local\",\"snap\":true}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (result_of (r)), "tool"), ==, "rotate");
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpint (lrg_editor_get_space (f->editor), ==, LRG_EDITOR_SPACE_LOCAL);
	g_assert_true (lrg_editor_get_snap_enabled (f->editor));

	/* Save with a relative path (resolved against the project). */
	r = call (f, "{\"id\":11,\"method\":\"level.save\",\"params\":{\"path\":\"a.rlevel\"}}");
	g_assert_cmpstr (json_node_get_string (result_of (r)), ==, path);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

	r = call (f, "{\"id\":12,\"method\":\"node.create\",\"params\":{\"name\":\"C\"}}");
	g_clear_pointer (&r, json_node_unref);

	/* Opening over unsaved changes needs force. */
	req = g_strdup_printf ("{\"id\":13,\"method\":\"level.open\",\"params\":{\"path\":\"%s\"}}", path);
	r = call (f, req);
	g_assert_cmpstr (error_code_of (r), ==, "failed");
	g_clear_pointer (&r, json_node_unref);
	g_free (req);
	req = g_strdup_printf ("{\"id\":14,\"method\":\"level.open\",\"params\":{\"path\":\"%s\",\"force\":true}}", path);
	r = call (f, req);
	result_of (r);
	g_clear_pointer (&r, json_node_unref);
	g_assert_null (lrg_editor_find_node (f->editor, "C"));

	r = call (f, "{\"id\":15,\"method\":\"editor.info\"}");
	{
		JsonObject *info = json_node_get_object (result_of (r));

		g_assert_false (json_object_get_boolean_member (info, "dirty"));
		g_assert_cmpstr (json_object_get_string_member (info, "level"), ==, path);
		g_assert_cmpint (json_object_get_int_member (info, "nodes"), ==, 3);
	}
	g_clear_pointer (&r, json_node_unref);

	r = call (f, "{\"id\":16,\"method\":\"level.serialize\"}");
	g_assert_nonnull (strstr (json_node_get_string (result_of (r)), "type: level"));
}

/* ==========================================================================
 * Socket transport
 * ========================================================================== */

typedef struct
{
	GSocketConnection *conn;
	GDataInputStream  *in;
	GOutputStream     *out;
	GQueue             lines;     /* gchar* */
	gboolean           reading;
	gboolean           eof;
} TestClient;

static void
on_test_line (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	TestClient *c = user_data;
	gchar      *line = g_data_input_stream_read_line_finish_utf8 (c->in, result, NULL, NULL);

	c->reading = FALSE;
	if (line == NULL)
		c->eof = TRUE;
	else
		g_queue_push_tail (&c->lines, line);
}

static TestClient *
test_client_connect (const gchar *path)
{
	g_autoptr(GSocketClient)  client = g_socket_client_new ();
	g_autoptr(GSocketAddress) addr = g_unix_socket_address_new (path);
	TestClient               *c = g_new0 (TestClient, 1);

	c->conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (addr), NULL, NULL);
	g_assert_nonnull (c->conn);
	c->in = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (c->conn)));
	c->out = g_io_stream_get_output_stream (G_IO_STREAM (c->conn));
	g_queue_init (&c->lines);
	return c;
}

static void
test_client_free (TestClient *c)
{
	g_io_stream_close (G_IO_STREAM (c->conn), NULL, NULL);
	/* Let a pending read observe the close. */
	while (c->reading)
		g_main_context_iteration (NULL, TRUE);
	g_object_unref (c->in);
	g_object_unref (c->conn);
	g_queue_clear_full (&c->lines, g_free);
	g_free (c);
}

/* Returns the next line, pumping the main loop (server + client). */
static gchar *
test_client_next (TestClient *c)
{
	gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

	while (g_queue_is_empty (&c->lines) && !c->eof)
	{
		if (!c->reading)
		{
			c->reading = TRUE;
			g_data_input_stream_read_line_async (c->in, G_PRIORITY_DEFAULT, NULL, on_test_line, c);
		}
		g_main_context_iteration (NULL, TRUE);
		g_assert_cmpint (g_get_monotonic_time (), <, deadline);
	}

	return g_queue_pop_head (&c->lines);
}

static void
test_client_write (TestClient  *c,
                   const gchar *line)
{
	g_autofree gchar *msg = g_strconcat (line, "\n", NULL);

	g_assert_true (g_output_stream_write_all (c->out, msg, strlen (msg), NULL, NULL, NULL));
}

/* Sends a request and returns the response, queueing events in @events. */
static JsonNode *
test_client_call (TestClient  *c,
                  const gchar *request,
                  GPtrArray   *events)
{
	test_client_write (c, request);

	for (;;)
	{
		g_autofree gchar     *line = test_client_next (c);
		g_autoptr(JsonParser) p = json_parser_new ();
		JsonObject           *obj;

		g_assert_nonnull (line);
		g_assert_true (json_parser_load_from_data (p, line, -1, NULL));
		obj = json_node_get_object (json_parser_get_root (p));
		if (json_object_has_member (obj, "event"))
		{
			if (events != NULL)
				g_ptr_array_add (events, g_strdup (json_object_get_string_member (obj, "event")));
			continue;
		}
		return json_node_copy (json_parser_get_root (p));
	}
}

static void
pump (guint ms)
{
	gint64 deadline = g_get_monotonic_time () + ms * 1000;

	while (g_get_monotonic_time () < deadline)
		g_main_context_iteration (NULL, FALSE);
}

static void
test_socket_roundtrip_and_events (Fixture       *f,
                                  gconstpointer  data)
{
	g_autofree gchar    *path = g_build_filename (f->dir, "run", "editor.sock", NULL);
	g_autoptr(GError)    error = NULL;
	g_autoptr(GPtrArray) events = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(JsonNode)  r = NULL;
	TestClient          *c;
	gboolean             saw_selection = FALSE;
	gboolean             saw_changed = FALSE;
	guint                i;

	g_assert_true (lrg_editor_server_start (f->server, path, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_server_get_socket_path (f->server), ==, path);

	c = test_client_connect (path);
	r = test_client_call (c, "{\"id\":1,\"method\":\"hello\",\"params\":{\"client\":\"t\"}}", events);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (result_of (r)), "protocol"), ==,
	                 LRG_EDITOR_SERVER_PROTOCOL_VERSION);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (lrg_editor_server_get_n_clients (f->server), ==, 1);

	/* Two requests in one write are both answered, in order. */
	test_client_write (c, "{\"id\":10,\"method\":\"ping\"}\n{\"id\":11,\"method\":\"ping\"}");
	{
		g_autofree gchar *a = test_client_next (c);
		g_autofree gchar *b = test_client_next (c);

		g_assert_nonnull (strstr (a, "\"id\":10"));
		g_assert_nonnull (strstr (b, "\"id\":11"));
	}

	/* Edits made elsewhere (here: in-process) stream events. */
	{
		g_autofree gchar *x = lrg_editor_server_dispatch (f->server,
			"{\"id\":2,\"method\":\"node.create\",\"params\":{\"name\":\"E\"}}");
	}
	pump (50);
	r = test_client_call (c, "{\"id\":3,\"method\":\"ping\"}", events);
	g_clear_pointer (&r, json_node_unref);
	for (i = 0; i < events->len; i++)
	{
		saw_selection |= g_strcmp0 (g_ptr_array_index (events, i), "selection") == 0;
		saw_changed |= g_strcmp0 (g_ptr_array_index (events, i), "changed") == 0;
	}
	g_assert_true (saw_selection);
	g_assert_true (saw_changed);

	/* Restricting subscriptions filters events. */
	g_ptr_array_set_size (events, 0);
	r = test_client_call (c, "{\"id\":4,\"method\":\"hello\",\"params\":{\"subscribe\":[\"saved\"]}}", events);
	g_clear_pointer (&r, json_node_unref);
	{
		g_autofree gchar *x = lrg_editor_server_dispatch (f->server,
			"{\"id\":5,\"method\":\"node.create\",\"params\":{\"name\":\"F\"}}");
	}
	pump (50);
	r = test_client_call (c, "{\"id\":6,\"method\":\"ping\"}", events);
	g_assert_cmpuint (events->len, ==, 0);

	test_client_free (c);
	pump (50);
	g_assert_cmpuint (lrg_editor_server_get_n_clients (f->server), ==, 0);
}

static void
test_socket_open_file_routing (Fixture       *f,
                               gconstpointer  data)
{
	g_autofree gchar             *path = g_build_filename (f->dir, "e.sock", NULL);
	g_autoptr(LrgEditorSettings)  settings = lrg_editor_settings_new ();
	g_autoptr(LrgExternalEditor)  ext = lrg_external_editor_new (settings);
	g_autoptr(GError)             error = NULL;
	g_autoptr(JsonNode)           r = NULL;
	g_autofree gchar             *line = NULL;
	TestClient                   *c;

	g_object_set (settings, "external-editor", "emacsclient", NULL);
	lrg_editor_server_set_external_editor (f->server, ext);
	g_assert_true (lrg_editor_server_start (f->server, path, &error));

	c = test_client_connect (path);
	r = test_client_call (c, "{\"id\":1,\"method\":\"hello\",\"params\":{\"client\":\"emacs\","
	                         "\"capabilities\":[\"open-file\"],\"subscribe\":[]}}", NULL);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (lrg_editor_server_has_capability (f->server, "open-file"));

	/* The launcher hands the request to the connected Emacs. */
	g_assert_true (lrg_external_editor_open (ext, "/tmp/game/player.lua", 12, 4, &error));
	g_assert_no_error (error);
	line = test_client_next (c);
	g_assert_nonnull (strstr (line, "\"event\":\"open-file\""));
	g_assert_nonnull (strstr (line, "player.lua"));
	g_assert_nonnull (strstr (line, "\"line\":12"));

	/* file.open goes the same way, resolving relative paths against the
	 * project. */
	g_clear_pointer (&line, g_free);
	r = test_client_call (c, "{\"id\":2,\"method\":\"file.open\",\"params\":{\"path\":\"s/x.lua\",\"line\":3}}", NULL);
	g_clear_pointer (&r, json_node_unref);
	pump (20);

	test_client_free (c);
}

static void
test_socket_lifecycle (Fixture       *f,
                       gconstpointer  data)
{
	g_autofree gchar           *path = g_build_filename (f->dir, "l.sock", NULL);
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgEditor)        ed2 = lrg_editor_new ();
	g_autoptr(LrgEditorServer)  srv2 = lrg_editor_server_new (ed2);

	/* A stale file is replaced. */
	g_assert_true (g_file_set_contents (path, "stale", -1, NULL));
	g_assert_true (lrg_editor_server_start (f->server, path, &error));
	g_assert_no_error (error);

	/* A live socket is not stolen. */
	g_assert_false (lrg_editor_server_start (srv2, path, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE);

	lrg_editor_server_stop (f->server);
	g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
	g_assert_null (lrg_editor_server_get_socket_path (f->server));
}

static void
test_log_methods (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgEditorLog) log = lrg_editor_log_new (3);
	g_autoptr(JsonNode)     r = NULL;
	JsonArray              *arr;
	guint                   i;

	lrg_editor_server_set_log (f->server, log);
	for (i = 0; i < 5; i++)
	{
		g_autofree gchar *m = g_strdup_printf ("line %u", i);

		lrg_editor_log_append (log, G_LOG_LEVEL_WARNING, "game", m);
	}
	g_assert_cmpuint (lrg_editor_log_get_n_records (log), ==, 3);
	g_assert_cmpuint (lrg_editor_log_count_level (log, G_LOG_LEVEL_WARNING), ==, 3);

	r = call (f, "{\"id\":1,\"method\":\"log.tail\",\"params\":{\"count\":2}}");
	arr = json_node_get_array (result_of (r));
	g_assert_cmpuint (json_array_get_length (arr), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (arr, 1), "message"),
	                 ==, "line 4");
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (arr, 1), "level"),
	                 ==, "warning");

	lrg_editor_log_clear (log);
	g_assert_cmpuint (lrg_editor_log_get_n_records (log), ==, 0);
	pump (10);
}

static JsonNode *
custom_method (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	(*(guint *) user_data)++;
	if (params != NULL && json_object_has_member (params, "fail"))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "boom");
		return NULL;
	}
	return NULL;   /* null result */
}

static void
test_custom_methods (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(JsonNode) r = NULL;
	guint               calls = 0;

	lrg_editor_server_register_method (f->server, "play.start", custom_method, &calls, NULL);
	r = call (f, "{\"id\":1,\"method\":\"play.start\"}");
	g_assert_true (JSON_NODE_HOLDS_NULL (result_of (r)));
	g_clear_pointer (&r, json_node_unref);
	r = call (f, "{\"id\":2,\"method\":\"play.start\",\"params\":{\"fail\":1}}");
	g_assert_cmpstr (error_code_of (r), ==, "failed");
	g_assert_cmpuint (calls, ==, 2);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/editor-server/protocol/basics", test_protocol_basics);
	ADD ("/editor-server/protocol/nodes", test_node_methods);
	ADD ("/editor-server/protocol/components-scripts", test_component_script_methods);
	ADD ("/editor-server/protocol/edit-level", test_edit_and_level_methods);
	ADD ("/editor-server/protocol/log", test_log_methods);
	ADD ("/editor-server/protocol/custom", test_custom_methods);
	ADD ("/editor-server/socket/roundtrip-events", test_socket_roundtrip_and_events);
	ADD ("/editor-server/socket/open-file", test_socket_open_file_routing);
	ADD ("/editor-server/socket/lifecycle", test_socket_lifecycle);
#undef ADD

	return g_test_run ();
}
