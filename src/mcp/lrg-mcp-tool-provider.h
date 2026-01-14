/* lrg-mcp-tool-provider.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that provide MCP tools.
 *
 * Implement this interface to expose MCP tools from any module.
 * The MCP server will query all registered providers for their tools.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <mcp.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_TOOL_PROVIDER (lrg_mcp_tool_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgMcpToolProvider, lrg_mcp_tool_provider, LRG, MCP_TOOL_PROVIDER, GObject)

/**
 * LrgMcpToolProviderInterface:
 * @parent_iface: parent interface
 * @list_tools: returns the list of tools provided
 * @call_tool: handles a tool invocation
 *
 * Interface structure for #LrgMcpToolProvider.
 *
 * Implementors must provide both methods. The list_tools method
 * returns ownership of the McpTool objects to the caller.
 */
struct _LrgMcpToolProviderInterface
{
	GTypeInterface parent_iface;

	/*< public >*/

	/**
	 * LrgMcpToolProviderInterface::list_tools:
	 * @self: a #LrgMcpToolProvider
	 *
	 * Lists all tools provided by this provider.
	 *
	 * Returns: (transfer full) (element-type McpTool): list of #McpTool objects
	 */
	GList * (*list_tools) (LrgMcpToolProvider *self);

	/**
	 * LrgMcpToolProviderInterface::call_tool:
	 * @self: a #LrgMcpToolProvider
	 * @name: the tool name
	 * @arguments: (nullable): JSON object with tool arguments
	 * @error: (optional): return location for a #GError
	 *
	 * Calls a tool by name with the given arguments.
	 *
	 * Returns: (transfer full) (nullable): the tool result, or %NULL on error
	 */
	McpToolResult * (*call_tool) (LrgMcpToolProvider  *self,
	                              const gchar         *name,
	                              JsonObject          *arguments,
	                              GError             **error);
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_mcp_tool_provider_list_tools:
 * @self: a #LrgMcpToolProvider
 *
 * Lists all tools provided by this provider.
 *
 * Returns: (transfer full) (element-type McpTool): list of #McpTool objects
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_mcp_tool_provider_list_tools (LrgMcpToolProvider *self);

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
LRG_AVAILABLE_IN_ALL
McpToolResult * lrg_mcp_tool_provider_call_tool (LrgMcpToolProvider  *self,
                                                  const gchar         *name,
                                                  JsonObject          *arguments,
                                                  GError             **error);

/**
 * lrg_mcp_tool_provider_has_tool:
 * @self: a #LrgMcpToolProvider
 * @name: the tool name to check
 *
 * Checks if this provider has a tool with the given name.
 *
 * Returns: %TRUE if the tool exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mcp_tool_provider_has_tool (LrgMcpToolProvider *self,
                                          const gchar        *name);

G_END_DECLS
