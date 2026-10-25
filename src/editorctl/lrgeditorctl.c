/* lrgeditorctl.c - control running libregnum editors
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * `lrgeditorctl` is to `lrgeditor` what kubectl is to a cluster: verbs
 * (get, describe, create, delete, set, edit, apply, ...) acting on
 * resources (nodes, selection, actions, problems, assets, ...) of a
 * running editor session, with -o yaml|json|table|wide|name output. It
 * talks to the editor's JSON-lines IPC socket and doubles as an MCP
 * server (`lrgeditorctl mcp`) so AI agents can drive the editor.
 */

#include "lrgeditorctl-client.h"
#include "lrgeditorctl-format.h"
#ifdef LRG_EDITORCTL_HAVE_MCP
#include "lrgeditorctl-mcp.h"
#endif
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef LRG_EDITORCTL_VERSION
#define LRG_EDITORCTL_VERSION "0.0.0"
#endif

/* ==========================================================================
 * Options
 * ========================================================================== */

static gchar    *opt_session  = NULL;
static gchar    *opt_output   = NULL;
static gint      opt_timeout  = 10;
static gchar    *opt_parent   = NULL;
static gchar    *opt_kind     = NULL;
static gchar    *opt_primitive = NULL;
static gint      opt_index    = -1;
static gboolean  opt_keep_world = FALSE;
static gboolean  opt_add      = FALSE;
static gboolean  opt_clear    = FALSE;
static gchar    *opt_filename = NULL;
static gboolean  opt_watch    = FALSE;
static gint      opt_tail     = 50;
static gboolean  opt_window   = FALSE;
static gboolean  opt_force    = FALSE;
static gint      opt_level    = 0;
static gchar    *opt_type     = NULL;
static gboolean  opt_license  = FALSE;
static gboolean  opt_version  = FALSE;
static gchar   **opt_args     = NULL;

static LrgCtlFormat output = LRG_CTL_FORMAT_DEFAULT;

static const GOptionEntry entries[] = {
	{ "session", 's', 0, G_OPTION_ARG_STRING, &opt_session,
	  "Editor to talk to: socket path, PID, or part of the project/level path (default: $LRG_EDITOR_SOCKET, else the newest)", "SESSION" },
	{ "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output,
	  "Output format: yaml, json, table, wide, name", "FORMAT" },
	{ "timeout", 0, 0, G_OPTION_ARG_INT, &opt_timeout, "Seconds to wait for a reply (default 10)", "SEC" },
	{ "parent", 'p', 0, G_OPTION_ARG_STRING, &opt_parent, "Parent node (create, move)", "NODE" },
	{ "kind", 'k', 0, G_OPTION_ARG_STRING, &opt_kind,
	  "Visual kind for create: none, primitive, mesh-asset, sprite, tilemap, light, camera, audio-emitter", "KIND" },
	{ "primitive", 0, 0, G_OPTION_ARG_STRING, &opt_primitive,
	  "Primitive for create: cube, uv-sphere, plane, cylinder, cone, torus, ...", "SHAPE" },
	{ "index", 'i', 0, G_OPTION_ARG_INT, &opt_index, "Position among siblings (create, move)", "N" },
	{ "keep-world", 0, 0, G_OPTION_ARG_NONE, &opt_keep_world, "Keep the world transform when moving", NULL },
	{ "add", 0, 0, G_OPTION_ARG_NONE, &opt_add, "Add to the selection instead of replacing it", NULL },
	{ "clear", 0, 0, G_OPTION_ARG_NONE, &opt_clear, "Clear the selection", NULL },
	{ "filename", 'f', 0, G_OPTION_ARG_FILENAME, &opt_filename, "YAML/JSON file for apply ('-' for stdin)", "FILE" },
	{ "watch", 'w', 0, G_OPTION_ARG_NONE, &opt_watch, "Keep streaming (logs)", NULL },
	{ "tail", 0, 0, G_OPTION_ARG_INT, &opt_tail, "Log records to show first (default 50)", "N" },
	{ "window", 0, 0, G_OPTION_ARG_NONE, &opt_window, "Screenshot the whole window, not just the viewport", NULL },
	{ "force", 0, 0, G_OPTION_ARG_NONE, &opt_force, "Discard unsaved changes (open, new, quit)", NULL },
	{ "level", 'l', 0, G_OPTION_ARG_INT, &opt_level, "Toast level for notify: 0 info, 1 success, 2 warning, 3 error", "N" },
	{ "type", 't', 0, G_OPTION_ARG_STRING, &opt_type, "Asset type filter for get assets", "TYPE" },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, "Print the version and exit", NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_args, NULL, "COMMAND [RESOURCE] [ARGS...]" },
	G_OPTION_ENTRY_NULL
};

static const gchar *description =
	"Commands:\n"
	"  sessions                         list running editors\n"
	"  get RESOURCE [NAME...]           nodes, selection, actions, problems, assets, project,\n"
	"                                   info, view, history, methods, state, play, sessions\n"
	"  describe node NAME               everything about a node\n"
	"  create node NAME [key=value...]  --parent, --kind, --primitive, --index\n"
	"  delete node NAME...\n"
	"  set node NAME key=value...       name, location, rotation_degrees, scale, visible, locked\n"
	"  rename node NAME NEW_NAME\n"
	"  move node NAME                   --parent (omit for top level), --index, --keep-world\n"
	"  duplicate node NAME\n"
	"  edit node NAME                   edit as YAML in $VISUAL/$EDITOR, apply on save\n"
	"  apply -f FILE                    create/update nodes from YAML or JSON ('-' = stdin)\n"
	"  select NAME... | select --clear  --add to extend the selection\n"
	"  run ACTION                       run an editor action (see `get actions`)\n"
	"  play [start|pause|step|stop|state]\n"
	"  stop                             stop play-in-editor (restores the level)\n"
	"  game start|stop                  run the game as its own process\n"
	"  undo | redo | save [PATH] | reload | validate\n"
	"  open LEVEL [--force] | new [NAME] [--force]\n"
	"  view [top|front|right|bottom|back|left|persp|all] | view focus NAME | view set key=value...\n"
	"  screenshot FILE.png [--window]\n"
	"  logs [--tail N] [-w]             editor log; -w follows\n"
	"  events [TYPE...]                 stream events (changed, selection, level, saved, log, ...)\n"
	"  call METHOD [key=value...|JSON]  any IPC method\n"
	"  notify MESSAGE [--level N]       toast in the editor\n"
	"  quit [--force]\n"
	"  mcp                              serve MCP on stdio (for AI agents)\n"
	"  completion bash                  shell completion script\n"
	"\n"
	"Examples:\n"
	"  lrgeditorctl sessions\n"
	"  lrgeditorctl get nodes\n"
	"  lrgeditorctl get node Props/Crate -o yaml\n"
	"  lrgeditorctl create node Crate --parent Props --primitive cube location=2,0.5,-3\n"
	"  lrgeditorctl set node Props/Crate rotation_degrees=0,45,0 scale=2,2,2\n"
	"  lrgeditorctl edit node Props/Crate\n"
	"  lrgeditorctl get node Props/Crate -o yaml > crate.yaml; lrgeditorctl apply -f crate.yaml\n"
	"  lrgeditorctl run view.frame-all\n"
	"  lrgeditorctl -s mygame screenshot /tmp/view.png --window\n"
	"  lrgeditorctl logs -w\n"
	"  claude mcp add libregnum -- lrgeditorctl mcp\n";

static const gchar *license_text =
	"lrgeditorctl - control running libregnum editors\n"
	"Copyright (C) 2025  Zach Podbielniak\n"
	"\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gchar *session_socket = NULL;

static JsonNode *
call (const gchar  *method,
      JsonObject   *params,
      GError      **error)
{
	if (session_socket == NULL)
	{
		session_socket = lrg_ctl_resolve_session (opt_session, error);
		if (session_socket == NULL)
			return NULL;
	}
	return lrg_ctl_call (session_socket, method, params, (guint) MAX (opt_timeout, 1) * 1000, error);
}

/* A call whose result only matters for errors. */
static gboolean
call_ok (const gchar  *method,
         JsonObject   *params,
         GError      **error)
{
	g_autoptr(JsonNode) r = call (method, params, error);

	return r != NULL;
}

static gboolean
usage_error (GError      **error,
             const gchar  *format,
             ...) G_GNUC_PRINTF (2, 3);

static gboolean
usage_error (GError      **error,
             const gchar  *format,
             ...)
{
	va_list           ap;
	g_autofree gchar *msg = NULL;

	va_start (ap, format);
	msg = g_strdup_vprintf (format, ap);
	va_end (ap);
	g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_USAGE, msg);
	return FALSE;
}

static JsonObject *
node_param (const gchar *node)
{
	JsonObject *p = json_object_new ();

	json_object_set_string_member (p, "node", node);
	return p;
}

static const gchar *
str_member (JsonObject  *o,
            const gchar *name)
{
	JsonNode *n;

	if (o == NULL || !json_object_has_member (o, name))
		return NULL;
	n = json_object_get_member (o, name);
	return (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
	       ? json_node_get_string (n) : NULL;
}

/* GUID of a node given by path or GUID (paths change on rename/move). */
static gchar *
node_guid (const gchar  *node,
           GError      **error)
{
	g_autoptr(JsonObject) p = node_param (node);
	g_autoptr(JsonNode)   n = call ("node.get", p, error);

	if (n == NULL || !JSON_NODE_HOLDS_OBJECT (n))
		return NULL;
	return g_strdup (str_member (json_node_get_object (n), "guid"));
}

/* Current path of a node, for reporting after an edit. */
static gchar *
node_path (const gchar *guid)
{
	g_autoptr(JsonObject) p = node_param (guid);
	g_autoptr(JsonNode)   n = call ("node.get", p, NULL);

	if (n == NULL || !JSON_NODE_HOLDS_OBJECT (n))
		return g_strdup (guid);
	return g_strdup (str_member (json_node_get_object (n), "path"));
}

/* Resource names and their aliases. */
static const gchar *
canonical_resource (const gchar *r)
{
	static const gchar *table[][2] = {
		{ "node", "nodes" }, { "no", "nodes" }, { "nodes", "nodes" },
		{ "selection", "selection" }, { "sel", "selection" },
		{ "action", "actions" }, { "actions", "actions" },
		{ "problem", "problems" }, { "problems", "problems" },
		{ "asset", "assets" }, { "assets", "assets" },
		{ "project", "project" }, { "info", "info" }, { "editor", "info" }, { "level", "info" },
		{ "view", "view" }, { "history", "history" }, { "methods", "methods" },
		{ "state", "state" }, { "ui", "state" }, { "play", "play" },
		{ "session", "sessions" }, { "sessions", "sessions" }
	};
	gsize i;

	for (i = 0; r != NULL && i < G_N_ELEMENTS (table); i++)
		if (g_strcmp0 (r, table[i][0]) == 0)
			return table[i][1];
	return NULL;
}

/* node.tree -> flat rows {path, name, guid, kind, visible, locked, children}. */
static void
flatten_tree (JsonArray   *nodes,
              const gchar *prefix,
              JsonArray   *out)
{
	guint i;

	for (i = 0; nodes != NULL && i < json_array_get_length (nodes); i++)
	{
		JsonObject       *n = json_array_get_object_element (nodes, i);
		JsonObject       *row = json_object_new ();
		const gchar      *name = str_member (n, "name");
		g_autofree gchar *path = (prefix != NULL) ? g_strdup_printf ("%s/%s", prefix, name != NULL ? name : "?")
		                                          : g_strdup (name != NULL ? name : "?");
		GList            *members, *l;

		members = json_object_get_members (n);
		for (l = members; l != NULL; l = l->next)
			if (g_strcmp0 (l->data, "nodes") != 0)
				json_object_set_member (row, l->data, json_node_copy (json_object_get_member (n, l->data)));
		g_list_free (members);
		json_object_set_string_member (row, "path", path);
		json_array_add_object_element (out, row);

		if (json_object_has_member (n, "nodes"))
			flatten_tree (json_object_get_array_member (n, "nodes"), path, out);
	}
}

static gchar *
format_time (gint64 usec)
{
	g_autoptr(GDateTime) t = g_date_time_new_from_unix_local_usec (usec);

	return (t != NULL) ? g_date_time_format (t, "%H:%M:%S") : g_strdup ("-");
}

/* ==========================================================================
 * get / describe / sessions
 * ========================================================================== */

static gboolean
cmd_sessions (GError **error)
{
	g_autoptr(GPtrArray) sessions = lrg_ctl_list_sessions ();
	g_autoptr(JsonArray) rows = json_array_new ();
	g_autofree gchar    *current = lrg_ctl_resolve_session (opt_session, NULL);
	static const LrgCtlColumn columns[] = {
		{ "CURRENT", "current", FALSE }, { "PID", "pid", FALSE }, { "PROJECT", "project", FALSE },
		{ "LEVEL", "level", FALSE }, { "SOCKET", "socket", TRUE }
	};
	guint i;

	for (i = 0; i < sessions->len; i++)
	{
		LrgCtlSession *s = g_ptr_array_index (sessions, i);
		JsonObject    *o = json_object_new ();

		json_object_set_string_member (o, "current", g_strcmp0 (s->socket, current) == 0 ? "*" : "");
		json_object_set_int_member (o, "pid", s->pid);
		json_object_set_string_member (o, "project", s->project != NULL ? s->project : "-");
		json_object_set_string_member (o, "level", s->level != NULL ? s->level : "-");
		json_object_set_string_member (o, "socket", s->socket);
		json_array_add_object_element (rows, o);
	}
	if (sessions->len == 0 && output != LRG_CTL_FORMAT_JSON && output != LRG_CTL_FORMAT_YAML)
	{
		g_printerr ("No running editors.\n");
		return TRUE;
	}
	lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "socket");
	return TRUE;
}

static gboolean
cmd_get (gchar   **args,
         GError  **error)
{
	const gchar *res = canonical_resource (args[0]);
	guint        i;

	if (res == NULL)
		return usage_error (error, "Unknown resource '%s' (nodes, selection, actions, problems, assets, project, "
		                    "info, view, history, methods, state, play, sessions)", args[0] != NULL ? args[0] : "");

	if (g_strcmp0 (res, "sessions") == 0)
		return cmd_sessions (error);

	/* get node NAME...: each node as YAML (a list when several). */
	if (g_strcmp0 (res, "nodes") == 0 && args[1] != NULL)
	{
		g_autoptr(JsonArray) all = json_array_new ();

		for (i = 1; args[i] != NULL; i++)
		{
			g_autoptr(JsonObject) p = node_param (args[i]);
			JsonNode             *n = call ("node.get", p, error);

			if (n == NULL)
				return FALSE;
			json_array_add_element (all, n);
		}
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (all); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (all, i), "path"));
			return TRUE;
		}
		if (json_array_get_length (all) == 1)
			lrg_ctl_print (json_array_get_element (all, 0), output);
		else
		{
			g_autoptr(JsonNode) list = json_node_new (JSON_NODE_ARRAY);

			json_node_set_array (list, all);
			lrg_ctl_print (list, output);
		}
		return TRUE;
	}

	if (g_strcmp0 (res, "nodes") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "PATH", "path", FALSE }, { "KIND", "kind", FALSE }, { "VISIBLE", "visible", FALSE },
			{ "LOCKED", "locked", FALSE }, { "CHILDREN", "children", FALSE }, { "GUID", "guid", TRUE }
		};
		g_autoptr(JsonNode)  tree = call ("node.tree", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();

		if (tree == NULL)
			return FALSE;
		if (output == LRG_CTL_FORMAT_YAML || output == LRG_CTL_FORMAT_JSON)
		{
			lrg_ctl_print (tree, output);
			return TRUE;
		}
		flatten_tree (json_node_get_array (tree), NULL, rows);
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (rows); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (rows, i), "path"));
			return TRUE;
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "selection") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "PATH", "path", FALSE }, { "PRIMARY", "primary", FALSE }, { "GUID", "guid", TRUE }
		};
		g_autoptr(JsonNode)  sel = call ("selection.get", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();
		JsonArray           *guids;
		const gchar         *primary;

		if (sel == NULL)
			return FALSE;
		guids = json_object_get_array_member (json_node_get_object (sel), "nodes");
		primary = str_member (json_node_get_object (sel), "primary");
		for (i = 0; guids != NULL && i < json_array_get_length (guids); i++)
		{
			const gchar          *guid = json_array_get_string_element (guids, i);
			g_autoptr(JsonObject) p = node_param (guid);
			g_autoptr(JsonNode)   n = call ("node.get", p, NULL);
			JsonObject           *row = json_object_new ();

			json_object_set_string_member (row, "guid", guid);
			json_object_set_string_member (row, "path", n != NULL ? str_member (json_node_get_object (n), "path") : guid);
			json_object_set_string_member (row, "primary", g_strcmp0 (guid, primary) == 0 ? "*" : "");
			json_array_add_object_element (rows, row);
		}
		if (output == LRG_CTL_FORMAT_NAME)
		{
			for (i = 0; i < json_array_get_length (rows); i++)
				printf ("node/%s\n", str_member (json_array_get_object_element (rows, i), "path"));
			return TRUE;
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "actions") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "ID", "id", FALSE }, { "LABEL", "label", FALSE }, { "SHORTCUT", "shortcut", FALSE },
			{ "ENABLED", "enabled", FALSE }, { "CATEGORY", "category", TRUE }, { "CHECKED", "checked", TRUE }
		};
		g_autoptr(JsonNode) list = call ("action.list", NULL, error);

		if (list == NULL)
			return FALSE;
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "id");
		return TRUE;
	}

	if (g_strcmp0 (res, "problems") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "SEVERITY", "severity", FALSE }, { "CODE", "code", FALSE }, { "PATH", "path", FALSE },
			{ "MESSAGE", "message", FALSE }, { "FILE", "file", TRUE }
		};
		g_autoptr(JsonNode) list = call ("level.validate", NULL, error);

		if (list == NULL)
			return FALSE;
		if (json_array_get_length (json_node_get_array (list)) == 0 && output == LRG_CTL_FORMAT_DEFAULT)
		{
			g_printerr ("No problems.\n");
			return TRUE;
		}
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "code");
		return TRUE;
	}

	if (g_strcmp0 (res, "assets") == 0)
	{
		static const LrgCtlColumn columns[] = { { "PATH", "path", FALSE }, { "TYPE", "type", FALSE } };
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   list = NULL;

		if (opt_type != NULL)
			json_object_set_string_member (p, "type", opt_type);
		list = call ("asset.list", p, error);
		if (list == NULL)
			return FALSE;
		lrg_ctl_print_table (json_node_get_array (list), columns, G_N_ELEMENTS (columns), output, "path");
		return TRUE;
	}

	if (g_strcmp0 (res, "history") == 0)
	{
		static const LrgCtlColumn columns[] = {
			{ "#", "index", FALSE }, { "STATE", "state", FALSE }, { "STEP", "step", FALSE }
		};
		g_autoptr(JsonNode)  h = call ("edit.history", NULL, error);
		g_autoptr(JsonArray) rows = json_array_new ();
		JsonArray           *steps;
		gint64               current;

		if (h == NULL)
			return FALSE;
		steps = json_object_get_array_member (json_node_get_object (h), "steps");
		current = json_object_get_int_member (json_node_get_object (h), "current");
		for (i = 0; i < json_array_get_length (steps); i++)
		{
			JsonObject *row = json_object_new ();

			json_object_set_int_member (row, "index", i + 1);
			json_object_set_string_member (row, "state", (gint64) i < current ? "done" : "redo");
			json_object_set_string_member (row, "step", json_array_get_string_element (steps, i));
			json_array_add_object_element (rows, row);
		}
		lrg_ctl_print_table (rows, columns, G_N_ELEMENTS (columns), output, "step");
		return TRUE;
	}

	/* Single objects. */
	{
		static const gchar *methods[][2] = {
			{ "project", "project.info" }, { "info", "editor.info" }, { "view", "view.get" },
			{ "methods", "methods" }, { "state", "ui.state" }, { "play", "play.state" }
		};

		for (i = 0; i < G_N_ELEMENTS (methods); i++)
			if (g_strcmp0 (res, methods[i][0]) == 0)
			{
				g_autoptr(JsonNode) r = call (methods[i][1], NULL, error);

				if (r == NULL)
					return FALSE;
				lrg_ctl_print (r, output);
				return TRUE;
			}
	}
	return usage_error (error, "Cannot get '%s'", args[0]);
}

/* ==========================================================================
 * Node verbs
 * ========================================================================== */

static gboolean
expect_node_resource (gchar       **args,
                      const gchar  *verb,
                      guint         min_names,
                      GError      **error)
{
	guint n = 0;

	if (args[0] == NULL || g_strcmp0 (canonical_resource (args[0]), "nodes") != 0)
		return usage_error (error, "%s works on nodes: lrgeditorctl %s node NAME ...", verb, verb);
	while (args[1 + n] != NULL)
		n++;
	if (n < min_names)
		return usage_error (error, "%s node needs %u argument(s)", verb, min_names);
	return TRUE;
}

static gboolean
cmd_create (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonObject) set = json_object_new ();
	g_autoptr(JsonNode)   created = NULL;

	if (!expect_node_resource (args, "create", 1, error))
		return FALSE;

	json_object_set_string_member (p, "name", args[1]);
	if (opt_parent != NULL)
		json_object_set_string_member (p, "parent", opt_parent);
	if (opt_kind != NULL)
		json_object_set_string_member (p, "kind", opt_kind);
	if (opt_primitive != NULL)
		json_object_set_string_member (p, "primitive", opt_primitive);
	if (opt_index >= 0)
		json_object_set_int_member (p, "index", opt_index);
	if (!lrg_ctl_parse_assignments (args + 2, set, error))
		return FALSE;

	created = call ("node.create", p, error);
	if (created == NULL)
		return FALSE;
	if (json_object_get_size (set) > 0)
	{
		json_object_set_string_member (set, "node", str_member (json_node_get_object (created), "guid"));
		json_object_set_boolean_member (set, "merge", TRUE);
		json_node_unref (created);
		created = call ("node.set", set, error);
		if (created == NULL)
			return FALSE;
	}
	if (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_NAME)
		printf ("node/%s created\n", str_member (json_node_get_object (created), "path"));
	else
		lrg_ctl_print (created, output);
	return TRUE;
}

static gboolean
cmd_delete (gchar  **args,
            GError **error)
{
	guint i;

	if (!expect_node_resource (args, "delete", 1, error))
		return FALSE;
	for (i = 1; args[i] != NULL; i++)
	{
		g_autoptr(JsonObject) p = node_param (args[i]);

		if (!call_ok ("node.delete", p, error))
			return FALSE;
		printf ("node/%s deleted\n", args[i]);
	}
	return TRUE;
}

static gboolean
cmd_set (gchar  **args,
         GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (!expect_node_resource (args, "set", 2, error))
		return FALSE;
	p = node_param (args[1]);
	if (!lrg_ctl_parse_assignments (args + 2, p, error))
		return FALSE;
	r = call ("node.set", p, error);
	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT || output == LRG_CTL_FORMAT_NAME)
		printf ("node/%s configured\n", str_member (json_node_get_object (r), "path"));
	else
		lrg_ctl_print (r, output);
	return TRUE;
}

static gboolean
cmd_rename (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *path = NULL;

	if (!expect_node_resource (args, "rename", 2, error))
		return FALSE;
	guid = node_guid (args[1], error);
	if (guid == NULL)
		return FALSE;
	p = node_param (guid);
	json_object_set_string_member (p, "name", args[2]);
	if (!call_ok ("node.rename", p, error))
		return FALSE;
	path = node_path (guid);
	printf ("node/%s renamed\n", path);
	return TRUE;
}

static gboolean
cmd_move (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *path = NULL;

	if (!expect_node_resource (args, "move", 1, error))
		return FALSE;
	guid = node_guid (args[1], error);
	if (guid == NULL)
		return FALSE;
	p = node_param (guid);
	if (opt_parent != NULL)
		json_object_set_string_member (p, "parent", opt_parent);
	if (opt_index >= 0)
		json_object_set_int_member (p, "index", opt_index);
	json_object_set_boolean_member (p, "keep_world", opt_keep_world);
	if (!call_ok ("node.move", p, error))
		return FALSE;
	path = node_path (guid);
	printf ("node/%s moved\n", path);
	return TRUE;
}

static gboolean
cmd_duplicate (gchar  **args,
               GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (!expect_node_resource (args, "duplicate", 1, error))
		return FALSE;
	p = node_param (args[1]);
	r = call ("node.duplicate", p, error);
	if (r == NULL)
		return FALSE;
	if (JSON_NODE_HOLDS_OBJECT (r))
		printf ("node/%s created\n", str_member (json_node_get_object (r), "path"));
	else
		printf ("node/%s duplicated\n", args[1]);
	return TRUE;
}

/* The editable view of a node, for edit and get -o yaml round trips. */
static JsonObject *
editable_node (JsonObject *node)
{
	static const gchar *keep[] = { "name", "location", "scale", "visible", "locked", "visual" };
	JsonObject         *out = json_object_new ();
	gsize               i;

	json_object_set_string_member (out, "node", str_member (node, "guid"));
	for (i = 0; i < G_N_ELEMENTS (keep); i++)
	{
		if (!json_object_has_member (node, keep[i]))
			continue;
		json_object_set_member (out, keep[i], json_node_copy (json_object_get_member (node, keep[i])));

		/* Degrees are friendlier to edit than radians. */
		if (g_strcmp0 (keep[i], "location") == 0 && json_object_has_member (node, "rotation"))
		{
			JsonArray *rad = json_object_get_array_member (node, "rotation");
			JsonArray *deg = json_array_new ();
			guint      k;

			for (k = 0; rad != NULL && k < json_array_get_length (rad); k++)
				json_array_add_double_element (deg, json_array_get_double_element (rad, k) * 180.0 / G_PI);
			json_object_set_array_member (out, "rotation_degrees", deg);
		}
	}
	return out;
}

static gchar *
editor_command (void)
{
	const gchar *e = g_getenv ("VISUAL");

	if (e == NULL || *e == '\0')
		e = g_getenv ("EDITOR");
	return g_strdup ((e != NULL && *e != '\0') ? e : "vi");
}

static gboolean
cmd_edit (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   node = NULL;
	g_autoptr(JsonNode)   editable = json_node_new (JSON_NODE_OBJECT);
	g_autoptr(GPtrArray)  docs = NULL;
	g_autofree gchar     *yaml = NULL;
	g_autofree gchar     *path = NULL;
	g_autofree gchar     *edited = NULL;
	g_autofree gchar     *before = NULL;
	g_autofree gchar     *cmd = NULL;
	g_auto(GStrv)         words = NULL;
	g_autoptr(GPtrArray)  argv = NULL;
	gint                  fd, status = 0;
	guint                 i;

	if (!expect_node_resource (args, "edit", 1, error))
		return FALSE;
	p = node_param (args[1]);
	node = call ("node.get", p, error);
	if (node == NULL)
		return FALSE;
	json_node_take_object (editable, editable_node (json_node_get_object (node)));
	yaml = lrg_ctl_to_yaml (editable);
	before = g_strdup (yaml);

	fd = g_file_open_tmp ("lrgeditorctl-XXXXXX.yaml", &path, error);
	if (fd < 0)
		return FALSE;
	close (fd);
	{
		g_autofree gchar *text = g_strdup_printf (
			"# Editing node/%s. Lines starting with '#' are ignored.\n"
			"# Save and quit to apply; an empty file or no change does nothing.\n"
			"%s", str_member (json_node_get_object (node), "path"), yaml);

		if (!g_file_set_contents (path, text, -1, error))
			return FALSE;
	}

	/* $VISUAL/$EDITOR may carry arguments ("emacsclient -t"); the editor
	 * shares our terminal (stdin inherited, output not captured). */
	cmd = editor_command ();
	if (!g_shell_parse_argv (cmd, NULL, &words, error))
	{
		g_unlink (path);
		return FALSE;
	}
	argv = g_ptr_array_new ();
	for (i = 0; words[i] != NULL; i++)
		g_ptr_array_add (argv, words[i]);
	g_ptr_array_add (argv, path);
	g_ptr_array_add (argv, NULL);
	if (!g_spawn_sync (NULL, (gchar **) argv->pdata, NULL,
	                   G_SPAWN_SEARCH_PATH | G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
	                   NULL, NULL, &status, error))
	{
		g_unlink (path);
		return FALSE;
	}
	if (!g_file_get_contents (path, &edited, NULL, error))
	{
		g_unlink (path);
		return FALSE;
	}
	g_unlink (path);
	if (!g_spawn_check_wait_status (status, error))
		return FALSE;

	docs = lrg_ctl_parse_documents (edited, error);
	if (docs == NULL)
		return FALSE;
	if (docs->len == 0 || !JSON_NODE_HOLDS_OBJECT (g_ptr_array_index (docs, 0)))
	{
		printf ("Edit cancelled, no changes made.\n");
		return TRUE;
	}
	{
		g_autofree gchar *after = lrg_ctl_to_yaml (g_ptr_array_index (docs, 0));
		JsonObject       *o = json_node_get_object (g_ptr_array_index (docs, 0));
		g_autoptr(JsonNode) r = NULL;

		if (g_strcmp0 (after, before) == 0)
		{
			printf ("Edit cancelled, no changes made.\n");
			return TRUE;
		}
		if (!json_object_has_member (o, "node"))
			json_object_set_string_member (o, "node", str_member (json_node_get_object (node), "guid"));
		r = call ("node.set", o, error);
		if (r == NULL)
			return FALSE;
		printf ("node/%s edited\n", str_member (json_node_get_object (r), "path"));
	}
	return TRUE;
}

/* Makes sure the node at @path exists, creating empty group nodes for
 * any missing segments (like mkdir -p). */
static gboolean
ensure_path (const gchar  *path,
             GError      **error)
{
	g_autoptr(JsonObject) get = node_param (path);
	g_autoptr(JsonNode)   found = call ("node.get", get, NULL);
	g_autoptr(JsonObject) create = NULL;
	g_autoptr(JsonNode)   created = NULL;
	const gchar          *slash;

	if (found != NULL)
		return TRUE;

	slash = strrchr (path, '/');
	create = json_object_new ();
	json_object_set_string_member (create, "name", slash != NULL ? slash + 1 : path);
	if (slash != NULL)
	{
		g_autofree gchar *parent = g_strndup (path, slash - path);

		if (!ensure_path (parent, error))
			return FALSE;
		json_object_set_string_member (create, "parent", parent);
	}
	created = call ("node.create", create, error);
	if (created == NULL)
		return FALSE;
	printf ("node/%s created\n", path);
	return TRUE;
}

/* apply: each document (or array element) is a node spec with "node" (a
 * GUID or path). Existing nodes are updated; missing ones are created
 * under the parent named by the path (missing parents too), then updated. */
static gboolean
apply_one (JsonObject  *spec,
           GError     **error)
{
	const gchar          *target = str_member (spec, "node");
	g_autoptr(JsonObject) get = NULL;
	g_autoptr(JsonNode)   existing = NULL;
	g_autoptr(JsonNode)   r = NULL;
	g_autoptr(GError)     lookup_error = NULL;
	const gchar          *verb = "configured";

	if (target == NULL)
		return usage_error (error, "Each document needs a 'node' (GUID or path)");

	get = node_param (target);
	existing = call ("node.get", get, &lookup_error);
	if (existing == NULL)
	{
		g_autoptr(JsonObject) create = json_object_new ();
		const gchar          *slash = strrchr (target, '/');
		g_autoptr(JsonNode)   created = NULL;
		const gchar          *kind = str_member (spec, "kind");
		const gchar          *primitive = str_member (spec, "primitive");

		if (!g_error_matches (lookup_error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE) ||
		    strstr (lookup_error->message, "not-found") == NULL)
		{
			g_propagate_error (error, g_steal_pointer (&lookup_error));
			return FALSE;
		}
		json_object_set_string_member (create, "name", slash != NULL ? slash + 1 : target);
		if (slash != NULL)
		{
			g_autofree gchar *parent = g_strndup (target, slash - target);

			if (!ensure_path (parent, error))
				return FALSE;
			json_object_set_string_member (create, "parent", parent);
		}
		if (kind != NULL)
			json_object_set_string_member (create, "kind", kind);
		if (primitive != NULL)
			json_object_set_string_member (create, "primitive", primitive);
		created = call ("node.create", create, error);
		if (created == NULL)
			return FALSE;
		json_object_set_string_member (spec, "node", str_member (json_node_get_object (created), "guid"));
		verb = "created";
	}
	json_object_remove_member (spec, "kind");
	json_object_remove_member (spec, "primitive");

	r = call ("node.set", spec, error);
	if (r == NULL)
		return FALSE;
	printf ("node/%s %s\n", str_member (json_node_get_object (r), "path"), verb);
	return TRUE;
}

static gboolean
cmd_apply (GError **error)
{
	g_autofree gchar    *text = NULL;
	g_autoptr(GPtrArray) docs = NULL;
	guint                i, k;

	if (opt_filename == NULL)
		return usage_error (error, "apply needs -f FILE ('-' reads stdin)");
	if (g_strcmp0 (opt_filename, "-") == 0)
	{
		GString *s = g_string_new (NULL);
		gchar    buf[4096];
		gsize    n;

		while ((n = fread (buf, 1, sizeof buf, stdin)) > 0)
			g_string_append_len (s, buf, (gssize) n);
		text = g_string_free (s, FALSE);
	}
	else if (!g_file_get_contents (opt_filename, &text, NULL, error))
		return FALSE;

	docs = lrg_ctl_parse_documents (text, error);
	if (docs == NULL)
		return FALSE;
	for (i = 0; i < docs->len; i++)
	{
		JsonNode *d = g_ptr_array_index (docs, i);

		if (JSON_NODE_HOLDS_OBJECT (d))
		{
			if (!apply_one (json_node_get_object (d), error))
				return FALSE;
		}
		else if (JSON_NODE_HOLDS_ARRAY (d))
		{
			JsonArray *a = json_node_get_array (d);

			for (k = 0; k < json_array_get_length (a); k++)
				if (JSON_NODE_HOLDS_OBJECT (json_array_get_element (a, k)) &&
				    !apply_one (json_array_get_object_element (a, k), error))
					return FALSE;
		}
	}
	return TRUE;
}

static gboolean
cmd_describe (gchar  **args,
              GError **error)
{
	if (args[0] != NULL && g_strcmp0 (canonical_resource (args[0]), "nodes") == 0 && args[1] != NULL)
	{
		g_autoptr(JsonObject) p = node_param (args[1]);
		g_autoptr(JsonNode)   n = NULL;

		json_object_set_boolean_member (p, "recursive", TRUE);
		n = call ("node.get", p, error);
		if (n == NULL)
			return FALSE;
		lrg_ctl_print (n, output == LRG_CTL_FORMAT_JSON ? output : LRG_CTL_FORMAT_YAML);
		return TRUE;
	}
	if (output == LRG_CTL_FORMAT_DEFAULT)
		output = LRG_CTL_FORMAT_YAML;
	return cmd_get (args, error);
}

/* ==========================================================================
 * Editor verbs
 * ========================================================================== */

static gboolean
cmd_select (gchar  **args,
            GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	JsonArray            *nodes = json_array_new ();
	guint                 i;

	if (opt_clear)
	{
		json_array_unref (nodes);
		return call_ok ("selection.clear", NULL, error);
	}
	if (args[0] == NULL)
	{
		json_array_unref (nodes);
		return usage_error (error, "select NAME... (or --clear)");
	}
	for (i = 0; args[i] != NULL; i++)
		json_array_add_string_element (nodes, args[i]);
	json_object_set_array_member (p, "nodes", nodes);
	json_object_set_boolean_member (p, "additive", opt_add);
	return call_ok ("selection.set", p, error);
}

static gboolean
cmd_view (gchar  **args,
          GError **error)
{
	static const gchar *aligned[][2] = {
		{ "top", "view.top" }, { "front", "view.front" }, { "right", "view.right" },
		{ "bottom", "view.bottom" }, { "back", "view.back" }, { "left", "view.left" },
		{ "persp", "view.perspective" }, { "ortho", "view.perspective" }, { "all", "view.frame-all" }
	};
	gsize i;

	if (args[0] == NULL)
	{
		g_autoptr(JsonNode) v = call ("view.get", NULL, error);

		if (v == NULL)
			return FALSE;
		lrg_ctl_print (v, output);
		return TRUE;
	}
	if (g_strcmp0 (args[0], "focus") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (args[1] != NULL)
			json_object_set_string_member (p, "node", args[1]);
		return call_ok ("view.focus", p, error);
	}
	if (g_strcmp0 (args[0], "set") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (!lrg_ctl_parse_assignments (args + 1, p, error))
			return FALSE;
		return call_ok ("view.set", p, error);
	}
	for (i = 0; i < G_N_ELEMENTS (aligned); i++)
		if (g_strcmp0 (args[0], aligned[i][0]) == 0)
		{
			g_autoptr(JsonObject) p = json_object_new ();

			json_object_set_string_member (p, "id", aligned[i][1]);
			return call_ok ("action.run", p, error);
		}
	return usage_error (error, "view top|front|right|bottom|back|left|persp|all, view focus NAME, view set key=value");
}

static gboolean
print_log_record (JsonNode *record)
{
	JsonObject       *o;
	g_autofree gchar *when = NULL;

	if (record == NULL || !JSON_NODE_HOLDS_OBJECT (record))
		return TRUE;
	if (output == LRG_CTL_FORMAT_JSON)
	{
		g_autofree gchar *line = lrg_ctl_to_json (record, FALSE);

		printf ("%s\n", line);
		fflush (stdout);
		return TRUE;
	}
	o = json_node_get_object (record);
	when = format_time (json_object_has_member (o, "time") ? json_object_get_int_member (o, "time") : 0);
	printf ("%s  %-8s %-10s %s\n", when, str_member (o, "level") != NULL ? str_member (o, "level") : "",
	        str_member (o, "domain") != NULL ? str_member (o, "domain") : "",
	        str_member (o, "message") != NULL ? str_member (o, "message") : "");
	fflush (stdout);
	return TRUE;
}

static gboolean
on_log_event (const gchar *event,
              JsonNode    *data,
              gpointer     user_data)
{
	return print_log_record (data);
}

static gboolean
cmd_logs (GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autoptr(JsonNode)   list = NULL;
	static const gchar   *log_only[] = { "log", NULL };
	guint                 i;

	json_object_set_int_member (p, "count", MAX (opt_tail, 0));
	list = call ("log.tail", p, error);
	if (list == NULL)
		return FALSE;
	for (i = 0; i < json_array_get_length (json_node_get_array (list)); i++)
		print_log_record (json_array_get_element (json_node_get_array (list), i));
	if (!opt_watch)
		return TRUE;
	return lrg_ctl_follow_events (session_socket, log_only, on_log_event, NULL, error);
}

static gboolean
on_event (const gchar *event,
          JsonNode    *data,
          gpointer     user_data)
{
	g_autoptr(JsonNode) wrapped = json_node_new (JSON_NODE_OBJECT);
	JsonObject         *o = json_object_new ();

	json_object_set_string_member (o, "event", event);
	json_object_set_member (o, "data", data != NULL ? json_node_copy (data) : json_node_new (JSON_NODE_NULL));
	json_node_take_object (wrapped, o);

	if (output == LRG_CTL_FORMAT_YAML)
	{
		g_autofree gchar *text = lrg_ctl_to_yaml (wrapped);

		printf ("---\n%s", text);
	}
	else
	{
		g_autofree gchar *line = lrg_ctl_to_json (wrapped, FALSE);

		printf ("%s\n", line);
	}
	fflush (stdout);
	return TRUE;
}

static gboolean
cmd_events (gchar  **args,
            GError **error)
{
	g_autoptr(JsonNode) info = call ("editor.info", NULL, error);

	if (info == NULL)
		return FALSE;
	return lrg_ctl_follow_events (session_socket, (const gchar * const *) args, on_event, NULL, error);
}

static gboolean
cmd_call (gchar  **args,
          GError **error)
{
	g_autoptr(JsonObject) p = NULL;
	g_autoptr(JsonNode)   r = NULL;

	if (args[0] == NULL)
		return usage_error (error, "call METHOD [key=value...|JSON]");
	if (args[1] != NULL && (args[1][0] == '{'))
	{
		g_autoptr(JsonParser) parser = json_parser_new ();

		if (!json_parser_load_from_data (parser, args[1], -1, error))
			return FALSE;
		if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
			return usage_error (error, "params must be a JSON object");
		p = json_object_ref (json_node_get_object (json_parser_get_root (parser)));
	}
	else
	{
		p = json_object_new ();
		if (!lrg_ctl_parse_assignments (args + 1, p, error))
			return FALSE;
	}
	r = call (args[0], p, error);
	if (r == NULL)
		return FALSE;
	lrg_ctl_print (r, output);
	return TRUE;
}

static gboolean
cmd_screenshot (gchar  **args,
                GError **error)
{
	g_autoptr(JsonObject) p = json_object_new ();
	g_autofree gchar     *abs = NULL;
	gint                  tries;

	if (args[0] == NULL)
		return usage_error (error, "screenshot FILE.png [--window]");
	abs = g_canonicalize_filename (args[0], NULL);
	json_object_set_string_member (p, "path", abs);
	json_object_set_boolean_member (p, "window", opt_window);
	g_unlink (abs);
	if (!call_ok ("view.screenshot", p, error))
		return FALSE;
	/* Window captures are written after the editor's next frame. */
	for (tries = 0; tries < 60 && !g_file_test (abs, G_FILE_TEST_EXISTS); tries++)
		g_usleep (50000);
	if (!g_file_test (abs, G_FILE_TEST_EXISTS))
		return usage_error (error, "The editor did not write %s", abs);
	printf ("%s\n", abs);
	return TRUE;
}

static gboolean
simple_method (const gchar  *method,
               JsonObject   *params,
               const gchar  *done,
               GError      **error)
{
	g_autoptr(JsonNode) r = call (method, params, error);

	if (r == NULL)
		return FALSE;
	if (output == LRG_CTL_FORMAT_DEFAULT && done != NULL)
		printf ("%s\n", done);
	else if (output != LRG_CTL_FORMAT_DEFAULT)
		lrg_ctl_print (r, output);
	return TRUE;
}

#ifdef LRG_EDITORCTL_HAVE_MCP
static gboolean
cmd_mcp (GError **error)
{
	g_autoptr(McpServer)         server = NULL;
	g_autoptr(McpStdioTransport) transport = NULL;

	/* Each tool call resolves the session again, so the bridge survives
	 * editor restarts and can start before any editor is running. */
	server = lrg_ctl_mcp_server_new (lrg_ctl_call_selected, opt_session);
	transport = mcp_stdio_transport_new ();
	mcp_server_set_transport (server, MCP_TRANSPORT (transport));
	if (mcp_server_run (server, error))
		return TRUE;
	/* The client closing stdin is how an MCP session normally ends. */
	if (error != NULL && (g_error_matches (*error, MCP_ERROR, MCP_ERROR_CONNECTION_CLOSED) ||
	                      g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)))
	{
		g_clear_error (error);
		return TRUE;
	}
	return FALSE;
}
#endif

static const gchar *completion_script =
	"# bash completion for lrgeditorctl\n"
	"_lrgeditorctl () {\n"
	"\tlocal cur prev verb\n"
	"\tcur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
	"\tprev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
	"\tverb=\"${COMP_WORDS[1]}\"\n"
	"\tcase \"${prev}\" in\n"
	"\t\t-o|--output) mapfile -t COMPREPLY < <(compgen -W 'yaml json table wide name' -- \"${cur}\"); return ;;\n"
	"\t\t-f|--filename) mapfile -t COMPREPLY < <(compgen -f -- \"${cur}\"); return ;;\n"
	"\t\t-p|--parent) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\"); return ;;\n"
	"\tesac\n"
	"\tif [[ ${COMP_CWORD} -eq 1 ]]\n"
	"\tthen\n"
	"\t\tmapfile -t COMPREPLY < <(compgen -W 'sessions get describe create delete set rename move duplicate edit apply select run play stop game undo redo save reload validate open new view screenshot logs events call notify quit mcp completion version' -- \"${cur}\")\n"
	"\t\treturn\n"
	"\tfi\n"
	"\tcase \"${verb}\" in\n"
	"\t\tget|describe)\n"
	"\t\t\tif [[ ${COMP_CWORD} -eq 2 ]]\n"
	"\t\t\tthen\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'nodes node selection actions problems assets project info view history methods state play sessions' -- \"${cur}\")\n"
	"\t\t\telse\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\")\n"
	"\t\t\tfi ;;\n"
	"\t\tcreate|delete|set|rename|move|duplicate|edit)\n"
	"\t\t\tif [[ ${COMP_CWORD} -eq 2 ]]\n"
	"\t\t\tthen\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W 'node' -- \"${cur}\")\n"
	"\t\t\telse\n"
	"\t\t\t\tmapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\")\n"
	"\t\t\tfi ;;\n"
	"\t\tselect|events) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get nodes -o name 2>/dev/null | sed 's|^node/||')\" -- \"${cur}\") ;;\n"
	"\t\trun) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get actions -o name 2>/dev/null)\" -- \"${cur}\") ;;\n"
	"\t\tplay) mapfile -t COMPREPLY < <(compgen -W 'start pause step stop state' -- \"${cur}\") ;;\n"
	"\t\tgame) mapfile -t COMPREPLY < <(compgen -W 'start stop' -- \"${cur}\") ;;\n"
	"\t\tview) mapfile -t COMPREPLY < <(compgen -W 'top front right bottom back left persp all focus set' -- \"${cur}\") ;;\n"
	"\t\tcall) mapfile -t COMPREPLY < <(compgen -W \"$(lrgeditorctl get methods -o json 2>/dev/null | tr -d '[]\\\",')\" -- \"${cur}\") ;;\n"
	"\t\tcompletion) mapfile -t COMPREPLY < <(compgen -W 'bash' -- \"${cur}\") ;;\n"
	"\tesac\n"
	"}\n"
	"complete -o default -F _lrgeditorctl lrgeditorctl\n";

/* ==========================================================================
 * Main
 * ========================================================================== */

static gboolean
dispatch (gchar  **argv,
          GError **error)
{
	const gchar  *verb = argv[0];
	gchar       **rest = argv + 1;

	if (g_strcmp0 (verb, "sessions") == 0)
		return cmd_sessions (error);
	if (g_strcmp0 (verb, "get") == 0)
		return cmd_get (rest, error);
	if (g_strcmp0 (verb, "describe") == 0)
		return cmd_describe (rest, error);
	if (g_strcmp0 (verb, "create") == 0)
		return cmd_create (rest, error);
	if (g_strcmp0 (verb, "delete") == 0 || g_strcmp0 (verb, "rm") == 0)
		return cmd_delete (rest, error);
	if (g_strcmp0 (verb, "set") == 0)
		return cmd_set (rest, error);
	if (g_strcmp0 (verb, "rename") == 0)
		return cmd_rename (rest, error);
	if (g_strcmp0 (verb, "move") == 0 || g_strcmp0 (verb, "mv") == 0)
		return cmd_move (rest, error);
	if (g_strcmp0 (verb, "duplicate") == 0 || g_strcmp0 (verb, "dup") == 0)
		return cmd_duplicate (rest, error);
	if (g_strcmp0 (verb, "edit") == 0)
		return cmd_edit (rest, error);
	if (g_strcmp0 (verb, "apply") == 0)
		return cmd_apply (error);
	if (g_strcmp0 (verb, "select") == 0)
		return cmd_select (rest, error);
	if (g_strcmp0 (verb, "run") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		if (rest[0] == NULL)
		{
			gchar *actions[] = { (gchar *) "actions", NULL };

			return cmd_get (actions, error);
		}
		json_object_set_string_member (p, "id", rest[0]);
		return simple_method ("action.run", p, NULL, error);
	}
	if (g_strcmp0 (verb, "play") == 0)
	{
		const gchar      *what = (rest[0] != NULL) ? rest[0] : "start";
		g_autofree gchar *method = g_strconcat ("play.", what, NULL);

		if (g_strcmp0 (what, "start") != 0 && g_strcmp0 (what, "pause") != 0 && g_strcmp0 (what, "step") != 0 &&
		    g_strcmp0 (what, "stop") != 0 && g_strcmp0 (what, "state") != 0)
			return usage_error (error, "play start|pause|step|stop|state");
		if (output == LRG_CTL_FORMAT_DEFAULT)
			output = LRG_CTL_FORMAT_YAML;
		return simple_method (method, NULL, NULL, error);
	}
	if (g_strcmp0 (verb, "stop") == 0)
		return simple_method ("play.stop", NULL, "stopped", error);
	if (g_strcmp0 (verb, "game") == 0)
	{
		if (g_strcmp0 (rest[0], "start") == 0)
			return simple_method ("run.start", NULL, "game started", error);
		if (g_strcmp0 (rest[0], "stop") == 0)
			return simple_method ("run.stop", NULL, "game stopped", error);
		return usage_error (error, "game start|stop");
	}
	if (g_strcmp0 (verb, "undo") == 0)
		return simple_method ("edit.undo", NULL, "undone", error);
	if (g_strcmp0 (verb, "redo") == 0)
		return simple_method ("edit.redo", NULL, "redone", error);
	if (g_strcmp0 (verb, "save") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   r = NULL;

		if (rest[0] != NULL)
		{
			g_autofree gchar *abs = g_canonicalize_filename (rest[0], NULL);

			json_object_set_string_member (p, "path", abs);
		}
		r = call ("level.save", p, error);
		if (r == NULL)
			return FALSE;
		printf ("saved %s\n", (JSON_NODE_HOLDS_VALUE (r) && json_node_get_value_type (r) == G_TYPE_STRING)
		                      ? json_node_get_string (r) : "");
		return TRUE;
	}
	if (g_strcmp0 (verb, "reload") == 0)
		return simple_method ("level.reload", NULL, "reloaded", error);
	if (g_strcmp0 (verb, "validate") == 0)
	{
		gchar             *problems[] = { (gchar *) "problems", NULL };
		g_autoptr(JsonNode) list = NULL;
		guint               i;

		if (!cmd_get (problems, error))
			return FALSE;
		/* Exit status 1 when there are errors (for scripts and CI). */
		list = call ("level.validate", NULL, error);
		for (i = 0; list != NULL && i < json_array_get_length (json_node_get_array (list)); i++)
			if (g_strcmp0 (str_member (json_array_get_object_element (json_node_get_array (list), i), "severity"),
			               "error") == 0)
			{
				g_set_error_literal (error, LRG_CTL_ERROR, LRG_CTL_ERROR_REMOTE, "the level has errors");
				return FALSE;
			}
		return list != NULL;
	}
	if (g_strcmp0 (verb, "open") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autofree gchar     *abs = NULL;

		if (rest[0] == NULL)
			return usage_error (error, "open LEVEL [--force]");
		abs = g_canonicalize_filename (rest[0], NULL);
		json_object_set_string_member (p, "path", g_file_test (abs, G_FILE_TEST_EXISTS) ? abs : rest[0]);
		json_object_set_boolean_member (p, "force", opt_force);
		return simple_method ("level.open", p, "opened", error);
	}
	if (g_strcmp0 (verb, "new") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autoptr(JsonNode)   info = NULL;

		info = call ("editor.info", NULL, error);
		if (info == NULL)
			return FALSE;
		if (json_object_get_boolean_member (json_node_get_object (info), "dirty") && !opt_force)
			return usage_error (error, "The level has unsaved changes; save it or pass --force");
		if (rest[0] != NULL)
			json_object_set_string_member (p, "name", rest[0]);
		return simple_method ("level.new", p, "new level", error);
	}
	if (g_strcmp0 (verb, "view") == 0)
		return cmd_view (rest, error);
	if (g_strcmp0 (verb, "screenshot") == 0)
		return cmd_screenshot (rest, error);
	if (g_strcmp0 (verb, "logs") == 0 || g_strcmp0 (verb, "log") == 0)
		return cmd_logs (error);
	if (g_strcmp0 (verb, "events") == 0 || g_strcmp0 (verb, "watch") == 0)
		return cmd_events (rest, error);
	if (g_strcmp0 (verb, "call") == 0)
		return cmd_call (rest, error);
	if (g_strcmp0 (verb, "notify") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();
		g_autofree gchar     *msg = NULL;

		if (rest[0] == NULL)
			return usage_error (error, "notify MESSAGE [--level N]");
		msg = g_strjoinv (" ", rest);
		json_object_set_string_member (p, "message", msg);
		json_object_set_int_member (p, "level", opt_level);
		return simple_method ("ui.notify", p, NULL, error);
	}
	if (g_strcmp0 (verb, "quit") == 0)
	{
		g_autoptr(JsonObject) p = json_object_new ();

		json_object_set_boolean_member (p, "force", opt_force);
		return simple_method ("editor.quit", p, "quitting", error);
	}
	if (g_strcmp0 (verb, "mcp") == 0)
	{
#ifdef LRG_EDITORCTL_HAVE_MCP
		return cmd_mcp (error);
#else
		return usage_error (error, "lrgeditorctl was built without MCP support (needs mcp-glib, libsoup-3.0, libdex-1)");
#endif
	}
	if (g_strcmp0 (verb, "completion") == 0)
	{
		if (g_strcmp0 (rest[0], "bash") != 0)
			return usage_error (error, "completion bash");
		fputs (completion_script, stdout);
		return TRUE;
	}
	if (g_strcmp0 (verb, "version") == 0)
	{
		g_autoptr(JsonNode) hello = NULL;

		printf ("lrgeditorctl %s\n", LRG_EDITORCTL_VERSION);
		hello = call ("hello", NULL, NULL);
		if (hello != NULL && JSON_NODE_HOLDS_OBJECT (hello))
			printf ("editor protocol %" G_GINT64_FORMAT " (pid %" G_GINT64_FORMAT ")\n",
			        json_object_get_int_member (json_node_get_object (hello), "protocol"),
			        json_object_get_int_member (json_node_get_object (hello), "pid"));
		return TRUE;
	}
	return usage_error (error, "Unknown command '%s' (see lrgeditorctl --help)", verb);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	gboolean                  ok;

	ctx = g_option_context_new ("- control running libregnum editors");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx, description);
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("lrgeditorctl: %s\n", error->message);
		return 2;
	}

	if (opt_license)
	{
		fputs (license_text, stdout);
		return 0;
	}
	if (opt_version)
	{
		printf ("lrgeditorctl %s\n", LRG_EDITORCTL_VERSION);
		return 0;
	}
	if (opt_output != NULL && !lrg_ctl_parse_format (opt_output, &output))
	{
		g_printerr ("lrgeditorctl: unknown output format '%s' (yaml, json, table, wide, name)\n", opt_output);
		return 2;
	}
	if (opt_args == NULL || opt_args[0] == NULL)
	{
		g_autofree gchar *help = g_option_context_get_help (ctx, TRUE, NULL);

		fputs (help, stderr);
		return 2;
	}

	ok = dispatch (opt_args, &error);
	if (!ok)
	{
		g_printerr ("lrgeditorctl: %s\n", error != NULL ? error->message : "failed");
		return g_error_matches (error, LRG_CTL_ERROR, LRG_CTL_ERROR_USAGE) ? 2 : 1;
	}
	g_free (session_socket);
	return 0;
}
