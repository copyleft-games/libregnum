/* lrg-mcp-resource-provider.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for objects that provide MCP resources.
 */

#include "lrg-mcp-resource-provider.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-resource-provider
 * @title: LrgMcpResourceProvider
 * @short_description: Interface for MCP resource providers
 *
 * The #LrgMcpResourceProvider interface defines how objects can expose
 * read-only data via MCP resources. Resources are identified by URIs
 * using the libregnum:// scheme.
 *
 * ## URI Scheme
 *
 * Resources use URIs in the format:
 * - `libregnum://engine/info` - Engine state
 * - `libregnum://ecs/worlds` - ECS world list
 * - `libregnum://screenshot/current` - Current screenshot
 *
 * ## Implementing the Interface
 *
 * |[<!-- language="C" -->
 * static GList *
 * my_provider_list_resources (LrgMcpResourceProvider *self)
 * {
 *     GList *resources = NULL;
 *     McpResource *resource;
 *
 *     resource = mcp_resource_new ("libregnum://my/resource", "My Resource");
 *     mcp_resource_set_mime_type (resource, "application/json");
 *     resources = g_list_append (resources, resource);
 *
 *     return resources;
 * }
 *
 * static GList *
 * my_provider_read_resource (LrgMcpResourceProvider  *self,
 *                            const gchar             *uri,
 *                            GError                 **error)
 * {
 *     GList *contents = NULL;
 *     McpResourceContents *content;
 *
 *     content = mcp_resource_contents_new_text (uri, "{\"status\":\"ok\"}",
 *                                               "application/json");
 *     contents = g_list_append (contents, content);
 *
 *     return contents;
 * }
 * ]|
 */

G_DEFINE_INTERFACE (LrgMcpResourceProvider, lrg_mcp_resource_provider, G_TYPE_OBJECT)

static gboolean
lrg_mcp_resource_provider_default_handles_uri (LrgMcpResourceProvider *self,
                                                const gchar            *uri)
{
	/* Default: check if URI is in our resource list */
	GList *resources;
	GList *iter;
	gboolean handles;

	resources = lrg_mcp_resource_provider_list_resources (self);
	handles = FALSE;

	for (iter = resources; iter != NULL; iter = iter->next)
	{
		McpResource *resource = MCP_RESOURCE (iter->data);
		if (g_strcmp0 (mcp_resource_get_uri (resource), uri) == 0)
		{
			handles = TRUE;
			break;
		}
	}

	g_list_free_full (resources, g_object_unref);
	return handles;
}

static void
lrg_mcp_resource_provider_default_init (LrgMcpResourceProviderInterface *iface)
{
	iface->handles_uri = lrg_mcp_resource_provider_default_handles_uri;
}

/**
 * lrg_mcp_resource_provider_list_resources:
 * @self: a #LrgMcpResourceProvider
 *
 * Lists all resources provided by this provider.
 *
 * Returns: (transfer full) (element-type McpResource): list of #McpResource objects
 */
GList *
lrg_mcp_resource_provider_list_resources (LrgMcpResourceProvider *self)
{
	LrgMcpResourceProviderInterface *iface;

	g_return_val_if_fail (LRG_IS_MCP_RESOURCE_PROVIDER (self), NULL);

	iface = LRG_MCP_RESOURCE_PROVIDER_GET_IFACE (self);
	g_return_val_if_fail (iface->list_resources != NULL, NULL);

	return iface->list_resources (self);
}

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
GList *
lrg_mcp_resource_provider_read_resource (LrgMcpResourceProvider  *self,
                                          const gchar             *uri,
                                          GError                 **error)
{
	LrgMcpResourceProviderInterface *iface;

	g_return_val_if_fail (LRG_IS_MCP_RESOURCE_PROVIDER (self), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	iface = LRG_MCP_RESOURCE_PROVIDER_GET_IFACE (self);
	g_return_val_if_fail (iface->read_resource != NULL, NULL);

	return iface->read_resource (self, uri, error);
}

/**
 * lrg_mcp_resource_provider_handles_uri:
 * @self: a #LrgMcpResourceProvider
 * @uri: the resource URI to check
 *
 * Checks if this provider handles the given URI.
 *
 * Returns: %TRUE if this provider can handle the URI
 */
gboolean
lrg_mcp_resource_provider_handles_uri (LrgMcpResourceProvider *self,
                                        const gchar            *uri)
{
	LrgMcpResourceProviderInterface *iface;

	g_return_val_if_fail (LRG_IS_MCP_RESOURCE_PROVIDER (self), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	iface = LRG_MCP_RESOURCE_PROVIDER_GET_IFACE (self);
	g_return_val_if_fail (iface->handles_uri != NULL, FALSE);

	return iface->handles_uri (self, uri);
}
