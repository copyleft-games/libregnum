/* lrg-mcp-ecs-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for ECS/World state.
 *
 */

#include "lrg-mcp-ecs-resources.h"
#include "../lrg-mcp-inspect-private.h"
#include "../lrg-mcp-tool-group.h"
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
 */

#define URI_PREFIX "libregnum://ecs/"

struct _LrgMcpEcsResources
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEcsResources, lrg_mcp_ecs_resources, LRG_TYPE_MCP_RESOURCE_GROUP)

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

	/* World and object URIs resolve dynamically against registered worlds. */
}

static GList *
lrg_mcp_ecs_resources_read_resource (LrgMcpResourceGroup *group,
                                     const gchar *uri,
                                     GError **error)
{
    g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);
    g_autofree gchar *text = NULL;
    JsonObject *object = NULL;
    McpResourceContents *contents;

    if (g_str_equal (uri, URI_PREFIX "worlds"))
        object = _lrg_mcp_worlds_json ();
    else if (g_str_has_prefix (uri, URI_PREFIX "world/"))
    {
        g_autofree gchar *name = g_uri_unescape_string (uri + strlen (URI_PREFIX "world/"), NULL);
        LrgWorld *world;
        if (name == NULL || *name == '\0')
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid world URI");
            return NULL;
        }
        world = _lrg_mcp_world (name, error);
        if (world == NULL)
            return NULL;
        object = _lrg_mcp_world_json (name, world);
    }
    else if (g_str_has_prefix (uri, URI_PREFIX "object/"))
    {
        LrgGameObject *entity = _lrg_mcp_find_object (uri + strlen (URI_PREFIX "object/"), NULL, error);
        if (entity == NULL)
            return NULL;
        object = _lrg_mcp_object_json (entity);
    }
    else
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown ECS resource: %s", uri);
        return NULL;
    }
    json_node_take_object (node, object);
    text = json_to_string (node, FALSE);
    contents = mcp_resource_contents_new_text (uri, text, "application/json");
    return g_list_append (NULL, contents);
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
