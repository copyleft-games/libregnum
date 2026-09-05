/* Internal MCP inspection helpers. SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mcp-tool-group.h"
#include "lrg-mcp-inspect-private.h"
#include <math.h>
#include <string.h>

static gboolean
has_word (const gchar *words,
          const gchar *word)
{
    g_auto(GStrv) split = g_strsplit (words, " ", -1);
    return g_strv_contains ((const gchar * const *) split, word);
}

gboolean
_lrg_mcp_args (JsonObject *args,
               const gchar *strings,
               const gchar *numbers,
               const gchar *required,
               GError **error)
{
    g_auto(GStrv) names = g_strsplit (required, " ", -1);
    g_autoptr(GList) members = NULL;
    GList *iter;
    guint i;

    for (i = 0; names[i] != NULL; i++)
    {
        if (*names[i] != '\0' && (args == NULL || !json_object_has_member (args, names[i])))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Missing required argument: %s", names[i]);
            return FALSE;
        }
    }
    if (args == NULL)
        return TRUE;
    members = json_object_get_members (args);
    for (iter = members; iter != NULL; iter = iter->next)
    {
        const gchar *name = iter->data;
        JsonNode *node = json_object_get_member (args, name);
        GType type = json_node_get_value_type (node);
        gboolean valid;

        valid = FALSE;
        if (has_word (strings, name))
            valid = type == G_TYPE_STRING && json_node_get_string (node) != NULL;
        else if (has_word (numbers, name))
        {
            if (type == G_TYPE_DOUBLE || type == G_TYPE_INT64)
            {
                gdouble value = json_node_get_double (node);
                valid = isfinite (value) && fabs (value) <= G_MAXFLOAT;
            }
        }
        else if (g_str_equal (name, "value") && has_word (required, "value"))
            valid = TRUE;
        if (!valid)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Unknown argument or invalid value: %s", name);
            return FALSE;
        }
        if (has_word (required, name) && type == G_TYPE_STRING &&
            *json_node_get_string (node) == '\0')
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "Argument must not be empty: %s", name);
            return FALSE;
        }
    }
    return TRUE;
}

McpToolResult *
_lrg_mcp_result (JsonObject *object)
{
    g_autoptr(JsonNode) node = json_node_new (JSON_NODE_OBJECT);
    g_autofree gchar *text = NULL;
    McpToolResult *result;

    json_node_take_object (node, object);
    text = json_to_string (node, FALSE);
    result = mcp_tool_result_new (FALSE);
    mcp_tool_result_add_text (result, text);
    return result;
}

void
_lrg_mcp_add_tool (LrgMcpToolGroup *group,
                   const gchar *name,
                   const gchar *description,
                   const gchar *strings,
                   const gchar *numbers,
                   const gchar *required)
{
    g_autoptr(JsonNode) schema = json_node_new (JSON_NODE_OBJECT);
    JsonObject *object = json_object_new ();
    JsonObject *properties = json_object_new ();
    JsonArray *mandatory = json_array_new ();
    g_autofree gchar *combined = g_strconcat (strings, " ", numbers, NULL);
    g_auto(GStrv) fields = g_strsplit (combined, " ", -1);
    g_auto(GStrv) required_fields = g_strsplit (required, " ", -1);
    McpTool *tool;
    guint i;

    json_object_set_string_member (object, "type", "object");
    json_object_set_boolean_member (object, "additionalProperties", FALSE);
    for (i = 0; fields[i] != NULL; i++)
    {
        JsonObject *property;
        if (*fields[i] == '\0')
            continue;
        property = json_object_new ();
        json_object_set_string_member (property, "type",
                                       has_word (strings, fields[i]) ? "string" : "number");
        if (has_word (strings, fields[i]) && has_word (required, fields[i]))
            json_object_set_int_member (property, "minLength", 1);
        if (has_word (numbers, fields[i]))
        {
            json_object_set_double_member (property, "minimum", -G_MAXFLOAT);
            json_object_set_double_member (property, "maximum", G_MAXFLOAT);
        }
        json_object_set_object_member (properties, fields[i], property);
    }
    if (has_word (required, "value"))
        json_object_set_object_member (properties, "value", json_object_new ());
    for (i = 0; required_fields[i] != NULL; i++)
    {
        if (*required_fields[i] != '\0')
            json_array_add_string_element (mandatory, required_fields[i]);
    }
    json_object_set_object_member (object, "properties", properties);
    json_object_set_array_member (object, "required", mandatory);
    json_node_take_object (schema, object);
    tool = mcp_tool_new (name, description);
    mcp_tool_set_input_schema (tool, schema);
    lrg_mcp_tool_group_add_tool (group, tool);
}

LrgWorld *
_lrg_mcp_world (const gchar *name,
                GError **error)
{
    LrgEngine *engine = lrg_engine_get_default ();
    LrgWorld *world = NULL;
    GList *names;
    GList *iter;

    if (name != NULL)
        world = lrg_engine_get_world (engine, name);
    else
    {
        names = lrg_engine_list_worlds (engine);
        for (iter = names; iter != NULL; iter = iter->next)
        {
            LrgWorld *candidate = lrg_engine_get_world (engine, iter->data);
            if (lrg_world_get_active (candidate))
            {
                world = candidate;
                break;
            }
        }
        g_list_free_full (names, g_free);
    }
    if (world == NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "World not found: %s", name != NULL ? name : "(first active)");
    return world;
}

const gchar *
_lrg_mcp_object_id (LrgGameObject *object)
{
    const gchar *id = g_object_get_data (G_OBJECT (object), "lrg-mcp-object-id");
    if (id == NULL)
    {
        g_object_set_data_full (G_OBJECT (object), "lrg-mcp-object-id",
                                g_uuid_string_random (), g_free);
        id = g_object_get_data (G_OBJECT (object), "lrg-mcp-object-id");
    }
    return id;
}

LrgGameObject *
_lrg_mcp_find_object (const gchar *id,
                      LrgWorld **world,
                      GError **error)
{
    LrgEngine *engine = lrg_engine_get_default ();
    GList *names = lrg_engine_list_worlds (engine);
    GList *iter;
    LrgGameObject *found = NULL;

    for (iter = names; iter != NULL && found == NULL; iter = iter->next)
    {
        LrgWorld *candidate = lrg_engine_get_world (engine, iter->data);
        g_autoptr(GList) objects = lrg_world_get_objects (candidate);
        GList *item;
        for (item = objects; item != NULL; item = item->next)
        {
            if (g_strcmp0 (id, _lrg_mcp_object_id (item->data)) == 0)
            {
                found = item->data;
                if (world != NULL)
                    *world = candidate;
                break;
            }
        }
    }
    g_list_free_full (names, g_free);
    if (found == NULL)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Object not found: %s", id);
    return found;
}

JsonObject *
_lrg_mcp_transform_json (LrgGameObject *object)
{
    JsonObject *json = json_object_new ();
    LrgTransformComponent *transform;
    GrlEntity *entity = GRL_ENTITY (object);
    gdouble x, y, rotation, scale_x, scale_y;

    transform = LRG_TRANSFORM_COMPONENT (lrg_game_object_get_component (object, LRG_TYPE_TRANSFORM_COMPONENT));
    if (transform != NULL)
    {
        g_autoptr(GrlVector2) scale = lrg_transform_component_get_local_scale (transform);
        x = lrg_transform_component_get_local_x (transform);
        y = lrg_transform_component_get_local_y (transform);
        rotation = lrg_transform_component_get_local_rotation (transform);
        scale_x = grl_vector2_get_x (scale);
        scale_y = grl_vector2_get_y (scale);
    }
    else
    {
        x = grl_entity_get_x (entity);
        y = grl_entity_get_y (entity);
        rotation = grl_entity_get_rotation (entity);
        scale_x = scale_y = grl_entity_get_scale (entity);
    }
    json_object_set_double_member (json, "x", x);
    json_object_set_double_member (json, "y", y);
    json_object_set_double_member (json, "rotation", rotation);
    json_object_set_double_member (json, "scale_x", scale_x);
    json_object_set_double_member (json, "scale_y", scale_y);
    json_object_set_string_member (json, "space", transform != NULL ? "local" : "entity");
    return json;
}

JsonObject *
_lrg_mcp_object_json (LrgGameObject *object)
{
    JsonObject *json = json_object_new ();
    JsonArray *array = json_array_new ();
    g_autoptr(GList) components = lrg_game_object_get_components (object);
    GList *iter;
    const gchar *tag = grl_entity_get_tag (GRL_ENTITY (object));

    json_object_set_string_member (json, "id", _lrg_mcp_object_id (object));
    json_object_set_string_member (json, "name", tag != NULL ? tag : G_OBJECT_TYPE_NAME (object));
    json_object_set_string_member (json, "type", G_OBJECT_TYPE_NAME (object));
    json_object_set_boolean_member (json, "active", grl_entity_get_active (GRL_ENTITY (object)));
    json_object_set_object_member (json, "transform", _lrg_mcp_transform_json (object));
    for (iter = components; iter != NULL; iter = iter->next)
        json_array_add_string_element (array, G_OBJECT_TYPE_NAME (iter->data));
    json_object_set_array_member (json, "components", array);
    return json;
}

JsonObject *
_lrg_mcp_world_json (const gchar *name,
                     LrgWorld *world)
{
    JsonObject *json = json_object_new ();
    JsonArray *array = json_array_new ();
    g_autoptr(GList) objects = lrg_world_get_objects (world);
    GList *iter;

    if (name != NULL)
        json_object_set_string_member (json, "name", name);
    json_object_set_boolean_member (json, "active", lrg_world_get_active (world));
    json_object_set_boolean_member (json, "paused", lrg_world_get_paused (world));
    json_object_set_int_member (json, "object_count", lrg_world_get_object_count (world));
    for (iter = objects; iter != NULL; iter = iter->next)
        json_array_add_object_element (array, _lrg_mcp_object_json (iter->data));
    json_object_set_array_member (json, "objects", array);
    return json;
}

JsonObject *
_lrg_mcp_worlds_json (void)
{
    JsonObject *json = json_object_new ();
    JsonArray *array = json_array_new ();
    LrgEngine *engine = lrg_engine_get_default ();
    GList *names = lrg_engine_list_worlds (engine);
    GList *iter;

    for (iter = names; iter != NULL; iter = iter->next)
    {
        LrgWorld *world = lrg_engine_get_world (engine, iter->data);
        JsonObject *item = json_object_new ();
        json_object_set_string_member (item, "name", iter->data);
        json_object_set_boolean_member (item, "active", lrg_world_get_active (world));
        json_object_set_boolean_member (item, "paused", lrg_world_get_paused (world));
        json_object_set_int_member (item, "object_count", lrg_world_get_object_count (world));
        json_array_add_object_element (array, item);
    }
    g_list_free_full (names, g_free);
    json_object_set_array_member (json, "worlds", array);
    return json;
}

static gboolean
supported_value_type (GType type)
{
    return type == G_TYPE_BOOLEAN || type == G_TYPE_STRING ||
           type == G_TYPE_INT || type == G_TYPE_UINT ||
           type == G_TYPE_INT64 || type == G_TYPE_UINT64 ||
           type == G_TYPE_LONG || type == G_TYPE_ULONG ||
           type == G_TYPE_CHAR || type == G_TYPE_UCHAR ||
           type == G_TYPE_FLOAT || type == G_TYPE_DOUBLE ||
           G_TYPE_IS_ENUM (type) || G_TYPE_IS_FLAGS (type);
}

JsonObject *
_lrg_mcp_component_json (LrgComponent *component)
{
    JsonObject *json = json_object_new ();
    JsonObject *values = json_object_new ();
    JsonArray *unsupported = json_array_new ();
    g_autofree GParamSpec **specs = NULL;
    guint count, i;

    json_object_set_string_member (json, "type", G_OBJECT_TYPE_NAME (component));
    specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (component), &count);
    for (i = 0; i < count; i++)
    {
        GParamSpec *spec = specs[i];
        GValue value = G_VALUE_INIT;
        JsonNode *node;
        if (!(spec->flags & G_PARAM_READABLE))
            continue;
        if (!supported_value_type (spec->value_type))
        {
            json_array_add_string_element (unsupported, spec->name);
            continue;
        }
        g_value_init (&value, spec->value_type);
        g_object_get_property (G_OBJECT (component), spec->name, &value);
        node = json_node_new (JSON_NODE_VALUE);
        if (spec->value_type == G_TYPE_STRING)
        {
            if (g_value_get_string (&value) != NULL)
                json_node_set_string (node, g_value_get_string (&value));
            else
                json_node_init_null (node);
        }
        else if (spec->value_type == G_TYPE_BOOLEAN)
            json_node_set_boolean (node, g_value_get_boolean (&value));
        else if (G_TYPE_IS_ENUM (spec->value_type))
            json_node_set_int (node, g_value_get_enum (&value));
        else if (G_TYPE_IS_FLAGS (spec->value_type))
            json_node_set_int (node, g_value_get_flags (&value));
        else if (spec->value_type == G_TYPE_FLOAT || spec->value_type == G_TYPE_DOUBLE)
        {
            GValue number = G_VALUE_INIT;
            g_value_init (&number, G_TYPE_DOUBLE);
            g_value_transform (&value, &number);
            if (isfinite (g_value_get_double (&number)))
                json_node_set_double (node, g_value_get_double (&number));
            else
                json_node_init_null (node);
            g_value_unset (&number);
        }
        else if ((spec->value_type == G_TYPE_UINT64 && g_value_get_uint64 (&value) > G_MAXINT64) ||
                 (spec->value_type == G_TYPE_ULONG && g_value_get_ulong (&value) > G_MAXINT64))
        {
            g_autofree gchar *decimal = g_strdup_value_contents (&value);
            json_node_set_string (node, decimal);
        }
        else
        {
            GValue number = G_VALUE_INIT;
            g_value_init (&number, G_TYPE_INT64);
            g_value_transform (&value, &number);
            json_node_set_int (node, g_value_get_int64 (&number));
            g_value_unset (&number);
        }
        g_value_unset (&value);
        if (node != NULL)
            json_object_set_member (values, spec->name, node);
    }
    json_object_set_object_member (json, "properties", values);
    json_object_set_array_member (json, "unsupported_properties", unsupported);
    return json;
}

gboolean
_lrg_mcp_set_property (GObject *object,
                       const gchar *name,
                       JsonNode *node,
                       GError **error)
{
    GParamSpec *spec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
    GValue value = G_VALUE_INIT;
    GType type;
    GType input;
    gboolean valid = FALSE;

    if (spec == NULL || !(spec->flags & G_PARAM_WRITABLE) ||
        (spec->flags & G_PARAM_CONSTRUCT_ONLY))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Property is missing or not writable: %s", name);
        return FALSE;
    }
    type = spec->value_type;
    if (!supported_value_type (type))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                     "Property type is not a JSON scalar: %s", name);
        return FALSE;
    }
    input = json_node_get_value_type (node);
    g_value_init (&value, type);
    if (type == G_TYPE_STRING && (input == G_TYPE_STRING || JSON_NODE_HOLDS_NULL (node)))
    {
        g_value_set_string (&value, JSON_NODE_HOLDS_NULL (node) ? NULL : json_node_get_string (node));
        valid = TRUE;
    }
    else if (type == G_TYPE_BOOLEAN && input == G_TYPE_BOOLEAN)
    {
        g_value_set_boolean (&value, json_node_get_boolean (node));
        valid = TRUE;
    }
    else if ((type == G_TYPE_FLOAT || type == G_TYPE_DOUBLE) &&
             (input == G_TYPE_INT64 || input == G_TYPE_DOUBLE))
    {
        gdouble number = json_node_get_double (node);
        valid = isfinite (number) && (type != G_TYPE_FLOAT || fabs (number) <= G_MAXFLOAT);
        if (valid && type == G_TYPE_FLOAT)
            g_value_set_float (&value, number);
        else if (valid)
            g_value_set_double (&value, number);
    }
    else if (G_TYPE_IS_ENUM (type) && input == G_TYPE_STRING)
    {
        GEnumClass *klass = g_type_class_ref (type);
        GEnumValue *entry = g_enum_get_value_by_nick (klass, json_node_get_string (node));
        if (entry == NULL)
            entry = g_enum_get_value_by_name (klass, json_node_get_string (node));
        valid = entry != NULL;
        if (valid)
            g_value_set_enum (&value, entry->value);
        g_type_class_unref (klass);
    }
    else if (input == G_TYPE_INT64 && type != G_TYPE_BOOLEAN && type != G_TYPE_STRING)
    {
        gint64 number = json_node_get_int (node);
        valid = TRUE;
        if (type == G_TYPE_INT && number >= G_MININT && number <= G_MAXINT)
            g_value_set_int (&value, number);
        else if (type == G_TYPE_UINT && number >= 0 && number <= G_MAXUINT)
            g_value_set_uint (&value, number);
        else if (type == G_TYPE_INT64)
            g_value_set_int64 (&value, number);
        else if (type == G_TYPE_UINT64 && number >= 0)
            g_value_set_uint64 (&value, number);
        else if (type == G_TYPE_LONG && number >= G_MINLONG && number <= G_MAXLONG)
            g_value_set_long (&value, number);
        else if (type == G_TYPE_ULONG && number >= 0 && (guint64) number <= G_MAXULONG)
            g_value_set_ulong (&value, number);
        else if (type == G_TYPE_CHAR && number >= G_MININT8 && number <= G_MAXINT8)
            g_value_set_schar (&value, number);
        else if (type == G_TYPE_UCHAR && number >= 0 && number <= G_MAXUINT8)
            g_value_set_uchar (&value, number);
        else if (G_TYPE_IS_ENUM (type) && number >= G_MININT && number <= G_MAXINT)
            g_value_set_enum (&value, number);
        else if (G_TYPE_IS_FLAGS (type) && number >= 0 && number <= G_MAXUINT)
            g_value_set_flags (&value, number);
        else
            valid = FALSE;
    }
    if (valid)
        valid = !g_param_value_validate (spec, &value);
    if (valid)
        g_object_set_property (object, name, &value);
    else
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Invalid type or out-of-range value for property: %s", name);
    g_value_unset (&value);
    return valid;
}
