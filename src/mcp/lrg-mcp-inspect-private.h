/* Internal MCP inspection helpers. SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>
#include "lrg-mcp-tool-group.h"
#include "../core/lrg-engine.h"
#include "../ecs/lrg-world.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/components/lrg-transform-component.h"

gboolean _lrg_mcp_args (JsonObject *args, const gchar *strings,
                         const gchar *numbers, const gchar *required,
                         GError **error);
McpToolResult *_lrg_mcp_result (JsonObject *object);
void _lrg_mcp_add_tool (LrgMcpToolGroup *group, const gchar *name,
                        const gchar *description, const gchar *strings,
                        const gchar *numbers, const gchar *required);
LrgWorld *_lrg_mcp_world (const gchar *name, GError **error);
const gchar *_lrg_mcp_object_id (LrgGameObject *object);
LrgGameObject *_lrg_mcp_find_object (const gchar *id, LrgWorld **world, GError **error);
JsonObject *_lrg_mcp_worlds_json (void);
JsonObject *_lrg_mcp_world_json (const gchar *name, LrgWorld *world);
JsonObject *_lrg_mcp_object_json (LrgGameObject *object);
JsonObject *_lrg_mcp_transform_json (LrgGameObject *object);
JsonObject *_lrg_mcp_component_json (LrgComponent *component);
gboolean _lrg_mcp_set_property (GObject *object, const gchar *name,
                                JsonNode *node, GError **error);
