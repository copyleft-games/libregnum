/* lrgeditorctl-mcp.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's MCP tools. Most tools map one-to-one onto an IPC method
 * and pass their arguments straight through (the schemas below document
 * the method's parameters for the model); a few compose methods
 * (create_node with a transform), pick a method from an argument (play,
 * run_game) or post-process the result (screenshot returns the image).
 */

#include "lrgeditorctl-mcp.h"
#include <glib/gstdio.h>
#include <string.h>

#define NODE_ARG \
	"\"node\":{\"type\":\"string\",\"description\":\"Node GUID or slash-separated name path from the top of the level, e.g. \\\"House/Door\\\"\"}"
#define VEC3(desc) \
	"{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"description\":\"" desc "\"}"

typedef enum
{
	TOOL_PASS,        /* arguments -> method params */
	TOOL_CREATE,      /* node.create, then node.set for a transform */
	TOOL_PLAY,        /* command -> play.<command> */
	TOOL_RUN,         /* stop -> run.stop, else run.start */
	TOOL_SCREENSHOT,  /* view.screenshot to a temp file, returned as an image */
	TOOL_SESSIONS,    /* running editors on this machine */
	TOOL_RAW          /* any method */
} ToolKind;

typedef struct
{
	const gchar *name;
	const gchar *method;
	ToolKind     kind;
	gboolean     read_only;
	gboolean     destructive;
	const gchar *description;
	const gchar *properties;   /* JSON object members of the input schema */
	const gchar *required;     /* JSON array, or NULL */
} ToolSpec;

static const ToolSpec tools[] = {
	{ "editor_info", "editor.info", TOOL_PASS, TRUE, FALSE,
	  "Summary of the running editor: project, level file, dirty flag, node count, undo/redo depth, active tool, snapping and selection. Call this first.",
	  "", NULL },
	{ "list_sessions", NULL, TOOL_SESSIONS, TRUE, FALSE,
	  "Running libregnum editors on this machine (socket, pid, project, level). The bridge talks to the newest unless started with --session.",
	  "", NULL },
	{ "get_tree", "node.tree", TOOL_PASS, TRUE, FALSE,
	  "The level's node hierarchy: nested {guid, name, kind, visible, locked, children, nodes}. Optionally only the subtree under one node.",
	  NODE_ARG, NULL },
	{ "get_node", "node.get", TOOL_PASS, TRUE, FALSE,
	  "Everything about one node: transform (location, rotation in radians, scale), visual (kind, primitive, asset, material), components, scripts, flags.",
	  NODE_ARG ",\"recursive\":{\"type\":\"boolean\",\"description\":\"Include children\"}", "[\"node\"]" },
	{ "find_nodes", "node.find", TOOL_PASS, TRUE, FALSE,
	  "Nodes whose name contains the query (case-insensitive), or whose GUID matches.",
	  "\"query\":{\"type\":\"string\"}", "[\"query\"]" },
	{ "create_node", "node.create", TOOL_CREATE, FALSE, FALSE,
	  "Create a node (undoable). kind: none, primitive, mesh-asset, sprite, tilemap, light, camera, audio-emitter. primitive: cube, uv-sphere, ico-sphere, plane, cylinder, cone, torus, circle, grid, rectangle-2d, circle-2d. Primitives are unit-sized (1 m). Returns the new node.",
	  "\"name\":{\"type\":\"string\"},\"parent\":{\"type\":\"string\",\"description\":\"Parent GUID or path; omit for top level\"},"
	  "\"kind\":{\"type\":\"string\"},\"primitive\":{\"type\":\"string\"},\"index\":{\"type\":\"integer\"},"
	  "\"location\":" VEC3 ("metres, local to the parent") ",\"rotation_degrees\":" VEC3 ("Euler XYZ degrees") ","
	  "\"scale\":" VEC3 ("per-axis scale"), "[\"name\"]" },
	{ "update_node", "node.set", TOOL_PASS, FALSE, FALSE,
	  "Change a node (one undo step): name, location, rotation_degrees (or rotation in radians), scale, visible, locked, visual {kind, primitive, asset, color [r,g,b,a] 0..1, roughness, metallic} (color/roughness/metallic may also be nested under material). Only given fields change.",
	  NODE_ARG ",\"name\":{\"type\":\"string\"},\"location\":" VEC3 ("metres") ",\"rotation_degrees\":" VEC3 ("degrees")
	  ",\"rotation\":" VEC3 ("radians") ",\"scale\":" VEC3 ("scale") ",\"visible\":{\"type\":\"boolean\"},"
	  "\"locked\":{\"type\":\"boolean\"},\"visual\":{\"type\":\"object\"}", "[\"node\"]" },
	{ "delete_node", "node.delete", TOOL_PASS, FALSE, TRUE,
	  "Delete a node and its children (undoable with undo).",
	  NODE_ARG, "[\"node\"]" },
	{ "duplicate_node", "node.duplicate", TOOL_PASS, FALSE, FALSE,
	  "Duplicate a node and its children next to it; returns the copy.",
	  NODE_ARG, "[\"node\"]" },
	{ "move_node", "node.move", TOOL_PASS, FALSE, FALSE,
	  "Reparent and/or reorder a node. keep_world keeps its world transform.",
	  NODE_ARG ",\"parent\":{\"type\":\"string\",\"description\":\"New parent; omit for top level\"},"
	  "\"index\":{\"type\":\"integer\"},\"keep_world\":{\"type\":\"boolean\"}", "[\"node\"]" },
	{ "add_component", "component.add", TOOL_PASS, FALSE, FALSE,
	  "Attach a component description (a registered component type name) with optional properties.",
	  NODE_ARG ",\"type\":{\"type\":\"string\"},\"props\":{\"type\":\"object\"}", "[\"node\",\"type\"]" },
	{ "set_component", "component.set", TOOL_PASS, FALSE, FALSE,
	  "Set one property of the node's component at index.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"key\":{\"type\":\"string\"},\"value\":{}", "[\"node\",\"index\",\"key\",\"value\"]" },
	{ "remove_component", "component.remove", TOOL_PASS, FALSE, TRUE,
	  "Remove the node's component at index.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"node\",\"index\"]" },
	{ "add_script", "script.add", TOOL_PASS, FALSE, FALSE,
	  "Attach a script file (path relative to the project) to a node. language: lua, python, gjs, crispy, elisp.",
	  NODE_ARG ",\"script\":{\"type\":\"string\"},\"language\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"}", "[\"node\",\"script\"]" },
	{ "remove_script", "script.remove", TOOL_PASS, FALSE, TRUE,
	  "Detach the node's script at index (the file is kept).",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"node\",\"index\"]" },
	{ "get_selection", "selection.get", TOOL_PASS, TRUE, FALSE,
	  "GUIDs of the selected nodes and the primary one.", "", NULL },
	{ "set_selection", "selection.set", TOOL_PASS, FALSE, FALSE,
	  "Select nodes (GUIDs or paths) in the editor; additive adds to the selection.",
	  "\"nodes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"additive\":{\"type\":\"boolean\"}", "[\"nodes\"]" },
	{ "undo", "edit.undo", TOOL_PASS, FALSE, FALSE, "Undo the last edit.", "", NULL },
	{ "redo", "edit.redo", TOOL_PASS, FALSE, FALSE, "Redo the last undone edit.", "", NULL },
	{ "get_history", "edit.history", TOOL_PASS, TRUE, FALSE, "Undo and redo step labels.", "", NULL },
	{ "save_level", "level.save", TOOL_PASS, FALSE, FALSE,
	  "Save the level (optionally to a new path, which becomes the document's path).",
	  "\"path\":{\"type\":\"string\"}", NULL },
	{ "open_level", "level.open", TOOL_PASS, FALSE, TRUE,
	  "Open a level file. Fails if there are unsaved changes unless force is true (which discards them).",
	  "\"path\":{\"type\":\"string\"},\"force\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "new_level", "level.new", TOOL_PASS, FALSE, TRUE,
	  "Start a new empty level (unsaved).", "\"name\":{\"type\":\"string\"}", NULL },
	{ "get_level_yaml", "level.serialize", TOOL_PASS, TRUE, FALSE,
	  "The whole level as .rlevel YAML text, exactly as it would be saved.", "", NULL },
	{ "validate_level", "level.validate", TOOL_PASS, TRUE, FALSE,
	  "Problems in the level: [{severity, code, node, path, message, file}]. Empty means clean.", "", NULL },
	{ "list_actions", "action.list", TOOL_PASS, TRUE, FALSE,
	  "Every editor action (menu command): {id, label, category, shortcut, enabled, checked}.", "", NULL },
	{ "run_action", "action.run", TOOL_PASS, FALSE, FALSE,
	  "Run an editor action by id, e.g. view.frame-all, view.top, edit.select-all, node.group, tool.rotate.",
	  "\"id\":{\"type\":\"string\"}", "[\"id\"]" },
	{ "play", NULL, TOOL_PLAY, FALSE, FALSE,
	  "Play-in-editor: start, pause (toggle), step one frame, stop (restores the level exactly), or state.",
	  "\"command\":{\"type\":\"string\",\"enum\":[\"start\",\"pause\",\"step\",\"stop\",\"state\"]}", "[\"command\"]" },
	{ "run_game", NULL, TOOL_RUN, FALSE, FALSE,
	  "Run the game as its own process with the project's run command (saves first), or stop it. Output goes to the editor log.",
	  "\"stop\":{\"type\":\"boolean\"}", NULL },
	{ "get_view", "view.get", TOOL_PASS, TRUE, FALSE,
	  "Viewport camera: position, target, fovy, orthographic, ortho_height, mode_2d.", "", NULL },
	{ "set_view", "view.set", TOOL_PASS, FALSE, FALSE,
	  "Move the viewport camera.",
	  "\"position\":" VEC3 ("camera position") ",\"target\":" VEC3 ("look-at point") ","
	  "\"orthographic\":{\"type\":\"boolean\"},\"ortho_height\":{\"type\":\"number\"},\"mode_2d\":{\"type\":\"boolean\"}", NULL },
	{ "focus_view", "view.focus", TOOL_PASS, FALSE, FALSE,
	  "Frame a node in the viewport (or the selection, or everything).", NODE_ARG, NULL },
	{ "screenshot", NULL, TOOL_SCREENSHOT, TRUE, FALSE,
	  "Look at the editor: returns a PNG of the 3D viewport, or of the whole window with window=true. Needs a windowed editor (not --headless).",
	  "\"window\":{\"type\":\"boolean\"}", NULL },
	{ "get_log", "log.tail", TOOL_PASS, TRUE, FALSE,
	  "Recent editor log records (console), including game output and errors.",
	  "\"count\":{\"type\":\"integer\"}", NULL },
	{ "list_assets", "asset.list", TOOL_PASS, TRUE, FALSE,
	  "Project assets {path, type}; type filters: level, prefab, texture, model, audio, script, font...",
	  "\"type\":{\"type\":\"string\"}", NULL },
	{ "project_info", "project.info", TOOL_PASS, TRUE, FALSE,
	  "Project manifest: root, name, default level, game module, run command, scripts dir, asset dirs.", "", NULL },
	{ "ui_state", "ui.state", TOOL_PASS, TRUE, FALSE,
	  "What the editor UI is doing: open menu, dialog, navigation mode, panel rects, last input seen.", "", NULL },
	{ "notify", "ui.notify", TOOL_PASS, FALSE, FALSE,
	  "Show a toast to the person using the editor. level: 0 info, 1 success, 2 warning, 3 error.",
	  "\"message\":{\"type\":\"string\"},\"level\":{\"type\":\"integer\"}", "[\"message\"]" },
	{ "call_method", NULL, TOOL_RAW, FALSE, TRUE,
	  "Escape hatch: call any editor IPC method (see docs/editor/ipc-protocol.org or the methods list) with raw params.",
	  "\"method\":{\"type\":\"string\"},\"params\":{\"type\":\"object\"}", "[\"method\"]" },
};

static const gchar *instructions =
	"Tools for the libregnum level/scene/world editor. Start with editor_info and get_tree. "
	"Nodes are addressed by GUID or by slash path from the top of the level (\"House/Door\"). "
	"Every edit is undoable (undo/redo) and marks the level dirty; call save_level to write it. "
	"Transforms: location in metres, rotation_degrees as Euler XYZ degrees, scale per axis; primitives "
	"are 1 m unit shapes. Use validate_level after bulk edits, screenshot to look at the result (windowed "
	"editors only), play to test in the editor (stop restores the level), and run_action for any menu "
	"command (list_actions).";

typedef struct
{
	const ToolSpec *spec;
	LrgCtlCallFunc  call;
	gpointer        call_data;
} ToolContext;

guint
lrg_ctl_mcp_n_tools (void)
{
	return G_N_ELEMENTS (tools);
}

const gchar *
lrg_ctl_mcp_tool_name (guint index)
{
	return (index < G_N_ELEMENTS (tools)) ? tools[index].name : NULL;
}

const gchar *
lrg_ctl_mcp_tool_description (guint index)
{
	return (index < G_N_ELEMENTS (tools)) ? tools[index].description : NULL;
}

/* ==========================================================================
 * Results
 * ========================================================================== */

static McpToolResult *
error_result (const gchar *message)
{
	McpToolResult *r = mcp_tool_result_new (TRUE);

	mcp_tool_result_add_text (r, message);
	return r;
}

static McpToolResult *
json_result (JsonNode *node)
{
	g_autoptr(JsonGenerator) gen = json_generator_new ();
	g_autofree gchar        *text = NULL;
	McpToolResult           *r = mcp_tool_result_new (FALSE);

	/* level.serialize returns YAML text: hand it over as-is. */
	if (node != NULL && JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
	{
		mcp_tool_result_add_text (r, json_node_get_string (node));
		return r;
	}

	json_generator_set_root (gen, node);
	json_generator_set_pretty (gen, TRUE);
	text = json_generator_to_data (gen, NULL);
	mcp_tool_result_add_text (r, text);
	return r;
}

static JsonObject *
copy_object (JsonObject *src)
{
	JsonObject *out = json_object_new ();
	GList      *members, *l;

	if (src == NULL)
		return out;
	members = json_object_get_members (src);
	for (l = members; l != NULL; l = l->next)
		json_object_set_member (out, l->data, json_node_copy (json_object_get_member (src, l->data)));
	g_list_free (members);
	return out;
}

/* ==========================================================================
 * Special tools
 * ========================================================================== */

static McpToolResult *
tool_create (ToolContext *ctx,
             JsonObject  *args)
{
	static const gchar *transform[] = { "location", "rotation_degrees", "rotation", "scale" };
	g_autoptr(JsonObject) create = copy_object (args);
	g_autoptr(JsonObject) set = json_object_new ();
	g_autoptr(JsonNode)   created = NULL;
	g_autoptr(GError)     error = NULL;
	gsize                 i;

	/* node.create takes name/parent/kind/primitive; the transform follows. */
	for (i = 0; i < G_N_ELEMENTS (transform); i++)
		if (json_object_has_member (create, transform[i]))
		{
			json_object_set_member (set, transform[i], json_node_copy (json_object_get_member (create, transform[i])));
			json_object_remove_member (create, transform[i]);
		}

	created = ctx->call ("node.create", create, ctx->call_data, &error);
	if (created == NULL)
		return error_result (error->message);
	if (json_object_get_size (set) == 0 || !JSON_NODE_HOLDS_OBJECT (created))
		return json_result (created);

	json_object_set_string_member (set, "node", json_object_get_string_member (json_node_get_object (created), "guid"));
	json_object_set_boolean_member (set, "merge", TRUE);
	{
		g_autoptr(JsonNode) updated = ctx->call ("node.set", set, ctx->call_data, &error);

		if (updated == NULL)
			return error_result (error->message);
		return json_result (updated);
	}
}

/* screenshot is a deferred tool: when the MCP server runs inside the
 * editor, its handler runs on the editor's main loop, and a window
 * capture is only written after the next frame. Blocking here would
 * stop that frame from ever being drawn, so the reply is sent from a
 * timeout once the file exists. */
typedef struct
{
	McpToolCall *call;
	gchar       *dir;
	gchar       *path;
	gint         tries;
	goffset      last_size;
} ShotWait;

static void
shot_wait_free (ShotWait *w)
{
	g_clear_object (&w->call);
	if (w->path != NULL)
		g_unlink (w->path);
	if (w->dir != NULL)
		g_rmdir (w->dir);
	g_free (w->path);
	g_free (w->dir);
	g_free (w);
}

static void
shot_reply (ShotWait      *w,
            McpToolResult *r)
{
	if (!mcp_tool_call_is_completed (w->call) && !mcp_tool_call_is_cancelled (w->call))
		mcp_tool_call_return_result (w->call, r);
	mcp_tool_result_unref (r);
}

static gboolean
shot_poll (gpointer user_data)
{
	ShotWait          *w = user_data;
	GStatBuf           st;
	g_autofree gchar  *data = NULL;
	g_autofree gchar  *b64 = NULL;
	g_autoptr(GError)  error = NULL;
	gsize              len = 0;
	McpToolResult     *r;

	/* Wait for the file, then for its size to settle. */
	if (g_stat (w->path, &st) != 0 || st.st_size == 0 || st.st_size != w->last_size)
	{
		w->last_size = (g_stat (w->path, &st) == 0) ? st.st_size : -1;
		if (++w->tries < 80)
			return G_SOURCE_CONTINUE;
		shot_reply (w, error_result ("The screenshot was not written (is the editor windowed and drawing?)"));
		shot_wait_free (w);
		return G_SOURCE_REMOVE;
	}

	if (!g_file_get_contents (w->path, &data, &len, &error))
		r = error_result (error->message);
	else
	{
		b64 = g_base64_encode ((const guchar *) data, len);
		r = mcp_tool_result_new (FALSE);
		mcp_tool_result_add_image (r, b64, "image/png");
	}
	shot_reply (w, r);
	shot_wait_free (w);
	return G_SOURCE_REMOVE;
}

static void
handle_screenshot (McpServer   *server,
                   McpToolCall *call,
                   gpointer     user_data)
{
	ToolContext          *ctx = user_data;
	JsonObject           *args = mcp_tool_call_get_arguments (call);
	g_autoptr(JsonObject) params = json_object_new ();
	g_autoptr(JsonNode)   res = NULL;
	g_autoptr(GError)     error = NULL;
	ShotWait             *w = g_new0 (ShotWait, 1);

	w->call = g_object_ref (call);
	w->last_size = -1;
	w->dir = g_dir_make_tmp ("lrgeditor-shot-XXXXXX", NULL);
	if (w->dir == NULL)
	{
		shot_reply (w, error_result ("Cannot create a temporary directory"));
		shot_wait_free (w);
		return;
	}
	w->path = g_build_filename (w->dir, "shot.png", NULL);
	json_object_set_string_member (params, "path", w->path);
	json_object_set_boolean_member (params, "window",
	                                args != NULL && json_object_has_member (args, "window") &&
	                                json_object_get_boolean_member (args, "window"));

	res = ctx->call ("view.screenshot", params, ctx->call_data, &error);
	if (res == NULL)
	{
		shot_reply (w, error_result (error->message));
		shot_wait_free (w);
		return;
	}
	g_timeout_add (25, shot_poll, w);
}

static McpToolResult *
tool_sessions (void)
{
	g_autoptr(GPtrArray) sessions = lrg_ctl_list_sessions ();
	g_autoptr(JsonNode)  node = json_node_new (JSON_NODE_ARRAY);
	JsonArray           *arr = json_array_new ();
	guint                i;

	for (i = 0; i < sessions->len; i++)
	{
		LrgCtlSession *s = g_ptr_array_index (sessions, i);
		JsonObject    *o = json_object_new ();

		json_object_set_string_member (o, "socket", s->socket);
		json_object_set_int_member (o, "pid", s->pid);
		json_object_set_string_member (o, "project", s->project != NULL ? s->project : "");
		json_object_set_string_member (o, "level", s->level != NULL ? s->level : "");
		json_array_add_object_element (arr, o);
	}
	json_node_take_array (node, arr);
	return json_result (node);
}

/* ==========================================================================
 * Dispatch
 * ========================================================================== */

static McpToolResult *
handle_tool (McpServer   *server,
             const gchar *name,
             JsonObject  *arguments,
             gpointer     user_data)
{
	ToolContext          *ctx = user_data;
	const ToolSpec       *spec = ctx->spec;
	g_autoptr(GError)     error = NULL;
	g_autoptr(JsonNode)   result = NULL;
	g_autoptr(JsonObject) params = NULL;
	g_autofree gchar     *method = NULL;

	switch (spec->kind)
	{
	case TOOL_CREATE:
		return tool_create (ctx, arguments);
	case TOOL_SESSIONS:
		return tool_sessions ();
	case TOOL_PLAY:
		{
			const gchar *cmd = (arguments != NULL && json_object_has_member (arguments, "command"))
			                   ? json_object_get_string_member (arguments, "command") : "state";

			if (g_strcmp0 (cmd, "start") != 0 && g_strcmp0 (cmd, "stop") != 0 && g_strcmp0 (cmd, "pause") != 0 &&
			    g_strcmp0 (cmd, "step") != 0 && g_strcmp0 (cmd, "state") != 0)
				return error_result ("command must be start, pause, step, stop or state");
			method = g_strconcat ("play.", cmd, NULL);
			params = json_object_new ();
		}
		break;
	case TOOL_RUN:
		method = g_strdup ((arguments != NULL && json_object_has_member (arguments, "stop") &&
		                    json_object_get_boolean_member (arguments, "stop")) ? "run.stop" : "run.start");
		params = json_object_new ();
		break;
	case TOOL_RAW:
		if (arguments == NULL || !json_object_has_member (arguments, "method"))
			return error_result ("method is required");
		method = g_strdup (json_object_get_string_member (arguments, "method"));
		params = (json_object_has_member (arguments, "params") &&
		          JSON_NODE_HOLDS_OBJECT (json_object_get_member (arguments, "params")))
		         ? copy_object (json_object_get_object_member (arguments, "params")) : json_object_new ();
		break;
	default:
		method = g_strdup (spec->method);
		params = copy_object (arguments);
		break;
	}

	result = ctx->call (method, params, ctx->call_data, &error);
	if (result == NULL)
		return error_result (error != NULL ? error->message : "the call failed");
	return json_result (result);
}

static JsonNode *
build_schema (const ToolSpec *spec)
{
	g_autofree gchar     *text = g_strdup_printf ("{\"type\":\"object\",\"properties\":{%s}%s%s}",
	                                              spec->properties,
	                                              spec->required != NULL ? ",\"required\":" : "",
	                                              spec->required != NULL ? spec->required : "");
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)     error = NULL;

	if (!json_parser_load_from_data (parser, text, -1, &error))
		g_error ("bad schema for %s: %s", spec->name, error->message);
	return json_node_copy (json_parser_get_root (parser));
}

McpServer *
lrg_ctl_mcp_server_new (LrgCtlCallFunc call,
                        gpointer       call_data)
{
	McpServer *server = mcp_server_new (LRG_CTL_MCP_SERVER_NAME, LRG_EDITORCTL_VERSION);
	gsize      i;

	mcp_server_set_instructions (server, instructions);
	for (i = 0; i < G_N_ELEMENTS (tools); i++)
	{
		g_autoptr(JsonNode) schema = build_schema (&tools[i]);
		g_autoptr(McpTool)  tool = mcp_tool_new (tools[i].name, tools[i].description);
		ToolContext        *ctx = g_new0 (ToolContext, 1);

		mcp_tool_set_input_schema (tool, schema);
		mcp_tool_set_read_only_hint (tool, tools[i].read_only);
		mcp_tool_set_destructive_hint (tool, tools[i].destructive);
		mcp_tool_set_open_world_hint (tool, FALSE);
		ctx->spec = &tools[i];
		ctx->call = call;
		ctx->call_data = call_data;
		if (tools[i].kind == TOOL_SCREENSHOT)
			mcp_server_add_deferred_tool (server, tool, handle_screenshot, ctx, g_free);
		else
			mcp_server_add_tool (server, tool, handle_tool, ctx, g_free);
	}
	return server;
}
