/* lrg-mcp-engine-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for engine control.
 */

#include "lrg-mcp-engine-tools.h"
#include "../../core/lrg-engine.h"
#include "../../lrg-log.h"
#include "../../lrg-version.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-engine-tools
 * @title: LrgMcpEngineTools
 * @short_description: MCP tools for engine control
 *
 * #LrgMcpEngineTools provides MCP tools for querying engine state.
 */

struct _LrgMcpEngineTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEngineTools, lrg_mcp_engine_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Tool Handlers
 * ========================================================================== */

static McpToolResult *
handle_get_info (LrgMcpEngineTools *self,
                 JsonObject        *arguments,
                 GError           **error)
{
	LrgEngine *engine;
	McpToolResult *result;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;
	LrgEngineState state;
	const gchar *state_str;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}

	state = lrg_engine_get_state (engine);
	switch (state)
	{
	case LRG_ENGINE_STATE_UNINITIALIZED:
		state_str = "uninitialized";
		break;
	case LRG_ENGINE_STATE_INITIALIZING:
		state_str = "initializing";
		break;
	case LRG_ENGINE_STATE_RUNNING:
		state_str = "running";
		break;
	case LRG_ENGINE_STATE_PAUSED:
		state_str = "paused";
		break;
	case LRG_ENGINE_STATE_SHUTTING_DOWN:
		state_str = "shutting_down";
		break;
	case LRG_ENGINE_STATE_TERMINATED:
		state_str = "terminated";
		break;
	default:
		state_str = "unknown";
		break;
	}

	/* Build JSON response with engine info */
	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "state");
	json_builder_add_string_value (builder, state_str);

	json_builder_set_member_name (builder, "running");
	json_builder_add_boolean_value (builder, lrg_engine_is_running (engine));

	json_builder_set_member_name (builder, "version");
	json_builder_add_string_value (builder, LRG_VERSION_STRING);

	json_builder_end_object (builder);

	/* Convert to string */
	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	result = mcp_tool_result_new (FALSE);
	mcp_tool_result_add_text (result, json_str);
	return result;
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_engine_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "engine";
}

static void
lrg_mcp_engine_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;

	tool = mcp_tool_new ("lrg_engine_get_info",
	                     "Get engine state information (state, running status, version)");
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_engine_tools_handle_tool (LrgMcpToolGroup  *group,
                                  const gchar      *name,
                                  JsonObject       *arguments,
                                  GError          **error)
{
	LrgMcpEngineTools *self = LRG_MCP_ENGINE_TOOLS (group);

	if (g_strcmp0 (name, "lrg_engine_get_info") == 0)
		return handle_get_info (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_engine_tools_class_init (LrgMcpEngineToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_engine_tools_get_group_name;
	group_class->register_tools = lrg_mcp_engine_tools_register_tools;
	group_class->handle_tool = lrg_mcp_engine_tools_handle_tool;
}

static void
lrg_mcp_engine_tools_init (LrgMcpEngineTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_engine_tools_new:
 *
 * Creates a new engine tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpEngineTools
 */
LrgMcpEngineTools *
lrg_mcp_engine_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_ENGINE_TOOLS, NULL);
}
