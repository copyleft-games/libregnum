/* lrg-mcp-engine-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for engine state.
 */

#include "lrg-mcp-engine-resources.h"
#include "../../core/lrg-engine.h"
#include "../../lrg-log.h"
#include "../../lrg-version.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-engine-resources
 * @title: LrgMcpEngineResources
 * @short_description: MCP resources for engine state
 *
 * #LrgMcpEngineResources provides MCP resources for read-only
 * access to engine state, configuration, and the type registry.
 *
 * Note: Some resources return placeholder data until additional
 * engine API is implemented.
 */

#define URI_PREFIX "libregnum://engine/"

struct _LrgMcpEngineResources
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEngineResources, lrg_mcp_engine_resources, LRG_TYPE_MCP_RESOURCE_GROUP)

/* ==========================================================================
 * Resource Handlers
 * ========================================================================== */

static GList *
read_engine_info (LrgMcpEngineResources  *self,
                  GError                **error)
{
	LrgEngine *engine;
	LrgEngineState state;
	const gchar *state_str;
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

	state = lrg_engine_get_state (engine);
	switch (state)
	{
	case LRG_ENGINE_STATE_UNINITIALIZED:
		state_str = "uninitialized";
		break;
	case LRG_ENGINE_STATE_INITIALIZING:
		state_str = "initializing";
		break;
	case LRG_ENGINE_STATE_RUNNING:
		state_str = "running";
		break;
	case LRG_ENGINE_STATE_PAUSED:
		state_str = "paused";
		break;
	case LRG_ENGINE_STATE_SHUTTING_DOWN:
		state_str = "shutting_down";
		break;
	case LRG_ENGINE_STATE_TERMINATED:
		state_str = "terminated";
		break;
	default:
		state_str = "unknown";
		break;
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "state");
	json_builder_add_string_value (builder, state_str);

	json_builder_set_member_name (builder, "running");
	json_builder_add_boolean_value (builder, lrg_engine_is_running (engine));

	json_builder_set_member_name (builder, "version");
	json_builder_add_string_value (builder, LRG_VERSION_STRING);

	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	contents = mcp_resource_contents_new_text (URI_PREFIX "info", json_str, "application/json");
	return g_list_append (NULL, contents);
}

static GList *
read_engine_config (LrgMcpEngineResources  *self,
                    GError                **error)
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

	/* Stub: Return placeholder config - detailed config API not yet available */
	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder, "Detailed config API not yet implemented");

	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	contents = mcp_resource_contents_new_text (URI_PREFIX "config", json_str, "application/json");
	return g_list_append (NULL, contents);
}

static GList *
read_engine_registry (LrgMcpEngineResources  *self,
                      GError                **error)
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

	/* Stub: Return placeholder registry - listing API not yet available */
	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "types");
	json_builder_begin_array (builder);
	json_builder_end_array (builder);

	json_builder_set_member_name (builder, "note");
	json_builder_add_string_value (builder, "Registry listing API not yet implemented");

	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	contents = mcp_resource_contents_new_text (URI_PREFIX "registry", json_str, "application/json");
	return g_list_append (NULL, contents);
}

/* ==========================================================================
 * LrgMcpResourceGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_engine_resources_get_group_name (LrgMcpResourceGroup *group)
{
	return "engine";
}

static void
lrg_mcp_engine_resources_register_resources (LrgMcpResourceGroup *group)
{
	McpResource *resource;

	lrg_mcp_resource_group_set_uri_prefix (group, URI_PREFIX);

	resource = mcp_resource_new (URI_PREFIX "info", "Engine state information");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);

	resource = mcp_resource_new (URI_PREFIX "config", "Engine configuration");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);

	resource = mcp_resource_new (URI_PREFIX "registry", "Registered type names");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);
}

static GList *
lrg_mcp_engine_resources_read_resource (LrgMcpResourceGroup  *group,
                                        const gchar          *uri,
                                        GError              **error)
{
	LrgMcpEngineResources *self = LRG_MCP_ENGINE_RESOURCES (group);

	if (g_strcmp0 (uri, URI_PREFIX "info") == 0)
		return read_engine_info (self, error);
	if (g_strcmp0 (uri, URI_PREFIX "config") == 0)
		return read_engine_config (self, error);
	if (g_strcmp0 (uri, URI_PREFIX "registry") == 0)
		return read_engine_registry (self, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	             "Unknown resource: %s", uri);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_engine_resources_class_init (LrgMcpEngineResourcesClass *klass)
{
	LrgMcpResourceGroupClass *group_class = LRG_MCP_RESOURCE_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_engine_resources_get_group_name;
	group_class->register_resources = lrg_mcp_engine_resources_register_resources;
	group_class->read_resource = lrg_mcp_engine_resources_read_resource;
}

static void
lrg_mcp_engine_resources_init (LrgMcpEngineResources *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_mcp_engine_resources_new:
 *
 * Creates a new engine resources provider.
 *
 * Returns: (transfer full): A new #LrgMcpEngineResources
 */
LrgMcpEngineResources *
lrg_mcp_engine_resources_new (void)
{
	return g_object_new (LRG_TYPE_MCP_ENGINE_RESOURCES, NULL);
}
