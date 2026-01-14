/* lrg-mcp-resource-group.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for MCP resource groups.
 *
 * Resource groups provide a set of related MCP resources. Subclass this
 * to create specialized resource providers (engine, ECS, screenshot).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <mcp.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-mcp-resource-provider.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_RESOURCE_GROUP (lrg_mcp_resource_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgMcpResourceGroup, lrg_mcp_resource_group, LRG, MCP_RESOURCE_GROUP, GObject)

/**
 * LrgMcpResourceGroupClass:
 * @parent_class: The parent class
 * @get_group_name: Returns the group name for logging/debugging
 * @register_resources: Called to register resources with the internal list
 * @read_resource: Called to read a specific resource
 *
 * The class structure for #LrgMcpResourceGroup.
 *
 * Subclasses should override @register_resources to add their resources
 * and @read_resource to provide resource data.
 */
struct _LrgMcpResourceGroupClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgMcpResourceGroupClass::get_group_name:
	 * @self: the resource group
	 *
	 * Gets the name of this resource group for logging/debugging.
	 *
	 * Returns: (transfer none): the group name
	 */
	const gchar * (*get_group_name) (LrgMcpResourceGroup *self);

	/**
	 * LrgMcpResourceGroupClass::register_resources:
	 * @self: the resource group
	 *
	 * Called during construction to register resources.
	 * Subclasses should call lrg_mcp_resource_group_add_resource().
	 */
	void (*register_resources) (LrgMcpResourceGroup *self);

	/**
	 * LrgMcpResourceGroupClass::read_resource:
	 * @self: the resource group
	 * @uri: the resource URI
	 * @error: (optional): return location for a #GError
	 *
	 * Reads a resource by URI. Subclasses must override this
	 * to provide resource data.
	 *
	 * Returns: (transfer full) (element-type McpResourceContents) (nullable):
	 *          list of resource contents, or %NULL on error
	 */
	GList * (*read_resource) (LrgMcpResourceGroup  *self,
	                          const gchar          *uri,
	                          GError              **error);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Resource Registration
 * ========================================================================== */

/**
 * lrg_mcp_resource_group_add_resource:
 * @self: an #LrgMcpResourceGroup
 * @resource: (transfer full): the resource to add
 *
 * Adds a resource to this group. The group takes ownership of the resource.
 * Call this from the register_resources virtual method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_resource_group_add_resource (LrgMcpResourceGroup *self,
                                           McpResource         *resource);

/**
 * lrg_mcp_resource_group_get_group_name:
 * @self: an #LrgMcpResourceGroup
 *
 * Gets the name of this resource group.
 *
 * Returns: (transfer none): the group name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_resource_group_get_group_name (LrgMcpResourceGroup *self);

/**
 * lrg_mcp_resource_group_get_uri_prefix:
 * @self: an #LrgMcpResourceGroup
 *
 * Gets the URI prefix for this resource group (e.g., "libregnum://engine/").
 * Used for handles_uri matching.
 *
 * Returns: (transfer none): the URI prefix
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_mcp_resource_group_get_uri_prefix (LrgMcpResourceGroup *self);

/**
 * lrg_mcp_resource_group_set_uri_prefix:
 * @self: an #LrgMcpResourceGroup
 * @prefix: the URI prefix
 *
 * Sets the URI prefix for this resource group.
 */
LRG_AVAILABLE_IN_ALL
void lrg_mcp_resource_group_set_uri_prefix (LrgMcpResourceGroup *self,
                                             const gchar         *prefix);

G_END_DECLS
