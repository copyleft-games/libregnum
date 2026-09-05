/* lrg-mcp-save-tools.c - SaveManager-backed MCP save operations.
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "lrg-mcp-save-tools.h"
#include "../lrg-mcp-inspect-private.h"
#include "../../save/lrg-save-manager.h"

struct _LrgMcpSaveTools
{
    LrgMcpToolGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpSaveTools, lrg_mcp_save_tools, LRG_TYPE_MCP_TOOL_GROUP)

static gboolean
valid_slot (const gchar *slot,
            GError **error)
{
    /* Slots are basenames, never paths into or out of the save directory. */
    if (*slot == '\0' || g_str_equal (slot, ".") || g_str_equal (slot, "..") ||
        strchr (slot, '/') != NULL || strchr (slot, '\\') != NULL ||
        strchr (slot, ':') != NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Save slot must be a nonempty filename without path separators");
        return FALSE;
    }
    return TRUE;
}

static JsonObject *
save_json (LrgSaveGame *save)
{
    JsonObject *json = json_object_new ();
    GDateTime *timestamp = lrg_save_game_get_timestamp (save);
    const gchar *description = lrg_save_game_get_display_name (save);
    g_autofree gchar *formatted = timestamp == NULL ? NULL : g_date_time_format_iso8601 (timestamp);

    json_object_set_string_member (json, "slot", lrg_save_game_get_slot_name (save));
    json_object_set_string_member (json, "name", lrg_save_game_get_slot_name (save));
    json_object_set_boolean_member (json, "has_save", TRUE);
    if (description != NULL)
        json_object_set_string_member (json, "description", description);
    else
        json_object_set_null_member (json, "description");
    if (formatted != NULL)
        json_object_set_string_member (json, "timestamp", formatted);
    else
        json_object_set_null_member (json, "timestamp");
    json_object_set_double_member (json, "playtime", lrg_save_game_get_playtime (save));
    json_object_set_int_member (json, "version", lrg_save_game_get_version (save));
    return json;
}

static McpToolResult *
lrg_mcp_save_tools_handle_tool (LrgMcpToolGroup *group,
                                const gchar *name,
                                JsonObject *args,
                                GError **error)
{
    LrgSaveManager *manager = lrg_save_manager_get_default ();
    const gchar *slot;
    const gchar *description = NULL;
    gboolean success;
    gboolean create = g_str_equal (name, "lrg_save_create");
    gboolean quick_save = g_str_equal (name, "lrg_save_quick_save");
    gboolean quick_load = g_str_equal (name, "lrg_save_quick_load");
    JsonObject *json;

    if (g_str_equal (name, "lrg_save_list_slots"))
    {
        GList *saves;
        GList *iter;
        JsonArray *array;
        g_autoptr(GFile) directory = NULL;
        g_autoptr(GFileEnumerator) enumerator = NULL;
        g_autoptr(GError) local_error = NULL;

        if (!_lrg_mcp_args (args, "", "", "", error))
            return NULL;
        /* Unlike an empty directory, an unreadable directory is an error. */
        directory = g_file_new_for_path (lrg_save_manager_get_save_directory (manager));
        enumerator = g_file_enumerate_children (directory, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                G_FILE_QUERY_INFO_NONE, NULL, &local_error);
        if (enumerator == NULL && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
            g_propagate_error (error, g_steal_pointer (&local_error));
            return NULL;
        }
        saves = lrg_save_manager_list_saves (manager);
        json = json_object_new ();
        array = json_array_new ();
        for (iter = saves; iter != NULL; iter = iter->next)
            json_array_add_object_element (array, save_json (iter->data));
        g_list_free_full (saves, g_object_unref);
        json_object_set_array_member (json, "slots", array);
        return _lrg_mcp_result (json);
    }
    if (!create && !quick_save && !quick_load &&
        !g_str_equal (name, "lrg_save_get_info") &&
        !g_str_equal (name, "lrg_save_load") &&
        !g_str_equal (name, "lrg_save_delete"))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unknown tool: %s", name);
        return NULL;
    }
    if (!_lrg_mcp_args (args, quick_save || quick_load ? "" : create ? "slot description" : "slot",
                        "", quick_save || quick_load ? "" : "slot", error))
        return NULL;
    slot = quick_save || quick_load ? "quicksave" : json_object_get_string_member (args, "slot");
    if (!valid_slot (slot, error))
        return NULL;
    if (create && json_object_has_member (args, "description"))
        description = json_object_get_string_member (args, "description");
    if (g_str_equal (name, "lrg_save_get_info"))
    {
        g_autoptr(LrgSaveGame) save = lrg_save_manager_get_save (manager, slot);
        if (save == NULL)
        {
            g_set_error (error, G_IO_ERROR,
                         lrg_save_manager_slot_exists (manager, slot) ? G_IO_ERROR_INVALID_DATA : G_IO_ERROR_NOT_FOUND,
                         "Save metadata unavailable: %s", slot);
            return NULL;
        }
        return _lrg_mcp_result (save_json (save));
    }
    if (create || quick_save)
        success = lrg_save_manager_save_with_description (manager, slot, description, error);
    else if (quick_load || g_str_equal (name, "lrg_save_load"))
        success = lrg_save_manager_load (manager, slot, error);
    else
        success = lrg_save_manager_delete_save (manager, slot, error);
    if (!success)
    {
        if (error != NULL && *error == NULL)
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Save operation failed: %s", slot);
        return NULL;
    }
    json = json_object_new ();
    json_object_set_string_member (json, "slot", slot);
    json_object_set_boolean_member (json, "success", TRUE);
    return _lrg_mcp_result (json);
}

static const gchar *
lrg_mcp_save_tools_get_group_name (LrgMcpToolGroup *group)
{
    return "save";
}

static void
lrg_mcp_save_tools_register_tools (LrgMcpToolGroup *group)
{
    _lrg_mcp_add_tool (group, "lrg_save_list_slots", "List available saves", "", "", "");
    _lrg_mcp_add_tool (group, "lrg_save_get_info", "Read save metadata", "slot", "", "slot");
    _lrg_mcp_add_tool (group, "lrg_save_create", "Save registered state to a slot", "slot description", "", "slot");
    _lrg_mcp_add_tool (group, "lrg_save_load", "Load registered state from a slot", "slot", "", "slot");
    _lrg_mcp_add_tool (group, "lrg_save_delete", "Delete a save slot", "slot", "", "slot");
    _lrg_mcp_add_tool (group, "lrg_save_quick_save", "Save to the quicksave slot", "", "", "");
    _lrg_mcp_add_tool (group, "lrg_save_quick_load", "Load the quicksave slot", "", "", "");
}

static void
lrg_mcp_save_tools_class_init (LrgMcpSaveToolsClass *klass)
{
    LrgMcpToolGroupClass *group = LRG_MCP_TOOL_GROUP_CLASS (klass);
    group->get_group_name = lrg_mcp_save_tools_get_group_name;
    group->register_tools = lrg_mcp_save_tools_register_tools;
    group->handle_tool = lrg_mcp_save_tools_handle_tool;
}

static void
lrg_mcp_save_tools_init (LrgMcpSaveTools *self)
{
}

/**
 * lrg_mcp_save_tools_new:
 *
 * Returns: (transfer full): a new save tools provider
 */
LrgMcpSaveTools *
lrg_mcp_save_tools_new (void)
{
    return g_object_new (LRG_TYPE_MCP_SAVE_TOOLS, NULL);
}
