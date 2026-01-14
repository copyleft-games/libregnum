/* lrg-mcp-tool-group.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for MCP tool groups.
 */

#include "lrg-mcp-tool-group.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-tool-group
 * @title: LrgMcpToolGroup
 * @short_description: Abstract base class for MCP tool groups
 *
 * #LrgMcpToolGroup is an abstract base class that implements the
 * #LrgMcpToolProvider interface. Subclasses provide related sets
 * of tools (e.g., input tools, screenshot tools).
 *
 * ## Subclassing
 *
 * Override the following virtual methods:
 * - `get_group_name`: Return a name for debugging
 * - `register_tools`: Add tools via lrg_mcp_tool_group_add_tool()
 * - `handle_tool`: Process tool invocations
 *
 * |[<!-- language="C" -->
 * static const gchar *
 * my_tool_group_get_group_name (LrgMcpToolGroup *self)
 * {
 *     return "my-tools";
 * }
 *
 * static void
 * my_tool_group_register_tools (LrgMcpToolGroup *self)
 * {
 *     McpTool *tool = mcp_tool_new ("my_tool", "Does something");
 *     lrg_mcp_tool_group_add_tool (self, tool);
 * }
 *
 * static McpToolResult *
 * my_tool_group_handle_tool (LrgMcpToolGroup  *self,
 *                            const gchar      *name,
 *                            JsonObject       *arguments,
 *                            GError          **error)
 * {
 *     if (g_strcmp0 (name, "my_tool") == 0)
 *     {
 *         McpToolResult *result = mcp_tool_result_new (FALSE);
 *         mcp_tool_result_add_text (result, "Success!");
 *         return result;
 *     }
 *     return NULL;
 * }
 * ]|
 */

typedef struct
{
	GPtrArray *tools;  /* Owned McpTool objects */
} LrgMcpToolGroupPrivate;

static void lrg_mcp_tool_provider_iface_init (LrgMcpToolProviderInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (LrgMcpToolGroup, lrg_mcp_tool_group, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (LrgMcpToolGroup)
                                  G_IMPLEMENT_INTERFACE (LRG_TYPE_MCP_TOOL_PROVIDER,
                                                         lrg_mcp_tool_provider_iface_init))

/* ==========================================================================
 * LrgMcpToolProvider Interface Implementation
 * ========================================================================== */

static GList *
lrg_mcp_tool_group_list_tools_impl (LrgMcpToolProvider *provider)
{
	LrgMcpToolGroup *self = LRG_MCP_TOOL_GROUP (provider);
	LrgMcpToolGroupPrivate *priv = lrg_mcp_tool_group_get_instance_private (self);
	GList *list = NULL;
	guint i;

	for (i = 0; i < priv->tools->len; i++)
	{
		McpTool *tool = g_ptr_array_index (priv->tools, i);
		list = g_list_append (list, g_object_ref (tool));
	}

	return list;
}

static McpToolResult *
lrg_mcp_tool_group_call_tool_impl (LrgMcpToolProvider  *provider,
                                    const gchar         *name,
                                    JsonObject          *arguments,
                                    GError             **error)
{
	LrgMcpToolGroup *self = LRG_MCP_TOOL_GROUP (provider);
	LrgMcpToolGroupClass *klass = LRG_MCP_TOOL_GROUP_GET_CLASS (self);

	if (klass->handle_tool == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Tool group does not implement handle_tool");
		return NULL;
	}

	return klass->handle_tool (self, name, arguments, error);
}

static void
lrg_mcp_tool_provider_iface_init (LrgMcpToolProviderInterface *iface)
{
	iface->list_tools = lrg_mcp_tool_group_list_tools_impl;
	iface->call_tool = lrg_mcp_tool_group_call_tool_impl;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_tool_group_constructed (GObject *object)
{
	LrgMcpToolGroup *self = LRG_MCP_TOOL_GROUP (object);
	LrgMcpToolGroupClass *klass = LRG_MCP_TOOL_GROUP_GET_CLASS (self);

	G_OBJECT_CLASS (lrg_mcp_tool_group_parent_class)->constructed (object);

	/* Call subclass to register its tools */
	if (klass->register_tools != NULL)
	{
		klass->register_tools (self);
	}
}

static void
lrg_mcp_tool_group_finalize (GObject *object)
{
	LrgMcpToolGroup *self = LRG_MCP_TOOL_GROUP (object);
	LrgMcpToolGroupPrivate *priv = lrg_mcp_tool_group_get_instance_private (self);

	g_clear_pointer (&priv->tools, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_mcp_tool_group_parent_class)->finalize (object);
}

static void
lrg_mcp_tool_group_class_init (LrgMcpToolGroupClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = lrg_mcp_tool_group_constructed;
	object_class->finalize = lrg_mcp_tool_group_finalize;

	/* Default implementations */
	klass->get_group_name = NULL;
	klass->register_tools = NULL;
	klass->handle_tool = NULL;
}

static void
lrg_mcp_tool_group_init (LrgMcpToolGroup *self)
{
	LrgMcpToolGroupPrivate *priv = lrg_mcp_tool_group_get_instance_private (self);

	priv->tools = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_tool_group_add_tool:
 * @self: an #LrgMcpToolGroup
 * @tool: (transfer full): the tool to add
 *
 * Adds a tool to this group. The group takes ownership of the tool.
 */
void
lrg_mcp_tool_group_add_tool (LrgMcpToolGroup *self,
                              McpTool         *tool)
{
	LrgMcpToolGroupPrivate *priv;

	g_return_if_fail (LRG_IS_MCP_TOOL_GROUP (self));
	g_return_if_fail (MCP_IS_TOOL (tool));

	priv = lrg_mcp_tool_group_get_instance_private (self);
	g_ptr_array_add (priv->tools, tool);  /* Takes ownership */
}

/**
 * lrg_mcp_tool_group_get_group_name:
 * @self: an #LrgMcpToolGroup
 *
 * Gets the name of this tool group.
 *
 * Returns: (transfer none): the group name
 */
const gchar *
lrg_mcp_tool_group_get_group_name (LrgMcpToolGroup *self)
{
	LrgMcpToolGroupClass *klass;

	g_return_val_if_fail (LRG_IS_MCP_TOOL_GROUP (self), NULL);

	klass = LRG_MCP_TOOL_GROUP_GET_CLASS (self);
	if (klass->get_group_name != NULL)
	{
		return klass->get_group_name (self);
	}

	return "unknown";
}

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
const gchar *
lrg_mcp_tool_group_get_string_arg (JsonObject  *arguments,
                                    const gchar *name,
                                    const gchar *default_value)
{
	if (arguments == NULL || !json_object_has_member (arguments, name))
	{
		return default_value;
	}

	return json_object_get_string_member (arguments, name);
}

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
gint64
lrg_mcp_tool_group_get_int_arg (JsonObject  *arguments,
                                 const gchar *name,
                                 gint64       default_value)
{
	if (arguments == NULL || !json_object_has_member (arguments, name))
	{
		return default_value;
	}

	return json_object_get_int_member (arguments, name);
}

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
gdouble
lrg_mcp_tool_group_get_double_arg (JsonObject  *arguments,
                                    const gchar *name,
                                    gdouble      default_value)
{
	if (arguments == NULL || !json_object_has_member (arguments, name))
	{
		return default_value;
	}

	return json_object_get_double_member (arguments, name);
}

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
gboolean
lrg_mcp_tool_group_get_bool_arg (JsonObject  *arguments,
                                  const gchar *name,
                                  gboolean     default_value)
{
	if (arguments == NULL || !json_object_has_member (arguments, name))
	{
		return default_value;
	}

	return json_object_get_boolean_member (arguments, name);
}
