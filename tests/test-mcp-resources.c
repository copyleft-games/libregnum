/* test-mcp-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for MCP resource groups.
 */

#include <glib.h>
#include <libregnum.h>

#ifdef LRG_ENABLE_MCP

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
	LrgMcpEngineResources     *engine_resources;
	LrgMcpEcsResources        *ecs_resources;
	LrgMcpScreenshotResources *screenshot_resources;
} ResourcesFixture;

static void
resources_fixture_set_up (ResourcesFixture *fixture,
                          gconstpointer     user_data)
{
	fixture->engine_resources = lrg_mcp_engine_resources_new ();
	fixture->ecs_resources = lrg_mcp_ecs_resources_new ();
	fixture->screenshot_resources = lrg_mcp_screenshot_resources_new ();

	g_assert_nonnull (fixture->engine_resources);
	g_assert_nonnull (fixture->ecs_resources);
	g_assert_nonnull (fixture->screenshot_resources);
}

static void
resources_fixture_tear_down (ResourcesFixture *fixture,
                             gconstpointer     user_data)
{
	g_clear_object (&fixture->engine_resources);
	g_clear_object (&fixture->ecs_resources);
	g_clear_object (&fixture->screenshot_resources);
}

/* ==========================================================================
 * Test Cases - Resource Group Creation
 * ========================================================================== */

static void
test_mcp_engine_resources_new (void)
{
	g_autoptr(LrgMcpEngineResources) resources = NULL;

	resources = lrg_mcp_engine_resources_new ();

	g_assert_nonnull (resources);
	g_assert_true (LRG_IS_MCP_ENGINE_RESOURCES (resources));
	g_assert_true (LRG_IS_MCP_RESOURCE_PROVIDER (resources));
}

static void
test_mcp_ecs_resources_new (void)
{
	g_autoptr(LrgMcpEcsResources) resources = NULL;

	resources = lrg_mcp_ecs_resources_new ();

	g_assert_nonnull (resources);
	g_assert_true (LRG_IS_MCP_ECS_RESOURCES (resources));
	g_assert_true (LRG_IS_MCP_RESOURCE_PROVIDER (resources));
}

static void
test_mcp_screenshot_resources_new (void)
{
	g_autoptr(LrgMcpScreenshotResources) resources = NULL;

	resources = lrg_mcp_screenshot_resources_new ();

	g_assert_nonnull (resources);
	g_assert_true (LRG_IS_MCP_SCREENSHOT_RESOURCES (resources));
	g_assert_true (LRG_IS_MCP_RESOURCE_PROVIDER (resources));
}

/* ==========================================================================
 * Test Cases - Resource Provider Interface
 * ========================================================================== */

static void
test_mcp_engine_resources_list_resources (ResourcesFixture *fixture,
                                          gconstpointer     user_data)
{
	LrgMcpResourceProvider *provider;
	GList *resources;

	provider = LRG_MCP_RESOURCE_PROVIDER (fixture->engine_resources);
	resources = lrg_mcp_resource_provider_list_resources (provider);

	/* Should have engine resources registered */
	g_assert_nonnull (resources);
	g_assert_cmpuint (g_list_length (resources), >, 0);

	g_list_free_full (resources, g_object_unref);
}

static void
test_mcp_ecs_resources_list_resources (ResourcesFixture *fixture,
                                       gconstpointer     user_data)
{
	LrgMcpResourceProvider *provider;
	GList *resources;

	provider = LRG_MCP_RESOURCE_PROVIDER (fixture->ecs_resources);
	resources = lrg_mcp_resource_provider_list_resources (provider);

	/* Should have ECS resources registered */
	g_assert_nonnull (resources);
	g_assert_cmpuint (g_list_length (resources), >, 0);

	g_list_free_full (resources, g_object_unref);
}

static void
test_mcp_screenshot_resources_list_resources (ResourcesFixture *fixture,
                                              gconstpointer     user_data)
{
	LrgMcpResourceProvider *provider;
	GList *resources;

	provider = LRG_MCP_RESOURCE_PROVIDER (fixture->screenshot_resources);
	resources = lrg_mcp_resource_provider_list_resources (provider);

	/* Should have screenshot resources registered */
	g_assert_nonnull (resources);
	g_assert_cmpuint (g_list_length (resources), >, 0);

	g_list_free_full (resources, g_object_unref);
}

/* ==========================================================================
 * Test Cases - Resource Group Names
 * ========================================================================== */

static void
test_mcp_engine_resources_group_name (ResourcesFixture *fixture,
                                      gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *name;

	group = LRG_MCP_RESOURCE_GROUP (fixture->engine_resources);
	name = lrg_mcp_resource_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "engine");
}

static void
test_mcp_ecs_resources_group_name (ResourcesFixture *fixture,
                                   gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *name;

	group = LRG_MCP_RESOURCE_GROUP (fixture->ecs_resources);
	name = lrg_mcp_resource_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "ecs");
}

static void
test_mcp_screenshot_resources_group_name (ResourcesFixture *fixture,
                                          gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *name;

	group = LRG_MCP_RESOURCE_GROUP (fixture->screenshot_resources);
	name = lrg_mcp_resource_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "screenshot");
}

/* ==========================================================================
 * Test Cases - URI Prefix
 * ========================================================================== */

static void
test_mcp_engine_resources_uri_prefix (ResourcesFixture *fixture,
                                      gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *prefix;

	group = LRG_MCP_RESOURCE_GROUP (fixture->engine_resources);
	prefix = lrg_mcp_resource_group_get_uri_prefix (group);

	g_assert_nonnull (prefix);
	g_assert_cmpstr (prefix, ==, "libregnum://engine/");
}

static void
test_mcp_ecs_resources_uri_prefix (ResourcesFixture *fixture,
                                   gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *prefix;

	group = LRG_MCP_RESOURCE_GROUP (fixture->ecs_resources);
	prefix = lrg_mcp_resource_group_get_uri_prefix (group);

	g_assert_nonnull (prefix);
	g_assert_cmpstr (prefix, ==, "libregnum://ecs/");
}

static void
test_mcp_screenshot_resources_uri_prefix (ResourcesFixture *fixture,
                                          gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	const gchar *prefix;

	group = LRG_MCP_RESOURCE_GROUP (fixture->screenshot_resources);
	prefix = lrg_mcp_resource_group_get_uri_prefix (group);

	g_assert_nonnull (prefix);
	g_assert_cmpstr (prefix, ==, "libregnum://screenshot/");
}

/* ==========================================================================
 * Test Cases - Can Handle URI
 * ========================================================================== */

static void
test_mcp_engine_resources_can_handle (ResourcesFixture *fixture,
                                      gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;

	group = LRG_MCP_RESOURCE_GROUP (fixture->engine_resources);

	/* Should handle engine URIs */
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://engine/info"));
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://engine/config"));
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://engine/registry"));

	/* Should not handle other URIs */
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://ecs/worlds"));
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://screenshot/current"));
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "https://example.com"));
}

static void
test_mcp_ecs_resources_can_handle (ResourcesFixture *fixture,
                                   gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;

	group = LRG_MCP_RESOURCE_GROUP (fixture->ecs_resources);

	/* Should handle ECS URIs */
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://ecs/worlds"));
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://ecs/world/test"));
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://ecs/object/123"));

	/* Should not handle other URIs */
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://engine/info"));
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://screenshot/current"));
}

static void
test_mcp_screenshot_resources_can_handle (ResourcesFixture *fixture,
                                          gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;

	group = LRG_MCP_RESOURCE_GROUP (fixture->screenshot_resources);

	/* Should handle screenshot URIs */
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://screenshot/current"));
	g_assert_true (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://screenshot/thumbnail"));

	/* Should not handle other URIs */
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://engine/info"));
	g_assert_false (lrg_mcp_resource_provider_handles_uri (LRG_MCP_RESOURCE_PROVIDER (group), "libregnum://ecs/worlds"));
}

/* ==========================================================================
 * Test Cases - Read Resource (Invalid URIs)
 * ========================================================================== */

static void
test_mcp_engine_resources_read_invalid (ResourcesFixture *fixture,
                                        gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	g_autoptr(GError) error = NULL;
	GList *contents;

	group = LRG_MCP_RESOURCE_GROUP (fixture->engine_resources);

	contents = lrg_mcp_resource_provider_read_resource (LRG_MCP_RESOURCE_PROVIDER (group),
	                                                  "libregnum://engine/nonexistent",
	                                                  &error);

	g_assert_null (contents);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_mcp_ecs_resources_read_invalid (ResourcesFixture *fixture,
                                     gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	g_autoptr(GError) error = NULL;
	GList *contents;

	group = LRG_MCP_RESOURCE_GROUP (fixture->ecs_resources);

	contents = lrg_mcp_resource_provider_read_resource (LRG_MCP_RESOURCE_PROVIDER (group),
	                                                  "libregnum://ecs/nonexistent",
	                                                  &error);

	g_assert_null (contents);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_mcp_screenshot_resources_read_invalid (ResourcesFixture *fixture,
                                            gconstpointer     user_data)
{
	LrgMcpResourceGroup *group;
	g_autoptr(GError) error = NULL;
	GList *contents;

	group = LRG_MCP_RESOURCE_GROUP (fixture->screenshot_resources);

	contents = lrg_mcp_resource_provider_read_resource (LRG_MCP_RESOURCE_PROVIDER (group),
	                                                  "libregnum://screenshot/nonexistent",
	                                                  &error);

	g_assert_null (contents);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	/* Resource Group Creation */
	g_test_add_func ("/mcp/resources/engine/new", test_mcp_engine_resources_new);
	g_test_add_func ("/mcp/resources/ecs/new", test_mcp_ecs_resources_new);
	g_test_add_func ("/mcp/resources/screenshot/new", test_mcp_screenshot_resources_new);

	/* Resource Provider Interface - List Resources */
	g_test_add ("/mcp/resources/engine/list-resources",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_engine_resources_list_resources,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/ecs/list-resources",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_ecs_resources_list_resources,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/screenshot/list-resources",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_screenshot_resources_list_resources,
	            resources_fixture_tear_down);

	/* Resource Group Names */
	g_test_add ("/mcp/resources/engine/group-name",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_engine_resources_group_name,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/ecs/group-name",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_ecs_resources_group_name,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/screenshot/group-name",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_screenshot_resources_group_name,
	            resources_fixture_tear_down);

	/* URI Prefix */
	g_test_add ("/mcp/resources/engine/uri-prefix",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_engine_resources_uri_prefix,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/ecs/uri-prefix",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_ecs_resources_uri_prefix,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/screenshot/uri-prefix",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_screenshot_resources_uri_prefix,
	            resources_fixture_tear_down);

	/* Can Handle URI */
	g_test_add ("/mcp/resources/engine/can-handle",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_engine_resources_can_handle,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/ecs/can-handle",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_ecs_resources_can_handle,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/screenshot/can-handle",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_screenshot_resources_can_handle,
	            resources_fixture_tear_down);

	/* Read Resource (Invalid URIs) */
	g_test_add ("/mcp/resources/engine/read-invalid",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_engine_resources_read_invalid,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/ecs/read-invalid",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_ecs_resources_read_invalid,
	            resources_fixture_tear_down);

	g_test_add ("/mcp/resources/screenshot/read-invalid",
	            ResourcesFixture, NULL,
	            resources_fixture_set_up,
	            test_mcp_screenshot_resources_read_invalid,
	            resources_fixture_tear_down);

	return g_test_run ();
}

#else /* !LRG_ENABLE_MCP */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_skip ("MCP support not enabled (build with MCP=1)");
	return 0;
}

#endif /* LRG_ENABLE_MCP */
