/* lrg-mcp-ecs-tools.c - live ECS inspection and editing.
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "lrg-mcp-ecs-tools.h"
#include "../lrg-mcp-inspect-private.h"
#include "../../core/lrg-registry.h"

struct _LrgMcpEcsTools
{
    LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEcsTools, lrg_mcp_ecs_tools, LRG_TYPE_MCP_TOOL_GROUP)

static const gchar *
string_arg (JsonObject *args,
            const gchar *name)
{
    return args != NULL && json_object_has_member (args, name) ?
           json_object_get_string_member (args, name) : NULL;
}

static McpToolResult *
set_transform (LrgGameObject *object,
               JsonObject *args,
               GError **error)
{
    g_autoptr(JsonObject) current = _lrg_mcp_transform_json (object);
    g_autoptr(LrgTransformComponent) transform = NULL;
    GrlEntity *entity = GRL_ENTITY (object);
    const gchar *fields[] = { "x", "y", "rotation", "scale_x", "scale_y" };
    gdouble values[5];
    guint i;

    for (i = 0; i < G_N_ELEMENTS (fields); i++)
        values[i] = json_object_get_double_member (
            json_object_has_member (args, fields[i]) ? args : current, fields[i]);
    transform = (LrgTransformComponent *) lrg_game_object_get_component (object, LRG_TYPE_TRANSFORM_COMPONENT);
    if (transform != NULL)
        g_object_ref (transform);
    else if (values[3] != values[4])
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "Nonuniform scale requires an LrgTransformComponent");
        return NULL;
    }
    /* Hold references across property notifications that may detach objects. */
    g_object_ref (object);
    if (transform != NULL)
    {
        lrg_transform_component_set_local_position_xy (transform, values[0], values[1]);
        lrg_transform_component_set_local_rotation (transform, values[2]);
        lrg_transform_component_set_local_scale_xy (transform, values[3], values[4]);
        lrg_transform_component_sync_to_entity (transform);
    }
    else
    {
        grl_entity_set_position_xy (entity, values[0], values[1]);
        grl_entity_set_rotation (entity, values[2]);
        grl_entity_set_scale (entity, values[3]);
    }
    g_clear_pointer (&current, json_object_unref);
    current = _lrg_mcp_transform_json (object);
    g_object_unref (object);
    return _lrg_mcp_result (g_steal_pointer (&current));
}

static McpToolResult *
spawn_object (JsonObject *args,
              GError **error)
{
    LrgEngine *engine = lrg_engine_get_default ();
    LrgRegistry *registry = lrg_engine_get_registry (engine);
    g_autoptr(LrgWorld) world = NULL;
    g_autoptr(LrgGameObject) object = NULL;
    GType type;
    JsonObject *json;

    world = _lrg_mcp_world (string_arg (args, "world"), error);
    if (world == NULL)
        return NULL;
    g_object_ref (world);
    if (registry == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "Engine registry is not initialized");
        return NULL;
    }
    type = lrg_registry_lookup (registry, string_arg (args, "type"));
    if (!g_type_is_a (type, LRG_TYPE_GAME_OBJECT) || G_TYPE_IS_ABSTRACT (type))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Registered type must be a concrete LrgGameObject");
        return NULL;
    }
    object = g_object_new (type, NULL);
    {
        g_autoptr(McpToolResult) positioned = set_transform (object, args, error);
        if (positioned == NULL)
            return NULL;
    }
    lrg_world_add_object (world, object);
    json = _lrg_mcp_object_json (object);
    json_object_set_boolean_member (json, "success", TRUE);
    return _lrg_mcp_result (json);
}

static McpToolResult *
lrg_mcp_ecs_tools_handle_tool (LrgMcpToolGroup *group,
                               const gchar *name,
                               JsonObject *args,
                               GError **error)
{
    LrgGameObject *object;
    LrgWorld *world = NULL;
    const gchar *id_field;

    if (g_str_equal (name, "lrg_ecs_list_worlds"))
    {
        if (!_lrg_mcp_args (args, "", "", "", error))
            return NULL;
        return _lrg_mcp_result (_lrg_mcp_worlds_json ());
    }
    if (g_str_equal (name, "lrg_ecs_list_game_objects"))
    {
        if (!_lrg_mcp_args (args, "world", "", "", error))
            return NULL;
        world = _lrg_mcp_world (string_arg (args, "world"), error);
        return world == NULL ? NULL : _lrg_mcp_result (_lrg_mcp_world_json (string_arg (args, "world"), world));
    }
    if (g_str_equal (name, "lrg_ecs_spawn_object"))
    {
        if (!_lrg_mcp_args (args, "type world", "x y", "type", error))
            return NULL;
        return spawn_object (args, error);
    }
    if (g_str_equal (name, "lrg_ecs_get_component") ||
        g_str_equal (name, "lrg_ecs_set_component_property"))
    {
        gboolean set = g_str_equal (name, "lrg_ecs_set_component_property");
        g_autoptr(LrgComponent) component = NULL;
        GType type;

        if (!_lrg_mcp_args (args, set ? "object_id type property" : "object_id type", "",
                           set ? "object_id type property value" : "object_id type", error))
            return NULL;
        object = _lrg_mcp_find_object (string_arg (args, "object_id"), NULL, error);
        if (object == NULL)
            return NULL;
        type = g_type_from_name (string_arg (args, "type"));
        if (!g_type_is_a (type, LRG_TYPE_COMPONENT))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown component type");
            return NULL;
        }
        component = lrg_game_object_get_component (object, type);
        if (component == NULL)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Object has no matching component");
            return NULL;
        }
        g_object_ref (component);
        if (set && !_lrg_mcp_set_property (G_OBJECT (component), string_arg (args, "property"),
                                           json_object_get_member (args, "value"), error))
            return NULL;
        return _lrg_mcp_result (_lrg_mcp_component_json (component));
    }
    if (g_str_equal (name, "lrg_ecs_get_game_object") ||
        g_str_equal (name, "lrg_ecs_destroy_object"))
        id_field = "id";
    else if (g_str_equal (name, "lrg_ecs_get_transform") ||
             g_str_equal (name, "lrg_ecs_set_transform"))
        id_field = "object_id";
    else
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unknown tool: %s", name);
        return NULL;
    }
    if (!_lrg_mcp_args (args, id_field,
                       g_str_equal (name, "lrg_ecs_set_transform") ? "x y rotation scale_x scale_y" : "",
                       id_field, error))
        return NULL;
    object = _lrg_mcp_find_object (string_arg (args, id_field), &world, error);
    if (object == NULL)
        return NULL;
    if (g_str_equal (name, "lrg_ecs_get_game_object"))
        return _lrg_mcp_result (_lrg_mcp_object_json (object));
    if (g_str_equal (name, "lrg_ecs_get_transform"))
        return _lrg_mcp_result (_lrg_mcp_transform_json (object));
    if (g_str_equal (name, "lrg_ecs_set_transform"))
        return set_transform (object, args, error);
    {
        JsonObject *json = json_object_new ();
        LrgEngine *engine = lrg_engine_get_default ();
        GList *names = lrg_engine_list_worlds (engine);
        GList *iter;
        g_autoptr(GPtrArray) worlds = g_ptr_array_new_with_free_func (g_object_unref);
        guint i;

        json_object_set_string_member (json, "id", _lrg_mcp_object_id (object));
        json_object_set_boolean_member (json, "success", TRUE);
        /* One object may belong to several registered worlds. */
        for (iter = names; iter != NULL; iter = iter->next)
            g_ptr_array_add (worlds, g_object_ref (lrg_engine_get_world (engine, iter->data)));
        g_list_free_full (names, g_free);
        g_object_ref (object);
        for (i = 0; i < worlds->len; i++)
        {
            LrgWorld *registered = g_ptr_array_index (worlds, i);
            g_autoptr(GList) objects = lrg_world_get_objects (registered);
            if (g_list_find (objects, object) != NULL)
                lrg_world_remove_object (registered, object);
        }
        g_object_unref (object);
        return _lrg_mcp_result (json);
    }
}

static const gchar *
lrg_mcp_ecs_tools_get_group_name (LrgMcpToolGroup *group)
{
    return "ecs";
}

static void
lrg_mcp_ecs_tools_register_tools (LrgMcpToolGroup *group)
{
    _lrg_mcp_add_tool (group, "lrg_ecs_list_worlds", "List registered worlds", "", "", "");
    _lrg_mcp_add_tool (group, "lrg_ecs_list_game_objects", "List objects; defaults to first active world", "world", "", "");
    _lrg_mcp_add_tool (group, "lrg_ecs_get_game_object", "Inspect an object by its returned ID", "id", "", "id");
    _lrg_mcp_add_tool (group, "lrg_ecs_destroy_object", "Remove an object from its world", "id", "", "id");
    _lrg_mcp_add_tool (group, "lrg_ecs_get_transform", "Read local component or entity transform", "object_id", "", "object_id");
    _lrg_mcp_add_tool (group, "lrg_ecs_set_transform", "Edit transform; nonuniform scale requires a transform component", "object_id", "x y rotation scale_x scale_y", "object_id");
    _lrg_mcp_add_tool (group, "lrg_ecs_get_component", "Read scalar component properties by GType name", "object_id type", "", "object_id type");
    _lrg_mcp_add_tool (group, "lrg_ecs_set_component_property", "Set a writable scalar component property", "object_id type property", "", "object_id type property value");
    _lrg_mcp_add_tool (group, "lrg_ecs_spawn_object", "Spawn a registered game object type", "type world", "x y", "type");
}

static void
lrg_mcp_ecs_tools_class_init (LrgMcpEcsToolsClass *klass)
{
    LrgMcpToolGroupClass *group = LRG_MCP_TOOL_GROUP_CLASS (klass);
    group->get_group_name = lrg_mcp_ecs_tools_get_group_name;
    group->register_tools = lrg_mcp_ecs_tools_register_tools;
    group->handle_tool = lrg_mcp_ecs_tools_handle_tool;
}

static void
lrg_mcp_ecs_tools_init (LrgMcpEcsTools *self)
{
}

/**
 * lrg_mcp_ecs_tools_new:
 *
 * Returns: (transfer full): a new ECS tools provider
 */
LrgMcpEcsTools *
lrg_mcp_ecs_tools_new (void)
{
    return g_object_new (LRG_TYPE_MCP_ECS_TOOLS, NULL);
}
