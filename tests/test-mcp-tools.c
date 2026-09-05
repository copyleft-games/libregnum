/* test-mcp-tools.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for MCP tool groups.
 */

#include <glib.h>
#include <glib/gstdio.h>
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

/* Calls tools through their public provider interface and parses real results. */
static JsonObject *
call_json (LrgMcpToolProvider *provider,
           const gchar *tool,
           const gchar *arguments)
{
    g_autoptr(JsonParser) parser = json_parser_new ();
    g_autoptr(JsonParser) output = json_parser_new ();
    g_autoptr(McpToolResult) result = NULL;
    g_autoptr(GError) error = NULL;
    JsonObject *text;

    g_assert_true (json_parser_load_from_data (parser, arguments, -1, &error));
    result = lrg_mcp_tool_provider_call_tool (provider, tool,
                                             json_node_get_object (json_parser_get_root (parser)), &error);
    g_assert_no_error (error);
    g_assert_nonnull (result);
    g_assert_false (mcp_tool_result_get_is_error (result));
    text = json_array_get_object_element (mcp_tool_result_get_content (result), 0);
    g_assert_true (json_parser_load_from_data (output, json_object_get_string_member (text, "text"), -1, &error));
    g_assert_no_error (error);
    return json_object_ref (json_node_get_object (json_parser_get_root (output)));
}

static void
call_error (LrgMcpToolProvider *provider,
            const gchar *tool,
            const gchar *arguments,
            GQuark domain,
            gint code)
{
    g_autoptr(JsonParser) parser = json_parser_new ();
    g_autoptr(McpToolResult) result = NULL;
    g_autoptr(GError) error = NULL;

    g_assert_true (json_parser_load_from_data (parser, arguments, -1, &error));
    result = lrg_mcp_tool_provider_call_tool (provider, tool,
                                             json_node_get_object (json_parser_get_root (parser)), &error);
    g_assert_null (result);
    g_assert_error (error, domain, code);
}

static void
test_mcp_ecs_empty_and_invalid (void)
{
    g_autoptr(LrgMcpEcsTools) tools = lrg_mcp_ecs_tools_new ();
    LrgMcpToolProvider *provider = LRG_MCP_TOOL_PROVIDER (tools);
    g_autoptr(JsonObject) json = call_json (provider, "lrg_ecs_list_worlds", "{}");

    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json, "worlds")), ==, 0);
    g_assert_false (json_object_has_member (json, "note"));
    call_error (provider, "lrg_ecs_list_game_objects", "{}", G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    call_error (provider, "lrg_ecs_get_game_object", "{}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_ecs_get_game_object", "{\"id\":3}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_ecs_get_game_object", "{\"id\":null}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_ecs_get_game_object", "{\"id\":\"\"}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_ecs_get_game_object", "{\"id\":\"missing\"}", G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    call_error (provider, "lrg_ecs_list_worlds", "{\"typo\":true}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_mcp_ecs_live_operations (void)
{
    LrgEngine *engine = lrg_engine_get_default ();
    g_autoptr(LrgWorld) world = lrg_world_new ();
    g_autoptr(LrgWorld) shared = lrg_world_new ();
    g_autoptr(LrgGameObject) object = lrg_game_object_new_at (10, 20);
    g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new_at (10, 20);
    g_autoptr(LrgMcpEcsTools) tools = lrg_mcp_ecs_tools_new ();
    LrgMcpToolProvider *provider = LRG_MCP_TOOL_PROVIDER (tools);
    g_autoptr(JsonObject) json = NULL;
    g_autofree gchar *id = NULL;
    g_autofree gchar *args = NULL;
    g_autoptr(GrlVector2) scale = NULL;
    JsonObject *entry;

    g_assert_true (lrg_engine_register_world (engine, "main", world));
    lrg_game_object_add_component (object, LRG_COMPONENT (transform));
    lrg_world_add_object (world, object);
    json = call_json (provider, "lrg_ecs_list_worlds", "{}");
    entry = json_array_get_object_element (json_object_get_array_member (json, "worlds"), 0);
    g_assert_cmpstr (json_object_get_string_member (entry, "name"), ==, "main");
    g_assert_cmpint (json_object_get_int_member (entry, "object_count"), ==, 1);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_list_game_objects", "{}");
    entry = json_array_get_object_element (json_object_get_array_member (json, "objects"), 0);
    id = g_strdup (json_object_get_string_member (entry, "id"));
    g_assert_true (g_uuid_string_is_valid (id));
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (entry, "components")), ==, 1);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"x\":42,\"rotation\":30,\"scale_x\":2,\"scale_y\":3}", id);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_set_transform", args);
    g_assert_cmpfloat (lrg_transform_component_get_local_x (transform), ==, 42);
    g_assert_cmpfloat (lrg_transform_component_get_local_y (transform), ==, 20);
    g_assert_cmpfloat (grl_entity_get_x (GRL_ENTITY (object)), ==, 42);
    scale = lrg_transform_component_get_local_scale (transform);
    g_assert_cmpfloat (grl_vector2_get_x (scale), ==, 2);
    g_assert_cmpfloat (grl_vector2_get_y (scale), ==, 3);
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"x\":99,\"y\":\"bad\"}", id);
    call_error (provider, "lrg_ecs_set_transform", args, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_cmpfloat (lrg_transform_component_get_local_x (transform), ==, 42);
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\"}", id);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_get_transform", args);
    g_assert_cmpfloat (json_object_get_double_member (json, "y"), ==, 20);
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"id\":\"%s\"}", id);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_get_game_object", args);
    g_assert_cmpstr (json_object_get_string_member (json, "id"), ==, id);
    g_clear_pointer (&json, json_object_unref);
    g_assert_true (lrg_engine_register_world (engine, "shared", shared));
    lrg_world_add_object (shared, object);
    json = call_json (provider, "lrg_ecs_destroy_object", args);
    g_assert_cmpuint (lrg_world_get_object_count (shared), ==, 0);
    g_assert_true (lrg_engine_unregister_world (engine, "shared"));
    g_assert_cmpuint (lrg_world_get_object_count (world), ==, 0);
    call_error (provider, "lrg_ecs_destroy_object", args, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_true (lrg_engine_unregister_world (engine, "main"));
}

#define TEST_TYPE_MCP_ENTITY (test_mcp_entity_get_type ())
G_DECLARE_FINAL_TYPE (TestMcpEntity, test_mcp_entity, TEST, MCP_ENTITY, LrgGameObject)
struct _TestMcpEntity
{
    LrgGameObject parent_instance;
};
G_DEFINE_TYPE (TestMcpEntity, test_mcp_entity, LRG_TYPE_GAME_OBJECT)
static void
test_mcp_entity_init (TestMcpEntity *self)
{
    g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new ();
    lrg_game_object_add_component (LRG_GAME_OBJECT (self), LRG_COMPONENT (transform));
}
static void
test_mcp_entity_class_init (TestMcpEntityClass *klass)
{
}

static void
test_mcp_ecs_spawn_with_transform (void)
{
    LrgEngine *engine = lrg_engine_get_default ();
    g_autoptr(LrgWorld) world = lrg_world_new ();
    g_autoptr(LrgMcpEcsTools) tools = lrg_mcp_ecs_tools_new ();
    g_autoptr(JsonObject) json = NULL;
    g_autoptr(GList) objects = NULL;
    LrgTransformComponent *transform;

    g_assert_true (lrg_engine_startup (engine, NULL));
    g_assert_true (lrg_engine_register_world (engine, "with-transform", world));
    lrg_registry_register (lrg_engine_get_registry (engine), "mcp-entity", TEST_TYPE_MCP_ENTITY);
    json = call_json (LRG_MCP_TOOL_PROVIDER (tools), "lrg_ecs_spawn_object", "{\"type\":\"mcp-entity\",\"x\":11,\"y\":22}");
    objects = lrg_world_get_objects (world);
    transform = LRG_TRANSFORM_COMPONENT (lrg_game_object_get_component (objects->data, LRG_TYPE_TRANSFORM_COMPONENT));
    g_assert_cmpfloat (lrg_transform_component_get_local_x (transform), ==, 11);
    g_assert_cmpfloat (lrg_transform_component_get_local_y (transform), ==, 22);
    lrg_transform_component_sync_to_entity (transform);
    g_assert_cmpfloat (grl_entity_get_x (GRL_ENTITY (objects->data)), ==, 11);
    g_assert_true (lrg_engine_unregister_world (engine, "with-transform"));
    lrg_engine_shutdown (engine);
}

static void
test_mcp_ecs_spawn_and_components (void)
{
    LrgEngine *engine = lrg_engine_get_default ();
    g_autoptr(LrgWorld) world = lrg_world_new ();
    g_autoptr(LrgMcpEcsTools) tools = lrg_mcp_ecs_tools_new ();
    LrgMcpToolProvider *provider = LRG_MCP_TOOL_PROVIDER (tools);
    g_autoptr(JsonObject) json = NULL;
    g_autofree gchar *id = NULL;
    g_autofree gchar *args = NULL;
    g_autoptr(GList) objects = NULL;
    g_autoptr(LrgTransformComponent) component = lrg_transform_component_new ();
    LrgGameObject *object;

    g_assert_true (lrg_engine_startup (engine, NULL));
    g_assert_true (lrg_engine_register_world (engine, "spawn", world));
    lrg_registry_register (lrg_engine_get_registry (engine), "mcp-test-object", LRG_TYPE_GAME_OBJECT);
    lrg_registry_register (lrg_engine_get_registry (engine), "mcp-bad-object", LRG_TYPE_WORLD);
    call_error (provider, "lrg_ecs_spawn_object", "{\"type\":\"mcp-bad-object\"}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_ecs_spawn_object", "{\"type\":\"missing\"}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    json = call_json (provider, "lrg_ecs_spawn_object", "{\"type\":\"mcp-test-object\",\"x\":12,\"y\":15}");
    id = g_strdup (json_object_get_string_member (json, "id"));
    objects = lrg_world_get_objects (world);
    object = objects->data;
    g_assert_cmpfloat (grl_entity_get_x (GRL_ENTITY (object)), ==, 12);
    g_assert_cmpfloat (grl_entity_get_y (GRL_ENTITY (object)), ==, 15);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"x\":99,\"scale_x\":2}", id);
    call_error (provider, "lrg_ecs_set_transform", args, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_assert_cmpfloat (grl_entity_get_x (GRL_ENTITY (object)), ==, 12);
    lrg_game_object_add_component (object, LRG_COMPONENT (component));
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"type\":\"LrgTransformComponent\"}", id);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_get_component", args);
    g_assert_true (json_object_get_boolean_member (json_object_get_object_member (json, "properties"), "enabled"));
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"type\":\"LrgTransformComponent\",\"property\":\"enabled\",\"value\":false}", id);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_ecs_set_component_property", args);
    g_assert_false (lrg_component_get_enabled (LRG_COMPONENT (component)));
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"type\":\"LrgTransformComponent\",\"property\":\"enabled\",\"value\":1}", id);
    call_error (provider, "lrg_ecs_set_component_property", args, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_false (lrg_component_get_enabled (LRG_COMPONENT (component)));
    g_clear_pointer (&args, g_free);
    args = g_strdup_printf ("{\"object_id\":\"%s\",\"type\":\"LrgTransformComponent\",\"property\":\"owner\",\"value\":null}", id);
    call_error (provider, "lrg_ecs_set_component_property", args, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    {
        const gchar *properties[] = { "missing", "local-x", "parent" };
        const gchar *values[] = { "1", "1e40", "null" };
        gint codes[] = { G_IO_ERROR_INVALID_ARGUMENT, G_IO_ERROR_INVALID_ARGUMENT, G_IO_ERROR_NOT_SUPPORTED };
        guint i;
        for (i = 0; i < G_N_ELEMENTS (properties); i++)
        {
            g_clear_pointer (&args, g_free);
            args = g_strdup_printf ("{\"object_id\":\"%s\",\"type\":\"LrgTransformComponent\",\"property\":\"%s\",\"value\":%s}", id, properties[i], values[i]);
            call_error (provider, "lrg_ecs_set_component_property", args, G_IO_ERROR, codes[i]);
        }
    }
    g_assert_true (lrg_engine_unregister_world (engine, "spawn"));
    lrg_engine_shutdown (engine);
}

/* A real Saveable proves that load changes live state, beyond file existence. */
#define TEST_TYPE_MCP_STATE (test_mcp_state_get_type ())
G_DECLARE_FINAL_TYPE (TestMcpState, test_mcp_state, TEST, MCP_STATE, GObject)
struct _TestMcpState
{
    GObject parent_instance;
    gint score;
};
static void test_mcp_state_saveable_init (LrgSaveableInterface *iface);
G_DEFINE_TYPE_WITH_CODE (TestMcpState, test_mcp_state, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE, test_mcp_state_saveable_init))
static const gchar *
test_mcp_state_id (LrgSaveable *self)
{
    return "mcp-state";
}
static gboolean
test_mcp_state_save (LrgSaveable *self, LrgSaveContext *context, GError **error)
{
    lrg_save_context_write_int (context, "score", TEST_MCP_STATE (self)->score);
    return TRUE;
}
static gboolean
test_mcp_state_load (LrgSaveable *self, LrgSaveContext *context, GError **error)
{
    TEST_MCP_STATE (self)->score = lrg_save_context_read_int (context, "score", 0);
    return TRUE;
}
static void
test_mcp_state_saveable_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = test_mcp_state_id;
    iface->save = test_mcp_state_save;
    iface->load = test_mcp_state_load;
}
static void
test_mcp_state_init (TestMcpState *self)
{
}
static void
test_mcp_state_class_init (TestMcpStateClass *klass)
{
}

static void
test_mcp_save_roundtrip (void)
{
    LrgSaveManager *manager = lrg_save_manager_get_default ();
    g_autoptr(LrgMcpSaveTools) tools = lrg_mcp_save_tools_new ();
    LrgMcpToolProvider *provider = LRG_MCP_TOOL_PROVIDER (tools);
    g_autoptr(TestMcpState) state = g_object_new (TEST_TYPE_MCP_STATE, NULL);
    g_autofree gchar *previous = g_strdup (lrg_save_manager_get_save_directory (manager));
    g_autofree gchar *directory = g_dir_make_tmp ("lrg-mcp-save-XXXXXX", NULL);
    g_autoptr(JsonObject) json = NULL;

    g_assert_nonnull (directory);
    lrg_save_manager_set_save_directory (manager, directory);
    lrg_save_manager_register (manager, LRG_SAVEABLE (state));
    json = call_json (provider, "lrg_save_list_slots", "{}");
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json, "slots")), ==, 0);
    g_assert_false (json_object_has_member (json, "note"));
    call_error (provider, "lrg_save_get_info", "{\"slot\":\"absent\"}", G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    call_error (provider, "lrg_save_create", "{\"slot\":\"../outside\"}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_save_load", "{\"slot\":12}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    state->score = 42;
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_create", "{\"slot\":\"manual\",\"description\":\"Before boss\"}");
    g_assert_true (lrg_save_manager_slot_exists (manager, "manual"));
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_get_info", "{\"slot\":\"manual\"}");
    g_assert_cmpstr (json_object_get_string_member (json, "description"), ==, "Before boss");
    g_assert_nonnull (json_object_get_string_member (json, "timestamp"));
    state->score = 0;
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_load", "{\"slot\":\"manual\"}");
    g_assert_cmpint (state->score, ==, 42);
    state->score = 73;
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_quick_save", "{}");
    state->score = 9;
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_quick_load", "{}");
    g_assert_cmpint (state->score, ==, 73);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_list_slots", "{}");
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json, "slots")), ==, 2);
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_delete", "{\"slot\":\"manual\"}");
    g_assert_false (lrg_save_manager_slot_exists (manager, "manual"));
    g_clear_pointer (&json, json_object_unref);
    json = call_json (provider, "lrg_save_delete", "{\"slot\":\"quicksave\"}");
    lrg_save_manager_unregister (manager, LRG_SAVEABLE (state));
    lrg_save_manager_set_save_directory (manager, previous);
    g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
test_mcp_save_errors (void)
{
    LrgSaveManager *manager = lrg_save_manager_get_default ();
    g_autoptr(LrgMcpSaveTools) tools = lrg_mcp_save_tools_new ();
    LrgMcpToolProvider *provider = LRG_MCP_TOOL_PROVIDER (tools);
    g_autofree gchar *previous = g_strdup (lrg_save_manager_get_save_directory (manager));
    g_autofree gchar *directory = g_dir_make_tmp ("lrg-mcp-errors-XXXXXX", NULL);
    g_autofree gchar *path = g_build_filename (directory, "broken.yaml", NULL);
    g_autofree gchar *blocker = g_build_filename (directory, "not-a-directory", NULL);
    g_autoptr(McpToolResult) result = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(JsonObject) args = json_object_new ();

    lrg_save_manager_set_save_directory (manager, directory);
    call_error (provider, "lrg_save_delete", "{\"slot\":\"absent\"}", LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND);
    call_error (provider, "lrg_save_load", "{\"slot\":\"absent\"}", LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND);
    call_error (provider, "lrg_save_quick_load", "{}", LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND);
    call_error (provider, "lrg_save_create", "{\"slot\":\"test\",\"description\":false}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    call_error (provider, "lrg_save_create", "{\"slot\":\"a\\\\b\"}", G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_true (g_file_set_contents (path, "invalid: [", -1, NULL));
    call_error (provider, "lrg_save_get_info", "{\"slot\":\"broken\"}", G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_assert_true (g_file_set_contents (blocker, "block", -1, NULL));
    lrg_save_manager_set_save_directory (manager, blocker);
    result = lrg_mcp_tool_provider_call_tool (provider, "lrg_save_quick_save", args, &error);
    g_assert_null (result);
    g_assert_nonnull (error);
    g_clear_error (&error);
    result = lrg_mcp_tool_provider_call_tool (provider, "lrg_save_list_slots", args, &error);
    g_assert_null (result);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
    lrg_save_manager_set_save_directory (manager, previous);
    g_assert_cmpint (g_remove (path), ==, 0);
    g_assert_cmpint (g_remove (blocker), ==, 0);
    g_assert_cmpint (g_rmdir (directory), ==, 0);
}

static void
test_mcp_tool_schema_lifecycle (void)
{
    g_autoptr(LrgMcpInputTools) input = lrg_mcp_input_tools_new ();
    g_autoptr(LrgMcpScreenshotTools) screenshot = lrg_mcp_screenshot_tools_new ();
    g_autoptr(LrgMcpDebugTools) debug = lrg_mcp_debug_tools_new ();
    g_autoptr(LrgMcpReelTools) reel = lrg_mcp_reel_tools_new ();
    g_autoptr(LrgMcpEcsTools) ecs = lrg_mcp_ecs_tools_new ();
    g_autoptr(LrgMcpSaveTools) save = lrg_mcp_save_tools_new ();
    LrgMcpToolProvider *providers[] = { LRG_MCP_TOOL_PROVIDER (ecs), LRG_MCP_TOOL_PROVIDER (save) };
    guint expected[] = { 9, 7 };
    guint i;

    /* Every advertised ECS/save operation has a strict object schema.
     * Destruction also lets LeakSanitizer observe schema-root ownership. */
    for (i = 0; i < G_N_ELEMENTS (providers); i++)
    {
        GList *tools = lrg_mcp_tool_provider_list_tools (providers[i]);
        GList *iter;
        g_assert_cmpuint (g_list_length (tools), ==, expected[i]);
        for (iter = tools; iter != NULL; iter = iter->next)
        {
            JsonNode *schema = mcp_tool_get_input_schema (iter->data);
            JsonObject *object;
            g_assert_true (JSON_NODE_HOLDS_OBJECT (schema));
            object = json_node_get_object (schema);
            g_assert_cmpstr (json_object_get_string_member (object, "type"), ==, "object");
            g_assert_false (json_object_get_boolean_member (object, "additionalProperties"));
            g_assert_nonnull (json_object_get_object_member (object, "properties"));
            g_assert_nonnull (json_object_get_array_member (object, "required"));
        }
        g_list_free_full (tools, g_object_unref);
    }
}


int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/mcp/tools/schema-lifecycle", test_mcp_tool_schema_lifecycle);
    g_test_add_func ("/mcp/tools/ecs/empty-and-invalid", test_mcp_ecs_empty_and_invalid);
    g_test_add_func ("/mcp/tools/ecs/live-operations", test_mcp_ecs_live_operations);
    g_test_add_func ("/mcp/tools/ecs/spawn-and-components", test_mcp_ecs_spawn_and_components);
    g_test_add_func ("/mcp/tools/ecs/spawn-with-transform", test_mcp_ecs_spawn_with_transform);
    g_test_add_func ("/mcp/tools/save/roundtrip", test_mcp_save_roundtrip);
    g_test_add_func ("/mcp/tools/save/errors", test_mcp_save_errors);


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
