/* lrg-mcp-debug-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for debugging operations.
 */

#include "lrg-mcp-debug-tools.h"
#include "../lrg-mcp-inspect-private.h"
#include "../../core/lrg-engine.h"
#include "../../graphics/lrg-window.h"
#include "../../debug/lrg-profiler.h"
#include "../../debug/lrg-debug-overlay.h"
#include "../../debug/lrg-debug-console.h"
#include "../../debug/lrg-inspector.h"
#include "../../ui/lrg-ui-inspector.h"
#include "../../ui/lrg-widget.h"
#include "../../lrg-enums.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>
#include <string.h>

/**
 * SECTION:lrg-mcp-debug-tools
 * @title: LrgMcpDebugTools
 * @short_description: MCP tools for debugging
 *
 * #LrgMcpDebugTools exposes the engine's debug module over MCP: logging,
 * the default #LrgProfiler (sections, frame statistics, reports), the
 * default #LrgDebugOverlay (visibility, flags, custom lines, rendered
 * text), the default #LrgDebugConsole (command execution and output) and
 * the default #LrgInspector (world, object and component inspection) and
 * the default #LrgUIInspector (retained UI tree, picking, highlighting,
 * live property edits and canvas timings; the app registers its canvas
 * with lrg_ui_inspector_register_canvas()).
 *
 * Every tool returns a JSON object as its single text content.
 */

struct _LrgMcpDebugTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpDebugTools, lrg_mcp_debug_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Overlay flag names
 * ========================================================================== */

/*
 * The overlay flags by their MCP names. "custom" is listed here because
 * the registered GFlags type predates LRG_DEBUG_OVERLAY_CUSTOM.
 */
static const struct
{
	const gchar         *name;
	LrgDebugOverlayFlags flag;
} overlay_flag_names[] = {
	{ "fps",        LRG_DEBUG_OVERLAY_FPS },
	{ "frame-time", LRG_DEBUG_OVERLAY_FRAME_TIME },
	{ "memory",     LRG_DEBUG_OVERLAY_MEMORY },
	{ "entities",   LRG_DEBUG_OVERLAY_ENTITIES },
	{ "physics",    LRG_DEBUG_OVERLAY_PHYSICS },
	{ "colliders",  LRG_DEBUG_OVERLAY_COLLIDERS },
	{ "profiler",   LRG_DEBUG_OVERLAY_PROFILER },
	{ "custom",     LRG_DEBUG_OVERLAY_CUSTOM },
};

/*
 * parse_overlay_flags:
 *
 * Parses a comma- or space-separated list of flag names ("fps,profiler",
 * "all", "none"). Returns %FALSE and sets @error on an unknown name.
 */
static gboolean
parse_overlay_flags (const gchar           *text,
                     LrgDebugOverlayFlags  *flags,
                     GError               **error)
{
	g_auto(GStrv) words = NULL;
	guint i;
	guint j;

	*flags = LRG_DEBUG_OVERLAY_NONE;
	words = g_strsplit_set (text, ", ", -1);
	for (i = 0; words[i] != NULL; i++)
	{
		gboolean found;

		if (*words[i] == '\0' || g_ascii_strcasecmp (words[i], "none") == 0)
			continue;
		if (g_ascii_strcasecmp (words[i], "all") == 0)
		{
			*flags |= LRG_DEBUG_OVERLAY_ALL;
			continue;
		}
		found = FALSE;
		for (j = 0; j < G_N_ELEMENTS (overlay_flag_names); j++)
		{
			if (g_ascii_strcasecmp (words[i], overlay_flag_names[j].name) == 0)
			{
				*flags |= overlay_flag_names[j].flag;
				found = TRUE;
				break;
			}
		}
		if (!found)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Unknown overlay flag: %s", words[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/* ==========================================================================
 * JSON helpers
 * ========================================================================== */

/*
 * section_json:
 *
 * Statistics for one profiler section, all durations in milliseconds.
 */
static JsonObject *
section_json (LrgProfiler *profiler,
              const gchar *name)
{
	JsonObject *json = json_object_new ();
	LrgProfilerSample *last;

	json_object_set_string_member (json, "name", name);
	json_object_set_boolean_member (json, "active",
	                                lrg_profiler_is_section_active (profiler, name));
	json_object_set_int_member (json, "sample_count",
	                            lrg_profiler_get_sample_count (profiler, name));
	last = lrg_profiler_get_last_sample (profiler, name);
	if (last != NULL)
	{
		json_object_set_double_member (json, "last_ms",
		                               lrg_profiler_sample_get_duration_ms (last));
		lrg_profiler_sample_free (last);
	}
	else
		json_object_set_null_member (json, "last_ms");
	json_object_set_double_member (json, "average_ms",
	                               lrg_profiler_get_average_ms (profiler, name));
	json_object_set_double_member (json, "min_ms",
	                               lrg_profiler_get_min_ms (profiler, name));
	json_object_set_double_member (json, "max_ms",
	                               lrg_profiler_get_max_ms (profiler, name));
	return json;
}

/*
 * overlay_json:
 *
 * The overlay's configuration plus the text it would draw this frame.
 */
static JsonObject *
overlay_json (LrgDebugOverlay *overlay)
{
	JsonObject *json = json_object_new ();
	JsonObject *position = json_object_new ();
	JsonArray *flags = json_array_new ();
	LrgDebugOverlayFlags value;
	g_autofree gchar *text = NULL;
	gint x;
	gint y;
	guint i;

	value = lrg_debug_overlay_get_flags (overlay);
	for (i = 0; i < G_N_ELEMENTS (overlay_flag_names); i++)
	{
		if ((value & overlay_flag_names[i].flag) != 0)
			json_array_add_string_element (flags, overlay_flag_names[i].name);
	}
	lrg_debug_overlay_get_position (overlay, &x, &y);
	json_object_set_int_member (position, "x", x);
	json_object_set_int_member (position, "y", y);
	text = lrg_debug_overlay_get_text (overlay);

	json_object_set_boolean_member (json, "visible", lrg_debug_overlay_is_visible (overlay));
	json_object_set_array_member (json, "flags", flags);
	json_object_set_int_member (json, "flags_value", value);
	json_object_set_object_member (json, "position", position);
	json_object_set_int_member (json, "font_size", lrg_debug_overlay_get_font_size (overlay));
	json_object_set_int_member (json, "padding", lrg_debug_overlay_get_padding (overlay));
	json_object_set_int_member (json, "line_count", lrg_debug_overlay_get_line_count (overlay));
	json_object_set_string_member (json, "text", text != NULL ? text : "");
	return json;
}

/* ==========================================================================
 * Tool Handlers - logging and frame statistics
 * ========================================================================== */

/*
 * handle_log:
 *
 * Logs through GLib under the MCP domain and mirrors the line into the
 * default debug console so it shows up in-game. "error" is logged as
 * critical: G_LOG_LEVEL_ERROR would abort the game.
 */
static McpToolResult *
handle_log (LrgMcpDebugTools *self,
            JsonObject       *arguments,
            GError          **error)
{
	const gchar *message;
	const gchar *level_str;
	GLogLevelFlags level;
	JsonObject *json;
	LrgDebugConsole *console;

	message = lrg_mcp_tool_group_get_string_arg (arguments, "message", NULL);
	if (message == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: message");
		return NULL;
	}

	level_str = lrg_mcp_tool_group_get_string_arg (arguments, "level", "info");

	/* Parse log level */
	if (g_ascii_strcasecmp (level_str, "debug") == 0)
		level = G_LOG_LEVEL_DEBUG;
	else if (g_ascii_strcasecmp (level_str, "info") == 0)
		level = G_LOG_LEVEL_INFO;
	else if (g_ascii_strcasecmp (level_str, "warning") == 0)
		level = G_LOG_LEVEL_WARNING;
	else if (g_ascii_strcasecmp (level_str, "error") == 0 ||
	         g_ascii_strcasecmp (level_str, "critical") == 0)
		level = G_LOG_LEVEL_CRITICAL;
	else
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Unknown log level: %s", level_str);
		return NULL;
	}

	g_log (LRG_LOG_DOMAIN_MCP, level, "[MCP] %s", message);

	/* Mirror into the in-game console */
	console = lrg_debug_console_get_default ();
	if (level == G_LOG_LEVEL_CRITICAL || level == G_LOG_LEVEL_WARNING)
		lrg_debug_console_print_error (console, message);
	else
		lrg_debug_console_print (console, message);

	json = json_object_new ();
	json_object_set_boolean_member (json, "logged", TRUE);
	json_object_set_string_member (json, "level", level_str);
	json_object_set_string_member (json, "message", message);
	return _lrg_mcp_result (json);
}

/*
 * handle_get_fps:
 *
 * Profiler frame statistics (filled when the game brackets frames with
 * lrg_profiler_begin_frame()/end_frame()) and, when the engine owns a
 * window, the window's own FPS counters.
 */
static McpToolResult *
handle_get_fps (LrgMcpDebugTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	LrgEngine *engine;
	LrgProfiler *profiler;
	LrgWindow *window;
	JsonObject *json;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}
	profiler = lrg_profiler_get_default ();

	json = json_object_new ();
	json_object_set_string_member (json, "state",
	                               lrg_engine_is_running (engine) ? "running" : "stopped");
	json_object_set_boolean_member (json, "profiler_enabled",
	                                lrg_profiler_is_enabled (profiler));
	json_object_set_double_member (json, "fps", lrg_profiler_get_fps (profiler));
	json_object_set_double_member (json, "frame_time_ms",
	                               lrg_profiler_get_frame_time_ms (profiler));

	window = lrg_engine_get_window (engine);
	if (window != NULL)
	{
		JsonObject *win = json_object_new ();

		json_object_set_int_member (win, "fps", lrg_window_get_fps (window));
		json_object_set_double_member (win, "frame_time_ms",
		                               (gdouble)lrg_window_get_frame_time (window) * 1000.0);
		json_object_set_int_member (win, "target_fps", lrg_window_get_target_fps (window));
		json_object_set_object_member (json, "window", win);
	}
	else
		json_object_set_null_member (json, "window");

	return _lrg_mcp_result (json);
}

/* ==========================================================================
 * Tool Handlers - profiler
 * ========================================================================== */

/*
 * handle_profiler_start:
 *
 * Enables the default profiler if needed and opens the named section.
 */
static McpToolResult *
handle_profiler_start (LrgMcpDebugTools *self,
                       JsonObject       *arguments,
                       GError          **error)
{
	LrgProfiler *profiler;
	const gchar *name;
	JsonObject *json;

	name = lrg_mcp_tool_group_get_string_arg (arguments, "name", NULL);
	if (name == NULL || *name == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: name");
		return NULL;
	}

	profiler = lrg_profiler_get_default ();
	if (!lrg_profiler_is_enabled (profiler))
		lrg_profiler_set_enabled (profiler, TRUE);
	lrg_profiler_begin_section (profiler, name);

	json = json_object_new ();
	json_object_set_string_member (json, "name", name);
	json_object_set_boolean_member (json, "active", TRUE);
	json_object_set_boolean_member (json, "profiler_enabled", TRUE);
	return _lrg_mcp_result (json);
}

/*
 * handle_profiler_stop:
 *
 * Closes an open section and returns its updated statistics. Closing a
 * section that is not open is reported as NOT_FOUND rather than letting
 * the profiler warn.
 */
static McpToolResult *
handle_profiler_stop (LrgMcpDebugTools *self,
                      JsonObject       *arguments,
                      GError          **error)
{
	LrgProfiler *profiler;
	const gchar *name;

	name = lrg_mcp_tool_group_get_string_arg (arguments, "name", NULL);
	if (name == NULL || *name == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: name");
		return NULL;
	}

	profiler = lrg_profiler_get_default ();
	if (!lrg_profiler_is_enabled (profiler) ||
	    !lrg_profiler_is_section_active (profiler, name))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "Profiler section is not running: %s", name);
		return NULL;
	}
	lrg_profiler_end_section (profiler, name);

	return _lrg_mcp_result (section_json (profiler, name));
}

/*
 * handle_profiler_report:
 *
 * Every recorded section (sorted by name), the open sections and the
 * frame statistics of the default profiler.
 */
static McpToolResult *
handle_profiler_report (LrgMcpDebugTools *self,
                        JsonObject       *arguments,
                        GError          **error)
{
	LrgProfiler *profiler = lrg_profiler_get_default ();
	JsonObject *json = json_object_new ();
	JsonArray *sections = json_array_new ();
	JsonArray *active = json_array_new ();
	GList *names;
	GList *open;
	GList *iter;

	names = g_list_sort (lrg_profiler_get_section_names (profiler),
	                     (GCompareFunc)g_strcmp0);
	for (iter = names; iter != NULL; iter = iter->next)
		json_array_add_object_element (sections, section_json (profiler, iter->data));
	g_list_free (names);

	open = g_list_sort (lrg_profiler_get_active_section_names (profiler),
	                    (GCompareFunc)g_strcmp0);
	for (iter = open; iter != NULL; iter = iter->next)
		json_array_add_string_element (active, iter->data);
	g_list_free (open);

	json_object_set_boolean_member (json, "enabled", lrg_profiler_is_enabled (profiler));
	json_object_set_int_member (json, "max_samples", lrg_profiler_get_max_samples (profiler));
	json_object_set_double_member (json, "fps", lrg_profiler_get_fps (profiler));
	json_object_set_double_member (json, "frame_time_ms",
	                               lrg_profiler_get_frame_time_ms (profiler));
	json_object_set_array_member (json, "active", active);
	json_object_set_array_member (json, "sections", sections);
	return _lrg_mcp_result (json);
}

/*
 * handle_profiler_clear:
 *
 * Drops one section's samples, or all samples when no name is given.
 */
static McpToolResult *
handle_profiler_clear (LrgMcpDebugTools *self,
                       JsonObject       *arguments,
                       GError          **error)
{
	LrgProfiler *profiler = lrg_profiler_get_default ();
	const gchar *name;
	JsonObject *json;

	name = lrg_mcp_tool_group_get_string_arg (arguments, "name", NULL);
	if (name != NULL && *name != '\0')
		lrg_profiler_clear_section (profiler, name);
	else
		lrg_profiler_clear (profiler);

	json = json_object_new ();
	json_object_set_boolean_member (json, "cleared", TRUE);
	if (name != NULL && *name != '\0')
		json_object_set_string_member (json, "name", name);
	return _lrg_mcp_result (json);
}

/* ==========================================================================
 * Tool Handlers - overlay
 * ========================================================================== */

static McpToolResult *
handle_overlay_get (LrgMcpDebugTools *self,
                    JsonObject       *arguments,
                    GError          **error)
{
	return _lrg_mcp_result (overlay_json (lrg_debug_overlay_get_default ()));
}

/*
 * handle_overlay_set:
 *
 * Applies any of: visible, flags (replaces the set), custom_key plus
 * custom_text (sets a custom line; an empty or missing text removes it).
 * All arguments are validated before anything changes.
 */
static McpToolResult *
handle_overlay_set (LrgMcpDebugTools *self,
                    JsonObject       *arguments,
                    GError          **error)
{
	LrgDebugOverlay *overlay = lrg_debug_overlay_get_default ();
	LrgDebugOverlayFlags flags = LRG_DEBUG_OVERLAY_NONE;
	const gchar *flags_text;
	const gchar *key;
	const gchar *text;

	flags_text = lrg_mcp_tool_group_get_string_arg (arguments, "flags", NULL);
	key = lrg_mcp_tool_group_get_string_arg (arguments, "custom_key", NULL);
	text = lrg_mcp_tool_group_get_string_arg (arguments, "custom_text", NULL);

	/* Validate first so a bad call leaves the overlay untouched */
	if (flags_text != NULL && !parse_overlay_flags (flags_text, &flags, error))
		return NULL;
	if (text != NULL && (key == NULL || *key == '\0'))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "custom_text requires custom_key");
		return NULL;
	}

	if (arguments != NULL && json_object_has_member (arguments, "visible"))
		lrg_debug_overlay_set_visible (overlay,
		                               lrg_mcp_tool_group_get_bool_arg (arguments, "visible", FALSE));
	if (flags_text != NULL)
		lrg_debug_overlay_set_flags (overlay, flags);
	if (key != NULL && *key != '\0')
	{
		if (text != NULL && *text != '\0')
			lrg_debug_overlay_set_custom_line (overlay, key, "%s", text);
		else
			lrg_debug_overlay_remove_custom_line (overlay, key);
	}

	return _lrg_mcp_result (overlay_json (overlay));
}

/* ==========================================================================
 * Tool Handlers - console
 * ========================================================================== */

/*
 * handle_console_execute:
 *
 * Runs a command line through the default debug console (registered
 * commands, or the scripting evaluator if one is installed). Console
 * errors are returned as tool errors in the LRG_DEBUG_ERROR domain.
 */
static McpToolResult *
handle_console_execute (LrgMcpDebugTools *self,
                        JsonObject       *arguments,
                        GError          **error)
{
	LrgDebugConsole *console = lrg_debug_console_get_default ();
	const gchar *command;
	g_autofree gchar *output = NULL;
	JsonObject *json;

	command = lrg_mcp_tool_group_get_string_arg (arguments, "command", NULL);
	if (command == NULL || *command == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: command");
		return NULL;
	}

	output = lrg_debug_console_execute (console, command, error);
	if (output == NULL && error != NULL && *error != NULL)
		return NULL;

	json = json_object_new ();
	json_object_set_string_member (json, "command", command);
	json_object_set_string_member (json, "output", output != NULL ? output : "");
	return _lrg_mcp_result (json);
}

/*
 * handle_console_get_output:
 *
 * The newest @limit console lines (default 50, oldest first), plus the
 * registered command names.
 */
static McpToolResult *
handle_console_get_output (LrgMcpDebugTools *self,
                           JsonObject       *arguments,
                           GError          **error)
{
	LrgDebugConsole *console = lrg_debug_console_get_default ();
	GQueue *output = lrg_debug_console_get_output (console);
	JsonObject *json = json_object_new ();
	JsonArray *entries = json_array_new ();
	JsonArray *commands = json_array_new ();
	GList *names;
	GList *iter;
	gint64 limit;
	guint skip;

	limit = lrg_mcp_tool_group_get_int_arg (arguments, "limit", 50);
	if (limit < 0)
		limit = 0;
	skip = output->length > (guint)limit ? output->length - (guint)limit : 0;

	for (iter = output->head; iter != NULL; iter = iter->next)
	{
		const LrgConsoleOutput *line = iter->data;
		JsonObject *entry;

		if (skip > 0)
		{
			skip--;
			continue;
		}
		entry = json_object_new ();
		json_object_set_string_member (entry, "text", lrg_console_output_get_text (line));
		json_object_set_boolean_member (entry, "command", lrg_console_output_is_command (line));
		json_object_set_boolean_member (entry, "error", lrg_console_output_is_error (line));
		json_object_set_int_member (entry, "timestamp", lrg_console_output_get_timestamp (line));
		json_array_add_object_element (entries, entry);
	}

	names = g_list_sort (lrg_debug_console_get_commands (console), (GCompareFunc)g_strcmp0);
	for (iter = names; iter != NULL; iter = iter->next)
		json_array_add_string_element (commands, iter->data);
	g_list_free (names);

	json_object_set_boolean_member (json, "visible", lrg_debug_console_is_visible (console));
	json_object_set_int_member (json, "total", output->length);
	json_object_set_array_member (json, "entries", entries);
	json_object_set_array_member (json, "commands", commands);
	return _lrg_mcp_result (json);
}

/* ==========================================================================
 * Tool Handlers - inspector
 * ========================================================================== */

/*
 * handle_inspect:
 *
 * Points the default inspector at a world (named, or the first active
 * one) and optionally selects a game object by MCP id, then returns the
 * inspector's world/object views plus each component's properties. The
 * selection is left in place so the in-game inspector shows it too.
 */
static McpToolResult *
handle_inspect (LrgMcpDebugTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	LrgInspector *inspector = lrg_inspector_get_default ();
	const gchar *world_name;
	const gchar *object_id;
	LrgWorld *world = NULL;
	LrgGameObject *object = NULL;
	g_autofree gchar *world_info = NULL;
	JsonObject *json;

	world_name = lrg_mcp_tool_group_get_string_arg (arguments, "world", NULL);
	object_id = lrg_mcp_tool_group_get_string_arg (arguments, "object_id", NULL);

	/* Resolve the target; an object id decides its own world */
	if (object_id != NULL && *object_id != '\0')
	{
		object = _lrg_mcp_find_object (object_id, &world, error);
		if (object == NULL)
			return NULL;
	}
	else
	{
		world = _lrg_mcp_world (world_name, error);
		if (world == NULL)
			return NULL;
	}

	if (lrg_inspector_get_world (inspector) != world)
		lrg_inspector_set_world (inspector, world);
	if (object != NULL)
		lrg_inspector_select_object (inspector, object);

	world_info = lrg_inspector_get_world_info (inspector);
	json = json_object_new ();
	json_object_set_boolean_member (json, "visible", lrg_inspector_is_visible (inspector));
	json_object_set_int_member (json, "object_count", lrg_inspector_get_object_count (inspector));
	json_object_set_string_member (json, "world_info", world_info != NULL ? world_info : "");

	/* Selected object: inspector text plus structured components */
	object = lrg_inspector_get_selected_object (inspector);
	if (object != NULL)
	{
		JsonObject *selected = _lrg_mcp_object_json (object);
		JsonArray *components = json_array_new ();
		g_autofree gchar *object_info = lrg_inspector_get_object_info (inspector);
		GList *list;
		GList *iter;

		list = lrg_inspector_get_components (inspector);
		for (iter = list; iter != NULL; iter = iter->next)
			json_array_add_object_element (components, _lrg_mcp_component_json (iter->data));
		g_list_free (list);

		json_object_set_string_member (selected, "info", object_info != NULL ? object_info : "");
		json_object_set_array_member (selected, "component_properties", components);
		json_object_set_object_member (json, "selected", selected);
	}
	else
		json_object_set_null_member (json, "selected");

	return _lrg_mcp_result (json);
}

/* ==========================================================================
 * Tool Handlers - UI inspector
 * ========================================================================== */

/*
 * The UI tools all talk to lrg_ui_inspector_get_default(); the app makes
 * its canvas visible with lrg_ui_inspector_register_canvas(). Results
 * are the inspector's own JSON text.
 */

/* text_result: wrap JSON text (taken) as the tool's single text content */
static McpToolResult *
text_result (gchar *text)
{
	McpToolResult *result = mcp_tool_result_new (FALSE);

	mcp_tool_result_add_text (result, text);
	g_free (text);
	return result;
}

/* ui_inspector: the default inspector, or an error when no canvas is registered */
static LrgUIInspector *
ui_inspector (GError **error)
{
	LrgUIInspector *inspector = lrg_ui_inspector_get_default ();

	if (lrg_ui_inspector_get_canvas (inspector) == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                     "No UI canvas registered (call lrg_ui_inspector_register_canvas())");
		return NULL;
	}
	return inspector;
}

/* describe: the widget at @path as JSON text, or an error */
static McpToolResult *
describe (LrgUIInspector  *inspector,
          LrgWidget       *widget,
          GError         **error)
{
	gchar *text = lrg_ui_inspector_describe_widget (inspector, widget, FALSE);

	if (text == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                     "Widget is not addressable by path (internal child)");
		return NULL;
	}
	return text_result (text);
}

static McpToolResult *
handle_ui_tree (LrgMcpDebugTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	LrgUIInspector *inspector = ui_inspector (error);

	if (inspector == NULL)
		return NULL;
	return text_result (lrg_ui_inspector_snapshot_json (inspector, FALSE));
}

/*
 * handle_ui_pick:
 *
 * The topmost widget at (x, y), as the canvas hit-tests clicks; with
 * select=true it also becomes the highlighted selection. Empty space
 * returns {"path": null}.
 */
static McpToolResult *
handle_ui_pick (LrgMcpDebugTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	LrgUIInspector *inspector = ui_inspector (error);
	LrgWidget      *widget;
	gdouble         x;
	gdouble         y;

	if (inspector == NULL)
		return NULL;

	x = lrg_mcp_tool_group_get_double_arg (arguments, "x", 0.0);
	y = lrg_mcp_tool_group_get_double_arg (arguments, "y", 0.0);
	widget = lrg_ui_inspector_pick (inspector, (gfloat) x, (gfloat) y);

	/* Internal children (kept out of their parent's child list) have no
	 * path: report the nearest addressable ancestor instead */
	while (widget != NULL)
	{
		g_autofree gchar *path = lrg_ui_inspector_get_path (inspector, widget);

		if (path != NULL)
			break;
		widget = (LrgWidget *) lrg_widget_get_parent (widget);
	}
	if (lrg_mcp_tool_group_get_bool_arg (arguments, "select", FALSE))
		lrg_ui_inspector_set_selected (inspector, widget);
	if (widget == NULL)
		return text_result (g_strdup ("{\"path\":null}"));
	return describe (inspector, widget, error);
}

static McpToolResult *
handle_ui_get_widget (LrgMcpDebugTools *self,
                      JsonObject       *arguments,
                      GError          **error)
{
	LrgUIInspector *inspector = ui_inspector (error);
	const gchar    *path;
	LrgWidget      *widget;

	if (inspector == NULL)
		return NULL;
	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", "");
	widget = lrg_ui_inspector_lookup (inspector, path);
	if (widget == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No widget at path \"%s\"", path);
		return NULL;
	}
	return describe (inspector, widget, error);
}

/*
 * handle_ui_set_property:
 *
 * Strings go through lrg_ui_inspector_set_property_from_string() (enum
 * nicks, flags "a|b", colours "#rrggbbaa", numbers as text); JSON
 * numbers and booleans are converted to the property type. Returns the
 * widget's description after the change.
 */
static McpToolResult *
handle_ui_set_property (LrgMcpDebugTools *self,
                        JsonObject       *arguments,
                        GError          **error)
{
	LrgUIInspector *inspector = ui_inspector (error);
	const gchar    *path;
	const gchar    *name;
	JsonNode       *node;
	GValue          value = G_VALUE_INIT;
	gboolean        ok;
	GType           input;

	if (inspector == NULL)
		return NULL;
	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", "");
	name = lrg_mcp_tool_group_get_string_arg (arguments, "property", "");
	node = json_object_get_member (arguments, "value");

	input = JSON_NODE_HOLDS_VALUE (node) ? json_node_get_value_type (node) : G_TYPE_INVALID;
	if (input == G_TYPE_STRING)
		ok = lrg_ui_inspector_set_property_from_string (inspector, path, name,
		                                                json_node_get_string (node), error);
	else
	{
		if (input == G_TYPE_BOOLEAN)
		{
			g_value_init (&value, G_TYPE_BOOLEAN);
			g_value_set_boolean (&value, json_node_get_boolean (node));
		}
		else if (input == G_TYPE_INT64)
		{
			g_value_init (&value, G_TYPE_INT64);
			g_value_set_int64 (&value, json_node_get_int (node));
		}
		else if (input == G_TYPE_DOUBLE)
		{
			g_value_init (&value, G_TYPE_DOUBLE);
			g_value_set_double (&value, json_node_get_double (node));
		}
		else if (JSON_NODE_HOLDS_NULL (node))
		{
			g_value_init (&value, G_TYPE_STRING);
			g_value_set_string (&value, NULL);
		}
		else
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                     "value must be a string, number, boolean or null");
			return NULL;
		}
		ok = lrg_ui_inspector_set_property (inspector, path, name, &value, error);
		g_value_unset (&value);
	}
	if (!ok)
		return NULL;
	return describe (inspector, lrg_ui_inspector_lookup (inspector, path), error);
}

/*
 * handle_ui_highlight:
 *
 * Selects (and so highlights) the widget at path; without a path the
 * selection is cleared. pick_mode switches interactive picking.
 */
static McpToolResult *
handle_ui_highlight (LrgMcpDebugTools *self,
                     JsonObject       *arguments,
                     GError          **error)
{
	LrgUIInspector   *inspector = ui_inspector (error);
	const gchar      *path;
	g_autofree gchar *selected = NULL;
	JsonObject       *json;

	if (inspector == NULL)
		return NULL;
	path = lrg_mcp_tool_group_get_string_arg (arguments, "path", NULL);
	if (!lrg_ui_inspector_highlight (inspector, path, error))
		return NULL;
	if (arguments != NULL && json_object_has_member (arguments, "pick_mode"))
		lrg_ui_inspector_set_pick_mode (inspector, json_object_get_boolean_member (arguments, "pick_mode"));

	json = json_object_new ();
	if (lrg_ui_inspector_get_selected (inspector) != NULL)
		selected = lrg_ui_inspector_get_path (inspector, lrg_ui_inspector_get_selected (inspector));
	if (selected != NULL)
		json_object_set_string_member (json, "selected", selected);
	else
		json_object_set_null_member (json, "selected");
	json_object_set_boolean_member (json, "pick_mode", lrg_ui_inspector_get_pick_mode (inspector));
	return _lrg_mcp_result (json);
}

static McpToolResult *
handle_ui_timings (LrgMcpDebugTools *self,
                   JsonObject       *arguments,
                   GError          **error)
{
	LrgUIInspector        *inspector = ui_inspector (error);
	LrgUIInspectorTimings  t;
	JsonObject            *json;

	if (inspector == NULL)
		return NULL;
	lrg_ui_inspector_get_timings (inspector, &t);
	json = json_object_new ();
	json_object_set_int_member (json, "step_us", t.step_us);
	json_object_set_int_member (json, "layout_us", t.layout_us);
	json_object_set_int_member (json, "record_us", t.record_us);
	json_object_set_int_member (json, "n_instances", t.n_instances);
	json_object_set_int_member (json, "n_commands", t.n_commands);
	json_object_set_int_member (json, "draw_calls", t.draw_calls);
	json_object_set_int_member (json, "frames", (gint64) t.frames);
	return _lrg_mcp_result (json);
}

/* ==========================================================================
 * Tool Table
 * ========================================================================== */

/*
 * DebugToolArg:
 *
 * One input-schema property: JSON type ("string", "integer", "number",
 * "boolean", or "any" for an untyped value), description and whether the caller must supply it. The same rows build
 * the advertised schema and validate incoming arguments.
 */
typedef struct
{
	const gchar *name;
	const gchar *type;
	const gchar *description;
	gboolean     required;
} DebugToolArg;

typedef McpToolResult * (*DebugToolHandler) (LrgMcpDebugTools  *self,
                                             JsonObject        *arguments,
                                             GError           **error);

/*
 * DebugTool:
 *
 * One tool: name, description, argument rows and handler.
 */
typedef struct
{
	const gchar        *name;
	const gchar        *description;
	const DebugToolArg *args;
	guint               n_args;
	DebugToolHandler    handler;
} DebugTool;

static const DebugToolArg log_args[] = {
	{ "message", "string", "Message to log", TRUE },
	{ "level", "string", "Log level (debug, info, warning, error, critical)", FALSE },
};
static const DebugToolArg section_args[] = {
	{ "name", "string", "Section name", TRUE },
};
static const DebugToolArg clear_args[] = {
	{ "name", "string", "Section to clear (all sections when omitted)", FALSE },
};
static const DebugToolArg overlay_args[] = {
	{ "visible", "boolean", "Show or hide the overlay", FALSE },
	{ "flags", "string",
	  "Comma-separated flags replacing the current set: fps, frame-time, memory, "
	  "entities, physics, colliders, profiler, custom, all, none", FALSE },
	{ "custom_key", "string", "Custom line key", FALSE },
	{ "custom_text", "string", "Custom line text (empty or omitted removes the line)", FALSE },
};
static const DebugToolArg execute_args[] = {
	{ "command", "string", "Console command line", TRUE },
};
static const DebugToolArg output_args[] = {
	{ "limit", "integer", "Maximum number of newest lines to return (default 50)", FALSE },
};
static const DebugToolArg ui_pick_args[] = {
	{ "x", "number", "X in canvas coordinates", TRUE },
	{ "y", "number", "Y in canvas coordinates", TRUE },
	{ "select", "boolean", "Also select (highlight) the picked widget", FALSE },
};
static const DebugToolArg ui_path_args[] = {
	{ "path", "string", "Widget path, e.g. \"LrgVBox#main/LrgButton#ok\" (\"\" is the canvas)", TRUE },
};
static const DebugToolArg ui_set_property_args[] = {
	{ "path", "string", "Widget path", TRUE },
	{ "property", "string", "GObject property name", TRUE },
	{ "value", "any", "New value: number, boolean, null or text (enum nick, flags a|b, #rrggbbaa)", TRUE },
};
static const DebugToolArg ui_highlight_args[] = {
	{ "path", "string", "Widget path to select and highlight (clears the selection when omitted)", FALSE },
	{ "pick_mode", "boolean", "Turn interactive pick mode on or off", FALSE },
};
static const DebugToolArg inspect_args[] = {
	{ "world", "string", "World name (first active world when omitted)", FALSE },
	{ "object_id", "string", "Game object id to select (from the ECS tools)", FALSE },
};

#define TOOL_ARGS(a) (a), G_N_ELEMENTS (a)

static const DebugTool debug_tools[] = {
	{ "lrg_debug_log", "Log a message to the engine log and the debug console",
	  TOOL_ARGS (log_args), handle_log },
	{ "lrg_debug_get_fps", "Get FPS and frame timing from the profiler and the window",
	  NULL, 0, handle_get_fps },
	{ "lrg_debug_profiler_start", "Start timing a named profiler section (enables the profiler)",
	  TOOL_ARGS (section_args), handle_profiler_start },
	{ "lrg_debug_profiler_stop", "Stop a running profiler section and return its statistics",
	  TOOL_ARGS (section_args), handle_profiler_stop },
	{ "lrg_debug_profiler_report", "Get the profiler report: every section's timings and frame statistics",
	  NULL, 0, handle_profiler_report },
	{ "lrg_debug_profiler_clear", "Clear recorded profiler samples",
	  TOOL_ARGS (clear_args), handle_profiler_clear },
	{ "lrg_debug_overlay_get", "Get the debug overlay's visibility, flags and rendered text",
	  NULL, 0, handle_overlay_get },
	{ "lrg_debug_overlay_set", "Change debug overlay visibility, flags or custom lines",
	  TOOL_ARGS (overlay_args), handle_overlay_set },
	{ "lrg_debug_console_execute", "Execute a debug console command and return its output",
	  TOOL_ARGS (execute_args), handle_console_execute },
	{ "lrg_debug_console_get_output", "Get recent debug console output and the registered commands",
	  TOOL_ARGS (output_args), handle_console_get_output },
	{ "lrg_debug_inspect", "Inspect a world or game object with the debug inspector",
	  TOOL_ARGS (inspect_args), handle_inspect },
	{ "lrg_ui_tree", "Snapshot the registered UI canvas: widget tree with paths, geometry, state and looks",
	  NULL, 0, handle_ui_tree },
	{ "lrg_ui_pick", "Find the topmost UI widget at a canvas point (as clicks hit-test)",
	  TOOL_ARGS (ui_pick_args), handle_ui_pick },
	{ "lrg_ui_get_widget", "Describe one UI widget by path, including all its properties",
	  TOOL_ARGS (ui_path_args), handle_ui_get_widget },
	{ "lrg_ui_set_property", "Set a UI widget property by name for live tweaking",
	  TOOL_ARGS (ui_set_property_args), handle_ui_set_property },
	{ "lrg_ui_highlight", "Select and highlight a UI widget by path, or toggle pick mode",
	  TOOL_ARGS (ui_highlight_args), handle_ui_highlight },
	{ "lrg_ui_timings", "Get the UI canvas frame timings (step, layout, record) and paint counts",
	  NULL, 0, handle_ui_timings },
};

/*
 * validate_args:
 *
 * Rejects missing required arguments, unknown arguments and values of the
 * wrong JSON type, so handlers can read arguments without type checks.
 */
static gboolean
validate_args (const DebugTool  *tool,
               JsonObject       *arguments,
               GError          **error)
{
	g_autoptr(GList) members = NULL;
	GList *iter;
	guint i;

	for (i = 0; i < tool->n_args; i++)
	{
		if (tool->args[i].required &&
		    (arguments == NULL || !json_object_has_member (arguments, tool->args[i].name)))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Missing required argument: %s", tool->args[i].name);
			return FALSE;
		}
	}
	if (arguments == NULL)
		return TRUE;

	members = json_object_get_members (arguments);
	for (iter = members; iter != NULL; iter = iter->next)
	{
		const gchar *name = iter->data;
		JsonNode *node = json_object_get_member (arguments, name);
		const DebugToolArg *spec = NULL;
		GType type;
		gboolean valid;

		for (i = 0; i < tool->n_args; i++)
		{
			if (g_str_equal (tool->args[i].name, name))
			{
				spec = &tool->args[i];
				break;
			}
		}
		if (spec == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Unknown argument: %s", name);
			return FALSE;
		}

		type = JSON_NODE_HOLDS_VALUE (node) ? json_node_get_value_type (node) : G_TYPE_INVALID;
		if (g_str_equal (spec->type, "any"))
			valid = TRUE;
		else if (g_str_equal (spec->type, "string"))
			valid = type == G_TYPE_STRING;
		else if (g_str_equal (spec->type, "integer"))
			valid = type == G_TYPE_INT64;
		else if (g_str_equal (spec->type, "number"))
			valid = type == G_TYPE_INT64 || type == G_TYPE_DOUBLE;
		else
			valid = type == G_TYPE_BOOLEAN;
		if (!valid)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			             "Argument %s must be a %s", name, spec->type);
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * add_tool:
 *
 * Registers @tool with an object schema built from its argument rows.
 */
static void
add_tool (LrgMcpToolGroup *group,
          const DebugTool *tool)
{
	g_autoptr(JsonNode) schema = json_node_new (JSON_NODE_OBJECT);
	JsonObject *root = json_object_new ();
	JsonObject *properties = json_object_new ();
	JsonArray *required = json_array_new ();
	McpTool *mcp_tool;
	guint i;

	for (i = 0; i < tool->n_args; i++)
	{
		JsonObject *property = json_object_new ();

		if (!g_str_equal (tool->args[i].type, "any"))
			json_object_set_string_member (property, "type", tool->args[i].type);
		json_object_set_string_member (property, "description", tool->args[i].description);
		json_object_set_object_member (properties, tool->args[i].name, property);
		if (tool->args[i].required)
			json_array_add_string_element (required, tool->args[i].name);
	}
	json_object_set_string_member (root, "type", "object");
	json_object_set_boolean_member (root, "additionalProperties", FALSE);
	json_object_set_object_member (root, "properties", properties);
	json_object_set_array_member (root, "required", required);
	json_node_take_object (schema, root);

	mcp_tool = mcp_tool_new (tool->name, tool->description);
	mcp_tool_set_input_schema (mcp_tool, schema);
	lrg_mcp_tool_group_add_tool (group, mcp_tool);
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_debug_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "debug";
}

static void
lrg_mcp_debug_tools_register_tools (LrgMcpToolGroup *group)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (debug_tools); i++)
		add_tool (group, &debug_tools[i]);
}

static McpToolResult *
lrg_mcp_debug_tools_handle_tool (LrgMcpToolGroup  *group,
                                 const gchar      *name,
                                 JsonObject       *arguments,
                                 GError          **error)
{
	LrgMcpDebugTools *self = LRG_MCP_DEBUG_TOOLS (group);
	guint i;

	for (i = 0; i < G_N_ELEMENTS (debug_tools); i++)
	{
		if (g_strcmp0 (name, debug_tools[i].name) != 0)
			continue;
		if (!validate_args (&debug_tools[i], arguments, error))
			return NULL;
		return debug_tools[i].handler (self, arguments, error);
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_debug_tools_class_init (LrgMcpDebugToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_debug_tools_get_group_name;
	group_class->register_tools = lrg_mcp_debug_tools_register_tools;
	group_class->handle_tool = lrg_mcp_debug_tools_handle_tool;
}

static void
lrg_mcp_debug_tools_init (LrgMcpDebugTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_mcp_debug_tools_new:
 *
 * Creates a new debug tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpDebugTools
 */
LrgMcpDebugTools *
lrg_mcp_debug_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_DEBUG_TOOLS, NULL);
}
