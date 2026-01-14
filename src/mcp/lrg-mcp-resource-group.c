/* lrg-mcp-resource-group.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for MCP resource groups.
 */

#include "lrg-mcp-resource-group.h"
#include "../lrg-log.h"

#include <gio/gio.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-resource-group
 * @title: LrgMcpResourceGroup
 * @short_description: Abstract base class for MCP resource groups
 *
 * #LrgMcpResourceGroup is an abstract base class that implements the
 * #LrgMcpResourceProvider interface. Subclasses provide related sets
 * of resources (e.g., engine resources, ECS resources).
 *
 * ## URI Scheme
 *
 * Resources use URIs in the `libregnum://` scheme:
 * - `libregnum://engine/info` - Engine state
 * - `libregnum://ecs/worlds` - ECS world list
 * - `libregnum://screenshot/current` - Current screenshot
 *
 * ## Subclassing
 *
 * Override the following virtual methods:
 * - `get_group_name`: Return a name for debugging
 * - `register_resources`: Add resources via lrg_mcp_resource_group_add_resource()
 * - `read_resource`: Provide resource data
 */

typedef struct
{
	GPtrArray *resources;  /* Owned McpResource objects */
	gchar     *uri_prefix; /* URI prefix for handles_uri matching */
} LrgMcpResourceGroupPrivate;

static void lrg_mcp_resource_provider_iface_init (LrgMcpResourceProviderInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (LrgMcpResourceGroup, lrg_mcp_resource_group, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (LrgMcpResourceGroup)
                                  G_IMPLEMENT_INTERFACE (LRG_TYPE_MCP_RESOURCE_PROVIDER,
                                                         lrg_mcp_resource_provider_iface_init))

/* ==========================================================================
 * LrgMcpResourceProvider Interface Implementation
 * ========================================================================== */

static GList *
lrg_mcp_resource_group_list_resources_impl (LrgMcpResourceProvider *provider)
{
	LrgMcpResourceGroup *self = LRG_MCP_RESOURCE_GROUP (provider);
	LrgMcpResourceGroupPrivate *priv = lrg_mcp_resource_group_get_instance_private (self);
	GList *list = NULL;
	guint i;

	for (i = 0; i < priv->resources->len; i++)
	{
		McpResource *resource = g_ptr_array_index (priv->resources, i);
		list = g_list_append (list, g_object_ref (resource));
	}

	return list;
}

static GList *
lrg_mcp_resource_group_read_resource_impl (LrgMcpResourceProvider  *provider,
                                            const gchar             *uri,
                                            GError                 **error)
{
	LrgMcpResourceGroup *self = LRG_MCP_RESOURCE_GROUP (provider);
	LrgMcpResourceGroupClass *klass = LRG_MCP_RESOURCE_GROUP_GET_CLASS (self);

	if (klass->read_resource == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Resource group does not implement read_resource");
		return NULL;
	}

	return klass->read_resource (self, uri, error);
}

static gboolean
lrg_mcp_resource_group_handles_uri_impl (LrgMcpResourceProvider *provider,
                                          const gchar            *uri)
{
	LrgMcpResourceGroup *self = LRG_MCP_RESOURCE_GROUP (provider);
	LrgMcpResourceGroupPrivate *priv = lrg_mcp_resource_group_get_instance_private (self);

	if (priv->uri_prefix != NULL)
	{
		return g_str_has_prefix (uri, priv->uri_prefix);
	}

	/* Fall back to checking resource list */
	return lrg_mcp_resource_provider_handles_uri (provider, uri);
}

static void
lrg_mcp_resource_provider_iface_init (LrgMcpResourceProviderInterface *iface)
{
	iface->list_resources = lrg_mcp_resource_group_list_resources_impl;
	iface->read_resource = lrg_mcp_resource_group_read_resource_impl;
	iface->handles_uri = lrg_mcp_resource_group_handles_uri_impl;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_resource_group_constructed (GObject *object)
{
	LrgMcpResourceGroup *self = LRG_MCP_RESOURCE_GROUP (object);
	LrgMcpResourceGroupClass *klass = LRG_MCP_RESOURCE_GROUP_GET_CLASS (self);

	G_OBJECT_CLASS (lrg_mcp_resource_group_parent_class)->constructed (object);

	/* Call subclass to register its resources */
	if (klass->register_resources != NULL)
	{
		klass->register_resources (self);
	}
}

static void
lrg_mcp_resource_group_finalize (GObject *object)
{
	LrgMcpResourceGroup *self = LRG_MCP_RESOURCE_GROUP (object);
	LrgMcpResourceGroupPrivate *priv = lrg_mcp_resource_group_get_instance_private (self);

	g_clear_pointer (&priv->resources, g_ptr_array_unref);
	g_clear_pointer (&priv->uri_prefix, g_free);

	G_OBJECT_CLASS (lrg_mcp_resource_group_parent_class)->finalize (object);
}

static void
lrg_mcp_resource_group_class_init (LrgMcpResourceGroupClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = lrg_mcp_resource_group_constructed;
	object_class->finalize = lrg_mcp_resource_group_finalize;

	/* Default implementations */
	klass->get_group_name = NULL;
	klass->register_resources = NULL;
	klass->read_resource = NULL;
}

static void
lrg_mcp_resource_group_init (LrgMcpResourceGroup *self)
{
	LrgMcpResourceGroupPrivate *priv = lrg_mcp_resource_group_get_instance_private (self);

	priv->resources = g_ptr_array_new_with_free_func (g_object_unref);
	priv->uri_prefix = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_resource_group_add_resource:
 * @self: an #LrgMcpResourceGroup
 * @resource: (transfer full): the resource to add
 *
 * Adds a resource to this group. The group takes ownership of the resource.
 */
void
lrg_mcp_resource_group_add_resource (LrgMcpResourceGroup *self,
                                      McpResource         *resource)
{
	LrgMcpResourceGroupPrivate *priv;

	g_return_if_fail (LRG_IS_MCP_RESOURCE_GROUP (self));
	g_return_if_fail (MCP_IS_RESOURCE (resource));

	priv = lrg_mcp_resource_group_get_instance_private (self);
	g_ptr_array_add (priv->resources, resource);  /* Takes ownership */
}

/**
 * lrg_mcp_resource_group_get_group_name:
 * @self: an #LrgMcpResourceGroup
 *
 * Gets the name of this resource group.
 *
 * Returns: (transfer none): the group name
 */
const gchar *
lrg_mcp_resource_group_get_group_name (LrgMcpResourceGroup *self)
{
	LrgMcpResourceGroupClass *klass;

	g_return_val_if_fail (LRG_IS_MCP_RESOURCE_GROUP (self), NULL);

	klass = LRG_MCP_RESOURCE_GROUP_GET_CLASS (self);
	if (klass->get_group_name != NULL)
	{
		return klass->get_group_name (self);
	}

	return "unknown";
}

/**
 * lrg_mcp_resource_group_get_uri_prefix:
 * @self: an #LrgMcpResourceGroup
 *
 * Gets the URI prefix for this resource group.
 *
 * Returns: (transfer none): the URI prefix
 */
const gchar *
lrg_mcp_resource_group_get_uri_prefix (LrgMcpResourceGroup *self)
{
	LrgMcpResourceGroupPrivate *priv;

	g_return_val_if_fail (LRG_IS_MCP_RESOURCE_GROUP (self), NULL);

	priv = lrg_mcp_resource_group_get_instance_private (self);
	return priv->uri_prefix;
}

/**
 * lrg_mcp_resource_group_set_uri_prefix:
 * @self: an #LrgMcpResourceGroup
 * @prefix: the URI prefix
 *
 * Sets the URI prefix for this resource group.
 */
void
lrg_mcp_resource_group_set_uri_prefix (LrgMcpResourceGroup *self,
                                        const gchar         *prefix)
{
	LrgMcpResourceGroupPrivate *priv;

	g_return_if_fail (LRG_IS_MCP_RESOURCE_GROUP (self));

	priv = lrg_mcp_resource_group_get_instance_private (self);
	g_free (priv->uri_prefix);
	priv->uri_prefix = g_strdup (prefix);
}
