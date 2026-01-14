/* lrg-mcp-ecs-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for ECS/World manipulation.
 *
 * NOTE: This is a stub implementation. Full ECS introspection requires
 * additional API to be added to LrgEngine, LrgWorld, and LrgGameObject.
 */

#include "lrg-mcp-ecs-tools.h"
#include "../../core/lrg-engine.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-ecs-tools
 * @title: LrgMcpEcsTools
 * @short_description: MCP tools for ECS manipulation
 *
 * #LrgMcpEcsTools provides MCP tools for querying and manipulating
 * the Entity-Component-System, including worlds, game objects,
 * components, and transforms.
 *
 * Note: This is currently a stub implementation that returns placeholder
 * data. Full implementation requires additional introspection API in the
 * ECS module.
 */

struct _LrgMcpEcsTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEcsTools, lrg_mcp_ecs_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Tool Handlers (Stub implementations)
 * ========================================================================== */

static McpToolResult *
handle_list_worlds (LrgMcpEcsTools *self,
                    JsonObject     *arguments,
                    GError        **error)
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

	/* Stub: Return empty world list
	 * TODO: Implement when lrg_engine_get_worlds() is available
	 */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "worlds");
	json_builder_begin_array (builder);
	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder, "ECS introspection API not yet implemented");
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
handle_list_game_objects (LrgMcpEcsTools *self,
                          JsonObject     *arguments,
                          GError        **error)
{
	McpToolResult *result;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;

	/* Stub: Return empty object list */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "objects");
	json_builder_begin_array (builder);
	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder, "ECS introspection API not yet implemented");
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
handle_get_game_object (LrgMcpEcsTools *self,
                        JsonObject     *arguments,
                        GError        **error)
{
	const gchar *id;

	id = lrg_mcp_tool_group_get_string_arg (arguments, "id", NULL);
	if (id == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: id");
		return NULL;
	}

	/* Stub: Return not found - API not implemented */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "ECS introspection API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_get_transform (LrgMcpEcsTools *self,
                      JsonObject     *arguments,
                      GError        **error)
{
	const gchar *object_id;

	object_id = lrg_mcp_tool_group_get_string_arg (arguments, "object_id", NULL);
	if (object_id == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: object_id");
		return NULL;
	}

	/* Stub: Return not supported - API not implemented */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "ECS introspection API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_set_transform (LrgMcpEcsTools *self,
                      JsonObject     *arguments,
                      GError        **error)
{
	const gchar *object_id;

	object_id = lrg_mcp_tool_group_get_string_arg (arguments, "object_id", NULL);
	if (object_id == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: object_id");
		return NULL;
	}

	/* Stub: Return not supported - API not implemented */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "ECS introspection API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_destroy_object (LrgMcpEcsTools *self,
                       JsonObject     *arguments,
                       GError        **error)
{
	const gchar *id;

	id = lrg_mcp_tool_group_get_string_arg (arguments, "id", NULL);
	if (id == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: id");
		return NULL;
	}

	/* Stub: Return not supported - API not implemented */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "ECS introspection API not yet implemented");
	return NULL;
}

/* ==========================================================================
 * Schema Builders
 * ========================================================================== */

static JsonNode *
build_schema_string_required (const gchar *name,
                              const gchar *description)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, name);
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, description);
	json_builder_end_object (builder);
	json_builder_end_object (builder);
	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, name);
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_list_game_objects (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "world");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "World name (optional, uses active world)");
	json_builder_end_object (builder);

	json_builder_end_object (builder);
	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_set_transform (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	/* object_id */
	json_builder_set_member_name (builder, "object_id");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "GameObject ID");
	json_builder_end_object (builder);

	/* x */
	json_builder_set_member_name (builder, "x");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "X position");
	json_builder_end_object (builder);

	/* y */
	json_builder_set_member_name (builder, "y");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Y position");
	json_builder_end_object (builder);

	/* rotation */
	json_builder_set_member_name (builder, "rotation");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Rotation in degrees");
	json_builder_end_object (builder);

	/* scale_x */
	json_builder_set_member_name (builder, "scale_x");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "X scale factor");
	json_builder_end_object (builder);

	/* scale_y */
	json_builder_set_member_name (builder, "scale_y");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "number");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Y scale factor");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "object_id");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_ecs_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "ecs";
}

static void
lrg_mcp_ecs_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;
	JsonNode *schema;

	/* World tools */
	tool = mcp_tool_new ("lrg_ecs_list_worlds",
	                     "List all active game worlds");
	lrg_mcp_tool_group_add_tool (group, tool);

	/* GameObject tools */
	tool = mcp_tool_new ("lrg_ecs_list_game_objects",
	                     "List GameObjects in a world");
	schema = build_schema_list_game_objects ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_ecs_get_game_object",
	                     "Get detailed information about a GameObject");
	schema = build_schema_string_required ("id", "GameObject ID");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_ecs_destroy_object",
	                     "Destroy a GameObject");
	schema = build_schema_string_required ("id", "GameObject ID to destroy");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	/* Transform tools */
	tool = mcp_tool_new ("lrg_ecs_get_transform",
	                     "Get transform data for a GameObject");
	schema = build_schema_string_required ("object_id", "GameObject ID");
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_ecs_set_transform",
	                     "Set transform values for a GameObject");
	schema = build_schema_set_transform ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_ecs_tools_handle_tool (LrgMcpToolGroup  *group,
                               const gchar      *name,
                               JsonObject       *arguments,
                               GError          **error)
{
	LrgMcpEcsTools *self = LRG_MCP_ECS_TOOLS (group);

	if (g_strcmp0 (name, "lrg_ecs_list_worlds") == 0)
		return handle_list_worlds (self, arguments, error);
	if (g_strcmp0 (name, "lrg_ecs_list_game_objects") == 0)
		return handle_list_game_objects (self, arguments, error);
	if (g_strcmp0 (name, "lrg_ecs_get_game_object") == 0)
		return handle_get_game_object (self, arguments, error);
	if (g_strcmp0 (name, "lrg_ecs_get_transform") == 0)
		return handle_get_transform (self, arguments, error);
	if (g_strcmp0 (name, "lrg_ecs_set_transform") == 0)
		return handle_set_transform (self, arguments, error);
	if (g_strcmp0 (name, "lrg_ecs_destroy_object") == 0)
		return handle_destroy_object (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_ecs_tools_class_init (LrgMcpEcsToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_ecs_tools_get_group_name;
	group_class->register_tools = lrg_mcp_ecs_tools_register_tools;
	group_class->handle_tool = lrg_mcp_ecs_tools_handle_tool;
}

static void
lrg_mcp_ecs_tools_init (LrgMcpEcsTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_ecs_tools_new:
 *
 * Creates a new ECS tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpEcsTools
 */
LrgMcpEcsTools *
lrg_mcp_ecs_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_ECS_TOOLS, NULL);
}
