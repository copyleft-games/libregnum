/* lrg-editor-server.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's IPC endpoint. See lrg-editor-server.h for the wire format
 * and docs/editor/ipc-protocol.org for the method reference.
 *
 * Structure of this file:
 *   1. errors and small JSON helpers
 *   2. node / value <-> JSON conversion
 *   3. client connections (non-blocking line I/O with an output queue)
 *   4. request dispatch
 *   5. built-in methods
 *   6. editor signal -> event forwarding
 *   7. lifecycle (start/stop) and public API
 */

#include "lrg-editor-server.h"
#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-editor-selection.h"
#include "lrg-editor-log.h"
#include "lrg-external-editor.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-prefab.h"
#include "../scene/lrg-material3d.h"
#include "../lrg-enums.h"
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib/gstdio.h>
#include <graylib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>

#define MAX_LINE_BYTES   (16 * 1024 * 1024)
#define MAX_QUEUED_BYTES (32 * 1024 * 1024)

/* ==========================================================================
 * 1. Errors and JSON helpers
 * ========================================================================== */

G_DEFINE_QUARK (lrg-editor-server-error-quark, lrg_editor_server_error)

static const gchar *
error_code_nick (const GError *error)
{
	static const gchar *nicks[] = {
		"parse", "invalid-request", "method-not-found",
		"invalid-params", "not-found", "failed"
	};

	if (error->domain == LRG_EDITOR_SERVER_ERROR &&
	    error->code >= 0 && (gsize) error->code < G_N_ELEMENTS (nicks))
		return nicks[error->code];

	return "failed";
}

static JsonNode *
json_string_node (const gchar *s)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	if (s == NULL)
	{
		json_node_init_null (n);
		return n;
	}
	json_node_set_string (n, s);
	return n;
}

static JsonNode *
json_object_node (JsonObject *obj)   /* transfer full */
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, obj);
	return n;
}

static JsonNode *
json_array_node (JsonArray *arr)   /* transfer full */
{
	JsonNode *n = json_node_new (JSON_NODE_ARRAY);

	json_node_take_array (n, arr);
	return n;
}

static JsonNode *
json_bool_node (gboolean v)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	json_node_set_boolean (n, v);
	return n;
}

static void
set_vec3 (JsonObject       *obj,
          const gchar      *key,
          const GrlVector3 *v)
{
	JsonArray *arr = json_array_new ();

	json_array_add_double_element (arr, v->x);
	json_array_add_double_element (arr, v->y);
	json_array_add_double_element (arr, v->z);
	json_object_set_array_member (obj, key, arr);
}

/* Reads an [x, y, z] number array; FALSE if absent, error if malformed. */
static gboolean
get_vec3 (JsonObject   *params,
          const gchar  *key,
          gfloat       *out,
          gboolean     *present,
          GError      **error)
{
	JsonNode  *node;
	JsonArray *arr;
	guint      i;

	*present = FALSE;
	if (params == NULL || !json_object_has_member (params, key))
		return TRUE;

	node = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_ARRAY (node) ||
	    json_array_get_length (arr = json_node_get_array (node)) != 3)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'%s' must be an array of three numbers", key);
		return FALSE;
	}

	for (i = 0; i < 3; i++)
	{
		JsonNode *e = json_array_get_element (arr, i);

		if (!JSON_NODE_HOLDS_VALUE (e) ||
		    (json_node_get_value_type (e) != G_TYPE_DOUBLE &&
		     json_node_get_value_type (e) != G_TYPE_INT64))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "'%s' must be an array of three numbers", key);
			return FALSE;
		}
		out[i] = (gfloat) json_node_get_double (e);
	}

	*present = TRUE;
	return TRUE;
}

/* Typed optional parameter accessors (absent -> default). */
static const gchar *
param_string (JsonObject  *params,
              const gchar *key)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, key))
		return NULL;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gboolean
param_has (JsonObject  *params,
           const gchar *key)
{
	return params != NULL && json_object_has_member (params, key) &&
	       !JSON_NODE_HOLDS_NULL (json_object_get_member (params, key));
}

static gint64
param_int (JsonObject  *params,
           const gchar *key,
           gint64       def)
{
	JsonNode *n;

	if (!param_has (params, key))
		return def;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n))
		return def;
	if (json_node_get_value_type (n) == G_TYPE_INT64)
		return json_node_get_int (n);
	if (json_node_get_value_type (n) == G_TYPE_DOUBLE)
		return (gint64) json_node_get_double (n);
	return def;
}

static gboolean
param_bool (JsonObject  *params,
            const gchar *key,
            gboolean     def)
{
	JsonNode *n;

	if (!param_has (params, key))
		return def;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return def;
	return json_node_get_boolean (n);
}

/* String element of an array, or NULL for any other element type. */
static const gchar *
array_string (JsonArray *arr,
              guint      index)
{
	JsonNode *e = json_array_get_element (arr, index);

	if (e == NULL || !JSON_NODE_HOLDS_VALUE (e) || json_node_get_value_type (e) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (e);
}

static const gchar *
require_string (JsonObject   *params,
                const gchar  *key,
                GError      **error)
{
	const gchar *s = param_string (params, key);

	if (s == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Missing string parameter '%s'", key);
	return s;
}

/* Parses an enum from its nick or name, tolerating '_' for '-' and a
 * redundant type prefix ("primitive_cube" -> "cube"). */
static gboolean
enum_from_string (GType        type,
                  const gchar *str,
                  gint        *out)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	g_autofree gchar     *norm = NULL;
	GEnumValue           *v;
	gchar                *p;

	if (str == NULL)
		return FALSE;

	norm = g_ascii_strdown (str, -1);
	for (p = norm; *p != '\0'; p++)
		if (*p == '_')
			*p = '-';

	v = g_enum_get_value_by_nick (klass, norm);
	if (v == NULL)
		v = g_enum_get_value_by_name (klass, str);
	if (v == NULL && g_str_has_prefix (norm, "primitive-"))
		v = g_enum_get_value_by_nick (klass, norm + strlen ("primitive-"));
	if (v == NULL)
		return FALSE;

	*out = v->value;
	return TRUE;
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *v = g_enum_get_value (klass, value);

	/* Nicks are static strings owned by the type system. */
	return (v != NULL) ? v->value_nick : "unknown";
}

/* ==========================================================================
 * 2. Values and nodes <-> JSON
 * ========================================================================== */

static JsonNode *
value_to_json (const GValue *value)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	switch (G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (value)))
	{
	case G_TYPE_BOOLEAN:
		json_node_set_boolean (n, g_value_get_boolean (value));
		break;
	case G_TYPE_INT:
		json_node_set_int (n, g_value_get_int (value));
		break;
	case G_TYPE_UINT:
		json_node_set_int (n, g_value_get_uint (value));
		break;
	case G_TYPE_INT64:
		json_node_set_int (n, g_value_get_int64 (value));
		break;
	case G_TYPE_UINT64:
		json_node_set_int (n, (gint64) g_value_get_uint64 (value));
		break;
	case G_TYPE_LONG:
		json_node_set_int (n, g_value_get_long (value));
		break;
	case G_TYPE_FLOAT:
		json_node_set_double (n, g_value_get_float (value));
		break;
	case G_TYPE_DOUBLE:
		json_node_set_double (n, g_value_get_double (value));
		break;
	case G_TYPE_STRING:
		if (g_value_get_string (value) != NULL)
			json_node_set_string (n, g_value_get_string (value));
		else
			json_node_init_null (n);
		break;
	case G_TYPE_ENUM:
		json_node_set_string (n, enum_nick (G_VALUE_TYPE (value), g_value_get_enum (value)));
		break;
	default:
		{
			g_autofree gchar *s = g_strdup_value_contents (value);

			json_node_set_string (n, s);
		}
		break;
	}

	return n;
}

/* JSON scalar -> GValue: ints are int64 (matching the .rlevel YAML),
 * numbers with a fraction are doubles. */
static gboolean
json_to_value (JsonNode *node,
               GValue   *value)
{
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return FALSE;

	switch (json_node_get_value_type (node))
	{
	case G_TYPE_BOOLEAN:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, json_node_get_boolean (node));
		return TRUE;
	case G_TYPE_INT64:
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, json_node_get_int (node));
		return TRUE;
	case G_TYPE_DOUBLE:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, json_node_get_double (node));
		return TRUE;
	case G_TYPE_STRING:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, json_node_get_string (node));
		return TRUE;
	default:
		return FALSE;
	}
}

static JsonNode *
visual_to_json (LrgNodeVisual *visual)
{
	JsonObject    *obj;
	JsonObject    *params;
	LrgMaterial3D *material;
	GList         *names, *l;

	if (visual == NULL)
	{
		JsonNode *n = json_node_new (JSON_NODE_NULL);

		return n;
	}

	obj = json_object_new ();
	json_object_set_string_member (obj, "kind",
	                               enum_nick (LRG_TYPE_NODE_VISUAL_KIND,
	                                          lrg_node_visual_get_kind (visual)));
	if (lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PRIMITIVE)
		json_object_set_string_member (obj, "primitive",
		                               enum_nick (LRG_TYPE_PRIMITIVE_TYPE,
		                                          lrg_node_visual_get_primitive (visual)));
	if (lrg_node_visual_get_asset (visual) != NULL)
		json_object_set_string_member (obj, "asset", lrg_node_visual_get_asset (visual));

	material = lrg_node_visual_get_material (visual);
	if (material != NULL)
	{
		JsonArray *color = json_array_new ();
		gfloat     r, g, b, a;

		lrg_material3d_get_color (material, &r, &g, &b, &a);
		json_array_add_double_element (color, r);
		json_array_add_double_element (color, g);
		json_array_add_double_element (color, b);
		json_array_add_double_element (color, a);
		json_object_set_array_member (obj, "color", color);
		json_object_set_double_member (obj, "roughness", lrg_material3d_get_roughness (material));
		json_object_set_double_member (obj, "metallic", lrg_material3d_get_metallic (material));
	}

	params = json_object_new ();
	names = g_list_sort (lrg_node_visual_get_param_names (visual), (GCompareFunc) g_strcmp0);
	for (l = names; l != NULL; l = l->next)
		json_object_set_member (params, l->data,
		                        value_to_json (lrg_node_visual_get_param (visual, l->data)));
	g_list_free (names);
	json_object_set_object_member (obj, "params", params);

	return json_object_node (obj);
}

JsonNode *
lrg_editor_server_node_to_json (LrgNode  *node,
                                gboolean  recursive)
{
	JsonObject       *obj;
	JsonArray        *components;
	JsonArray        *scripts;
	JsonArray        *children;
	GPtrArray        *arr;
	g_autofree gchar *path = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	obj = json_object_new ();
	path = lrg_node_get_path (node);
	json_object_set_string_member (obj, "guid", lrg_node_get_guid (node));
	json_object_set_member (obj, "name", json_string_node (lrg_node_get_name (node)));
	json_object_set_string_member (obj, "path", path);
	json_object_set_boolean_member (obj, "visible", lrg_node_get_visible (node));
	json_object_set_boolean_member (obj, "locked", lrg_node_get_locked (node));
	json_object_set_boolean_member (obj, "is_2d", lrg_node_get_is_2d (node));
	set_vec3 (obj, "location", lrg_node_get_location (node));
	set_vec3 (obj, "rotation", lrg_node_get_rotation (node));
	set_vec3 (obj, "scale", lrg_node_get_scale (node));
	json_object_set_member (obj, "visual", visual_to_json (lrg_node_get_visual (node)));

	components = json_array_new ();
	arr = lrg_node_get_components (node);
	for (i = 0; i < arr->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (arr, i);
		JsonObject       *c = json_object_new ();
		JsonObject       *props = json_object_new ();
		GList            *keys, *l;

		json_object_set_member (c, "type", json_string_node (lrg_component_desc_get_type_name (desc)));
		keys = lrg_component_desc_get_sorted_keys (desc);
		for (l = keys; l != NULL; l = l->next)
			json_object_set_member (props, l->data,
			                        value_to_json (lrg_component_desc_get_value (desc, l->data)));
		g_list_free (keys);
		json_object_set_object_member (c, "props", props);
		json_array_add_object_element (components, c);
	}
	json_object_set_array_member (obj, "components", components);

	scripts = json_array_new ();
	arr = lrg_node_get_scripts (node);
	for (i = 0; i < arr->len; i++)
	{
		LrgScriptBinding *s = g_ptr_array_index (arr, i);
		JsonObject       *o = json_object_new ();

		json_object_set_string_member (o, "language",
		                               enum_nick (LRG_TYPE_SCRIPT_LANGUAGE,
		                                          lrg_script_binding_get_language (s)));
		json_object_set_member (o, "script", json_string_node (lrg_script_binding_get_script (s)));
		json_object_set_boolean_member (o, "enabled", lrg_script_binding_get_enabled (s));
		json_array_add_object_element (scripts, o);
	}
	json_object_set_array_member (obj, "scripts", scripts);

	children = json_array_new ();
	arr = lrg_node_get_children (node);
	for (i = 0; i < arr->len; i++)
	{
		LrgNode *child = g_ptr_array_index (arr, i);

		if (recursive)
			json_array_add_element (children, lrg_editor_server_node_to_json (child, TRUE));
		else
			json_array_add_string_element (children, lrg_node_get_guid (child));
	}
	json_object_set_array_member (obj, "children", children);

	return json_object_node (obj);
}

/* Compact node summary used by node.tree / node.list. */
static JsonObject *
node_summary (LrgNode *node)
{
	JsonObject    *obj = json_object_new ();
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	json_object_set_string_member (obj, "guid", lrg_node_get_guid (node));
	json_object_set_member (obj, "name", json_string_node (lrg_node_get_name (node)));
	json_object_set_string_member (obj, "kind",
	                               visual != NULL
	                               ? enum_nick (LRG_TYPE_NODE_VISUAL_KIND, lrg_node_visual_get_kind (visual))
	                               : "none");
	json_object_set_boolean_member (obj, "visible", lrg_node_get_visible (node));
	json_object_set_boolean_member (obj, "locked", lrg_node_get_locked (node));
	json_object_set_int_member (obj, "children", lrg_node_get_n_children (node));
	return obj;
}

/* ==========================================================================
 * Server struct
 * ========================================================================== */

typedef struct
{
	LrgEditorServerMethod func;
	gpointer              user_data;
	GDestroyNotify        destroy;
} MethodEntry;

static void
method_entry_free (gpointer data)
{
	MethodEntry *e = data;

	if (e->destroy != NULL)
		e->destroy (e->user_data);
	g_free (e);
}

typedef struct _Client Client;

struct _LrgEditorServer
{
	GObject parent_instance;

	LrgEditor         *editor;     /* ref */
	LrgExternalEditor *external;   /* ref, nullable */
	LrgEditorLog      *log;        /* ref, nullable */
	gchar             *project_dir;

	GHashTable        *methods;    /* name -> MethodEntry */

	GSocketService    *service;
	gchar             *socket_path;
	GList             *clients;    /* Client* (owned ref) */
	guint              next_client_id;
	Client            *current;    /* client being served, or NULL */

	guint              changed_idle;
	gulong             editor_ids[6];
	gulong             selection_id;
	gulong             external_id;
	gulong             log_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorServer, lrg_editor_server, G_TYPE_OBJECT)

enum
{
	SIGNAL_CLIENT_CONNECTED,
	SIGNAL_CLIENT_DISCONNECTED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * 3. Clients
 * ========================================================================== */

struct _Client
{
	gint               ref_count;
	LrgEditorServer   *server;     /* weak: cleared on close */
	guint              id;
	GSocketConnection *connection;
	GDataInputStream  *input;
	GOutputStream     *output;
	GCancellable      *cancellable;
	GString           *outbuf;
	GSource           *write_source;
	gboolean           closed;

	gchar             *name;
	GHashTable        *capabilities;   /* set */
	GHashTable        *subscriptions;  /* set; NULL = default */
	gint64             last_active;
};

static Client *
client_ref (Client *c)
{
	c->ref_count++;
	return c;
}

static void
client_unref (Client *c)
{
	if (--c->ref_count > 0)
		return;

	g_clear_object (&c->connection);
	g_clear_object (&c->input);
	g_clear_object (&c->cancellable);
	g_string_free (c->outbuf, TRUE);
	g_free (c->name);
	g_hash_table_unref (c->capabilities);
	g_clear_pointer (&c->subscriptions, g_hash_table_unref);
	g_free (c);
}

static void
client_close (Client *c)
{
	LrgEditorServer *server = c->server;

	if (c->closed)
		return;
	c->closed = TRUE;

	g_cancellable_cancel (c->cancellable);
	if (c->write_source != NULL)
	{
		g_source_destroy (c->write_source);
		g_source_unref (c->write_source);
		c->write_source = NULL;
	}
	g_io_stream_close (G_IO_STREAM (c->connection), NULL, NULL);

	if (server != NULL)
	{
		c->server = NULL;
		server->clients = g_list_remove (server->clients, c);
		g_signal_emit (server, signals[SIGNAL_CLIENT_DISCONNECTED], 0, c->id);
		client_unref (c);  /* the list's reference */
	}
}

static gboolean client_flush (Client *c);

static gboolean
on_client_writable (GObject  *stream,
                    gpointer  user_data)
{
	Client *c = user_data;

	return client_flush (c) ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

/*
 * client_flush:
 *
 * Writes as much of the output queue as the socket accepts. Returns TRUE
 * when the queue is empty (or the client is gone); otherwise a writable
 * source is armed and FALSE is returned.
 */
static gboolean
client_flush (Client *c)
{
	GPollableOutputStream *pout = G_POLLABLE_OUTPUT_STREAM (c->output);

	while (!c->closed && c->outbuf->len > 0)
	{
		g_autoptr(GError) error = NULL;
		gssize            n;

		n = g_pollable_output_stream_write_nonblocking (pout, c->outbuf->str,
		                                                c->outbuf->len, NULL, &error);
		if (n < 0)
		{
			if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
			{
				if (c->write_source == NULL)
				{
					c->write_source = g_pollable_output_stream_create_source (pout, NULL);
					g_source_set_callback (c->write_source,
					                       (GSourceFunc) (void (*) (void)) on_client_writable,
					                       c, NULL);
					g_source_attach (c->write_source, g_main_context_get_thread_default ());
				}
				return FALSE;
			}
			client_close (c);
			return TRUE;
		}
		g_string_erase (c->outbuf, 0, n);
	}

	if (c->write_source != NULL)
	{
		g_source_destroy (c->write_source);
		g_source_unref (c->write_source);
		c->write_source = NULL;
	}
	return TRUE;
}

static void
client_send (Client      *c,
             const gchar *line)
{
	if (c->closed)
		return;

	g_string_append (c->outbuf, line);
	g_string_append_c (c->outbuf, '\n');

	/* A client that stops reading must not grow our memory forever. */
	if (c->outbuf->len > MAX_QUEUED_BYTES)
	{
		g_message ("editor server: dropping client %u (%s): output queue overflow",
		           c->id, c->name != NULL ? c->name : "unnamed");
		client_close (c);
		return;
	}

	if (c->write_source == NULL)
		client_flush (c);
}

static gboolean
client_wants (Client      *c,
              const gchar *event)
{
	if (c->subscriptions == NULL)
		return g_strcmp0 (event, "log") != 0;   /* log is opt-in: chatty */

	return g_hash_table_contains (c->subscriptions, "*") ||
	       g_hash_table_contains (c->subscriptions, event);
}

static gchar *dispatch_line (LrgEditorServer *self, Client *client, const gchar *line);

static void
on_line_read (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	Client           *c = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;
	gsize             length = 0;

	line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source),
	                                                   result, &length, &error);

	if (c->closed)
	{
		client_unref (c);
		return;
	}

	if (line == NULL)
	{
		/* EOF, invalid UTF-8 or a socket error: drop the client. A
		 * one-shot client (lrgeditorctl) that hangs up with event lines
		 * still unread resets the connection; that is a normal goodbye. */
		if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE) &&
		    !(error->domain == G_IO_ERROR && error->code == G_IO_ERROR_FAILED &&
		      strstr (error->message, "reset by peer") != NULL))
			g_message ("editor server: client %u: %s", c->id, error->message);
		client_close (c);
		client_unref (c);
		return;
	}

	if (length > MAX_LINE_BYTES)
	{
		client_send (c, "{\"id\":null,\"error\":{\"code\":\"invalid-request\","
		                "\"message\":\"Request too large\"}}");
		client_close (c);
		client_unref (c);
		return;
	}

	c->last_active = g_get_monotonic_time ();
	if (*g_strstrip (line) != '\0' && c->server != NULL)
	{
		g_autofree gchar *response = dispatch_line (c->server, c, line);

		if (response != NULL)
			client_send (c, response);
	}

	/* Keep reading (the pending read holds our reference). */
	if (!c->closed)
		g_data_input_stream_read_line_async (c->input, G_PRIORITY_DEFAULT,
		                                     c->cancellable, on_line_read, c);
	else
		client_unref (c);
}

static gboolean
on_incoming (GSocketService    *service,
             GSocketConnection *connection,
             GObject           *source_object,
             gpointer           user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	Client          *c;
	GSocket         *socket;

	c = g_new0 (Client, 1);
	c->ref_count = 1;  /* owned by the client list */
	c->server = self;
	c->id = ++self->next_client_id;
	c->connection = g_object_ref (connection);
	c->input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
	g_data_input_stream_set_newline_type (c->input, G_DATA_STREAM_NEWLINE_TYPE_ANY);
	c->output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
	c->cancellable = g_cancellable_new ();
	c->outbuf = g_string_new (NULL);
	c->capabilities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	c->last_active = g_get_monotonic_time ();

	/* Writes go through the non-blocking queue. */
	socket = g_socket_connection_get_socket (connection);
	g_socket_set_blocking (socket, FALSE);

	self->clients = g_list_append (self->clients, c);
	g_signal_emit (self, signals[SIGNAL_CLIENT_CONNECTED], 0, c->id);

	g_data_input_stream_read_line_async (c->input, G_PRIORITY_DEFAULT, c->cancellable,
	                                     on_line_read, client_ref (c));
	return TRUE;
}

/* ==========================================================================
 * 4. Dispatch
 * ========================================================================== */

static gchar *
node_to_line (JsonNode *node)
{
	g_autoptr(JsonGenerator) gen = json_generator_new ();

	json_generator_set_root (gen, node);
	json_generator_set_pretty (gen, FALSE);
	return json_generator_to_data (gen, NULL);
}

static gchar *
error_response (JsonNode     *id,
                const GError *error)
{
	g_autoptr(JsonNode) root = NULL;
	JsonObject         *obj = json_object_new ();
	JsonObject         *err = json_object_new ();

	json_object_set_member (obj, "id", id != NULL ? json_node_copy (id) : json_node_new (JSON_NODE_NULL));
	json_object_set_string_member (err, "code", error_code_nick (error));
	json_object_set_string_member (err, "message", error->message);
	json_object_set_object_member (obj, "error", err);

	root = json_object_node (obj);
	return node_to_line (root);
}

static gchar *
dispatch_line (LrgEditorServer *self,
               Client          *client,
               const gchar     *line)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)     error = NULL;
	JsonNode             *root;
	JsonObject           *request;
	JsonNode             *id = NULL;
	JsonObject           *params = NULL;
	const gchar          *method;
	MethodEntry          *entry;
	JsonNode             *result;

	if (!json_parser_load_from_data (parser, line, -1, &error))
	{
		g_autoptr(GError) e = g_error_new (LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_PARSE,
		                                   "Invalid JSON: %s", error->message);
		return error_response (NULL, e);
	}

	root = json_parser_get_root (parser);
	if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
	{
		g_autoptr(GError) e = g_error_new_literal (LRG_EDITOR_SERVER_ERROR,
		                                           LRG_EDITOR_SERVER_ERROR_INVALID_REQUEST,
		                                           "A request must be a JSON object");
		return error_response (NULL, e);
	}

	request = json_node_get_object (root);
	if (json_object_has_member (request, "id"))
		id = json_object_get_member (request, "id");

	method = param_string (request, "method");
	if (method == NULL)
	{
		g_autoptr(GError) e = g_error_new_literal (LRG_EDITOR_SERVER_ERROR,
		                                           LRG_EDITOR_SERVER_ERROR_INVALID_REQUEST,
		                                           "Missing \"method\"");
		return error_response (id, e);
	}

	if (json_object_has_member (request, "params"))
	{
		JsonNode *p = json_object_get_member (request, "params");

		if (JSON_NODE_HOLDS_OBJECT (p))
			params = json_node_get_object (p);
		else if (!JSON_NODE_HOLDS_NULL (p))
		{
			g_autoptr(GError) e = g_error_new_literal (LRG_EDITOR_SERVER_ERROR,
			                                           LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                                           "\"params\" must be an object");
			return error_response (id, e);
		}
	}

	entry = g_hash_table_lookup (self->methods, method);
	if (entry == NULL)
	{
		g_autoptr(GError) e = g_error_new (LRG_EDITOR_SERVER_ERROR,
		                                   LRG_EDITOR_SERVER_ERROR_METHOD_NOT_FOUND,
		                                   "Unknown method '%s'", method);
		return error_response (id, e);
	}

	/* Run the handler with the calling client available to "hello". */
	self->current = client;
	result = entry->func (self, params, entry->user_data, &error);
	self->current = NULL;

	if (error != NULL)
	{
		if (result != NULL)
			json_node_unref (result);
		return (id != NULL) ? error_response (id, error) : NULL;
	}

	if (id == NULL)
	{
		if (result != NULL)
			json_node_unref (result);
		return NULL;   /* notification: no response */
	}

	{
		g_autoptr(JsonNode) resp = NULL;
		JsonObject         *obj = json_object_new ();

		json_object_set_member (obj, "id", json_node_copy (id));
		json_object_set_member (obj, "result", result != NULL ? result : json_node_new (JSON_NODE_NULL));
		resp = json_object_node (obj);
		return node_to_line (resp);
	}
}

gchar *
lrg_editor_server_dispatch (LrgEditorServer *self,
                            const gchar     *line)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), NULL);
	g_return_val_if_fail (line != NULL, NULL);

	return dispatch_line (self, NULL, line);
}

/* ==========================================================================
 * 5. Built-in methods
 * ========================================================================== */

static LrgNode *
require_node (LrgEditorServer  *self,
              JsonObject       *params,
              const gchar      *key,
              GError          **error)
{
	const gchar *ref = require_string (params, key, error);
	LrgNode     *node;

	if (ref == NULL)
		return NULL;

	node = lrg_editor_find_node (self->editor, ref);
	if (node == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No node '%s'", ref);
	return node;
}

/* Optional node reference; absent -> @def, unresolvable -> error. */
static gboolean
optional_node (LrgEditorServer  *self,
               JsonObject       *params,
               const gchar      *key,
               LrgNode         **out,
               GError          **error)
{
	*out = NULL;
	if (!param_has (params, key))
		return TRUE;

	*out = require_node (self, params, key, error);
	return *out != NULL;
}

static gchar *
resolve_path (LrgEditorServer *self,
              const gchar     *path)
{
	if (g_path_is_absolute (path) || self->project_dir == NULL)
		return g_canonicalize_filename (path, NULL);

	return g_canonicalize_filename (path, self->project_dir);
}

static JsonNode *
selection_json (LrgEditorServer *self)
{
	LrgEditorSelection *sel = lrg_editor_get_selection (self->editor);
	GPtrArray          *nodes = lrg_editor_selection_get_nodes (sel);
	LrgNode            *primary = lrg_editor_selection_get_primary (sel);
	JsonObject         *obj = json_object_new ();
	JsonArray          *arr = json_array_new ();
	guint               i;

	for (i = 0; i < nodes->len; i++)
		json_array_add_string_element (arr, lrg_node_get_guid (g_ptr_array_index (nodes, i)));
	json_object_set_array_member (obj, "nodes", arr);
	json_object_set_member (obj, "primary",
	                        json_string_node (primary != NULL ? lrg_node_get_guid (primary) : NULL));
	return json_object_node (obj);
}

static JsonNode *
m_hello (LrgEditorServer  *self,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	JsonObject *obj;
	Client     *c = self->current;

	if (c != NULL && params != NULL)
	{
		const gchar *name = param_string (params, "client");
		JsonNode    *caps = json_object_has_member (params, "capabilities")
		                    ? json_object_get_member (params, "capabilities") : NULL;
		JsonNode    *subs = json_object_has_member (params, "subscribe")
		                    ? json_object_get_member (params, "subscribe") : NULL;
		guint        i;

		if (name != NULL)
		{
			g_free (c->name);
			c->name = g_strdup (name);
		}
		if (caps != NULL && JSON_NODE_HOLDS_ARRAY (caps))
		{
			JsonArray *arr = json_node_get_array (caps);

			for (i = 0; i < json_array_get_length (arr); i++)
			{
				const gchar *s = array_string (arr, i);

				if (s != NULL)
					g_hash_table_add (c->capabilities, g_strdup (s));
			}
		}
		if (subs != NULL && JSON_NODE_HOLDS_ARRAY (subs))
		{
			JsonArray *arr = json_node_get_array (subs);

			g_clear_pointer (&c->subscriptions, g_hash_table_unref);
			c->subscriptions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
			for (i = 0; i < json_array_get_length (arr); i++)
			{
				const gchar *s = array_string (arr, i);

				if (s != NULL)
					g_hash_table_add (c->subscriptions, g_strdup (s));
			}
		}
	}

	obj = json_object_new ();
	json_object_set_string_member (obj, "server", "libregnum-editor");
	json_object_set_int_member (obj, "protocol", LRG_EDITOR_SERVER_PROTOCOL_VERSION);
	json_object_set_int_member (obj, "pid", getpid ());
	json_object_set_member (obj, "project", json_string_node (self->project_dir));
	json_object_set_member (obj, "level", json_string_node (lrg_editor_get_path (self->editor)));
	if (c != NULL)
		json_object_set_int_member (obj, "client_id", c->id);
	return json_object_node (obj);
}

static JsonNode *
m_ping (LrgEditorServer  *self,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	return json_string_node ("pong");
}

static JsonNode *
m_methods (LrgEditorServer  *self,
           JsonObject       *params,
           gpointer          user_data,
           GError          **error)
{
	JsonArray *arr = json_array_new ();
	GList     *names = lrg_editor_server_list_methods (self);
	GList     *l;

	for (l = names; l != NULL; l = l->next)
		json_array_add_string_element (arr, l->data);
	g_list_free (names);
	return json_array_node (arr);
}

static JsonNode *
m_editor_info (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	JsonObject *obj = json_object_new ();
	LrgLevel   *level = lrg_editor_get_level (self->editor);
	gdouble     st, sr, ss;

	lrg_editor_get_snap (self->editor, &st, &sr, &ss);
	json_object_set_int_member (obj, "pid", getpid ());
	json_object_set_member (obj, "project", json_string_node (self->project_dir));
	json_object_set_member (obj, "level", json_string_node (lrg_editor_get_path (self->editor)));
	json_object_set_member (obj, "name", json_string_node (lrg_level_get_name (level)));
	json_object_set_boolean_member (obj, "dirty", lrg_editor_is_dirty (self->editor));
	json_object_set_int_member (obj, "nodes", lrg_level_count_nodes (level));
	json_object_set_int_member (obj, "undo", lrg_editor_get_undo_count (self->editor));
	json_object_set_int_member (obj, "redo", lrg_editor_get_redo_count (self->editor));
	json_object_set_string_member (obj, "tool",
	                               enum_nick (LRG_TYPE_EDITOR_TOOL, lrg_editor_get_tool (self->editor)));
	json_object_set_string_member (obj, "space",
	                               enum_nick (LRG_TYPE_EDITOR_SPACE, lrg_editor_get_space (self->editor)));
	json_object_set_boolean_member (obj, "snap", lrg_editor_get_snap_enabled (self->editor));
	json_object_set_double_member (obj, "snap_translate", st);
	json_object_set_double_member (obj, "snap_rotate", sr);
	json_object_set_double_member (obj, "snap_scale", ss);
	json_object_set_member (obj, "selection", selection_json (self));
	json_object_set_int_member (obj, "clients", g_list_length (self->clients));
	return json_object_node (obj);
}

/* ---- level ---- */

static JsonNode *
m_level_new (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	lrg_editor_new_level (self->editor, param_string (params, "name"));
	return json_bool_node (TRUE);
}

static JsonNode *
m_level_open (LrgEditorServer  *self,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar      *path = require_string (params, "path", error);
	g_autofree gchar *full = NULL;

	if (path == NULL)
		return NULL;

	full = resolve_path (self, path);
	if (lrg_editor_is_dirty (self->editor) && !param_bool (params, "force", FALSE))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The current level has unsaved changes; pass \"force\": true to discard them");
		return NULL;
	}
	if (!lrg_editor_load_level (self->editor, full, error))
		return NULL;
	return json_string_node (full);
}

static JsonNode *
m_level_save (LrgEditorServer  *self,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	const gchar *path = param_string (params, "path");
	gboolean     ok;

	if (path != NULL)
	{
		g_autofree gchar *full = resolve_path (self, path);

		ok = lrg_editor_save_level (self->editor, full, error);
	}
	else
	{
		ok = lrg_editor_save (self->editor, error);
	}

	return ok ? json_string_node (lrg_editor_get_path (self->editor)) : NULL;
}

static JsonNode *
m_level_reload (LrgEditorServer  *self,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	return lrg_editor_reload (self->editor, error) ? json_bool_node (TRUE) : NULL;
}

static JsonNode *
m_level_serialize (LrgEditorServer  *self,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	g_autofree gchar *data = lrg_editor_serialize (self->editor);

	return json_string_node (data);
}

/* ---- nodes ---- */

static void
tree_recursive (LrgNode   *node,
                JsonArray *out)
{
	JsonObject *obj = node_summary (node);
	JsonArray  *children = json_array_new ();
	GPtrArray  *arr = lrg_node_get_children (node);
	guint       i;

	for (i = 0; i < arr->len; i++)
		tree_recursive (g_ptr_array_index (arr, i), children);
	json_object_set_array_member (obj, "nodes", children);
	json_array_add_object_element (out, obj);
}

static JsonNode *
m_node_tree (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	JsonArray *out = json_array_new ();
	LrgNode   *root;
	GPtrArray *top;
	guint      i;

	if (!optional_node (self, params, "node", &root, error))
	{
		json_array_unref (out);
		return NULL;
	}
	if (root != NULL)
	{
		tree_recursive (root, out);
		return json_array_node (out);
	}

	top = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (self->editor)));
	for (i = 0; i < top->len; i++)
		tree_recursive (g_ptr_array_index (top, i), out);
	return json_array_node (out);
}

static gboolean
list_visit (LrgNode  *node,
            gpointer  user_data)
{
	JsonArray        *out = user_data;
	JsonObject       *obj = node_summary (node);
	g_autofree gchar *path = lrg_node_get_path (node);
	LrgNode          *parent = lrg_node_get_parent (node);

	json_object_set_string_member (obj, "path", path);
	json_object_set_int_member (obj, "depth", (gint64) lrg_node_get_depth (node) - 1);
	json_object_set_member (obj, "parent",
	                        json_string_node (parent != NULL && lrg_node_get_parent (parent) != NULL
	                                          ? lrg_node_get_guid (parent) : NULL));
	json_array_add_object_element (out, obj);
	return TRUE;
}

static JsonNode *
m_node_list (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	JsonArray *out = json_array_new ();
	GPtrArray *top;
	guint      i;

	top = lrg_node_get_children (lrg_level_get_root (lrg_editor_get_level (self->editor)));
	for (i = 0; i < top->len; i++)
		lrg_node_foreach (g_ptr_array_index (top, i), list_visit, out);
	return json_array_node (out);
}

static JsonNode *
m_node_get (LrgEditorServer  *self,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgNode *node = require_node (self, params, "node", error);

	if (node == NULL)
		return NULL;
	return lrg_editor_server_node_to_json (node, param_bool (params, "recursive", FALSE));
}

static JsonNode *
m_node_find (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar         *query = require_string (params, "query", error);
	g_autoptr(GPtrArray) found = NULL;
	JsonArray           *out;
	guint                i;

	if (query == NULL)
		return NULL;

	found = lrg_editor_find_nodes (self->editor, query);
	out = json_array_new ();
	for (i = 0; i < found->len; i++)
	{
		JsonObject       *obj = node_summary (g_ptr_array_index (found, i));
		g_autofree gchar *path = lrg_node_get_path (g_ptr_array_index (found, i));

		json_object_set_string_member (obj, "path", path);
		json_array_add_object_element (out, obj);
	}
	return json_array_node (out);
}

/*
 * apply_visual_params:
 *
 * Applies "kind", "primitive", "asset", "color", "roughness", "metallic",
 * "material" {color, roughness, metallic} and "params" from @spec onto
 * @visual (creating the material on demand).
 */
static gboolean
apply_visual_spec (LrgNodeVisual  *visual,
                   JsonObject     *spec,
                   GError        **error)
{
	const gchar *s;
	gint         v;

	if ((s = param_string (spec, "kind")) != NULL)
	{
		if (!enum_from_string (LRG_TYPE_NODE_VISUAL_KIND, s, &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown visual kind '%s'", s);
			return FALSE;
		}
		lrg_node_visual_set_kind (visual, v);
	}
	if ((s = param_string (spec, "primitive")) != NULL)
	{
		if (!enum_from_string (LRG_TYPE_PRIMITIVE_TYPE, s, &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown primitive '%s'", s);
			return FALSE;
		}
		lrg_node_visual_set_primitive (visual, v);
	}
	if (param_has (spec, "asset"))
		lrg_node_visual_set_asset (visual, param_string (spec, "asset"));

	/* "material": {color, roughness, metallic} is accepted as well as the
	 * flat form (it is how .rlevel files spell it). */
	if (param_has (spec, "material") && JSON_NODE_HOLDS_OBJECT (json_object_get_member (spec, "material")) &&
	    !apply_visual_spec (visual, json_object_get_object_member (spec, "material"), error))
		return FALSE;

	if (param_has (spec, "color") || param_has (spec, "roughness") || param_has (spec, "metallic"))
	{
		LrgMaterial3D *mat = lrg_node_visual_get_material (visual);

		if (mat == NULL)
		{
			g_autoptr(LrgMaterial3D) fresh = lrg_material3d_new ();

			lrg_node_visual_set_material (visual, fresh);
			mat = fresh;
		}
		if (param_has (spec, "color"))
		{
			JsonNode  *cn = json_object_get_member (spec, "color");
			JsonArray *ca;
			gfloat     c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			guint      i, n;

			if (!JSON_NODE_HOLDS_ARRAY (cn) ||
			    (n = json_array_get_length (ca = json_node_get_array (cn))) < 3 || n > 4)
			{
				g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR,
				                     LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
				                     "'color' must be [r, g, b] or [r, g, b, a] in 0..1");
				return FALSE;
			}
			for (i = 0; i < n; i++)
				c[i] = (gfloat) json_array_get_double_element (ca, i);
			lrg_material3d_set_color (mat, c[0], c[1], c[2], c[3]);
		}
		if (param_has (spec, "roughness"))
			lrg_material3d_set_roughness (mat, (gfloat) json_object_get_double_member (spec, "roughness"));
		if (param_has (spec, "metallic"))
			lrg_material3d_set_metallic (mat, (gfloat) json_object_get_double_member (spec, "metallic"));
	}

	if (param_has (spec, "params"))
	{
		JsonNode   *pn = json_object_get_member (spec, "params");
		JsonObject *po;
		GList      *keys, *l;

		if (!JSON_NODE_HOLDS_OBJECT (pn))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "'params' must be an object");
			return FALSE;
		}
		po = json_node_get_object (pn);
		keys = json_object_get_members (po);
		for (l = keys; l != NULL; l = l->next)
		{
			JsonNode *e = json_object_get_member (po, l->data);
			GValue    val = G_VALUE_INIT;

			if (JSON_NODE_HOLDS_NULL (e))
				lrg_node_visual_remove_param (visual, l->data);
			else if (json_to_value (e, &val))
			{
				lrg_node_visual_set_param (visual, l->data, &val);
				g_value_unset (&val);
			}
		}
		g_list_free (keys);
	}

	return TRUE;
}

/*
 * apply_node_spec:
 *
 * Applies the editable fields of a node.set / node.create request to
 * @node in place. Callers wrap this in a snapshot commit so the whole
 * request is one undo step.
 */
static gboolean
apply_node_spec (LrgNode     *node,
                 JsonObject  *params,
                 GError     **error)
{
	gfloat   v[3];
	gboolean present;

	if (param_has (params, "name"))
		lrg_node_set_name (node, param_string (params, "name"));
	if (param_has (params, "visible"))
		lrg_node_set_visible (node, param_bool (params, "visible", TRUE));
	if (param_has (params, "locked"))
		lrg_node_set_locked (node, param_bool (params, "locked", FALSE));
	if (param_has (params, "is_2d"))
		lrg_node_set_is_2d (node, param_bool (params, "is_2d", FALSE));

	if (!get_vec3 (params, "location", v, &present, error))
		return FALSE;
	if (present)
		lrg_node_set_location_xyz (node, v[0], v[1], v[2]);

	if (!get_vec3 (params, "rotation", v, &present, error))
		return FALSE;
	if (present)
		lrg_node_set_rotation_xyz (node, v[0], v[1], v[2]);

	if (!get_vec3 (params, "rotation_degrees", v, &present, error))
		return FALSE;
	if (present)
		lrg_node_set_rotation_xyz (node, v[0] * (gfloat) G_PI / 180.0f,
		                           v[1] * (gfloat) G_PI / 180.0f,
		                           v[2] * (gfloat) G_PI / 180.0f);

	if (!get_vec3 (params, "scale", v, &present, error))
		return FALSE;
	if (present)
		lrg_node_set_scale_xyz (node, v[0], v[1], v[2]);

	if (params != NULL && json_object_has_member (params, "visual"))
	{
		JsonNode *vn = json_object_get_member (params, "visual");

		if (JSON_NODE_HOLDS_NULL (vn))
		{
			lrg_node_set_visual (node, NULL);
		}
		else if (JSON_NODE_HOLDS_OBJECT (vn))
		{
			g_autoptr(LrgNodeVisual) copy = NULL;

			/* Edit a copy so a failing spec leaves the node untouched. */
			copy = (lrg_node_get_visual (node) != NULL)
			       ? lrg_node_visual_copy (lrg_node_get_visual (node))
			       : lrg_node_visual_new (LRG_NODE_VISUAL_NONE);
			if (!apply_visual_spec (copy, json_node_get_object (vn), error))
				return FALSE;
			lrg_node_set_visual (node, copy);
		}
		else
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "'visual' must be an object or null");
			return FALSE;
		}
	}

	return TRUE;
}

static JsonNode *
m_node_set (LrgEditorServer  *self,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgNode            *node = require_node (self, params, "node", error);
	g_autoptr(LrgNode)  before = NULL;
	g_autofree gchar   *label = NULL;

	if (node == NULL)
		return NULL;

	before = lrg_editor_snapshot_node (node);
	if (!apply_node_spec (node, params, error))
	{
		lrg_node_copy_state (node, before);
		return NULL;
	}

	label = g_strdup_printf ("Edit %s", lrg_node_get_name (node) != NULL
	                                    ? lrg_node_get_name (node) : "node");
	lrg_editor_commit_node_state (self->editor, node, before, label,
	                              param_bool (params, "merge", FALSE));
	return lrg_editor_server_node_to_json (node, FALSE);
}

static JsonNode *
m_node_create (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	g_autoptr(LrgNode) node = NULL;
	LrgNode           *parent;
	const gchar       *primitive = param_string (params, "primitive");
	const gchar       *kind = param_string (params, "kind");

	if (!optional_node (self, params, "parent", &parent, error))
		return NULL;

	node = lrg_node_new (param_string (params, "name") != NULL
	                     ? param_string (params, "name") : "Node");

	/* Shorthand: "primitive" / "kind" at the top level. */
	if (primitive != NULL || kind != NULL)
	{
		g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (primitive != NULL
		                                                       ? LRG_NODE_VISUAL_PRIMITIVE
		                                                       : LRG_NODE_VISUAL_NONE);
		g_autoptr(JsonObject)    spec = json_object_new ();

		if (kind != NULL)
			json_object_set_string_member (spec, "kind", kind);
		if (primitive != NULL)
			json_object_set_string_member (spec, "primitive", primitive);
		if (!apply_visual_spec (visual, spec, error))
			return NULL;
		lrg_node_set_visual (node, visual);
	}

	if (!apply_node_spec (node, params, error))
		return NULL;

	lrg_editor_insert_node (self->editor, node, parent,
	                        (gint) param_int (params, "index", -1));
	return lrg_editor_server_node_to_json (node, FALSE);
}

static JsonNode *
m_node_delete (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgNode *node;

	if (!param_has (params, "node"))
		return json_bool_node (lrg_editor_delete_selection (self->editor) > 0);

	node = require_node (self, params, "node", error);
	if (node == NULL)
		return NULL;
	return json_bool_node (lrg_editor_delete_node (self->editor, node));
}

static JsonNode *
m_node_duplicate (LrgEditorServer  *self,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	g_autoptr(GPtrArray) created = NULL;
	JsonArray           *out;
	LrgNode             *node;
	guint                i;

	if (!optional_node (self, params, "node", &node, error))
		return NULL;
	if (node != NULL)
		lrg_editor_select (self->editor, node, FALSE);

	created = lrg_editor_duplicate_selection (self->editor);
	out = json_array_new ();
	for (i = 0; i < created->len; i++)
		json_array_add_string_element (out, lrg_node_get_guid (g_ptr_array_index (created, i)));
	return json_array_node (out);
}

static JsonNode *
m_node_move (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	LrgNode *node = require_node (self, params, "node", error);
	LrgNode *parent;

	if (node == NULL || !optional_node (self, params, "parent", &parent, error))
		return NULL;

	if (parent != NULL && lrg_node_is_ancestor_of (node, parent))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "A node cannot be moved under itself");
		return NULL;
	}

	return json_bool_node (lrg_editor_move_node (self->editor, node, parent,
	                                             (gint) param_int (params, "index", -1),
	                                             param_bool (params, "keep_world", TRUE)));
}

static JsonNode *
m_node_rename (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgNode     *node = require_node (self, params, "node", error);
	const gchar *name;

	if (node == NULL || (name = require_string (params, "name", error)) == NULL)
		return NULL;

	lrg_editor_rename_node (self->editor, node, name);
	return json_bool_node (TRUE);
}

/* ---- components and scripts ---- */

static JsonNode *
m_component_add (LrgEditorServer  *self,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode                    *node = require_node (self, params, "node", error);
	const gchar                *type;
	g_autoptr(LrgComponentDesc) desc = NULL;

	if (node == NULL || (type = require_string (params, "type", error)) == NULL)
		return NULL;

	desc = lrg_component_desc_new (type);
	if (param_has (params, "props"))
	{
		JsonNode   *pn = json_object_get_member (params, "props");
		JsonObject *po;
		GList      *keys, *l;

		if (!JSON_NODE_HOLDS_OBJECT (pn))
		{
			g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			                     "'props' must be an object");
			return NULL;
		}
		po = json_node_get_object (pn);
		keys = json_object_get_members (po);
		for (l = keys; l != NULL; l = l->next)
		{
			GValue val = G_VALUE_INIT;

			if (json_to_value (json_object_get_member (po, l->data), &val))
			{
				lrg_component_desc_set_value (desc, l->data, &val);
				g_value_unset (&val);
			}
		}
		g_list_free (keys);
	}

	lrg_editor_add_component (self->editor, node, desc);
	return json_bool_node (TRUE);
}

static JsonNode *
m_component_remove (LrgEditorServer  *self,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgNode *node = require_node (self, params, "node", error);

	if (node == NULL)
		return NULL;
	if (!lrg_editor_remove_component (self->editor, node, (guint) param_int (params, "index", 0)))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                     "No component at that index");
		return NULL;
	}
	return json_bool_node (TRUE);
}

static JsonNode *
m_component_set (LrgEditorServer  *self,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode     *node = require_node (self, params, "node", error);
	const gchar *key;
	GValue       val = G_VALUE_INIT;
	gboolean     ok;

	if (node == NULL || (key = require_string (params, "key", error)) == NULL)
		return NULL;

	if (!json_object_has_member (params, "value") ||
	    JSON_NODE_HOLDS_NULL (json_object_get_member (params, "value")))
	{
		ok = lrg_editor_set_component_value (self->editor, node,
		                                     (guint) param_int (params, "index", 0),
		                                     key, NULL, param_bool (params, "merge", FALSE));
	}
	else if (json_to_value (json_object_get_member (params, "value"), &val))
	{
		ok = lrg_editor_set_component_value (self->editor, node,
		                                     (guint) param_int (params, "index", 0),
		                                     key, &val, param_bool (params, "merge", FALSE));
		g_value_unset (&val);
	}
	else
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'value' must be a boolean, number or string");
		return NULL;
	}

	return json_bool_node (ok);
}

static JsonNode *
m_script_add (LrgEditorServer  *self,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgNode                    *node = require_node (self, params, "node", error);
	const gchar                *script;
	const gchar                *language;
	gint                        lang = LRG_SCRIPT_LANGUAGE_LUA;
	g_autoptr(LrgScriptBinding) binding = NULL;

	if (node == NULL || (script = require_string (params, "script", error)) == NULL)
		return NULL;

	language = param_string (params, "language");
	if (language != NULL && !enum_from_string (LRG_TYPE_SCRIPT_LANGUAGE, language, &lang))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Unknown script language '%s'", language);
		return NULL;
	}
	if (language == NULL)
	{
		/* Infer from the extension. */
		if (g_str_has_suffix (script, ".py"))
			lang = LRG_SCRIPT_LANGUAGE_PYTHON;
		else if (g_str_has_suffix (script, ".js"))
			lang = LRG_SCRIPT_LANGUAGE_GJS;
		else if (g_str_has_suffix (script, ".c"))
			lang = LRG_SCRIPT_LANGUAGE_CRISPY;
		else if (g_str_has_suffix (script, ".el"))
			lang = LRG_SCRIPT_LANGUAGE_ELISP;
		else if (g_str_has_suffix (script, ".so"))
			lang = LRG_SCRIPT_LANGUAGE_NATIVE;
	}

	binding = lrg_script_binding_new (lang, script);
	lrg_script_binding_set_enabled (binding, param_bool (params, "enabled", TRUE));
	lrg_editor_add_script (self->editor, node, binding);
	return json_bool_node (TRUE);
}

static JsonNode *
m_script_remove (LrgEditorServer  *self,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgNode *node = require_node (self, params, "node", error);

	if (node == NULL)
		return NULL;
	if (!lrg_editor_remove_script (self->editor, node, (guint) param_int (params, "index", 0)))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                     "No script at that index");
		return NULL;
	}
	return json_bool_node (TRUE);
}

static JsonNode *
open_file (LrgEditorServer  *self,
           const gchar      *path,
           gint              line,
           gint              col,
           GError          **error)
{
	g_autofree gchar *full = resolve_path (self, path);

	if (self->external == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "No external editor is configured");
		return NULL;
	}
	if (!lrg_external_editor_open (self->external, full, line, col, error))
		return NULL;
	return json_string_node (full);
}

static JsonNode *
m_script_open (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgNode   *node = require_node (self, params, "node", error);
	GPtrArray *scripts;
	guint      index;

	if (node == NULL)
		return NULL;

	scripts = lrg_node_get_scripts (node);
	index = (guint) param_int (params, "index", 0);
	if (index >= scripts->len ||
	    lrg_script_binding_get_script (g_ptr_array_index (scripts, index)) == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                     "No script at that index");
		return NULL;
	}

	return open_file (self, lrg_script_binding_get_script (g_ptr_array_index (scripts, index)),
	                  (gint) param_int (params, "line", 1), (gint) param_int (params, "col", 1),
	                  error);
}

static JsonNode *
m_file_open (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar *path = require_string (params, "path", error);

	if (path == NULL)
		return NULL;
	return open_file (self, path, (gint) param_int (params, "line", 1),
	                  (gint) param_int (params, "col", 1), error);
}

/* ---- selection ---- */

static JsonNode *
m_selection_get (LrgEditorServer  *self,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	return selection_json (self);
}

static JsonNode *
m_selection_set (LrgEditorServer  *self,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	g_autoptr(GPtrArray) nodes = g_ptr_array_new ();
	JsonNode            *arr_node;
	JsonArray           *arr;
	guint                i;

	if (params == NULL || !json_object_has_member (params, "nodes") ||
	    !JSON_NODE_HOLDS_ARRAY (arr_node = json_object_get_member (params, "nodes")))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'nodes' must be an array of node references");
		return NULL;
	}

	arr = json_node_get_array (arr_node);
	for (i = 0; i < json_array_get_length (arr); i++)
	{
		const gchar *ref = array_string (arr, i);
		LrgNode     *node = (ref != NULL) ? lrg_editor_find_node (self->editor, ref) : NULL;

		if (node == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No node '%s'", ref != NULL ? ref : "(non-string)");
			return NULL;
		}
		g_ptr_array_add (nodes, node);
	}

	lrg_editor_select_nodes (self->editor, nodes, param_bool (params, "additive", FALSE));
	return selection_json (self);
}

static JsonNode *
m_selection_clear (LrgEditorServer  *self,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	lrg_editor_select (self->editor, NULL, FALSE);
	return selection_json (self);
}

/* ---- edit ---- */

static JsonNode *
m_edit_undo (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	gboolean could = lrg_editor_can_undo (self->editor);

	lrg_editor_undo (self->editor);
	return json_bool_node (could);
}

static JsonNode *
m_edit_redo (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	gboolean could = lrg_editor_can_redo (self->editor);

	lrg_editor_redo (self->editor);
	return json_bool_node (could);
}

static JsonNode *
m_edit_history (LrgEditorServer  *self,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	JsonObject *obj = json_object_new ();
	JsonArray  *steps = json_array_new ();
	guint       i, n_undo, n_redo;

	n_undo = lrg_editor_get_undo_count (self->editor);
	n_redo = lrg_editor_get_redo_count (self->editor);

	/* Oldest first: applied steps, then the redoable ones. */
	for (i = 0; i < n_undo; i++)
	{
		g_autofree gchar *d = lrg_editor_command_describe (lrg_editor_get_undo_command (self->editor, i));

		json_array_add_string_element (steps, d);
	}
	for (i = 0; i < n_redo; i++)
	{
		g_autofree gchar *d = lrg_editor_command_describe (lrg_editor_get_redo_command (self->editor, i));

		json_array_add_string_element (steps, d);
	}
	json_object_set_array_member (obj, "steps", steps);
	json_object_set_int_member (obj, "current", n_undo);
	return json_object_node (obj);
}

static JsonNode *
m_edit_jump (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	gint64 to = param_int (params, "index", -1);

	if (to < 0)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'index' (number of applied steps) is required");
		return NULL;
	}
	lrg_editor_jump_to_history (self->editor, (guint) to);
	return json_bool_node (TRUE);
}

static JsonNode *
m_edit_copy (LrgEditorServer  *self,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	g_autofree gchar *data = lrg_editor_copy_selection (self->editor);

	return json_string_node (data);
}

static JsonNode *
m_edit_cut (LrgEditorServer  *self,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	g_autofree gchar *data = lrg_editor_cut_selection (self->editor);

	return json_string_node (data);
}

static JsonNode *
m_edit_paste (LrgEditorServer  *self,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	g_autoptr(GPtrArray) created = NULL;
	LrgNode             *parent;
	JsonArray           *out;
	guint                i;

	if (!optional_node (self, params, "parent", &parent, error))
		return NULL;

	created = lrg_editor_paste (self->editor, param_string (params, "data"), parent, error);
	if (created == NULL)
		return NULL;

	out = json_array_new ();
	for (i = 0; i < created->len; i++)
		json_array_add_string_element (out, lrg_node_get_guid (g_ptr_array_index (created, i)));
	return json_array_node (out);
}

/* ---- tools ---- */

static JsonNode *
m_tool_set (LrgEditorServer  *self,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	const gchar *s;
	gint         v;

	if ((s = param_string (params, "tool")) != NULL)
	{
		if (!enum_from_string (LRG_TYPE_EDITOR_TOOL, s, &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown tool '%s'", s);
			return NULL;
		}
		lrg_editor_set_tool (self->editor, v);
	}
	if ((s = param_string (params, "space")) != NULL)
	{
		if (!enum_from_string (LRG_TYPE_EDITOR_SPACE, s, &v))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown space '%s'", s);
			return NULL;
		}
		lrg_editor_set_space (self->editor, v);
	}
	if (param_has (params, "snap"))
		lrg_editor_set_snap_enabled (self->editor, param_bool (params, "snap", FALSE));

	return m_editor_info (self, NULL, NULL, error);
}

/* ---- prefabs ---- */

static JsonNode *
m_prefab_instantiate (LrgEditorServer  *self,
                      JsonObject       *params,
                      gpointer          user_data,
                      GError          **error)
{
	const gchar      *path = require_string (params, "path", error);
	g_autofree gchar *full = NULL;
	LrgNode          *parent;
	LrgNode          *instance;

	if (path == NULL || !optional_node (self, params, "parent", &parent, error))
		return NULL;

	full = resolve_path (self, path);
	instance = lrg_editor_instantiate_prefab (self->editor, full, parent, error);
	return (instance != NULL) ? lrg_editor_server_node_to_json (instance, FALSE) : NULL;
}

static JsonNode *
m_prefab_save (LrgEditorServer  *self,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgNode          *node = require_node (self, params, "node", error);
	const gchar      *path;
	g_autofree gchar *full = NULL;

	if (node == NULL || (path = require_string (params, "path", error)) == NULL)
		return NULL;

	full = resolve_path (self, path);
	return lrg_prefab_save (node, full, error) ? json_string_node (full) : NULL;
}

/* ---- log ---- */

static JsonNode *
record_to_json (const LrgEditorLogRecord *r)
{
	JsonObject *obj = json_object_new ();

	json_object_set_int_member (obj, "serial", (gint64) r->serial);
	json_object_set_int_member (obj, "time", r->timestamp);
	json_object_set_string_member (obj, "level", lrg_editor_log_level_to_string (r->level));
	json_object_set_member (obj, "domain", json_string_node (r->domain));
	json_object_set_string_member (obj, "message", r->message);
	if (r->file != NULL)
	{
		json_object_set_string_member (obj, "file", r->file);
		json_object_set_int_member (obj, "line", r->line);
	}
	return json_object_node (obj);
}

static JsonNode *
m_log_tail (LrgEditorServer  *self,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	JsonArray *out = json_array_new ();
	guint      n, count, i;

	if (self->log == NULL)
		return json_array_node (out);

	n = lrg_editor_log_get_n_records (self->log);
	count = (guint) CLAMP (param_int (params, "count", 100), 0, (gint64) n);
	for (i = n - count; i < n; i++)
	{
		const LrgEditorLogRecord *r = lrg_editor_log_get_record (self->log, i);

		if (r != NULL)
			json_array_add_element (out, record_to_json (r));
	}
	return json_array_node (out);
}

static void
register_builtin (LrgEditorServer *self)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} table[] = {
		{ "hello", m_hello },
		{ "ping", m_ping },
		{ "methods", m_methods },
		{ "editor.info", m_editor_info },
		{ "level.new", m_level_new },
		{ "level.open", m_level_open },
		{ "level.save", m_level_save },
		{ "level.reload", m_level_reload },
		{ "level.serialize", m_level_serialize },
		{ "node.tree", m_node_tree },
		{ "node.list", m_node_list },
		{ "node.get", m_node_get },
		{ "node.find", m_node_find },
		{ "node.set", m_node_set },
		{ "node.create", m_node_create },
		{ "node.delete", m_node_delete },
		{ "node.duplicate", m_node_duplicate },
		{ "node.move", m_node_move },
		{ "node.rename", m_node_rename },
		{ "component.add", m_component_add },
		{ "component.remove", m_component_remove },
		{ "component.set", m_component_set },
		{ "script.add", m_script_add },
		{ "script.remove", m_script_remove },
		{ "script.open", m_script_open },
		{ "file.open", m_file_open },
		{ "selection.get", m_selection_get },
		{ "selection.set", m_selection_set },
		{ "selection.clear", m_selection_clear },
		{ "edit.undo", m_edit_undo },
		{ "edit.redo", m_edit_redo },
		{ "edit.history", m_edit_history },
		{ "edit.jump", m_edit_jump },
		{ "edit.copy", m_edit_copy },
		{ "edit.cut", m_edit_cut },
		{ "edit.paste", m_edit_paste },
		{ "tool.set", m_tool_set },
		{ "prefab.instantiate", m_prefab_instantiate },
		{ "prefab.save", m_prefab_save },
		{ "log.tail", m_log_tail },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
		lrg_editor_server_register_method (self, table[i].name, table[i].func, NULL, NULL);
}

/* ==========================================================================
 * 6. Events
 * ========================================================================== */

void
lrg_editor_server_emit_event (LrgEditorServer *self,
                              const gchar     *event,
                              JsonNode        *data)
{
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar   *line = NULL;
	JsonObject         *obj;
	GList              *l, *snapshot;

	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));
	g_return_if_fail (event != NULL);

	obj = json_object_new ();
	json_object_set_string_member (obj, "event", event);
	json_object_set_member (obj, "data", data != NULL ? data : json_node_new (JSON_NODE_NULL));
	root = json_object_node (obj);

	if (self->clients == NULL)
		return;

	line = node_to_line (root);

	/* Sending can drop a client; iterate a referenced snapshot. */
	snapshot = g_list_copy (self->clients);
	for (l = snapshot; l != NULL; l = l->next)
		client_ref (l->data);
	for (l = snapshot; l != NULL; l = l->next)
	{
		Client *c = l->data;

		if (client_wants (c, event))
			client_send (c, line);
	}
	g_list_free_full (snapshot, (GDestroyNotify) client_unref);
}

static gboolean
emit_changed_idle (gpointer user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	JsonObject      *obj = json_object_new ();
	guint            n_undo;

	self->changed_idle = 0;

	n_undo = lrg_editor_get_undo_count (self->editor);
	json_object_set_boolean_member (obj, "dirty", lrg_editor_is_dirty (self->editor));
	json_object_set_int_member (obj, "undo", n_undo);
	json_object_set_int_member (obj, "redo", lrg_editor_get_redo_count (self->editor));
	json_object_set_int_member (obj, "nodes",
	                            lrg_level_count_nodes (lrg_editor_get_level (self->editor)));
	if (n_undo > 0)
	{
		g_autofree gchar *last = lrg_editor_command_describe (
			lrg_editor_get_undo_command (self->editor, n_undo - 1));

		json_object_set_string_member (obj, "last", last);
	}
	lrg_editor_server_emit_event (self, "changed", json_object_node (obj));

	return G_SOURCE_REMOVE;
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);

	/* Coalesce bursts (a drag emits per frame) into one event. */
	if (self->changed_idle == 0 && self->clients != NULL)
		self->changed_idle = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, emit_changed_idle,
		                                      g_object_ref (self), g_object_unref);
}

static void
on_selection_changed (LrgEditorSelection *sel,
                      gpointer            user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);

	if (self->clients != NULL)
		lrg_editor_server_emit_event (self, "selection", selection_json (self));
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	JsonObject      *obj = json_object_new ();

	json_object_set_member (obj, "path", json_string_node (lrg_editor_get_path (editor)));
	json_object_set_member (obj, "name", json_string_node (lrg_level_get_name (level)));
	lrg_editor_server_emit_event (self, "level", json_object_node (obj));
}

static void
on_saved (LrgEditor   *editor,
          const gchar *path,
          gpointer     user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	JsonObject      *obj = json_object_new ();

	json_object_set_string_member (obj, "path", path);
	lrg_editor_server_emit_event (self, "saved", json_object_node (obj));
}

static void
on_tool_notify (GObject    *object,
                GParamSpec *pspec,
                gpointer    user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	JsonObject      *obj = json_object_new ();

	json_object_set_string_member (obj, "tool",
	                               enum_nick (LRG_TYPE_EDITOR_TOOL, lrg_editor_get_tool (self->editor)));
	json_object_set_string_member (obj, "space",
	                               enum_nick (LRG_TYPE_EDITOR_SPACE, lrg_editor_get_space (self->editor)));
	json_object_set_boolean_member (obj, "snap", lrg_editor_get_snap_enabled (self->editor));
	lrg_editor_server_emit_event (self, "tool", json_object_node (obj));
}

static void
on_log_message (LrgEditorLog             *log,
                const LrgEditorLogRecord *record,
                gpointer                  user_data)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (user_data);
	GList           *l;
	gboolean         any = FALSE;

	/* Avoid building JSON nobody asked for. */
	for (l = self->clients; l != NULL && !any; l = l->next)
		any = client_wants (l->data, "log");
	if (any)
		lrg_editor_server_emit_event (self, "log", record_to_json (record));
}

/* Offers an open-file request to the most recently active client that
 * declared the "open-file" capability (normally a connected Emacs). */
static gboolean
on_external_open_file (LrgExternalEditor *external,
                       const gchar       *file,
                       gint               line,
                       gint               column,
                       gpointer           user_data)
{
	LrgEditorServer    *self = LRG_EDITOR_SERVER (user_data);
	Client             *best = NULL;
	GList              *l;
	JsonObject         *obj;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar   *msg = NULL;

	for (l = self->clients; l != NULL; l = l->next)
	{
		Client *c = l->data;

		if (!c->closed && g_hash_table_contains (c->capabilities, "open-file") &&
		    (best == NULL || c->last_active > best->last_active))
			best = c;
	}
	if (best == NULL)
		return FALSE;

	obj = json_object_new ();
	json_object_set_string_member (obj, "event", "open-file");
	{
		JsonObject *data = json_object_new ();

		json_object_set_string_member (data, "file", file);
		json_object_set_int_member (data, "line", line);
		json_object_set_int_member (data, "col", column);
		json_object_set_object_member (obj, "data", data);
	}
	root = json_object_node (obj);
	msg = node_to_line (root);
	client_send (best, msg);
	return TRUE;
}

/* ==========================================================================
 * 7. GObject, lifecycle and public API
 * ========================================================================== */

static void
lrg_editor_server_dispose (GObject *object)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (object);
	guint            i;

	lrg_editor_server_stop (self);

	if (self->editor != NULL)
	{
		for (i = 0; i < G_N_ELEMENTS (self->editor_ids); i++)
			g_clear_signal_handler (&self->editor_ids[i], self->editor);
		g_clear_signal_handler (&self->selection_id, lrg_editor_get_selection (self->editor));
	}
	lrg_editor_server_set_external_editor (self, NULL);
	lrg_editor_server_set_log (self, NULL);
	g_clear_handle_id (&self->changed_idle, g_source_remove);

	G_OBJECT_CLASS (lrg_editor_server_parent_class)->dispose (object);
}

static void
lrg_editor_server_finalize (GObject *object)
{
	LrgEditorServer *self = LRG_EDITOR_SERVER (object);

	g_clear_object (&self->editor);
	g_free (self->project_dir);
	g_hash_table_unref (self->methods);

	G_OBJECT_CLASS (lrg_editor_server_parent_class)->finalize (object);
}

static void
lrg_editor_server_class_init (LrgEditorServerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_editor_server_dispose;
	object_class->finalize = lrg_editor_server_finalize;

	/**
	 * LrgEditorServer::client-connected:
	 * @self: the server
	 * @client_id: the new client's id
	 *
	 * Emitted when a client connects.
	 */
	signals[SIGNAL_CLIENT_CONNECTED] =
		g_signal_new ("client-connected",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgEditorServer::client-disconnected:
	 * @self: the server
	 * @client_id: the departed client's id
	 *
	 * Emitted when a client disconnects or is dropped.
	 */
	signals[SIGNAL_CLIENT_DISCONNECTED] =
		g_signal_new ("client-disconnected",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_editor_server_init (LrgEditorServer *self)
{
	self->methods = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, method_entry_free);
}

LrgEditorServer *
lrg_editor_server_new (LrgEditor *editor)
{
	LrgEditorServer *self;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_SERVER, NULL);
	self->editor = g_object_ref (editor);
	register_builtin (self);

	self->editor_ids[0] = g_signal_connect (editor, "changed", G_CALLBACK (on_editor_changed), self);
	self->editor_ids[1] = g_signal_connect (editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	self->editor_ids[2] = g_signal_connect (editor, "saved", G_CALLBACK (on_saved), self);
	self->editor_ids[3] = g_signal_connect (editor, "notify::tool", G_CALLBACK (on_tool_notify), self);
	self->editor_ids[4] = g_signal_connect (editor, "notify::space", G_CALLBACK (on_tool_notify), self);
	self->editor_ids[5] = g_signal_connect (editor, "notify::snap-enabled", G_CALLBACK (on_tool_notify), self);
	self->selection_id = g_signal_connect (lrg_editor_get_selection (editor), "changed",
	                                       G_CALLBACK (on_selection_changed), self);
	return self;
}

LrgEditor *
lrg_editor_server_get_editor (LrgEditorServer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), NULL);

	return self->editor;
}

void
lrg_editor_server_set_external_editor (LrgEditorServer   *self,
                                       LrgExternalEditor *external)
{
	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));

	if (self->external == external)
		return;

	if (self->external != NULL)
		g_clear_signal_handler (&self->external_id, self->external);
	g_clear_object (&self->external);

	if (external != NULL)
	{
		self->external = g_object_ref (external);
		self->external_id = g_signal_connect (external, "open-file",
		                                      G_CALLBACK (on_external_open_file), self);
	}
}

void
lrg_editor_server_set_log (LrgEditorServer *self,
                           LrgEditorLog    *log)
{
	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));

	if (self->log == log)
		return;

	if (self->log != NULL)
		g_clear_signal_handler (&self->log_id, self->log);
	g_clear_object (&self->log);

	if (log != NULL)
	{
		self->log = g_object_ref (log);
		self->log_id = g_signal_connect (log, "message", G_CALLBACK (on_log_message), self);
	}
}

void
lrg_editor_server_set_project_dir (LrgEditorServer *self,
                                   const gchar     *dir)
{
	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));

	g_free (self->project_dir);
	self->project_dir = g_strdup (dir);
}

const gchar *
lrg_editor_server_get_project_dir (LrgEditorServer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), NULL);

	return self->project_dir;
}

gchar *
lrg_editor_server_get_default_socket_path (void)
{
	const gchar      *env = g_getenv ("LRG_EDITOR_SOCKET");
	g_autofree gchar *name = NULL;

	if (env != NULL && *env != '\0')
		return g_strdup (env);

	name = g_strdup_printf ("editor-%d.sock", (gint) getpid ());
	return g_build_filename (g_get_user_runtime_dir (), "libregnum", name, NULL);
}

/* TRUE if something is accepting connections on @path. */
static gboolean
socket_is_live (const gchar *path)
{
	g_autoptr(GSocketClient)     client = g_socket_client_new ();
	g_autoptr(GSocketAddress)    addr = g_unix_socket_address_new (path);
	g_autoptr(GSocketConnection) conn = NULL;

	conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (addr), NULL, NULL);
	return conn != NULL;
}

gboolean
lrg_editor_server_start (LrgEditorServer  *self,
                         const gchar      *socket_path,
                         GError          **error)
{
	g_autofree gchar         *path = NULL;
	g_autofree gchar         *dir = NULL;
	g_autoptr(GSocketAddress) addr = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), FALSE);

	if (self->service != NULL)
		return TRUE;

	path = (socket_path != NULL) ? g_strdup (socket_path)
	                             : lrg_editor_server_get_default_socket_path ();

	/* sockaddr_un holds ~108 bytes; GLib would silently truncate. */
	if (strlen (path) >= sizeof (((struct sockaddr_un *) NULL)->sun_path))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FILENAME_TOO_LONG,
		             "Socket path is too long (%" G_GSIZE_FORMAT " bytes, the limit is %" G_GSIZE_FORMAT "): %s",
		             strlen (path), sizeof (((struct sockaddr_un *) NULL)->sun_path) - 1, path);
		return FALSE;
	}
	dir = g_path_get_dirname (path);
	if (g_mkdir_with_parents (dir, 0700) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		             "Cannot create %s: %s", dir, g_strerror (errno));
		return FALSE;
	}

	/* Replace a stale socket, never a live one. */
	if (g_file_test (path, G_FILE_TEST_EXISTS))
	{
		if (socket_is_live (path))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE,
			             "Another editor is listening on %s", path);
			return FALSE;
		}
		g_unlink (path);
	}

	self->service = g_socket_service_new ();
	addr = g_unix_socket_address_new (path);
	if (!g_socket_listener_add_address (G_SOCKET_LISTENER (self->service), addr,
	                                    G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
	                                    NULL, NULL, error))
	{
		g_clear_object (&self->service);
		return FALSE;
	}
	g_chmod (path, 0600);

	g_signal_connect (self->service, "incoming", G_CALLBACK (on_incoming), self);
	g_socket_service_start (self->service);
	self->socket_path = g_steal_pointer (&path);

	return TRUE;
}

void
lrg_editor_server_stop (LrgEditorServer *self)
{
	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));

	while (self->clients != NULL)
		client_close (self->clients->data);

	if (self->service != NULL)
	{
		g_socket_service_stop (self->service);
		g_socket_listener_close (G_SOCKET_LISTENER (self->service));
		g_signal_handlers_disconnect_by_data (self->service, self);
		g_clear_object (&self->service);
	}

	if (self->socket_path != NULL)
	{
		g_unlink (self->socket_path);
		g_clear_pointer (&self->socket_path, g_free);
	}
}

const gchar *
lrg_editor_server_get_socket_path (LrgEditorServer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), NULL);

	return self->socket_path;
}

guint
lrg_editor_server_get_n_clients (LrgEditorServer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), 0);

	return g_list_length (self->clients);
}

gboolean
lrg_editor_server_has_capability (LrgEditorServer *self,
                                  const gchar     *capability)
{
	GList *l;

	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), FALSE);
	g_return_val_if_fail (capability != NULL, FALSE);

	for (l = self->clients; l != NULL; l = l->next)
		if (g_hash_table_contains (((Client *) l->data)->capabilities, capability))
			return TRUE;

	return FALSE;
}

void
lrg_editor_server_register_method (LrgEditorServer       *self,
                                   const gchar           *name,
                                   LrgEditorServerMethod  method,
                                   gpointer               user_data,
                                   GDestroyNotify         destroy)
{
	MethodEntry *entry;

	g_return_if_fail (LRG_IS_EDITOR_SERVER (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (method != NULL);

	entry = g_new0 (MethodEntry, 1);
	entry->func = method;
	entry->user_data = user_data;
	entry->destroy = destroy;
	g_hash_table_replace (self->methods, g_strdup (name), entry);
}

GList *
lrg_editor_server_list_methods (LrgEditorServer *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SERVER (self), NULL);

	return g_list_sort (g_hash_table_get_keys (self->methods), (GCompareFunc) g_strcmp0);
}
