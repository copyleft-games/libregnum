/* test-mcp-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for MCP tool groups.
 */

#include <glib.h>
#include <libregnum.h>

#ifdef LRG_ENABLE_MCP

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
	LrgMcpInputTools      *input_tools;
	LrgMcpScreenshotTools *screenshot_tools;
	LrgMcpEngineTools     *engine_tools;
	LrgMcpEcsTools        *ecs_tools;
	LrgMcpSaveTools       *save_tools;
	LrgMcpDebugTools      *debug_tools;
} ToolsFixture;

static void
tools_fixture_set_up (ToolsFixture  *fixture,
                      gconstpointer  user_data)
{
	fixture->input_tools = lrg_mcp_input_tools_new ();
	fixture->screenshot_tools = lrg_mcp_screenshot_tools_new ();
	fixture->engine_tools = lrg_mcp_engine_tools_new ();
	fixture->ecs_tools = lrg_mcp_ecs_tools_new ();
	fixture->save_tools = lrg_mcp_save_tools_new ();
	fixture->debug_tools = lrg_mcp_debug_tools_new ();

	g_assert_nonnull (fixture->input_tools);
	g_assert_nonnull (fixture->screenshot_tools);
	g_assert_nonnull (fixture->engine_tools);
	g_assert_nonnull (fixture->ecs_tools);
	g_assert_nonnull (fixture->save_tools);
	g_assert_nonnull (fixture->debug_tools);
}

static void
tools_fixture_tear_down (ToolsFixture  *fixture,
                         gconstpointer  user_data)
{
	g_clear_object (&fixture->input_tools);
	g_clear_object (&fixture->screenshot_tools);
	g_clear_object (&fixture->engine_tools);
	g_clear_object (&fixture->ecs_tools);
	g_clear_object (&fixture->save_tools);
	g_clear_object (&fixture->debug_tools);
}

/* ==========================================================================
 * Test Cases - Tool Group Creation
 * ========================================================================== */

static void
test_mcp_input_tools_new (void)
{
	g_autoptr(LrgMcpInputTools) tools = NULL;

	tools = lrg_mcp_input_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_INPUT_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

static void
test_mcp_screenshot_tools_new (void)
{
	g_autoptr(LrgMcpScreenshotTools) tools = NULL;

	tools = lrg_mcp_screenshot_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_SCREENSHOT_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

static void
test_mcp_engine_tools_new (void)
{
	g_autoptr(LrgMcpEngineTools) tools = NULL;

	tools = lrg_mcp_engine_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_ENGINE_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

static void
test_mcp_ecs_tools_new (void)
{
	g_autoptr(LrgMcpEcsTools) tools = NULL;

	tools = lrg_mcp_ecs_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_ECS_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

static void
test_mcp_save_tools_new (void)
{
	g_autoptr(LrgMcpSaveTools) tools = NULL;

	tools = lrg_mcp_save_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_SAVE_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

static void
test_mcp_debug_tools_new (void)
{
	g_autoptr(LrgMcpDebugTools) tools = NULL;

	tools = lrg_mcp_debug_tools_new ();

	g_assert_nonnull (tools);
	g_assert_true (LRG_IS_MCP_DEBUG_TOOLS (tools));
	g_assert_true (LRG_IS_MCP_TOOL_PROVIDER (tools));
}

/* ==========================================================================
 * Test Cases - Tool Provider Interface
 * ========================================================================== */

static void
test_mcp_input_tools_list_tools (ToolsFixture  *fixture,
                                 gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->input_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have multiple input tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

static void
test_mcp_screenshot_tools_list_tools (ToolsFixture  *fixture,
                                      gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->screenshot_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have screenshot tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

static void
test_mcp_engine_tools_list_tools (ToolsFixture  *fixture,
                                  gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->engine_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have engine tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

static void
test_mcp_ecs_tools_list_tools (ToolsFixture  *fixture,
                               gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->ecs_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have ECS tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

static void
test_mcp_save_tools_list_tools (ToolsFixture  *fixture,
                                gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->save_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have save tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

static void
test_mcp_debug_tools_list_tools (ToolsFixture  *fixture,
                                 gconstpointer  user_data)
{
	LrgMcpToolProvider *provider;
	GList *tools;

	provider = LRG_MCP_TOOL_PROVIDER (fixture->debug_tools);
	tools = lrg_mcp_tool_provider_list_tools (provider);

	/* Should have debug tools registered */
	g_assert_nonnull (tools);
	g_assert_cmpuint (g_list_length (tools), >, 0);

	g_list_free_full (tools, g_object_unref);
}

/* ==========================================================================
 * Test Cases - Tool Group Names
 * ========================================================================== */

static void
test_mcp_input_tools_group_name (ToolsFixture  *fixture,
                                 gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->input_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "input");
}

static void
test_mcp_screenshot_tools_group_name (ToolsFixture  *fixture,
                                      gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->screenshot_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "screenshot");
}

static void
test_mcp_engine_tools_group_name (ToolsFixture  *fixture,
                                  gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->engine_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "engine");
}

static void
test_mcp_ecs_tools_group_name (ToolsFixture  *fixture,
                               gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->ecs_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "ecs");
}

static void
test_mcp_save_tools_group_name (ToolsFixture  *fixture,
                                gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->save_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "save");
}

static void
test_mcp_debug_tools_group_name (ToolsFixture  *fixture,
                                 gconstpointer  user_data)
{
	LrgMcpToolGroup *group;
	const gchar *name;

	group = LRG_MCP_TOOL_GROUP (fixture->debug_tools);
	name = lrg_mcp_tool_group_get_group_name (group);

	g_assert_nonnull (name);
	g_assert_cmpstr (name, ==, "debug");
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	/* Tool Group Creation */
	g_test_add_func ("/mcp/tools/input/new", test_mcp_input_tools_new);
	g_test_add_func ("/mcp/tools/screenshot/new", test_mcp_screenshot_tools_new);
	g_test_add_func ("/mcp/tools/engine/new", test_mcp_engine_tools_new);
	g_test_add_func ("/mcp/tools/ecs/new", test_mcp_ecs_tools_new);
	g_test_add_func ("/mcp/tools/save/new", test_mcp_save_tools_new);
	g_test_add_func ("/mcp/tools/debug/new", test_mcp_debug_tools_new);

	/* Tool Provider Interface - List Tools */
	g_test_add ("/mcp/tools/input/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_input_tools_list_tools,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/screenshot/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_screenshot_tools_list_tools,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/engine/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_engine_tools_list_tools,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/ecs/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_ecs_tools_list_tools,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/save/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_save_tools_list_tools,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/debug/list-tools",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_debug_tools_list_tools,
	            tools_fixture_tear_down);

	/* Tool Group Names */
	g_test_add ("/mcp/tools/input/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_input_tools_group_name,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/screenshot/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_screenshot_tools_group_name,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/engine/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_engine_tools_group_name,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/ecs/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_ecs_tools_group_name,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/save/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_save_tools_group_name,
	            tools_fixture_tear_down);

	g_test_add ("/mcp/tools/debug/group-name",
	            ToolsFixture, NULL,
	            tools_fixture_set_up,
	            test_mcp_debug_tools_group_name,
	            tools_fixture_tear_down);

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
