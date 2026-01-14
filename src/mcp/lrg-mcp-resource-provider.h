/* lrg-mcp-resource-provider.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that provide MCP resources.
 *
 * Implement this interface to expose read-only data via MCP resources.
 * Resources use a URI scheme (libregnum://) for identification.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <mcp.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_MCP_RESOURCE_PROVIDER (lrg_mcp_resource_provider_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgMcpResourceProvider, lrg_mcp_resource_provider, LRG, MCP_RESOURCE_PROVIDER, GObject)

/**
 * LrgMcpResourceProviderInterface:
 * @parent_iface: parent interface
 * @list_resources: returns the list of resources provided
 * @read_resource: reads a resource by URI
 *
 * Interface structure for #LrgMcpResourceProvider.
 *
 * Implementors must provide both methods. Resources are identified
 * by URIs in the libregnum:// scheme.
 */
struct _LrgMcpResourceProviderInterface
{
	GTypeInterface parent_iface;

	/*< public >*/

	/**
	 * LrgMcpResourceProviderInterface::list_resources:
	 * @self: a #LrgMcpResourceProvider
	 *
	 * Lists all resources provided by this provider.
	 *
	 * Returns: (transfer full) (element-type McpResource): list of #McpResource objects
	 */
	GList * (*list_resources) (LrgMcpResourceProvider *self);

	/**
	 * LrgMcpResourceProviderInterface::read_resource:
	 * @self: a #LrgMcpResourceProvider
	 * @uri: the resource URI
	 * @error: (optional): return location for a #GError
	 *
	 * Reads a resource by URI.
	 *
	 * Returns: (transfer full) (element-type McpResourceContents) (nullable):
	 *          list of resource contents, or %NULL on error
	 */
	GList * (*read_resource) (LrgMcpResourceProvider  *self,
	                          const gchar             *uri,
	                          GError                 **error);

	/**
	 * LrgMcpResourceProviderInterface::handles_uri:
	 * @self: a #LrgMcpResourceProvider
	 * @uri: the resource URI to check
	 *
	 * Checks if this provider handles the given URI.
	 *
	 * Returns: %TRUE if this provider can handle the URI
	 */
	gboolean (*handles_uri) (LrgMcpResourceProvider *self,
	                         const gchar            *uri);
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_mcp_resource_provider_list_resources:
 * @self: a #LrgMcpResourceProvider
 *
 * Lists all resources provided by this provider.
 *
 * Returns: (transfer full) (element-type McpResource): list of #McpResource objects
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_mcp_resource_provider_list_resources (LrgMcpResourceProvider *self);

/**
 * lrg_mcp_resource_provider_read_resource:
 * @self: a #LrgMcpResourceProvider
 * @uri: the resource URI
 * @error: (optional): return location for a #GError
 *
 * Reads a resource by URI.
 *
 * Returns: (transfer full) (element-type McpResourceContents) (nullable):
 *          list of resource contents, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_mcp_resource_provider_read_resource (LrgMcpResourceProvider  *self,
                                                  const gchar             *uri,
                                                  GError                 **error);

/**
 * lrg_mcp_resource_provider_handles_uri:
 * @self: a #LrgMcpResourceProvider
 * @uri: the resource URI to check
 *
 * Checks if this provider handles the given URI.
 *
 * Returns: %TRUE if this provider can handle the URI
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mcp_resource_provider_handles_uri (LrgMcpResourceProvider *self,
                                                 const gchar            *uri);

G_END_DECLS
