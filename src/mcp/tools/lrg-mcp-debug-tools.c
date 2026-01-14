/* lrg-mcp-debug-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for debugging operations.
 */

#include "lrg-mcp-debug-tools.h"
#include "../../core/lrg-engine.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-debug-tools
 * @title: LrgMcpDebugTools
 * @short_description: MCP tools for debugging
 *
 * #LrgMcpDebugTools provides MCP tools for logging, profiling,
 * and other debug operations.
 */

struct _LrgMcpDebugTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpDebugTools, lrg_mcp_debug_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Tool Handlers
 * ========================================================================== */

static McpToolResult *
handle_log (LrgMcpDebugTools *self,
            JsonObject       *arguments,
            GError          **error)
{
	const gchar *message;
	const gchar *level_str;
	GLogLevelFlags level;
	McpToolResult *result;

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
	else if (g_ascii_strcasecmp (level_str, "error") == 0)
		level = G_LOG_LEVEL_ERROR;
	else if (g_ascii_strcasecmp (level_str, "critical") == 0)
		level = G_LOG_LEVEL_CRITICAL;
	else
		level = G_LOG_LEVEL_INFO;

	/* Log the message */
	g_log (LRG_LOG_DOMAIN_MCP, level, "[MCP] %s", message);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, "Message logged");
	return result;
}

static McpToolResult *
handle_get_fps (LrgMcpDebugTools *self,
                JsonObject       *arguments,
                GError          **error)
{
	LrgEngine *engine;
	McpToolResult *result;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	/* NOTE: Detailed FPS/timing stats require profiler API which
	 * may not be implemented yet. Return basic info for now.
	 */
	json_builder_set_member_name (builder, "state");
	json_builder_add_string_value (builder,
	                               lrg_engine_is_running (engine) ? "running" : "stopped");

	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder,
	                               "Detailed FPS statistics require profiler API");

	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, json_str);
	return result;
}

static McpToolResult *
handle_profiler_start (LrgMcpDebugTools *self,
                       JsonObject       *arguments,
                       GError          **error)
{
	const gchar *name;

	name = lrg_mcp_tool_group_get_string_arg (arguments, "name", NULL);
	if (name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: name");
		return NULL;
	}

	/* Stub: Profiler API not yet available */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Profiler API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_profiler_stop (LrgMcpDebugTools *self,
                      JsonObject       *arguments,
                      GError          **error)
{
	const gchar *name;

	name = lrg_mcp_tool_group_get_string_arg (arguments, "name", NULL);
	if (name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: name");
		return NULL;
	}

	/* Stub: Profiler API not yet available */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Profiler API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_profiler_report (LrgMcpDebugTools *self,
                        JsonObject       *arguments,
                        GError          **error)
{
	/* Stub: Profiler API not yet available */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Profiler API not yet implemented");
	return NULL;
}

/* ==========================================================================
 * Schema Builders
 * ========================================================================== */

static JsonNode *
build_schema_log (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "message");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Message to log");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "level");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Log level (debug, info, warning, error, critical)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "message");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_name_required (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "name");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Section name");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "name");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
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
	McpTool *tool;
	JsonNode *schema;

	tool = mcp_tool_new ("lrg_debug_log",
	                     "Log a message to the debug console");
	schema = build_schema_log ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_debug_get_fps",
	                     "Get detailed FPS and frame timing statistics");
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_debug_profiler_start",
	                     "Start a named profiler section");
	schema = build_schema_name_required ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_debug_profiler_stop",
	                     "Stop a named profiler section");
	schema = build_schema_name_required ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_debug_profiler_report",
	                     "Get the current profiler report");
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_debug_tools_handle_tool (LrgMcpToolGroup  *group,
                                 const gchar      *name,
                                 JsonObject       *arguments,
                                 GError          **error)
{
	LrgMcpDebugTools *self = LRG_MCP_DEBUG_TOOLS (group);

	if (g_strcmp0 (name, "lrg_debug_log") == 0)
		return handle_log (self, arguments, error);
	if (g_strcmp0 (name, "lrg_debug_get_fps") == 0)
		return handle_get_fps (self, arguments, error);
	if (g_strcmp0 (name, "lrg_debug_profiler_start") == 0)
		return handle_profiler_start (self, arguments, error);
	if (g_strcmp0 (name, "lrg_debug_profiler_stop") == 0)
		return handle_profiler_stop (self, arguments, error);
	if (g_strcmp0 (name, "lrg_debug_profiler_report") == 0)
		return handle_profiler_report (self, arguments, error);

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

/**
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
