/* lrg-mcp-tool-group.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for MCP tool groups.
 *
 * Tool groups provide a set of related MCP tools. Subclass this
 * to create specialized tool providers (input, screenshot, etc.).
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
#include "lrg-mcp-tool-provider.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_TOOL_GROUP (lrg_mcp_tool_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgMcpToolGroup, lrg_mcp_tool_group, LRG, MCP_TOOL_GROUP, GObject)

/**
 * LrgMcpToolGroupClass:
 * @parent_class: The parent class
 * @get_group_name: Returns the group name for logging/debugging
 * @register_tools: Called to register tools with the internal tool list
 * @handle_tool: Called to handle a specific tool invocation
 *
 * The class structure for #LrgMcpToolGroup.
 *
 * Subclasses should override @register_tools to add their tools
 * and @handle_tool to process tool invocations.
 */
struct _LrgMcpToolGroupClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgMcpToolGroupClass::get_group_name:
	 * @self: the tool group
	 *
	 * Gets the name of this tool group for logging/debugging.
	 *
	 * Returns: (transfer none): the group name
	 */
	const gchar * (*get_group_name) (LrgMcpToolGroup *self);

	/**
	 * LrgMcpToolGroupClass::register_tools:
	 * @self: the tool group
	 *
	 * Called during construction to register tools.
	 * Subclasses should call lrg_mcp_tool_group_add_tool() for each tool.
	 */
	void (*register_tools) (LrgMcpToolGroup *self);

	/**
	 * LrgMcpToolGroupClass::handle_tool:
	 * @self: the tool group
	 * @name: the tool name
	 * @arguments: (nullable): JSON object with tool arguments
	 * @error: (optional): return location for a #GError
	 *
	 * Handles a tool invocation. Subclasses must override this
	 * to implement their tool logic.
	 *
	 * Returns: (transfer full) (nullable): the tool result, or %NULL on error
	 */
	McpToolResult * (*handle_tool) (LrgMcpToolGroup  *self,
	                                const gchar      *name,
	                                JsonObject       *arguments,
	                                GError          **error);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Tool Registration
 * ========================================================================== */

/**
 * lrg_mcp_tool_group_add_tool:
 * @self: an #LrgMcpToolGroup
 * @tool: (transfer full): the tool to add
 *
 * Adds a tool to this group. The group takes ownership of the tool.
 * Call this from the register_tools virtual method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_tool_group_add_tool (LrgMcpToolGroup *self,
                                   McpTool         *tool);

/**
 * lrg_mcp_tool_group_get_group_name:
 * @self: an #LrgMcpToolGroup
 *
 * Gets the name of this tool group.
 *
 * Returns: (transfer none): the group name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_tool_group_get_group_name (LrgMcpToolGroup *self);

/* ==========================================================================
 * JSON Argument Helpers
 * ========================================================================== */

/**
 * lrg_mcp_tool_group_get_string_arg:
 * @arguments: (nullable): JSON arguments object
 * @name: argument name
 * @default_value: (nullable): default if not found
 *
 * Gets a string argument from the JSON arguments object.
 *
 * Returns: (transfer none) (nullable): the argument value or default
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_tool_group_get_string_arg (JsonObject  *arguments,
                                                  const gchar *name,
                                                  const gchar *default_value);

/**
 * lrg_mcp_tool_group_get_int_arg:
 * @arguments: (nullable): JSON arguments object
 * @name: argument name
 * @default_value: default if not found
 *
 * Gets an integer argument from the JSON arguments object.
 *
 * Returns: the argument value or default
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_mcp_tool_group_get_int_arg (JsonObject  *arguments,
                                        const gchar *name,
                                        gint64       default_value);

/**
 * lrg_mcp_tool_group_get_double_arg:
 * @arguments: (nullable): JSON arguments object
 * @name: argument name
 * @default_value: default if not found
 *
 * Gets a double argument from the JSON arguments object.
 *
 * Returns: the argument value or default
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_mcp_tool_group_get_double_arg (JsonObject  *arguments,
                                            const gchar *name,
                                            gdouble      default_value);

/**
 * lrg_mcp_tool_group_get_bool_arg:
 * @arguments: (nullable): JSON arguments object
 * @name: argument name
 * @default_value: default if not found
 *
 * Gets a boolean argument from the JSON arguments object.
 *
 * Returns: the argument value or default
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mcp_tool_group_get_bool_arg (JsonObject  *arguments,
                                           const gchar *name,
                                           gboolean     default_value);

G_END_DECLS
