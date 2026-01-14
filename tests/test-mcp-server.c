/* test-mcp-server.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgMcpServer.
 */

#include <glib.h>
#include <libregnum.h>

#ifdef LRG_ENABLE_MCP

/* ==========================================================================
 * Skip Macro for MCP Tests
 *
 * MCP tests may require specific runtime conditions.
 * ========================================================================== */

#define SKIP_IF_NO_MCP() \
	do { \
		/* MCP is enabled at compile time, proceed with tests */ \
	} while (0)

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
	LrgMcpServer *server;
} McpServerFixture;

static void
server_fixture_set_up (McpServerFixture *fixture,
                       gconstpointer     user_data)
{
	fixture->server = lrg_mcp_server_get_default ();
	g_assert_nonnull (fixture->server);
}

static void
server_fixture_tear_down (McpServerFixture *fixture,
                          gconstpointer     user_data)
{
	/* Don't unref the singleton - it's managed by the library */
	if (lrg_mcp_server_is_running (fixture->server))
	{
		lrg_mcp_server_stop (fixture->server);
	}
	fixture->server = NULL;
}

/* ==========================================================================
 * Test Cases - Singleton
 * ========================================================================== */

static void
test_mcp_server_singleton (void)
{
	LrgMcpServer *server1;
	LrgMcpServer *server2;

	server1 = lrg_mcp_server_get_default ();
	g_assert_nonnull (server1);
	g_assert_true (LRG_IS_MCP_SERVER (server1));

	server2 = lrg_mcp_server_get_default ();
	g_assert_nonnull (server2);
	g_assert_true (server1 == server2);
}

/* ==========================================================================
 * Test Cases - Properties
 * ========================================================================== */

static void
test_mcp_server_properties (McpServerFixture *fixture,
                            gconstpointer     user_data)
{
	g_autofree gchar *server_name = NULL;
	gboolean running;

	g_object_get (fixture->server,
	              "server-name", &server_name,
	              "running", &running,
	              NULL);

	g_assert_nonnull (server_name);
	g_assert_cmpstr (server_name, ==, "libregnum");
	/* Server should not be running initially */
	g_assert_false (running);
}

static void
test_mcp_server_set_server_name (McpServerFixture *fixture,
                                 gconstpointer     user_data)
{
	const gchar *server_name;

	lrg_mcp_server_set_server_name (fixture->server, "test-game");
	server_name = lrg_mcp_server_get_server_name (fixture->server);

	g_assert_cmpstr (server_name, ==, "test-game");

	/* Reset for other tests */
	lrg_mcp_server_set_server_name (fixture->server, "libregnum");
}

/* ==========================================================================
 * Test Cases - Running State
 * ========================================================================== */

static void
test_mcp_server_is_running_initial (McpServerFixture *fixture,
                                    gconstpointer     user_data)
{
	g_assert_false (lrg_mcp_server_is_running (fixture->server));
}

/* ==========================================================================
 * Test Cases - Provider Registration
 * ========================================================================== */

/* Mock Tool Provider for testing */
#define TEST_TYPE_TOOL_PROVIDER (test_tool_provider_get_type ())
G_DECLARE_FINAL_TYPE (TestToolProvider, test_tool_provider, TEST, TOOL_PROVIDER, LrgMcpToolGroup)

struct _TestToolProvider
{
	LrgMcpToolGroup parent_instance;
};

G_DEFINE_TYPE (TestToolProvider, test_tool_provider, LRG_TYPE_MCP_TOOL_GROUP)

static const gchar *
test_tool_provider_get_group_name (LrgMcpToolGroup *group)
{
	return "test";
}

static void
test_tool_provider_register_tools (LrgMcpToolGroup *group)
{
	/* No tools registered for this mock */
}

static void
test_tool_provider_class_init (TestToolProviderClass *klass)
{
	LrgMcpToolGroupClass *group_class = LRG_MCP_TOOL_GROUP_CLASS (klass);
	group_class->get_group_name = test_tool_provider_get_group_name;
	group_class->register_tools = test_tool_provider_register_tools;
}

static void
test_tool_provider_init (TestToolProvider *self)
{
}

static TestToolProvider *
test_tool_provider_new (void)
{
	return g_object_new (TEST_TYPE_TOOL_PROVIDER, NULL);
}

/* Mock Resource Provider for testing */
#define TEST_TYPE_RESOURCE_PROVIDER (test_resource_provider_get_type ())
G_DECLARE_FINAL_TYPE (TestResourceProvider, test_resource_provider, TEST, RESOURCE_PROVIDER, LrgMcpResourceGroup)

struct _TestResourceProvider
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_TYPE (TestResourceProvider, test_resource_provider, LRG_TYPE_MCP_RESOURCE_GROUP)

static const gchar *
test_resource_provider_get_group_name (LrgMcpResourceGroup *group)
{
	return "test";
}

static void
test_resource_provider_register_resources (LrgMcpResourceGroup *group)
{
	/* No resources registered for this mock */
}

static GList *
test_resource_provider_read_resource (LrgMcpResourceGroup  *group,
                                      const gchar          *uri,
                                      GError              **error)
{
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	             "Resource not found: %s", uri);
	return NULL;
}

static void
test_resource_provider_class_init (TestResourceProviderClass *klass)
{
	LrgMcpResourceGroupClass *group_class = LRG_MCP_RESOURCE_GROUP_CLASS (klass);
	group_class->get_group_name = test_resource_provider_get_group_name;
	group_class->register_resources = test_resource_provider_register_resources;
	group_class->read_resource = test_resource_provider_read_resource;
}

static void
test_resource_provider_init (TestResourceProvider *self)
{
}

static TestResourceProvider *
test_resource_provider_new (void)
{
	return g_object_new (TEST_TYPE_RESOURCE_PROVIDER, NULL);
}

static void
test_mcp_server_add_tool_provider (McpServerFixture *fixture,
                                   gconstpointer     user_data)
{
	TestToolProvider *provider;

	provider = test_tool_provider_new ();
	g_assert_nonnull (provider);

	/* This should not crash */
	lrg_mcp_server_add_tool_provider (fixture->server,
	                                   LRG_MCP_TOOL_PROVIDER (provider));
}

static void
test_mcp_server_add_resource_provider (McpServerFixture *fixture,
                                       gconstpointer     user_data)
{
	TestResourceProvider *provider;

	provider = test_resource_provider_new ();
	g_assert_nonnull (provider);

	/* This should not crash */
	lrg_mcp_server_add_resource_provider (fixture->server,
	                                       LRG_MCP_RESOURCE_PROVIDER (provider));
}

/* ==========================================================================
 * Test Cases - Default Providers
 * ========================================================================== */

static void
test_mcp_server_register_default_providers (McpServerFixture *fixture,
                                            gconstpointer     user_data)
{
	/* This should not crash and should register all default providers */
	lrg_mcp_server_register_default_providers (fixture->server);

	/* We can't easily verify the providers were registered without
	 * exposing internal state, but at least it shouldn't crash */
	g_assert_true (TRUE);
}

/* ==========================================================================
 * Test Cases - Transport Type Configuration
 * ========================================================================== */

static void
test_mcp_server_transport_type_default (McpServerFixture *fixture,
                                        gconstpointer     user_data)
{
	LrgMcpTransportType type;

	type = lrg_mcp_server_get_transport_type (fixture->server);
	g_assert_cmpint (type, ==, LRG_MCP_TRANSPORT_STDIO);
}

static void
test_mcp_server_set_transport_type (McpServerFixture *fixture,
                                    gconstpointer     user_data)
{
	/* Test HTTP transport type */
	lrg_mcp_server_set_transport_type (fixture->server, LRG_MCP_TRANSPORT_HTTP);
	g_assert_cmpint (lrg_mcp_server_get_transport_type (fixture->server),
	                 ==, LRG_MCP_TRANSPORT_HTTP);

	/* Test BOTH transport type */
	lrg_mcp_server_set_transport_type (fixture->server, LRG_MCP_TRANSPORT_BOTH);
	g_assert_cmpint (lrg_mcp_server_get_transport_type (fixture->server),
	                 ==, LRG_MCP_TRANSPORT_BOTH);

	/* Reset to default for other tests */
	lrg_mcp_server_set_transport_type (fixture->server, LRG_MCP_TRANSPORT_STDIO);
}

/* ==========================================================================
 * Test Cases - HTTP Configuration
 * ========================================================================== */

static void
test_mcp_server_http_port (McpServerFixture *fixture,
                           gconstpointer     user_data)
{
	/* Default port should be 8080 */
	g_assert_cmpuint (lrg_mcp_server_get_http_port (fixture->server), ==, 8080);

	/* Set custom port */
	lrg_mcp_server_set_http_port (fixture->server, 9090);
	g_assert_cmpuint (lrg_mcp_server_get_http_port (fixture->server), ==, 9090);

	/* Test port 0 (auto-assign) */
	lrg_mcp_server_set_http_port (fixture->server, 0);
	g_assert_cmpuint (lrg_mcp_server_get_http_port (fixture->server), ==, 0);

	/* Reset to default */
	lrg_mcp_server_set_http_port (fixture->server, 8080);
}

static void
test_mcp_server_http_host (McpServerFixture *fixture,
                           gconstpointer     user_data)
{
	const gchar *host;

	/* Default host should be NULL (all interfaces) */
	host = lrg_mcp_server_get_http_host (fixture->server);
	g_assert_null (host);

	/* Set localhost binding */
	lrg_mcp_server_set_http_host (fixture->server, "127.0.0.1");
	host = lrg_mcp_server_get_http_host (fixture->server);
	g_assert_cmpstr (host, ==, "127.0.0.1");

	/* Reset to NULL */
	lrg_mcp_server_set_http_host (fixture->server, NULL);
	host = lrg_mcp_server_get_http_host (fixture->server);
	g_assert_null (host);
}

static void
test_mcp_server_http_auth (McpServerFixture *fixture,
                           gconstpointer     user_data)
{
	/* Setting auth should not crash */
	lrg_mcp_server_set_http_auth (fixture->server, TRUE, "test-token");

	/* Disable auth */
	lrg_mcp_server_set_http_auth (fixture->server, FALSE, NULL);

	/* Test with empty token */
	lrg_mcp_server_set_http_auth (fixture->server, TRUE, "");

	/* Reset */
	lrg_mcp_server_set_http_auth (fixture->server, FALSE, NULL);
}

static void
test_mcp_server_actual_http_port (McpServerFixture *fixture,
                                  gconstpointer     user_data)
{
	guint port;

	/* When not running, actual port should be 0 */
	port = lrg_mcp_server_get_actual_http_port (fixture->server);
	g_assert_cmpuint (port, ==, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	/* Singleton */
	g_test_add_func ("/mcp/server/singleton", test_mcp_server_singleton);

	/* Properties */
	g_test_add ("/mcp/server/properties/basic",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_properties,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/properties/set-name",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_set_server_name,
	            server_fixture_tear_down);

	/* Running State */
	g_test_add ("/mcp/server/running/initial",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_is_running_initial,
	            server_fixture_tear_down);

	/* Provider Registration */
	g_test_add ("/mcp/server/provider/add-tool",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_add_tool_provider,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/provider/add-resource",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_add_resource_provider,
	            server_fixture_tear_down);

	/* Default Providers */
	g_test_add ("/mcp/server/provider/register-defaults",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_register_default_providers,
	            server_fixture_tear_down);

	/* Transport Type */
	g_test_add ("/mcp/server/transport/default",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_transport_type_default,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/transport/set-type",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_set_transport_type,
	            server_fixture_tear_down);

	/* HTTP Configuration */
	g_test_add ("/mcp/server/http/port",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_http_port,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/http/host",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_http_host,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/http/auth",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_http_auth,
	            server_fixture_tear_down);

	g_test_add ("/mcp/server/http/actual-port",
	            McpServerFixture, NULL,
	            server_fixture_set_up,
	            test_mcp_server_actual_http_port,
	            server_fixture_tear_down);

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
