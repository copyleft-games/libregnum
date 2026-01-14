/* lrg-mcp-save-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP tool group for save/load operations.
 *
 * NOTE: This is a stub implementation until the SaveManager API
 * is fully available.
 */

#include "lrg-mcp-save-tools.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-save-tools
 * @title: LrgMcpSaveTools
 * @short_description: MCP tools for save/load operations
 *
 * #LrgMcpSaveTools provides MCP tools for managing game saves,
 * including listing slots, saving, loading, and deleting.
 *
 * Note: This is currently a stub implementation that returns placeholder
 * data. Full implementation requires the SaveManager module.
 */

struct _LrgMcpSaveTools
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpSaveTools, lrg_mcp_save_tools, LRG_TYPE_MCP_TOOL_GROUP)

/* ==========================================================================
 * Tool Handlers (Stub implementations)
 * ========================================================================== */

static McpToolResult *
handle_list_slots (LrgMcpSaveTools *self,
                   JsonObject      *arguments,
                   GError         **error)
{
	McpToolResult *result;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;

	/* Stub: Return empty slot list */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "slots");
	json_builder_begin_array (builder);
	json_builder_end_array (builder);
	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder, "SaveManager API not yet implemented");
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
handle_get_info (LrgMcpSaveTools *self,
                 JsonObject      *arguments,
                 GError         **error)
{
	const gchar *slot;

	slot = lrg_mcp_tool_group_get_string_arg (arguments, "slot", NULL);
	if (slot == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: slot");
		return NULL;
	}

	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_create (LrgMcpSaveTools *self,
               JsonObject      *arguments,
               GError         **error)
{
	const gchar *slot;

	slot = lrg_mcp_tool_group_get_string_arg (arguments, "slot", NULL);
	if (slot == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: slot");
		return NULL;
	}

	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_load (LrgMcpSaveTools *self,
             JsonObject      *arguments,
             GError         **error)
{
	const gchar *slot;

	slot = lrg_mcp_tool_group_get_string_arg (arguments, "slot", NULL);
	if (slot == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: slot");
		return NULL;
	}

	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_delete (LrgMcpSaveTools *self,
               JsonObject      *arguments,
               GError         **error)
{
	const gchar *slot;

	slot = lrg_mcp_tool_group_get_string_arg (arguments, "slot", NULL);
	if (slot == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "Missing required argument: slot");
		return NULL;
	}

	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_quick_save (LrgMcpSaveTools *self,
                   JsonObject      *arguments,
                   GError         **error)
{
	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

static McpToolResult *
handle_quick_load (LrgMcpSaveTools *self,
                   JsonObject      *arguments,
                   GError         **error)
{
	/* Stub: Return not supported */
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "SaveManager API not yet implemented");
	return NULL;
}

/* ==========================================================================
 * Schema Builders
 * ========================================================================== */

static JsonNode *
build_schema_slot_required (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "slot");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Save slot name");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "slot");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

static JsonNode *
build_schema_create (void)
{
	g_autoptr(JsonBuilder) builder = json_builder_new ();

	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "object");
	json_builder_set_member_name (builder, "properties");
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "slot");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Save slot name");
	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "description");
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "type");
	json_builder_add_string_value (builder, "string");
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, "Save description");
	json_builder_end_object (builder);

	json_builder_end_object (builder);

	json_builder_set_member_name (builder, "required");
	json_builder_begin_array (builder);
	json_builder_add_string_value (builder, "slot");
	json_builder_end_array (builder);

	json_builder_end_object (builder);

	return json_builder_get_root (builder);
}

/* ==========================================================================
 * LrgMcpToolGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_save_tools_get_group_name (LrgMcpToolGroup *group)
{
	return "save";
}

static void
lrg_mcp_save_tools_register_tools (LrgMcpToolGroup *group)
{
	McpTool *tool;
	JsonNode *schema;

	tool = mcp_tool_new ("lrg_save_list_slots",
	                     "List all available save slots");
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_get_info",
	                     "Get metadata for a save slot");
	schema = build_schema_slot_required ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_create",
	                     "Create a save in the specified slot");
	schema = build_schema_create ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_load",
	                     "Load game from the specified slot");
	schema = build_schema_slot_required ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_delete",
	                     "Delete the save in the specified slot");
	schema = build_schema_slot_required ();
	mcp_tool_set_input_schema (tool, schema);
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_quick_save",
	                     "Trigger a quick save");
	lrg_mcp_tool_group_add_tool (group, tool);

	tool = mcp_tool_new ("lrg_save_quick_load",
	                     "Trigger a quick load");
	lrg_mcp_tool_group_add_tool (group, tool);
}

static McpToolResult *
lrg_mcp_save_tools_handle_tool (LrgMcpToolGroup  *group,
                                const gchar      *name,
                                JsonObject       *arguments,
                                GError          **error)
{
	LrgMcpSaveTools *self = LRG_MCP_SAVE_TOOLS (group);

	if (g_strcmp0 (name, "lrg_save_list_slots") == 0)
		return handle_list_slots (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_get_info") == 0)
		return handle_get_info (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_create") == 0)
		return handle_create (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_load") == 0)
		return handle_load (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_delete") == 0)
		return handle_delete (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_quick_save") == 0)
		return handle_quick_save (self, arguments, error);
	if (g_strcmp0 (name, "lrg_save_quick_load") == 0)
		return handle_quick_load (self, arguments, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "Unknown tool: %s", name);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_save_tools_class_init (LrgMcpSaveToolsClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_save_tools_get_group_name;
	group_class->register_tools = lrg_mcp_save_tools_register_tools;
	group_class->handle_tool = lrg_mcp_save_tools_handle_tool;
}

static void
lrg_mcp_save_tools_init (LrgMcpSaveTools *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_save_tools_new:
 *
 * Creates a new save tools provider.
 *
 * Returns: (transfer full): A new #LrgMcpSaveTools
 */
LrgMcpSaveTools *
lrg_mcp_save_tools_new (void)
{
	return g_object_new (LRG_TYPE_MCP_SAVE_TOOLS, NULL);
}
