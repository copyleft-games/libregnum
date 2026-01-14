/* lrg-mcp-ecs-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for ECS/World state.
 *
 * NOTE: This is a stub implementation. Full ECS introspection requires
 * additional API to be added to LrgEngine, LrgWorld, and LrgGameObject.
 */

#include "lrg-mcp-ecs-resources.h"
#include "../../core/lrg-engine.h"
#include "../../lrg-log.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-ecs-resources
 * @title: LrgMcpEcsResources
 * @short_description: MCP resources for ECS state
 *
 * #LrgMcpEcsResources provides MCP resources for read-only
 * access to worlds, game objects, and their components.
 *
 * Note: This is currently a stub implementation that returns placeholder
 * data. Full implementation requires additional introspection API in the
 * ECS module.
 */

#define URI_PREFIX "libregnum://ecs/"

struct _LrgMcpEcsResources
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEcsResources, lrg_mcp_ecs_resources, LRG_TYPE_MCP_RESOURCE_GROUP)

/* ==========================================================================
 * Resource Handlers (Stub implementations)
 * ========================================================================== */

static GList *
read_worlds_list (LrgMcpEcsResources  *self,
                  GError             **error)
{
	LrgEngine *engine;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;
	McpResourceContents *contents;

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

	contents = mcp_resource_contents_new_text (URI_PREFIX "worlds", json_str, "application/json");
	return g_list_append (NULL, contents);
}

/* ==========================================================================
 * LrgMcpResourceGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_ecs_resources_get_group_name (LrgMcpResourceGroup *group)
{
	return "ecs";
}

static void
lrg_mcp_ecs_resources_register_resources (LrgMcpResourceGroup *group)
{
	McpResource *resource;

	lrg_mcp_resource_group_set_uri_prefix (group, URI_PREFIX);

	resource = mcp_resource_new (URI_PREFIX "worlds", "List of active game worlds");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);

	/* World and object resources are dynamic (template-based)
	 * but not yet implemented due to missing introspection API */
}

static GList *
lrg_mcp_ecs_resources_read_resource (LrgMcpResourceGroup  *group,
                                     const gchar          *uri,
                                     GError              **error)
{
	LrgMcpEcsResources *self = LRG_MCP_ECS_RESOURCES (group);

	/* Static resource */
	if (g_strcmp0 (uri, URI_PREFIX "worlds") == 0)
		return read_worlds_list (self, error);

	/* Dynamic resources not yet implemented */
	if (g_str_has_prefix (uri, URI_PREFIX "world/") ||
	    g_str_has_prefix (uri, URI_PREFIX "object/"))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "ECS introspection API not yet implemented");
		return NULL;
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	             "Unknown resource: %s", uri);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_ecs_resources_class_init (LrgMcpEcsResourcesClass *klass)
{
	LrgMcpResourceGroupClass *group_class = LRG_MCP_RESOURCE_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_ecs_resources_get_group_name;
	group_class->register_resources = lrg_mcp_ecs_resources_register_resources;
	group_class->read_resource = lrg_mcp_ecs_resources_read_resource;
}

static void
lrg_mcp_ecs_resources_init (LrgMcpEcsResources *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_ecs_resources_new:
 *
 * Creates a new ECS resources provider.
 *
 * Returns: (transfer full): A new #LrgMcpEcsResources
 */
LrgMcpEcsResources *
lrg_mcp_ecs_resources_new (void)
{
	return g_object_new (LRG_TYPE_MCP_ECS_RESOURCES, NULL);
}
