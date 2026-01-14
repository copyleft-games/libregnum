/* lrg-mcp-tool-provider.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that provide MCP tools.
 */

#include "lrg-mcp-tool-provider.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-tool-provider
 * @title: LrgMcpToolProvider
 * @short_description: Interface for MCP tool providers
 *
 * The #LrgMcpToolProvider interface defines how objects can expose
 * MCP tools to the MCP server. Any module that wants to provide
 * tools for AI interaction should implement this interface.
 *
 * ## Implementing the Interface
 *
 * To implement the interface, provide implementations for list_tools()
 * and call_tool(). The list_tools() method should return a list of
 * McpTool objects describing the available tools. The call_tool()
 * method handles the actual tool invocation.
 *
 * |[<!-- language="C" -->
 * static GList *
 * my_provider_list_tools (LrgMcpToolProvider *self)
 * {
 *     GList *tools = NULL;
 *     McpTool *tool;
 *
 *     tool = mcp_tool_new ("my_tool", "Does something useful");
 *     tools = g_list_append (tools, tool);
 *
 *     return tools;
 * }
 *
 * static McpToolResult *
 * my_provider_call_tool (LrgMcpToolProvider  *self,
 *                        const gchar         *name,
 *                        JsonObject          *arguments,
 *                        GError             **error)
 * {
 *     if (g_strcmp0 (name, "my_tool") == 0)
 *     {
 *         McpToolResult *result = mcp_tool_result_new (FALSE);
 *         mcp_tool_result_add_text (result, "Tool executed successfully");
 *         return result;
 *     }
 *
 *     g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
 *                  "Unknown tool: %s", name);
 *     return NULL;
 * }
 * ]|
 */

G_DEFINE_INTERFACE (LrgMcpToolProvider, lrg_mcp_tool_provider, G_TYPE_OBJECT)

static void
lrg_mcp_tool_provider_default_init (LrgMcpToolProviderInterface *iface)
{
	/* Default implementation does nothing */
}

/**
 * lrg_mcp_tool_provider_list_tools:
 * @self: a #LrgMcpToolProvider
 *
 * Lists all tools provided by this provider.
 *
 * Returns: (transfer full) (element-type McpTool): list of #McpTool objects
 */
GList *
lrg_mcp_tool_provider_list_tools (LrgMcpToolProvider *self)
{
	LrgMcpToolProviderInterface *iface;

	g_return_val_if_fail (LRG_IS_MCP_TOOL_PROVIDER (self), NULL);

	iface = LRG_MCP_TOOL_PROVIDER_GET_IFACE (self);
	g_return_val_if_fail (iface->list_tools != NULL, NULL);

	return iface->list_tools (self);
}

/**
 * lrg_mcp_tool_provider_call_tool:
 * @self: a #LrgMcpToolProvider
 * @name: the tool name
 * @arguments: (nullable): JSON object with tool arguments
 * @error: (optional): return location for a #GError
 *
 * Calls a tool by name with the given arguments.
 *
 * Returns: (transfer full) (nullable): the tool result, or %NULL on error
 */
McpToolResult *
lrg_mcp_tool_provider_call_tool (LrgMcpToolProvider  *self,
                                  const gchar         *name,
                                  JsonObject          *arguments,
                                  GError             **error)
{
	LrgMcpToolProviderInterface *iface;

	g_return_val_if_fail (LRG_IS_MCP_TOOL_PROVIDER (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	iface = LRG_MCP_TOOL_PROVIDER_GET_IFACE (self);
	g_return_val_if_fail (iface->call_tool != NULL, NULL);

	return iface->call_tool (self, name, arguments, error);
}

/**
 * lrg_mcp_tool_provider_has_tool:
 * @self: a #LrgMcpToolProvider
 * @name: the tool name to check
 *
 * Checks if this provider has a tool with the given name.
 *
 * Returns: %TRUE if the tool exists
 */
gboolean
lrg_mcp_tool_provider_has_tool (LrgMcpToolProvider *self,
                                 const gchar        *name)
{
	GList *tools;
	GList *iter;
	gboolean found;

	g_return_val_if_fail (LRG_IS_MCP_TOOL_PROVIDER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	tools = lrg_mcp_tool_provider_list_tools (self);
	found = FALSE;

	for (iter = tools; iter != NULL; iter = iter->next)
	{
		McpTool *tool = MCP_TOOL (iter->data);
		if (g_strcmp0 (mcp_tool_get_name (tool), name) == 0)
		{
			found = TRUE;
			break;
		}
	}

	g_list_free_full (tools, g_object_unref);
	return found;
}
